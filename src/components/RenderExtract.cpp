#include "RenderExtract.hpp"
#include "CameraComponent.hpp"
#include "DecalComponent.hpp"
#include "HeightFogComponent.hpp"
#include "LightComponent.hpp"
#include "ParticleSystemComponent.hpp"
#include "ReflectionCaptureComponent.hpp"
#include "SkinnedMeshComponent.hpp"
#include "StaticMeshComponent.hpp"
#include "SubtitlePanel.hpp"
#include "UIPanelComponent.hpp"
#include "../scene/Actor.hpp"
#include "../core/Accessibility.hpp"
#include "../scene/World.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

void extractRenderScene(const World& world, RenderSceneInput& out) {
    if (const CameraComponent* cam = CameraComponent::activeIn(world)) {
        out.camera.present    = true;
        out.camera.view       = cam->viewMatrix();
        out.camera.xrOrigin   = cam->xrOrigin();
        out.camera.fovY       = cam->fovY;
        out.camera.zNear      = cam->zNear;
        out.camera.zFar       = cam->zFar;
        out.camera.exposureEv = cam->exposureEv;
        // 접근성(Docs/Accessibility.md D6 · D9): 비네트 세기 · 색 보정. 서비스가 없으면(시험 월드 등) 0.
        if (const AccessibilityState* acc = world.service<AccessibilityState>()) {
            out.camera.vignette    = acc->vignette();
            out.camera.colorFilter = acc->access.colorFilter;
        }
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

    out.meshes.clear();
    const std::vector<StaticMeshComponent*> meshes = StaticMeshComponent::visibleIn(world);
    out.meshes.reserve(meshes.size());
    for (const StaticMeshComponent* mc : meshes) {
        if (!mc->visible || !mc->mesh) continue;
        RenderMesh rm;
        rm.key                = reinterpret_cast<uint64_t>(mc);
        rm.actorId            = mc->owner ? mc->owner->id : 0;
        rm.debugName          = mc->owner ? mc->owner->name.c_str() : "?";
        rm.mesh               = mc->mesh;
        rm.lodMeshes          = mc->lodMeshes.empty() ? nullptr : mc->lodMeshes.data();
        rm.lodMeshCount       = mc->lodMeshes.size();
        rm.lodScreenSizes     = mc->lodScreenSizes.empty() ? nullptr : mc->lodScreenSizes.data();
        rm.lodScreenSizeCount = mc->lodScreenSizes.size();
        rm.autoLod            = mc->autoLod;
        rm.material           = mc->material;
        rm.model              = mc->owner ? mc->owner->worldMatrix() : glm::mat4(1.f);
        rm.transparent        = mc->transparent;
        rm.lightChannels      = mc->lightChannels;
        out.meshes.push_back(rm);
    }

    out.skinned.clear();
    for (const SkinnedMeshComponent* sk : world.componentsOf<SkinnedMeshComponent>()) {
        const Actor* a = sk->owner;
        if (!a->activeInHierarchy()) continue;
        if (!sk->isEnabled() || !sk->visible || !sk->loaded()) continue;
        RenderSkinnedMesh rs;
        rs.source        = sk;
        rs.baseMesh      = sk->baseMesh;
        rs.vertCount     = (uint32_t)sk->vertCount;
        rs.actorId       = a->id;
        rs.debugName     = a->name.c_str();
        rs.material      = sk->material;
        rs.model         = a->worldMatrix();
        rs.transparent   = sk->transparent;
        rs.lightChannels = sk->lightChannels;
        out.skinned.push_back(rs);
    }

    out.capture = RenderReflectionCapture{};
    for (const ReflectionCaptureComponent* c : world.componentsOf<ReflectionCaptureComponent>()) {
        if (!c->owner->activeInHierarchy()) continue;
        if (c->isEnabled()) {
            out.capture.present    = true;
            out.capture.actorId    = c->owner->id;
            out.capture.pos        = c->owner->worldPosition();
            out.capture.radius     = c->radius;
            out.capture.intensity  = c->intensity;
            out.capture.version    = c->version();
            out.capture.worldToken = &world;
            break;
        }
    }

    out.panels.clear();
    for (UIPanelComponent* pc : world.componentsOf<UIPanelComponent>()) {
        const Actor* a = pc->owner;
        if (!a->activeInHierarchy()) continue;
        if (!pc->isEnabled()) continue;
        RenderUIPanel rp;
        rp.key         = reinterpret_cast<uint64_t>(pc);
        rp.source      = pc;
        rp.model       = a->worldMatrix() * glm::scale(glm::mat4(1.f), glm::vec3(pc->width, pc->height, 1.f));
        rp.textureSize = pc->textureSize();
        rp.twoSided    = pc->twoSided;
        rp.brightness  = std::clamp(pc->brightness, UIPanelComponent::kMinBrightness, 1.f);
        out.panels.push_back(rp);
    }
    // 자막(Docs/Accessibility.md D4): 엔진이 가진 표시가 보일 때만 패널 목록 끝에 더합니다(액터가 아님).
    if (SubtitlePanel* sp = world.service<SubtitlePanel>(); sp && sp->visible()) {
        RenderUIPanel rp;
        rp.key         = reinterpret_cast<uint64_t>(sp);
        rp.source      = sp;
        rp.model       = sp->model();
        rp.textureSize = sp->textureSize();
        out.panels.push_back(rp);
    }

    out.particles.clear();
    for (const ParticleSystemComponent* ps : world.componentsOf<ParticleSystemComponent>()) {
        if (!ps->owner->activeInHierarchy() || !ps->isEnabled()) continue;
        if (ps->instances().empty()) continue;
        RenderParticleBatch b;
        b.data   = ps->instances().data();
        b.count  = (uint32_t)ps->instances().size();
        b.blend  = ps->desc.blend;
        b.center = ps->center();
        out.particles.push_back(b);
    }

    out.decals.clear();
    for (const DecalComponent* dc : world.componentsOf<DecalComponent>()) {
        const Actor* a = dc->owner;
        if (!a->activeInHierarchy() || !dc->isEnabled() || !(dc->opacity > 0.f)) continue;
        const glm::mat4 box = a->worldMatrix() * glm::scale(glm::mat4(1.f), dc->size);
        if (std::fabs(glm::determinant(glm::mat3(box))) < 1e-12f) continue;   // 크기 0 인 축
        RenderDecal d;
        d.worldToBox = glm::inverse(box);
        const glm::vec3 up = glm::vec3(box[1]);
        d.axis      = glm::normalize(up);
        d.center    = glm::vec3(box[3]);
        d.color     = dc->color;
        d.opacity   = dc->opacity;
        d.roughness = dc->roughness;
        d.blend     = (int32_t)dc->blend;
        d.shape     = (int32_t)dc->shape;
        out.decals.push_back(d);
    }
}
