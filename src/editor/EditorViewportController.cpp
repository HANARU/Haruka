#include "EditorViewportController.hpp"
#include "EditorTheme.hpp"
#include "../components/LightComponent.hpp"
#include "../components/XrComponents.hpp"
#include "../core/DebugDraw.hpp"
#include "../input/HInputSystem.hpp"
#include "../math/Geometry.hpp"
#include "../platform/IWindow.hpp"
#include "../renderer/Renderer.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
#define GLM_FORCE_RADIANS
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

void appendXrControllerLines(World& world, std::vector<LineVertex>& lines) {
    const HXrInputState& xr = HInputSystem::get().xr();
    if (!xr.focused) return;
    CameraComponent* cam = CameraComponent::activeIn(world);
    const glm::mat4 origin = cam ? cam->xrOrigin() : glm::mat4(1.f);
    auto worldOf = [&](const HPose& p) {
        return origin * glm::translate(glm::mat4(1.f), p.position) * glm::mat4_cast(p.orientation);
    };
    for (const HHandInput& h : xr.hands) {
        if (!h.active) continue;
        if (h.grip.valid) {
            const glm::mat4 m = worldOf(h.grip);
            const glm::vec3 o(m[3]);
            const glm::vec4 axisColor[3] = { { 1.f, 0.25f, 0.25f, 1.f }, { 0.25f, 1.f, 0.25f, 1.f },
                                             { 0.3f, 0.5f, 1.f, 1.f } };
            for (int k = 0; k < 3; ++k) {
                lines.push_back({ o, axisColor[k] });
                lines.push_back({ o + glm::vec3(m[k]) * 0.1f, axisColor[k] });
            }
        }
        if (h.aim.valid) {
            const glm::mat4 m = worldOf(h.aim);
            const glm::vec3 o(m[3]);
            const glm::vec3 fwd = -glm::vec3(m[2]);
            const float     t   = std::clamp(h.trigger, 0.f, 1.f);
            const glm::vec4 c(1.f, 1.f, 1.f - 0.8f * t, 1.f);
            lines.push_back({ o, c });
            lines.push_back({ o + fwd * (0.5f + 1.5f * std::clamp(h.squeeze, 0.f, 1.f)), c });
        }
    }
}

} // namespace

void EditorViewportController::init(Deps deps) {
    deps_ = std::move(deps);
    gizmo_.onCommit = [this](const Transform& before, const Transform& after) {
        if (deps_.onCommitGizmoDrag) deps_.onCommitGizmoDrag(before, after);
    };
}

void EditorViewportController::spawnEditorCamera(World& world) {
    editorCamActor_ = world.spawnActor("Camera");
    editorCamActor_->setLocalPosition({ 0.f, 0.3f, 2.5f });
    editorCamActor_->setLocalRotation(CameraComponent::yawPitchRotation(editorYaw_, editorPitch_));
    editorCam_ = editorCamActor_->addComponent<CameraComponent>();
    viewCamera_.camera    = editorCam_;
    viewCamera_.preferred = true;
    world.setService<ViewCameraOverride>(&viewCamera_);
}

void EditorViewportController::cleanup(World* world) {
    if (world && world->service<ViewCameraOverride>() == &viewCamera_)
        world->setService<ViewCameraOverride>(nullptr);
}

std::shared_ptr<SBorder> EditorViewportController::buildViewportWidget() {
    viewportWidget_ = HNew(SBorder)->background(EditorTheme::transparent);
    return viewportWidget_;
}

void EditorViewportController::startFocusAnim(const glm::vec3& from, const glm::vec3& to) {
    focusFrom_ = from;
    focusTo_   = to;
    focusT_    = 0.f;
    focusAnim_ = true;
}

HViewportRect EditorViewportController::fullFbRect() const {
    int fw = 1, fh = 1;
    if (deps_.ctx->window) deps_.ctx->window->getFramebufferSize(fw, fh);
    return { 0.f, 0.f, (float)fw, (float)fh };
}

