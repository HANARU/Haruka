#pragma once
#include "../core/Component.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// zFar uses makePropFloat directly to clamp against the live zNear value.
#define CAMERA_PROPS(p) \
    XPROP_FloatClamp(p, "FovY",  PropColor::Neutral, 1.f,   fovY,  10.f, 170.f), \
    XPROP_FloatMin  (p, "zNear", PropColor::Neutral, 0.01f, zNear, 0.001f),       \
    makePropFloat("zFar", PropColor::Neutral, 1.f,                                \
        [p]() -> float { return (p)->zFar; },                                     \
        [p](float _v){ (p)->zFar = std::max((p)->zNear + 1.f, _v); }),            \
    XPROP_Toggle(p, "Primary", primary)

class CameraComponent : public Component {
public:
    float fovY    =  45.0f;
    float zNear   =  0.1f;
    float zFar    =  100.0f;
    bool  primary =  true;

    bool editorIcon(glm::vec4& c) const override { c = {0.35f, 0.6f, 1.f, 1.f}; return true; }

    const char*           typeName()   const override { return "CameraComponent"; }
    std::vector<PropDesc> buildProps()       override;

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

    // 네 방향 시야각(라디안, 왼쪽 · 아래는 음수)으로 만든 비대칭 투영입니다. OpenXR 이 눈마다 주는
    // XrFovf 를 그대로 받습니다(DEF-21). Vulkan 규약(깊이 [0,1], 클립 Y 아래)이고, 좌우 · 상하가
    // 대칭이면 projMatrix 와 같은 행렬입니다.
    static glm::mat4 projFromFov(float angleLeft, float angleRight, float angleUp, float angleDown,
                                 float zNear, float zFar);

    // XR 원점: 헤드셋 트래킹 공간을 월드에 놓는 자리입니다. 카메라 액터의 월드 위치와 요(yaw)만 씁니다.
    // 숙임 · 롤까지 넣으면 헤드셋 안의 지평선이 기울어 멀미가 납니다. 액터가 없으면 단위 행렬입니다.
    // 렌더러의 눈 자세와 VR 입력(HXrInputState) 자세가 모두 이 원점 기준입니다.
    glm::mat4 xrOrigin() const;
};
