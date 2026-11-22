#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

// 보조 OS 창 출력(다중 OS 창 M2, 2026-11-21, Docs/Editor_MultiWindow_Design.md D2 · D3)의 순수 판정. Vulkan 호출 없이
// 결과 코드 · 상태만 보므로 단위 시험(window_target)이 그대로 확인합니다. 렌더러(Renderer_window.cpp)가 프레임마다 씁니다.
namespace hwin {

// 이번 프레임에 보조 창 하나를 어떻게 할지.
enum class FramePlan : uint8_t {
    SkipNoArea,      // 최소화(표면 넓이 0) — acquire 하지 않음(DEF-106 규칙)
    SkipUnchanged,   // 그리기 목록 해시가 직전에 표시한 것과 같고 다시 그릴 이유가 없음 — acquire · 기록 · 표시 모두 건너뜀
    Draw,            // acquire 를 시도하고, 받으면 기록 · 표시
};

// hasArea: 표면 넓이가 있음. drawn: 이 스왑체인에 한 번이라도 그려 표시했음. forced: 다시 만들었거나 표시가 실패했거나
// 예산 때문에 지우기만 했음(다음에 반드시 그림). 해시가 같아도 처음이거나 강제면 그립니다.
inline FramePlan planFrame(bool hasArea, bool drawn, bool forced, uint64_t hash, uint64_t lastHash) {
    if (!hasArea) return FramePlan::SkipNoArea;
    if (drawn && !forced && hash == lastHash) return FramePlan::SkipUnchanged;
    return FramePlan::Draw;
}

// 데스크톱 프레임 하나를 어떻게 그릴지(M3a). 메인 창이 최소화되면(표면 넓이 0) 예전에는 XR 프레임이 아닌 한 drawFrame 이 아무것도
// 하지 않았습니다(DEF-106). 떠 있는 창이 있으면 그 창들은 계속 보여야 하므로, 메인 · 3D 는 건너뛰고 보조 창 UI 만 기록 · 제출 ·
// 표시하는 프레임을 둡니다. 보조 창이 없거나 모두 그릴 수 없으면(최소화 · 멈춤 · 문맥 없음) 예전과 같이 아무것도 하지 않습니다.
enum class DesktopFrame : uint8_t {
    Full,      // 예전 경로(메인 창 또는 XR 눈을 그림, 보조 창은 그 프레임에 함께)
    AuxOnly,   // 메인 최소화 · XR 아님 · 그릴 수 있는 보조 창이 있음 — 보조 창 UI 만
    Nothing,   // 그릴 것 없음(예전의 이른 반환)
};

// desktop: 메인 스왑체인 표면에 넓이가 있음. xrFrame: 이번 drawFrame 이 XR(또는 합성) 눈을 그림. auxDrawable: 보조 창 가운데
// 하나라도 멈추지 않았고 · 문맥이 있고 · 넓이가 있음.
inline DesktopFrame chooseDesktopFrame(bool desktop, bool xrFrame, bool auxDrawable) {
    if (desktop || xrFrame) return DesktopFrame::Full;
    return auxDrawable ? DesktopFrame::AuxOnly : DesktopFrame::Nothing;
}

// 타임아웃 0 acquire 의 결과.
struct AcquireOutcome {
    bool acquired = false;   // 이미지를 받았고 세마포어가 신호됨 → 이번 제출이 반드시 기다리고 표시해야 함
    bool recreate = false;   // 스왑체인을 다시 만들어야 함(SUBOPTIMAL 은 받은 뒤 표시까지 하고 다시 만듦)
    bool notReady = false;   // 준비된 이미지가 없음(이번 프레임만 건너뜀, 세마포어는 신호되지 않음)
    bool failed   = false;   // 그 밖의 오류(표면 잃음 등) — 그 창만 멈추고 메인은 계속
};

inline AcquireOutcome classifyAcquire(VkResult r) {
    AcquireOutcome o;
    switch (r) {
    case VK_SUCCESS:               o.acquired = true; break;
    case VK_SUBOPTIMAL_KHR:        o.acquired = true; o.recreate = true; break;
    case VK_NOT_READY:
    case VK_TIMEOUT:               o.notReady = true; break;
    case VK_ERROR_OUT_OF_DATE_KHR: o.recreate = true; break;
    default:                       o.failed = true; break;
    }
    return o;
}

// 표시 결과. OUT_OF_DATE · SUBOPTIMAL 은 그 창만 다시 만들게 표시하고, 다른 오류는 그 창을 멈춥니다(메인 표시와 따로, D2).
struct PresentOutcome {
    bool shown    = false;   // 표시 요청이 받아들여짐(SUBOPTIMAL 포함)
    bool recreate = false;
    bool failed   = false;
};

inline PresentOutcome classifyPresent(VkResult r) {
    PresentOutcome o;
    switch (r) {
    case VK_SUCCESS:               o.shown = true; break;
    case VK_SUBOPTIMAL_KHR:        o.shown = true; o.recreate = true; break;
    case VK_ERROR_OUT_OF_DATE_KHR: o.recreate = true; break;
    default:                       o.failed = true; break;
    }
    return o;
}

// 제출 한 번의 대기 · 신호 세마포어 목록. 메인 창은 데스크톱을 그린 프레임에만, 보조 창은 이번 프레임에 acquire 한 것만
// 넣습니다. 받지 못한 창의 세마포어는 신호되지 않았으므로 넣으면 제출이 영원히 기다립니다.
struct SubmitSemaphores {
    std::vector<VkSemaphore>          wait;
    std::vector<VkPipelineStageFlags> stages;
    std::vector<VkSemaphore>          signal;
    void clear() { wait.clear(); stages.clear(); signal.clear(); }
    // acquired 가 거짓이면 아무것도 넣지 않습니다.
    void add(bool acquired, VkSemaphore imageAvailable, VkSemaphore renderDone) {
        if (!acquired) return;
        wait.push_back(imageAvailable);
        stages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        signal.push_back(renderDone);
    }
};

}  // namespace hwin
