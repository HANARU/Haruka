#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

// ── 3D 사운드 감쇠 설정 ───────────────────────────────────────────────────────
// AudioComponent 가 들고 있고, AudioSystem 이 매 틱 miniaudio 사운드에 그대로 넘깁니다.
// 거리 의미는 miniaudio(= OpenAL 거리 모델)를 따르므로 이름도 실제 의미대로 붙였습니다.
// UE 의 Inner Radius / Falloff Distance 는 "Falloff 밖은 무음"을 뜻하는데, miniaudio 의
// Inverse·Exponential 은 Max Distance 밖에서 줄지 않고 그 값에 머뭅니다. 이름만 빌리면
// 의미가 어긋나므로 Min/Max Distance 를 씁니다(Docs/Audio_Design.md 3절).

// 값은 miniaudio 의 ma_attenuation_model 과 같습니다. miniaudio 의 none(0)은 거리 감쇠뿐
// 아니라 좌우 패닝까지 꺼서 Spatialize 를 끈 것과 같으므로 목록에 두지 않습니다. 방향은
// 살리고 거리 감쇠만 없애려면 Rolloff 를 0 으로 둡니다. d 는 [min, max] 로 자른 거리입니다.
enum class EAudioAttenuation : int {
    Inverse     = 1,   // min / (min + rolloff·(d − min))
    Linear      = 2,   // 1 − rolloff·(d − min) / (max − min), 0 아래는 0
    Exponential = 3,   // (d / min)^(−rolloff)
};

struct AudioAttenuationSettings {
    bool              spatialize  = true;    // 끄면 2D: 위치와 무관하게 감쇠·패닝 없이 들립니다
    EAudioAttenuation model       = EAudioAttenuation::Inverse;
    float             minDistance = 1.f;     // 이 거리 안에서는 줄지 않습니다(m)
    float             maxDistance = 100.f;   // 이 거리 밖에서는 더 줄지 않습니다(m)
    float             rolloff     = 1.f;     // 곡선 기울기, 0 이면 거리 감쇠가 없습니다
    // 최대 거리 밖은 무음(2026-11-17, UE 의 Falloff Distance 의미). 끄면(기본, miniaudio · OpenAL) 최대 거리 밖에서 그 거리의
    // 값에 머뭅니다(Inverse 기본값이면 −40 dB). 켜면 곡선을 (g − g_max) / (1 − g_max) 로 다시 맞춰 최대 거리에서 정확히 0 이
    // 되게 합니다(모양은 같고 끝만 0 으로 이어짐). 씬 파일에는 AUDIO_3D 줄을 늘리지 않고 다른 키(silentBeyondMax)로 씁니다.
    bool              silentBeyondMax = false;

    // 에디터 입력의 하한과 Min/Max 사이 최소 간격입니다. miniaudio 는 min >= max 이면
    // 감쇠를 통째로 끄므로, 둘이 겹치지 않게 세터가 이 간격을 지킵니다.
    static constexpr float kMinDistanceFloor = 0.01f;
    static constexpr float kMinGap           = 0.01f;

    // 씬 파일 AUDIO_3D 값입니다. 예: "1 inverse 1 100 1"
    // (spatialize, 모델 이름, min, max, rolloff). 모델은 순번이 아니라 이름으로 씁니다.
    std::string toText() const;
    // 다섯 토큰이 모두 있고 범위가 맞아야 true 입니다(0 < min < max, rolloff >= 0, 유한값).
    // 실패하면 out 을 건드리지 않습니다.
    static bool fromText(std::string_view text, AudioAttenuationSettings& out);

    static const char* modelName(EAudioAttenuation m);                        // "inverse" 등
    static bool        modelFromName(std::string_view name, EAudioAttenuation& out);

    bool operator==(const AudioAttenuationSettings& o) const {
        return spatialize == o.spatialize && model == o.model && minDistance == o.minDistance
            && maxDistance == o.maxDistance && rolloff == o.rolloff && silentBeyondMax == o.silentBeyondMax;
    }
};

// ── 소리 차폐 설정 (2026-11-06, Docs/Audio_Design.md 9-4) ──────────────────────
// 청취자와 음원 사이를 물리 충돌체(RigidBody 의 상자 · 구 · 캡슐)가 막으면 소리를 줄이고 먹먹하게(저역 통과) 합니다. UE 내장
// 차폐(Sound Attenuation 의 Occlusion)와 같은 항목이고, 반지름을 주면 Steam Audio 의 Volumetric 처럼 음원 주변 여러 점으로
// 일부만 막힌 정도를 구합니다(모서리를 돌 때 소리가 서서히 바뀌어 회절을 흉내 냄). 공간화를 끈 소리(2D)에는 걸리지 않습니다.
struct AudioOcclusionSettings {
    bool  enabled    = false;    // 기본 끔: 기존 씬 · 소리가 바뀌지 않게
    float lowPassHz  = 1200.f;   // 완전히 막혔을 때의 저역 통과 차단 주파수(Hz). 열려 있을 때는 필터를 거치지 않습니다
    float volume     = 0.4f;     // 완전히 막혔을 때의 볼륨 배율(0.4 ≈ -8 dB)
    float smoothTime = 0.1f;     // 막힘 ↔ 열림 전환의 시간 상수(초). 0 이면 판정 즉시 바뀝니다
    float radius     = 0.f;      // 음원 반지름(m). 0 = 레이 하나(막힘/열림 둘 중 하나), 0 보다 크면 원판 위 표본 9개(일부 차폐)

