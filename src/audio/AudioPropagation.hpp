#pragma once
// ── 소리 전파 계산: 도플러 · 공기 흡수 · 두께 투과 · dB (수학 보강 S1 · S2 · S4 · S6, 2026-11-17) ──────────────────
// AudioSystem 이 게임 스레드에서 쓰는 순수 계산입니다. miniaudio · 물리와 무관해서 오디오 테스트가 따로 검산합니다.
//
//  S1 도플러: OpenAL 1.1 식. 음원 → 청취자 방향 성분의 속도로 f' = f · (c − k·v_L) / (c − k·v_S). 속도는 위치 차분을 평활한
//     값이라 순간 이동(텔레포트 · 리그 스냅 회전)에 튀지 않게 상한을 둡니다.
//  S2 공기 흡수: 거리가 멀수록 높은 소리가 먼저 사라집니다(ISO 9613-1, 10 kHz 는 100 m 에 약 10 dB). UE 의 Air Absorption 처럼
//     최소 ~ 최대 거리 사이를 로그 주파수 축으로 옮겨 1차 저역 통과의 차단 주파수를 정합니다.
//  S4 두께 투과: 막은 물체의 두께를 재서, 정한 두께(fullThickness)보다 얇으면 일부만 막힌 것으로 칩니다(얇은 판자 너머 소리).
//  S6 dB: 게인 ↔ dB 변환과 dB 축 보간. 귀는 크기를 로그로 들으므로 새 계산(공기 흡수의 크기 · 투과)은 dB 축에서 합니다.
#include "AudioAttenuation.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// 소리마다의 전파 설정. 씬 파일에는 "propagation" 키 하나("도플러 공기흡수 차단주파수", 예: "0 0 3000")로 씁니다.
struct AudioPropagationSettings {
    float dopplerFactor   = 0.f;      // 0 = 끔(기본, 기존 소리 그대로), 1 = 물리 그대로, 그 사이 · 위는 과장 · 약화
    bool  airAbsorption   = false;    // 거리에 따른 높은 소리 흡수
    float airAbsorptionHz = 3000.f;   // 최대 거리에서의 차단 주파수(Hz). 최소 거리에서는 20 kHz(필터 없음)

    static constexpr float kMaxDoppler = 5.f;
    static constexpr float kMinHz      = 20.f;
    static constexpr float kMaxHz      = 20000.f;

    std::string toText() const;
    static bool fromText(std::string_view text, AudioPropagationSettings& out);
    bool operator==(const AudioPropagationSettings& o) const {
        return dopplerFactor == o.dopplerFactor && airAbsorption == o.airAbsorption && airAbsorptionHz == o.airAbsorptionHz;
    }
};

// 소리 버스(사운드 단계 4, 2026-11-17): 소리마다 하나를 고르고, AudioSystem 의 버스 볼륨 × 마스터 볼륨이 곱해집니다(UE Sound Class ·
// Unity Mixer 그룹의 가장 단순한 형태). 게임 설정 화면의 "음악 · 효과음 · 대사" 크기 조절이 이 값을 바꿉니다.
enum class EAudioBus : int { Sfx = 0, Music = 1, Voice = 2, Ambient = 3, Ui = 4 };
constexpr int kAudioBusCount = 5;
inline const char* audioBusName(EAudioBus b) {
    switch (b) {
        case EAudioBus::Music:   return "music";
        case EAudioBus::Voice:   return "voice";
        case EAudioBus::Ambient: return "ambient";
        case EAudioBus::Ui:      return "ui";
        default:                 return "sfx";
    }
}
inline bool audioBusFromName(std::string_view n, EAudioBus& out) {
    for (int i = 0; i < kAudioBusCount; ++i)
        if (n == audioBusName((EAudioBus)i)) { out = (EAudioBus)i; return true; }
    return false;
}

// 지향성 콘(사운드 단계 4): 액터 앞쪽(-Z)을 향해 소리를 냅니다. 안쪽 각 안 = 1, 바깥 각 밖 = outerGain, 그 사이는 코사인 축 선형
// 보간(miniaudio · OpenAL 과 같은 식). 안쪽 각 360 이면 끔(기본). 씬 파일 키 "cone" = "안쪽 바깥 바깥게인", 예: "60 120 0.2".
struct AudioConeSettings {
    float innerDeg  = 360.f;
    float outerDeg  = 360.f;
    float outerGain = 0.f;
    bool enabled() const { return innerDeg < 360.f; }
    std::string toText() const;
    static bool fromText(std::string_view text, AudioConeSettings& out);
    bool operator==(const AudioConeSettings& o) const {
        return innerDeg == o.innerDeg && outerDeg == o.outerDeg && outerGain == o.outerGain;
    }
};

namespace hdb {
inline float gainToDb(float g) { return g > 0.f ? 20.f * std::log10(g) : -144.f; }
inline float dbToGain(float db) { return db <= -144.f ? 0.f : std::pow(10.f, db / 20.f); }
// 두 게인 사이를 dB 축으로 보간(한쪽이 0 이면 −144 dB 로 봄).
inline float lerpGainDb(float a, float b, float t) { return dbToGain(gainToDb(a) + (gainToDb(b) - gainToDb(a)) * t); }
// 두 주파수 사이를 로그 축으로 보간(옥타브가 고르게).
inline float lerpHzLog(float a, float b, float t) { return std::exp(std::log(a) + (std::log(b) - std::log(a)) * t); }
} // namespace hdb

