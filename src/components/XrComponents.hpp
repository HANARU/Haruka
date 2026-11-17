#pragma once
#include "../scene/Component.hpp"
#include "../scene/ComponentFields.hpp"
#include "../core/ProjectSettings.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
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

// 던지기 속도(수학 보강 A1, 2026-11-17). 잡은 물체는 손에 단단히 붙어 손과 같은 각속도 ω 로 돕니다. 그래서 질량 중심은
// 손 원점(grip)보다 ω × (질량 중심 − 손) 만큼 더 빠르게 움직입니다(강체 속도장 v(p) = v₀ + ω × (p − p₀)). 손 속도는 리그
// 기준이라 리그가 움직이는 속도도 더합니다. 예전에는 손 선속도만 넣어 손목을 튕기는 던지기가 약했습니다.
struct ThrowVelocity { glm::vec3 linear{ 0.f }, angular{ 0.f }; };
ThrowVelocity throwVelocity(const glm::vec3& handLinear, const glm::vec3& handAngular, const glm::vec3& handPos,
                            const glm::vec3& centerOfMass, const glm::vec3& rigLinear);

// 최근 몇 프레임의 손 속도(월드) 평균. 놓는 순간 한 프레임 값은 추적 잡음과 손을 펴는 동작의 감속을 그대로 받으므로
// 짧은 창(kSize 프레임, 90 Hz 에서 약 55 ms)의 평균을 씁니다(SteamVR · Unity XRI 도 여러 프레임 추정을 씀).
struct VelocityHistory {
    static constexpr int kSize = 5;
    glm::vec3 linear[kSize]{}, angular[kSize]{};
    int       count = 0, next = 0;
    void clear() { count = 0; next = 0; }
    void push(const glm::vec3& lin, const glm::vec3& ang) {
        linear[next] = lin; angular[next] = ang;
        next = (next + 1) % kSize;
        if (count < kSize) ++count;
    }
    // 표본이 없으면 false(값은 0).
    bool mean(glm::vec3& lin, glm::vec3& ang) const {
        lin = glm::vec3(0.f); ang = glm::vec3(0.f);
        if (count == 0) return false;
        for (int i = 0; i < count; ++i) { lin += linear[i]; ang += angular[i]; }
        lin /= float(count); ang /= float(count);
        return true;
    }
};
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

// 텔레포트 포물선(2026-11-17). origin 에서 dir 방향 speed(m/s)로 던진 점이 아래로 gravity(m/s²)를 받아 그리는 길을 step 초 간격
// 선분으로 나눠, ray(a, b, 맞은 점, 법선)로 앞에서부터 맞춥니다. 처음 맞은 곳에서 멈추고 maxTime 안에 맞지 않으면 hit = false.
// points 는 그릴 꺾은선(맞은 점으로 끝남)입니다. UE VR 템플릿 · Unity XRI 의 투사체 곡선과 같은 방식입니다.
struct TeleportArc {
    std::vector<glm::vec3> points;
    bool      hit = false;
    glm::vec3 point{ 0.f };
    glm::vec3 normal{ 0.f, 1.f, 0.f };
};
using ArcRay = std::function<bool(const glm::vec3& a, const glm::vec3& b, glm::vec3& point, glm::vec3& normal)>;
TeleportArc teleportArc(const glm::vec3& origin, const glm::vec3& dir, float speed, float gravity, float maxTime,
                        float step, const ArcRay& ray);
