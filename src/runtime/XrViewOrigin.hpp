#pragma once

// ── 화면 카메라의 XR 원점 (2026-11-18 Engine::run 에서 떼어 냄, 엔진 구조 안정화 S2) ─────────────────────
// XR 에는 언제나 리그가 있습니다(2026-10-15 플레이어 리그, 2026-10-16 보이지 않는 리그). 프레임마다 화면 카메라를 보고
//   · 씬 리그의 머리면(카메라 액터에 TrackedPose[Head], 부모에 XrRig) 그 리그,
//   · 머리 추적은 있지만 부모가 리그가 아니면 그 부모(없으면 월드 원점, DEF-144),
//   · 아니면 보이지 않는 리그(카메라 아래 바닥을 한 번 찾아 그 거리 아래)
// 를 원점으로 카메라에 알립니다. 데스크톱 · 스테레오 흉내는 예전처럼 카메라 위치 + 요입니다. 로직은 예전 메인 루프 그대로입니다.

#include "../components/XrComponents.hpp"

#include <cstdint>

class CameraComponent;
class World;
class XrSystem;
struct ProjectSettings;

// 이번 프레임의 화면 카메라와 그 원점. 프레임을 시작하기 전에 정해 한 프레임 안에서는 바뀌지 않게 합니다.
struct XrViewChoice {
    CameraComponent*    cam    = nullptr;
    hxr::XrOriginChoice origin;
    const Actor*        rig    = nullptr;   // 씬 리그일 때만
};

class XrViewOrigin {
public:
    XrViewChoice choose(World& world) const;
    // 화면 카메라가 씬 리그의 머리면 그 리그의 높이 모드를, 아니면 프로젝트 설정의 값을 XR 에 넣습니다(세션이 있을 때).
    void applyRigSpace(XrSystem& xr, const XrViewChoice& view, const ProjectSettings& project) const;
    // 원점을 카메라에 알립니다. xr 은 없어도 됩니다(세션이 있는지는 여기서 봄).
    void applyToCamera(const XrViewChoice& view, XrSystem* xr, World& world);
    // 세션이 끝났습니다. 다음 Play 에서 바닥을 다시 찾고 원점 안내를 다시 남깁니다.
    void reset() { implicitRigCam_ = 0; trackedOriginCam_ = 0; }

private:
    // 리그가 없는 씬의 보이지 않는 리그(2026-10-16): 바닥을 찾은 카메라 액터(0 = 아직)와 카메라에서 바닥까지의 거리.
    uint64_t implicitRigCam_  = 0;
    float    implicitRigDrop_ = 0.f;
    // 머리 추적 카메라의 부모를 원점으로 쓴다고 알린 카메라 액터(0 = 아직, DEF-144). 같은 카메라는 한 번만 알립니다.
    uint64_t trackedOriginCam_ = 0;
};
