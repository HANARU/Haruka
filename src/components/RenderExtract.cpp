#include "RenderExtract.hpp"
#include "CameraComponent.hpp"
#include "HeightFogComponent.hpp"
#include "LightComponent.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
#include <algorithm>

void extractRenderScene(const World& world, RenderSceneInput& out) {
    if (const CameraComponent* cam = CameraComponent::activeIn(world)) {
        out.camera.present    = true;
        out.camera.view       = cam->viewMatrix();
        out.camera.xrOrigin   = cam->xrOrigin();
        out.camera.fovY       = cam->fovY;
        out.camera.zNear      = cam->zNear;
        out.camera.zFar       = cam->zFar;
        out.camera.exposureEv = cam->exposureEv;
    } else {
        out.camera = RenderCamera{};
    }

    out.lights.clear();
    const std::vector<LightComponent*> lights = LightComponent::enabledIn(world);
    out.lights.reserve(lights.size());
    for (const LightComponent* lc : lights) {
        RenderLight rl;
        switch (lc->lightType) {
            case LightType::Point: rl.kind = RenderLightKind::Point;       break;
            case LightType::Spot:  rl.kind = RenderLightKind::Spot;        break;
            default:               rl.kind = RenderLightKind::Directional; break;
        }
        rl.position            = lc->owner ? glm::vec3(lc->owner->worldMatrix()[3]) : glm::vec3(0.f);
        rl.emitDirection       = lc->emitDirection();
        rl.color               = lc->effectiveColor() * lc->intensity;
        rl.radius              = std::max(lc->attenuationRadius, LightComponent::kMinAttenuationRadius);
        rl.innerCone           = lc->innerConeAngle;
        rl.outerCone           = lc->outerConeAngle;
        rl.contactShadowLength = lc->contactShadowLength;
        rl.channels            = lc->lightChannels;
        rl.castShadows         = lc->castShadows;
        out.lights.push_back(rl);
    }

    out.fog = RenderFog{};
    for (const HeightFogComponent* fog : world.componentsOf<HeightFogComponent>()) {
        if (!fog->owner->activeInHierarchy() || !fog->isEnabled()) continue;
        out.fog.present       = true;
        out.fog.color         = fog->color;
        out.fog.maxOpacity    = fog->maxOpacity;
        out.fog.density       = fog->density;
        out.fog.heightFalloff = fog->heightFalloff;
        out.fog.height        = fog->owner->worldPosition().y;
        out.fog.startDistance = fog->startDistance;
        break;
    }
}
