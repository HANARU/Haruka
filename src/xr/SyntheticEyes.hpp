#pragma once

// ── 합성 눈 (창 없는 렌더 경로) ───────────────────────────────────────────────────────────────
// 헤드셋 없이 두 눈 프레임을 돌릴 때(--synthetic-eyes) XR 런타임 대신 눈 두 개를 만듭니다. 공식은 스테레오 흉내
// (--emulate-stereo, Renderer::gatherViews)와 같습니다: 머리 좌우로 눈 사이 거리의 절반씩 옮긴 평행한 두 눈이고,
// 세로 시야는 tanUp, 가로는 눈 하나의 화면 비율로 정한 뒤 코 쪽 45% · 바깥쪽 55% 로 나눈 비대칭 시야입니다.
// 결과는 XR 런타임이 주는 값과 같은 형식(원점 공간 자세 + 네 방향 시야각, 라디안)이라 렌더러의 XR 경로를 그대로 탑니다.
//
// headY 는 원점 공간의 눈높이입니다(씬 리그가 있으면 XrRigComponent 의 눈높이, 없으면 0).
// 순수 함수라 HarukaStereoTest(stereo_views)가 화면 없이 검산합니다.

#include "../renderer/RenderTypes.hpp"

#include <array>
#include <cmath>

namespace hxr {

inline constexpr float kSyntheticIpd = 0.064f;   // 스테레오 흉내와 같은 64mm

inline std::array<XrEyePose, 2> syntheticEyes(float ipd, float tanUp, float aspect, float headY) {
    const float tanWidth = 2.f * tanUp * aspect;
    const float tanOuter = 0.55f * tanWidth;
    const float tanInner = 0.45f * tanWidth;
    std::array<XrEyePose, 2> eyes{};
    for (uint32_t e = 0; e < 2; ++e) {
        const float side = (e == 0) ? -1.f : 1.f;   // 왼쪽 눈은 머리의 -X 쪽
        XrEyePose& p = eyes[e];
        p.position   = glm::vec3(side * ipd * 0.5f, headY, 0.f);
        p.qx = 0.f; p.qy = 0.f; p.qz = 0.f; p.qw = 1.f;
        const float tanLeft  = (e == 0) ? -tanOuter : -tanInner;
        const float tanRight = (e == 0) ?  tanInner :  tanOuter;
        p.angleLeft  = std::atan(tanLeft);
        p.angleRight = std::atan(tanRight);
        p.angleUp    = std::atan(tanUp);
        p.angleDown  = -std::atan(tanUp);
    }
    return eyes;
}

} // namespace hxr
