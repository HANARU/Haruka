#include "XrComponents.hpp"
#include "RigidBodyComponent.hpp"
#include "../core/Actor.hpp"
#include "../core/ComponentRegistry.hpp"
#include "../core/DebugDraw.hpp"
#include "../core/Reflect.hpp"
#include "../core/World.hpp"
#include "../input/HInputSystem.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

// ── 리그 ─────────────────────────────────────────────────────────────────────────
std::vector<PropDesc> XrRigComponent::buildProps() {
    auto* p = this;
    return {
        makePropEnum("Height", { "Tracked (real)", "Fixed" },
                     [p] { return p->trackedHeight ? 0 : 1; }, [p](int v) { p->trackedHeight = (v == 0); }),
        makePropFloat("Eye Height", PropColor::Neutral, 0.01f, [p] { return p->eyeHeight; },
                      [p](float v) { p->eyeHeight = std::clamp(v, xr_rig::kMinEyeHeight, xr_rig::kMaxEyeHeight); },
                      PropUnit::Offset),
    };
}

void XrRigComponent::writeFields(ComponentFieldWriter& w) const {
    w.s("height", trackedHeight ? "tracked" : "fixed");
    w.f("eyeHeight", eyeHeight);
}

bool XrRigComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if (key == "height") {
        if      (value == "tracked") trackedHeight = true;
        else if (value == "fixed")   trackedHeight = false;
        else ok = false;
    } else if (key == "eyeHeight") {
        float v = eyeHeight;
        ok = fieldparse::toFloat(value, v) && v >= xr_rig::kMinEyeHeight && v <= xr_rig::kMaxEyeHeight;
        if (ok) eyeHeight = v;
    } else {
        return false;
    }
    if (!ok) reportBadField(key, value);
    return true;
}

bool hxr::floorBelow(const World& world, const glm::vec3& from, const Actor* skip, float& outY) {
    float bestT = FLT_MAX;
    for (const auto& ap : world.getActors()) {
        const Actor* a = ap.get();
        if (!a->activeInHierarchy()) continue;
        bool skipped = false;
        for (const Actor* q = a; q; q = q->getParent())
            if (q == skip) { skipped = true; break; }
        if (skipped) continue;
        glm::vec3 lo, hi;
        bool has = false;
        for (const auto& c : a->getComponents()) {
            glm::vec3 cmin, cmax;
            if (c->isEnabled() && c->localBounds(cmin, cmax)) {
                lo  = has ? glm::min(lo, cmin) : cmin;
                hi  = has ? glm::max(hi, cmax) : cmax;
                has = true;
            }
        }
        if (!has) continue;
        // 아래 방향 광선을 액터 로컬로 옮겨 OBB(로컬 AABB) 슬랩 판정을 합니다. 방향은 정규화하지 않으므로 t 는 월드 거리입니다.
        const glm::mat4 wm  = a->worldMatrix();
        const glm::mat4 inv = glm::inverse(wm);
        const glm::vec3 ro  = glm::vec3(inv * glm::vec4(from, 1.f));
        const glm::vec3 rd  = glm::vec3(inv * glm::vec4(0.f, -1.f, 0.f, 0.f));
        float tmin = -FLT_MAX, tmax = FLT_MAX;
        bool  hit  = true;
        for (int k = 0; k < 3; ++k) {
            if (std::fabs(rd[k]) < 1e-8f) {
                if (ro[k] < lo[k] || ro[k] > hi[k]) { hit = false; break; }
            } else {
                float t1 = (lo[k] - ro[k]) / rd[k];
                float t2 = (hi[k] - ro[k]) / rd[k];
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
            }
        }
        // 들어가는 지점이 시작점 아래에 있는 것만 바닥 후보입니다(시작점을 감싼 바운드는 건너뜀).
        if (!hit || tmin < 0.f || tmax < tmin) continue;
        if (tmin < bestT) bestT = tmin;
    }
    if (bestT == FLT_MAX) return false;
    outY = from.y - bestT;
    return true;
}

// ── 추적 자세 ─────────────────────────────────────────────────────────────────────
static int g_liveHands = 0;
int TrackedPoseComponent::liveHands() { return g_liveHands; }

void TrackedPoseComponent::onEnable() {
    if (handIndex() >= 0 && !countedHand_) { ++g_liveHands; countedHand_ = true; }
}
void TrackedPoseComponent::onDisable() {
    if (countedHand_) { --g_liveHands; countedHand_ = false; }
}
TrackedPoseComponent::~TrackedPoseComponent() { onDisable(); }

