#include "WorldUiDriver.hpp"

#include "IEngineFrontend.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/UIPanelComponent.hpp"
#include "../components/UIPanelPointer.hpp"
#include "../input/HInputSystem.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../renderer/Renderer.hpp"
#include "../scene/World.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

WorldUiDriver::WorldUiDriver()  = default;
WorldUiDriver::~WorldUiDriver() = default;

void WorldUiDriver::updateFollowPanels(World& world, bool xrSessionLive, float dt) {
    CameraComponent* cam = world.getActiveCamera();
    if (!cam || !cam->owner) return;
    glm::vec3 headPos = cam->owner->worldPosition();
    glm::quat headRot = cam->owner->worldRotation();
    const HXrInputState& xr = HInputSystem::get().xr();
    if (xrSessionLive && xr.focused && xr.head.valid) {
        const glm::mat4 m = cam->xrOrigin() * glm::translate(glm::mat4(1.f), xr.head.position) * glm::mat4_cast(xr.head.orientation);
        headPos = glm::vec3(m[3]);
        headRot = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                                           glm::normalize(glm::vec3(m[2]))));
    }
    for (UIPanelComponent* pc : world.componentsOf<UIPanelComponent>()) {
        if (!pc->owner->activeInHierarchy()) continue;
        if (pc->isEnabled() && pc->placement == UIPanelComponent::Placement::FollowHead)
            pc->stepFollow(headPos, headRot, dt);
    }
}

void WorldUiDriver::updatePointers(IEngineFrontend& front, World& world, PhysicsSystem* physics, Renderer& renderer,
                                   const PointerInput& in) {
    auto stop = [](std::unique_ptr<UIPanelPointer>& p, bool& live) {
        if (live && p) p->reset();
        live = false;
    };
    if (!in.mouseActive) stop(mouse_, mouseLive_);
    if (!in.xrActive)
        for (int h = 0; h < 2; ++h) stop(hands_[h], handLive_[h]);
    if (!in.mouseActive && !in.xrActive) {
        front.setPointerOverWorldUi(false);
        if (linesShown_) { renderer.setPointerLines({}); linesShown_ = false; }
        return;
    }

    // 보이는 패널(렌더러가 그리는 것과 같은 조건: 켜진 액터의 켜진 첫 패널)
    std::vector<UIPanelComponent*> panels;
    for (UIPanelComponent* pc : world.componentsOf<UIPanelComponent>())
        if (pc->owner->activeInHierarchy() && pc->isEnabled()) panels.push_back(pc);

    // 가림은 물리 레이캐스트로 봅니다. 플레이어(활성 카메라의 맨 위 조상) 아래의 액터는 모두 뺍니다. 광선이 몸 · 리그 · 손의
    // 충돌체 안에서 출발하면 거리 0 으로 맞기 때문입니다(Jolt 기본 동작).
    CameraComponent*    cam = world.getActiveCamera();
    std::vector<Actor*> ignore;
    if (cam && cam->owner) {
        const Actor* root = cam->owner;
        while (root->getParent()) root = root->getParent();
        for (const auto& ap : world.getActors()) {
            const Actor* top = ap.get();
            while (top->getParent()) top = top->getParent();
            if (top == root) ignore.push_back(ap.get());
        }
    }
    UIPanelPointer::Occluder occluder;
    if (physics)
        occluder = [physics, &ignore](const glm::vec3& o, const glm::vec3& d, float maxD, float& outD, Actor*& outA) {
            PhysicsSystem::RaycastHit h;
            if (!physics->raycast(o, d, maxD, h, ignore)) return false;
            outD = h.distance;
            outA = h.actor;
            return true;
        };

    // ── 마우스 ──
    bool mouseOver = false;
    if (in.mouseActive) {
        if (!mouse_) mouse_ = std::make_unique<UIPanelPointer>();
        UIPanelPointer::Ray ray;
        ray.valid = !panels.empty() && front.pointerRay(in.mx, in.my, (float)in.winW, (float)in.winH, ray.origin, ray.dir);
        mouse_->update(panels, ray, { in.lmb, 0.f }, occluder);
        mouseLive_ = true;
        mouseOver = mouse_->overPanel() || mouse_->capturing();
    }
    front.setPointerOverWorldUi(mouseOver);

    // ── XR 두 손: 조준 자세 광선, 트리거 = 클릭, 스틱 위아래 = 휠, 레이저 · 커서 ──
    std::vector<UIPanelPointer::Segment> segs;
    if (in.xrActive) {
        const HXrInputState& xr = HInputSystem::get().xr();
        const glm::mat4 origin = cam ? cam->xrOrigin() : glm::mat4(1.f);
        for (int h = 0; h < 2; ++h) {
            if (!hands_[h]) hands_[h] = std::make_unique<UIPanelPointer>();
            const HHandInput& hand = xr.hands[h];
            UIPanelPointer::Ray ray;
            if (xr.focused && hand.active && hand.aim.valid && cam && !panels.empty())
                ray = UIPanelPointer::rayFromAim(origin, hand.aim.position, hand.aim.orientation);
            const bool wasPressed = hands_[h]->capturing();
            const UIPanelPointer::Buttons b{ hands_[h]->trigger.update(xr.focused && hand.active ? hand.trigger : 0.f),
                                             UIPanelPointer::stickWheel(hand.thumbstick.y, in.dt) };
            hands_[h]->update(panels, ray, b, occluder);
            handLive_[h] = true;
            // 패널을 누른 순간 짧게 진동합니다(누른 것을 손으로 느끼게).
            if (!wasPressed && hands_[h]->capturing()) HInputSystem::get().haptic(h, 0.35f, 0.02f);
            if (ray.valid) hands_[h]->appendLaser(ray.origin, segs);
        }
    }
    if (!segs.empty() || linesShown_) {
        std::vector<LineVertex> lines;
        lines.reserve(segs.size() * 2);
        for (const auto& sg : segs) { lines.push_back({ sg.a, sg.color }); lines.push_back({ sg.b, sg.color }); }
        linesShown_ = !lines.empty();
        renderer.setPointerLines(std::move(lines));
    }
}
