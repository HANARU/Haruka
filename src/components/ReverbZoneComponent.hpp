#pragma once
#include "../scene/Component.hpp"
#include "../audio/AudioReverb.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <string>

// ── 잔향 구역 (2026-11-06, Docs/Audio_Design.md 9-5) ─────────────────────────────────────
// 청취자가 이 구역 안에 있으면 공용 리버브의 매개변수(잔향 시간 · damping · 폭 · 크기)를 이 값으로 옮깁니다. UE 의 Audio Volume ·
// Audio Gameplay Volume Reverb, Unity 의 Audio Reverb Zone 과 같은 역할입니다. 판정은 청취자 위치 하나라 물리가 필요 없습니다.
//
//  - 겹치면 우선순위(priority)가 높은 구역, 같으면 더 깊이 들어간 구역(가중치), 그것도 같으면 액터 ID 가 작은 구역입니다.
//  - 경계 밖 blendDistance 안에서는 크기가 1 → 0 으로 줄어듭니다(Unity 의 Min/Max 거리와 같은 역할).
//  - 구역이 바뀌면 새 구역의 fadeTime(시간 상수)으로 매개변수가 따라갑니다(UE 의 Fade Time). 구역을 모두 벗어나면 잔향 크기가 0 으로
//    줄어들고, 0 이 되면 리버브를 멈춥니다(CPU 를 쓰지 않음).
//  - unbounded 를 켜면 위치와 무관한 장면 기본 잔향입니다(야외 등). 우선순위를 낮게 두고 방마다 구역을 겹쳐 씁니다.
//  - 상자 크기 · 구 반지름에는 액터 크기(scale)를 곱하고, 위치 · 회전은 액터를 따릅니다.
enum class EReverbZoneShape : int { Box = 0, Sphere = 1 };

class ReverbZoneComponent : public Component {
public:
    EReverbZoneShape shape         = EReverbZoneShape::Box;
    glm::vec3        halfExtent    { 5.f, 3.f, 5.f };   // 상자 반 크기(m, 액터 크기를 곱하기 전)
    float            radius        = 5.f;                // 구 반지름(m)
    float            blendDistance = 1.f;                // 경계 밖 전환 거리(m)
    int              priority      = 0;
    bool             unbounded     = false;
    float            decayTime     = 1.5f;               // 잔향 시간 RT60(초, verblib 로 만들 수 있는 약 0.61 ~ 10.7 s 로 자름)
    float            damping       = 0.5f;               // 0 ~ 1
    float            width         = 1.f;                // 0 ~ 1
    float            level         = 1.f;                // 잔향 크기(0 ~ 4)
    float            fadeTime      = 0.5f;               // 이 구역으로 바뀔 때의 시간 상수(초)

    static constexpr float kMaxLevel = 4.f;
    static constexpr float kMaxFade  = 10.f;

    // 월드 좌표 청취자 위치에서의 가중치(0 ~ 1). unbounded 면 1 입니다.
    float weightAt(const glm::vec3& worldPoint) const;
    // 이 구역이 정하는 리버브 매개변수(level 은 가중치를 곱하기 전 값).
    hrev::ReverbParams params() const;

    const char*           typeName() const override { return "ReverbZoneComponent"; }
    void                  writeFields(ComponentFieldWriter& w) const override;
    bool                  readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps() override;

    static const char* shapeName(EReverbZoneShape s);
    static bool        shapeFromText(const std::string& t, EReverbZoneShape& out);
};
