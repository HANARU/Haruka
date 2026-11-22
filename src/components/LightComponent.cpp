#include "LightComponent.hpp"
#include "LightChannels.hpp"
#include "../scene/Actor.hpp"
#include "CameraComponent.hpp"
#include "../scene/Actor.hpp"
#include "../scene/Reflect.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/SceneLog.hpp"
#include "../scene/World.hpp"
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
        [p](int v){ p->lightType = static_cast<LightType>(v); }).withDefault(LightType::Directional));

    props.push_back(makePropFloat("Intensity", PropColor::Neutral, 0.01f,
        [p]{ return p->intensity; },
        [p](float v){ p->intensity = std::max(0.f, v); }, PropUnit::Size).withDefault(3.f));

    // 색(선형 RGB 틴트, H3a 에서 R · G · B 세 칸을 색 칸 하나로).
    props.push_back(makePropColor("Color",
        [p]{ return glm::vec4(p->color, 1.f); },
        [p](const glm::vec4& v){ p->color = glm::clamp(glm::vec3(v), glm::vec3(0.f), glm::vec3(1.f)); }).withDefault(glm::vec3(1.f)));

    // 색온도: 토글이 켜졌을 때만 Kelvin 행을 노출(addBoolRow 도 변경 시 재구성).
    props.push_back(makePropToggle("Use Temperature",
        [p]{ return p->useTemperature; }, [p](bool v){ p->useTemperature = v; }).withDefault(false));
    if (useTemperature)
        props.push_back(makePropFloat("Temperature", PropColor::Neutral, 50.f,
            [p]{ return p->temperatureK; },
            [p](float v){ p->temperatureK = std::clamp(v, 1000.f, 15000.f); }).withDefault(6500.f));

    // Attenuation Radius 는 Point/Spot 에서만 의미가 있습니다(방향광은 무한 원거리).
    if (lightType != LightType::Directional)
        props.push_back(makePropFloat("Attenuation Radius", PropColor::Neutral, 0.1f,
            [p]{ return p->attenuationRadius; },
            [p](float v){ p->attenuationRadius = std::max(kMinAttenuationRadius, v); }, PropUnit::Size).withDefault(10.f));

    // Cone Angle 은 Spot 전용.
    if (lightType == LightType::Spot) {
        props.push_back(makePropFloat("Inner Cone Angle", PropColor::Neutral, 0.5f,
            [p]{ return p->innerConeAngle; },
            [p](float v){ p->innerConeAngle = std::clamp(v, 0.f, 89.f); }, PropUnit::Angle).withDefault(15.f));
        props.push_back(makePropFloat("Outer Cone Angle", PropColor::Neutral, 0.5f,
            [p]{ return p->outerConeAngle; },
            [p](float v){ p->outerConeAngle = std::clamp(v, 0.f, 89.f); }, PropUnit::Angle).withDefault(30.f));
    }

    // Cast Shadows 는 실제로 방향광만 섀도맵을 던지므로 Directional 에서만 노출(오해 방지).
    if (lightType == LightType::Directional)
        props.push_back(makePropToggle("Cast Shadows",
            [p]{ return p->castShadows; }, [p](bool v){ p->castShadows = v; }).withDefault(true));
    if (lightType == LightType::Directional)
        props.push_back(makePropFloat("Contact Shadow Length", PropColor::Neutral, 0.01f,
            [p]{ return p->contactShadowLength; },
            [p](float v){ p->contactShadowLength = std::clamp(v, 0.f, 2.f); }, PropUnit::Size).withDefault(0.f));
    hchan::addProps(props, &lightChannels);   // 라이트 채널(2026-11-17)

    return props;
}

glm::vec3 LightComponent::emitDirection() const {
    constexpr glm::vec3 kLocalForward{ 0.f, 0.f, -1.f };
    if (!owner) return kLocalForward;
    return glm::normalize(owner->worldRotation() * kLocalForward);
}

