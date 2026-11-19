#pragma once
// 트윈(2026-11-20, Docs/Animation_System.md A7 · D8): 액터 위치 · 회전 · 배율과 임의 float 값을 시간에 따라 이징으로 바꿉니다.
// Godot Tween · Unity DOTween 의 작은 판입니다. World 가 하나를 갖고(World::tweens) 월드 틱 끝에 진행하므로, Play 가 아니거나
// 일시정지면 멈춥니다. 액터가 지워지면 그 액터의 트윈은 함께 사라집니다(끝 콜백 없이). 같은 액터의 같은 속성에 새 트윈을
// 걸면 앞 트윈을 대신합니다(float 트윈은 대신하지 않음).

#include "Actor.hpp"
#include "../math/Damping.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

enum class EEase : uint8_t { Linear, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic, Smoothstep, OutBack, OutElastic };

inline float applyEase(EEase e, float t) {
    using namespace hmath::ease;
    switch (e) {
        case EEase::Linear:     return linear(t);
        case EEase::InQuad:     return inQuad(t);
        case EEase::OutQuad:    return outQuad(t);
        case EEase::InOutQuad:  return inOutQuad(t);
        case EEase::InCubic:    return inCubic(t);
        case EEase::OutCubic:   return outCubic(t);
        case EEase::InOutCubic: return inOutCubic(t);
        case EEase::Smoothstep: return smoothstep(t);
        case EEase::OutBack:    return outBack(t);
        case EEase::OutElastic: return outElastic(t);
    }
    return linear(t);
}

// 트윈 하나의 설정. 클래스 밖에 두는 것은 clang 이 "둘러싼 클래스 안의 기본 멤버 초기화를 가진 형"을 같은 클래스의
// 기본 인자(= {})로 쓰지 못하게 하기 때문입니다(Android 빌드).
struct TweenOptions {
    float                 delay  = 0.f;               // 시작 전 기다림(초). 시작 값은 기다림이 끝날 때 읽습니다
    EEase                 ease   = EEase::InOutQuad;
    int                   repeat = 0;                 // 더 도는 횟수(-1 = 끝없이)
    bool                  yoyo   = false;             // 되풀이할 때 갔다가 돌아옴
    std::function<void()> onDone;                     // 끝났을 때(취소 · 액터 삭제 때는 부르지 않음)
};

class TweenSystem {
public:
    using Handle  = uint32_t;   // 0 = 없음
    using Options = TweenOptions;

    Handle moveTo(Actor* a, const glm::vec3& to, float duration, Options o = Options())   { return addActor(a, Kind::Position, glm::vec4(to, 0.f), {}, duration, std::move(o)); }
    Handle scaleTo(Actor* a, const glm::vec3& to, float duration, Options o = Options())  { return addActor(a, Kind::Scale, glm::vec4(to, 0.f), {}, duration, std::move(o)); }
    Handle rotateTo(Actor* a, const glm::quat& to, float duration, Options o = Options()) { return addActor(a, Kind::Rotation, {}, to, duration, std::move(o)); }
    // 임의 값. owner 가 있으면 그 액터가 지워질 때 함께 취소됩니다.
    Handle floatTo(std::function<float()> get, std::function<void(float)> set, float to, float duration, Options o = Options(),
                   Actor* owner = nullptr) {
        if (!get || !set) return 0;
        Tween t;
        t.kind = Kind::Float; t.actor = owner; t.get = std::move(get); t.set = std::move(set);
        t.toV = glm::vec4(to, 0.f, 0.f, 0.f);
        return push(std::move(t), duration, std::move(o));
    }

    void cancel(Handle h) {
        for (Tween& t : tweens_) if (t.id == h) t.dead = true;
    }
    void cancelAll(const Actor* a) {
        if (!a) return;
        for (Tween& t : tweens_) if (t.actor == a) t.dead = true;
    }
    bool   active(Handle h) const {
        for (const Tween& t : tweens_) if (t.id == h && !t.dead) return true;
        return false;
    }
    size_t count() const {
        size_t n = 0;
        for (const Tween& t : tweens_) n += t.dead ? 0 : 1;
        return n;
    }

