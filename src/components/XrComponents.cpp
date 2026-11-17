#include "XrComponents.hpp"
#include "RigidBodyComponent.hpp"
#include "HandPoseComponent.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../xr/GraspSolver.hpp"
#include "../math/Damping.hpp"
#include "../core/Actor.hpp"
#include "../core/ComponentRegistry.hpp"
#include "../core/DebugDraw.hpp"
#include "../core/Reflect.hpp"
#include "../core/World.hpp"
#include "../input/HInputSystem.hpp"
#include "../math/Geometry.hpp"

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
        glm::vec3 lo(0.f), hi(0.f);
        bool has = false;
        for (const auto& c : a->getComponents()) {
            glm::vec3 cmin, cmax;
            if (c->isEnabled() && c->blocksFloor() && c->localBounds(cmin, cmax)) {
                lo  = has ? glm::min(lo, cmin) : cmin;
                hi  = has ? glm::max(hi, cmax) : cmax;
                has = true;
            }
        }
        if (!has) continue;
        // 아래 방향 광선을 액터 로컬로 옮겨 OBB(로컬 AABB) 슬랩 판정을 합니다. 방향은 정규화하지 않으므로 t 는 월드 거리입니다.
        const hmath::Ray local = hmath::transformRay({ from, glm::vec3(0.f, -1.f, 0.f) }, hmath::affineInverse(a->worldMatrix()));
        float tmin = 0.f, tmax = 0.f;
        // 들어가는 지점이 시작점 아래에 있는 것만 바닥 후보입니다(시작점을 감싼 바운드는 건너뜀).
        if (!hmath::intersectRayAabb(local, { lo, hi }, tmin, tmax) || tmin < 0.f) continue;
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
namespace hxr {
TeleportArc teleportArc(const glm::vec3& origin, const glm::vec3& dir, float speed, float gravity, float maxTime,
                        float step, const ArcRay& ray) {
    TeleportArc arc;
    const float     len = glm::length(dir);
    const glm::vec3 v0  = len > 1e-6f ? dir / len * speed : glm::vec3(0.f);
    const glm::vec3 g{ 0.f, -gravity, 0.f };
    step = std::max(step, 1e-3f);
    arc.points.push_back(origin);
    glm::vec3 prev = origin;
    const int n = int(maxTime / step + 0.5f);
    for (int i = 1; i <= n; ++i) {
        const float     t   = step * float(i);
        const glm::vec3 cur = origin + v0 * t + 0.5f * g * t * t;
        glm::vec3 p, nrm;
        if (ray && ray(prev, cur, p, nrm)) {
            arc.points.push_back(p);
            arc.hit    = true;
            arc.point  = p;
            arc.normal = nrm;
            return arc;
        }
        arc.points.push_back(cur);
        prev = cur;
    }
    return arc;
}

bool teleportValid(const TeleportArc& arc, float maxSlopeDeg) {
    if (!arc.hit) return false;
    const float l = glm::length(arc.normal);
    return l > 1e-6f && arc.normal.y / l >= std::cos(glm::radians(maxSlopeDeg));
}

glm::vec3 teleportRigPosition(const glm::vec3& rigPos, const glm::vec3& headPos, const glm::vec3& target) {
    return { rigPos.x + (target.x - headPos.x), target.y, rigPos.z + (target.z - headPos.z) };
}
} // namespace hxr

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
    velocity_ = move * moveSpeed;
    if (collide && PhysicsSystem::current()) moveCollide(rig, headPos, move, dt);
    else {
        releaseCharacter();
        if (glm::dot(move, move) > 0.f) rig.setWorldPosition(rig.worldPosition() + move * moveSpeed * dt);
    }
    if (teleport && xr.focused) tickTeleport(rig, head ? head->worldPosition() : rig.worldPosition());
    else                        aiming_ = false;

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

// 리그 자신(자식 포함)과 손에 든 물체는 캐릭터 · 텔레포트 판정에서 뺍니다.
static void rigIgnoreList(Actor& rig, std::vector<Actor*>& out, std::vector<Actor*>& stack) {
    out.clear();
    stack.clear();
    stack.push_back(&rig);
    while (!stack.empty()) {
        Actor* a = stack.back();
        stack.pop_back();
        out.push_back(a);
        for (Actor* c : a->getChildren()) stack.push_back(c);
    }
    for (GrabbableComponent* g : GrabbableComponent::live())
        if (g->holder() && g->owner) out.push_back(g->owner);
}