hxr::XrOriginChoice hxr::chooseOrigin(const Actor* cam) {
    XrOriginChoice c;
    if (!cam) return c;
    const auto* tp = cam->getComponent<TrackedPoseComponent>();
    if (!tp || tp->source != ETrackedSource::Head) return c;
    const Actor* parent = cam->getParent();
    // 씬 리그는 예전 규칙 그대로입니다(머리 추적을 꺼도 리그가 원점).
    if (parent && parent->getComponent<XrRigComponent>()) return { EXrOrigin::SceneRig, parent };
    // 꺼진 머리 추적은 카메라를 움직이지 않으므로 두 번 적용될 것이 없습니다. 예전처럼 카메라 기준으로 둡니다.
    if (!tp->isLive()) return c;
    return { EXrOrigin::TrackedParent, parent };
}

void TrackedPoseComponent::tick(float /*dt*/) {
    // 손 · 머리를 바꿔 고른 뒤에도 셈이 맞도록 매 틱 맞춥니다.
    if ((handIndex() >= 0) != countedHand_) { if (countedHand_) onDisable(); else onEnable(); }
    const HXrInputState& xr = HInputSystem::get().xr();
    if (!xr.focused || !owner) return;
    const int     hand = handIndex();
    const HPose&  p    = hand < 0 ? xr.head : xr.hands[hand].grip;
    // 바닥 기준 공간이 없으면 자세의 원점이 머리 높이라 눈높이만큼 올려 리그(바닥) 기준으로 맞춥니다.
    const glm::vec3 lift(0.f, xr.floorOffset, 0.f);
    if (p.valid) {
        owner->setLocalPosition(p.position + lift);
        owner->setLocalRotation(p.orientation);
    }
    if (!showDebug || hand < 0) return;
    const HHandInput& h = xr.hands[hand];
    if (!h.active) return;
    if (h.grip.valid) hdebug::axes(owner->worldMatrix(), 0.1f);
    if (h.aim.valid) {
        // 가리키는 자세는 리그 원점 기준입니다(부모 = 리그).
        const Actor*    rig    = owner->getParent();
        const glm::quat rigRot = rig ? rig->worldRotation() : glm::quat(1.f, 0.f, 0.f, 0.f);
        const glm::vec3 rigPos = rig ? rig->worldPosition() : glm::vec3(0.f);
        const glm::vec3 o   = rigPos + rigRot * (h.aim.position + lift);
        const glm::vec3 fwd = rigRot * (h.aim.orientation * glm::vec3(0.f, 0.f, -1.f));   // OpenXR aim 은 -Z 가 앞
        const float     t   = std::clamp(h.trigger, 0.f, 1.f);
        hdebug::line(o, o + fwd * (0.5f + 1.5f * std::clamp(h.squeeze, 0.f, 1.f)), { 1.f, 1.f, 1.f - 0.8f * t, 1.f });
    }
}

std::vector<PropDesc> TrackedPoseComponent::buildProps() {
    auto* p = this;
    return {
        makePropEnum("Source", { "Head", "Left Hand", "Right Hand" },
                     [p] { return (int)p->source; }, [p](int v) { p->source = (ETrackedSource)std::clamp(v, 0, 2); }),
        makePropToggle("Show Debug", [p] { return p->showDebug; }, [p](bool v) { p->showDebug = v; }),
    };
}

void TrackedPoseComponent::writeFields(ComponentFieldWriter& w) const {
    w.s("source", source == ETrackedSource::Head ? "head" : source == ETrackedSource::LeftHand ? "left" : "right");
    w.b("showDebug", showDebug);
}

bool TrackedPoseComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if (key == "source") {
        if      (value == "head")  source = ETrackedSource::Head;
        else if (value == "left")  source = ETrackedSource::LeftHand;
        else if (value == "right") source = ETrackedSource::RightHand;
        else ok = false;
    } else if (key == "showDebug") ok = fieldparse::toBool(value, showDebug);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}

// ── 기본 이동 ─────────────────────────────────────────────────────────────────────
Actor* RigLocomotionComponent::findHead(const Actor& rig) {
    for (Actor* c : rig.getChildren())
        if (auto* tp = c->getComponent<TrackedPoseComponent>(); tp && tp->source == ETrackedSource::Head) return c;
    return nullptr;
}

