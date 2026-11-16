#pragma once
// ── 잔향 계산 (2026-11-06, Docs/Audio_Design.md 9-5) ──────────────────────────────────
// AudioSystem 이 게임 스레드에서 쓰는 순수 계산입니다. miniaudio 와 무관해서 오디오 테스트가 따로 검산합니다.
//
// 잔향은 UE · Unity · FMOD 처럼 공용 리버브 하나에 음원마다 보내는 양(센드)을 주는 구조이고, 리버브 매개변수는 청취자가 있는
// 잔향 구역(ReverbZoneComponent)이 정합니다.
#include "AudioAttenuation.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace hrev {

// verblib(Freeverb) 콤 필터 8개의 평균 지연. 튜닝 값(44.1 kHz 기준 1116 ~ 1617 샘플, 평균 1378)을 샘플레이트에 맞춰 늘리므로
// 초 단위로는 늘 같습니다.
constexpr float kMeanCombSec = 1378.f / 44100.f;   // 약 31.2 ms
// verblib 의 방 크기 0 ~ 1 은 콤 되먹임 g = 0.7 ~ 0.98 입니다(scaleroom 0.28, offsetroom 0.7).
constexpr float kFeedbackMin = 0.7f;
constexpr float kFeedbackMax = 0.98f;
// 되먹임 g 의 콤이 60 dB 줄어드는 시간 = 평균 지연 × 3 / (−log10 g). 그래서 만들 수 있는 잔향 시간은 약 0.61 ~ 10.7 s 입니다.
inline float decayForFeedback(float g) { return kMeanCombSec * 3.f / -std::log10(g); }
inline const float kMinDecay = decayForFeedback(kFeedbackMin);   // 약 0.605 s
inline const float kMaxDecay = decayForFeedback(kFeedbackMax);   // 약 10.68 s

// 잔향 시간(RT60, 초) → verblib 방 크기(0 ~ 1). 범위 밖은 끝값으로 자릅니다(0.6 s 보다 짧은 방은 만들 수 없음, damping 으로 흉내).
// 저역 기준 값이고, damping 이 높으면 고역은 더 빨리 줄어듭니다.
inline float roomSizeForDecay(float decaySec) {
    const float t = std::clamp(decaySec, kMinDecay, kMaxDecay);
    const float g = std::pow(10.f, -3.f * kMeanCombSec / t);
    return std::clamp((g - kFeedbackMin) / (kFeedbackMax - kFeedbackMin), 0.f, 1.f);
}

// miniaudio 의 거리 감쇠 이득과 같은 식(Docs/Audio_Design.md 3-2). miniaudio 는 계산한 감쇠 이득을 돌려주지 않아 같은 식으로 셉니다.
inline float attenuationGain(const AudioAttenuationSettings& a, float distance) {
    if (!(a.minDistance < a.maxDistance)) return 1.f;   // miniaudio 는 min ≥ max 이면 감쇠를 끕니다
    const float d = std::clamp(distance, a.minDistance, a.maxDistance);
    float g = 1.f;
    switch (a.model) {
        case EAudioAttenuation::Inverse:
            g = a.minDistance / (a.minDistance + a.rolloff * (d - a.minDistance));
            break;
        case EAudioAttenuation::Linear:
            g = 1.f - a.rolloff * (d - a.minDistance) / (a.maxDistance - a.minDistance);
            break;
        case EAudioAttenuation::Exponential:
            g = std::pow(d / a.minDistance, -a.rolloff);
            break;
    }
    return std::clamp(g, 0.f, 1.f);
}

// 센드 출력의 게인. 센드 갈래는 보이스 필터 뒤(= 거리 감쇠 · 패닝 뒤)에서 나오므로 이미 감쇠 이득 g 를 받았습니다. 원하는 잔향 크기를
// send × g^(1 − reach) 로 두면, 센드에 곱할 값은 send × g^(−reach) 입니다(Meta XR Audio 의 Reverb Reach 와 같은 뜻).
//   reach 0: 직접음 대 잔향 비율이 거리와 무관하게 일정
//   reach 1: 잔향 크기가 거리와 무관(먼 소리일수록 잔향 비중이 커짐. 한 방 안의 확산 잔향은 위치에 거의 무관하다는 물리와 맞음)
// 감쇠가 0 에 가까우면 끝없이 커지지 않게 kMaxSendGain 으로 자릅니다.
constexpr float kMaxSendGain = 10.f;   // +20 dB
inline float sendGain(float send, float reach, float attenuation) {
    if (!(send > 0.f)) return 0.f;
    const float g = std::max(attenuation, 1e-6f);
    return std::min(kMaxSendGain, send * std::pow(g, -reach));
}

// 잔향 구역의 가중치: 청취자가 구역 안이면 1, 경계 밖 blendDistance 안이면 1 → 0 선형, 그 밖은 0.
// localPoint 는 구역의 위치 · 회전으로 옮긴 청취자 위치(크기는 반영하지 않음)이고, halfExtents · radius 는 액터 크기를 곱한 월드 길이입니다.
inline float boxWeight(const glm::vec3& localPoint, const glm::vec3& halfExtents, float blendDistance) {
    const glm::vec3 q = glm::max(glm::abs(localPoint) - halfExtents, glm::vec3(0.f));
    const float     d = glm::length(q);
    if (d <= 0.f) return 1.f;
    return blendDistance > 0.f ? std::max(0.f, 1.f - d / blendDistance) : 0.f;
}
inline float sphereWeight(const glm::vec3& localPoint, float radius, float blendDistance) {
    const float d = std::max(0.f, glm::length(localPoint) - radius);
    if (d <= 0.f) return 1.f;
    return blendDistance > 0.f ? std::max(0.f, 1.f - d / blendDistance) : 0.f;
}

// 리버브 매개변수(구역이 정하는 목표, 게임 스레드가 fadeTime 으로 따라감).
struct ReverbParams {
    float decayTime = 1.5f;   // RT60(초)
    float damping   = 0.5f;   // 0 ~ 1, 높을수록 고역이 빨리 줄어듦
    float width     = 1.f;    // 0 ~ 1, 스테레오 폭
    float level     = 0.f;    // 잔향 출력 크기(구역 level × 가중치). 0 이면 리버브를 멈춥니다
};

// 지수 평활(시간 상수 tau 초). 목표에 snap 안으로 오면 목표로 맞춥니다. tau 가 0 이면 바로 목표.
inline float follow(float current, float target, float dt, float tau, float snap) {
    if (tau <= 0.f) return target;
    if (dt <= 0.f)  return current;
    const float next = target + (current - target) * std::exp(-dt / tau);
    return std::fabs(next - target) < snap ? target : next;
}
inline ReverbParams follow(const ReverbParams& cur, const ReverbParams& target, float dt, float tau) {
    ReverbParams r;
    r.decayTime = follow(cur.decayTime, target.decayTime, dt, tau, 1e-3f);
    r.damping   = follow(cur.damping,   target.damping,   dt, tau, 1e-4f);
    r.width     = follow(cur.width,     target.width,     dt, tau, 1e-4f);
    r.level     = follow(cur.level,     target.level,     dt, tau, 1e-4f);
    return r;
}

} // namespace hrev