HViewportRect EditorViewportController::viewportFbRect() const {
    if (viewportWidget_) {
        const HGeometry& g = viewportWidget_->cachedGeometry();
        if (g.size.x >= 1.f && g.size.y >= 1.f) {
            lastValidVpRect_ = { std::floor(g.position.x), std::floor(g.position.y),
                                 std::floor(g.size.x),     std::floor(g.size.y) };
            return lastValidVpRect_;
        }
    }
    if (lastValidVpRect_.width >= 1.f && lastValidVpRect_.height >= 1.f)
        return lastValidVpRect_;
    return fullFbRect();
}

glm::vec2 EditorViewportController::mouseToFb(float mx, float my, float winW, float winH) const {
    int fw = 1, fh = 1;
    if (deps_.ctx->window) deps_.ctx->window->getFramebufferSize(fw, fh);
    return { mx * (float)fw / std::max(1.f, winW),
             my * (float)fh / std::max(1.f, winH) };
}

WorldViewport EditorViewportController::worldVP(float mx, float my, float winW, float winH) const {
    return { viewportFbRect(), mouseToFb(mx, my, winW, winH) };
}

bool EditorViewportController::pointerRay(float mx, float my, float winW, float winH,
                                          glm::vec3& origin, glm::vec3& dir) const {
    if (!deps_.ctx->world || !inViewport(mx, my, winW, winH)) return false;
    CameraComponent* cam = CameraComponent::activeIn(*deps_.ctx->world);
    if (!cam || !cam->owner || cam == editorCam_) return false;
    const WorldViewport wvp = worldVP(mx, my, winW, winH);
    if (wvp.fbRect.width <= 0.f || wvp.fbRect.height <= 0.f) return false;
    return wvp.ray(cam->projMatrix(wvp.aspect()) * cam->viewMatrix(), origin, dir);
}

bool EditorViewportController::inViewport(float mx, float my, float winW, float winH) const {
    if (deps_.isViewportVisible && !deps_.isViewportVisible()) return false;
    if (deps_.isMenusOpen       &&  deps_.isMenusOpen())       return false;
    if (deps_.isModalOpen       &&  deps_.isModalOpen())       return false;
    const glm::vec2 fb = mouseToFb(mx, my, winW, winH);
    if (deps_.vpToolbarFbRect) {
        const HViewportRect tb = deps_.vpToolbarFbRect();
        if (tb.width >= 1.f && tb.contains(fb.x, fb.y)) return false;
    }
    if (deps_.logPopupContains && deps_.logPopupContains(fb)) return false;
    return viewportFbRect().contains(fb.x, fb.y);
}

void EditorViewportController::tickHotkeys() {
    const bool drivingPlayer = deps_.ctx->world && CameraComponent::activeIn(*deps_.ctx->world) != editorCam_;
    const bool modalOpen     = deps_.isModalOpen && deps_.isModalOpen();
    const bool textFree      = !deps_.isTextInputFree || deps_.isTextInputFree();
    const bool menuKeys      = deps_.isKeyboardCaptured && deps_.isKeyboardCaptured();   // 열린 메뉴가 키를 받음(H2b)
    if (deps_.ctx->window && textFree && !menuKeys && !gizmo_.isDragging() &&
        !deps_.ctx->window->isCameraModeActive() && !modalOpen && !drivingPlayer) {
        IWindow& win = *deps_.ctx->window;
        bool qNow = win.isKeyDown(HInput::KEY_Q);
        bool wNow = win.isKeyDown(HInput::KEY_W);
        bool eNow = win.isKeyDown(HInput::KEY_E);
        bool rNow = win.isKeyDown(HInput::KEY_R);
        if (qNow && !prevGizQDown_) *deps_.gizmoMode = 0;
        if (wNow && !prevGizWDown_) *deps_.gizmoMode = 1;
        if (eNow && !prevGizEDown_) *deps_.gizmoMode = 2;
        if (rNow && !prevGizRDown_) *deps_.gizmoMode = 3;
        prevGizQDown_ = qNow; prevGizWDown_ = wNow;
        prevGizEDown_ = eNow; prevGizRDown_ = rNow;
    }
}