void RigLocomotionComponent::moveCollide(Actor& rig, const glm::vec3& headPos, const glm::vec3& move, float dt) {
    PhysicsSystem* ps = PhysicsSystem::current();
    // 캡슐은 머리 아래, 리그 바닥 높이에 섭니다(방 안에서 걸어간 자리를 따라감).
    const glm::vec3 feet{ headPos.x, rig.worldPosition().y, headPos.z };
    if (!character_) {
        PhysicsSystem::CharacterDesc d;
        d.radius = kBodyRadius;
        d.height = kBodyHeight;
        character_ = ps->createCharacter(d, feet);
        fallSpeed_ = 0.f;
        if (!character_) return;
    }
    glm::vec3 v = move * moveSpeed;
    if (!flying) v.y = fallSpeed_;
    std::vector<Actor*>& ignore = ignoreScratch_;
    rigIgnoreList(rig, ignore, stackScratch_);
    PhysicsSystem::CharacterMove m;
    if (!ps->moveCharacter(character_, feet, v, dt, m, ignore)) { character_ = 0; return; }
    rig.setWorldPosition(rig.worldPosition() + (m.position - feet));
    if (dt > 0.f) velocity_ = (m.position - feet) / dt;
    if (flying || m.onGround) fallSpeed_ = 0.f;
    else fallSpeed_ = std::max(fallSpeed_ + ps->gravity().y * dt, -kMaxFallSpeed);
}

void RigLocomotionComponent::releaseCharacter() {
    if (character_) {
        if (PhysicsSystem* ps = PhysicsSystem::current()) ps->destroyCharacter(character_);
        character_ = 0;
    }
    fallSpeed_ = 0.f;
}

void RigLocomotionComponent::onDisable() { releaseCharacter(); aiming_ = false; }
RigLocomotionComponent::~RigLocomotionComponent() { releaseCharacter(); }

void RigLocomotionComponent::tickTeleport(Actor& rig, const glm::vec3& headPos) {
    HInputSystem& in = HInputSystem::get();
    const float   t  = in.axis("Teleport");
    Actor* hand = nullptr;
    for (Actor* c : rig.getChildren())
        if (auto* tp = c->getComponent<TrackedPoseComponent>(); tp && tp->source == ETrackedSource::RightHand) hand = c;
    PhysicsSystem* ps = PhysicsSystem::current();
    if (!hand || !ps) { aiming_ = false; return; }

    if (!aiming_ && t > 0.7f) aiming_ = true;
    if (!aiming_) return;

    std::vector<Actor*>& ignore = ignoreScratch_;
    rigIgnoreList(rig, ignore, stackScratch_);
    const auto ray = [&](const glm::vec3& a, const glm::vec3& b, glm::vec3& point, glm::vec3& normal) {
        PhysicsSystem::RaycastHit h;
        const float len = glm::length(b - a);
        if (len < 1e-6f || !ps->raycast(a, b - a, len, h, ignore)) return false;
        point  = h.point;
        normal = h.normal;
        return true;
    };
    const glm::vec3 o   = hand->worldPosition();
    const glm::vec3 dir = hand->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
    const hxr::TeleportArc arc = hxr::teleportArc(o, dir, kTeleportSpeed, -ps->gravity().y, kTeleportMaxTime,
                                                  kTeleportStep, ray);
    const bool ok = hxr::teleportValid(arc, kTeleportMaxSlope);

    if (t < 0.3f) {                                   // 스틱을 놓음 → 옮기기
        aiming_ = false;
        if (ok) {
            rig.setWorldPosition(hxr::teleportRigPosition(rig.worldPosition(), headPos, arc.point));
            fallSpeed_ = 0.f;
            if (const auto* tp = hand->getComponent<TrackedPoseComponent>()) in.haptic(tp->handIndex(), 0.3f, 0.03f);
        }
        return;
    }
    const glm::vec4 col = ok ? glm::vec4(0.3f, 0.9f, 1.f, 1.f) : glm::vec4(1.f, 0.3f, 0.3f, 1.f);
    for (size_t i = 1; i < arc.points.size(); ++i) hdebug::line(arc.points[i - 1], arc.points[i], col);
    if (arc.hit) hdebug::circle(arc.point + arc.normal * 0.01f, arc.normal, 0.3f, col);
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
        makePropToggle("Collide", [p] { return p->collide; }, [p](bool v) { p->collide = v; }),
        makePropToggle("Teleport", [p] { return p->teleport; }, [p](bool v) { p->teleport = v; }),
    };
}

