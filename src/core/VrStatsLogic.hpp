#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace hvrstats {

enum class VrStatsButton : uint8_t {
    X = 0,
    Y,
    None,
};

inline bool parseVrStatsButton(std::string_view s, VrStatsButton& out) {
    if (s == "x")    { out = VrStatsButton::X;    return true; }
    if (s == "y")    { out = VrStatsButton::Y;    return true; }
    if (s == "none") { out = VrStatsButton::None; return true; }
    return false;
}

inline const char* vrStatsButtonName(VrStatsButton b) {
    switch (b) {
        case VrStatsButton::X:    return "x";
        case VrStatsButton::Y:    return "y";
        case VrStatsButton::None: return "none";
    }
    return "x";
}

struct ToggleInput {
    bool          buttonDown        = false;
    bool          wasButtonDown     = false;
    bool          buttonBoundInGame = false;
    VrStatsButton configuredButton  = VrStatsButton::X;
};

// 눌린 가장자리이고, 게임 입력에 묶이지 않았을 때만 토글합니다(3D 패널 클릭은 트리거라 X와 간섭하지 않음).
inline bool shouldToggleVrStats(const ToggleInput& in) {
    if (in.configuredButton == VrStatsButton::None) return false;
    if (in.buttonBoundInGame) return false;
    return in.buttonDown && !in.wasButtonDown;
}

enum class BudgetZone : uint8_t {
    Under = 0,       // < 90% (또는 FPS >= 98%)
    Warn90To100,     // 90% .. 100% (또는 FPS 90% .. 98%)
    Over,            // > 100% (또는 FPS < 90%)
};

inline BudgetZone classifyBudget(float ms, float budgetMs) {
    if (budgetMs <= 0.f) return BudgetZone::Under;
    if (ms > budgetMs) return BudgetZone::Over;
    if (ms >= budgetMs * 0.9f) return BudgetZone::Warn90To100;
    return BudgetZone::Under;
}

// FPS 줄은 목표 주사율 대비 달성률로 색을 정합니다(XR vsync 환경에서 90 Hz 유지 시 초록).
inline BudgetZone classifyFps(float fpsAvg, float targetHz) {
    if (targetHz <= 0.f) return BudgetZone::Under;
    if (fpsAvg >= targetHz * 0.98f) return BudgetZone::Under;
    if (fpsAvg >= targetHz * 0.90f) return BudgetZone::Warn90To100;
    return BudgetZone::Over;
}

// 0.5초 갱신 구간 평균과 지난 1초 FPS(평균 · 최소)를 모으는 순수 누적기(VS1b).
struct VrStatsAccumulator {
    double   cpuWorkSum      = 0.0;
    double   frameSum        = 0.0;
    uint32_t samples         = 0;

    uint32_t fpsFrames       = 0;
    float    fpsElapsedSec   = 0.f;
    float    fpsMaxFrameMs   = 0.f;
    float    lastFpsAvg      = 0.f;
    float    lastFpsMin      = 0.f;
    bool     hasCompletedSec = false;

    void reset() {
        cpuWorkSum = 0.0; frameSum = 0.0; samples = 0;
        fpsFrames = 0; fpsElapsedSec = 0.f; fpsMaxFrameMs = 0.f;
        lastFpsAvg = 0.f; lastFpsMin = 0.f; hasCompletedSec = false;
    }

    void addFrame(float cpuWorkMs, float frameMs, float wallDtSec) {
        cpuWorkSum += cpuWorkMs;
        frameSum   += frameMs;
        ++samples;

        ++fpsFrames;
        fpsElapsedSec += std::max(wallDtSec, 0.f);
        if (frameMs > fpsMaxFrameMs) fpsMaxFrameMs = frameMs;
        if (fpsElapsedSec >= 1.0f && fpsFrames > 0) {
            lastFpsAvg      = static_cast<float>(fpsFrames) / fpsElapsedSec;
            lastFpsMin      = fpsMaxFrameMs > 0.f ? (1000.f / fpsMaxFrameMs) : lastFpsAvg;
            hasCompletedSec = true;
            fpsFrames       = 0;
            fpsElapsedSec   = 0.f;
            fpsMaxFrameMs   = 0.f;
        }
    }

