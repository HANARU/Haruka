#pragma once
#include "../core/Component.hpp"
#include <cstdint>
#include <string>

class PhysicsSystem;

// 값은 옛 씬 파일의 `RIGID:` 순번과 같습니다. 지금은 이름으로 기록하지만(DEF-44) 옛 파일의
// 숫자도 읽으므로, 기존 항목의 값을 바꾸지 말고 새 항목은 끝에 명시값으로 붙이십시오.
enum class EPhysicsShape { Box = 0, Sphere = 1, Capsule = 2 };
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

    void onDestroy() override;
    void onDisable() override;              // 꺼지면 바디를 지웁니다(DEF-80)
    void onPropsCommitted() override;       // 셰이프·모션·크기·질량이 바뀌었으면 바디 재생성(DEF-43)
    void onTransformCommitted() override;   // 스케일이 바뀌었으면 재생성, 아니면 바디를 옮김

    const char*           typeName()     const override { return "RigidBodyComponent"; }
    std::vector<PropDesc> buildProps()         override;

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

private:
    bool bodyParamsChanged() const;   // 만들 때 쓴 셰이프·모션·크기·질량과 지금 값이 다른가
    void releaseBody();               // 바디만 지움(createFailed_ 는 건드리지 않음)
};
