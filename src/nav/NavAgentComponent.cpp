#include "NavAgentComponent.hpp"
#include "NavEvents.hpp"
#include "NavLog.hpp"

#include "../core/FieldParse.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../scene/Actor.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/Reflect.hpp"
#include "../scene/World.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace {
constexpr float kOffPathDistance = 1.f;    // 경로 선분에서 이만큼 밀려나면 다시 찾음(D6)
constexpr float kTargetMoved     = 0.25f;  // 쫓는 액터가 이만큼 움직이면 다시 찾음
constexpr float kStuckTime       = 2.f;    // 이 시간 동안 거의 못 움직이면 실패(막힘)
constexpr float kMaxFallSpeed    = 50.f;

glm::vec3 flat(const glm::vec3& v) { return { v.x, 0.f, v.z }; }
float flatDist(const glm::vec3& a, const glm::vec3& b) { return glm::length(flat(a - b)); }

// p 에서 선분 a-b 까지의 가로 거리.
float flatDistToSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 ab = flat(b - a), ap = flat(p - a);
    const float l2 = glm::dot(ab, ab);
    const float t = l2 > 1e-8f ? std::clamp(glm::dot(ap, ab) / l2, 0.f, 1.f) : 0.f;
    return glm::length(ap - ab * t);
}
} // namespace

const char* navMoveStatusName(ENavMoveStatus s) {
    switch (s) {
        case ENavMoveStatus::Moving:  return "moving";
        case ENavMoveStatus::Arrived: return "arrived";
        case ENavMoveStatus::Failed:  return "failed";
        default:                      return "idle";
    }
}

const char* aiStateName(EAIState s) {
    switch (s) {
        case EAIState::Patrol: return "patrol";
        case EAIState::Chase:  return "chase";
        default:               return "wait";
    }
}

bool NavAgentComponent::moveTo(const glm::vec3& goal) {
    targetId_ = 0;
    return plan(goal, true);
}

bool NavAgentComponent::moveToActor(Actor* target) {
    if (!target) return false;
    targetId_ = target->id;
    return plan(target->worldPosition(), true);
}

void NavAgentComponent::stop() {
    status_   = ENavMoveStatus::Idle;
    targetId_ = 0;
    velocity_ = glm::vec3(0.f);
    path_.points.clear();
}

bool NavAgentComponent::plan(const glm::vec3& goal, bool notifyOnFail) {
    World* w = owner ? owner->world() : nullptr;
    NavSystem* nav = w ? w->service<NavSystem>() : nullptr;
    lastPlanTime_ = w ? w->gameTime() : 0.0;
    ++repaths_;
    goal_ = goal;
    plannedTargetPos_ = goal;
    NavPath p;
    if (nav && owner) nav->findPath(owner->worldPosition(), goal, p);
    path_ = std::move(p);
    if (path_.result == ENavPathResult::NoPath || (path_.result == ENavPathResult::Partial && !allowPartial)) {
        if (notifyOnFail) finish(false);
        else status_ = ENavMoveStatus::Failed;
        return false;
    }
    corner_ = path_.points.size() > 1 ? 1 : 0;
    status_ = ENavMoveStatus::Moving;
    stuckTime_ = 0.f;
    return true;
}

void NavAgentComponent::finish(bool success) {
    status_   = success ? ENavMoveStatus::Arrived : ENavMoveStatus::Failed;
    velocity_ = glm::vec3(0.f);
    if (!owner) return;
    const glm::vec3 p = owner->worldPosition();
    HLOG(LogNav, Verbose) << "Nav agent '" << owner->name << "' " << (success ? "arrived" : "failed") << " at (" << p.x << ", "
                          << p.y << ", " << p.z << "), path " << navPathResultName(path_.result);
    if (World* w = owner->world()) {
        NavMoveFinished e;
        e.agent = owner;
        e.success = success;
        e.path = path_.result;
        w->events().publish(e);   // 받는 쪽이 새 이동을 시작해도 됨(상태는 이미 바뀜)
    }
}