void EditorViewportController::tickCamera(float dt, float scroll) {
    if (!editorCam_ || !editorCamActor_ || !deps_.ctx->window) return;
    IWindow& win = *deps_.ctx->window;

    if (deps_.ctx->world && CameraComponent::activeIn(*deps_.ctx->world) != editorCam_) {
        float rx = 0.f, ry = 0.f, lx = 0.f, ly = 0.f;
        win.getRightMouseDelta(rx, ry);
        win.getMouseDelta(lx, ly);
        HInputSystem::get().setLookDelta(rx, ry);
        focusAnim_ = false;
        return;
    }

    if (focusAnim_) {
        if (win.isCameraModeActive() || lmbOrbit_) {
            focusAnim_ = false;
        } else {
            focusT_ = std::min(1.f, focusT_ + dt / 0.25f);
            const float t = focusT_ * focusT_ * (3.f - 2.f * focusT_);
            editorCamActor_->setLocalPosition(glm::mix(focusFrom_, focusTo_, t));
            if (focusT_ >= 1.f) focusAnim_ = false;
        }
    }

    const glm::vec3 fwd = editorCam_->forward();
    const glm::vec3 rgt = glm::normalize(glm::cross(fwd, glm::vec3(0.f, 1.f, 0.f)));

    if (win.isCameraModeActive()) {
        const float speed = *deps_.camSpeed * dt;
        if (win.isKeyDown(HInput::KEY_W)) editorCamActor_->translateLocal(fwd * speed);
        if (win.isKeyDown(HInput::KEY_S)) editorCamActor_->translateLocal(-fwd * speed);
        if (win.isKeyDown(HInput::KEY_D)) editorCamActor_->translateLocal(rgt * speed);
        if (win.isKeyDown(HInput::KEY_A)) editorCamActor_->translateLocal(-rgt * speed);
        if (win.isKeyDown(HInput::KEY_E)) editorCamActor_->translateLocal(glm::vec3(0,1,0) * speed);
        if (win.isKeyDown(HInput::KEY_Q)) editorCamActor_->translateLocal(-glm::vec3(0,1,0) * speed);
    }

    float rx, ry;
    win.getRightMouseDelta(rx, ry);
    if (rx != 0 || ry != 0) {
        editorYaw_   += rx * 0.003f;
        editorPitch_  = std::clamp(editorPitch_ + ry * 0.003f, -1.5f, 1.5f);
        editorCamActor_->setLocalRotation(CameraComponent::yawPitchRotation(editorYaw_, editorPitch_));
    }

    float lx, ly;
    win.getMouseDelta(lx, ly);
    if (lmbOrbit_) {
        if (lx != 0 || ly != 0) {
            glm::vec3 up = glm::cross(rgt, fwd);
            editorCamActor_->translateLocal(-rgt * (lx * 0.005f));
            editorCamActor_->translateLocal(up * (ly * 0.005f));
        }
    }

    if (scroll != 0.f)
        editorCamActor_->translateLocal(fwd * (scroll * 0.3f));
}

void EditorViewportController::tickOverlay(float uiScale,
                                           std::vector<LineVertex>* outLines,
                                           std::vector<LineVertex>* outTris) {
    if (!deps_.ctx->renderer || !deps_.ctx->world) return;
    const HViewportRect vfr = viewportFbRect();
    deps_.ctx->renderer->setViewportRect((int)vfr.left, (int)vfr.top,
                                         (int)vfr.width, (int)vfr.height);
    float mx = 0.f, my = 0.f; int wW = 1, wH = 1;
    if (deps_.ctx->window) {
        deps_.ctx->window->getMousePosition(mx, my);
        deps_.ctx->window->getWindowSize(wW, wH);
    }
    const WorldViewport wvp = worldVP(mx, my, (float)wW, (float)wH);
    const bool inView = inViewport(mx, my, (float)wW, (float)wH);
    std::vector<LineVertex> lines, tris;
    Actor* gizmoTarget =
        (deps_.ctx->selected && deps_.ctx->selected->activeInHierarchy()) ? deps_.ctx->selected : nullptr;
    gizmo_.setSnap(*deps_.snap);
    gizmo_.tick(gizmoTarget, *deps_.ctx->world, wvp, 18.f * uiScale,
                *deps_.gizmoMode, *deps_.worldSpace, inView, lines, tris);
    for (Actor* o : deps_.ctx->selection)
        if (o && o != deps_.ctx->selected && o->activeInHierarchy())
            EditorGizmo::appendSelectionBox(o, lines, { 0.85f, 0.65f, 0.15f, 1.f });
    buildIcons(*deps_.ctx->world, lines, tris);
    if (TrackedPoseComponent::liveHandsIn(*deps_.ctx->world) == 0)
        appendXrControllerLines(*deps_.ctx->world, lines);
    if (deps_.visualizers) {
        for (Actor* o : deps_.ctx->selection)
            if (o && o->activeInHierarchy())
                for (const auto& c : o->getComponents())
                    if (c->isEnabled()) deps_.visualizers->drawSelected(*c);
    }
    for (const hdebug::Line& l : hdebug::lines()) {
        lines.push_back({ l.a, l.color });
        lines.push_back({ l.b, l.color });
    }
    hdebug::lines().clear();
    if (outLines) *outLines = lines;
    if (outTris)  *outTris  = tris;
    deps_.ctx->renderer->setEditorLines(std::move(lines));
    deps_.ctx->renderer->setGizmoTris(std::move(tris));
}