void RigLocomotionComponent::writeFields(ComponentFieldWriter& w) const {
    w.f("moveSpeed", moveSpeed);
    w.f("snapTurn", snapTurnDeg);
    w.b("flying", flying);
    w.f("lookSensitivity", lookSensitivity);
    w.b("collide", collide);
    w.b("teleport", teleport);
}

bool RigLocomotionComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if      (key == "moveSpeed")       ok = fieldparse::toFloat(value, moveSpeed);
    else if (key == "snapTurn")        ok = fieldparse::toFloat(value, snapTurnDeg);
    else if (key == "flying")          ok = fieldparse::toBool(value, flying);
    else if (key == "lookSensitivity") ok = fieldparse::toFloat(value, lookSensitivity);
    else if (key == "collide")         ok = fieldparse::toBool(value, collide);
    else if (key == "teleport")        ok = fieldparse::toBool(value, teleport);
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
    if (holder_) { holder_->held_ = nullptr; holder_->wasDynamic_ = false; holder_->tracking_ = false; holder_ = nullptr; }
}

std::vector<PropDesc> GrabbableComponent::buildProps() {
    auto* p = this;
    return { makePropToggle("Throwable", [p] { return p->throwable; }, [p](bool v) { p->throwable = v; }),
             makePropToggle("Velocity Tracking", [p] { return p->velocityTracking; }, [p](bool v) { p->velocityTracking = v; }) };
}
void GrabbableComponent::writeFields(ComponentFieldWriter& w) const {
    w.b("throwable", throwable);
    if (velocityTracking) w.b("velocityTracking", true);   // 기본값이면 쓰지 않음
}
bool GrabbableComponent::readField(const std::string& key, const std::string& value) {
    bool* target = key == "throwable" ? &throwable : key == "velocityTracking" ? &velocityTracking : nullptr;
    if (!target) return false;
    if (!fieldparse::toBool(value, *target)) reportBadField(key, value);
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
    const glm::vec3 local = glm::vec3(hmath::affineInverse(wm) * glm::vec4(p, 1.f));
    const glm::vec3 near  = glm::clamp(local, lo, hi);
    return glm::length(glm::vec3(wm * glm::vec4(near, 1.f)) - p);
}

hxr::ThrowVelocity hxr::throwVelocity(const glm::vec3& handLinear, const glm::vec3& handAngular, const glm::vec3& handPos,
                                      const glm::vec3& centerOfMass, const glm::vec3& rigLinear) {
    ThrowVelocity t;
    t.angular = handAngular;
    t.linear  = handLinear + glm::cross(handAngular, centerOfMass - handPos) + rigLinear;
    return t;
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
    tracking_   = false;
    comLocal_   = glm::vec3(0.f);
    velHistory_.clear();
    if (auto* rb = a.getComponent<RigidBodyComponent>()) {
        // 질량 중심을 액터 기준으로 기억합니다. 잡는 동안 Kinematic 바디는 액터를 한 스텝 늦게 따라가므로 놓을 때 바디에서
        // 다시 읽지 않고 액터의 그때 자세로 옮깁니다.
        comLocal_ = glm::inverse(a.worldRotation()) * (rb->centerOfMass() - a.worldPosition());
        if (rb->motionType == EMotionType::Dynamic) {
            if (g->velocityTracking && rb->hasBody()) {
                // Dynamic 그대로 두고 중력만 끕니다(손 아래로 처지지 않게). 막힌 동안 목표와 벌어진 거리만큼 속도가 커져
                // 얇은 벽을 한 스텝에 지나갈 수 있으므로 잡는 동안 연속 충돌을 켭니다(P1).
                tracking_ = true;
                rb->setGravityFactorRuntime(0.f);
                rb->setContinuousRuntime(true);
            } else {
                wasDynamic_ = rb->setMotionRuntime(EMotionType::Kinematic);
            }
        }
    }
    held_      = g;
    g->holder_ = this;
    fitted_    = false;
    fitT_      = 1.f;
    if (autoFit) fitGrasp(handPos, handRot);
}

