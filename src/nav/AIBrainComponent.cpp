#include "AIBrainComponent.hpp"
#include "NavAgentComponent.hpp"
#include "NavLog.hpp"

#include "../core/FieldParse.hpp"
#include "../scene/Actor.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/Reflect.hpp"
#include "../scene/World.hpp"

#include <algorithm>
#include <cmath>

NavAgentComponent* AIBrainComponent::agent() const {
    return owner ? owner->getComponent<NavAgentComponent>() : nullptr;
}

Actor* AIBrainComponent::target() const {
    World* w = owner ? owner->world() : nullptr;
    return (w && targetId_) ? w->findById(targetId_) : nullptr;
}

Actor* AIBrainComponent::nearestTarget(float radius) const {
    World* w = owner ? owner->world() : nullptr;
    if (!w || targetTag.empty()) return nullptr;
    const glm::vec3 me = owner->worldPosition();
    Actor* best = nullptr;
    float bestD = radius;
    for (Actor* a : w->actorsWithTag(targetTag)) {
        if (a == owner || !a->activeInHierarchy()) continue;
        const float d = glm::length(a->worldPosition() - me);
        if (d <= bestD) { bestD = d; best = a; }
    }
    return best;
}

void AIBrainComponent::onStart() {
    started_ = true;
    start();
}

void AIBrainComponent::onEnable() {
    if (started_ && !running_) start();   // 껐다 켠 뒤(처음은 onStart)
}

void AIBrainComponent::start() {
    World* w = owner ? owner->world() : nullptr;
    if (!w || running_) return;
    running_ = true;
    moveSub_ = w->events().subscribe<NavMoveFinished>([this](const NavMoveFinished& e) { onMoveFinished(e); });
    senseTimer_ = w->timers().every(std::max(senseInterval, 0.02f), [this] { sense(); }, owner, 0.f);
    if (!patrolTag.empty() && !w->actorsWithTag(patrolTag).empty()) goToPatrolPoint();
    else enter(EAIState::Wait);
}

void AIBrainComponent::stopAll() {
    if (World* w = owner ? owner->world() : nullptr) {
        if (senseTimer_) w->timers().cancel(senseTimer_);
        if (waitTimer_)  w->timers().cancel(waitTimer_);
    }
    senseTimer_ = waitTimer_ = 0;
    moveSub_.reset();
    running_ = false;
}

void AIBrainComponent::onDisable() {
    stopAll();
    if (NavAgentComponent* a = agent()) a->stop();
}

void AIBrainComponent::onDestroy() { stopAll(); }

void AIBrainComponent::enter(EAIState s, Actor* t) {
    World* w = owner ? owner->world() : nullptr;
    const uint64_t tid = t ? t->id : 0;
    if (s == state_ && tid == targetId_ && changes_ > 0) return;
    const EAIState from = state_;
    state_ = s;
    targetId_ = tid;
    ++changes_;
    if (waitTimer_ && w) { w->timers().cancel(waitTimer_); waitTimer_ = 0; }
    HLOG(LogNav, Verbose) << "AI '" << owner->name << "': " << aiStateName(from) << " -> " << aiStateName(s);
    if (s == EAIState::Wait && w && !patrolTag.empty()) {
        waitTimer_ = w->timers().after(std::max(waitTime, 0.f), [this] {
            waitTimer_ = 0;
            if (state_ == EAIState::Wait) goToPatrolPoint();
        }, owner);
    }
    if (w) {
        AIStateChanged e;
        e.actor = owner;
        e.from = from;
        e.to = s;
        e.target = t;
        w->events().publish(e);
    }
}

