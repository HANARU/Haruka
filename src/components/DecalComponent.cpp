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
        // 상자 크기(가로 · 투영 깊이 · 세로)와 선형 알베도(H3a 에서 성분 칸 · 색 칸 하나로).
        makePropVec3("Size", 0.01f,
            [p]{ return p->size; }, [p](const glm::vec3& v){ p->size = glm::clamp(v, glm::vec3(0.01f), glm::vec3(100.f)); },
            PropUnit::Size).withDefault(glm::vec3(1.f, 0.5f, 1.f)),
        makePropColor("Color",
            [p]{ return glm::vec4(p->color, 1.f); },
            [p](const glm::vec4& v){ p->color = glm::clamp(glm::vec3(v), glm::vec3(0.f), glm::vec3(1.f)); }, false, false, 0.01f,
            PropUnit::Scalar)
            .withDefault(glm::vec3(0.6f, 0.08f, 0.05f)),
        makePropFloat("Opacity", PropColor::Neutral, 0.01f,
            [p]{ return p->opacity; }, [p](float v){ p->opacity = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio).withDefault(1.f),
        makePropFloat("Roughness", PropColor::Neutral, 0.01f,
            [p]{ return p->roughness; }, [p](float v){ p->roughness = std::clamp(v, -1.f, 1.f); }).withDefault(-1.f),
        makePropEnum("Blend", { "Replace", "Multiply" },
            [p]{ return (int)p->blend; }, [p](int v){ p->blend = (Blend)std::clamp(v, 0, 1); }).withDefault(DecalComponent::Blend::Replace),
        makePropEnum("Shape", { "Circle", "Soft Circle", "Ring", "Square" },
            [p]{ return (int)p->shape; }, [p](int v){ p->shape = (Shape)std::clamp(v, 0, 3); }).withDefault(DecalComponent::Shape::Circle),
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
