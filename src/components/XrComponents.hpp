#pragma once
#include "../core/Component.hpp"
#include "../core/ComponentFields.hpp"
#include "../core/ProjectSettings.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

class Actor;
class World;
class GrabInteractorComponent;

// ── 플레이어 리그 · VR 상호작용 기초 (2026-10-15) ──────────────────────────────
// 엔진 코어(Layer 1)는 리그 · 추적 자세 · 기본 이동 · 잡기의 기초만 둡니다. 무중력 비행 같은 도메인 이동은 Layer 2 가
// 이 컴포넌트들 대신(또는 함께) 붙입니다. 표준 구성은 assets/prefabs/XrPlayer.hprefab 입니다.
//
//   XrPlayer (XrRigComponent + RigLocomotionComponent)    ← 바닥의 플레이어 원점
//     ├ Head      (CameraComponent + TrackedPoseComponent[Head])
//     ├ LeftHand  (TrackedPoseComponent[LeftHand]  + GrabInteractorComponent)
//     └ RightHand (TrackedPoseComponent[RightHand] + GrabInteractorComponent)
//
// Play 중에는 리그의 카메라가 화면 카메라가 되고(World::getActiveCamera), 헤드셋 원점은 리그입니다. 리그가 있으면
// 헤드셋 자세를 바닥 기준 공간(LOCAL_FLOOR)으로 받아 실제 키가 반영됩니다.

// 플레이어 원점(바닥). 자식의 추적 자세가 이 액터 기준 로컬로 들어갑니다.
//
// 높이(2026-10-16, Unity XR Origin 의 Floor · Device 모드와 같은 구분):
//   trackedHeight = true  OpenXR 바닥 기준 공간(LOCAL_FLOOR)으로 실제 키를 씁니다. 런타임에 그 공간이 없으면 eyeHeight 로 갑니다.
//   trackedHeight = false 머리 기준 공간(LOCAL)에 eyeHeight 를 더해 눈높이를 고정합니다(앉든 서든 같은 높이).
// 리그가 없는 씬은 XR Play 동안 엔진이 카메라 아래 바닥에 보이지 않는 리그를 붙이고, 그 높이 값은 프로젝트 설정
// (assets/project.ini 의 xr.height · xr.eyeHeight)을 씁니다.
class XrRigComponent : public Component {
public:
    bool  trackedHeight = true;
    float eyeHeight     = xr_rig::kDefaultEyeHeight;   // m, 바닥에서 눈까지

    const char* typeName() const override { return "XrRigComponent"; }
    std::string readOnlyInfo() const override { return "Player origin on the floor. Children: Head (camera) and hands."; }
    std::vector<PropDesc> buildProps() override;
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
};

namespace hxr {
// from 에서 똑바로 아래로 광선을 쏴 처음 만나는 윗면의 높이(월드 y)를 찾습니다. 켜진 액터의 컴포넌트 로컬 바운드
// 합집합(에디터 픽과 같은 기준)에 광선-OBB 판정을 하고, 안에서 시작하는 바운드(광선 시작점을 감싼 것)는 건너뜁니다.
// skip 과 그 자식은 보지 않습니다(카메라 자신 등). 아래에 아무것도 없으면 false.
bool floorBelow(const World& world, const glm::vec3& from, const Actor* skip, float& outY);
} // namespace hxr

enum class ETrackedSource { Head = 0, LeftHand = 1, RightHand = 2 };

namespace hxr {
// 화면 카메라의 XR 원점을 무엇으로 정하는지(2026-11-06, DEF-144). Unity 의 Tracked Pose Driver · UE 의 카메라처럼, 머리
// 추적(TrackedPose Head)이 붙은 카메라는 부모가 추적 원점입니다. 머리 추적이 카메라 액터를 이미 움직이므로, 카메라 위치를
// 원점으로 쓰면 머리 이동 · 요 · 높이가 두 번 적용됩니다(30° 돌면 60°).
//   SceneRig      부모가 XrRigComponent(씬 리그). actor = 리그
//   TrackedParent 머리 추적이 켜져 있지만 부모가 리그가 아님. actor = 부모(null 이면 월드 원점)
//   CameraOnly    머리 추적이 없거나 꺼짐. XR 동안 엔진이 보이지 않는 리그를 붙이고, 데스크톱은 카메라 위치 + 요
enum class EXrOrigin { SceneRig, TrackedParent, CameraOnly };
struct XrOriginChoice {
    EXrOrigin    kind  = EXrOrigin::CameraOnly;
    const Actor* actor = nullptr;
};
XrOriginChoice chooseOrigin(const Actor* cameraActor);
} // namespace hxr

// 헤드셋 · 컨트롤러의 자세를 이 액터의 로컬 트랜스폼으로 씁니다(부모 = 리그). XR 세션이 입력을 받을 때만 바꾸고,
// 아니면(데스크톱 Play) 손대지 않습니다. 손은 쥐는 자세(grip)를 쓰고, showDebug 면 축과 가리키는 선을 그립니다
// (트리거를 당기면 노래지고 그립을 쥐면 길어짐. 예전에 에디터가 그리던 표시를 손 액터로 옮김).
class TrackedPoseComponent : public Component {
public:
    ETrackedSource source    = ETrackedSource::Head;
    bool           showDebug = true;