    static constexpr float kMinHz     = 20.f;
    static constexpr float kMaxHz     = 20000.f;
    static constexpr float kMaxSmooth = 10.f;
    static constexpr float kMaxRadius = 10.f;

    // 씬 파일 값입니다. 예: "1 1200 0.4 0.1 0.5" (enabled, lowPassHz, volume, smoothTime, radius).
    // AUDIO_3D 와 같이 토큰 수를 정확히 요구하고, 값이 늘면 다른 키로 추가합니다.
    std::string toText() const;
    // 다섯 토큰이 모두 있고 범위가 맞아야 true 입니다(20 ≤ lowPassHz ≤ 20000, 0 ≤ volume ≤ 1, 0 ≤ smoothTime ≤ 10,
    // 0 ≤ radius ≤ 10, 유한값). 실패하면 out 을 건드리지 않습니다.
    static bool fromText(std::string_view text, AudioOcclusionSettings& out);

    bool operator==(const AudioOcclusionSettings& o) const {
        return enabled == o.enabled && lowPassHz == o.lowPassHz && volume == o.volume
            && smoothTime == o.smoothTime && radius == o.radius;
    }
};

// ── 들리지 않는 소리의 가상화 (수학 보강 S3, 2026-11-17) ─────────────────────────────
// 예상 게인(볼륨 × 거리 감쇠 × 차폐)이 kVirtualGain(−60 dB) 아래로 내려가면 보이스를 멈춰 믹싱(디코딩 · 공간화 · 필터 · HRTF)을
// 하지 않고 재생 위치만 셉니다. kRestoreGain(약 −54 dB) 위로 올라오면 센 위치로 되감아 이어 냅니다(6 dB 이력 띠: 경계에서
// 프레임마다 멈췄다 켜지지 않게). FMOD 의 vol0virtualvol, UE 의 가상 보이스와 같은 생각입니다. 공간화를 끈 소리(2D)는
// 거리와 무관하므로 가상화하지 않습니다. 콘 · 도플러는 지금 쓰지 않아 계산에 넣지 않습니다.
namespace hvirt {

constexpr float kVirtualGain = 0.001f;   // −60 dB
constexpr float kRestoreGain = 0.002f;   // 약 −54 dB

// miniaudio 가 직접 내는 거리 감쇠와 같은 식(ma_attenuation_inverse · linear · exponential, 엔진 기본 최소 · 최대 게인 0 · 1 로
// 자름, silentBeyondMax 는 보지 않음). minDistance ≥ maxDistance 면 miniaudio 처럼 줄이지 않습니다(1).
inline float miniaudioGain(const AudioAttenuationSettings& a, float distance) {
    if (!a.spatialize) return 1.f;
    if (a.minDistance >= a.maxDistance) return 1.f;
    const float d = std::clamp(distance, a.minDistance, a.maxDistance);
    float g = 1.f;
    switch (a.model) {
        case EAudioAttenuation::Inverse:     g = a.minDistance / (a.minDistance + a.rolloff * (d - a.minDistance)); break;
        case EAudioAttenuation::Linear:      g = 1.f - a.rolloff * (d - a.minDistance) / (a.maxDistance - a.minDistance); break;
        case EAudioAttenuation::Exponential: g = std::pow(d / a.minDistance, -a.rolloff); break;
    }
    return std::clamp(g, 0.f, 1.f);
}

// 실제로 들리는 거리 감쇠: silentBeyondMax 면 최대 거리에서 0 이 되도록 다시 맞춘 값, 아니면 miniaudioGain 과 같습니다.
// rolloff 0 처럼 최대 거리에서도 줄지 않는 곡선(g_max = 1)은 최대 거리 안 1, 밖 0 입니다.
inline float distanceGain(const AudioAttenuationSettings& a, float distance) {
    const float g = miniaudioGain(a, distance);
    if (!a.spatialize || !a.silentBeyondMax || a.minDistance >= a.maxDistance) return g;
    if (distance >= a.maxDistance) return 0.f;
    const float gMax = miniaudioGain(a, a.maxDistance);
    if (gMax >= 1.f) return 1.f;
    return std::clamp((g - gMax) / (1.f - gMax), 0.f, 1.f);
}

// miniaudio 가 낸 게인에 곱할 배율(distanceGain ÷ miniaudioGain). 엔진은 이 배율을 소리 볼륨에 곱해 무음 곡선을 만듭니다.
inline float silenceFactor(const AudioAttenuationSettings& a, float distance) {
    if (!a.silentBeyondMax) return 1.f;
    const float raw = miniaudioGain(a, distance);
    return raw > 0.f ? distanceGain(a, distance) / raw : 0.f;
}

// 지금 가상인지와 예상 게인으로 다음 상태(가상이면 true)를 정합니다.
inline bool nextVirtual(bool isVirtual, float gain) {
    return isVirtual ? !(gain > kRestoreGain) : gain < kVirtualGain;
}

// 가상 재생 위치를 frames 만큼 옮깁니다. 반복이면 길이로 접고, 아니면 끝에 닿았을 때 ended 를 세웁니다(위치는 길이).
inline double advanceCursor(double cursor, double frames, uint64_t length, bool loop, bool& ended) {
    ended = false;
    if (length == 0) return cursor;
    const double c   = cursor + std::max(0.0, frames);
    const double len = (double)length;
    if (loop) return std::fmod(c, len);
    if (c >= len) { ended = true; return len; }
    return c;
}

} // namespace hvirt
