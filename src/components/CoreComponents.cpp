#include "CoreComponents.hpp"

#include "../scene/ComponentRegistry.hpp"
#include "AnimationComponent.hpp"
#include "../audio/AudioComponent.hpp"
#include "CameraComponent.hpp"
#include "HandPoseComponent.hpp"
#include "LightComponent.hpp"
#include "HeightFogComponent.hpp"
#include "ReflectionCaptureComponent.hpp"
#include "../physics/RigidBodyComponent.hpp"
#include "SkinnedMeshComponent.hpp"
#include "StaticMeshComponent.hpp"
#include "UIPanelComponent.hpp"
#include "ParticleSystemComponent.hpp"
#include "DecalComponent.hpp"
#include "../nav/NavAgentComponent.hpp"

void registerCoreComponents() {
    registerPhysicsComponents();
    registerAudioComponents();
    registerNavComponents();

    auto& r = ComponentRegistry::get();
    // 버튼 순서는 예전 에디터와 같게 둡니다(Light · Camera · Mesh · Audio · RigidBody).
    if (!r.find("LightComponent"))      r.registerType<LightComponent>     ("LightComponent",      "Light");
    if (!r.find("CameraComponent"))     r.registerType<CameraComponent>    ("CameraComponent",     "Camera");
    if (!r.find("StaticMeshComponent")) r.registerType<StaticMeshComponent>("StaticMeshComponent", "Mesh");
    if (!r.find("HeightFogComponent"))  r.registerType<HeightFogComponent> ("HeightFogComponent",  "Exponential Height Fog");   // 2026-11-17
    if (!r.find("ReflectionCaptureComponent")) r.registerType<ReflectionCaptureComponent>("ReflectionCaptureComponent", "Reflection Capture");   // 2026-11-17
    // 손 쥐기 A 단계(2026-11-07, Hand_Grasp_Design 4-1)
    if (!r.find("SkinnedMeshComponent")) r.registerType<SkinnedMeshComponent>("SkinnedMeshComponent", "Skinned Mesh");
    if (!r.find("HandPoseComponent"))    r.registerType<HandPoseComponent>   ("HandPoseComponent",    "Hand Pose");
    // 애니메이션(2026-11-20, Docs/Animation_System.md A3)
    if (!r.find("AnimationComponent"))   r.registerType<AnimationComponent>  ("AnimationComponent",   "Animation");
    // 파티클 · 데칼(2026-11-20, Docs/Particles_And_Decals.md)
    if (!r.find("ParticleSystemComponent")) r.registerType<ParticleSystemComponent>("ParticleSystemComponent", "Particle System");
    if (!r.find("DecalComponent"))       r.registerType<DecalComponent>      ("DecalComponent",       "Decal");
    // 3D UI 패널 P1b(2026-11-16, Spatial_Representation_Design 6단계)
    if (!r.find("UIPanelComponent"))     r.registerType<UIPanelComponent>    ("UIPanelComponent",     "UI Panel");

    // 에디터 Add Component 팝업의 분류(2026-11-21). 순서는 팝업이 정합니다(ComponentPickerModel.hpp).
    static const std::pair<const char*, const char*> kCategories[] = {
        { "LightComponent", "Rendering" }, { "CameraComponent", "Rendering" }, { "StaticMeshComponent", "Rendering" },
        { "SkinnedMeshComponent", "Rendering" }, { "DecalComponent", "Rendering" }, { "HeightFogComponent", "Rendering" },
        { "ReflectionCaptureComponent", "Rendering" },
        { "AnimationComponent", "Animation" },
        { "ParticleSystemComponent", "Effects" },
        { "HandPoseComponent", "XR" },
        { "UIPanelComponent", "UI" },
    };
    for (const auto& [name, cat] : kCategories) r.setCategory(name, cat);
}
