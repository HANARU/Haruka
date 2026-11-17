#include "ReflectionCaptureComponent.hpp"
#include "../core/DebugDraw.hpp"
#include "../core/Actor.hpp"
#include "../core/ComponentFields.hpp"
#include "../core/Reflect.hpp"

#include <algorithm>

std::vector<PropDesc> ReflectionCaptureComponent::buildProps() {
    auto* p = this;
    return {
        makePropFloat("Influence Radius", PropColor::Neutral, 0.1f,
            [p]{ return p->radius; }, [p](float v){ p->radius = std::clamp(v, kMinRadius, kMaxRadius); }, PropUnit::Size),
        makePropFloat("Brightness", PropColor::Neutral, 0.01f,
            [p]{ return p->intensity; }, [p](float v){ p->intensity = std::clamp(v, 0.f, 4.f); }, PropUnit::Ratio),
    };
}

void ReflectionCaptureComponent::writeFields(ComponentFieldWriter& w) const {
    w.f("radius",    radius);
    w.f("intensity", intensity);
}

bool ReflectionCaptureComponent::readField(const std::string& key, const std::string& value) {
    float v  = 0.f;
    bool  ok = true;
    if (key == "radius") {
        ok = fieldparse::toFloat(value, v) && v >= kMinRadius && v <= kMaxRadius;
        if (ok) radius = v;
    } else if (key == "intensity") {
        ok = fieldparse::toFloat(value, v) && v >= 0.f && v <= 4.f;
        if (ok) intensity = v;
    } else {
        return false;
    }
    if (!ok) reportBadField(key, value);
    return true;
}

void ReflectionCaptureComponent::drawEditorVisual() const {
    if (!owner) return;
    hdebug::sphere(owner->worldPosition(), radius, { 0.8f, 0.85f, 1.f, 1.f });
    hdebug::sphere(owner->worldPosition(), radius * 0.7f, { 0.8f, 0.85f, 1.f, 0.4f });   // 섞기 시작
}
