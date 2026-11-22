#include "JointComponent.hpp"
#include "PhysicsSystem.hpp"
#include "../scene/Reflect.hpp"
#include "../scene/ComponentFields.hpp"
#include <algorithm>
#include <cmath>

const char* JointComponent::typeNameOf(EJointType t) {
    switch (t) {
        case EJointType::Fixed:    return "fixed";
        case EJointType::Slider:   return "slider";
        case EJointType::Distance: return "distance";
        default:                   return "hinge";
    }
}

bool JointComponent::typeFromText(const std::string& s, EJointType& out) {
    if (s == "fixed")    { out = EJointType::Fixed;    return true; }
    if (s == "hinge")    { out = EJointType::Hinge;    return true; }
    if (s == "slider")   { out = EJointType::Slider;   return true; }
    if (s == "distance") { out = EJointType::Distance; return true; }
    return false;
}

void JointComponent::rebuild() {
    if (physSys_ && handle_ != 0) physSys_->removeJoint(handle_);
    handle_  = 0;
    physSys_ = nullptr;
}

void JointComponent::onDestroy()       { rebuild(); }
void JointComponent::onDisable()       { rebuild(); }
void JointComponent::onPropsCommitted() { rebuild(); }

float JointComponent::currentValue() const {
    return hasJoint() ? physSys_->jointValue(handle_) : 0.f;
}

std::vector<PropDesc> JointComponent::buildProps() {
    auto* p = this;
    return {
        makePropEnum("Type", { "Fixed", "Hinge", "Slider", "Distance" },
            [p]{ return (int)p->type; },
            [p](int v){ p->type = (EJointType)std::clamp(v, 0, 3); }).withDefault(EJointType::Hinge),
        XPROP_ActorRef(p, "Target", target),
        // 성분 칸(H3a 에서 X · Y · Z 세 칸을 하나로, 단위는 예전 그대로).
        makePropVec3("Anchor", 0.01f, [p]{ return p->anchor; }, [p](const glm::vec3& v){ p->anchor = v; }, PropUnit::Size)
            .withDefault(glm::vec3(0.f)),
        makePropVec3("Axis", 0.01f, [p]{ return p->axis; }, [p](const glm::vec3& v){ p->axis = v; })
            .withDefault(glm::vec3(0.f, 1.f, 0.f)),
        makePropVec3("Connected Anchor", 0.01f, [p]{ return p->connectedAnchor; }, [p](const glm::vec3& v){ p->connectedAnchor = v; },
                     PropUnit::Size).withDefault(glm::vec3(0.f)),
        makePropToggle("Limits", [p]{ return p->limits; }, [p](bool v){ p->limits = v; }).withDefault(false),
        makePropFloat("Limit Min", PropColor::Neutral, 0.1f, [p]{ return p->limitMin; }, [p](float v){ p->limitMin = v; }).withDefault(-45.f),
        makePropFloat("Limit Max", PropColor::Neutral, 0.1f, [p]{ return p->limitMax; }, [p](float v){ p->limitMax = v; }).withDefault(45.f),
    };
}

void JointComponent::writeFields(ComponentFieldWriter& w) const {
    w.s ("type", typeNameOf(type));
    w.s ("target", target.toText());
    w.v3("anchor", anchor);
    w.v3("axis", axis);
    if (connectedAnchor != glm::vec3(0.f)) w.v3("connectedAnchor", connectedAnchor);
    w.b ("limits", limits);
    w.f ("limitMin", limitMin);
    w.f ("limitMax", limitMax);
}

bool JointComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    auto vec = [&](glm::vec3& dst) {
        glm::vec3 v;
        ok = fieldparse::toVec3(value, v) && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        if (ok) dst = v;
    };
    auto num = [&](float& dst) {
        float v = 0.f;
        ok = fieldparse::toFloat(value, v) && std::isfinite(v);
        if (ok) dst = v;
    };
    if      (key == "type")            ok = typeFromText(value, type);
    else if (key == "target")          ok = HActorRef::fromText(value, target);
    else if (key == "anchor")          vec(anchor);
    else if (key == "axis")            vec(axis);
    else if (key == "connectedAnchor") vec(connectedAnchor);
    else if (key == "limits")          ok = fieldparse::toBool(value, limits);
    else if (key == "limitMin")        num(limitMin);
    else if (key == "limitMax")        num(limitMax);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}
