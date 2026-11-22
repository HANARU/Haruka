#pragma once
#include "../scene/Component.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class World;
class CameraComponent;

// 앞단이 정하는 화면 카메라(2026-11-18, 엔진 구조 안정화 S3). 에디터가 자기 카메라를 월드 단위 서비스로 넣습니다
// (world.setService<ViewCameraOverride>). 예전에는 World 가 에디터 카메라와 Play 여부(editorCam_ · playing_)를 들었습니다.
struct ViewCameraOverride {
    CameraComponent* camera    = nullptr;   // 앞단의 카메라(에디터 카메라)
    bool             preferred = false;     // 참이면 씬 카메라보다 먼저 고름(에디터 편집 중). 거짓이면 씬 카메라가 없을 때만
};

// zFar uses makePropFloat directly to clamp against the live zNear value.
#define CAMERA_PROPS(p) \
    XPROP_FloatClamp(p, "FovY",  PropColor::Neutral, 1.f,   fovY,  10.f, 170.f).withDefault(45.f), \
    XPROP_FloatMin  (p, "zNear", PropColor::Neutral, 0.01f, zNear, 0.001f).withDefault(0.1f),      \
    makePropFloat("zFar", PropColor::Neutral, 1.f,                                \
        [p]() -> float { return (p)->zFar; },                                     \
        [p](float _v){ (p)->zFar = std::max((p)->zNear + 1.f, _v); }).withDefault(100.f), \
    XPROP_Toggle(p, "Primary", primary).withDefault(true),                        \
    XPROP_FloatClamp(p, "Exposure Compensation", PropColor::Neutral, 0.1f, exposureEv, -10.f, 10.f).withDefault(0.f)

class CameraComponent : public Component {
public:
    float fovY    =  45.0f;
    float zNear   =  0.1f;
    float zFar    =  100.0f;
    bool  primary =  true;
    // 노출 보정(EV, 2026-11-17 노출 배선): 화면 밝기 × 2^EV. 0 이면 프로젝트 기준 노출 그대로(UE Exposure Compensation 과 같은 뜻,
    // 자동 노출은 쓰지 않음, Lighting_UE_Design Phase 3).
    float exposureEv = 0.f;

    // 화면을 그릴 카메라(예전 World::getActiveCamera). 앞단 카메라가 먼저이면(에디터 편집 중) 그것이고, 아니면 씬의 primary
    // 카메라(활성 · 켜짐, 액터 순서상 첫 번째, 앞단 카메라 제외)를 먼저 고르고 없으면 앞단 카메라입니다(2026-10-15). 앞단
    // 카메라가 없으면(게임 실행 파일) 첫 primary 카메라이고, 그것도 없으면 nullptr 입니다.
    static CameraComponent* activeIn(const World& world);
    // activeIn 의 액터(없으면 nullptr). 소리 청취자를 고르는 함수로 넘깁니다(AudioSystem::setListenerSelector).
    static Actor* activeOwnerIn(const World& world) { CameraComponent* c = activeIn(world); return c ? c->owner : nullptr; }

    const char*           typeName()   const override { return "CameraComponent"; }
    std::vector<PropDesc> buildProps()       override;

    // 직렬화 (DEF-22). 이름은 멤버 이름을 따릅니다.
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;

    // 카메라의 자세는 액터의 월드 회전과 월드 위치가 정합니다(DEF-21). 예전에는 컴포넌트의
    // yaw/pitch 로 방향을 만들고 액터의 회전과 부모를 무시해서, 롤을 줄 수 없었고 씬 카메라의
    // 방향을 정하거나 저장할 수 없었습니다(DEF-102). 렌더(viewMatrix), 피킹, 오디오 청취자가
    // 모두 이 값을 써서 화면과 소리의 좌우가 어긋나지 않습니다. 회전이 없으면 -Z 를 보고
    // 오른쪽이 +X, 위가 +Y 입니다. 액터의 크기는 뷰에 넣지 않습니다(UE · Unity · Godot 공통).
    glm::vec3 forward() const;
    glm::vec3 up()      const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;

    // 에디터 비행 카메라의 yaw/pitch(라디안)를 액터 회전으로 바꿉니다. yaw 가 늘면 오른쪽으로,
    // pitch 가 늘면 아래로 돕니다(예전 컴포넌트 필드와 같은 규약이라 화면이 그대로입니다).
    // 롤은 없습니다. yaw/pitch 는 에디터만 가지며, 게임 카메라는 액터 회전을 직접 씁니다.
    static glm::quat yawPitchRotation(float yaw, float pitch);

    // XR 원점: 헤드셋 트래킹 공간을 월드에 놓는 자리입니다. 카메라 액터의 월드 위치와 요(yaw)만 씁니다.
    // 숙임 · 롤까지 넣으면 헤드셋 안의 지평선이 기울어 멀미가 납니다. 액터가 없으면 단위 행렬입니다.
    // 렌더러의 눈 자세와 VR 입력(HXrInputState) 자세가 모두 이 원점 기준입니다.
    // 플레이어 리그(2026-10-15): 이 카메라가 리그의 머리라면 원점은 리그 액터(바닥)의 위치와 회전이고, 머리 기준 공간을
    // 쓰는 경우(고정 눈높이, 바닥 공간이 없는 런타임)에는 lift 만큼 올립니다. 엔진이 매 프레임 setXrRig 로 알립니다.
    // 보이지 않는 리그(2026-10-16): 리그가 없는 씬은 XR 동안 엔진이 setXrImplicitRig 로 카메라에서 drop 만큼 아래
    // (카메라 아래 바닥)에 리그를 둡니다. 수평 위치와 요는 카메라를 따릅니다. 둘 다 아니면(데스크톱) 예전처럼
    // 카메라 위치 + 요입니다.
    // 머리 추적 카메라의 부모가 리그가 아니면(2026-11-06, DEF-144) 엔진이 setXrTrackedParent 로 그 부모를 리그처럼 원점으로
    // 씁니다. 부모가 없으면 월드 원점(+ lift)입니다(hxr::chooseOrigin).
    glm::mat4 xrOrigin() const;
    void setXrRig(const Actor* rig, float lift) { xrRig_ = rig; xrRigLift_ = lift; implicitRig_ = false; worldOrigin_ = false; }
    void setXrImplicitRig(float drop, float lift) {
        xrRig_ = nullptr; xrRigLift_ = lift; implicitRig_ = true; implicitDrop_ = drop; worldOrigin_ = false;
    }
    void setXrTrackedParent(const Actor* parent, float lift) {
        xrRig_ = parent; xrRigLift_ = lift; implicitRig_ = false; worldOrigin_ = (parent == nullptr);
    }
    const Actor* xrRig() const { return xrRig_; }
    bool         hasImplicitXrRig() const { return implicitRig_; }

private:
    const Actor* xrRig_        = nullptr;
    float        xrRigLift_    = 0.f;
    bool         implicitRig_  = false;
    float        implicitDrop_ = 0.f;
    bool         worldOrigin_  = false;   // 부모 없는 머리 추적 카메라: 원점은 월드 원점
};
