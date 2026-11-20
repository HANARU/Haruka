#pragma once
#include "../core/IEngineModule.hpp"
#include "PhysicsLayers.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <span>
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

    // 시뮬레이션할 월드. startup ~ shutdown 사이에는 그 월드의 서비스로 자신을 넣습니다(world.service<PhysicsSystem>(),
    // 2026-11-18 S3). 컴포넌트(리그 이동 · 텔레포트)는 owner->world() 로 찾습니다. 예전 정적 포인터 current() 자리입니다.
    void setWorld(World* w);

    // 물리 스텝 주기(Hz). 기본 60 입니다. XR 세션 동안 엔진이 헤드셋 주사율에 맞춥니다(Godot 의 XR
    // 문서 권고). 스텝이 화면보다 느리면 어떤 프레임은 스텝이 없어 움직이는 물체가 떨려 보입니다.
    void  setStepRate(float hz);

    // 바디 상한(DEF-10, Config/engine.ini 의 physics.maxBodies). startup 전에 부릅니다(모듈 등록 전).
    void     setMaxBodies(uint32_t n) { maxBodies_ = n; }
    uint32_t maxBodies() const        { return maxBodies_; }
    float stepRate() const { return 1.f / step_; }

    // 충돌 레이어 표(2026-11-20, Docs/Physics_Joints_And_Events.md D1). 엔진이 startup 전에 project.ini 의 physics. 줄로 넣습니다.
    // 몸을 만든 뒤에 바꾸면 이미 있는 몸의 레이어 쌍 판정은 다음 스텝부터 새 표를 따릅니다.
    void                     setLayers(const PhysicsLayerTable& t) { layers_ = t; }
    const PhysicsLayerTable& layers() const { return layers_; }
    // 살아 있는 몸의 레이어(0 ~ 15)와 접촉 알림 켬을 바꿉니다(RigidBodyComponent 의 칸이 부름).
    void setLayer(uint32_t body, int layer);
    void setContactEvents(uint32_t body, bool on);

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
    // 질량 중심의 월드 위치(Jolt 는 선속도를 이 점의 속도로 다룹니다). 바디가 없으면 false.
    bool      centerOfMass      (uint32_t body, glm::vec3& out) const;
    // 연속 충돌(Jolt EMotionQuality::LinearCast)을 켜고 끕니다. 바디가 없으면 아무 일도 하지 않습니다.
    void      setContinuous     (uint32_t body, bool on);
    // 중력 배율(Jolt GravityFactor, 1 = 보통). 속도 추적으로 잡은 물체가 손 아래로 처지지 않게 0 으로 둡니다(P5).
    void      setGravityFactor  (uint32_t body, float factor);
    bool      isContinuous      (uint32_t body) const;
    // Dynamic ↔ Kinematic 을 바디를 다시 만들지 않고 바꿉니다(Jolt SetMotionType, 2026-10-15). VR 손으로 잡는 동안
    // Kinematic 으로 손을 따라가게 했다가, 놓는 순간 Dynamic 으로 돌리고 손 속도를 넣는 데 씁니다(바디를 다시 만들면
    // 다음 틱까지 바디가 없어 속도를 넣을 수 없음). Static 바디와 오가지는 않습니다. kinematic 이면 true.
    void      setMotionKinematic(uint32_t body, bool kinematic);

    struct RaycastHit {
        Actor*    actor    = nullptr;
        glm::vec3 point{ 0.f };
        glm::vec3 normal{ 0.f };
        float     distance = 0.f;
    };
    // origin 에서 dir 방향(길이는 상관없음)으로 maxDistance 안에서 가장 가까운 바디를 찾습니다.
    // ignore 에 든 액터의 바디는 건너뜁니다(2026-11-06). 소리 차폐가 음원 자신 · 청취자 리그의 충돌체를 빼는 데 씁니다.
    // 액터 단위이므로 자식 액터의 바디는 따로 넣어야 합니다. 볼록 셰이프 안에서 출발한 레이는 그 셰이프에 거리 0 으로
    // 맞으므로(Jolt 의 기본 동작), 출발점을 감싼 바디는 빼야 그 너머를 봅니다. nullptr 항목은 무시합니다.
    // layerMask 는 맞출 레이어의 비트(PhysicsLayerTable::bit)입니다. 트리거는 맞추지 않습니다(D3).
    bool raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance, RaycastHit& out,
                 std::span<Actor* const> ignore = {}, uint16_t layerMask = PhysicsLayerTable::kAllMask) const;
    // 구와 겹치는 바디의 액터를 모읍니다(VR 손으로 잡을 물체 찾기). out 은 비우고 채웁니다. 트리거는 빼냅니다.
    void overlapSphere(const glm::vec3& center, float radius, std::vector<Actor*>& out,
                       uint16_t layerMask = PhysicsLayerTable::kAllMask) const;

    // ── 셰이프 캐스트(스윕, D6) ───────────────────────────────────────────────────────────────────────
    // 모양을 start(중심) · rotation 자세에서 dir 방향으로 maxDistance 만큼 훑어 가장 가까운 첫 충돌을 찾습니다. 시작부터 겹치면 거리 0.
    // 캡슐은 로컬 Y 축(충돌체 규약과 같음). 트리거 · ignore 액터의 몸 · layerMask 밖의 레이어는 건너뜁니다. out.normal 은 맞은 면에서 바깥.
    struct SweepShape {
        enum class Kind : uint8_t { Sphere, Box, Capsule } kind = Kind::Sphere;
        glm::vec3 halfExtent{ 0.5f };   // Box
        float     radius     = 0.5f;    // Sphere · Capsule
        float     halfHeight = 0.5f;    // Capsule 원통부 반높이
        static SweepShape sphere(float r)                  { SweepShape s; s.kind = Kind::Sphere;  s.radius = r; return s; }
        static SweepShape box(const glm::vec3& he)         { SweepShape s; s.kind = Kind::Box;     s.halfExtent = he; return s; }
        static SweepShape capsule(float r, float halfH)    { SweepShape s; s.kind = Kind::Capsule; s.radius = r; s.halfHeight = halfH; return s; }
    };
    bool shapeCast(const SweepShape& shape, const glm::vec3& start, const glm::quat& rotation, const glm::vec3& dir,
                   float maxDistance, RaycastHit& out, uint16_t layerMask = PhysicsLayerTable::kAllMask,
                   std::span<Actor* const> ignore = {}) const;

    // ── 조인트(D5) ────────────────────────────────────────────────────────────────────────────────────
    // JointComponent 는 물리 틱이 만듭니다. 핸들로 지우고 지금 값을 읽습니다(경첩 각도 도 · 슬라이더 위치 m · 거리 m).
    void  removeJoint(uint32_t handle);
    bool  hasJoint(uint32_t handle) const;
    float jointValue(uint32_t handle) const;
    size_t jointCount() const;

    // ── 바디 하나만 대상으로 하는 질의(손 쥐기 C, Hand_Grasp_Design 4-3) ──────────────────────────────
    // 다른 바디는 보지 않고 그 바디의 셰이프와만 판정합니다(Jolt TransformedShape). 쥐는 손의 마디가 잡은 물체에만 닿는지 볼 때 씁니다.
    // 광선: origin 에서 dir 방향(길이 상관없음) maxDistance 안의 첫 표면. 바디가 없거나 못 맞추면 false(out.actor 는 비움).
    bool raycastBody(uint32_t body, const glm::vec3& origin, const glm::vec3& dir, float maxDistance, RaycastHit& out) const;
    // 캡슐(선분 a-b, 반지름 radius)과 바디의 최대 침투 깊이(m). 닿지 않으면 음수(−1). a == b 면 구로 봅니다.
    float capsulePenetration(uint32_t body, const glm::vec3& a, const glm::vec3& b, float radius) const;

    // ── 캐릭터 컨트롤러(2026-11-17, Jolt CharacterVirtual) ─────────────────────────────────────────────
    // 바디가 아닌 가상 캡슐로 "미끄러지며 막히기(collide and slide)"를 합니다. 벽을 따라 미끄러지고, 비탈은 maxSlopeDeg 까지
    // 걸어 오르며, stepHeight 이하 턱은 올라섭니다(UE CharacterMovement · Unity CharacterController 와 같은 역할). 다른 바디를
    // 밀지는 않습니다(가상 캐릭터). 위쪽은 언제나 월드 +Y 입니다.
    struct CharacterDesc {
        float radius      = 0.25f;   // m
        float height      = 1.5f;    // m, 발에서 머리 끝까지(2·radius 이상)
        float maxSlopeDeg = 45.f;
        float stepHeight  = 0.35f;   // m
    };
    struct CharacterMove {
        glm::vec3 position{ 0.f };       // 움직인 뒤 발 위치
        bool      onGround = false;      // 걸을 수 있는 바닥 위
        glm::vec3 groundNormal{ 0.f, 1.f, 0.f };
    };
    // 발 위치(캡슐 바닥) 기준. 실패하면 0, 성공하면 0 이 아닌 핸들.
    uint32_t createCharacter(const CharacterDesc& d, const glm::vec3& feet);
    void     destroyCharacter(uint32_t handle);
    // feet 에서 시작해 velocity(m/s, 중력은 호출자가 넣음)로 dt 동안 움직입니다. ignore 액터의 바디는 통과합니다(리그 자신 ·
    // 손에 든 물체). 바닥에 붙어 내려가기와 계단 오르기를 함께 합니다. 핸들이 없으면 false.
    bool     moveCharacter(uint32_t handle, const glm::vec3& feet, const glm::vec3& velocity, float dt,
                           CharacterMove& out, std::span<Actor* const> ignore = {});

