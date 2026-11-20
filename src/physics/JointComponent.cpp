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
            [p](int v){ p->type = (EJointType)std::clamp(v, 0, 3); }),
        XPROP_ActorRef(p, "Target", target),
        makePropFloat("AnchorX", PropColor::Red,   0.01f, [p]{ return p->anchor.x; }, [p](float v){ p->anchor.x = v; }, PropUnit::Size),
        makePropFloat("AnchorY", PropColor::Green, 0.01f, [p]{ return p->anchor.y; }, [p](float v){ p->anchor.y = v; }, PropUnit::Size),
        makePropFloat("AnchorZ", PropColor::Blue,  0.01f, [p]{ return p->anchor.z; }, [p](float v){ p->anchor.z = v; }, PropUnit::Size),
        makePropFloat("AxisX",   PropColor::Red,   0.01f, [p]{ return p->axis.x; },   [p](float v){ p->axis.x = v; }),
        makePropFloat("AxisY",   PropColor::Green, 0.01f, [p]{ return p->axis.y; },   [p](float v){ p->axis.y = v; }),
        makePropFloat("AxisZ",   PropColor::Blue,  0.01f, [p]{ return p->axis.z; },   [p](float v){ p->axis.z = v; }),
        makePropFloat("Connected AnchorX", PropColor::Red,   0.01f, [p]{ return p->connectedAnchor.x; }, [p](float v){ p->connectedAnchor.x = v; }, PropUnit::Size),
        makePropFloat("Connected AnchorY", PropColor::Green, 0.01f, [p]{ return p->connectedAnchor.y; }, [p](float v){ p->connectedAnchor.y = v; }, PropUnit::Size),
        makePropFloat("Connected AnchorZ", PropColor::Blue,  0.01f, [p]{ return p->connectedAnchor.z; }, [p](float v){ p->connectedAnchor.z = v; }, PropUnit::Size),
        makePropToggle("Limits", [p]{ return p->limits; }, [p](bool v){ p->limits = v; }),
        makePropFloat("Limit Min", PropColor::Neutral, 0.1f, [p]{ return p->limitMin; }, [p](float v){ p->limitMin = v; }),
        makePropFloat("Limit Max", PropColor::Neutral, 0.1f, [p]{ return p->limitMax; }, [p](float v){ p->limitMax = v; }),
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