bool EditorViewportController::handleGizmoDrag(float mx, float my, float winW, float winH,
                                               bool leftDown, bool prevLeftDown) {
    bool gizmoConsumed = false;
    if (deps_.ctx->world && deps_.ctx->window && deps_.ctx->selected &&
        deps_.ctx->selected != editorCamActor_ &&
        (deps_.ctx->selected->activeInHierarchy() || gizmo_.isDragging())) {
        const bool inView = inViewport(mx, my, winW, winH) &&
                            deps_.ctx->selected->activeInHierarchy();
        gizmoConsumed = gizmo_.handleDrag(deps_.ctx->selected, *deps_.ctx->world, *deps_.cmdStack,
                                          worldVP(mx, my, winW, winH),
                                          leftDown, prevLeftDown,
                                          *deps_.gizmoMode, *deps_.worldSpace, inView);
    } else if (gizmo_.isDragging()) {
        gizmo_.cancel();
    }
    const bool dragNow = gizmo_.isDragging();
    if (dragNow) {
        if (!prevGizmoDragging_ && deps_.onBeginGroupDrag) deps_.onBeginGroupDrag();
        if (deps_.onUpdateGroupDrag) deps_.onUpdateGroupDrag();
    } else if (prevGizmoDragging_ && deps_.onClearGroupDrag) {
        deps_.onClearGroupDrag();
    }
    if (prevGizmoDragging_ && !dragNow && *deps_.gizmoMode == 2 && deps_.onSyncEulerCache)
        deps_.onSyncEulerCache();
    prevGizmoDragging_ = dragNow;
    return gizmoConsumed;
}

void EditorViewportController::onViewportPress(float mx, float my, float winW, float winH,
                                               bool pointerOverWorldUi) {
    if (inViewport(mx, my, winW, winH) && !pointerOverWorldUi) {
        pickActor(mx, my, winW, winH);
        lmbOrbit_ = true;
    }
}

