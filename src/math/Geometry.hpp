#pragma once

// ── 기본 도형과 판정 (B1, 2026-11-17) ────────────────────────────────────────────
// 광선 · 평면 · 구 · AABB · OBB 와 그 사이의 판정을 한 곳에 둡니다. 예전에는 3D UI 포인터 · 바닥 찾기 · 기즈모 · 컬링이
// 같은 판정을 따로 짰고 허용 오차 리터럴도 제각각이었습니다(Docs/Math_Reinforcement.md B1). glm 만 쓰는 헤더라
// 엔진 어디서나(에디터 · 런타임 · 시험) 씁니다. 허용 오차는 Tolerance.hpp 의 이름만 씁니다.
//
// 규약:
//   · 광선 방향은 정규화하지 않아도 됩니다. 결과 t 는 "origin + dir · t" 의 매개변수이고, dir 가 단위 벡터일 때만 거리입니다.
//   · 평면은 dot(normal, p) + d = 0 이고 normal 쪽이 앞(양의 거리)입니다. 절두체 평면(FrustumCull.hpp)도 같은 꼴입니다.
//   · 판정 함수는 실패하면 false 를 돌려주고 출력 인자를 건드리지 않습니다. NaN 입력은 실패로 끝납니다(비교가 거짓).

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "Tolerance.hpp"

namespace hmath {

struct Ray {
    glm::vec3 origin{ 0.f };
    glm::vec3 dir{ 0.f, 0.f, -1.f };
    glm::vec3 at(float t) const { return origin + dir * t; }
};

struct Plane {
    glm::vec3 normal{ 0.f, 1.f, 0.f };
    float     d = 0.f;
    // 점과 법선으로 만듭니다. 법선은 정규화해야 signedDistance 가 미터가 됩니다.
    static Plane fromPointNormal(const glm::vec3& p, const glm::vec3& n) { return { n, -glm::dot(n, p) }; }
    float signedDistance(const glm::vec3& p) const { return glm::dot(normal, p) + d; }
};

struct Sphere {
    glm::vec3 center{ 0.f };
    float     radius = 0.f;
};

struct Aabb {
    glm::vec3 min{ FLT_MAX };
    glm::vec3 max{ -FLT_MAX };
    bool      valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    glm::vec3 center() const { return 0.5f * (min + max); }
    glm::vec3 extents() const { return 0.5f * (max - min); }   // 반변
    void      expand(const glm::vec3& p) { min = glm::min(min, p); max = glm::max(max, p); }
    void      expand(const Aabb& b) { if (b.valid()) { min = glm::min(min, b.min); max = glm::max(max, b.max); } }
    bool      contains(const glm::vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
};

// 방향이 있는 상자: 가운데, 단위 축 세 개(열), 축마다 반변.
struct Obb {
    glm::vec3 center{ 0.f };
    glm::mat3 axes{ 1.f };
    glm::vec3 half{ 0.f };
    // 로컬 AABB 와 아핀 행렬(회전 · 크기 · 이동, 비균등 크기 가능)에서 만듭니다. 기울임(shear)이 있으면 근사입니다.
    static Obb fromLocalAabb(const Aabb& local, const glm::mat4& m) {
        Obb o;
        o.center = glm::vec3(m * glm::vec4(local.center(), 1.f));
        const glm::vec3 e = local.extents();
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 col = glm::vec3(m[k]);
            const float     len = glm::length(col);
            o.axes[k] = len > tol::kDirLen ? col / len : glm::vec3(k == 0, k == 1, k == 2);
            o.half[k] = e[k] * len;
        }
        return o;
    }
};

// ── 벡터 도우미 ─────────────────────────────────────────────────────────────────
// 길이가 kDirLen 보다 작으면 fallback 을 돌려줍니다(0 으로 나누어 NaN 이 퍼지지 않게).
inline glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback = glm::vec3(0.f)) {
    const float l = glm::length(v);
    return l > tol::kDirLen ? v / l : fallback;
}

// 선분 [a, b] 위에서 p 에 가장 가까운 점의 매개변수(0..1).
inline float closestParamOnSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 ab = b - a;
    const float     l2 = glm::dot(ab, ab);
    if (l2 < tol::kDirLen2) return 0.f;
    return std::clamp(glm::dot(p - a, ab) / l2, 0.f, 1.f);
}
inline float distancePointSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    return glm::length(p - (a + (b - a) * closestParamOnSegment(p, a, b)));
}

// AABB 에서 p 에 가장 가까운 점.
inline glm::vec3 closestPointOnAabb(const glm::vec3& p, const Aabb& b) { return glm::clamp(p, b.min, b.max); }

