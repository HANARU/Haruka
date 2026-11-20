#include "NavMeshBuild.hpp"

#include <Recast.h>
#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

void NavBuildSettings::sanitize() {
    auto fix = [](float& v, float lo, float hi, float def) {
        if (!std::isfinite(v)) v = def;
        v = std::clamp(v, lo, hi);
    };
    fix(agentRadius, 0.f, 10.f, 0.3f);
    fix(agentHeight, 0.1f, 20.f, 1.8f);
    fix(maxSlopeDeg, 0.f, 89.f, 45.f);
    fix(maxClimb, 0.f, 10.f, 0.35f);
    fix(cellSize, 0.01f, 5.f, 0.1f);
    fix(cellHeight, 0.01f, 5.f, 0.05f);
    fix(regionMinSize, 0.f, 150.f, 8.f);
    fix(regionMergeSize, 0.f, 150.f, 20.f);
    fix(edgeMaxLen, 0.f, 100.f, 12.f);
    fix(edgeMaxError, 0.1f, 3.f, 1.3f);
    fix(detailSampleDist, 0.f, 16.f, 6.f);
    fix(detailSampleMaxError, 0.f, 16.f, 1.f);
}

void NavInputGeometry::addTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const uint32_t base = (uint32_t)verts.size();
    verts.push_back(a);
    verts.push_back(b);
    verts.push_back(c);
    tris.push_back(base);
    tris.push_back(base + 1);
    tris.push_back(base + 2);
}

namespace hnav {
namespace {

// Recast 객체를 함수를 벗어날 때 지웁니다.
struct RecastScratch {
    rcHeightfield*        hf    = nullptr;
    rcCompactHeightfield* chf   = nullptr;
    rcContourSet*         cset  = nullptr;
    rcPolyMesh*           pmesh = nullptr;
    rcPolyMeshDetail*     dmesh = nullptr;
    ~RecastScratch() {
        rcFreeHeightField(hf);
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
    }
};

void hashBytes(uint64_t& h, const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x100000001B3ull; }
}

// 파일 쓰기 · 읽기 도우미(리틀 엔디언 그대로, D5).
template <class T> void put(std::vector<uint8_t>& out, const T& v) {
    const size_t at = out.size();
    out.resize(at + sizeof(T));
    std::memcpy(out.data() + at, &v, sizeof(T));
}
struct Reader {
    const uint8_t* p;
    size_t         left;
    template <class T> bool get(T& v) {
        if (left < sizeof(T)) return false;
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        left -= sizeof(T);
        return true;
    }
};

constexpr int kSettingFloats = 12;
void settingsToArray(const NavBuildSettings& s, float (&a)[kSettingFloats]) {
    const float v[kSettingFloats] = { s.agentRadius, s.agentHeight, s.maxSlopeDeg, s.maxClimb, s.cellSize, s.cellHeight,
                                      s.regionMinSize, s.regionMergeSize, s.edgeMaxLen, s.edgeMaxError,
                                      s.detailSampleDist, s.detailSampleMaxError };
    std::memcpy(a, v, sizeof v);
}
void settingsFromArray(NavBuildSettings& s, const float (&a)[kSettingFloats]) {
    s.agentRadius = a[0]; s.agentHeight = a[1]; s.maxSlopeDeg = a[2]; s.maxClimb = a[3]; s.cellSize = a[4]; s.cellHeight = a[5];
    s.regionMinSize = a[6]; s.regionMergeSize = a[7]; s.edgeMaxLen = a[8]; s.edgeMaxError = a[9];
    s.detailSampleDist = a[10]; s.detailSampleMaxError = a[11];
}

} // namespace

uint64_t inputHash(const NavInputGeometry& in, const NavBuildSettings& s, const glm::vec3& bmin, const glm::vec3& bmax) {
    uint64_t h = 0xCBF29CE484222325ull;
    float a[kSettingFloats];
    settingsToArray(s, a);
    hashBytes(h, a, sizeof a);
    hashBytes(h, &bmin, sizeof bmin);
    hashBytes(h, &bmax, sizeof bmax);
    const uint64_t nv = in.verts.size(), nt = in.tris.size();
    hashBytes(h, &nv, sizeof nv);
    hashBytes(h, &nt, sizeof nt);
    if (!in.verts.empty()) hashBytes(h, in.verts.data(), in.verts.size() * sizeof(glm::vec3));
    if (!in.tris.empty())  hashBytes(h, in.tris.data(), in.tris.size() * sizeof(uint32_t));
    return h;
}