static bool finite3(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// 요 · 피치를 직접 구해 CameraComponent 와 같은 회전 규약(정면 (cos p · sin y, -sin p, -cos p · cos y))으로
// 만듭니다. 최단호 쿼터니언은 롤이 생기고 정반대 벡터를 따로 처리해야 해서 쓰지 않습니다. 피치는 atan2 로
// 구하고(asin 은 천정 근처에서 정밀도를 잃음), 요는 수평 성분이 정확히 0 일 때만 0 으로 둡니다.
glm::quat LightComponent::rotationForEmitDirection(const glm::vec3& e) {
    if (!finite3(e) || !(glm::dot(e, e) > 1e-8f)) return glm::quat(1.f, 0.f, 0.f, 0.f);
    const glm::vec3 n = glm::normalize(e);
    const float h     = std::sqrt(n.x * n.x + n.z * n.z);
    const float pitch = std::atan2(-n.y, h);
    const float yaw   = (h > 0.f) ? std::atan2(n.x, -n.z) : 0.f;
    return CameraComponent::yawPitchRotation(yaw, pitch);
}

std::optional<glm::vec3> LightComponent::legacyEmitDirection(LightType t, const glm::vec3& d) {
    if (t == LightType::Point) return std::nullopt;
    if (!finite3(d) || !(glm::dot(d, d) > 1e-8f)) return std::nullopt;
    const glm::vec3 n = glm::normalize(d);
    return t == LightType::Directional ? -n : n;
}

void LightComponent::foldLegacyDirection(const glm::vec3& d) {
    if (!owner || lightType == LightType::Point) return;
    std::optional<glm::vec3> e = legacyEmitDirection(lightType, d);
    if (!e) {
        HLOG(LogScene, Warning) << "Light on '" << owner->name
            << "' has invalid legacy direction (" << d.x << ", " << d.y << ", " << d.z
            << "); using fallback";
        e = (lightType == LightType::Directional)
            ? glm::vec3(0.f, 1.f, 0.f)
            : glm::vec3(0.f, -1.f, 0.f);
    }
    owner->setLocalRotation(owner->transform().rotation() * rotationForEmitDirection(*e));
}

glm::vec3 LightComponent::effectiveColor() const {
    return useTemperature ? color * kelvinToRGB(temperatureK) : color;
}

// ── 직렬화 (DEF-22) ─────────────────────────────────────────────────────────────
// 종류는 이름으로 씁니다(순번이면 값을 중간에 끼울 때 옛 씬이 다른 종류로 읽힘). 숫자도 받습니다.
static const char* lightTypeName(LightType t) {
    switch (t) {
        case LightType::Point: return "point";
        case LightType::Spot:  return "spot";
        default:               return "directional";
    }
}

static bool lightTypeFromText(const std::string& s, LightType& out) {
    if (s == "directional") { out = LightType::Directional; return true; }
    if (s == "point")       { out = LightType::Point;       return true; }
    if (s == "spot")        { out = LightType::Spot;        return true; }
    int v = -1;
    if (fieldparse::toInt(s, v) && v >= 0 && v <= 2) { out = static_cast<LightType>(v); return true; }
    return false;
}

void LightComponent::writeFields(ComponentFieldWriter& w) const {
    w.s ("type",              lightTypeName(lightType));
    w.v3("color",             color);
    w.f ("intensity",         intensity);
    w.f ("attenuationRadius", attenuationRadius);
    w.f ("innerConeAngle",    innerConeAngle);
    w.f ("outerConeAngle",    outerConeAngle);
    w.b ("useTemperature",    useTemperature);
    w.f ("temperatureK",      temperatureK);
    w.b ("castShadows",       castShadows);
    hchan::write(w, lightChannels);
    if (contactShadowLength > 0.f) w.f("contactShadowLength", contactShadowLength);   // 2026-11-17. 끄면 쓰지 않음
}

bool LightComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if      (key == "type")              ok = lightTypeFromText(value, lightType);
    else if (key == "color")             ok = fieldparse::toVec3 (value, color);
    else if (key == "intensity")         ok = fieldparse::toFloat(value, intensity);
    else if (key == "attenuationRadius") ok = fieldparse::toFloat(value, attenuationRadius);
    else if (key == "innerConeAngle")    ok = fieldparse::toFloat(value, innerConeAngle);
    else if (key == "outerConeAngle")    ok = fieldparse::toFloat(value, outerConeAngle);
    else if (key == "direction") {
        glm::vec3 d{};
        ok = fieldparse::toVec3(value, d);
        if (ok) pendingLegacyDirection_ = d;
        else    pendingLegacyDirection_.reset();
    }
    else if (key == "useTemperature")    ok = fieldparse::toBool (value, useTemperature);
    else if (key == "temperatureK")      ok = fieldparse::toFloat(value, temperatureK);
    else if (key == "castShadows")       ok = fieldparse::toBool (value, castShadows);
    else if (key == "lightChannels")     ok = hchan::read(value, lightChannels);
    else if (key == "contactShadowLength") {
        float v = 0.f;
        ok = fieldparse::toFloat(value, v) && v >= 0.f && v <= 2.f;
        if (ok) contactShadowLength = v;
    }
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}

// 옛 direction 이 있으면 액터 로컬 회전에 접고, 반경은 0 보다 커야 합니다(DEF-35).
// Point · Spot 에서 0 이하(또는 NaN)가 들어오면 최솟값으로 고치고 알립니다.
// Directional 은 반경을 쓰지 않으므로 조용히 고칩니다.
void LightComponent::onFieldsLoaded(const ComponentLoadContext& /*ctx*/) {
    if (pendingLegacyDirection_) {
        const glm::vec3 d = *pendingLegacyDirection_;
        pendingLegacyDirection_.reset();
        foldLegacyDirection(d);
    }
    if (attenuationRadius >= kMinAttenuationRadius) return;
    if (lightType != LightType::Directional)
        HLOG(LogScene, Warning) << "Light on '" << (owner ? owner->name : std::string("?"))
            << "' has attenuation radius " << attenuationRadius << "; clamped to "
            << kMinAttenuationRadius << " m";
    attenuationRadius = kMinAttenuationRadius;
}

std::vector<LightComponent*> LightComponent::enabledIn(const World& world) {
    std::vector<LightComponent*> result;
    for (LightComponent* l : world.componentsOf<LightComponent>())
        if (l->owner->activeInHierarchy() && l->isEnabled())
            result.push_back(l);
    return result;
}
