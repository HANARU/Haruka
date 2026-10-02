#pragma once
#include "../core/Component.hpp"
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

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
    float yaw     =  0.0f;
    float pitch   =  0.15f;
    float fovY    =  45.0f;
    float zNear   =  0.1f;
    float zFar    =  100.0f;
    bool  primary =  true;

    bool editorIcon(glm::vec4& c) const override { c = {0.35f, 0.6f, 1.f, 1.f}; return true; }

    const char*           typeName()   const override { return "CameraComponent"; }
    std::vector<PropDesc> buildProps()       override;

    // 카메라가 보는 방향(단위 벡터)입니다. 회전은 액터 트랜스폼이 아니라 yaw/pitch 가
    // 정합니다. 렌더(viewMatrix), 에디터 카메라 이동, 오디오 청취자가 이 값을 함께 써서
    // 화면과 소리의 좌우가 어긋나지 않습니다. yaw=pitch=0 이면 -Z 를 보고 오른쪽이 +X 입니다.
    glm::vec3 forward() const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;
};