// 내릴 수 있는 바닥인지: 맞았고, 법선이 위쪽(+Y)과 maxSlopeDeg 안.
bool teleportValid(const TeleportArc& arc, float maxSlopeDeg);
// 텔레포트 뒤 리그 위치: 머리의 수평 위치가 target 위에 오고 리그 바닥 높이가 target.y 가 되게 옮깁니다(방 안에서 걸어간 만큼의
// 머리 · 리그 차이를 지킴, 리그 위쪽은 +Y 로 봄).
glm::vec3 teleportRigPosition(const glm::vec3& rigPos, const glm::vec3& headPos, const glm::vec3& target);
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
//
// 충돌(collide, 2026-11-17): 켜면 머리 아래 발 위치에 세운 캡슐(PhysicsSystem 캐릭터 컨트롤러)로 스틱 이동을 하여 벽에 막히고
// 비탈 · 턱을 오르며, 걷기(flying 꺼짐)면 중력으로 떨어집니다. 바닥은 물리 충돌체(RigidBody)여야 합니다(충돌체 없는 바닥이면
// 떨어짐). 방 안에서 몸으로 걸은 이동은 막지 않습니다(헤드셋 화면을 붙잡으면 멀미가 나서, 대부분의 VR 게임과 같음). 기본은 꺼짐
// (예전 씬의 동작 유지).
// 텔레포트(teleport, VR): 오른쪽 스틱을 앞으로 밀면(Teleport 축) 오른손에서 포물선을 그리고, 놓으면 맞은 바닥으로 옮깁니다.
// 비탈이 kTeleportMaxSlope 보다 가파르거나 맞은 것이 없으면 빨간 선이고 옮기지 않습니다. 대상 판정은 물리 충돌체입니다.
class RigLocomotionComponent : public Component {
public:
    float moveSpeed       = 2.f;     // m/s
    float snapTurnDeg     = 45.f;
    bool  flying          = false;
    float lookSensitivity = 0.15f;   // 데스크톱 둘러보기, 도/픽셀
    bool  collide         = false;   // 캐릭터 컨트롤러로 이동(벽 · 중력)
    bool  teleport        = true;    // VR 텔레포트

    static constexpr float kBodyRadius       = 0.25f;  // 캐릭터 캡슐(m)
    static constexpr float kBodyHeight       = 1.5f;   // 눈높이보다 낮게 두어 낮은 천장에 머리가 막히지 않게 함
    static constexpr float kMaxFallSpeed     = 50.f;   // m/s
    static constexpr float kTeleportSpeed    = 8.f;    // m/s, 포물선 처음 속도(평지 최대 거리 약 7 m)
    static constexpr float kTeleportMaxTime  = 2.f;    // s
    static constexpr float kTeleportStep     = 0.04f;  // s
    static constexpr float kTeleportMaxSlope = 40.f;   // 도

    void tick(float dt) override;
    void onDisable() override;
    ~RigLocomotionComponent() override;
    const char* typeName() const override { return "RigLocomotionComponent"; }
    std::vector<PropDesc> buildProps() override;
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;

    // 리그의 머리(자식 중 TrackedPose[Head])를 찾습니다. 없으면 null.
    static Actor* findHead(const Actor& rig);
    // 리그를 pivot(월드) 둘레로 axis 방향 angleRad 만큼 돌립니다. 스냅 회전이 머리 자리를 지키는 데 씁니다.
    static void rotateRigAbout(Actor& rig, const glm::vec3& pivot, const glm::vec3& axis, float angleRad);
    // 이번 틱에 스틱 이동으로 낸 리그 속도(월드, m/s). 스냅 회전 · 순간 이동은 넣지 않습니다(던지기에 더함).
    const glm::vec3& velocity() const { return velocity_; }

private:
    glm::vec3 velocity_{ 0.f };
    bool  snapArmed_ = true;
    float pitchDeg_  = 0.f;

    // 충돌 이동: 캐릭터 핸들(0 = 없음)과 떨어지는 속도.
    void     moveCollide(Actor& rig, const glm::vec3& headPos, const glm::vec3& move, float dt);
    void     releaseCharacter();
    uint32_t character_ = 0;
    float    fallSpeed_ = 0.f;
    // 텔레포트: 겨누는 중인지와 마지막 포물선.
    void     tickTeleport(Actor& rig, const glm::vec3& headPos);
    bool     aiming_    = false;
    // 판정에서 뺄 액터 목록과 자손 탐색 스택. 틱마다 새로 할당하지 않고 다시 씁니다(2026-11-18, 성능 안정화 1-5).
    std::vector<Actor*> ignoreScratch_;
    std::vector<Actor*> stackScratch_;
};

