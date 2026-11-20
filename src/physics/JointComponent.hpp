#pragma once
// 물리 조인트(2026-11-20, Docs/Physics_Joints_And_Events.md D5).
//
// 이 액터의 몸(RigidBodyComponent)을 상대 액터의 몸에 잇습니다. 상대가 비면 월드에 고정합니다(Unity connectedBody 가 빈 것과 같음).
//   fixed    : 지금 상대 자세를 그대로 유지
//   hinge    : anchor 를 지나는 axis 둘레로만 돎. limits 를 켜면 limitMin ~ limitMax(도, −180 ~ 180)
//   slider   : axis 를 따라서만 미끄러짐(돌지 않음). limits 를 켜면 limitMin ~ limitMax(m)
//   distance : anchor 와 상대의 connectedAnchor 사이 거리. limits 를 켜면 limitMin ~ limitMax(m), 끄면 만들 때의 거리로 고정
// anchor · axis 는 이 액터 로컬(크기를 곱한 자리), connectedAnchor 는 상대 액터 로컬(상대가 없으면 월드 좌표)입니다.
// 각도 · 위치 0 은 조인트를 만든 순간의 자세입니다(Play 시작 또는 속성 확정).
//
// 물리 틱이 양쪽 몸이 생긴 뒤 만듭니다. 상대를 정했는데 찾지 못하면(지워짐 등) 만들지 않고 기다립니다. 어느 쪽 몸이든 지워지면 조인트도
// 함께 지웁니다. 조인트 기록은 물리 시스템이 핸들로 갖습니다.
#include "../scene/Component.hpp"
#include "../scene/ActorRef.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

class PhysicsSystem;

enum class EJointType : uint8_t { Fixed, Hinge, Slider, Distance };

class JointComponent : public Component {
public:
    EJointType type = EJointType::Hinge;
    HActorRef  target;                         // 상대(비면 월드)
    glm::vec3  anchor{ 0.f };                  // 이 액터 로컬
    glm::vec3  axis{ 0.f, 1.f, 0.f };          // 이 액터 로컬(경첩 · 슬라이더)
    glm::vec3  connectedAnchor{ 0.f };         // 상대 로컬(거리 조인트), 상대가 없으면 월드
    bool       limits   = false;
    float      limitMin = -45.f;               // 경첩: 도, 슬라이더 · 거리: m
    float      limitMax = 45.f;

    const char* typeName() const override { return "JointComponent"; }
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps() override;
    void onDestroy() override;
    void onDisable() override;
    void onPropsCommitted() override;   // 값을 확정하면 다시 만듦(지금 자세가 새 0)

    // 조인트가 물리에 있는지.
    bool  hasJoint() const { return physSys_ != nullptr && handle_ != 0; }
    // 지금 값: 경첩 각도(도), 슬라이더 위치(m), 거리 조인트 길이(m). 조인트가 없거나 fixed 면 0.
    float currentValue() const;
    // 조인트를 지워 다음 물리 틱에 지금 값 · 자세로 다시 만들게 합니다.
    void  rebuild();

    static const char* typeNameOf(EJointType t);
    static bool        typeFromText(const std::string& s, EJointType& out);

    // PhysicsSystem 이 씁니다.
    uint32_t       handle_  = 0;
    PhysicsSystem* physSys_ = nullptr;
};
