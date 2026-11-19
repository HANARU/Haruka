#include "AnimationComponent.hpp"
#include "SkinnedMeshComponent.hpp"
#include "../scene/Actor.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/SceneLog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {
constexpr int kMaxEventsPerTick = 256;   // 아주 큰 dt 로 짧은 클립을 수천 바퀴 돌 때의 상한
}

SkinnedMeshComponent* AnimationComponent::mesh() const {
    return owner ? owner->getComponent<SkinnedMeshComponent>() : nullptr;
}

const std::vector<hanim::Clip>* AnimationComponent::clips() const {
    const SkinnedMeshComponent* m = mesh();
    return (m && m->skin()) ? &m->skin()->clips : nullptr;
}

int AnimationComponent::clipIndex(const std::string& name) const {
    const auto* cs = clips();
    return cs ? hanim::findClip(*cs, name) : -1;
}

std::string AnimationComponent::currentClip() const {
    const auto* cs = clips();
    return (cs && cur_.clip >= 0 && cur_.clip < (int)cs->size()) ? (*cs)[(size_t)cur_.clip].name : std::string();
}

float AnimationComponent::clipDuration(const std::string& name) const {
    const int i = clipIndex(name);
    return i >= 0 ? (*clips())[(size_t)i].duration : 0.f;
}

std::vector<std::string> AnimationComponent::clipNames() const {
    std::vector<std::string> out;
    if (const auto* cs = clips()) for (const hanim::Clip& c : *cs) out.push_back(c.name);
    return out;
}

bool AnimationComponent::playIndex(int idx, float fade, bool lp, float spd) {
    const auto* cs = clips();
    if (!cs || idx < 0 || idx >= (int)cs->size()) return false;
    const float dur = (*cs)[(size_t)idx].duration;
    if (fade > 0.f && cur_.clip >= 0) {
        prev_   = cur_;
        fading_ = true;
        fadeDur_ = fade;
        fadeT_   = 0.f;
    } else {
        fading_ = false;
    }
    cur_ = Layer{};
    cur_.clip  = idx;
    cur_.loop  = lp;
    cur_.speed = spd;
    cur_.time  = spd < 0.f ? dur : 0.f;
    stopped_ = false;
    lastRoot_  = rootTranslation(idx, cur_.time);
    rootValid_ = true;
    return true;
}

bool AnimationComponent::play(const std::string& name, float fadeSeconds) {
    started_ = true;
    return playIndex(clipIndex(name), fadeSeconds, loop, speed);
}

void AnimationComponent::stop() { stopped_ = true; }

void AnimationComponent::setTime(float seconds) {
    const auto* cs = clips();
    if (!cs || cur_.clip < 0) return;
    cur_.time = hanim::wrapTime(seconds, (*cs)[(size_t)cur_.clip].duration, cur_.loop);
    cur_.finished = false;
    cur_.cursor.reset();
    lastRoot_ = rootTranslation(cur_.clip, cur_.time);
}

bool AnimationComponent::playAdditive(const std::string& name, float weight) {
    add_ = Layer{};
    addRef_.clear();
    addWeight_ = weight;
    if (name.empty()) return true;
    const int idx = clipIndex(name);
    const SkinnedMeshComponent* m = mesh();
    if (idx < 0 || !m || !m->skin()) return false;
    add_.clip = idx;
    add_.loop = true;
    hskin::restPose(*m->skin(), addRef_);
    hanim::sample((*clips())[(size_t)idx], 0.f, addRef_);
    return true;
}

void AnimationComponent::addNotify(const std::string& clipName, float t, const std::string& eventName) {
    notifies_.push_back({ clipName, t, eventName });
}

void AnimationComponent::setStateMachine(std::shared_ptr<hanim::StateMachine> sm) {
    sm_ = std::move(sm);
    if (sm_) sm_->reset(sm_->current() >= 0 ? sm_->current() : 0);
}

// ── 진행 · 이벤트 ─────────────────────────────────────────────────────────────

