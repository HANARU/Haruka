#include "CameraComponent.hpp"
#include "../core/Actor.hpp"
#include "../core/ComponentFields.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace {
// 액터의 월드 회전입니다. 액터가 없으면 회전이 없는 것으로 봅니다.
glm::quat ownerRotation(const Actor* owner) {
    return owner ? glm::normalize(owner->worldRotation()) : glm::quat(1.f, 0.f, 0.f, 0.f);
}
} // namespace

glm::vec3 CameraComponent::forward() const {
    return ownerRotation(owner) * glm::vec3(0.f, 0.f, -1.f);
}

glm::vec3 CameraComponent::up() const {
    return ownerRotation(owner) * glm::vec3(0.f, 1.f, 0.f);
}

glm::mat4 CameraComponent::viewMatrix() const {
    // 회전과 위치로 이뤄진 카메라 월드 자세의 역행렬입니다: R^T · T(-p).
    const glm::quat inv = glm::conjugate(ownerRotation(owner));
    const glm::vec3 pos = owner ? owner->worldPosition() : glm::vec3(0.f);
    glm::mat4 view = glm::mat4_cast(inv);
    view[3] = glm::vec4(-(inv * pos), 1.f);
    return view;
}

glm::mat4 CameraComponent::projMatrix(float aspect) const {
    glm::mat4 proj = glm::perspective(glm::radians(fovY), aspect, zNear, zFar);
    proj[1][1] *= -1.0f;  // Vulkan Y-flip
    return proj;
}

glm::mat4 CameraComponent::projFromFov(float angleLeft, float angleRight, float angleUp,
                                       float angleDown, float zNear, float zFar) {
    const float tanL = std::tan(angleLeft),  tanR = std::tan(angleRight);
    const float tanU = std::tan(angleUp),    tanD = std::tan(angleDown);
    const float tanW = tanR - tanL;
    const float tanH = tanD - tanU;   // Vulkan 은 클립 Y 가 아래라 위 · 아래를 뒤집습니다
    glm::mat4 m(0.f);
    m[0][0] = 2.f / tanW;
    m[2][0] = (tanR + tanL) / tanW;
    m[1][1] = 2.f / tanH;
    m[2][1] = (tanU + tanD) / tanH;
    m[2][2] = -zFar / (zFar - zNear);             // 깊이 [0,1]
    m[3][2] = -(zFar * zNear) / (zFar - zNear);
    m[2][3] = -1.f;
    return m;
}

glm::mat4 CameraComponent::xrOrigin() const {
    if (!owner) return glm::mat4(1.f);
    // 정면을 수평면에 내린 방향의 요입니다. 똑바로 위 · 아래를 보면 요가 정해지지 않아 0 으로 둡니다.
    const glm::vec3 f = forward();
    const float yaw = (f.x * f.x + f.z * f.z > 1e-8f) ? std::atan2(-f.x, -f.z) : 0.f;
    return glm::translate(glm::mat4(1.f), owner->worldPosition()) *
           glm::mat4_cast(glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)));
}

glm::quat CameraComponent::yawPitchRotation(float yaw, float pitch) {
    // -Z 를 Y 축으로 -yaw 만큼 돌리면 (sin yaw, 0, -cos yaw), X 축으로 -pitch 만큼 돌리면
    // 아래(-Y)로 숙입니다. 예전 forward() 공식 (cos p · sin y, -sin p, -cos p · cos y) 와 같습니다.
    return glm::angleAxis(-yaw,   glm::vec3(0.f, 1.f, 0.f)) *
           glm::angleAxis(-pitch, glm::vec3(1.f, 0.f, 0.f));
}

std::vector<PropDesc> CameraComponent::buildProps() {
    auto* p = this;
    return { CAMERA_PROPS(p) };
}

// ── 직렬화 (DEF-22) ─────────────────────────────────────────────────────────────
// 방향은 액터 회전이라(DEF-21) 액터의 ROT 로 저장되고, 여기서는 투영 값만 씁니다.
void CameraComponent::writeFields(ComponentFieldWriter& w) const {
    w.f("fovY",    fovY);
    w.f("zNear",   zNear);
    w.f("zFar",    zFar);
    w.b("primary", primary);
}

bool CameraComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if      (key == "fovY")    ok = fieldparse::toFloat(value, fovY);
    else if (key == "zNear")   ok = fieldparse::toFloat(value, zNear);
    else if (key == "zFar")    ok = fieldparse::toFloat(value, zFar);
    else if (key == "primary") ok = fieldparse::toBool (value, primary);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}
