#include "NavGeometry.hpp"

#include "../geometry/CollisionMesh.hpp"
#include "../physics/RigidBodyComponent.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"

#include <algorithm>
#include <cmath>

namespace hnav {
namespace {

void addOutwardTri(NavInputGeometry& out, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& outward) {
    if (glm::dot(glm::cross(b - a, c - a), outward) >= 0.f) out.addTriangle(a, b, c);
    else                                                     out.addTriangle(a, c, b);
}

bool overlaps(const glm::vec3& amin, const glm::vec3& amax, const glm::vec3& bmin, const glm::vec3& bmax) {
    return amin.x <= bmax.x && amax.x >= bmin.x && amin.y <= bmax.y && amax.y >= bmin.y && amin.z <= bmax.z && amax.z >= bmin.z;
}

} // namespace

void addBox(NavInputGeometry& out, const glm::mat4& m, const glm::vec3& center, const glm::vec3& half) {
    glm::vec3 w[8];
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 l = center + glm::vec3((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
        w[i] = glm::vec3(m * glm::vec4(l, 1.f));
    }
    const glm::vec3 c = glm::vec3(m * glm::vec4(center, 1.f));
    // 면마다 꼭짓점 넷(비트: x=1 · y=2 · z=4). 감기는 면 중심 → 바깥 방향으로 맞춥니다(거울 크기도 자동으로 맞음).
    static constexpr int kFaces[6][4] = {
        { 0, 2, 6, 4 }, { 1, 3, 7, 5 },   // -x, +x
        { 0, 1, 5, 4 }, { 2, 3, 7, 6 },   // -y, +y
        { 0, 1, 3, 2 }, { 4, 5, 7, 6 },   // -z, +z
    };
    for (const auto& f : kFaces) {
        const glm::vec3 fc = (w[f[0]] + w[f[1]] + w[f[2]] + w[f[3]]) * 0.25f;
        const glm::vec3 outward = fc - c;
        addOutwardTri(out, w[f[0]], w[f[1]], w[f[2]], outward);
        addOutwardTri(out, w[f[0]], w[f[2]], w[f[3]], outward);
    }
}

size_t collectStaticGeometry(const World& world, const glm::vec3& bmin, const glm::vec3& bmax, NavInputGeometry& out) {
    size_t used = 0;
    for (const auto& up : world.getActors()) {
        const Actor* a = up.get();
        if (!a || !a->activeInHierarchy()) continue;
        const auto* rb = a->getComponent<RigidBodyComponent>();
        if (!rb || !rb->isEnabled() || rb->trigger || rb->motionType != EMotionType::Static) continue;
        const glm::mat4 m = a->worldMatrix();
        NavInputGeometry part;
        const HalfExt& he = rb->halfExtent;
        switch (rb->shape) {
            case EPhysicsShape::Sphere:
                addBox(part, m, rb->center, glm::vec3(he.x));
                break;
            case EPhysicsShape::Capsule:
                addBox(part, m, rb->center, glm::vec3(he.x, he.y + he.x, he.x));
                break;
            case EPhysicsShape::ConvexHull:
            case EPhysicsShape::Mesh: {
                const CollisionMesh* cm = nullptr;
                for (const auto& c : a->getComponents())
                    if (const auto* src = dynamic_cast<const ICollisionMeshSource*>(c.get()))
                        if (const CollisionMesh* mm = src->collisionMesh(); mm && !mm->empty()) { cm = mm; break; }
                if (!cm) break;
                // 메시는 감기 방향을 그대로 쓰고, 거울 크기면 뒤집습니다(물리와 같음, DEF-170). 볼록 껍질도 원래 메시 면을 씁니다.
                const bool flip = glm::determinant(glm::mat3(m)) < 0.f;
                const uint32_t n = (uint32_t)cm->positions.size();
                for (size_t i = 0; i + 2 < cm->indices.size(); i += 3) {
                    const uint32_t i0 = cm->indices[i], i1 = cm->indices[i + 1], i2 = cm->indices[i + 2];
                    if (i0 >= n || i1 >= n || i2 >= n) continue;
                    const glm::vec3 p0 = glm::vec3(m * glm::vec4(cm->positions[i0], 1.f));
                    const glm::vec3 p1 = glm::vec3(m * glm::vec4(cm->positions[i1], 1.f));
                    const glm::vec3 p2 = glm::vec3(m * glm::vec4(cm->positions[i2], 1.f));
                    if (flip) part.addTriangle(p0, p2, p1);
                    else      part.addTriangle(p0, p1, p2);
                }
                break;
            }
            default:
                addBox(part, m, rb->center, glm::vec3(he.x, he.y, he.z));
                break;
        }
        if (part.verts.empty()) continue;
        glm::vec3 pmin = part.verts[0], pmax = part.verts[0];
        for (const glm::vec3& v : part.verts) { pmin = glm::min(pmin, v); pmax = glm::max(pmax, v); }
        if (!overlaps(pmin, pmax, bmin, bmax)) continue;
        const uint32_t base = (uint32_t)out.verts.size();
        out.verts.insert(out.verts.end(), part.verts.begin(), part.verts.end());
        for (uint32_t i : part.tris) out.tris.push_back(base + i);
        ++used;
    }
    return used;
}

} // namespace hnav
