#pragma once
#include <algorithm>
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

struct PropDesc {
    enum Kind { Float, Toggle, Enum };

    const char*                   label   = "";
    glm::vec4                     color   {PropColor::Neutral};
    Kind                          kind    {Float};
    float                         step    {0.01f};
    std::function<float()>        getF;
    std::function<void(float)>    setF;
    std::function<bool()>         getB;
    std::function<void(bool)>     setB;
    std::vector<const char*>      options;   // Enum kind
    std::function<int()>          getI;
    std::function<void(int)>      setI;
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
                               std::function<float()> g, std::function<void(float)> s) {
    PropDesc d;
    d.label = lbl; d.color = col; d.kind = PropDesc::Float; d.step = stp;
    d.getF  = std::move(g); d.setF = std::move(s);
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

#define XPROP_Toggle(p, lbl, field) \
    makePropToggle(lbl, \
        [p]() -> bool { return static_cast<bool>((p)->field); }, \
        [p](bool _v){ (p)->field = static_cast<decltype((p)->field)>(_v); })
