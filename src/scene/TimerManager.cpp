#include "TimerManager.hpp"

#include <algorithm>
#include <cmath>

TimerHandle TimerManager::add(double delay, float interval, Fn fn, const Actor* owner) {
    if (!fn) return 0;
    auto t      = std::make_unique<Timer>();
    t->handle   = nextHandle_++;
    t->due      = now_ + std::max(0.0, std::isfinite(delay) ? delay : 0.0);
    t->interval = interval;
    t->owner    = owner;
    t->fn       = std::move(fn);
    const TimerHandle h = t->handle;
    timers_.push_back(std::move(t));
    return h;
}

TimerHandle TimerManager::after(float seconds, Fn fn, const Actor* owner) {
    return add((double)seconds, 0.f, std::move(fn), owner);
}

TimerHandle TimerManager::every(float interval, Fn fn, const Actor* owner, float firstDelay) {
    const float iv = std::isfinite(interval) ? std::max(interval, 1e-4f) : 1.f;
    return add(firstDelay < 0.f ? (double)iv : (double)firstDelay, iv, std::move(fn), owner);
}

TimerManager::Timer* TimerManager::find(TimerHandle h) {
    for (auto& t : timers_)
        if (t->handle == h && !t->dead) return t.get();
    return nullptr;
}
const TimerManager::Timer* TimerManager::find(TimerHandle h) const {
    for (const auto& t : timers_)
        if (t->handle == h && !t->dead) return t.get();
    return nullptr;
}

bool TimerManager::cancel(TimerHandle h) {
    Timer* t = h ? find(h) : nullptr;
    if (!t) return false;
    t->dead = true;
    t->fn   = nullptr;   // 붙잡은 값을 바로 놓음(도는 중이면 tick 이 부른 사본이 끝까지 삶)
    return true;
}

void TimerManager::cancelAll(const Actor* owner) {
    if (!owner) return;
    for (auto& t : timers_)
        if (t->owner == owner && !t->dead) { t->dead = true; t->fn = nullptr; }
}

void TimerManager::clear() {
    for (auto& t : timers_) { t->dead = true; t->fn = nullptr; }
    if (!ticking_) timers_.clear();
}

bool TimerManager::active(TimerHandle h) const { return h && find(h); }

float TimerManager::remaining(TimerHandle h) const {
    const Timer* t = h ? find(h) : nullptr;
    return t ? (float)std::max(0.0, t->due - now_) : -1.f;
}

size_t TimerManager::count() const {
    size_t n = 0;
    for (const auto& t : timers_) n += t->dead ? 0 : 1;
    return n;
}

void TimerManager::tick(double now) {
    now_     = now;
    ticking_ = true;
    // 콜백이 더한 타이머는 이번 틱에 보지 않습니다(크기를 먼저 잼). unique_ptr 라 더해도 앞의 포인터는 그대로입니다.
    const size_t n = timers_.size();
    for (size_t i = 0; i < n; ++i) {
        Timer* t = timers_[i].get();
        int calls = 0;
        while (!t->dead && t->due <= now && calls < kMaxCallsPerTick) {
            ++calls;
            if (t->interval <= 0.f) t->dead = true;   // 한 번: 부르기 전에 끝냄(콜백 안의 cancel · active 가 맞게)
            else t->due += t->interval;
            Fn fn = t->dead ? std::move(t->fn) : t->fn;   // 콜백이 자기를 취소해 t->fn 을 비워도 안전하게 사본으로 부름
            fn();
        }
        if (!t->dead && t->interval > 0.f && t->due <= now) t->due = now + t->interval;   // 상한을 넘은 몫은 버림
    }
    ticking_ = false;
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [](const std::unique_ptr<Timer>& t) { return t->dead; }),
                  timers_.end());
}