    struct WindowAvg {
        float cpuWorkMs = 0.f;
        float frameMs   = 0.f;
        float fpsAvg    = 0.f;
        float fpsMin    = 0.f;
    };

    WindowAvg flushRefreshWindow(float fallbackCpuMs, float fallbackFrameMs) {
        WindowAvg out;
        out.cpuWorkMs = samples > 0 ? static_cast<float>(cpuWorkSum / samples) : fallbackCpuMs;
        out.frameMs   = samples > 0 ? static_cast<float>(frameSum / samples) : fallbackFrameMs;
        if (hasCompletedSec) {
            out.fpsAvg = lastFpsAvg;
            out.fpsMin = lastFpsMin;
        } else if (fpsElapsedSec > 0.f && fpsFrames > 0) {
            out.fpsAvg = static_cast<float>(fpsFrames) / fpsElapsedSec;
            out.fpsMin = fpsMaxFrameMs > 0.f ? (1000.f / fpsMaxFrameMs) : out.fpsAvg;
        } else if (fallbackFrameMs > 0.f) {
            out.fpsAvg = out.fpsMin = 1000.f / fallbackFrameMs;
        }
        cpuWorkSum = 0.0;
        frameSum   = 0.0;
        samples    = 0;
        return out;
    }
};

struct VrStatsSnapshot {
    float       fpsAvg      = 0.f;
    float       fpsMin      = 0.f;
    float       targetHz    = 90.f;
    float       frameMs     = 0.f;
    float       gpuTotalMs  = 0.f;
    float       shadowMs    = 0.f;
    float       mainMs      = 0.f;
    float       aoMs        = 0.f;
    float       bloomMs     = 0.f;
    float       tonemapMs   = 0.f;
    float       uiMs        = 0.f;
    float       cpuWorkMs   = 0.f;
    uint32_t    eyeW        = 0;
    uint32_t    eyeH        = 0;
    float       xrScale     = 1.f;
    int         msaaSamples = 4;
    std::string perfLevel;
    uint32_t    heapChunks         = 0;
    uint64_t    heapCommittedBytes = 0;
    double      rssMb              = 0.0;
    uint32_t    drawCalls          = 0;
    uint32_t    tris               = 0;
};

inline std::vector<std::string> formatVrStatsLines(const VrStatsSnapshot& s) {
    std::vector<std::string> lines;
    char buf[160];

    std::snprintf(buf, sizeof(buf), "FPS  avg %5.1f  min %3.0f / %3.0f Hz   frame %5.2f ms",
                  s.fpsAvg, s.fpsMin, s.targetHz, s.frameMs);
    lines.emplace_back(buf);

    std::snprintf(buf, sizeof(buf), "GPU  total %5.2f ms   CPU work %5.2f ms",
                  s.gpuTotalMs, s.cpuWorkMs);
    lines.emplace_back(buf);

    std::snprintf(buf, sizeof(buf), "Pass shd %4.2f  main %4.2f  ao %4.2f  blm %4.2f  tm %4.2f  ui %4.2f",
                  s.shadowMs, s.mainMs, s.aoMs, s.bloomMs, s.tonemapMs, s.uiMs);
    lines.emplace_back(buf);

    if (s.perfLevel.empty()) {
        std::snprintf(buf, sizeof(buf), "Eye  %ux%u (x%.2f)  MSAA %dx",
                      s.eyeW, s.eyeH, s.xrScale, s.msaaSamples);
    } else {
        std::snprintf(buf, sizeof(buf), "Eye  %ux%u (x%.2f)  MSAA %dx  Perf %s",
                      s.eyeW, s.eyeH, s.xrScale, s.msaaSamples, s.perfLevel.c_str());
    }
    lines.emplace_back(buf);

    const uint64_t mb = s.heapCommittedBytes / (1024ULL * 1024ULL);
    std::snprintf(buf, sizeof(buf), "Mem  chunks %u (%llu MB)  RSS %.1f MB",
                  s.heapChunks, static_cast<unsigned long long>(mb), s.rssMb);
    lines.emplace_back(buf);

    std::snprintf(buf, sizeof(buf), "Draw %-5u  Tris %u", s.drawCalls, s.tris);
    lines.emplace_back(buf);

    return lines;
}

} // namespace hvrstats
