#pragma once
#include "../core/IEngineModule.hpp"
#include <memory>

class World;
struct JoltImpl;

class PhysicsSystem : public IEngineModule {
public:
    PhysicsSystem();
    ~PhysicsSystem();
    void        startup()      override;
    void        shutdown()     override;
    void        tick(float dt) override;
    void        onBeginPlay()  override;
    const char* name() const   override { return "PhysicsSystem"; }

    void setWorld(World* w) { world_ = w; }

    // Called by RigidBodyComponent::onDestroy()
    void removeBody(uint32_t id);

private:
    std::unique_ptr<JoltImpl> impl_;
    World* world_ = nullptr;

    // Fixed-step accumulator
    float accumulator_ = 0.f;
    static constexpr float kStep = 1.f / 60.f;
};
