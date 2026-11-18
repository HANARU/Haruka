#pragma once

// ── 프레임 통계 (2026-11-18 Engine 에서 떼어 냄, 엔진 구조 안정화 S2) ─────────────────────────────────────
// 창 제목의 FPS, 프레임 시간 분포와 끊김 로그(2026-11-18 성능 안정화 1-1), 시작 시간을 맡습니다. 분포는 XR 세션 · 합성 눈
// 프레임만 모읍니다(데스크톱 창은 모니터 대기가 섞여 VR 예산과 비교할 수 없음). 첫 프레임은 시작 비용(파이프라인 · 캡처 등)이라
// 분포에서 빼고 "Startup" 줄에 따로 남깁니다. 로직은 예전 Engine 의 메인 루프 끝 · logFramePacing 그대로입니다.

#include "../core/FramePacing.hpp"

#include <chrono>
#include <cstdint>
#include <string>

class IWindow;

class FrameStats {
public:
    // 엔진 생성 시작(시작 시간 로그의 기준).
    void   markBoot()          { bootStart_ = std::chrono::steady_clock::now(); }
    double msSinceBoot() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bootStart_).count(); }

    // 메인 루프를 시작할 때 FPS 타이머를 맞춥니다.
    void startLoop() { fpsTimer_ = std::chrono::high_resolution_clock::now(); fpsFrames_ = 0; }

    // 루프 1 회를 기록합니다. XR 프레임을 시작했거나(refreshHz = 헤드셋 주사율) 합성 눈을 그린 프레임만 분포에 넣고, 예산을 넘은
    // 프레임은 구간별 시간을 남깁니다(상한 kMaxHitchLogs, 그 뒤로는 세기만).
    void recordFrame(float loopMs, bool xrBegun, bool drewSynthetic, float refreshHz, uint32_t drawnFrames, float gpuMs);

    // 0.5 초마다 창 제목에 FPS 를 붙입니다(창 없는 실행은 안 함). vrNote 는 "  |  VR unavailable" 같은 덧붙임입니다.
    void updateTitle(IWindow& win, const std::string& title, const char* vrNote, bool headless);

    // 모은 분포를 한 줄로 남기고 비웁니다(XR 세션 끝 · 자동 실행 끝).
    void logPacing(const char* when);

private:
    hperf::FrameHistory                   cpuFrames_;            // 루프 1 회(ms)
    hperf::FrameHistory                   gpuFrames_;            // GPU 합계(ms, 타임스탬프라 MAX_FRAMES 앞 프레임 것)
    uint32_t                              overBudget_    = 0;    // 예산을 넘은 프레임 수
    uint32_t                              hitchesLogged_ = 0;    // 자세히 남긴 끊김 수(상한 kMaxHitchLogs)
    float                                 budgetMs_      = 0.f;  // 마지막으로 모은 프레임의 예산(세션을 지운 뒤에도 씀)
    std::chrono::steady_clock::time_point bootStart_{};          // 엔진 생성 시작
    bool                                  firstFrameLogged_ = false;
    static constexpr uint32_t             kMaxHitchLogs = 20;

    std::chrono::high_resolution_clock::time_point fpsTimer_{};
    int                                            fpsFrames_ = 0;
};
