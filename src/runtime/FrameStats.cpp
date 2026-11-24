#include "FrameStats.hpp"

#include "../core/FrameTimings.hpp"
#include "../core/HLog.hpp"
#include "../platform/IWindow.hpp"

#include <cstdio>

void FrameStats::recordFrame(float loopMs, bool xrBegun, bool drewSynthetic, float refreshHz, uint32_t drawnFrames,
                             float gpuMs) {
    if (!(xrBegun || drewSynthetic)) return;
    budgetMs_ = hperf::frameBudgetMs(xrBegun ? refreshHz : 0.f);
    if (!firstFrameLogged_) {
        firstFrameLogged_ = true;
        HLOG(LogEngine, Log) << "Startup: first frame drawn " << msSinceBoot()
                             << " ms after the engine started (that frame " << loopMs << " ms)";
        return;
    }
    cpuFrames_.add(loopMs);
    gpuFrames_.add(gpuMs);
    if (!hperf::overBudget(loopMs, budgetMs_, xrBegun)) return;
    ++overBudget_;
    if (hitchesLogged_ >= kMaxHitchLogs) return;
    ++hitchesLogged_;
    const FrameTimings& f = gFrameTimings();
    char buf[400];
    std::snprintf(buf, sizeof(buf),
        "Hitch: frame %u took %.2f ms (budget %.1f): poll %.2f, xr wait %.2f, tick %.2f (world %.2f, modules %.2f, "
        "physics %.2f), input %.2f, draw %.2f (fence %.2f, capture %.2f, extract %.2f, record %.2f, submit %.2f, "
        "present %.2f), GPU %.2f",
        drawnFrames, loopMs, budgetMs_, f.pollEvents.last, f.xrWait.last, f.tick.last, f.worldTick.last,
        f.modules.last, f.physics.last, f.handleInput.last, f.drawFrame.last, f.fenceWait.last,
        f.reflCapture.last, f.extract.last, f.record.last, f.submit.last, f.present.last, gpuMs);
    HLOG(LogEngine, Log) << buf;
    if (hitchesLogged_ == kMaxHitchLogs) {
        HLOG(LogEngine, Log) << "Hitch: " << kMaxHitchLogs << " logged; later ones are only counted";
    }
}

void FrameStats::updateTitle(IWindow& win, const std::string& title, const char* vrNote, bool headless) {
    ++fpsFrames_;
    const auto  now = std::chrono::high_resolution_clock::now();
    const float dt  = std::chrono::duration<float>(now - fpsTimer_).count();
    if (dt >= 0.5f && !headless) {
        const int fps = (int)(fpsFrames_ / dt);
        win.setTitle(title + vrNote + "  |  " + std::to_string(fps) + " FPS");
        fpsTimer_  = now;
        fpsFrames_ = 0;
    }
}

void FrameStats::logPacing(const char* when) {
    if (cpuFrames_.total() == 0) return;
    const hperf::Distribution c = cpuFrames_.summary();
    const hperf::Distribution g = gpuFrames_.summary();
    const float budget = budgetMs_ > 0.f ? budgetMs_ : hperf::frameBudgetMs(0.f);
    char buf[360];
    std::snprintf(buf, sizeof(buf),
                  "Frame pacing (%s): %u frames, CPU frame median %.2f, p99 %.2f, max %.2f ms; GPU median %.2f, p99 %.2f, "
                  "max %.2f ms; over budget %u (budget %.1f ms); dropped sim ticks %llu",
                  when, c.count, c.median, c.p99, c.max, g.median, g.p99, g.max, overBudget_, budget,
                  (unsigned long long)droppedSimTicks_);
    HLOG(LogEngine, Log) << buf;
    cpuFrames_.clear();
    gpuFrames_.clear();
    overBudget_      = 0;
    hitchesLogged_   = 0;
    droppedSimTicks_ = 0;
}
