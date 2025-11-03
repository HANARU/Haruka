#include "RigidBodyComponent.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../core/Reflect.hpp"
#include <algorithm>

void RigidBodyComponent::onDestroy() {
    if (bodyId_ != 0xFFFFFFFFu && physSys_) {
        physSys_->removeBody(bodyId_);
        bodyId_  = 0xFFFFFFFFu;
        physSys_ = nullptr;
    }
}

std::vector<PropDesc> RigidBodyComponent::buildProps() {
    auto* p = this;

    // Destroy existing body so it gets recreated with new params next tick.
    auto resetIfLive = [p] {
        if (p->bodyId_ != 0xFFFFFFFFu && p->physSys_) {
            p->physSys_->removeBody(p->bodyId_);
            p->bodyId_  = 0xFFFFFFFFu;
            p->physSys_ = nullptr;
        }
        p->createFailed_ = false;
    };

    return {
        makePropEnum("Shape", {"Box", "Sphere", "Capsule"},
            [p]{ return (int)p->shape; },
            [p, resetIfLive](int v){ resetIfLive(); p->shape = (EPhysicsShape)v; }),
        makePropEnum("Motion", {"Static", "Dynamic", "Kinematic"},
            [p]{ return (int)p->motionType; },
            [p, resetIfLive](int v){ resetIfLive(); p->motionType = (EMotionType)v; }),
        makePropFloat("Friction",    PropColor::Neutral, 0.01f,
            [p]{ return p->friction; },
            [p](float v){ p->friction = std::max(0.f, v); }),
        makePropFloat("Restitution", PropColor::Neutral, 0.01f,
            [p]{ return p->restitution; },
            [p](float v){ p->restitution = std::clamp(v, 0.f, 1.f); }),
        makePropFloat("Mass",        PropColor::Neutral, 0.1f,
            [p]{ return p->mass; },
            [p](float v){ p->mass = std::max(0.001f, v); }),
        makePropFloat("HalfX",       PropColor::Red,   0.01f,
            [p]{ return p->halfExtent.x; },
            [p, resetIfLive](float v){ resetIfLive(); p->halfExtent.x = std::max(0.001f, v); }),
        makePropFloat("HalfY",       PropColor::Green, 0.01f,
            [p]{ return p->halfExtent.y; },
            [p, resetIfLive](float v){ resetIfLive(); p->halfExtent.y = std::max(0.001f, v); }),
        makePropFloat("HalfZ",       PropColor::Blue,  0.01f,
            [p]{ return p->halfExtent.z; },
            [p, resetIfLive](float v){ resetIfLive(); p->halfExtent.z = std::max(0.001f, v); }),
    };
}