    int  handIndex() const { return source == ETrackedSource::LeftHand ? 0 : source == ETrackedSource::RightHand ? 1 : -1; }
    // 살아 있는 손 추적 컴포넌트 수. 0 이면 에디터가 예전처럼 카메라 원점 기준으로 손 표시를 그립니다.
    static int liveHands();
    ~TrackedPoseComponent() override;

    void tick(float dt) override;
    void onEnable() override;
    void onDisable() override;
    const char* typeName() const override { return "TrackedPoseComponent"; }
    std::vector<PropDesc> buildProps() override;
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;

private:
    bool countedHand_ = false;
};

// 리그의 기본 이동(Layer 1 기본값, Layer 2 가 바꿀 수 있음).
//   VR:      왼쪽 스틱(MoveX · MoveY) 부드러운 이동(머리가 보는 방향 기준), 오른쪽 스틱(TurnX) 스냅 회전(머리 둘레).
//   데스크톱: WASD(MoveX · MoveY) 이동, 오른쪽 버튼을 누른 채 마우스(LookX · LookY)로 둘러보기.
// flying 이 꺼져 있으면 리그의 수평면 위로만 움직이고, 켜면 보는 방향 그대로 움직이며 E/Q(MoveUp)로 오르내립니다.
// 벽에 막히지 않습니다(캐릭터 컨트롤러는 아직 없음).
class RigLocomotionComponent : public Component {
public:
    float moveSpeed       = 2.f;     // m/s
    float snapTurnDeg     = 45.f;
    bool  flying          = false;
    float lookSensitivity = 0.15f;   // 데스크톱 둘러보기, 도/픽셀

    void tick(float dt) override;
    const char* typeName() const override { return "RigLocomotionComponent"; }
    std::vector<PropDesc> buildProps() override;
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;

    // 리그의 머리(자식 중 TrackedPose[Head])를 찾습니다. 없으면 null.
    static Actor* findHead(const Actor& rig);
    // 리그를 pivot(월드) 둘레로 axis 방향 angleRad 만큼 돌립니다. 스냅 회전이 머리 자리를 지키는 데 씁니다.
    static void rotateRigAbout(Actor& rig, const glm::vec3& pivot, const glm::vec3& axis, float angleRad);

private:
    bool  snapArmed_ = true;
    float pitchDeg_  = 0.f;
};

// 손으로 잡을 수 있는 액터. 강체(Dynamic)가 있으면 잡는 동안 Kinematic 으로 손을 따라가고, 놓으면 Dynamic 으로 돌아가며
// throwable 이면 손 속도를 받습니다(던지기). 강체가 없으면 손을 따라가다 놓은 자리에 남습니다.
class GrabbableComponent : public Component {
public:
    bool throwable = true;

    // 지금 살아 있는(켜진) 잡을 수 있는 컴포넌트들. 손이 잡을 대상을 고를 때 씁니다.
    static const std::vector<GrabbableComponent*>& live();
    GrabInteractorComponent* holder() const { return holder_; }
    // 월드를 통째로 지우면 onDisable 없이 해제되므로 소멸자에서도 목록 · 잡은 손과의 연결을 정리합니다.
    ~GrabbableComponent() override;

    void onEnable() override;
    void onDisable() override;
    const char* typeName() const override { return "GrabbableComponent"; }
    std::vector<PropDesc> buildProps() override;
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;

private:
    friend class GrabInteractorComponent;
    GrabInteractorComponent* holder_ = nullptr;
};

// 손 액터(TrackedPose[LeftHand/RightHand])에 붙여 그립으로 잡고 놓습니다. 액션 이름은 손에 따라 GrabLeft · GrabRight
// (프로젝트 설정의 입력 바인딩). 누르는 순간 반지름 안에서 가장 가까운 잡을 수 있는 액터를 잡고, 진동을 짧게 보냅니다.
class GrabInteractorComponent : public Component {
public:
    float radius = 0.12f;   // m

    GrabbableComponent* held() const { return held_; }
    ~GrabInteractorComponent() override;
    // 잡을 대상 고르기: 손 위치 hand 에서 바운드(없으면 액터 위치)까지 거리가 radius 안이면서 가장 가까운 것.
    static GrabbableComponent* pick(const glm::vec3& hand, float radius);
    // 손 자세(월드)로 잡기 · 따라가기 · 놓기. tick 이 입력을 읽어 부르고, 테스트가 직접 부릅니다.
    void grab(GrabbableComponent* g, const glm::vec3& handPos, const glm::quat& handRot);
    void follow(const glm::vec3& handPos, const glm::quat& handRot);
    void release(const glm::vec3& linearVelocity, const glm::vec3& angularVelocity, bool throwIt);

    void tick(float dt) override;
    void onDisable() override;
    const char* typeName() const override { return "GrabInteractorComponent"; }
    std::vector<PropDesc> buildProps() override;
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;

private:
    friend class GrabbableComponent;
    void drop();   // 잡힌 것이 사라질 때(속도 없이)

    GrabbableComponent* held_       = nullptr;
    glm::vec3           offPos_     { 0.f };
    glm::quat           offRot_     { 1.f, 0.f, 0.f, 0.f };
    bool                wasDynamic_ = false;
    bool                prevHeld_   = false;
};

// 엔진 기본 컴포넌트와 함께 등록합니다(CoreComponents.cpp).
void registerXrComponents();