NavBuildResult build(const NavInputGeometry& in, const NavBuildSettings& settings, const glm::vec3& bmin, const glm::vec3& bmax) {
    const auto t0 = std::chrono::steady_clock::now();
    NavBuildResult r;
    auto finish = [&](bool ok, std::string err = {}) {
        r.ok = ok;
        r.error = std::move(err);
        r.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return r;
    };
    NavBuildSettings s = settings;
    s.sanitize();
    if (!(bmax.x > bmin.x && bmax.y > bmin.y && bmax.z > bmin.z)) return finish(false, "the volume has no size");
    for (uint32_t i : in.tris)
        if (i >= in.verts.size()) return finish(false, "triangle index out of range");

    rcConfig cfg{};
    cfg.cs = s.cellSize;
    cfg.ch = s.cellHeight;
    cfg.walkableSlopeAngle = s.maxSlopeDeg;
    cfg.walkableHeight = (int)std::ceil(s.agentHeight / cfg.ch);
    cfg.walkableClimb  = (int)std::floor(s.maxClimb / cfg.ch);
    cfg.walkableRadius = (int)std::ceil(s.agentRadius / cfg.cs);
    cfg.maxEdgeLen = (int)(s.edgeMaxLen / cfg.cs);
    cfg.maxSimplificationError = s.edgeMaxError;
    cfg.minRegionArea   = (int)(s.regionMinSize * s.regionMinSize);
    cfg.mergeRegionArea = (int)(s.regionMergeSize * s.regionMergeSize);
    cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    cfg.detailSampleDist     = s.detailSampleDist < 0.9f ? 0.f : cfg.cs * s.detailSampleDist;
    cfg.detailSampleMaxError = cfg.ch * s.detailSampleMaxError;
    cfg.bmin[0] = bmin.x; cfg.bmin[1] = bmin.y; cfg.bmin[2] = bmin.z;
    cfg.bmax[0] = bmax.x; cfg.bmax[1] = bmax.y; cfg.bmax[2] = bmax.z;
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
    if ((int64_t)cfg.width * cfg.height > kMaxCells)
        return finish(false, "too many cells (" + std::to_string(cfg.width) + " x " + std::to_string(cfg.height) +
                             "); raise Cell Size or make the volume smaller");
    if (in.tris.empty()) return finish(true);   // 걸을 곳 없음(빈 메시)

    rcContext ctx(false);
    RecastScratch sc;
    sc.hf = rcAllocHeightfield();
    if (!sc.hf || !rcCreateHeightfield(&ctx, *sc.hf, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
        return finish(false, "could not create the heightfield");

    const int nv = (int)in.verts.size(), nt = (int)in.triangleCount();
    std::vector<unsigned char> areas((size_t)nt, 0);
    const float* vp = &in.verts[0].x;
    const int*   tp = reinterpret_cast<const int*>(in.tris.data());
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, vp, nv, tp, nt, areas.data());
    if (!rcRasterizeTriangles(&ctx, vp, nv, tp, areas.data(), nt, *sc.hf, cfg.walkableClimb))
        return finish(false, "could not rasterize the triangles");
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *sc.hf);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *sc.hf);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *sc.hf);

    sc.chf = rcAllocCompactHeightfield();
    if (!sc.chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *sc.hf, *sc.chf))
        return finish(false, "could not build the compact heightfield");
    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *sc.chf)) return finish(false, "could not erode the walkable area");
    if (!rcBuildDistanceField(&ctx, *sc.chf)) return finish(false, "could not build the distance field");
    if (!rcBuildRegions(&ctx, *sc.chf, 0, cfg.minRegionArea, cfg.mergeRegionArea)) return finish(false, "could not build regions");

    sc.cset = rcAllocContourSet();
    if (!sc.cset || !rcBuildContours(&ctx, *sc.chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *sc.cset))
        return finish(false, "could not build contours");
    sc.pmesh = rcAllocPolyMesh();
    if (!sc.pmesh || !rcBuildPolyMesh(&ctx, *sc.cset, cfg.maxVertsPerPoly, *sc.pmesh))
        return finish(false, "could not build the polygon mesh");
    sc.dmesh = rcAllocPolyMeshDetail();
    if (!sc.dmesh || !rcBuildPolyMeshDetail(&ctx, *sc.pmesh, *sc.chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *sc.dmesh))
        return finish(false, "could not build the detail mesh");
    if (sc.pmesh->npolys == 0) return finish(true);

    // 걸을 수 있는 영역은 모두 같은 종류(영역 0, 플래그 1)로 둡니다. 영역 비용 · 문 · 물은 남음.
    for (int i = 0; i < sc.pmesh->npolys; ++i) {
        sc.pmesh->flags[i] = sc.pmesh->areas[i] == RC_WALKABLE_AREA ? 1 : 0;
        sc.pmesh->areas[i] = 0;
    }
    if (sc.pmesh->nverts >= 0xFFFF) return finish(false, "too many vertices for one tile");

    dtNavMeshCreateParams p{};
    p.verts = sc.pmesh->verts;
    p.vertCount = sc.pmesh->nverts;
    p.polys = sc.pmesh->polys;
    p.polyAreas = sc.pmesh->areas;
    p.polyFlags = sc.pmesh->flags;
    p.polyCount = sc.pmesh->npolys;
    p.nvp = sc.pmesh->nvp;
    p.detailMeshes = sc.dmesh->meshes;
    p.detailVerts = sc.dmesh->verts;
    p.detailVertsCount = sc.dmesh->nverts;
    p.detailTris = sc.dmesh->tris;
    p.detailTriCount = sc.dmesh->ntris;
    p.walkableHeight = s.agentHeight;
    p.walkableRadius = s.agentRadius;
    p.walkableClimb  = s.maxClimb;
    std::memcpy(p.bmin, sc.pmesh->bmin, sizeof p.bmin);
    std::memcpy(p.bmax, sc.pmesh->bmax, sizeof p.bmax);
    p.cs = cfg.cs;
    p.ch = cfg.ch;
    p.buildBvTree = true;
    unsigned char* data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&p, &data, &size) || !data) return finish(false, "could not create the Detour tile");
    r.tile.assign(data, data + size);
    dtFree(data);
    r.polygons = sc.pmesh->npolys;
    r.vertices = sc.pmesh->nverts;
    return finish(true);
}

