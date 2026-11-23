// 시뮬레이션 층(HarukaSim)만 렌더러 · Vulkan · 창 · XR 없이 링크되는지 검산합니다(E1b, 2026-11-23).
// - ComponentRegistry 에 시뮬레이션 쪽 컴포넌트(physics · audio · nav)가 등록되고 생성되는지 확인합니다.
// - World 에 액터와 RigidBodyComponent 를 붙이고 PhysicsSystem 을 몇 스텝 돌려 중력으로 위치가 바뀌는지 확인합니다.
//
// 실행: build/Release/HarukaSimLinkTest.exe

#include "../src/scene/World.hpp"
#include "../src/scene/Actor.hpp"
#include "../src/scene/ComponentRegistry.hpp"
#include "../src/physics/PhysicsSystem.hpp"
#include "../src/physics/RigidBodyComponent.hpp"
#include "../src/audio/AudioComponent.hpp"
#include "../src/nav/NavAgentComponent.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

} // namespace

int main() {
    std::printf("Haruka 시뮬레이션 층(HarukaSim) 단독 링크 시험 (E1b)\n");

    // 1. 시뮬레이션 컴포넌트 등록 확인(E1a 남은 점)
    registerPhysicsComponents();
    registerAudioComponents();
    registerNavComponents();

    auto& reg = ComponentRegistry::get();
    const char* simTypes[] = {
        "RigidBodyComponent",
        "JointComponent",
        "AudioComponent",
        "ReverbZoneComponent",
        "NavMeshVolumeComponent",
        "NavAgentComponent",
        "AIBrainComponent",
    };
    World probeWorld;
    for (const char* name : simTypes) {
        const ComponentRegistry::Entry* entry = reg.find(name);
        check(entry != nullptr && entry->add != nullptr, std::string("ComponentRegistry 에 ") + name + " 등록됨");
        if (entry && entry->add) {
            Actor* a = probeWorld.spawnActor(name);
            Component* c = entry->add(*a);
            check(c != nullptr && std::string(c->typeName()) == name, std::string(name) + " 인스턴스 생성됨");
        }
    }

    // 2. World + Actor + RigidBodyComponent + PhysicsSystem 스텝 확인
    {
        World world;
        PhysicsSystem physics;
        physics.startup();
        physics.setWorld(&world);

        Actor* box = world.spawnActor("FallingBox");
        box->setLocalPosition({ 0.f, 5.f, 0.f });
        auto* rb = box->addComponent<RigidBodyComponent>();
        rb->shape      = EPhysicsShape::Box;
        rb->motionType = EMotionType::Dynamic;
        rb->halfExtent = { 0.5f, 0.5f, 0.5f };

        const float initialY = box->worldPosition().y;
        for (int i = 0; i < 30; ++i) {
            physics.tick(1.f / 60.f);
        }
        const float finalY = box->worldPosition().y;
        check(rb->hasBody(), "RigidBodyComponent 에 Jolt 물리 바디가 생성됨");
        check(finalY < initialY - 0.1f, "30 스텝 후 중력으로 위치가 내려감 (" +
              std::to_string(initialY) + " -> " + std::to_string(finalY) + ")");

        physics.shutdown();
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