void AIBrainComponent::goToPatrolPoint() {
    World* w = owner ? owner->world() : nullptr;
    NavAgentComponent* a = agent();
    if (!w || !a || patrolTag.empty()) return;
    std::vector<Actor*> pts;
    for (Actor* p : w->actorsWithTag(patrolTag))
        if (p != owner && p->activeInHierarchy()) pts.push_back(p);
    if (pts.empty()) { enter(EAIState::Wait); return; }
    std::sort(pts.begin(), pts.end(), [](const Actor* x, const Actor* y) { return x->name != y->name ? x->name < y->name : x->id < y->id; });
    patrolIndex_ = (patrolIndex_ + 1) % (int)pts.size();
    enter(EAIState::Patrol);
    a->moveTo(pts[(size_t)patrolIndex_]->worldPosition());   // 실패하면 NavMoveFinished → Wait → 다음 점
}

void AIBrainComponent::onMoveFinished(const NavMoveFinished& e) {
    if (e.agent != owner) return;
    if (state_ == EAIState::Patrol) enter(EAIState::Wait);
}

void AIBrainComponent::sense() {
    NavAgentComponent* a = agent();
    if (!a || targetTag.empty()) return;
    if (state_ == EAIState::Chase) {
        Actor* t = target();
        const float lose = std::max(loseRadius, sightRadius);
        if (!t || !t->activeInHierarchy() || glm::length(t->worldPosition() - owner->worldPosition()) > lose) {
            a->stop();
            enter(EAIState::Wait);
            return;
        }
        // 도착해 멈췄는데 대상이 다시 멀어지면 다시 쫓음.
        const glm::vec3 d = t->worldPosition() - owner->worldPosition();
        if (a->status() != ENavMoveStatus::Moving && std::sqrt(d.x * d.x + d.z * d.z) > a->arrivalRadius + 0.5f) a->moveToActor(t);
        return;
    }
    if (Actor* t = nearestTarget(sightRadius)) {
        enter(EAIState::Chase, t);
        a->moveToActor(t);
    }
}

void AIBrainComponent::writeFields(ComponentFieldWriter& w) const {
    if (!patrolTag.empty()) w.s("patrolTag", patrolTag);
    if (!targetTag.empty()) w.s("targetTag", targetTag);
    w.f("sightRadius", sightRadius);
    w.f("loseRadius", loseRadius);
    w.f("waitTime", waitTime);
    w.f("senseInterval", senseInterval);
}

bool AIBrainComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    auto num = [&](float& dst, float lo, float hi) {
        float v = 0.f;
        ok = fieldparse::toFloat(value, v) && std::isfinite(v);
        if (ok) dst = std::clamp(v, lo, hi);
    };
    if      (key == "patrolTag")     { ok = value.empty() || Actor::validTag(value); if (ok) patrolTag = value; }
    else if (key == "targetTag")     { ok = value.empty() || Actor::validTag(value); if (ok) targetTag = value; }
    else if (key == "sightRadius")   num(sightRadius, 0.f, 1000.f);
    else if (key == "loseRadius")    num(loseRadius, 0.f, 1000.f);
    else if (key == "waitTime")      num(waitTime, 0.f, 3600.f);
    else if (key == "senseInterval") num(senseInterval, 0.02f, 60.f);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}

std::vector<PropDesc> AIBrainComponent::buildProps() {
    auto* p = this;
    return {
        XPROP_FloatClamp(p, "Sight Radius", PropColor::Neutral, 0.1f, sightRadius, 0.f, 1000.f),
        XPROP_FloatClamp(p, "Lose Radius", PropColor::Neutral, 0.1f, loseRadius, 0.f, 1000.f),
        XPROP_FloatClamp(p, "Wait Time", PropColor::Neutral, 0.05f, waitTime, 0.f, 3600.f),
        XPROP_FloatClamp(p, "Sense Interval", PropColor::Neutral, 0.01f, senseInterval, 0.02f, 60.f),
    };
}

std::string AIBrainComponent::readOnlyInfo() const {
    std::string s = std::string("State: ") + aiStateName(state_);
    s += "\nPatrol tag: " + (patrolTag.empty() ? std::string("(none)") : patrolTag);
    s += "\nTarget tag: " + (targetTag.empty() ? std::string("(none)") : targetTag);
    return s;
}
