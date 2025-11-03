#include "LightComponent.hpp"
#include "../core/Actor.hpp"
#include "../core/Reflect.hpp"
#include <algorithm>
#include <cmath>

// 흑체 색온도(Kelvin) → RGB 틴트. Tanner Helland 근사를 0~1 로 정규화한 것입니다.
// 물리적으로 엄밀한 분광 계산이 아니라 UE 의 상대적 색온도 틴트에 해당하며, 6500K
// 근처가 백색(약 (1,1,1))이라 중성입니다. Haruka 의 무단위 라이팅 철학과 정합합니다.
static glm::vec3 kelvinToRGB(float kelvin) {
    float t = glm::clamp(kelvin, 1000.f, 15000.f) / 100.f;
    float r, g, b;
    if (t <= 66.f) {
        r = 1.f;
        g = glm::clamp(0.39008157876f * std::log(t) - 0.63184144378f, 0.f, 1.f);
    } else {
        r = glm::clamp(1.29293618606f * std::pow(t - 60.f, -0.1332047592f), 0.f, 1.f);
        g = glm::clamp(1.12989086089f * std::pow(t - 60.f, -0.0755148492f), 0.f, 1.f);
    }
    if      (t >= 66.f) b = 1.f;
    else if (t <= 19.f) b = 0.f;
    else                b = glm::clamp(0.54320678911f * std::log(t - 10.f) - 1.19625408914f, 0.f, 1.f);
    return glm::vec3(r, g, b);
}

std::vector<PropDesc> LightComponent::buildProps() {
    auto* p = this;
    std::vector<PropDesc> props;

    // 타입은 드롭다운. addEnumRow 가 값 변경 시 패널을 재구성하므로, 아래 조건부 행들이
    // 새 타입에 맞춰 다시 구성됩니다(UE 처럼 무관한 필드는 숨김).
    props.push_back(makePropEnum("Type", {"Directional", "Point", "Spot"},
        [p]{ return (int)p->lightType; },
        [p](int v){ p->lightType = static_cast<LightType>(v); }));

    props.push_back(makePropFloat("Intensity", PropColor::Neutral, 0.01f,
        [p]{ return p->intensity; },
        [p](float v){ p->intensity = std::max(0.f, v); }));

    props.push_back(makePropFloat("R", PropColor::Red,   0.01f,
        [p]{ return p->color.r; }, [p](float v){ p->color.r = std::clamp(v, 0.f, 1.f); }));
    props.push_back(makePropFloat("G", PropColor::Green, 0.01f,
        [p]{ return p->color.g; }, [p](float v){ p->color.g = std::clamp(v, 0.f, 1.f); }));
    props.push_back(makePropFloat("B", PropColor::Blue,  0.01f,
        [p]{ return p->color.b; }, [p](float v){ p->color.b = std::clamp(v, 0.f, 1.f); }));

    // 색온도: 토글이 켜졌을 때만 Kelvin 행을 노출(addBoolRow 도 변경 시 재구성).
    props.push_back(makePropToggle("Use Temperature",
        [p]{ return p->useTemperature; }, [p](bool v){ p->useTemperature = v; }));
    if (useTemperature)
        props.push_back(makePropFloat("Temperature", PropColor::Neutral, 50.f,
            [p]{ return p->temperatureK; },
            [p](float v){ p->temperatureK = std::clamp(v, 1000.f, 15000.f); }));

    // Attenuation Radius 는 Point/Spot 에서만 의미가 있습니다(방향광은 무한 원거리).
    if (lightType != LightType::Directional)
        props.push_back(makePropFloat("Attenuation Radius", PropColor::Neutral, 0.1f,
            [p]{ return p->attenuationRadius; },
            [p](float v){ p->attenuationRadius = std::max(0.f, v); }));

    // Cone Angle 은 Spot 전용.
    if (lightType == LightType::Spot) {
        props.push_back(makePropFloat("Inner Cone Angle", PropColor::Neutral, 0.5f,
            [p]{ return p->innerConeAngle; },
            [p](float v){ p->innerConeAngle = std::clamp(v, 0.f, 89.f); }));
        props.push_back(makePropFloat("Outer Cone Angle", PropColor::Neutral, 0.5f,
            [p]{ return p->outerConeAngle; },
            [p](float v){ p->outerConeAngle = std::clamp(v, 0.f, 89.f); }));
    }

    // Cast Shadows 는 실제로 방향광만 섀도맵을 던지므로 Directional 에서만 노출(오해 방지).
    if (lightType == LightType::Directional)
        props.push_back(makePropToggle("Cast Shadows",
            [p]{ return p->castShadows; }, [p](bool v){ p->castShadows = v; }));

    return props;
}

// 로컬 direction 에 액터 월드 회전을 적용해 월드 방향을 구합니다. 회전이 항등이면
// 로컬 값 그대로라 기본 씬 조명이 유지되고, 회전 기즈모로 라이트를 돌리면 방향이 따라옵니다.
glm::vec3 LightComponent::worldDirection() const {
    glm::vec3 local = glm::dot(direction, direction) > 1e-8f
                    ? glm::normalize(direction) : glm::vec3(0.f, -1.f, 0.f);
    if (!owner) return local;
    return glm::normalize(owner->worldRotation() * local);
}

glm::vec3 LightComponent::effectiveColor() const {
    return useTemperature ? color * kelvinToRGB(temperatureK) : color;
}
