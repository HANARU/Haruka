#pragma once
#include <chrono>
#include <cstdint>
#include <initializer_list>

#include "Profiler.hpp"

// ── CPU 프레임 계측 (진단용) ──────────────────────────────────────────────────
//
// **왜 있는가**
//
// 이 엔진에는 GPU 타임스탬프 계측만 있었고 CPU 벽시계 계측이 한 줄도 없었습니다.
// 그 결과 "GPU 합계 0.37 ms 인데 실제 프레임은 11 ms" 라는 상황에서 나머지 10 ms 가
// 어디 있는지 아무도 알 수 없었습니다. GPU 타임스탬프는 원리적으로 vkWaitForFences,
// vkAcquireNextImageKHR, vkQueuePresentKHR 대기와 스왑체인 재생성을 담을 수 없습니다.
//
// **왜 전역인가**
//
// 메인 루프(main.cpp), 렌더러(Renderer_render.cpp), 에디터 오버레이(EditorApp.cpp)가
// 서로 다른 계층에 있는데 계측만을 위해 세 계층에 포인터를 꿰는 것은 과합니다.
// 진단 도구이므로 전역으로 둡니다.
//
// **추적 파일과의 관계(2026-11-20, Docs/CPU_Profiling.md D7)**
//
// 이 평균은 화면 표 · 끊김 로그용으로 남고, 이름 붙은 구간은 기록 중일 때 같은 두 시각으로 추적 이벤트(hprof)도
// 남깁니다. 그래서 이 구간에는 HPROF_SCOPE 를 따로 넣지 않습니다.
// **이 구조체에 렌더링이나 게임 로직이 의존하게 만들지 마십시오.**
//
// 단일 스레드(메인 루프)에서만 씁니다. 다른 스레드에서 건드리지 마십시오.

struct FrameTimings {
    // 지수 이동 평균입니다. 한 프레임짜리 튀는 값에 눈이 따라가지 않게 합니다.
    // last 는 이번 프레임의 값입니다(루프 시작에 resetLast 가 0 으로). 예산을 넘은 프레임이 어느 구간 때문인지
    // 알리는 데 씁니다(2026-11-18, 성능 안정화 1-1).
    struct Avg {
        const char* name = nullptr;   // 추적 이벤트 이름(정적 수명). 없으면 추적에 남기지 않습니다
        float v    = 0.f;
        float last = 0.f;
        void  add(float ms) { v = v * 0.9f + ms * 0.1f; last += ms; }
        // 구간 [t0, t1] 을 평균에 더하고, 기록 중이면 같은 시각으로 추적 이벤트를 남깁니다(측정 지점 공유).
        void addSpan(std::chrono::steady_clock::time_point t0, std::chrono::steady_clock::time_point t1) {
            add(std::chrono::duration<float, std::milli>(t1 - t0).count());
#if HARUKA_PROFILER
            if (name && hprof::recording()) hprof::emit(name, hprof::toNs(t0), hprof::toNs(t1));
#endif
        }
    };

    // ── Renderer::drawFrame 내부 구간 ────────────────────────────────────────
    Avg fenceWait{ "Renderer.FenceWait" };          // 프레임 슬롯 펜스 대기
    Avg reflCapture{ "Renderer.ReflectionCapture" };// 반사 캡처 확인(찍는 프레임만 큼)
    Avg acquire{ "Renderer.Acquire" };              // vkAcquireNextImageKHR
    Avg imgFence{ "Renderer.ImageFence" };          // imagesInFlight 펜스 대기
    Avg extract{ "Renderer.Extract" };              // extractRenderView + 컬 입력 업로드
    Avg record{ "Renderer.Record" };                // 커맨드 버퍼 기록 전체
    Avg uiRecord{ "Renderer.RecordUI" };            // 그중 HSlate UI 기록
    Avg submit{ "Renderer.Submit" };                // vkQueueSubmit
    Avg present{ "Renderer.Present" };              // vkQueuePresentKHR + 필요 시 재생성
    Avg drawFrame{ "Renderer.DrawFrame" };          // drawFrame 전체

    // ── 메인 루프의 drawFrame 바깥 구간 ──────────────────────────────────────
    Avg pollEvents{ "Window.PollEvents" };
    Avg xrWait{ "XR.WaitFrame" };                   // xrWaitFrame + xrBeginFrame(헤드셋이 루프 속도를 정하는 대기)
    Avg tick{ "Engine.Tick" };                      // editor.tick + world.tick + 모듈
    Avg worldTick{ "World.Tick" };                  // 그중 world.tick + 게임 모듈
    Avg modules{ "Engine.Modules" };                // 그중 모듈(물리 · 소리)
    Avg physics{ "Physics.Step" };                  // 그중 물리
    Avg handleInput{ "Engine.FrontendInput" };
    Avg loopTotal;    // 루프 1 회 전체. 이것이 곧 실제 프레임 간격입니다

    void resetLast() {
        for (Avg* a : { &fenceWait, &reflCapture, &acquire, &imgFence, &extract, &record, &uiRecord, &submit, &present,
                        &drawFrame, &pollEvents, &xrWait, &tick, &worldTick, &modules, &physics, &handleInput, &loopTotal })
            a->last = 0.f;
    }

    // ── 카운터 ───────────────────────────────────────────────────────────────
    // 스왑체인 또는 씬 렌더 타깃(뷰포트 크기 변경)의 재생성 횟수입니다. 이 값이 초당
    // 수십 회로 오르면 매 프레임 재생성 루프에 빠진 것이고, 그 비용은 어떤 GPU
    // 타임스탬프에도 잡히지 않습니다. 스플리터를 끄는 동안에는 크기가 바뀌므로 오릅니다.
    uint64_t recreateCount = 0;

    // 프레젠트 모드와 스왑체인 이미지 수. 실행 중에 확인할 방법이 없었습니다.
    int presentMode = -1;
    int imageCount  = 0;
};

// 진단 전용 전역입니다. 위 주석을 읽으십시오.
inline FrameTimings& gFrameTimings() {
    static FrameTimings t;
    return t;
}

// 구간 하나를 재는 도우미입니다. 소멸 시점에 평균에 더합니다(이름이 있으면 추적 이벤트도).
class ScopedCpuTimer {
public:
    explicit ScopedCpuTimer(FrameTimings::Avg& slot)
        : slot_(slot), t0_(std::chrono::steady_clock::now()) {}
    ~ScopedCpuTimer() { slot_.addSpan(t0_, std::chrono::steady_clock::now()); }
    ScopedCpuTimer(const ScopedCpuTimer&)            = delete;
    ScopedCpuTimer& operator=(const ScopedCpuTimer&) = delete;
private:
    FrameTimings::Avg&                                 slot_;
    std::chrono::steady_clock::time_point              t0_;
};