namespace hprop {

constexpr float kSpeedOfSound = 343.3f;   // m/s, 20 °C 공기(miniaudio · OpenAL 기본값과 같음)
constexpr float kMaxTrackedSpeed = 100.f; // 위치 차분 속도의 상한(m/s). 순간 이동 한 틱이 큰 도플러로 들리지 않게
constexpr float kMinPitch = 0.25f, kMaxPitch = 4.f;

// 도플러 배율. 속도는 월드 m/s. factor 0 이면 1. 같은 자리면 1.
inline float dopplerPitch(const glm::vec3& listenerPos, const glm::vec3& listenerVel, const glm::vec3& sourcePos,
                          const glm::vec3& sourceVel, float factor, float speedOfSound = kSpeedOfSound) {
    if (!(factor > 0.f)) return 1.f;
    const glm::vec3 sl = listenerPos - sourcePos;   // 음원 → 청취자(OpenAL 의 SL)
    const float len = glm::length(sl);
    if (len < 1e-4f) return 1.f;
    const glm::vec3 dir = sl / len;
    const float limit = speedOfSound / factor;       // OpenAL: 음속 이상은 자름
    const float vls = std::min(glm::dot(listenerVel, dir), limit);
    const float vss = std::min(glm::dot(sourceVel, dir), limit);
    const float num = speedOfSound - factor * vls;
    const float den = speedOfSound - factor * vss;
    if (!(den > 1e-3f)) return kMaxPitch;
    return std::clamp(num / den, kMinPitch, kMaxPitch);
}

// 위치 차분으로 속도를 추정하고 시간 상수 tau(초)로 평활합니다. 처음(hasPrev 거짓)이거나 dt 가 0 이면 속도는 그대로.
inline glm::vec3 trackVelocity(const glm::vec3& prevPos, const glm::vec3& pos, float dt, const glm::vec3& prevVel, float tau = 0.05f) {
    if (!(dt > 0.f)) return prevVel;
    glm::vec3 v = (pos - prevPos) / dt;
    const float s = glm::length(v);
    if (s > kMaxTrackedSpeed) return prevVel;   // 순간 이동은 무시(이전 속도 유지)
    const float k = tau > 0.f ? 1.f - std::exp(-dt / tau) : 1.f;
    return prevVel + (v - prevVel) * k;
}

// 공기 흡수의 차단 주파수(Hz). 최소 거리 안 = 20 kHz(필터 없음), 최대 거리 이상 = airAbsorptionHz, 그 사이는 로그 축.
inline float airCutoffHz(const AudioAttenuationSettings& a, const AudioPropagationSettings& p, float distance) {
    if (!p.airAbsorption || !a.spatialize || !(a.maxDistance > a.minDistance)) return AudioPropagationSettings::kMaxHz;
    const float t = std::clamp((distance - a.minDistance) / (a.maxDistance - a.minDistance), 0.f, 1.f);
    if (!(t > 0.f)) return AudioPropagationSettings::kMaxHz;   // 최소 거리 안: 정확히 20 kHz(로그 · 지수 반올림 없이, 필터 건너뜀)
    return hdb::lerpHzLog(AudioPropagationSettings::kMaxHz, std::clamp(p.airAbsorptionHz, 20.f, 20000.f), t);
}

// 표본 하나의 막힘 정도: 두께가 0 이면 열림, fullThickness 이상이면 1, 그 사이는 두께 비율. fullThickness ≤ 0 이면 예전처럼
// 조금이라도 막히면 1 입니다.
inline float blockFromThickness(float thickness, float fullThickness) {
    if (!(thickness > 0.f)) return 0.f;
    if (!(fullThickness > 0.f)) return 1.f;
    return std::clamp(thickness / fullThickness, 0.f, 1.f);
}

// 콘 게인(miniaudio ma_calculate_angular_gain 과 같은 식). facing = 음원이 향한 방향(단위), toListener = 음원 → 청취자(단위).
inline float coneGain(const AudioConeSettings& c, const glm::vec3& facing, const glm::vec3& toListener) {
    if (!c.enabled()) return 1.f;
    const float d  = glm::dot(facing, toListener);
    const float ci = std::cos(glm::radians(c.innerDeg) * 0.5f);
    const float co = std::cos(glm::radians(std::max(c.outerDeg, c.innerDeg)) * 0.5f);
    if (d > ci) return 1.f;
    if (d > co && ci > co) return c.outerGain + (1.f - c.outerGain) * (d - co) / (ci - co);
    return c.outerGain;
}

// 보이스 예산(사운드 단계 4): 들리는 크기 순으로 앞에서 budget 개만 실제로 섞고 나머지는 가상화합니다. gains[i] 의 순위가
// budget 이상이면 culled[i] = true. 크기가 같으면 번호가 작은 것이 앞(결과가 흔들리지 않게). budget 0 = 제한 없음.
inline void rankBudget(const std::vector<float>& gains, uint32_t budget, std::vector<bool>& culled) {
    culled.assign(gains.size(), false);
    if (budget == 0 || gains.size() <= budget) return;
    std::vector<uint32_t> order(gains.size());
    for (uint32_t i = 0; i < (uint32_t)order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return gains[a] > gains[b]; });
    for (size_t r = budget; r < order.size(); ++r) culled[order[r]] = true;
}

// 차폐 표본 원판의 회전(S5): 판정할 때마다 황금각(137.5°)만큼 돌려, 평활과 함께 표본 8개보다 촘촘하게 둘레를 훑습니다.
inline float ringPhase(uint32_t traceIndex) { return std::fmod((float)traceIndex * 2.39996323f, 6.28318531f); }

} // namespace hprop