void EditorViewportController::pickActor(float mx, float my, float winW, float winH) {
    if (!deps_.ctx->world) return;
    CameraComponent* cam = CameraComponent::activeIn(*deps_.ctx->world);
    if (!cam || !cam->owner) return;

    const WorldViewport wvp = worldVP(mx, my, winW, winH);
    const HViewportRect& fb = wvp.fbRect;
    if (fb.width <= 0.f || fb.height <= 0.f) return;

    const glm::mat4 vp = cam->projMatrix(wvp.aspect()) * cam->viewMatrix();
    glm::vec3 rayO, rayD;
    if (!wvp.ray(vp, rayO, rayD)) return;

    const bool ctrl = deps_.ctx->window && deps_.ctx->window->isCtrlDown();
    auto pick = [&](Actor* a) {
        if (ctrl) { if (a && deps_.onToggleSelect) deps_.onToggleSelect(a); }
        else      { if (deps_.onSelectOnly)        deps_.onSelectOnly(a); }
    };

    // ── 아이콘 픽 우선 ────────────────────────────────────────────────────────
    {
        auto toFb = [&](const glm::vec3& wp) { return wvp.project(vp, wp); };
        Actor* iconBest = nullptr;
        float  iconBestD = 18.f;
        for (auto& ap : deps_.ctx->world->getActors()) {
            Actor* a = ap.get();
            if (a == editorCamActor_ || !a->activeInHierarchy()) continue;
            glm::vec4 c; bool has = false;
            if (deps_.visualizers)
                for (const auto& comp : a->getComponents())
                    if (deps_.visualizers->icon(*comp, c)) { has = true; break; }
            if (!has) continue;
            float d = glm::length(toFb(a->worldPosition()) - wvp.mouseFb);
            if (d < iconBestD) { iconBestD = d; iconBest = a; }
        }
        if (iconBest) { pick(iconBest); return; }
    }

    Actor* best  = nullptr;
    float  bestT = std::numeric_limits<float>::max();
    for (auto& ap : deps_.ctx->world->getActors()) {
        Actor* a = ap.get();
        if (!a->activeInHierarchy()) continue;
        glm::vec3 lo(0.f), hi(0.f);
        bool hasBounds = false;
        for (const auto& comp : a->getComponents()) {
            glm::vec3 cmin, cmax;
            if (comp->localBounds(cmin, cmax)) {
                if (!hasBounds) { lo = cmin; hi = cmax; hasBounds = true; }
                else { lo = glm::min(lo, cmin); hi = glm::max(hi, cmax); }
            }
        }
        if (!hasBounds) continue;

        const glm::mat4  wm    = a->worldMatrix();
        const hmath::Ray local = hmath::transformRay({ rayO, rayD }, hmath::affineInverse(wm));
        float tmin = 0.f, tmax = 0.f;
        if (!hmath::intersectRayAabb(local, { lo, hi }, tmin, tmax)) continue;

        const float     tHit     = (tmin >= 0.f) ? tmin : tmax;
        const glm::vec3 localHit = local.origin + local.dir * tHit;
        const glm::vec3 worldHit = glm::vec3(wm * glm::vec4(localHit, 1.f));
        const float     worldT   = glm::length(worldHit - rayO);
        if (worldT < bestT) { bestT = worldT; best = a; }
    }
    pick(best);
}

bool EditorViewportController::nearestBoundsHit(const glm::vec3& rayO, const glm::vec3& rayD, float maxT, float& t,
                                                Actor*& actor) const {
    t = maxT;
    actor = nullptr;
    if (!deps_.ctx || !deps_.ctx->world) return false;
    for (auto& ap : deps_.ctx->world->getActors()) {
        Actor* a = ap.get();
        if (a == editorCamActor_ || !a->activeInHierarchy()) continue;
        glm::vec3 lo(0.f), hi(0.f);
        bool hasBounds = false;
        for (const auto& comp : a->getComponents()) {
            glm::vec3 cmin, cmax;
            if (comp->localBounds(cmin, cmax)) {
                if (!hasBounds) { lo = cmin; hi = cmax; hasBounds = true; }
                else { lo = glm::min(lo, cmin); hi = glm::max(hi, cmax); }
            }
        }
        if (!hasBounds) continue;
        const glm::mat4  wm    = a->worldMatrix();
        const hmath::Ray local = hmath::transformRay({ rayO, rayD }, hmath::affineInverse(wm));
        float tmin = 0.f, tmax = 0.f;
        if (!hmath::intersectRayAabb(local, { lo, hi }, tmin, tmax) || tmin < 0.f) continue;   // 광선 시작이 상자 안이면 건너뜀
        const glm::vec3 worldHit = glm::vec3(wm * glm::vec4(local.origin + local.dir * tmin, 1.f));
        const float     worldT   = glm::length(worldHit - rayO);
        if (worldT < t) { t = worldT; actor = a; }
    }
    return actor != nullptr;
}

glm::vec3 EditorViewportController::placementPoint() const {
    if (!editorCamActor_) return glm::vec3(0.f);
    const glm::mat4 cm   = editorCamActor_->worldMatrix();
    const glm::vec3 rayO = glm::vec3(cm[3]);
    const glm::vec3 rayD = -glm::normalize(glm::vec3(cm[2]));
    float t = 0.f;
    Actor* hit = nullptr;
    return rayO + rayD * (nearestBoundsHit(rayO, rayD, 20.f, t, hit) ? t : 3.f);
}

