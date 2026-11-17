#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <array>
#include <cstdint>

#include "../math/Geometry.hpp"

// ── 절두체 컬링 ────────────────────────────────────────────────────────────────
// Renderer 의 CPU 컬링이 쓰는 판정입니다. Vulkan 도 Renderer 도 없이 시험할 수 있도록 glm 만
// 쓰는 헤더로 뺐습니다(tests/StereoViewTest.cpp).
//
// 깊이 프리패스와 컬러 패스가 이 한 판정으로 만든 같은 목록을 그립니다(GPU 컬링은 A-4 에서 없앴고
// 2026-10-04 에 코드도 지웠습니다). 예전에는 CPU 가 AABB 를, GPU 가 구를 써서 두 판정이
// 갈렸습니다(DEF-25). 구는 회전에 불변이므로 그 흔들림이 없습니다.

// 절두체 평면은 공통 평면(hmath::Plane, dot(normal, p) + d = 0, 법선 쪽이 안)입니다(B1).
using FrustumPlane = hmath::Plane;

// Gribb-Hartmann 평면 추출(Vulkan NDC z=[0,1]). 법선은 안쪽을 향합니다.
inline std::array<FrustumPlane, 6> extractFrustumPlanes(const glm::mat4& vp) {
    auto row = [&](int r) {
        return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
    };
    // 무한 원평면 투영(C1 Reverse-Z)은 먼 쪽 평면의 법선이 0 입니다(z_ndc ≥ 0 이 늘 참). 그 평면은 늘 통과하는 평면으로 두고,
    // 카메라 컬링 거리는 replaceInfinitePlane 으로 따로 넣습니다. 평면 여섯 개의 집합은 표준 · 역 깊이 규약이 같습니다
    // (가까운 면 · 먼 면의 이름만 바뀜).
    auto makePlane = [](glm::vec4 v) -> FrustumPlane {
        float len = glm::length(glm::vec3(v));
        if (!(len > 1e-12f)) return { glm::vec3(0.f), 1.f };
        return { glm::vec3(v) / len, v.w / len };
    };
    auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    return {
        makePlane(r3 + r0),   // Left
        makePlane(r3 - r0),   // Right
        makePlane(r3 + r1),   // Bottom
        makePlane(r3 - r1),   // Top
        makePlane(r2),        // Near (Vulkan z=0)
        makePlane(r3 - r2),   // Far  (Vulkan z=1)
    };
}

// 법선이 0 인(무한히 먼) 평면을 카메라 앞 farDist 의 평면으로 바꿉니다. view 는 월드 → 눈(강체) 행렬이고, 눈은 -Z 를 봅니다.
// 남는 평면: z_view ≥ −farDist  ⇔  (view 의 3행) · p + farDist ≥ 0.
inline void replaceInfinitePlane(std::array<FrustumPlane, 6>& planes, const glm::mat4& view, float farDist) {
    for (auto& p : planes) {
        if (glm::dot(p.normal, p.normal) > 0.f) continue;
        const glm::vec3 n(view[0][2], view[1][2], view[2][2]);
        const float len = glm::length(n);
        if (!(len > 1e-12f)) return;
        p.normal = n / len;
        p.d      = (view[3][2] + farDist) / len;
        return;
    }
}

// 월드 공간 바운딩 구 대 절두체 판정.
inline bool sphereInFrustum(const std::array<FrustumPlane, 6>& planes,
                            const glm::vec3& center, float radius) {
    for (const auto& p : planes)
        if (glm::dot(p.normal, center) + p.d < -radius) return false;
    return true;
}

// 여러 뷰(눈) 중 하나라도 구를 볼 수 있는지(DEF-31). 스테레오에서 한쪽 눈 절두체로만 거르면,
// 그 눈 밖이지만 반대쪽 눈 안에 있는 물체가 한쪽 눈에서만 사라집니다(두 눈이 다른 장면을 봄).
// 두 절두체를 감싸는 하나의 절두체를 만드는 대신 눈마다 판정해 하나라도 통과하면 그립니다.
// 정확하고, 두 눈 시야가 기울어진(canted) 기기에도 그대로 맞습니다. Quest 2 · Pro · 3 은 두 눈이
// 평행하고 시야가 비대칭입니다.
inline bool sphereInAnyView(const std::array<FrustumPlane, 6>* views, uint32_t viewCount,
                            const glm::vec3& center, float radius) {
    for (uint32_t i = 0; i < viewCount; ++i)
        if (sphereInFrustum(views[i], center, radius)) return true;
    return false;
}
