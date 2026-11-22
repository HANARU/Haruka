#include "HeightFogComponent.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/Reflect.hpp"

#include <algorithm>
#include <cmath>

std::vector<PropDesc> HeightFogComponent::buildProps() {
    auto* p = this;
    return {
        makePropFloat("Density", PropColor::Neutral, 0.001f,
            [p]{ return p->density; }, [p](float v){ p->density = std::clamp(v, 0.f, kMaxDensity); }).withDefault(0.02f),
        makePropFloat("Height Falloff", PropColor::Neutral, 0.01f,
            [p]{ return p->heightFalloff; }, [p](float v){ p->heightFalloff = std::clamp(v, 0.001f, kMaxFalloff); }).withDefault(0.2f),
        // 안개 색(선형 HDR, 0 ~ 100, H3a 에서 색 칸 하나로).
        makePropColor("Color",
            [p]{ return glm::vec4(p->color, 1.f); },
            [p](const glm::vec4& v){ p->color = glm::clamp(glm::vec3(v), glm::vec3(0.f), glm::vec3(100.f)); },
            false, false, 0.01f, PropUnit::Scalar).withDefault(glm::vec3(0.45f, 0.52f, 0.6f)),
        makePropFloat("Start Distance", PropColor::Neutral, 0.1f,
            [p]{ return p->startDistance; }, [p](float v){ p->startDistance = std::max(0.f, v); }, PropUnit::Size).withDefault(0.f),
        makePropFloat("Max Opacity", PropColor::Neutral, 0.01f,
            [p]{ return p->maxOpacity; }, [p](float v){ p->maxOpacity = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio).withDefault(1.f),
    };
}

void HeightFogComponent::writeFields(ComponentFieldWriter& w) const {
    w.f ("density",       density);
    w.f ("heightFalloff", heightFalloff);
    w.v3("color",         color);
    w.f ("startDistance", startDistance);
    w.f ("maxOpacity",    maxOpacity);
}

bool HeightFogComponent::readField(const std::string& key, const std::string& value) {
    bool  ok = true;
    float f  = 0.f;
    if (key == "density") {
        ok = fieldparse::toFloat(value, f) && f >= 0.f && f <= kMaxDensity;
        if (ok) density = f;
    } else if (key == "heightFalloff") {
        ok = fieldparse::toFloat(value, f) && f > 0.f && f <= kMaxFalloff;
        if (ok) heightFalloff = f;
    } else if (key == "color") {
        glm::vec3 c;
        ok = fieldparse::toVec3(value, c) && c.r >= 0.f && c.g >= 0.f && c.b >= 0.f &&
             std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b);
        if (ok) color = c;
    } else if (key == "startDistance") {
        ok = fieldparse::toFloat(value, f) && f >= 0.f;
        if (ok) startDistance = f;
    } else if (key == "maxOpacity") {
        ok = fieldparse::toFloat(value, f) && f >= 0.f && f <= 1.f;
        if (ok) maxOpacity = f;
    } else {
        return false;
    }
    if (!ok) reportBadField(key, value);
    return true;
}
