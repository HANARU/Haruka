#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

namespace PropColor {
    constexpr glm::vec4 Red    {0.9f, 0.3f, 0.3f, 1.f};
    constexpr glm::vec4 Green  {0.3f, 0.9f, 0.3f, 1.f};
    constexpr glm::vec4 Blue   {0.3f, 0.6f, 1.0f, 1.f};
    constexpr glm::vec4 Neutral{0.65f, 0.65f, 0.70f, 1.f};
}

// 숫자 속성의 단위 종류. 디테일 패널의 숫자 칸이 끌기 감도와 표시 자릿수를 이것으로 정합니다(DEF-46).
//   Scalar = step 그대로(px 당), 소수 넷째 자리까지
//   Offset = 위치처럼 0 을 지나는 거리: 1cm/px, 다섯째 자리까지
//   Size   = 늘 양수인 크기(배율·반경·질량 등): 끌기 시작값의 1%/px(최소 step 의 1/10), 다섯째 자리까지
//   Angle  = 각도(도): 0.5°/px, 둘째 자리까지
//   Ratio  = 0~1 비율(색 성분·마찰 등): 0.005/px, 넷째 자리까지
enum class PropUnit { Scalar, Offset, Size, Angle, Ratio };

struct PropDesc {
    // ActorRef(참조 체계 R-7): 다른 액터를 가리키는 값. 디테일 패널은 대상 이름을 보이고 누르면 액터 목록에서 고릅니다.
    enum Kind { Float, Toggle, Enum, ActorRef };

    const char*                   label   = "";
    glm::vec4                     color   {PropColor::Neutral};
    Kind                          kind    {Float};
    float                         step    {0.01f};
    PropUnit                      unit    {PropUnit::Scalar};
    std::function<float()>        getF;
    std::function<void(float)>    setF;
    std::function<bool()>         getB;
    std::function<void(bool)>     setB;
    std::vector<const char*>      options;   // Enum kind
    std::function<int()>          getI;
    std::function<void(int)>      setI;
    std::function<uint64_t()>     getG;      // ActorRef kind: 대상 GUID 값(0 = 없음)
    std::function<void(uint64_t)> setG;
};

// ── Factory helpers ────────────────────────────────────────────────────────────
inline PropDesc makePropEnum(const char* lbl, std::vector<const char*> opts,
                              std::function<int()> g, std::function<void(int)> s) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::Enum;
    d.options = std::move(opts);
    d.getI = std::move(g); d.setI = std::move(s);
    return d;
}

inline PropDesc makePropFloat(const char* lbl, glm::vec4 col, float stp,
                               std::function<float()> g, std::function<void(float)> s,
                               PropUnit unit = PropUnit::Scalar) {
    PropDesc d;
    d.label = lbl; d.color = col; d.kind = PropDesc::Float; d.step = stp; d.unit = unit;
    d.getF  = std::move(g); d.setF = std::move(s);
    return d;
}

// 액터 참조 칸. 값은 대상 GUID(HGuid::value, 0 = 없음)입니다. 필드가 HActorRef 이면 XPROP_ActorRef 를 쓰십시오(scene/ActorRef.hpp).
inline PropDesc makePropActorRef(const char* lbl,
                                 std::function<uint64_t()> g, std::function<void(uint64_t)> s) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::ActorRef;
    d.getG  = std::move(g); d.setG = std::move(s);
    return d;
}

inline PropDesc makePropToggle(const char* lbl,
                                std::function<bool()> g, std::function<void(bool)> s) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::Toggle;
    d.getB  = std::move(g); d.setB = std::move(s);
    return d;
}

// ── X-Macro expansion helpers ─────────────────────────────────────────────────
// Usage pattern in component header:
//   #define MY_PROPS(p) \
//       XPROP_Float(p, "Speed", PropColor::Neutral, 0.1f, speed), \
//       XPROP_Toggle(p, "Active", active)
//
// Usage in component .cpp:
//   std::vector<PropDesc> MyComponent::buildProps() {
//       auto* p = this; return { MY_PROPS(p) };
//   }

#define XPROP_Float(p, lbl, col, stp, field) \
    makePropFloat(lbl, col, stp, \
        [p]() -> float { return static_cast<float>((p)->field); }, \
        [p](float _v){ (p)->field = static_cast<decltype((p)->field)>(_v); })

#define XPROP_FloatMin(p, lbl, col, stp, field, mn) \
    makePropFloat(lbl, col, stp, \
        [p]() -> float { return static_cast<float>((p)->field); }, \
        [p](float _v){ (p)->field = std::max(static_cast<float>(mn), _v); })

#define XPROP_FloatClamp(p, lbl, col, stp, field, mn, mx) \
    makePropFloat(lbl, col, stp, \
        [p]() -> float { return static_cast<float>((p)->field); }, \
        [p](float _v){ (p)->field = std::clamp(_v, static_cast<float>(mn), static_cast<float>(mx)); })

// field 는 HActorRef 여야 합니다(scene/ActorRef.hpp 를 포함한 곳에서 씀).
#define XPROP_ActorRef(p, lbl, field) \
    makePropActorRef(lbl, \
        [p]() -> uint64_t { return (p)->field.guid().value; }, \
        [p](uint64_t _v){ (p)->field.setGuid(HGuid{ _v }); })

#define XPROP_Toggle(p, lbl, field) \
    makePropToggle(lbl, \
        [p]() -> bool { return static_cast<bool>((p)->field); }, \
        [p](bool _v){ (p)->field = static_cast<decltype((p)->field)>(_v); })
