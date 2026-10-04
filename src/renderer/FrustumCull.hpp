#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <array>
#include <cstdint>

// ── 절두체 컬링 ────────────────────────────────────────────────────────────────
// Renderer 의 CPU 컬링이 쓰는 판정입니다. Vulkan 도 Renderer 도 없이 시험할 수 있도록 glm 만
// 쓰는 헤더로 뺐습니다(tests/StereoViewTest.cpp).
//
// 깊이 프리패스와 컬러 패스가 이 한 판정으로 만든 같은 목록을 그립니다(GPU 컬링은 A-4 에서 없앴고
// 2026-10-04 에 코드도 지웠습니다). 예전에는 CPU 가 AABB 를, GPU 가 구를 써서 두 판정이
// 갈렸습니다(DEF-25). 구는 회전에 불변이므로 그 흔들림이 없습니다.

struct FrustumPlane {
    glm::vec3 normal;
    float     d;
};

// Gribb-Hartmann 평면 추출(Vulkan NDC z=[0,1]). 법선은 안쪽을 향합니다.
inline std::array<FrustumPlane, 6> extractFrustumPlanes(const glm::mat4& vp) {
    auto row = [&](int r) {
        return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
    };
    auto makePlane = [](glm::vec4 v) -> FrustumPlane {
        float len = glm::length(glm::vec3(v));
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