void GrabInteractorComponent::fitGrasp(const glm::vec3& handPos, const glm::quat& handRot) {
    if (!owner || !held_ || !held_->owner) return;
    HandPoseComponent* hand = owner->getComponentInChildren<HandPoseComponent>();
    auto* rb = held_->owner->getComponent<RigidBodyComponent>();
    if (!hand || !rb || !rb->hasBody()) return;
    const PhysicsSystem* ps = rb->physSys_;
    glm::mat4 palmM;
    if (!hand->jointWorld(hhand::EJoint::Palm, palmM)) return;
    // 1. 손바닥 맞추기: 손바닥(관절 -Y 쪽)에서 표면을 찾고, 없으면 물체 중심 쪽으로 찾습니다. 표면이 손바닥 두께만큼 떨어지게
    //    물체를 손바닥 노멀 방향으로 옮깁니다(이동만, 회전은 잡은 순간의 상대 회전 그대로).
    const glm::vec3 palm   = glm::vec3(palmM[3]);
    const glm::vec3 normal = -glm::normalize(glm::vec3(palmM[1]));
    PhysicsSystem::RaycastHit hit;
    glm::vec3 dir = normal;
    bool found = ps->raycastBody(rb->bodyId_, palm, dir, kPalmReach, hit);
    if (!found) {
        dir   = rb->centerOfMass() - palm;
        found = glm::length(dir) > 1e-4f && ps->raycastBody(rb->bodyId_, palm, dir, kPalmReach, hit);
    }
    glm::vec3 shift(0.f);
    if (found) {
        const glm::vec3 d = glm::normalize(dir);
        shift = -d * std::clamp(hit.distance - kPalmThickness, -kMaxFitShift, kMaxFitShift);
    }
    // 2. 손가락 감싸기: 물체를 shift 만큼 옮긴 자리로 판정합니다(바디는 아직 옛 자리라 판정 쪽 캡슐을 반대로 옮김).
    const hgrasp::Result r = hgrasp::solve(
        [hand](const hgrasp::Flex& flex, hgrasp::JointPos& out) { hand->jointWorldPositions(flex, out); },
        [ps, rb, shift](const glm::vec3& a, const glm::vec3& b, float radius) {
            return ps->capsulePenetration(rb->bodyId_, a - shift, b - shift, radius);
        });
    if (!r.fitted) return;   // 맞춤 실패: 예전 잡기 그대로
    hand->setGrasp(r.flex, kFitBlend);
    fitted_  = true;
    fitFrom_ = offPos_;
    fitTo_   = offPos_ + glm::inverse(handRot) * shift;
    fitT_    = 0.f;
    (void)handPos;
}

void GrabInteractorComponent::trackingVelocity(const glm::vec3& comNow, const glm::quat& rotNow, const glm::vec3& comGoal,
                                               const glm::quat& rotGoal, float dt, glm::vec3& linear, glm::vec3& angular) {
    linear = angular = glm::vec3(0.f);
    if (!(dt > 0.f)) return;
    linear = (comGoal - comNow) / dt;
    const float sp = glm::length(linear);
    if (sp > kMaxTrackSpeed) linear *= kMaxTrackSpeed / sp;
    // 회전 차이 goal · now⁻¹ 를 축-각으로(최단 경로). 각속도 = 축 × 각 / dt.
    glm::quat d = glm::normalize(rotGoal * glm::inverse(rotNow));
    if (d.w < 0.f) d = -d;
    const glm::vec3 v(d.x, d.y, d.z);
    const float s = glm::length(v);
    if (s > 1e-7f) {
        const float angle = 2.f * std::atan2(s, d.w);
        angular = v / s * (angle / dt);
        const float w = glm::length(angular);
        if (w > kMaxTrackAngular) angular *= kMaxTrackAngular / w;
    }
}

void GrabInteractorComponent::follow(const glm::vec3& handPos, const glm::quat& handRot, float dt) {
    if (!held_ || !held_->owner) return;
    if (fitT_ < 1.f) {   // 자동 맞춤 전환: 물체 상대 위치를 ease-in-out 으로 옮김
        fitT_   = std::min(1.f, fitT_ + std::max(dt, 0.f) / kFitBlend);
        offPos_ = glm::mix(fitFrom_, fitTo_, hmath::ease::inOutCubic(fitT_));
    }
    const glm::vec3 goalPos = handPos + handRot * offPos_;
    const glm::quat goalRot = glm::normalize(handRot * offRot_);
    if (tracking_) {
        // 속도 추적(P5): 다음 틱까지 목표 자세에 닿는 속도를 넣습니다. 질량 중심 속도이므로 질량 중심끼리 비교합니다.
        auto* rb = held_->owner->getComponent<RigidBodyComponent>();
        if (!rb || !rb->hasBody() || !(dt > 0.f)) return;
        glm::vec3 lin, ang;
        trackingVelocity(rb->centerOfMass(), held_->owner->worldRotation(), goalPos + goalRot * comLocal_, goalRot, dt, lin, ang);
        rb->setLinearVelocity(lin);
        rb->setAngularVelocity(ang);
        return;
    }
    held_->owner->setWorldPosition(goalPos);
    held_->owner->setWorldRotation(goalRot);
}