void AnimationComponent::fireRange(int clipIdx, float t0, float t1, bool lp, bool inclusiveStart) {
    if (notifies_.empty() || onNotify.empty()) return;
    const auto* cs = clips();
    if (!cs) return;
    const hanim::Clip& c = (*cs)[(size_t)clipIdx];
    const float dur = c.duration;
    if (!(dur > 0.f) || t0 == t1) return;
    const bool fwd = t1 > t0;
    std::vector<std::pair<float, const std::string*>> hits;   // (지난 시점, 이름) — 지난 순서로 알림
    for (const Notify& n : notifies_) {
        if (n.clipName != c.name || n.time < 0.f || n.time > dur) continue;
        if (!lp) {
            const bool in = fwd ? ((inclusiveStart ? n.time >= t0 : n.time > t0) && n.time <= t1)
                                : ((inclusiveStart ? n.time <= t0 : n.time < t0) && n.time >= t1);
            if (in) hits.push_back({ fwd ? n.time : -n.time, &n.name });
            continue;
        }
        // 반복: n.time + k * dur 가 (t0, t1] 에(거꾸로면 [t1, t0)) 드는 k 마다.
        if (fwd) {
            float at = n.time + dur * std::ceil((t0 - n.time) / dur);
            if (at < t0) at += dur;
            if (at == t0 && !inclusiveStart) at += dur;
            for (int k = 0; at <= t1 && k < kMaxEventsPerTick; ++k, at += dur) hits.push_back({ at, &n.name });
        } else {
            float at = n.time + dur * std::floor((t0 - n.time) / dur);
            if (at > t0) at -= dur;
            if (at == t0 && !inclusiveStart) at -= dur;
            for (int k = 0; at >= t1 && k < kMaxEventsPerTick; ++k, at -= dur) hits.push_back({ -at, &n.name });
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (hits.size() > (size_t)kMaxEventsPerTick) hits.resize(kMaxEventsPerTick);
    for (const auto& h : hits) onNotify.broadcast(*h.second);
}

bool AnimationComponent::advance(Layer& L, float dt, bool fire) {
    L.wraps = 0;
    const auto* cs = clips();
    if (!cs || L.clip < 0 || L.clip >= (int)cs->size() || L.finished) return false;
    const float dur = (*cs)[(size_t)L.clip].duration;
    if (!(dur > 0.f)) return false;
    const float t0 = L.time;
    float t1 = t0 + dt * L.speed;
    bool ended = false;
    if (L.loop) {
        if (fire) fireRange(L.clip, t0, t1, true, L.fresh);
        L.wraps = (int)std::floor(t1 / dur);
        if (L.wraps != 0) ended = true;
        L.time = hanim::wrapTime(t1, dur, true);
    } else {
        if (L.speed >= 0.f && t1 >= dur) { t1 = dur; L.finished = true; ended = true; }
        else if (L.speed < 0.f && t1 <= 0.f) { t1 = 0.f; L.finished = true; ended = true; }
        if (fire) fireRange(L.clip, t0, t1, false, L.fresh);
        L.time = t1;
        if (ended && fire) onFinished.broadcast((*cs)[(size_t)L.clip].name);
    }
    L.fresh = false;
    return ended;
}

// ── 루트 모션 ────────────────────────────────────────────────────────────────

int AnimationComponent::rootJoint() const {
    const SkinnedMeshComponent* m = mesh();
    if (!m || !m->skin()) return -1;
    const SkinSourceData& s = *m->skin();
    for (int j : s.order) if (s.joints[(size_t)j].parent < 0) return j;
    return s.joints.empty() ? -1 : 0;
}

glm::vec3 AnimationComponent::rootTranslation(int clipIdx, float t) const {
    const SkinnedMeshComponent* m = mesh();
    const int root = rootJoint();
    const auto* cs = clips();
    if (!m || root < 0 || !cs || clipIdx < 0 || clipIdx >= (int)cs->size()) return glm::vec3(0.f);
    glm::vec3 v = m->skin()->joints[(size_t)root].restT;
    for (const hanim::Track& tr : (*cs)[(size_t)clipIdx].tracks) {
        if (tr.target != root || tr.path != hanim::Path::Translation) continue;
        float out[3];
        hanim::sampleTrack(tr, t, out);
        v = glm::vec3(out[0], out[1], out[2]);
    }
    return v;
}

void AnimationComponent::applyRootMotion() {
    if (!rootMotion || !owner || cur_.clip < 0 || !rootValid_) return;
    const auto* cs = clips();
    if (!cs) return;
    const float dur = (*cs)[(size_t)cur_.clip].duration;
    const glm::vec3 now = rootTranslation(cur_.clip, cur_.time);
    glm::vec3 delta = now - lastRoot_;
    if (cur_.wraps != 0)   // 감은 바퀴마다 한 바퀴 이동(끝 - 처음)을 더합니다
        delta += (float)cur_.wraps * (rootTranslation(cur_.clip, dur) - rootTranslation(cur_.clip, 0.f));
    lastRoot_ = now;
    if (delta == glm::vec3(0.f)) return;
    const Transform& tf = owner->transform();
    owner->translateLocal(tf.rotation() * (delta * tf.scale()));
}

// ── 자세 ─────────────────────────────────────────────────────────────────────

bool AnimationComponent::applyPose() {
    SkinnedMeshComponent* m = mesh();
    const auto* cs = clips();
    if (!m || !cs || !m->skin()) return false;
    const SkinSourceData& skin = *m->skin();
    const bool morph = skin.morphCount > 0;
    std::vector<float>* wA = morph ? &weightsA_ : nullptr;
    std::vector<float>* wB = morph ? &weightsB_ : nullptr;
    if (morph) { weightsA_ = skin.defaultWeights; weightsB_ = skin.defaultWeights; }
    hskin::restPose(skin, scratchA_);
    if (cur_.clip >= 0 && cur_.clip < (int)cs->size())
        hanim::sample((*cs)[(size_t)cur_.clip], cur_.time, scratchA_, wA, &cur_.cursor);
    if (fading_ && prev_.clip >= 0 && prev_.clip < (int)cs->size() && fadeDur_ > 0.f) {
        hskin::restPose(skin, scratchB_);
        hanim::sample((*cs)[(size_t)prev_.clip], prev_.time, scratchB_, wB, &prev_.cursor);
        const float w = std::clamp(fadeT_ / fadeDur_, 0.f, 1.f);
        hanim::blend(scratchB_, scratchA_, w, scratchA_);
        if (morph) for (size_t i = 0; i < weightsA_.size(); ++i) weightsA_[i] = weightsB_[i] + (weightsA_[i] - weightsB_[i]) * w;
    }
    if (add_.clip >= 0 && add_.clip < (int)cs->size() && addWeight_ != 0.f) {
        hskin::restPose(skin, scratchB_);
        hanim::sample((*cs)[(size_t)add_.clip], add_.time, scratchB_, nullptr, &add_.cursor);
        hanim::addAdditive(scratchA_, scratchB_, addRef_, addWeight_);
    }
    if (rootMotion && cur_.clip >= 0) {
        const int root = rootJoint();
        if (root >= 0) scratchA_[(size_t)root].t = rootTranslation(cur_.clip, 0.f);   // 뿌리는 제자리(이동은 액터로)
    }
    m->setPose(scratchA_);
    if (morph) m->setMorphWeights(weightsA_);
    return true;
}

void AnimationComponent::applyPreview() {
    const auto* cs = clips();
    if (!cs || cs->empty()) return;
    int idx = clipIndex(clip);
    if (idx < 0) idx = 0;
    cur_ = Layer{};
    cur_.clip = idx;
    cur_.loop = loop;
    cur_.time = hanim::wrapTime(previewTime, (*cs)[(size_t)idx].duration, loop);
    fading_ = false;
    const bool rm = rootMotion;
    rootMotion = false;   // 미리보기는 액터를 옮기지 않고 뿌리 이동도 그대로 보입니다
    applyPose();
    rootMotion = rm;
}

void AnimationComponent::tick(float dt) {
    if (!owner) return;
    for (const auto& c : owner->getComponents()) {
        if (std::string(c->typeName()) == "HandPoseComponent") {
            if (!warnedHand_) {
                HLOG(LogScene, Warning) << "actor '" << owner->name
                                        << "': AnimationComponent is idle because HandPoseComponent drives the same skeleton";
                warnedHand_ = true;
            }
            return;
        }
    }
    const auto* cs = clips();
    if (!cs || cs->empty()) return;
    if (!started_) {
        started_ = true;
        if (!sm_ && autoPlay) {
            int idx = clipIndex(clip);
            if (idx < 0) idx = 0;
            playIndex(idx, 0.f, loop, speed);
        }
    }
    bool ended = false;
    if (!stopped_) {
        ended = advance(cur_, dt, true);
        if (fading_) {
            advance(prev_, dt, false);
            fadeT_ += dt;
            if (fadeT_ >= fadeDur_) fading_ = false;
        }
        if (add_.clip >= 0) advance(add_, dt, false);
        applyRootMotion();
    }
    if (sm_) {
        const hanim::StateMachine::Step st = sm_->update(ended);
        if (st.to >= 0) {
            const hanim::StateMachine::State& s = sm_->state(st.to);
            const int idx = clipIndex(s.clip);
            if (idx >= 0) playIndex(idx, st.blend, s.loop, s.speed);
            else HLOG(LogScene, Warning) << "actor '" << owner->name << "': state '" << s.name << "' has no clip '" << s.clip << "'";
        }
    }
    if (cur_.clip >= 0) applyPose();
}

// ── 에디터 · 직렬화 ──────────────────────────────────────────────────────────

std::vector<PropDesc> AnimationComponent::buildProps() {
    auto* p = this;
    optionNames_ = clipNames();
    std::vector<const char*> opts;
    for (const std::string& n : optionNames_) opts.push_back(n.c_str());
    if (opts.empty()) opts.push_back("(no clips)");
    std::vector<PropDesc> props;
    props.push_back(makePropEnum("Clip", opts,
        [p] {
            const int i = p->clipIndex(p->clip);
            return i < 0 ? 0 : i;
        },
        [p](int i) {
            if (i >= 0 && i < (int)p->optionNames_.size()) p->clip = p->optionNames_[(size_t)i];
            p->applyPreview();
        }));
    props.push_back(XPROP_Toggle(p, "Auto Play", autoPlay));
    props.push_back(XPROP_Toggle(p, "Loop", loop));
    props.push_back(XPROP_Float(p, "Speed", PropColor::Neutral, 0.01f, speed));
    props.push_back(XPROP_Toggle(p, "Root Motion", rootMotion));
    props.push_back(makePropFloat("Preview Time", PropColor::Neutral, 0.01f,
        [p] { return p->previewTime; },
        [p](float v) { p->previewTime = std::max(0.f, v); p->applyPreview(); }));
    return props;
}

std::string AnimationComponent::readOnlyInfo() const {
    const auto* cs = clips();
    if (!cs) return "no skinned mesh with clips on this actor";
    std::string s = "clips: " + std::to_string(cs->size());
    for (const hanim::Clip& c : *cs) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "  %s %.2fs", c.name.c_str(), c.duration);
        s += buf;
    }
    return s;
}

void AnimationComponent::writeFields(ComponentFieldWriter& w) const {
    if (!clip.empty()) w.s("clip", clip);
    w.b("autoPlay",   autoPlay);
    w.b("loop",       loop);
    w.f("speed",      speed);
    w.b("rootMotion", rootMotion);
}

bool AnimationComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if      (key == "clip")       clip = value;
    else if (key == "autoPlay")   ok = fieldparse::toBool(value, autoPlay);
    else if (key == "loop")       ok = fieldparse::toBool(value, loop);
    else if (key == "speed")      ok = fieldparse::toFloat(value, speed);
    else if (key == "rootMotion") ok = fieldparse::toBool(value, rootMotion);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}
