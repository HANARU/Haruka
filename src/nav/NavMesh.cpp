#include "NavMesh.hpp"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <cstring>

namespace {
constexpr int kMaxPathPolys = 256;   // 한 경로의 다각형 수 상한(넘으면 그 앞까지의 부분 경로)
constexpr int kMaxNodes     = 2048;  // 질의 노드 풀
constexpr int kMaxVisited   = 16;    // moveAlongSurface 가 지나는 다각형

glm::vec3 toVec(const float* p) { return { p[0], p[1], p[2] }; }
} // namespace

const char* navPathResultName(ENavPathResult r) {
    switch (r) {
        case ENavPathResult::Complete: return "complete";
        case ENavPathResult::Partial:  return "partial";
        default:                       return "none";
    }
}

struct NavMesh::Impl {
    dtNavMesh*      mesh  = nullptr;
    dtNavMeshQuery* query = nullptr;
    dtQueryFilter   filter;
    int             polygons = 0;

    Impl() {
        filter.setIncludeFlags(1);
        filter.setExcludeFlags(0);
    }
    ~Impl() { release(); }
    void release() {
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
        query = nullptr;
        mesh = nullptr;
        polygons = 0;
    }
    dtPolyRef nearest(const glm::vec3& p, const glm::vec3& ext, float* outPt) const {
        dtPolyRef ref = 0;
        if (!query) return 0;
        if (dtStatusFailed(query->findNearestPoly(&p.x, &ext.x, &filter, &ref, outPt))) return 0;
        return ref;
    }
};

NavMesh::NavMesh() : impl_(std::make_unique<Impl>()) {}
NavMesh::~NavMesh() = default;

void NavMesh::clear() { impl_->release(); }
bool NavMesh::valid() const { return impl_->query != nullptr && impl_->polygons > 0; }
int  NavMesh::polygonCount() const { return impl_->polygons; }

bool NavMesh::load(const std::vector<uint8_t>& tile, std::string& err) {
    impl_->release();
    if (tile.empty()) return true;
    auto* data = static_cast<unsigned char*>(dtAlloc((int)tile.size(), DT_ALLOC_PERM));
    if (!data) { err = "out of memory"; return false; }
    std::memcpy(data, tile.data(), tile.size());
    dtNavMesh* mesh = dtAllocNavMesh();
    if (!mesh) { dtFree(data); err = "out of memory"; return false; }
    // 타일 하나짜리 메시(solo). 메시가 data 를 맡아 지웁니다(DT_TILE_FREE_DATA).
    if (dtStatusFailed(mesh->init(data, (int)tile.size(), DT_TILE_FREE_DATA))) {
        dtFreeNavMesh(mesh);
        dtFree(data);
        err = "Detour rejected the tile data";
        return false;
    }
    dtNavMeshQuery* q = dtAllocNavMeshQuery();
    if (!q || dtStatusFailed(q->init(mesh, kMaxNodes))) {
        dtFreeNavMeshQuery(q);
        dtFreeNavMesh(mesh);
        err = "could not create the navigation query";
        return false;
    }
    impl_->mesh  = mesh;
    impl_->query = q;
    const dtNavMesh* cm = mesh;
    for (int i = 0; i < cm->getMaxTiles(); ++i)
        if (const dtMeshTile* t = cm->getTile(i); t && t->header) impl_->polygons += t->header->polyCount;
    return true;
}

ENavPathResult NavMesh::findPath(const glm::vec3& start, const glm::vec3& end, const glm::vec3& extents,
                                 std::vector<glm::vec3>& out) const {
    out.clear();
    if (!valid()) return ENavPathResult::NoPath;
    glm::vec3 s, e;
    const dtPolyRef sRef = impl_->nearest(start, extents, &s.x);
    const dtPolyRef eRef = impl_->nearest(end, extents, &e.x);
    if (!sRef || !eRef) return ENavPathResult::NoPath;

    dtPolyRef polys[kMaxPathPolys];
    int n = 0;
    const dtStatus st = impl_->query->findPath(sRef, eRef, &s.x, &e.x, &impl_->filter, polys, &n, kMaxPathPolys);
    if (dtStatusFailed(st) || n <= 0) return ENavPathResult::NoPath;
    ENavPathResult result = ENavPathResult::Complete;
    if (polys[n - 1] != eRef || dtStatusDetail(st, DT_PARTIAL_RESULT)) {
        // 끝에 닿지 못함: 마지막 다각형에서 끝점에 가장 가까운 점까지(D7).
        result = ENavPathResult::Partial;
        glm::vec3 c;
        bool over = false;
        if (dtStatusSucceed(impl_->query->closestPointOnPoly(polys[n - 1], &e.x, &c.x, &over))) e = c;
    }
    float pts[kMaxPathPolys * 3];
    unsigned char flags[kMaxPathPolys];
    dtPolyRef refs[kMaxPathPolys];
    int m = 0;
    if (dtStatusFailed(impl_->query->findStraightPath(&s.x, &e.x, polys, n, pts, flags, refs, &m, kMaxPathPolys)) || m <= 0)
        return ENavPathResult::NoPath;
    out.reserve((size_t)m);
    for (int i = 0; i < m; ++i) out.push_back(toVec(&pts[i * 3]));
    return result;
}

bool NavMesh::projectPoint(const glm::vec3& p, const glm::vec3& extents, glm::vec3& out) const {
    if (!valid()) return false;
    glm::vec3 q;
    if (!impl_->nearest(p, extents, &q.x)) return false;
    out = q;
    return true;
}

bool NavMesh::moveAlongSurface(const glm::vec3& from, const glm::vec3& to, const glm::vec3& extents, glm::vec3& out) const {
    if (!valid()) return false;
    glm::vec3 s;
    const dtPolyRef sRef = impl_->nearest(from, extents, &s.x);
    if (!sRef) return false;
    glm::vec3 r;
    dtPolyRef visited[kMaxVisited];
    int nv = 0;
    if (dtStatusFailed(impl_->query->moveAlongSurface(sRef, &s.x, &to.x, &impl_->filter, &r.x, visited, &nv, kMaxVisited)) || nv <= 0)
        return false;
    float h = r.y;
    if (dtStatusSucceed(impl_->query->getPolyHeight(visited[nv - 1], &r.x, &h))) r.y = h;
    out = r;
    return true;
}

void NavMesh::edges(std::vector<std::pair<glm::vec3, glm::vec3>>& out) const {
    out.clear();
    if (!impl_->mesh) return;
    const dtNavMesh* cm = impl_->mesh;
    for (int ti = 0; ti < cm->getMaxTiles(); ++ti) {
        const dtMeshTile* t = cm->getTile(ti);
        if (!t || !t->header) continue;
        for (int i = 0; i < t->header->polyCount; ++i) {
            const dtPoly& p = t->polys[i];
            if (p.getType() != DT_POLYTYPE_GROUND) continue;
            for (int j = 0; j < p.vertCount; ++j) {
                const unsigned short nei = p.neis[j];
                // 안쪽 변은 번호가 작은 쪽에서 한 번만.
                if (nei != 0 && !(nei & DT_EXT_LINK) && (int)(nei - 1) < i) continue;
                const float* a = &t->verts[p.verts[j] * 3];
                const float* b = &t->verts[p.verts[(j + 1) % p.vertCount] * 3];
                out.emplace_back(toVec(a), toVec(b));
            }
        }
    }
}
