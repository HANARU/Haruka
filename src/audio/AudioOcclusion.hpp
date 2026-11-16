#pragma once
// ── 소리 차폐 계산 (2026-11-06, Docs/Audio_Design.md 9-4) ─────────────────────────
// AudioSystem 이 게임 스레드에서 쓰는 순수 계산입니다. miniaudio · 물리와 무관해서 오디오 테스트가 따로 검산합니다.
//
// 차폐 정도 o 는 0 = 열림, 1 = 완전히 막힘입니다. 레이 하나면 0 아니면 1 이고, 반지름을 주면 음원 둘레 표본 중 막힌 비율이라
// 그 사이 값이 나옵니다(Steam Audio Volumetric 과 같은 생각, 그쪽 값은 1 = 열림이라 1 − o).
#include "AudioAttenuation.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <cmath>
#include <vector>

namespace hocc {

constexpr float kPi          = 3.14159265358979f;
constexpr float kOpenHz      = 20000.f;   // 로그 보간에서 열림 쪽 끝. 실제로 열려 있으면(o = 0) 필터를 거치지 않습니다
constexpr float kRetraceSec  = 0.1f;      // 같은 소리를 다시 판정하는 간격(UE 보간 기본 · Steam Audio 갱신 간격과 같은 0.1 s)
constexpr int   kRingSamples = 8;         // 반지름 > 0 일 때 원판 둘레 표본 수(중심 하나를 더해 9개)
constexpr float kSnap        = 1e-3f;     // 평활이 목표에 이만큼 가까우면 목표로 맞춥니다(열림 0 에 정확히 닿아야 필터를 건너뜀)

// 차폐 정도에 따른 게인: 열림 1 → 완전히 막힘 s.volume.
inline float gain(float o, const AudioOcclusionSettings& s) { return 1.f + (s.volume - 1.f) * o; }

// 차폐 정도에 따른 차단 주파수. 귀는 주파수를 로그로 듣기 때문에 로그 축으로 옮깁니다(UE 의 Air Absorption 로그 보간과 같음).
inline float cutoffHz(float o, const AudioOcclusionSettings& s) {
    return std::exp(std::log(kOpenHz) + (std::log(s.lowPassHz) - std::log(kOpenHz)) * o);
}

// 1차 저역 통과 y = (1 − a)·x + a·y' 의 a. 차단 주파수가 낮을수록 1 에 가깝습니다.
inline float onePoleCoef(float cutoff, float sampleRate) { return std::exp(-2.f * kPi * cutoff / sampleRate); }

// 지수 평활(시간 상수 tau 초). tau 가 0 이면 바로 목표입니다. 목표에 kSnap 안으로 오면 목표로 맞춥니다.
inline float smooth(float current, float target, float dt, float tau) {
    if (tau <= 0.f) return target;
    if (dt <= 0.f)  return current;
    const float next = target + (current - target) * std::exp(-dt / tau);
    return std::fabs(next - target) < kSnap ? target : next;
}

// 음원 쪽 표본점: 중심(음원 위치) 하나와, 청취자 → 음원 축에 수직인 반지름 radius 원판 둘레의 kRingSamples 개.
// 반지름이 0 이거나 청취자와 음원이 겹치면 중심만 넣습니다. out 은 비우고 채웁니다.
inline void samplePoints(const glm::vec3& listener, const glm::vec3& source, float radius, std::vector<glm::vec3>& out) {
    out.clear();
    out.push_back(source);
    const glm::vec3 axis = source - listener;
    const float     len  = glm::length(axis);
    if (!(radius > 0.f) || len < 1e-4f) return;
    const glm::vec3 d   = axis / len;
    // 축과 가장 덜 나란한 월드 축으로 수직 기저를 만듭니다.
    const glm::vec3 ref = std::fabs(d.y) < 0.9f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
    const glm::vec3 u   = glm::normalize(glm::cross(d, ref));
    const glm::vec3 v   = glm::cross(d, u);
    for (int k = 0; k < kRingSamples; ++k) {
        const float t = 2.f * kPi * (float)k / (float)kRingSamples;
        out.push_back(source + radius * (std::cos(t) * u + std::sin(t) * v));
    }
}

} // namespace hocc