// 손으로 잡을 수 있는 액터. 강체(Dynamic)가 있으면 잡는 동안 Kinematic 으로 손을 따라가고, 놓으면 Dynamic 으로 돌아가며
// throwable 이면 손 속도를 받습니다(던지기). 강체가 없으면 손을 따라가다 놓은 자리에 남습니다.
class GrabbableComponent : public Component {
public:
    bool throwable = true;
    // 속도 추적 잡기(P5, 2026-11-17, Unity XRI 의 Velocity Tracking · UE Physics Handle 과 같은 생각): 켜면 잡는 동안에도 Dynamic
    // 으로 두고 손 자세를 향하는 속도를 틱마다 넣습니다. 손에 든 물체가 벽 · 탁자에 막히고 다른 물체를 밀어냅니다(Kinematic 은
    // 벽을 뚫고 지나감). 대신 손보다 한 스텝쯤 늦고, 무거운 것에 막히면 손에서 떨어져 보입니다. 끄면(기본) Kinematic 으로 손에 붙음.
    bool velocityTracking = false;

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
    // dt 는 속도 추적 잡기(P5)만 씁니다(다음 틱까지 목표 자세에 닿는 속도). 0 이면 속도 추적은 아무 일도 하지 않습니다.
    void follow(const glm::vec3& handPos, const glm::quat& handRot, float dt = 0.f);
    // 속도 추적의 목표 속도(순수 계산, 시험용). 현재 질량 중심 · 회전에서 dt 뒤 목표에 닿는 선속도 · 각속도, 상한으로 자름.
    static void trackingVelocity(const glm::vec3& comNow, const glm::quat& rotNow, const glm::vec3& comGoal,
                                 const glm::quat& rotGoal, float dt, glm::vec3& linear, glm::vec3& angular);
    // 자동 맞춤 쥐기(손 쥐기 D, Hand_Grasp_Design 4-4). 이 손 액터 아래(자식 장갑)에 HandPoseComponent 와 스킨 메시가 있고 잡은
    // 물체에 바디가 있으면, 잡는 순간 손바닥을 표면에 맞추고(물체를 손바닥 노멀 방향으로 옮김) 손가락을 물체 모양대로 감쌉니다.
    // 맞추지 못하면(fitted 거짓) 예전처럼 상대 자세를 지키는 잡기입니다. 계산은 hgrasp::solve(순수 함수)입니다.
    bool autoFit = true;
    bool lastFitted() const { return fitted_; }
    static constexpr float kPalmReach     = 0.12f;   // 손바닥에서 표면을 찾는 거리(m)
    static constexpr float kPalmThickness = 0.015f;  // 손바닥 관절에서 장갑 표면까지(m)
    static constexpr float kMaxFitShift   = 0.08f;   // 물체를 옮기는 최대 거리(m)
    static constexpr float kFitBlend      = 0.1f;    // 전환 시간(초, Meta 기본값과 같음)
    static constexpr float kMaxTrackSpeed   = 20.f;   // m/s. 막혔다 풀릴 때 튀지 않게
    static constexpr float kMaxTrackAngular = 40.f;   // rad/s
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
    glm::vec3           comLocal_   { 0.f };   // 잡을 때 잰 질량 중심(액터 위치 · 회전 기준, 크기는 바디 모양에 이미 들어감)
    hxr::VelocityHistory velHistory_;          // 손 속도(월드) 최근 표본
    bool                wasDynamic_ = false;
    bool                tracking_   = false;   // 속도 추적으로 잡고 있음(바디는 Dynamic 그대로)
    // 자동 맞춤(D): 물체 상대 위치를 kFitBlend 동안 옮기고, 손 자세는 HandPoseComponent 가 섞습니다.
    void                fitGrasp(const glm::vec3& handPos, const glm::quat& handRot);
    bool                fitted_     = false;
    glm::vec3           fitFrom_    { 0.f };
    glm::vec3           fitTo_      { 0.f };
    float               fitT_       = 1.f;
    bool                prevHeld_   = false;
};

// 엔진 기본 컴포넌트와 함께 등록합니다(CoreComponents.cpp).
void registerXrComponents();
