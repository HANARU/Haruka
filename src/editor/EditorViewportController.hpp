#pragma once
#include "EditorContext.hpp"
#include "EditorGizmo.hpp"
#include "ComponentVisualizers.hpp"
#include "HCommandStack.hpp"
#include "../components/CameraComponent.hpp"
#include "../renderer/MeshHandle.hpp"
#include "../ui/slate/HSlate.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <vector>

class Actor;
class World;

// 뷰포트 조작 컨트롤러(S6g).
// 에디터 카메라(WASD 비행 · RMB 시선 · LMB 오빗 · 휠 돌리 · F 맞춤 애니메이션),
// 기즈모(Q/W/E/R 단축키 · 드래그 · 오버레이 선/삼각형 수집), 뷰포트 좌표 변환 및 액터 피킹을 담당합니다.
// gizmoMode_ · worldSpace_ · snap_ · camSpeed_ 는 EditorApp 이 소유하고 포인터로 받습니다.
class EditorViewportController {
public:
    struct Deps {
        const EditorContext*        ctx         = nullptr;
        HCommandStack*              cmdStack    = nullptr;
        const ComponentVisualizers* visualizers = nullptr;
        int*                        gizmoMode   = nullptr;
        bool*                       worldSpace  = nullptr;
        HGizmoSnap*                 snap        = nullptr;
        float*                      camSpeed    = nullptr;

        std::function<bool()>              isModalOpen;
        std::function<bool()>              isMenusOpen;
        std::function<bool()>              isViewportVisible;
        std::function<bool()>              isTextInputFree;
        std::function<HViewportRect()>     vpToolbarFbRect;
        std::function<bool(glm::vec2)>     logPopupContains;
        std::function<bool(const Actor*)>  isSelected;
        std::function<void(Actor*)>        onSelectOnly;
        std::function<void(Actor*)>        onToggleSelect;
        std::function<void()>              onSyncEulerCache;
        std::function<void()>              onBeginGroupDrag;
        std::function<void()>              onUpdateGroupDrag;
        std::function<void()>              onClearGroupDrag;
        std::function<void(const Transform&, const Transform&)> onCommitGizmoDrag;
    };

    void init(Deps deps);
    void spawnEditorCamera(World& world);
    void cleanup(World* world);
    std::shared_ptr<SBorder> buildViewportWidget();

    void tickCamera(float dt, float scroll);
    void tickHotkeys();
    void tickOverlay(float uiScale, std::vector<LineVertex>* outLines, std::vector<LineVertex>* outTris);

    bool handleGizmoDrag(float mx, float my, float winW, float winH, bool leftDown, bool prevLeftDown);
    void onViewportPress(float mx, float my, float winW, float winH, bool pointerOverWorldUi);
    void releaseOrbit(bool leftDown) { if (!leftDown) lmbOrbit_ = false; }

    void pickActor(float mx, float my, float winW, float winH);
    bool pointerRay(float mx, float my, float winW, float winH, glm::vec3& origin, glm::vec3& dir) const;
    bool inViewport(float mx, float my, float winW, float winH) const;

    HViewportRect fullFbRect() const;
    HViewportRect viewportFbRect() const;
    glm::vec2     mouseToFb(float mx, float my, float winW, float winH) const;
    WorldViewport worldVP(float mx, float my, float winW, float winH) const;

    Actor*           cameraActor() const    { return editorCamActor_; }
    Actor* const*    cameraActorPtr() const { return &editorCamActor_; }
    CameraComponent* camera() const         { return editorCam_; }
    void             setViewPreferred(bool p) { viewCamera_.preferred = p; }
    void             startFocusAnim(const glm::vec3& from, const glm::vec3& to);

    EditorGizmo&       gizmo()       { return gizmo_; }
    const EditorGizmo& gizmo() const { return gizmo_; }

private:
    void buildIcons(World& world, std::vector<LineVertex>& lines, std::vector<LineVertex>& tris) const;

    Deps deps_;
    Actor*              editorCamActor_ = nullptr;
    CameraComponent*    editorCam_      = nullptr;
    ViewCameraOverride  viewCamera_{};
    float               editorYaw_      = 0.f;
    float               editorPitch_    = 0.15f;
    bool                lmbOrbit_       = false;

    bool      focusAnim_ = false;
    glm::vec3 focusFrom_{ 0.f };
    glm::vec3 focusTo_  { 0.f };
    float     focusT_    = 0.f;

    EditorGizmo gizmo_;
    bool        prevGizmoDragging_ = false;
    bool        prevGizQDown_      = false;
    bool        prevGizWDown_      = false;
    bool        prevGizEDown_      = false;
    bool        prevGizRDown_      = false;

    std::shared_ptr<SBorder> viewportWidget_;
    mutable HViewportRect    lastValidVpRect_{};
};
