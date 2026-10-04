#pragma once
#include "../core/IEngineModule.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

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

    // 물리 스텝 주기(Hz). 기본 60 입니다. XR 세션 동안 엔진이 헤드셋 주사율에 맞춥니다(Godot 의 XR
    // 문서 권고). 스텝이 화면보다 느리면 어떤 프레임은 스텝이 없어 움직이는 물체가 떨려 보입니다.
    void  setStepRate(float hz);
    float stepRate() const { return 1.f / step_; }

    // RigidBodyComponent 가 부릅니다(파괴·비활성·재생성). 지운 자리 주변의 잠든 바디를
    // 깨웁니다. Jolt 는 받치던 바디가 사라져도 그 위에서 잠든 바디를 깨우지 않습니다(DEF-80).
    void removeBody(uint32_t id);
    // 살아 있는 바디의 마찰·반발을 바꿉니다(재생성 없음, DEF-81).
    void setMaterial(uint32_t id, float friction, float restitution);
    // 바디를 액터의 지금 월드 위치·회전으로 옮기고 깨웁니다(에디터 트랜스폼 편집 확정).
    void teleport(uint32_t id, const Actor& a);

    // 진단·테스트용 조회. 바디가 없으면 false 를 돌려줍니다.
    struct BodyInfo {
        bool      active      = false;   // 깨어 있는지(잠들면 false)
        float     friction    = 0.f;
        float     restitution = 0.f;
        float     posY        = 0.f;
        glm::vec3 position{ 0.f };
    };
    bool queryBody(uint32_t id, BodyInfo& out) const;

    // ── 물리 API (DEF-19) ─────────────────────────────────────────────────────
    // 규칙(UE 식, 2026-10-04 사용자 결정):
    //  - 힘 · 토크는 부른 프레임 동안 걸립니다. 그 프레임의 충격량(힘 × 프레임 시간)을 그 프레임의
    //    물리 스텝들에 나눠 넣어, 스텝이 몇 번 돌든 같은 양이 들어갑니다. 스텝이 없는 프레임의 몫은 다음
    //    스텝에 넣습니다. 계속 밀려면 매 틱 부릅니다. Jolt 는 힘을 스텝 한 번 뒤에 지우므로, 그대로
    //    넘기면 주사율에 따라 들어가는 양이 달라집니다(90Hz 스텝 · 60Hz 틱이면 들쭉날쭉).
    //  - 충격량은 곧바로 속도를 바꿉니다(한 번).
    //  - 게임 코드가 액터 위치 · 회전을 바꾸면 Dynamic 바디는 그 자리로 순간 이동하고 속도를 유지합니다
    //    (UE · Unity · Godot 의 기본 동작). 예전에는 물리가 매 프레임 덮어써 바꾼 값이 사라졌습니다.
    //  - Kinematic 바디는 액터의 월드 자세를 목표로 그 프레임의 스텝마다 나눠 움직여, 속도를 만들어
    //    부딪힌 물체를 밉니다. 예전에는 스텝 루프 뒤에 한 번 옮겨 한 프레임 늦고, 스텝이 여럿이면
    //    목표를 지나쳤습니다(DEF-99).
    //  - 부모 아래 Dynamic 바디는 월드 자세를 유지합니다(부모가 움직여도 끌려가지 않음, UE 같음).
    // 바디가 아직 없거나(다음 물리 틱에 생김) Static 이면 아무 일도 하지 않습니다.
    void      setGravity(const glm::vec3& g);
    glm::vec3 gravity() const;

    void addForce          (uint32_t body, const glm::vec3& force);                            // N
    void addForceAtPosition(uint32_t body, const glm::vec3& force, const glm::vec3& worldPoint);
    void addTorque         (uint32_t body, const glm::vec3& torque);                           // N·m
    void addImpulse          (uint32_t body, const glm::vec3& impulse);                        // N·s
    void addImpulseAtPosition(uint32_t body, const glm::vec3& impulse, const glm::vec3& worldPoint);
    void      setLinearVelocity (uint32_t body, const glm::vec3& v);   // m/s
    glm::vec3 linearVelocity    (uint32_t body) const;
    void      setAngularVelocity(uint32_t body, const glm::vec3& w);   // rad/s
    glm::vec3 angularVelocity   (uint32_t body) const;

    struct RaycastHit {
        Actor*    actor    = nullptr;
        glm::vec3 point{ 0.f };
        glm::vec3 normal{ 0.f };
        float     distance = 0.f;
    };
    // origin 에서 dir 방향(길이는 상관없음)으로 maxDistance 안에서 가장 가까운 바디를 찾습니다.
    bool raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance, RaycastHit& out) const;
    // 구와 겹치는 바디의 액터를 모읍니다(VR 손으로 잡을 물체 찾기). out 은 비우고 채웁니다.
    void overlapSphere(const glm::vec3& center, float radius, std::vector<Actor*>& out) const;

private:
    std::unique_ptr<JoltImpl> impl_;
    World* world_ = nullptr;

    // Fixed-step accumulator
    float accumulator_ = 0.f;
    float step_        = 1.f / 60.f;

    // 힘 · 토크(DEF-19). frame 은 이번 틱에 받은 합, budget 은 아직 넣지 못한 충격량(합 × 틱 시간)입니다.
    struct ForceTorque { glm::vec3 force{ 0.f }, torque{ 0.f }; };
    std::unordered_map<uint32_t, ForceTorque> frameForces_;
    std::unordered_map<uint32_t, ForceTorque> impulseBudget_;
    bool isDynamic(uint32_t id) const;
    // 상한 진단(DEF-10). 바디 상한에 닿거나 접촉 버퍼가 넘치면 Jolt 는 아무 말이 없으므로
    // 처음 한 번만 알립니다.
    bool bodyCapWarned_     = false;
    bool updateErrorWarned_ = false;
};
