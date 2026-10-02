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