    void tick(float dt) {
        // 콜백이 새 트윈을 더할 수 있으므로 번호로 돕니다(새 것은 다음 틱부터).
        const size_t n = tweens_.size();
        for (size_t i = 0; i < n; ++i) {
            if (tweens_[i].dead) continue;
            step(i, dt);
        }
        tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(), [](const Tween& t) { return t.dead; }), tweens_.end());
    }

private:
    enum class Kind : uint8_t { Position, Rotation, Scale, Float };
    struct Tween {
        Handle    id = 0;
        Kind      kind = Kind::Float;
        Actor*    actor = nullptr;
        std::function<float()>     get;
        std::function<void(float)> set;
        glm::vec4 fromV{ 0.f }, toV{ 0.f };
        glm::quat fromQ{ 1.f, 0.f, 0.f, 0.f }, toQ{ 1.f, 0.f, 0.f, 0.f };
        float     duration = 0.f, elapsed = 0.f, delay = 0.f;
        EEase     ease = EEase::InOutQuad;
        int       repeatLeft = 0;
        bool      yoyo = false, reverse = false, started = false, dead = false;
        std::function<void()> onDone;
    };

    Handle addActor(Actor* a, Kind k, const glm::vec4& v, const glm::quat& q, float duration, Options o) {
        if (!a) return 0;
        for (Tween& t : tweens_) if (t.actor == a && t.kind == k) t.dead = true;   // 같은 속성의 앞 트윈을 대신함
        Tween t;
        t.kind = k; t.actor = a; t.toV = v; t.toQ = q;
        return push(std::move(t), duration, std::move(o));
    }
    Handle push(Tween t, float duration, Options o) {
        t.id = ++next_;
        if (t.id == 0) t.id = ++next_;
        t.duration = std::max(duration, 0.f);
        t.delay = std::max(o.delay, 0.f);
        t.ease = o.ease;
        t.repeatLeft = o.repeat;
        t.yoyo = o.yoyo;
        t.onDone = std::move(o.onDone);
        tweens_.push_back(std::move(t));
        return tweens_.back().id;
    }
    void readStart(Tween& t) {
        switch (t.kind) {
            case Kind::Position: t.fromV = glm::vec4(t.actor->transform().position(), 0.f); break;
            case Kind::Scale:    t.fromV = glm::vec4(t.actor->transform().scale(), 0.f); break;
            case Kind::Rotation: t.fromQ = t.actor->transform().rotation(); break;
            case Kind::Float:    t.fromV = glm::vec4(t.get(), 0.f, 0.f, 0.f); break;
        }
    }
    void apply(Tween& t, float u) {
        const float e = applyEase(t.ease, t.reverse ? 1.f - u : u);
        switch (t.kind) {
            case Kind::Position: t.actor->setLocalPosition(glm::vec3(glm::mix(t.fromV, t.toV, e))); break;
            case Kind::Scale:    t.actor->setLocalScale(glm::vec3(glm::mix(t.fromV, t.toV, e))); break;
            case Kind::Rotation: t.actor->setLocalRotation(glm::normalize(glm::slerp(t.fromQ, t.toQ, e))); break;
            case Kind::Float:    t.set(t.fromV.x + (t.toV.x - t.fromV.x) * e); break;
        }
    }
    void step(size_t i, float dt) {
        Tween* t = &tweens_[i];
        if (t->delay > 0.f) {
            t->delay -= dt;
            if (t->delay > 0.f) return;
            dt = -t->delay;   // 기다림을 넘은 만큼은 진행
            t->delay = 0.f;
        }
        if (!t->started) { readStart(*t); t->started = true; }
        t->elapsed += dt;
        for (int guard = 0; guard < 1000; ++guard) {
            const float u = t->duration > 0.f ? std::min(t->elapsed / t->duration, 1.f) : 1.f;
            apply(*t, u);
            if (u < 1.f) return;
            if (t->repeatLeft == 0) {
                t->dead = true;
                if (t->onDone) {
                    std::function<void()> done = std::move(t->onDone);
                    done();   // 콜백이 tweens_ 에 더할 수 있으므로 t 를 다시 쓰지 않음
                }
                return;
            }
            if (t->repeatLeft > 0) --t->repeatLeft;
            t->elapsed -= t->duration;
            if (t->yoyo) t->reverse = !t->reverse;
            if (t->duration <= 0.f) return;
        }
    }

    std::vector<Tween> tweens_;
    Handle             next_ = 0;
};
