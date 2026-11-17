#pragma once
#include "../core/Component.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <string>

class PhysicsSystem;

// 값은 옛 씬 파일의 `RIGID:` 순번과 같습니다. 지금은 이름으로 기록하지만(DEF-44) 옛 파일의
// 숫자도 읽으므로, 기존 항목의 값을 바꾸지 말고 새 항목은 끝에 명시값으로 붙이십시오.
// ConvexHull · Mesh(2026-11-17, 수학 보강 P2)는 같은 액터의 메시 형상(Component::collisionMesh)으로 만듭니다. 크기는 액터 월드
// 크기를 점마다 곱해 비균등 크기도 정확합니다(halfExtent 는 쓰지 않음). Mesh(삼각형 그대로, 오목 가능)는 Static · Kinematic
// 전용이고, Dynamic 이면 경고하고 볼록 껍질로 만듭니다(Jolt · UE · Unity 모두 움직이는 오목 메시 충돌을 지원하지 않음).
enum class EPhysicsShape { Box = 0, Sphere = 1, Capsule = 2, ConvexHull = 3, Mesh = 4 };
enum class EMotionType   { Static = 0, Dynamic = 1, Kinematic = 2 };

struct HalfExt { float x = 0.5f, y = 0.5f, z = 0.5f; };

class RigidBodyComponent : public Component {
public:
    EPhysicsShape shape      = EPhysicsShape::Box;
    EMotionType   motionType = EMotionType::Dynamic;
    float         friction    = 0.5f;
    float         restitution = 0.3f;
    float         mass        = 1.f;
    HalfExt       halfExtent;   // used for Box; radius = halfExtent.x for Sphere/Capsule
    // 연속 충돌(CCD, 수학 보강 P1, 2026-11-17): 켜면 한 스텝에 많이 움직이는 바디가 그 경로를 훑어(Jolt LinearCast) 얇은 벽을
    // 뚫지 않습니다. 끄면(기본) 스텝 끝 자리만 봅니다. 90 Hz 에서 10 m/s 는 스텝당 11 cm 라, 그보다 얇은 것 사이를 작은 물체가
    // 지나갈 수 있습니다. Jolt 는 움직임이 바디 안쪽 반지름의 75 % 를 넘는 스텝에서만 훑으므로 느릴 때는 비용이 거의 없습니다.
    // 던진 물체(GrabInteractor)는 이 값과 상관없이 놓는 순간 켜집니다.
    bool          continuous  = false;

    void onDestroy() override;
    void onDisable() override;              // 꺼지면 바디를 지웁니다(DEF-80)
    void onPropsCommitted() override;       // 셰이프·모션·크기·질량이 바뀌었으면 바디 재생성(DEF-43)
    void onTransformCommitted() override;   // 스케일이 바뀌었으면 재생성, 아니면 바디를 옮김

    const char*           typeName()     const override { return "RigidBodyComponent"; }
    // 직렬화 (DEF-22). 이름은 멤버 이름을 따릅니다.
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps()         override;

    // ── 물리 API (DEF-19) ─────────────────────────────────────────────────────
    // PhysicsSystem 의 같은 이름 함수에 이 바디를 넘깁니다. 규칙(힘은 그 프레임 동안 · 충격량은 즉시 ·
    // 위치를 바꾸면 순간 이동)은 PhysicsSystem.hpp 에 있습니다. 바디가 아직 없으면(다음 물리 틱에 생김)
    // 아무 일도 하지 않고 0 을 돌려줍니다. Dynamic 바디에만 걸립니다.
    bool      hasBody() const { return physSys_ != nullptr && bodyId_ != 0xFFFFFFFFu; }
    void      addForce(const glm::vec3& force);
    void      addForceAtPosition(const glm::vec3& force, const glm::vec3& worldPoint);
    void      addTorque(const glm::vec3& torque);
    void      addImpulse(const glm::vec3& impulse);
    void      addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& worldPoint);
    void      setLinearVelocity(const glm::vec3& v);
    glm::vec3 linearVelocity() const;
    void      setAngularVelocity(const glm::vec3& w);
    glm::vec3 angularVelocity() const;
    // 질량 중심의 월드 위치. 바디가 없으면 액터의 월드 위치를 돌려줍니다.
    glm::vec3 centerOfMass() const;

    // Dynamic ↔ Kinematic 을 바디를 유지한 채 바꿉니다(VR 잡기, 2026-10-15). 바디가 아직 없으면 값만 바꿔 다음 틱에
    // 그 종류로 만들어집니다. Static 과는 오가지 않습니다(false).
    bool setMotionRuntime(EMotionType m);
    // 살아 있는 바디의 연속 충돌만 켜고 끕니다(continuous 값은 바꾸지 않음, 바디를 다시 만들지 않음).
    void setContinuousRuntime(bool on);

    // 살아 있는 바디를 지워, 다음 물리 틱에 지금 값으로 다시 만들게 합니다. 게임 코드가
    // shape·motionType·halfExtent·mass 필드를 직접 바꿨다면 이것을 불러야 반영됩니다.
    void rebuildBody();

    // 씬 파일용 이름. 모르는 이름이면 false 를 돌려주고 out 은 그대로 둡니다.
    static const char* shapeName(EPhysicsShape s);
    static const char* motionName(EMotionType m);
    static bool        shapeFromText(const std::string& t, EPhysicsShape& out);
    static bool        motionFromText(const std::string& t, EMotionType& out);

    // Set by PhysicsSystem — do not touch.
    // Reset createFailed_ to false after fixing halfExtent to trigger a retry.
    uint32_t       bodyId_       = 0xFFFFFFFFu;
    PhysicsSystem* physSys_      = nullptr;
    bool           createFailed_ = false;
    // 바디를 만들 때 쓴 값입니다. 편집을 확정할 때 지금 값과 비교해, 실제로 달라졌을 때만
    // 바디를 다시 만듭니다(세터가 불렸다는 사실만으로는 다시 만들지 않음).
    glm::vec3      createdScale_ { 1.f };   // 월드 스케일(절댓값)
    EPhysicsShape  createdShape_  = EPhysicsShape::Box;
    EMotionType    createdMotion_ = EMotionType::Dynamic;
    HalfExt        createdHalf_;
    float          createdMass_   = 1.f;
    // 물리가 마지막으로 액터에 쓴 로컬 자세(Dynamic). 다음 틱에 액터의 로컬 자세가 이와 다르면 게임
    // 코드가 옮긴 것이라 바디를 그 자리로 순간 이동합니다(DEF-19).
    glm::vec3      syncedLocalPos_ { 0.f };
    glm::quat      syncedLocalRot_ { 1.f, 0.f, 0.f, 0.f };
    bool           synced_         = false;

private:
    bool bodyParamsChanged() const;   // 만들 때 쓴 셰이프·모션·크기·질량과 지금 값이 다른가
    void releaseBody();               // 바디만 지움(createFailed_ 는 건드리지 않음)
};