bool EditorViewportController::dropPoint(float mx, float my, float winW, float winH, glm::vec3& point, Actor** hitActor) const {
    if (hitActor) *hitActor = nullptr;
    if (!deps_.ctx || !deps_.ctx->world || !inViewport(mx, my, winW, winH)) return false;
    CameraComponent* cam = CameraComponent::activeIn(*deps_.ctx->world);
    if (!cam || !cam->owner) return false;
    const WorldViewport wvp = worldVP(mx, my, winW, winH);
    if (wvp.fbRect.width <= 0.f || wvp.fbRect.height <= 0.f) return false;
    const glm::mat4 vp = cam->projMatrix(wvp.aspect()) * cam->viewMatrix();
    glm::vec3 rayO, rayD;
    if (!wvp.ray(vp, rayO, rayD)) return false;
    rayD = glm::normalize(rayD);
    // 아이콘이 있는 액터(소리 · 라이트 등, 메시가 없어 경계 상자가 없음)는 고르기와 같이 아이콘 18 px 안이면 닿은 것으로 봅니다.
    if (hitActor && deps_.visualizers) {
        Actor* iconBest = nullptr;
        float  iconBestD = 18.f;
        for (auto& ap : deps_.ctx->world->getActors()) {
            Actor* a = ap.get();
            if (a == editorCamActor_ || !a->activeInHierarchy()) continue;
            glm::vec4 c;
            bool has = false;
            for (const auto& comp : a->getComponents())
                if (deps_.visualizers->icon(*comp, c)) { has = true; break; }
            if (!has) continue;
            const float d = glm::length(wvp.project(vp, a->worldPosition()) - wvp.mouseFb);
            if (d < iconBestD) { iconBestD = d; iconBest = a; }
        }
        if (iconBest) {
            point = iconBest->worldPosition();
            *hitActor = iconBest;
            return true;
        }
    }
    float t = 0.f;
    Actor* hit = nullptr;
    if (nearestBoundsHit(rayO, rayD, 200.f, t, hit)) {
        point = rayO + rayD * t;
        if (hitActor) *hitActor = hit;
        return true;
    }
    if (rayD.y < -1e-4f) {
        const float tp = -rayO.y / rayD.y;
        if (tp > 0.f && tp < 200.f) { point = rayO + rayD * tp; return true; }
    }
    point = rayO + rayD * 5.f;
    return true;
}

