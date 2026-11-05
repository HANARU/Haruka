#pragma once
// 방향광 섀도 카메라 맞춤. glm 만 쓰는 헤더라 단위 시험(HarukaLightTest)에서 장치 없이 검산합니다.
//
// 머리 중심 추종(2026-11-02, 조사 shadow_fit 권장안 A). 그림자 거리 kRadius 의 구를 머리(두 눈 평균, 단안이면
// 카메라) 중심에 두고 그 구를 덮는 정사영 하나를 씁니다. 예전의 원점 고정 40 m 상자는 원점에서 24 ~ 32 m 를 넘으면
// 그림자가 직선으로 잘렸고(조사 M1), 상자 뒤쪽 far 가 긴 그림자를 잘랐습니다(M2 의 far 쪽).
//  - 빛 행렬은 회전만 씁니다(원점에서 빛 방향을 봄). 그래서 머리의 회전은 행렬에 들어가지 않고, 머리 위치만 들어갑니다.
//  - 맵 중심을 월드 원점 기준 텍셀 격자에 스냅합니다. 머리가 텍셀보다 작게 움직이면 행렬이 그대로라 그림자 경계가
//    떨리지 않고, 크게 움직여도 텍셀 단위로만 옮겨집니다(Bevy · Wicked · MS 식 floor 스냅).
//  - 구보다 빛 쪽에 있는 높은 캐스터: 장치에 depthClamp 가 있으면 투영 near 를 구 경계에 두고 그 앞의 캐스터를 깊이 0 에
//    눌러 담습니다(pancaking). 없으면 투영 near 를 kCasterReach 만큼 빛 쪽으로 늘립니다. 캐스터 컬링은 두 경우 모두
//    늘린 near 의 행렬(cullViewProj)을 씁니다. 컬링 near 를 늘리지 않으면 높은 캐스터가 버려집니다(DEF-161 과 같은 꼴).
//  - 구 경계 근처는 페이드로 그림자를 없앱니다(0.8R 에서 시작, R - 2텍셀에서 끝). 2텍셀은 3x3 PCF 의 ±1텍셀과
//    쌍선형 비교의 ±1텍셀을 합친 여유입니다. 맵이 스냅으로 최대 1텍셀 어긋나도 페이드 안쪽은 늘 맵 안입니다.
//  - 깊이 범위(far - near)는 depthClamp 여부에 따라 2R 또는 2R + kCasterReach 입니다. 바이어스를 섀도 NDC 로 바꿀 때
//    이 값으로 나눕니다(depthRangeFor). 래스터 경사 상한도 같은 값으로 나눕니다(ShadowPass).

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>

namespace hshadow {

constexpr float kRadius         = 20.f;    // 그림자 거리: 머리 중심 구의 반지름(m). UE 의 Dynamic Shadow Distance 에 해당
constexpr float kCasterReach    = 100.f;   // 구보다 빛 쪽으로 캐스터를 더 받는 거리(m)
constexpr float kFadeStartFrac  = 0.8f;    // 페이드 시작 = kFadeStartFrac × R
constexpr float kFadeEndTexels  = 2.f;     // 페이드 끝 = R - kFadeEndTexels 텍셀

struct DirectionalFit {
    glm::mat4 viewProj{ 1.f };      // 섀도맵 투영(Vulkan Y 반전 포함)
    glm::mat4 cullViewProj{ 1.f };  // 캐스터 컬링. near 가 늘 kCasterReach 만큼 빛 쪽(depthClamp 가 없으면 viewProj 와 같음)
    float     texelWorld = 0.f;     // 텍셀 하나의 월드 크기(m)
    float     depthRange = 0.f;     // 투영의 far - near(m)
    float     fadeStart  = 0.f;     // 머리에서의 거리(m). 여기서부터 그림자가 옅어짐
    float     fadeEnd    = 0.f;     // 여기부터 그림자 없음
};

// dirToLight 가 거의 수직이면 (1,0,0) 을 위로 씁니다.
inline glm::vec3 lightUp(const glm::vec3& dirToLight) {
    return (std::abs(dirToLight.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
}

constexpr float depthRangeFor(float radius, bool depthClamp) {
    return 2.f * radius + (depthClamp ? 0.f : kCasterReach);
}

inline DirectionalFit fitDirectional(const glm::vec3& center, const glm::vec3& dirToLight, float radius,
                                     uint32_t mapSize, bool depthClamp) {
    DirectionalFit f;
    f.texelWorld = 2.f * radius / (float)mapSize;
    // 회전만 쓰는 빛 행렬: 뷰 z = dot(p, dirToLight), 빛까지 가까울수록 near 쪽입니다.
    const glm::mat4 lv = glm::lookAt(glm::vec3(0.f), -dirToLight, lightUp(dirToLight));
    const glm::vec3 c  = glm::vec3(lv * glm::vec4(center, 1.f));
    const float cx = std::floor(c.x / f.texelWorld) * f.texelWorld;
    const float cy = std::floor(c.y / f.texelWorld) * f.texelWorld;
    const float farD       = -c.z + radius;
    const float nearSphere = -c.z - radius;
    const float nearReach  = nearSphere - kCasterReach;
    auto ortho = [&](float n) {
        glm::mat4 p = glm::ortho(cx - radius, cx + radius, cy - radius, cy + radius, n, farD);
        // Vulkan Y flip. 상자가 원점 대칭이 아니므로 배율과 이동 항을 함께 뒤집습니다(배율만 뒤집으면 중심이 -cy 로 감).
        p[1][1] *= -1.f;
        p[3][1] *= -1.f;
        return p;
    };
    f.viewProj     = ortho(depthClamp ? nearSphere : nearReach) * lv;
    f.cullViewProj = ortho(nearReach) * lv;
    f.depthRange   = depthRangeFor(radius, depthClamp);
    f.fadeStart    = kFadeStartFrac * radius;
    f.fadeEnd      = radius - kFadeEndTexels * f.texelWorld;
    return f;
}

// 리시버 상수 바이어스(월드 m)를 섀도 NDC 로 바꿉니다.
inline float receiverBiasNdc(float biasWorldM, float depthRange) { return biasWorldM / depthRange; }

} // namespace hshadow