std::vector<uint8_t> writeFile(const NavFile& f) {
    std::vector<uint8_t> out;
    out.reserve(96 + f.tile.size());
    put(out, NavFile::kMagic);
    put(out, NavFile::kVersion);
    float a[kSettingFloats];
    settingsToArray(f.settings, a);
    put(out, a);
    put(out, f.inputHash);
    put(out, f.boundsMin);
    put(out, f.boundsMax);
    put(out, f.polygons);
    put(out, (uint32_t)f.tile.size());
    out.insert(out.end(), f.tile.begin(), f.tile.end());
    return out;
}

bool readFile(const uint8_t* data, size_t size, NavFile& out, std::string& err) {
    Reader rd{ data, size };
    uint32_t magic = 0, version = 0, tileSize = 0;
    float a[kSettingFloats];
    if (!rd.get(magic) || magic != NavFile::kMagic) { err = "not a navigation file"; return false; }
    if (!rd.get(version) || version != NavFile::kVersion) {
        err = "unsupported navigation file version " + std::to_string(version);
        return false;
    }
    NavFile f;
    if (!rd.get(a) || !rd.get(f.inputHash) || !rd.get(f.boundsMin) || !rd.get(f.boundsMax) || !rd.get(f.polygons) ||
        !rd.get(tileSize)) {
        err = "truncated header";
        return false;
    }
    if (tileSize != rd.left) { err = "tile size does not match the file size"; return false; }
    settingsFromArray(f.settings, a);
    if (tileSize > 0) {
        dtMeshHeader h;
        if (tileSize < sizeof h) { err = "tile is too small"; return false; }
        std::memcpy(&h, rd.p, sizeof h);
        if (h.magic != DT_NAVMESH_MAGIC || h.version != DT_NAVMESH_VERSION) { err = "tile is not Detour navigation data"; return false; }
        f.tile.assign(rd.p, rd.p + tileSize);
    }
    out = std::move(f);
    return true;
}

} // namespace hnav
