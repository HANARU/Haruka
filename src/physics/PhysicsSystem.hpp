#pragma once
#include "../core/IEngineModule.hpp"
#include <memory>

class World;
class Actor;
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

    // RigidBodyComponent 가 부릅니다(파괴·비활성·재생성). 지운 자리 주변의 잠든 바디를
    // 깨웁니다. Jolt 는 받치던 바디가 사라져도 그 위에서 잠든 바디를 깨우지 않습니다(DEF-80).
    void removeBody(uint32_t id);
    // 살아 있는 바디의 마찰·반발을 바꿉니다(재생성 없음, DEF-81).
    void setMaterial(uint32_t id, float friction, float restitution);
    // 바디를 액터의 지금 월드 위치·회전으로 옮기고 깨웁니다(에디터 트랜스폼 편집 확정).
    void teleport(uint32_t id, const Actor& a);

    // 진단·테스트용 조회. 바디가 없으면 false 를 돌려줍니다.
    struct BodyInfo {
        bool  active      = false;   // 깨어 있는지(잠들면 false)
        float friction    = 0.f;
        float restitution = 0.f;
        float posY        = 0.f;
    };
    bool queryBody(uint32_t id, BodyInfo& out) const;

private:
    std::unique_ptr<JoltImpl> impl_;
    World* world_ = nullptr;

    // Fixed-step accumulator
    float accumulator_ = 0.f;
    static constexpr float kStep = 1.f / 60.f;
    // 상한 진단(DEF-10). 바디 상한에 닿거나 접촉 버퍼가 넘치면 Jolt 는 아무 말이 없으므로
    // 처음 한 번만 알립니다.
    bool bodyCapWarned_     = false;
    bool updateErrorWarned_ = false;
};
