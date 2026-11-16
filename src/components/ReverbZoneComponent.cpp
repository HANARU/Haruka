#include "ReverbZoneComponent.hpp"
#include "../core/Actor.hpp"
#include "../core/ComponentFields.hpp"
#include "../core/Reflect.hpp"

#include <algorithm>
#include <cmath>

float ReverbZoneComponent::weightAt(const glm::vec3& worldPoint) const {
    if (unbounded) return 1.f;
    if (!owner) return 0.f;
    // 위치 · 회전만 되돌리고, 크기는 구역 쪽 길이에 곱합니다. 그래서 전환 거리가 액터 크기와 무관하게 미터입니다.
    const glm::vec3 local = glm::conjugate(owner->worldRotation()) * (worldPoint - owner->worldPosition());
    const glm::vec3 s     = glm::abs(owner->worldScale());
    if (shape == EReverbZoneShape::Sphere)
        return hrev::sphereWeight(local, radius * std::max({ s.x, s.y, s.z }), blendDistance);
    return hrev::boxWeight(local, halfExtent * s, blendDistance);
}

hrev::ReverbParams ReverbZoneComponent::params() const {
    hrev::ReverbParams p;
    p.decayTime = decayTime;
    p.damping   = damping;
    p.width     = width;
    p.level     = level;
    return p;
}

std::vector<PropDesc> ReverbZoneComponent::buildProps() {
    auto* p = this;
    std::vector<PropDesc> props;
    props.push_back(makePropToggle("Unbounded",
        [p]{ return p->unbounded; }, [p](bool v){ p->unbounded = v; }));
    if (!unbounded) {
        props.push_back(makePropEnum("Shape", { "Box", "Sphere" },
            [p]{ return (int)p->shape; }, [p](int v){ p->shape = (EReverbZoneShape)std::clamp(v, 0, 1); }));
        if (shape == EReverbZoneShape::Box) {
            props.push_back(makePropFloat("HalfX", PropColor::Red, 0.05f,
                [p]{ return p->halfExtent.x; }, [p](float v){ p->halfExtent.x = std::max(0.01f, v); }, PropUnit::Size));
            props.push_back(makePropFloat("HalfY", PropColor::Green, 0.05f,
                [p]{ return p->halfExtent.y; }, [p](float v){ p->halfExtent.y = std::max(0.01f, v); }, PropUnit::Size));
            props.push_back(makePropFloat("HalfZ", PropColor::Blue, 0.05f,
                [p]{ return p->halfExtent.z; }, [p](float v){ p->halfExtent.z = std::max(0.01f, v); }, PropUnit::Size));
        } else {
            props.push_back(makePropFloat("Radius", PropColor::Neutral, 0.05f,
                [p]{ return p->radius; }, [p](float v){ p->radius = std::max(0.01f, v); }, PropUnit::Size));
        }
        props.push_back(makePropFloat("Blend Distance", PropColor::Neutral, 0.05f,
            [p]{ return p->blendDistance; }, [p](float v){ p->blendDistance = std::max(0.f, v); }, PropUnit::Size));
    }
    props.push_back(makePropFloat("Priority", PropColor::Neutral, 1.f,
        [p]{ return (float)p->priority; }, [p](float v){ p->priority = (int)std::lround(v); }));
    props.push_back(makePropFloat("Decay Time", PropColor::Neutral, 0.05f,
        [p]{ return p->decayTime; }, [p](float v){ p->decayTime = std::clamp(v, hrev::kMinDecay, hrev::kMaxDecay); }));
    props.push_back(makePropFloat("Damping", PropColor::Neutral, 0.01f,
        [p]{ return p->damping; }, [p](float v){ p->damping = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio));
    props.push_back(makePropFloat("Width", PropColor::Neutral, 0.01f,
        [p]{ return p->width; }, [p](float v){ p->width = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio));
    props.push_back(makePropFloat("Level", PropColor::Neutral, 0.01f,
        [p]{ return p->level; }, [p](float v){ p->level = std::clamp(v, 0.f, kMaxLevel); }, PropUnit::Ratio));
    props.push_back(makePropFloat("Fade Time", PropColor::Neutral, 0.05f,
        [p]{ return p->fadeTime; }, [p](float v){ p->fadeTime = std::clamp(v, 0.f, kMaxFade); }));
    return props;
}

const char* ReverbZoneComponent::shapeName(EReverbZoneShape s) {
    return s == EReverbZoneShape::Sphere ? "sphere" : "box";
}

bool ReverbZoneComponent::shapeFromText(const std::string& t, EReverbZoneShape& out) {
    if (t == "box")    { out = EReverbZoneShape::Box;    return true; }
    if (t == "sphere") { out = EReverbZoneShape::Sphere; return true; }
    return false;
}

// ── 직렬화 (DEF-22): 이름은 멤버 이름을 따르고, 모양은 이름으로 씁니다(DEF-44) ──────────────────────
void ReverbZoneComponent::writeFields(ComponentFieldWriter& w) const {
    w.s ("shape",         shapeName(shape));
    w.v3("halfExtent",    halfExtent);
    w.f ("radius",        radius);
    w.f ("blendDistance", blendDistance);
    w.i ("priority",      priority);
    w.b ("unbounded",     unbounded);
    w.f ("decayTime",     decayTime);
    w.f ("damping",       damping);
    w.f ("width",         width);
    w.f ("level",         level);
    w.f ("fadeTime",      fadeTime);
}

bool ReverbZoneComponent::readField(const std::string& key, const std::string& value) {
    // 값이 깨졌거나 범위 밖이면 기본값을 두고 알립니다(소리 자체는 그대로).
    auto ranged = [&](float& field, float lo, float hi) {
        float v;
        if (!fieldparse::toFloat(value, v) || !(v >= lo && v <= hi)) return false;
        field = v;
        return true;
    };
    bool ok = true;
    if      (key == "shape")         ok = shapeFromText(value, shape);
    else if (key == "halfExtent") {
        glm::vec3 h;
        ok = fieldparse::toVec3(value, h) && h.x > 0.f && h.y > 0.f && h.z > 0.f;
        if (ok) halfExtent = h;
    }
    else if (key == "radius")        ok = ranged(radius, 1e-3f, 1e6f);
    else if (key == "blendDistance") ok = ranged(blendDistance, 0.f, 1e6f);
    else if (key == "priority")      ok = fieldparse::toInt(value, priority);
    else if (key == "unbounded")     ok = fieldparse::toBool(value, unbounded);
    else if (key == "decayTime")     ok = ranged(decayTime, 0.f, 1e3f);   // 만들 수 있는 범위는 쓸 때 자릅니다
    else if (key == "damping")       ok = ranged(damping, 0.f, 1.f);
    else if (key == "width")         ok = ranged(width, 0.f, 1.f);
    else if (key == "level")         ok = ranged(level, 0.f, kMaxLevel);
    else if (key == "fadeTime")      ok = ranged(fadeTime, 0.f, kMaxFade);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}
