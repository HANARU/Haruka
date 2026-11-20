#include "DecalComponent.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/Reflect.hpp"

namespace {
const char* const kBlendNames[] = { "replace", "multiply" };
const char* const kShapeNames[] = { "circle", "softCircle", "ring", "square" };
template <size_t N>
int indexOf(const char* const (&names)[N], const std::string& v) {
    for (size_t i = 0; i < N; ++i) if (v == names[i]) return (int)i;
    return -1;
}
} // namespace

std::vector<PropDesc> DecalComponent::buildProps() {
    auto* p = this;
    return {
        makePropFloat("Size X", PropColor::Red, 0.01f,
            [p]{ return p->size.x; }, [p](float v){ p->size.x = std::clamp(v, 0.01f, 100.f); }, PropUnit::Size),
        makePropFloat("Depth", PropColor::Green, 0.01f,
            [p]{ return p->size.y; }, [p](float v){ p->size.y = std::clamp(v, 0.01f, 100.f); }, PropUnit::Size),
        makePropFloat("Size Z", PropColor::Blue, 0.01f,
            [p]{ return p->size.z; }, [p](float v){ p->size.z = std::clamp(v, 0.01f, 100.f); }, PropUnit::Size),
        makePropFloat("Color R", PropColor::Red, 0.01f,
            [p]{ return p->color.r; }, [p](float v){ p->color.r = std::clamp(v, 0.f, 1.f); }),
        makePropFloat("Color G", PropColor::Green, 0.01f,
            [p]{ return p->color.g; }, [p](float v){ p->color.g = std::clamp(v, 0.f, 1.f); }),
        makePropFloat("Color B", PropColor::Blue, 0.01f,
            [p]{ return p->color.b; }, [p](float v){ p->color.b = std::clamp(v, 0.f, 1.f); }),
        makePropFloat("Opacity", PropColor::Neutral, 0.01f,
            [p]{ return p->opacity; }, [p](float v){ p->opacity = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio),
        makePropFloat("Roughness", PropColor::Neutral, 0.01f,
            [p]{ return p->roughness; }, [p](float v){ p->roughness = std::clamp(v, -1.f, 1.f); }),
        makePropEnum("Blend", { "Replace", "Multiply" },
            [p]{ return (int)p->blend; }, [p](int v){ p->blend = (Blend)std::clamp(v, 0, 1); }),
        makePropEnum("Shape", { "Circle", "Soft Circle", "Ring", "Square" },
            [p]{ return (int)p->shape; }, [p](int v){ p->shape = (Shape)std::clamp(v, 0, 3); }),
    };
}

void DecalComponent::writeFields(ComponentFieldWriter& w) const {
    w.v3("size",      size);
    w.v3("color",     color);
    w.f ("opacity",   opacity);
    w.f ("roughness", roughness);
    w.s ("blend",     kBlendNames[(int)blend]);
    w.s ("shape",     kShapeNames[(int)shape]);
}

bool DecalComponent::readField(const std::string& key, const std::string& value) {
    bool  ok = true;
    float f  = 0.f;
    if (key == "size") {
        glm::vec3 v;
        ok = fieldparse::toVec3(value, v) && v.x >= 0.01f && v.y >= 0.01f && v.z >= 0.01f &&
             v.x <= 100.f && v.y <= 100.f && v.z <= 100.f;
        if (ok) size = v;
    } else if (key == "color") {
        glm::vec3 c;
        ok = fieldparse::toVec3(value, c) && c.r >= 0.f && c.g >= 0.f && c.b >= 0.f && c.r <= 1.f && c.g <= 1.f && c.b <= 1.f;
        if (ok) color = c;
    } else if (key == "opacity") {
        ok = fieldparse::toFloat(value, f) && f >= 0.f && f <= 1.f;
        if (ok) opacity = f;
    } else if (key == "roughness") {
        ok = fieldparse::toFloat(value, f) && f >= -1.f && f <= 1.f;
        if (ok) roughness = f;
    } else if (key == "blend") {
        const int i = indexOf(kBlendNames, value);
        ok = i >= 0;
        if (ok) blend = (Blend)i;
    } else if (key == "shape") {
        const int i = indexOf(kShapeNames, value);
        ok = i >= 0;
        if (ok) shape = (Shape)i;
    } else {
        return false;
    }
    if (!ok) reportBadField(key, value);
    return true;
}
