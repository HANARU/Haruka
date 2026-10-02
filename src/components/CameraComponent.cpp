#include "CameraComponent.hpp"
#include "../core/Actor.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

glm::vec3 CameraComponent::forward() const {
    return glm::vec3(cosf(pitch) * sinf(yaw),
                     -sinf(pitch),
                     -cosf(pitch) * cosf(yaw));
}

glm::mat4 CameraComponent::viewMatrix() const {
    glm::vec3 pos(0.0f);
    if (owner) pos = owner->transform().position();

    return glm::lookAt(pos, pos + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 CameraComponent::projMatrix(float aspect) const {
    glm::mat4 proj = glm::perspective(glm::radians(fovY), aspect, zNear, zFar);
    proj[1][1] *= -1.0f;  // Vulkan Y-flip
    return proj;
}

std::vector<PropDesc> CameraComponent::buildProps() {
    auto* p = this;
    return { CAMERA_PROPS(p) };
}
