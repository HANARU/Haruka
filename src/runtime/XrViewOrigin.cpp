#include "XrViewOrigin.hpp"

#include "../components/CameraComponent.hpp"
#include "../core/HLog.hpp"
#include "../core/ProjectSettings.hpp"
#include "../scene/World.hpp"
#include "../xr/XrSystem.hpp"

#include <string>

XrViewChoice XrViewOrigin::choose(World& world) const {
    XrViewChoice v;
    v.cam    = CameraComponent::activeIn(world);
    v.origin = hxr::chooseOrigin(v.cam ? v.cam->owner : nullptr);
    v.rig    = v.origin.kind == hxr::EXrOrigin::SceneRig ? v.origin.actor : nullptr;
    return v;
}

void XrViewOrigin::applyRigSpace(XrSystem& xr, const XrViewChoice& view, const ProjectSettings& project) const {
    if (!xr.hasSession()) return;
    const auto* rc = view.rig ? view.rig->getComponent<XrRigComponent>() : nullptr;
    xr.setRigSpace(rc ? rc->trackedHeight : project.xrTrackedHeight, rc ? rc->eyeHeight : project.xrEyeHeight);
}

void XrViewOrigin::applyToCamera(const XrViewChoice& view, XrSystem* xr, World& world) {
    CameraComponent* const viewCam = view.cam;
    if (!viewCam) return;
    const bool xrLive = xr && xr->hasSession();
    if (xrLive && viewCam->owner && view.origin.kind == hxr::EXrOrigin::TrackedParent) {
        // 머리 추적 카메라인데 부모가 리그가 아님: 그 부모(없으면 월드 원점)를 리그처럼 원점으로 씁니다. 예전에는 보이지
        // 않는 리그를 카메라에 붙여, 머리 추적이 이미 옮긴 카메라 위치 · 요를 원점으로 다시 써서 두 번 적용됐습니다(DEF-144).
        viewCam->setXrTrackedParent(view.origin.actor, xr->rigLift());
        implicitRigCam_ = 0;
        if (trackedOriginCam_ != viewCam->owner->id) {
            trackedOriginCam_ = viewCam->owner->id;
            HLOG(LogEngine, Log) << "XR rig: camera '" << viewCam->owner->name << "' tracks the head without an XR Rig parent; "
                                 << (view.origin.actor ? "its parent '" + view.origin.actor->name + "'" : std::string("the world origin"))
                                 << " is the tracking origin";
        }
    } else if (view.rig || !xrLive || !viewCam->owner) {
        // 씬 리그(원점 = 리그 액터), 또는 XR 이 아님(데스크톱 · 스테레오 흉내는 예전처럼 카메라 위치 + 요).
        viewCam->setXrRig(view.rig, xrLive ? xr->rigLift() : 0.f);
        if (!xrLive) implicitRigCam_ = 0;
    } else {
        // 보이지 않는 리그: 이 카메라로 XR 을 처음 볼 때 카메라 아래 바닥을 한 번 찾고, 그 뒤에는 카메라를 따라 같은
        // 거리 아래에 둡니다. 아래에 아무것도 없으면(우주 씬 등) 카메라 위치에 둡니다.
        Actor* const camActor = viewCam->owner;
        if (implicitRigCam_ != camActor->id) {
            const glm::vec3 cp = camActor->worldPosition();
            float floorY = 0.f;
            const bool found = hxr::floorBelow(world, cp, camActor, floorY);
            implicitRigDrop_ = found ? cp.y - floorY : 0.f;
            implicitRigCam_  = camActor->id;
            if (found) HLOG(LogEngine, Log) << "XR rig: none in the scene; standing on the floor " << implicitRigDrop_ << " m below the camera";
            else       HLOG(LogEngine, Log) << "XR rig: none in the scene and nothing below the camera; rig placed at the camera";
        }
        viewCam->setXrImplicitRig(implicitRigDrop_, xr->rigLift());
    }
}
