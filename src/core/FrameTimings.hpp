#pragma once
#include <chrono>
#include <cstdint>

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
// 진단 도구이며 원인이 확정되면 걷어낼 것이므로 전역으로 둡니다.
// **이 구조체에 렌더링이나 게임 로직이 의존하게 만들지 마십시오.**
//
// 단일 스레드(메인 루프)에서만 씁니다. 다른 스레드에서 건드리지 마십시오.

struct FrameTimings {
    // 지수 이동 평균입니다. 한 프레임짜리 튀는 값에 눈이 따라가지 않게 합니다.
    struct Avg {
        float v = 0.f;
        void  add(float ms) { v = v * 0.9f + ms * 0.1f; }
    };

    // ── Renderer::drawFrame 내부 구간 ────────────────────────────────────────
    Avg fenceWait;    // 프레임 슬롯 펜스 대기
    Avg acquire;      // vkAcquireNextImageKHR
    Avg imgFence;     // imagesInFlight 펜스 대기
    Avg extract;      // extractRenderView + 컬 입력 업로드
    Avg record;       // 커맨드 버퍼 기록 전체
    Avg uiRecord;     // 그중 HSlate UI 기록
    Avg submit;       // vkQueueSubmit
    Avg present;      // vkQueuePresentKHR + 필요 시 재생성
    Avg drawFrame;    // drawFrame 전체

    // ── 메인 루프의 drawFrame 바깥 구간 ──────────────────────────────────────
    Avg pollEvents;
    Avg tick;         // editor.tick + world.tick + 모듈
    Avg handleInput;
    Avg loopTotal;    // 루프 1 회 전체. 이것이 곧 실제 프레임 간격입니다

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

// 구간 하나를 재는 도우미입니다. 소멸 시점에 평균에 더합니다.
class ScopedCpuTimer {
public:
    explicit ScopedCpuTimer(FrameTimings::Avg& slot)
        : slot_(slot), t0_(std::chrono::steady_clock::now()) {}
    ~ScopedCpuTimer() {
        const auto dt = std::chrono::steady_clock::now() - t0_;
        slot_.add(std::chrono::duration<float, std::milli>(dt).count());
    }
    ScopedCpuTimer(const ScopedCpuTimer&)            = delete;
    ScopedCpuTimer& operator=(const ScopedCpuTimer&) = delete;
private:
    FrameTimings::Avg&                                 slot_;
    std::chrono::steady_clock::time_point              t0_;
};
