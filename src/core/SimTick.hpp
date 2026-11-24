#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hsim {

// ── 고정 시뮬레이션 틱 누적기 (D2a) ──────────────────────────────────────────────
// 프레임 시간(시간 배율 적용)을 누적해 고정 길이(tickStep)의 시뮬레이션 틱을 몇 번 돌릴지 결정합니다.
// 한 프레임에 돌릴 수 있는 최대 틱 수(maxTicksPerFrame)를 넘으면 스파이럴(느린 프레임 뒤 틱이 쌓여
// 더 느려지는 현상)을 막기 위해 남은 시간을 버리고 dropped = true 를 보고합니다.
struct SimAccumulator {
    struct StepResult {
        int   ticks   = 0;       // 이번 프레임에 실행할 시뮬레이션 틱 수
        float alpha   = 0.f;     // 남은 누적 시간 비율 [0, 1)
        bool  dropped = false;   // 상한 초과로 남은 시간을 버렸는지
    };

    float    accum         = 0.f;
    uint64_t totalTicks    = 0;
    uint64_t droppedFrames = 0;

    StepResult advance(float scaledDt, float tickStep, int maxTicksPerFrame) {
        StepResult res;
        if (!(tickStep > 0.f) || !std::isfinite(tickStep)) return res;
        const int cap = std::max(1, maxTicksPerFrame);
        if (scaledDt > 0.f && std::isfinite(scaledDt)) accum += scaledDt;

        while (res.ticks < cap && accum >= tickStep) {
            accum -= tickStep;
            ++res.ticks;
        }
        if (accum >= tickStep) {
            // 상한 초과: 남은 시간을 버려 스파이럴 방지
            accum       = 0.f;
            res.dropped = true;
            ++droppedFrames;
        }
        totalTicks += (uint64_t)res.ticks;
        res.alpha   = std::clamp(accum / tickStep, 0.f, 1.f);
        return res;
    }

    void reset() {
        accum         = 0.f;
        totalTicks    = 0;
        droppedFrames = 0;
    }
};

// ── 틱 입력 눌림 · 뗌 보존 (D2a) ───────────────────────────────────────────────
// 프레임에 시뮬레이션 틱이 0번일 때 눌림/뗌이 사라지거나, 2번일 때 두 번 처리되는 것을 막습니다.
// 프레임 update 마다 recordFrame(heldNow) 로 엣지를 래치하고, 시뮬레이션 틱마다
// beginTick() -> 조회(held/pressed/released) -> consumeTick() 순서로 한 번만 소비합니다.
struct ActionEdgeLatch {
    bool frameHeld       = false;
    bool prevFrameHeld   = false;
    bool framePressed    = false;
    bool frameReleased   = false;

    bool pendingPressed  = false;
    bool pendingReleased = false;

    void recordFrame(bool heldNow) {
        frameHeld     = heldNow;
        framePressed  = heldNow && !prevFrameHeld;
        frameReleased = !heldNow && prevFrameHeld;
        prevFrameHeld = heldNow;
        if (framePressed)  pendingPressed  = true;
        if (frameReleased) pendingReleased = true;
    }

    // 시뮬레이션 틱 중 조회 값
    bool tickPressed()  const { return pendingPressed; }
    bool tickReleased() const { return pendingReleased; }
    // 0틱 프레임 사이에 눌렸다 뗀 경우(pendingPressed && !frameHeld)에도 첫 틱이 눌림을 볼 수 있게 함
    bool tickHeld()     const { return frameHeld || pendingPressed; }

    void consumeTick() {
        pendingPressed  = false;
        pendingReleased = false;
    }

    void reset() {
        frameHeld       = false;
        prevFrameHeld   = false;
        framePressed    = false;
        frameReleased   = false;
        pendingPressed  = false;
        pendingReleased = false;
    }
};

} // namespace hsim