glm::vec3 NavAgentComponent::separation(const glm::vec3& pos, const glm::vec3& heading) const {
    glm::vec3 push(0.f);
    World* w = owner ? owner->world() : nullptr;
    if (!w) return push;
    for (NavAgentComponent* o : w->componentsOf<NavAgentComponent>()) {
        if (o == this || !o->isLive() || !o->owner) continue;
        glm::vec3 off = flat(pos - o->owner->worldPosition());
        const float minD = radius + o->radius;
        float d = glm::length(off);
        // 내다보기: 진행 방향 앞, 반경 합보다 가까운 옆 거리로 지나갈 상대는 미리 옆으로 비켜 감(상대가 내 오른쪽이면 왼쪽, 아니면
        // 오른쪽 → 마주 오면 둘 다 오른쪽으로 엇갈림). 범위는 반경 합 + 0.75 초 동안 갈 거리입니다.
        const float lookAhead = minD + speed * 0.75f;
        if (d < lookAhead && d > 1e-4f) {
            const glm::vec3 toOther = -off;
            const glm::vec3 right = glm::cross(heading, glm::vec3(0.f, 1.f, 0.f));
            const float lateral = glm::dot(toOther, right);
            if (glm::dot(toOther, heading) > 0.f && std::fabs(lateral) < minD)
                push += right * (lateral > 0.05f ? -1.f : 1.f) * (1.f - d / lookAhead) * speed;
        }
        if (d >= minD) continue;
        if (d < 1e-4f) {   // 같은 자리: 액터 번호로 방향을 정해 결정적으로 갈라짐
            off = glm::vec3(owner->id < o->owner->id ? 1.f : -1.f, 0.f, 0.f);
            d = 1e-4f;
        }
        // 겹침: 겹친 만큼 밀어냄.
        push += off / d * ((minD - d) / minD) * speed;
    }
    return push;
}

void NavAgentComponent::moveBy(const glm::vec3& hv, float dt) {
    World* w = owner->world();
    const glm::vec3 pos = owner->worldPosition();
    PhysicsSystem* ps = (useCharacter && w) ? w->service<PhysicsSystem>() : nullptr;
    if (ps) {
        if (!character_) {
            PhysicsSystem::CharacterDesc d;
            d.radius = std::max(radius, 0.05f);
            d.height = std::max(height, 2.f * d.radius + 0.01f);
            character_ = ps->createCharacter(d, pos);
            fallSpeed_ = 0.f;
        }
        if (character_) {
            glm::vec3 v = hv;
            v.y = fallSpeed_;
            Actor* ignore[1] = { owner };
            PhysicsSystem::CharacterMove m;
            if (ps->moveCharacter(character_, pos, v, dt, m, ignore)) {
                owner->setWorldPosition(m.position);
                velocity_ = dt > 0.f ? (m.position - pos) / dt : glm::vec3(0.f);
                if (m.onGround) fallSpeed_ = 0.f;
                else fallSpeed_ = std::max(fallSpeed_ + ps->gravity().y * dt, -kMaxFallSpeed);
                return;
            }
            character_ = 0;
        }
    }
    if (glm::dot(hv, hv) < 1e-12f) { velocity_ = glm::vec3(0.f); return; }
    NavSystem* nav = w ? w->service<NavSystem>() : nullptr;
    const glm::vec3 to = pos + hv * dt;
    glm::vec3 out = to;
    if (!nav || !nav->moveAlongSurface(pos, to, out)) out = to;
    owner->setWorldPosition(out);
    velocity_ = dt > 0.f ? (out - pos) / dt : glm::vec3(0.f);
}

void NavAgentComponent::turnToward(const glm::vec3& dir, float dt) {
    const glm::vec3 d = flat(dir);
    if (glm::dot(d, d) < 1e-8f) return;
    const float target = std::atan2(-d.x, -d.z);   // R_y(θ)·(0,0,-1) = d
    const glm::vec3 f = owner->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
    const float cur = std::atan2(-f.x, -f.z);
    float delta = target - cur;
    while (delta >  glm::pi<float>()) delta -= glm::two_pi<float>();
    while (delta < -glm::pi<float>()) delta += glm::two_pi<float>();
    const float step = glm::radians(std::max(angularSpeed, 0.f)) * dt;
    const float yaw = cur + std::clamp(delta, -step, step);
    owner->setWorldRotation(glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)));
}