// ── 변환 ────────────────────────────────────────────────────────────────────────
// 아핀 행렬로 AABB 를 옮긴 뒤 감싸는 AABB(Arvo 1990): 축마다 |M| · 반변으로 새 반변을 구합니다. 꼭짓점 여덟 개를 옮기는 것과
// 같은 결과이고 곱셈은 절반 아래입니다.
inline Aabb transformAabb(const Aabb& b, const glm::mat4& m) {
    if (!b.valid()) return b;
    const glm::vec3 c = glm::vec3(m * glm::vec4(b.center(), 1.f));
    const glm::vec3 e = b.extents();
    glm::vec3 ne(0.f);
    for (int col = 0; col < 3; ++col)
        ne += glm::abs(glm::vec3(m[col])) * e[col];
    return { c - ne, c + ne };
}

// 아핀 행렬(마지막 행 0 0 0 1)의 역(O2). 3×3 부분의 역(여인수 / 행렬식)과 이동만 계산하므로 glm::inverse(4×4 일반 역)보다
// 곱셈이 절반 아래입니다. 비균등 크기 · 기울임도 정확합니다. 행렬식이 0 이면(크기 0) 일반 역과 같이 무한 · NaN 이 나옵니다.
inline glm::mat4 affineInverse(const glm::mat4& m) {
    const glm::mat3 inv3 = glm::inverse(glm::mat3(m));
    glm::mat4 r(inv3);
    r[3] = glm::vec4(-(inv3 * glm::vec3(m[3])), 1.f);
    return r;
}
// 회전 + 이동만 있는 행렬(크기 1)의 역: 전치 + 이동.
inline glm::mat4 rigidInverse(const glm::mat4& m) {
    const glm::mat3 rt = glm::transpose(glm::mat3(m));
    glm::mat4 r(rt);
    r[3] = glm::vec4(-(rt * glm::vec3(m[3])), 1.f);
    return r;
}

// 광선을 행렬의 역으로 옮깁니다(월드 → 로컬). 방향은 정규화하지 않으므로 로컬 t 와 월드 t 가 같습니다.
inline Ray transformRay(const Ray& r, const glm::mat4& m) {
    return { glm::vec3(m * glm::vec4(r.origin, 1.f)), glm::vec3(m * glm::vec4(r.dir, 0.f)) };
}

// ── 광선 판정 ───────────────────────────────────────────────────────────────────
// 광선 · 평면. t ≥ 0 인 교점만 받습니다. 나란하면 실패합니다.
inline bool intersectRayPlane(const Ray& r, const Plane& p, float& tOut) {
    const float denom = glm::dot(p.normal, r.dir);
    if (!(std::fabs(denom) >= tol::kParallel)) return false;
    const float t = -(glm::dot(p.normal, r.origin) + p.d) / denom;
    if (!(t >= 0.f)) return false;
    tOut = t;
    return true;
}

// 광선 · 평면이지만 뒤쪽 교점(t < 0)도 받습니다(기즈모 드래그처럼 무한 직선이 필요할 때).
inline bool intersectLinePlane(const Ray& r, const Plane& p, float& tOut) {
    const float denom = glm::dot(p.normal, r.dir);
    if (!(std::fabs(denom) >= tol::kParallel)) return false;
    const float t = -(glm::dot(p.normal, r.origin) + p.d) / denom;
    if (!std::isfinite(t)) return false;
    tOut = t;
    return true;
}

// 광선 · 구. 시작점이 구 안이면 나가는 점(t > 0)을 줍니다. 가장 가까운 t ≥ 0.
inline bool intersectRaySphere(const Ray& r, const Sphere& s, float& tOut) {
    const glm::vec3 oc = r.origin - s.center;
    const float a = glm::dot(r.dir, r.dir);
    if (a < tol::kDirLen2) return false;
    const float b = glm::dot(oc, r.dir);
    const float c = glm::dot(oc, oc) - s.radius * s.radius;
    const float disc = b * b - a * c;
    if (!(disc >= 0.f)) return false;
    const float sq = std::sqrt(disc);
    float t = (-b - sq) / a;
    if (t < 0.f) t = (-b + sq) / a;
    if (!(t >= 0.f)) return false;
    tOut = t;
    return true;
}

