#pragma once
// 타이머 · 지연 호출(2026-11-20, Docs/Gameplay_Tools.md D2).
//
// 월드가 소유하고(World::timers()), 게임 시간(시간 배율 적용, 일시정지 동안 멈춤)으로 셉니다. 주인 액터를 주면 그 액터가 지워질 때
// 함께 취소됩니다(트윈과 같은 자리, World::destroyImmediate). UE FTimerManager · Godot SceneTree.create_timer 와 같은 역할입니다.
//
//   auto h = world.timers().after(2.f, [this] { openDoor(); }, owner);        // 2 초 뒤 한 번
//   world.timers().every(0.5f, [this] { spawnWave(); }, owner, 0.f);          // 바로 한 번, 그 뒤 0.5 초마다
//   world.timers().cancel(h);
//
// 한 틱에 반복 간격이 여러 번 지나면 지난 만큼 부르되 틱당 kMaxCallsPerTick 번까지이고, 다음 시각은 예정 시각에 간격을 더해 정합니다
// (프레임 시간이 흔들려도 밀리지 않음). 상한을 넘으면 남은 몫은 버리고 지금부터 다시 셉니다. 같은 틱 안에서는 등록 순서로 부릅니다.
// 콜백 안에서 타이머를 더하거나 취소해도 됩니다. 콜백 안에서 더한 타이머는 빨라도 다음 틱에 부릅니다. 메인 스레드 전용입니다.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class Actor;

using TimerHandle = uint64_t;   // 0 = 없음

class TimerManager {
public:
    using Fn = std::function<void()>;
    static constexpr int kMaxCallsPerTick = 16;

    // seconds 뒤 한 번(0 이하면 다음 틱). fn 이 비었으면 0.
    TimerHandle after(float seconds, Fn fn, const Actor* owner = nullptr);
    // interval 마다(최소 0.0001 초). firstDelay 가 음수면 interval 뒤 처음.
    TimerHandle every(float interval, Fn fn, const Actor* owner = nullptr, float firstDelay = -1.f);

    bool  cancel(TimerHandle h);           // 있었으면 true
    void  cancelAll(const Actor* owner);   // 그 주인의 타이머 모두
    void  clear();                         // 모두(에디터 Play 끝)
    bool  active(TimerHandle h) const;
    float remaining(TimerHandle h) const;  // 다음 호출까지 게임 시간(초), 없으면 -1
    size_t count() const;                  // 살아 있는 타이머 수

    // 월드가 게임 시간을 올린 뒤 부릅니다.
    void tick(double now);

private:
    struct Timer {
        TimerHandle  handle   = 0;
        double       due      = 0.0;
        float        interval = 0.f;   // 0 = 한 번
        const Actor* owner    = nullptr;
        Fn           fn;
        bool         dead     = false;
    };
    Timer*       find(TimerHandle h);
    const Timer* find(TimerHandle h) const;
    TimerHandle  add(double delay, float interval, Fn fn, const Actor* owner);

    std::vector<std::unique_ptr<Timer>> timers_;
    double      now_        = 0.0;
    TimerHandle nextHandle_ = 1;
    bool        ticking_    = false;
};