void GrabInteractorComponent::release(const glm::vec3& linearVelocity, const glm::vec3& angularVelocity, bool throwIt) {
    if (!held_) return;
    if (fitted_ && owner)
        if (auto* hand = owner->getComponentInChildren<HandPoseComponent>()) hand->clearGrasp(kFitBlend);
    fitted_ = false;
    fitT_   = 1.f;
    GrabbableComponent* g = held_;
    held_      = nullptr;
    g->holder_ = nullptr;
    if (!g->owner) return;
    auto* rb = g->owner->getComponent<RigidBodyComponent>();
    if (rb && tracking_) {
        rb->setGravityFactorRuntime(1.f);
        rb->setContinuousRuntime(rb->continuous);   // 던지면 아래에서 다시 켬
    }
    if (rb && (wasDynamic_ || tracking_)) {
        if (wasDynamic_) rb->setMotionRuntime(EMotionType::Dynamic);
        // 잡은 동안 Kinematic 바디는 손을 따라가는 속도(MoveKinematic)를 갖고 있고, Dynamic 으로 돌려도 그 속도가 남습니다.
        // 던지지 않고 놓을 때(throwable=false, 다른 물체로 바꿔 잡기, 손 · 잡기 끄기)는 속도를 지워 그 자리에서 떨어지게
        // 합니다. 예전에는 남은 손 속도로 날아가서 throwable=false 가 사실상 동작하지 않았습니다(DEF-143).
        const bool toss = throwIt && g->throwable;
        rb->setLinearVelocity(toss ? linearVelocity : glm::vec3(0.f));
        rb->setAngularVelocity(toss ? angularVelocity : glm::vec3(0.f));
        // 던진 물체는 빠르고 작아 얇은 벽을 뚫기 쉬우므로 연속 충돌을 켭니다(P1). 느려지면 Jolt 가 훑지 않아 비용이 거의 없습니다.
        if (toss) rb->setContinuousRuntime(true);
    }
    wasDynamic_ = false;
    tracking_   = false;
}

void GrabInteractorComponent::drop() { release(glm::vec3(0.f), glm::vec3(0.f), false); }
GrabInteractorComponent::~GrabInteractorComponent() { if (held_) held_->holder_ = nullptr; }

void GrabInteractorComponent::onDisable() {
    drop();
    prevHeld_ = false;
}

void GrabInteractorComponent::tick(float dt) {
    if (!owner) return;
    const auto* tp   = owner->getComponent<TrackedPoseComponent>();
    const int   hand = tp ? tp->handIndex() : -1;
    if (hand < 0) return;
    HInputSystem&        in = HInputSystem::get();
    const HXrInputState& xr = in.xr();
    const bool held = in.held(hand == 0 ? "GrabLeft" : "GrabRight");
    const glm::vec3 hp = owner->worldPosition();
    const glm::quat hr = owner->worldRotation();
    // 손 속도는 리그(부모) 기준이라 월드로 돌립니다. 잡고 있는 동안 표본을 모읍니다(놓는 틱 포함).
    const Actor*    rig  = owner->getParent();
    const glm::quat rigR = rig ? rig->worldRotation() : glm::quat(1.f, 0.f, 0.f, 0.f);
    if (held_) velHistory_.push(rigR * xr.hands[hand].linearVelocity, rigR * xr.hands[hand].angularVelocity);
    if (held && !prevHeld_) {
        if (GrabbableComponent* g = pick(hp, radius)) {
            grab(g, hp, hr);
            in.haptic(hand, 0.6f, 0.04f);
        }
    } else if (!held && prevHeld_ && held_) {
        glm::vec3 lin, ang;
        velHistory_.mean(lin, ang);
        const auto*     loco   = rig ? rig->getComponent<RigLocomotionComponent>() : nullptr;
        // 물체는 아직 지난 틱의 손 자세에 있으므로 지금 손 자세로 질량 중심을 구합니다(follow 와 같은 식).
        const glm::vec3 com    = hp + hr * (offPos_ + offRot_ * comLocal_);
        const auto      v      = hxr::throwVelocity(lin, ang, hp, com, loco ? loco->velocity() : glm::vec3(0.f));
        release(v.linear, v.angular, true);
        in.haptic(hand, 0.3f, 0.02f);
    }
    prevHeld_ = held;
    follow(hp, hr, dt);
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
