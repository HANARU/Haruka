#pragma once
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
            && maxDistance == o.maxDistance && rolloff == o.rolloff;
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