void NavAgentComponent::tick(float dt) {
    if (!owner) return;
    World* w = owner->world();
    if (status_ != ENavMoveStatus::Moving) {
        if (character_) moveBy(glm::vec3(0.f), dt);   // 서 있어도 중력 · 바닥 붙음
        return;
    }
    const double now = w ? w->gameTime() : 0.0;
    const bool canReplan = now - lastPlanTime_ >= repathInterval;
    glm::vec3 pos = owner->worldPosition();
    if (targetId_) {
        Actor* t = w ? w->findById(targetId_) : nullptr;
        if (!t) { targetId_ = 0; finish(false); return; }
        if (canReplan && flatDist(t->worldPosition(), plannedTargetPos_) > kTargetMoved && !plan(t->worldPosition(), true)) return;
    }
    if (canReplan && corner_ > 0 && corner_ < path_.points.size() &&
        flatDistToSegment(pos, path_.points[corner_ - 1], path_.points[corner_]) > kOffPathDistance) {
        if (!plan(goal_, true)) return;
    }
    const size_t n = path_.points.size();
    if (n == 0) { finish(false); return; }
    // 닿은 모서리를 지나 다음 꺾이는 점으로.
    while (corner_ < n) {
        const float d = flatDist(pos, path_.points[corner_]);
        if (corner_ + 1 == n) {
            if (d <= arrivalRadius) { finish(true); return; }
            break;
        }
        if (d <= std::max(0.1f, speed * dt)) ++corner_;
        else break;
    }
    if (corner_ >= n) { finish(true); return; }
    const glm::vec3 to = flat(path_.points[corner_] - pos);
    const float d = glm::length(to);
    if (d < 1e-5f) { ++corner_; return; }
    float s = std::max(speed, 0.f);
    if (corner_ + 1 == n && dt > 0.f) s = std::min(s, d / dt);   // 끝점을 지나치지 않음
    const glm::vec3 heading = to / d;
    glm::vec3 desired = heading * s;
    if (avoidance) {
        desired += separation(pos, heading);
        const float l = glm::length(desired);
        if (l > speed && l > 0.f) desired *= speed / l;
    }
    moveBy(desired, dt);
    turnToward(desired, dt);
    // 막힘: 원하는 속도의 1/10 도 못 내는 시간이 쌓이면 실패.
    if (dt > 0.f) {
        const float got = glm::length(flat(velocity_));
        stuckTime_ = got < 0.1f * std::max(s, 0.1f) ? stuckTime_ + dt : 0.f;
        if (stuckTime_ > kStuckTime) finish(false);
    }
}

void NavAgentComponent::releaseCharacter() {
    if (character_ && owner && owner->world())
        if (PhysicsSystem* ps = owner->world()->service<PhysicsSystem>()) ps->destroyCharacter(character_);
    character_ = 0;
    fallSpeed_ = 0.f;
}

void NavAgentComponent::onDisable() {
    releaseCharacter();
    stop();
}

void NavAgentComponent::onDestroy() {
    releaseCharacter();
}

void NavAgentComponent::writeFields(ComponentFieldWriter& w) const {
    w.f("speed", speed);
    w.f("angularSpeed", angularSpeed);
    w.f("arrivalRadius", arrivalRadius);
    w.f("radius", radius);
    w.f("height", height);
    w.b("useCharacter", useCharacter);
    w.b("avoidance", avoidance);
    w.b("allowPartial", allowPartial);
    w.f("repathInterval", repathInterval);
}

bool NavAgentComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    auto num = [&](float& dst, float lo, float hi) {
        float v = 0.f;
        ok = fieldparse::toFloat(value, v) && std::isfinite(v);
        if (ok) dst = std::clamp(v, lo, hi);
    };
    auto flag = [&](bool& dst) { ok = fieldparse::toBool(value, dst); };
    if      (key == "speed")          num(speed, 0.f, 100.f);
    else if (key == "angularSpeed")   num(angularSpeed, 0.f, 10000.f);
    else if (key == "arrivalRadius")  num(arrivalRadius, 0.01f, 100.f);
    else if (key == "radius")         num(radius, 0.01f, 10.f);
    else if (key == "height")         num(height, 0.05f, 20.f);
    else if (key == "useCharacter")   flag(useCharacter);
    else if (key == "avoidance")      flag(avoidance);
    else if (key == "allowPartial")   flag(allowPartial);
    else if (key == "repathInterval") num(repathInterval, 0.f, 60.f);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}

std::vector<PropDesc> NavAgentComponent::buildProps() {
    auto* p = this;
    return {
        XPROP_FloatClamp(p, "Speed", PropColor::Neutral, 0.05f, speed, 0.f, 100.f).withDefault(2.f),
        XPROP_FloatClamp(p, "Angular Speed", PropColor::Neutral, 5.f, angularSpeed, 0.f, 10000.f).withDefault(360.f),
        XPROP_FloatClamp(p, "Arrival Radius", PropColor::Neutral, 0.01f, arrivalRadius, 0.01f, 100.f).withDefault(0.3f),
        XPROP_FloatClamp(p, "Radius", PropColor::Neutral, 0.01f, radius, 0.01f, 10.f).withDefault(0.3f),
        XPROP_FloatClamp(p, "Height", PropColor::Neutral, 0.01f, height, 0.05f, 20.f).withDefault(1.8f),
        XPROP_Toggle(p, "Use Character", useCharacter).withDefault(true),
        XPROP_Toggle(p, "Avoidance", avoidance).withDefault(true),
        XPROP_Toggle(p, "Allow Partial", allowPartial).withDefault(true),
        XPROP_FloatClamp(p, "Repath Interval", PropColor::Neutral, 0.05f, repathInterval, 0.f, 60.f).withDefault(0.5f),
    };
}

std::string NavAgentComponent::readOnlyInfo() const {
    std::string s = std::string("Status: ") + navMoveStatusName(status_);
    if (!path_.points.empty())
        s += "\nPath: " + std::to_string(path_.points.size()) + " points, " + navPathResultName(path_.result);
    return s;
}