void RigLocomotionComponent::rotateRigAbout(Actor& rig, const glm::vec3& pivot, const glm::vec3& axis, float angleRad) {
    const glm::quat q = glm::angleAxis(angleRad, glm::normalize(axis));
    const glm::vec3 p = rig.worldPosition();
    rig.setWorldPosition(pivot + q * (p - pivot));
    rig.setWorldRotation(glm::normalize(q * rig.worldRotation()));
}

void RigLocomotionComponent::tick(float dt) {
    if (!owner) return;
    HInputSystem&        in   = HInputSystem::get();
    const HXrInputState& xr   = in.xr();
    Actor&               rig  = *owner;
    Actor*               head = findHead(rig);
    const glm::quat      rigRot  = rig.worldRotation();
    const glm::vec3      up      = rigRot * glm::vec3(0.f, 1.f, 0.f);
    const glm::vec3      headPos = head ? head->worldPosition() : rig.worldPosition();
    const glm::quat      headRot = head ? head->worldRotation() : rigRot;

    // 이동: 머리가 보는 방향 기준. 걷기면 리그의 수평면으로 내립니다.
    glm::vec3 fwd   = headRot * glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 right = headRot * glm::vec3(1.f, 0.f, 0.f);
    if (!flying) {
        fwd   -= up * glm::dot(fwd, up);
        right -= up * glm::dot(right, up);
    }
    auto norm = [](const glm::vec3& v) { const float l = glm::length(v); return l > 1e-5f ? v / l : glm::vec3(0.f); };
    fwd   = norm(fwd);
    right = norm(right);
    glm::vec3 move = right * in.axis("MoveX") + fwd * in.axis("MoveY");
    if (glm::length(move) > 1.f) move = glm::normalize(move);
    if (flying) move += up * std::clamp(in.axis("MoveUp"), -1.f, 1.f);
    if (glm::dot(move, move) > 0.f) rig.setWorldPosition(rig.worldPosition() + move * moveSpeed * dt);

    if (xr.focused) {
        // 스냅 회전: 스틱을 70% 넘게 기울이면 한 번, 30% 아래로 돌아오면 다시 받을 준비. 머리 둘레로 돌려 제자리에서 돕니다.
        const float t = in.axis("TurnX");
        if (snapArmed_ && std::fabs(t) > 0.7f) {
            rotateRigAbout(rig, headPos + move * moveSpeed * dt, up, glm::radians(t > 0.f ? -snapTurnDeg : snapTurnDeg));
            snapArmed_ = false;
        } else if (std::fabs(t) < 0.3f) {
            snapArmed_ = true;
        }
    } else {
        // 데스크톱: 마우스(오른쪽 버튼 끌기)로 둘러보기. 좌우는 리그를 머리 둘레로, 위아래는 머리만 숙입니다.
        const float lx = in.axis("LookX"), ly = in.axis("LookY");
        if (lx != 0.f) rotateRigAbout(rig, headPos, up, glm::radians(-lx * lookSensitivity));
        if (ly != 0.f && head) {
            pitchDeg_ = std::clamp(pitchDeg_ - ly * lookSensitivity, -85.f, 85.f);
            head->setLocalRotation(glm::angleAxis(glm::radians(pitchDeg_), glm::vec3(1.f, 0.f, 0.f)));
        }
    }
}

std::vector<PropDesc> RigLocomotionComponent::buildProps() {
    auto* p = this;
    return {
        makePropFloat("Move Speed", PropColor::Neutral, 0.05f, [p] { return p->moveSpeed; },
                      [p](float v) { p->moveSpeed = std::clamp(v, 0.f, 50.f); }),
        makePropFloat("Snap Turn", PropColor::Neutral, 1.f, [p] { return p->snapTurnDeg; },
                      [p](float v) { p->snapTurnDeg = std::clamp(v, 5.f, 180.f); }, PropUnit::Angle),
        makePropToggle("Flying", [p] { return p->flying; }, [p](bool v) { p->flying = v; }),
        makePropFloat("Look Sensitivity", PropColor::Neutral, 0.01f, [p] { return p->lookSensitivity; },
                      [p](float v) { p->lookSensitivity = std::clamp(v, 0.01f, 2.f); }),
    };
}

void RigLocomotionComponent::writeFields(ComponentFieldWriter& w) const {
    w.f("moveSpeed", moveSpeed);
    w.f("snapTurn", snapTurnDeg);
    w.b("flying", flying);
    w.f("lookSensitivity", lookSensitivity);
}

