#include "CoreComponents.hpp"

#include "../core/ComponentRegistry.hpp"
#include "AudioComponent.hpp"
#include "CameraComponent.hpp"
#include "LightComponent.hpp"
#include "RigidBodyComponent.hpp"
#include "StaticMeshComponent.hpp"

void registerCoreComponents() {
    auto& r = ComponentRegistry::get();
    // 버튼 순서는 예전 에디터와 같게 둡니다(Light · Camera · Mesh · Audio · RigidBody).
    if (!r.find("LightComponent"))      r.registerType<LightComponent>     ("LightComponent",      "Light");
    if (!r.find("CameraComponent"))     r.registerType<CameraComponent>    ("CameraComponent",     "Camera");
    if (!r.find("StaticMeshComponent")) r.registerType<StaticMeshComponent>("StaticMeshComponent", "Mesh");
    if (!r.find("AudioComponent"))      r.registerType<AudioComponent>     ("AudioComponent",      "Audio");
    if (!r.find("RigidBodyComponent"))  r.registerType<RigidBodyComponent> ("RigidBodyComponent",  "RigidBody");
}
