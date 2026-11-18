#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cmath>

namespace hmath {

// 깊이 규약(C1, 2026-11-17): Reverse-Z + 무한 원평면. 가까운 면 = 깊이 1, 무한히 먼 곳 = 깊이 0 이고
// z_ndc = zNear / 거리 입니다. float 깊이는 0 근처에 표현 가능한 값이 몰려 있어, 거리에 반비례하는
// 깊이를 0 쪽으로 보내면 먼 곳의 정밀도가 크게 늡니다(Reed 2015 "Depth Precision Visualized",
// UE · Godot 4 의 기본). zFar 는 투영에 쓰지 않고 컬링 거리(FrustumCull 의 먼 평면) · AO 배경
// 판정에만 씁니다. 깊이 비교는 GREATER(_OR_EQUAL), 깊이 지우기 값은 0 입니다(GeometryPass).
inline glm::mat4 perspectiveReverseZ(float fovYDeg, float aspect, float zNear) {
    const float f = 1.f / std::tan(glm::radians(fovYDeg) * 0.5f);
    glm::mat4 m(0.f);
    m[0][0] = f / aspect;
    m[1][1] = -f;          // Vulkan Y-flip
    m[2][2] = 0.f;
    m[3][2] = zNear;       // Reverse-Z 무한 원평면: z_clip = zNear, w_clip = −z_view
    m[2][3] = -1.f;
    return m;
}

// 네 방향 시야각(라디안, 왼쪽 · 아래는 음수)으로 만든 비대칭 투영입니다. OpenXR 이 눈마다 주는
// XrFovf 를 그대로 받습니다(DEF-21). Vulkan 규약(깊이 [0,1], 클립 Y 아래)이고, 좌우 · 상하가
// 대칭이면 perspectiveReverseZ 와 같은 행렬입니다.
inline glm::mat4 perspectiveFromFov(float angleLeft, float angleRight, float angleUp,
                                    float angleDown, float zNear) {
    const float tanL = std::tan(angleLeft),  tanR = std::tan(angleRight);
    const float tanU = std::tan(angleUp),    tanD = std::tan(angleDown);
    const float tanW = tanR - tanL;
    const float tanH = tanD - tanU;   // Vulkan 은 클립 Y 가 아래라 위 · 아래를 뒤집습니다
    glm::mat4 m(0.f);
    m[0][0] = 2.f / tanW;
    m[2][0] = (tanR + tanL) / tanW;
    m[1][1] = 2.f / tanH;
    m[2][1] = (tanU + tanD) / tanH;
    m[2][2] = 0.f;                                 // Reverse-Z 무한 원평면(C1): 깊이 = zNear / 거리, 가까운 면 1 · 무한 0
    m[3][2] = zNear;
    m[2][3] = -1.f;
    return m;
}

} // namespace hmath