bool RigLocomotionComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if      (key == "moveSpeed")       ok = fieldparse::toFloat(value, moveSpeed);
    else if (key == "snapTurn")        ok = fieldparse::toFloat(value, snapTurnDeg);
    else if (key == "flying")          ok = fieldparse::toBool(value, flying);
    else if (key == "lookSensitivity") ok = fieldparse::toFloat(value, lookSensitivity);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}

// ── 잡을 수 있는 것 ───────────────────────────────────────────────────────────────
static std::vector<GrabbableComponent*>& grabbables() {
    static std::vector<GrabbableComponent*> v;
    return v;
}
const std::vector<GrabbableComponent*>& GrabbableComponent::live() { return grabbables(); }

void GrabbableComponent::onEnable() {
    auto& v = grabbables();
    if (std::find(v.begin(), v.end(), this) == v.end()) v.push_back(this);
}
void GrabbableComponent::onDisable() {
    auto& v = grabbables();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
    if (holder_) holder_->drop();
}
GrabbableComponent::~GrabbableComponent() {
    auto& v = grabbables();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
    if (holder_) { holder_->held_ = nullptr; holder_->wasDynamic_ = false; holder_ = nullptr; }
}

std::vector<PropDesc> GrabbableComponent::buildProps() {
    auto* p = this;
    return { makePropToggle("Throwable", [p] { return p->throwable; }, [p](bool v) { p->throwable = v; }) };
}
void GrabbableComponent::writeFields(ComponentFieldWriter& w) const { w.b("throwable", throwable); }
bool GrabbableComponent::readField(const std::string& key, const std::string& value) {
    if (key != "throwable") return false;
    if (!fieldparse::toBool(value, throwable)) reportBadField(key, value);
    return true;
}

// ── 손으로 잡기 ───────────────────────────────────────────────────────────────────
// 점에서 액터 바운드(로컬 상자를 월드로 옮긴 것)까지 거리. 바운드가 없으면 액터 위치까지.
static float distanceToActor(const glm::vec3& p, const Actor& a) {
    glm::vec3 lo(0.f), hi(0.f);
    bool has = false;
    for (const auto& c : a.getComponents()) {
        glm::vec3 cl, ch;
        if (c->localBounds(cl, ch)) {
            if (!has) { lo = cl; hi = ch; has = true; }
            else      { lo = glm::min(lo, cl); hi = glm::max(hi, ch); }
        }
    }
    if (!has) return glm::length(p - a.worldPosition());
    const glm::mat4 wm    = a.worldMatrix();
    const glm::vec3 local = glm::vec3(glm::inverse(wm) * glm::vec4(p, 1.f));
    const glm::vec3 near  = glm::clamp(local, lo, hi);
    return glm::length(glm::vec3(wm * glm::vec4(near, 1.f)) - p);
}

GrabbableComponent* GrabInteractorComponent::pick(const glm::vec3& hand, float radius) {
    GrabbableComponent* best  = nullptr;
    float               bestD = radius;
    for (GrabbableComponent* g : GrabbableComponent::live()) {
        if (!g->owner || !g->owner->activeInHierarchy()) continue;
        const float d = distanceToActor(hand, *g->owner);
        if (d <= bestD) { bestD = d; best = g; }
    }
    return best;
}

void GrabInteractorComponent::grab(GrabbableComponent* g, const glm::vec3& handPos, const glm::quat& handRot) {
    if (!g || !g->owner) return;
    if (held_) release(glm::vec3(0.f), glm::vec3(0.f), false);
    if (g->holder_ && g->holder_ != this) g->holder_->drop();   // 다른 손에서 넘겨받음
    Actor& a = *g->owner;
    const glm::quat inv = glm::inverse(handRot);
    offPos_ = inv * (a.worldPosition() - handPos);
    offRot_ = glm::normalize(inv * a.worldRotation());
    wasDynamic_ = false;
    if (auto* rb = a.getComponent<RigidBodyComponent>(); rb && rb->motionType == EMotionType::Dynamic)
        wasDynamic_ = rb->setMotionRuntime(EMotionType::Kinematic);
    held_      = g;
    g->holder_ = this;
}

void GrabInteractorComponent::follow(const glm::vec3& handPos, const glm::quat& handRot) {
    if (!held_ || !held_->owner) return;
    held_->owner->setWorldPosition(handPos + handRot * offPos_);
    held_->owner->setWorldRotation(glm::normalize(handRot * offRot_));
}