private:
    std::unique_ptr<JoltImpl> impl_;
    World* world_ = nullptr;

    uint32_t maxBodies_ = 4096;   // setMaxBodies(DEF-10)

    // Fixed-step accumulator
    float accumulator_ = 0.f;
    float step_        = 1.f / 60.f;

    // 힘 · 토크(DEF-19). frame 은 이번 틱에 받은 합, budget 은 아직 넣지 못한 충격량(합 × 틱 시간)입니다.
    struct ForceTorque { glm::vec3 force{ 0.f }, torque{ 0.f }; };
    std::unordered_map<uint32_t, ForceTorque> frameForces_;
    std::unordered_map<uint32_t, ForceTorque> impulseBudget_;
    bool isDynamic(uint32_t id) const;
    PhysicsLayerTable layers_;
    void createJoints();
    void processContacts();                // 접촉 큐를 비워 쌍을 셈(스텝마다), 알림 목록에 더함
    void dispatchContacts(bool stepped);   // 물리 틱 끝: 유지 알림 · 보내기
    // 상한 진단(DEF-10). 바디 상한에 닿거나 접촉 버퍼가 넘치면 Jolt 는 아무 말이 없으므로
    // 처음 한 번만 알립니다.
    bool bodyCapWarned_     = false;
    bool updateErrorWarned_ = false;
};