void EditorViewportController::buildIcons(World& world,
                                          std::vector<LineVertex>& lines,
                                          std::vector<LineVertex>& tris) const {
    CameraComponent* cam = CameraComponent::activeIn(world);
    if (!cam || !cam->owner) return;
    const glm::vec3 camPos  = cam->owner->worldPosition();
    const glm::mat4 invView = glm::inverse(cam->viewMatrix());
    const glm::vec3 right = glm::normalize(glm::vec3(invView[0]));
    const glm::vec3 up    = glm::normalize(glm::vec3(invView[1]));

    for (auto& ap : world.getActors()) {
        Actor* a = ap.get();
        if (a == editorCamActor_ || !a->activeInHierarchy()) continue;
        glm::vec4 color{0.f}; bool has = false;
        if (deps_.visualizers)
            for (const auto& comp : a->getComponents())
                if (deps_.visualizers->icon(*comp, color)) { has = true; break; }
        if (!has) continue;

        const glm::vec3 pos  = a->worldPosition();
        const float     dist = glm::length(camPos - pos);
        const float     ssz  = std::max(0.02f, dist * 0.03f);
        const bool      sel  = deps_.isSelected && deps_.isSelected(a);
        const glm::vec4 col  = sel ? glm::vec4(1.f, 1.f, 1.f, 1.f) : color;

        const glm::vec3 r = right * ssz, u = up * ssz;
        const glm::vec3 p0 = pos - r - u, p1 = pos + r - u, p2 = pos + r + u, p3 = pos - r + u;
        int shape = 0;
        if (deps_.visualizers)
            for (const auto& comp : a->getComponents())
                if (glm::vec4 tmp; deps_.visualizers->icon(*comp, tmp, &shape)) break;
        const glm::vec4 bg = glm::vec4(glm::vec3(col) * 0.35f, 1.f);
        auto quad = [&](glm::vec2 a0, glm::vec2 a1, const glm::vec4& c) {
            const glm::vec3 q0 = pos + right * (ssz * a0.x) + up * (ssz * a0.y), q1 = pos + right * (ssz * a1.x) + up * (ssz * a0.y);
            const glm::vec3 q2 = pos + right * (ssz * a1.x) + up * (ssz * a1.y), q3 = pos + right * (ssz * a0.x) + up * (ssz * a1.y);
            tris.push_back({q0,c}); tris.push_back({q1,c}); tris.push_back({q2,c});
            tris.push_back({q0,c}); tris.push_back({q2,c}); tris.push_back({q3,c});
        };
        auto tri = [&](glm::vec2 a0, glm::vec2 a1, glm::vec2 a2, const glm::vec4& c) {
            auto at = [&](glm::vec2 q) { return pos + right * (ssz * q.x) + up * (ssz * q.y); };
            tris.push_back({at(a0),c}); tris.push_back({at(a1),c}); tris.push_back({at(a2),c});
        };
        auto disk = [&](glm::vec2 c0, float rad, const glm::vec4& c) {
            for (int k = 0; k < 12; ++k) {
                const float t0 = 6.2831853f * k / 12.f, t1 = 6.2831853f * (k + 1) / 12.f;
                tri(c0, c0 + rad * glm::vec2(std::cos(t0), std::sin(t0)), c0 + rad * glm::vec2(std::cos(t1), std::sin(t1)), c);
            }
        };
        if (shape == 0) {
            quad({ -1.f, -1.f }, { 1.f, 1.f }, col);
        } else {
            quad({ -1.f, -1.f }, { 1.f, 1.f }, bg);
            switch (shape) {
                case 1: disk({ 0.f, 0.15f }, 0.5f, col); quad({ -0.22f, -0.75f }, { 0.22f, -0.35f }, col); break;
                case 2: quad({ -0.8f, -0.45f }, { 0.3f, 0.45f }, col); tri({ 0.3f, 0.f }, { 0.85f, 0.45f }, { 0.85f, -0.45f }, col); break;
                case 3: quad({ -0.75f, -0.3f }, { -0.3f, 0.3f }, col); tri({ -0.3f, 0.f }, { 0.35f, 0.7f }, { 0.35f, -0.7f }, col); quad({ 0.5f, -0.35f }, { 0.62f, 0.35f }, col); break;
                case 4: quad({ -0.8f, -0.8f }, { 0.8f, -0.6f }, col); quad({ -0.8f, 0.6f }, { 0.8f, 0.8f }, col); quad({ -0.8f, -0.8f }, { -0.6f, 0.8f }, col); quad({ 0.6f, -0.8f }, { 0.8f, 0.8f }, col); break;
                case 5: for (float y : { -0.5f, 0.f, 0.5f }) quad({ -0.8f, y - 0.1f }, { 0.8f, y + 0.1f }, col); break;
                case 6: disk({ 0.f, 0.f }, 0.75f, col); disk({ -0.2f, 0.2f }, 0.25f, bg); break;
                default: quad({ -1.f, -1.f }, { 1.f, 1.f }, col);
            }
        }

        if (sel) {
            const glm::vec4 oc{1.f, 0.85f, 0.15f, 1.f};
            lines.push_back({p0,oc}); lines.push_back({p1,oc});
            lines.push_back({p1,oc}); lines.push_back({p2,oc});
            lines.push_back({p2,oc}); lines.push_back({p3,oc});
            lines.push_back({p3,oc}); lines.push_back({p0,oc});
        }

        for (const auto& comp : a->getComponents()) {
            const auto* lc = dynamic_cast<const LightComponent*>(comp.get());
            if (!lc || lc->lightType == LightType::Point) continue;
            const glm::vec3 e     = lc->emitDirection();
            const glm::vec3 start = pos + e * ssz;
            const glm::vec3 tip   = pos + e * (ssz * 8.f);
            const glm::vec3 back  = tip - e * (ssz * 2.f);
            glm::vec3 side = glm::cross(e, camPos - pos);
            side = (glm::dot(side, side) > 1e-12f) ? glm::normalize(side) : right;
            lines.push_back({start, col});            lines.push_back({tip, col});
            lines.push_back({tip, col});              lines.push_back({back + side * ssz, col});
            lines.push_back({tip, col});              lines.push_back({back - side * ssz, col});
        }
    }
}