// 광선 · AABB 슬랩 판정(Kay-Kajiya). 들어가는 t 와 나가는 t 를 줍니다. 시작점이 상자 안이면 tEnter < 0 입니다.
// 축과 나란한 광선은 그 축의 슬랩 안에 있을 때만 통과합니다.
inline bool intersectRayAabb(const Ray& r, const Aabb& b, float& tEnter, float& tExit) {
    float tmin = -FLT_MAX, tmax = FLT_MAX;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(r.dir[k]) < tol::kParallel) {
            if (r.origin[k] < b.min[k] || r.origin[k] > b.max[k]) return false;
            continue;
        }
        float t1 = (b.min[k] - r.origin[k]) / r.dir[k];
        float t2 = (b.max[k] - r.origin[k]) / r.dir[k];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (tmax < 0.f) return false;   // 상자가 광선 뒤
    tEnter = tmin;
    tExit  = tmax;
    return true;
}

// 광선 · OBB. 광선을 상자 축으로 옮겨 AABB 판정을 합니다.
inline bool intersectRayObb(const Ray& r, const Obb& o, float& tEnter, float& tExit) {
    const glm::mat3 inv = glm::transpose(o.axes);   // 정규 직교 축
    const Ray local{ inv * (r.origin - o.center), inv * r.dir };
    return intersectRayAabb(local, { -o.half, o.half }, tEnter, tExit);
}

// 광선 · 삼각형(Möller-Trumbore 1997). 양면입니다. 무게중심 좌표(u, v)도 줍니다(교점 = (1−u−v)·a + u·b + v·c).
inline bool intersectRayTriangle(const Ray& r, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                 float& tOut, float& uOut, float& vOut) {
    const glm::vec3 e1 = b - a, e2 = c - a;
    const glm::vec3 pv = glm::cross(r.dir, e2);
    const float det = glm::dot(e1, pv);
    if (!(std::fabs(det) >= tol::kParallel)) return false;   // 면과 나란하거나 퇴화 삼각형
    const float inv = 1.f / det;
    const glm::vec3 tv = r.origin - a;
    const float u = glm::dot(tv, pv) * inv;
    if (u < 0.f || u > 1.f) return false;
    const glm::vec3 qv = glm::cross(tv, e1);
    const float v = glm::dot(r.dir, qv) * inv;
    if (v < 0.f || u + v > 1.f) return false;
    const float t = glm::dot(e2, qv) * inv;
    if (!(t >= 0.f)) return false;
    tOut = t; uOut = u; vOut = v;
    return true;
}

// ── 겹침 판정 ───────────────────────────────────────────────────────────────────
inline bool overlapAabbAabb(const Aabb& a, const Aabb& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}
inline bool overlapSphereSphere(const Sphere& a, const Sphere& b) {
    const glm::vec3 d = a.center - b.center;
    const float r = a.radius + b.radius;
    return glm::dot(d, d) <= r * r;
}
inline bool overlapSphereAabb(const Sphere& s, const Aabb& b) {
    const glm::vec3 d = s.center - closestPointOnAabb(s.center, b);
    return glm::dot(d, d) <= s.radius * s.radius;
}
// 평면에 대한 구의 쪽: +1 앞, −1 뒤, 0 걸침.
inline int classifySpherePlane(const Sphere& s, const Plane& p) {
    const float dist = p.signedDistance(s.center);
    if (dist > s.radius) return 1;
    if (dist < -s.radius) return -1;
    return 0;
}

// ── 투영 ────────────────────────────────────────────────────────────────────────
// NDC(x, y ∈ [−1, 1])의 한 점을 지나는 월드 광선. invViewProj 는 (proj · view)⁻¹ 입니다. nearZ · farZ 는 광선 위 두 점을 뽑을
// NDC 깊이로, 기본값은 표준 깊이(0 = 가까운 면 · 1 = 먼 면)입니다. 엔진 카메라는 Reverse-Z 무한 원평면(C1)이라 (1, 0.5)를
// 넘깁니다(0 은 무한히 먼 곳이라 w = 0). 방향은 정규화합니다. 투영이 퇴화했으면 실패합니다.
inline bool rayFromNdc(const glm::mat4& invViewProj, const glm::vec2& ndc, Ray& out, float nearZ = 0.f, float farZ = 1.f) {
    const glm::vec4 pN = invViewProj * glm::vec4(ndc, nearZ, 1.f);
    const glm::vec4 pF = invViewProj * glm::vec4(ndc, farZ, 1.f);
    if (!(std::fabs(pN.w) >= tol::kHomogW) || !(std::fabs(pF.w) >= tol::kHomogW)) return false;
    const glm::vec3 o = glm::vec3(pN) / pN.w;
    const glm::vec3 d = glm::vec3(pF) / pF.w - o;
    if (!(glm::dot(d, d) >= tol::kDirLen2)) return false;
    out.origin = o;
    out.dir    = glm::normalize(d);
    return true;
}

} // namespace hmath
