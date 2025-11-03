#pragma once
#include "../core/Component.hpp"
#include <cstdint>

class PhysicsSystem;

enum class EPhysicsShape { Box, Sphere, Capsule };
enum class EMotionType   { Static, Dynamic, Kinematic };

struct HalfExt { float x = 0.5f, y = 0.5f, z = 0.5f; };

class RigidBodyComponent : public Component {
public:
    EPhysicsShape shape      = EPhysicsShape::Box;
    EMotionType   motionType = EMotionType::Dynamic;
    float         friction    = 0.5f;
    float         restitution = 0.3f;
    float         mass        = 1.f;
    HalfExt       halfExtent;   // used for Box; radius = halfExtent.x for Sphere/Capsule

    void onDestroy() override;

    const char*           typeName()     const override { return "RigidBodyComponent"; }
    std::vector<PropDesc> buildProps()         override;

    // Set by PhysicsSystem — do not touch.
    // Reset createFailed_ to false after fixing halfExtent to trigger a retry.
    uint32_t       bodyId_       = 0xFFFFFFFFu;
    PhysicsSystem* physSys_      = nullptr;
    bool           createFailed_ = false;
};