void GrabInteractorComponent::release(const glm::vec3& linearVelocity, const glm::vec3& angularVelocity, bool throwIt) {
    if (!held_) return;
    GrabbableComponent* g = held_;
    held_      = nullptr;
    g->holder_ = nullptr;
    if (!g->owner) return;
    auto* rb = g->owner->getComponent<RigidBodyComponent>();
    if (rb && wasDynamic_) {
        rb->setMotionRuntime(EMotionType::Dynamic);
        // 잡은 동안 Kinematic 바디는 손을 따라가는 속도(MoveKinematic)를 갖고 있고, Dynamic 으로 돌려도 그 속도가 남습니다.
        // 던지지 않고 놓을 때(throwable=false, 다른 물체로 바꿔 잡기, 손 · 잡기 끄기)는 속도를 지워 그 자리에서 떨어지게
        // 합니다. 예전에는 남은 손 속도로 날아가서 throwable=false 가 사실상 동작하지 않았습니다(DEF-143).
        const bool toss = throwIt && g->throwable;
        rb->setLinearVelocity(toss ? linearVelocity : glm::vec3(0.f));
        rb->setAngularVelocity(toss ? angularVelocity : glm::vec3(0.f));
    }
    wasDynamic_ = false;
}

void GrabInteractorComponent::drop() { release(glm::vec3(0.f), glm::vec3(0.f), false); }
GrabInteractorComponent::~GrabInteractorComponent() { if (held_) held_->holder_ = nullptr; }

void GrabInteractorComponent::onDisable() {
    drop();
    prevHeld_ = false;
}

void GrabInteractorComponent::tick(float /*dt*/) {
    if (!owner) return;
    const auto* tp   = owner->getComponent<TrackedPoseComponent>();
    const int   hand = tp ? tp->handIndex() : -1;
    if (hand < 0) return;
    HInputSystem&        in = HInputSystem::get();
    const HXrInputState& xr = in.xr();
    const bool held = in.held(hand == 0 ? "GrabLeft" : "GrabRight");
    const glm::vec3 hp = owner->worldPosition();
    const glm::quat hr = owner->worldRotation();
    if (held && !prevHeld_) {
        if (GrabbableComponent* g = pick(hp, radius)) {
            grab(g, hp, hr);
            in.haptic(hand, 0.6f, 0.04f);
        }
    } else if (!held && prevHeld_ && held_) {
        // 손 속도는 리그(부모) 기준이라 월드로 돌립니다.
        const Actor*    rig = owner->getParent();
        const glm::quat r   = rig ? rig->worldRotation() : glm::quat(1.f, 0.f, 0.f, 0.f);
        release(r * xr.hands[hand].linearVelocity, r * xr.hands[hand].angularVelocity, true);
        in.haptic(hand, 0.3f, 0.02f);
    }
    prevHeld_ = held;
    follow(hp, hr);
}

std::vector<PropDesc> GrabInteractorComponent::buildProps() {
    auto* p = this;
    return { makePropFloat("Radius", PropColor::Neutral, 0.01f, [p] { return p->radius; },
                           [p](float v) { p->radius = std::clamp(v, 0.01f, 1.f); }, PropUnit::Offset) };
}
void GrabInteractorComponent::writeFields(ComponentFieldWriter& w) const { w.f("radius", radius); }
bool GrabInteractorComponent::readField(const std::string& key, const std::string& value) {
    if (key != "radius") return false;
    if (!fieldparse::toFloat(value, radius)) reportBadField(key, value);
    return true;
}

// ── 등록 ─────────────────────────────────────────────────────────────────────────
void registerXrComponents() {
    auto& r = ComponentRegistry::get();
    if (!r.find("XrRigComponent"))          r.registerType<XrRigComponent>         ("XrRigComponent",          "XR Rig");
    if (!r.find("TrackedPoseComponent"))    r.registerType<TrackedPoseComponent>   ("TrackedPoseComponent",    "Tracked Pose");
    if (!r.find("RigLocomotionComponent"))  r.registerType<RigLocomotionComponent> ("RigLocomotionComponent",  "Locomotion");
    if (!r.find("GrabbableComponent"))      r.registerType<GrabbableComponent>     ("GrabbableComponent",      "Grabbable");
    if (!r.find("GrabInteractorComponent")) r.registerType<GrabInteractorComponent>("GrabInteractorComponent", "Grab Hand");
}
