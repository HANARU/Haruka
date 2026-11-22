#pragma once
#include "Renderer.hpp"
#include "GlfwSwapchain.hpp"
#include "../ui/slate/HSlateContext.hpp"
#include <array>
#include <memory>
#include <vector>

// 보조 OS 창 출력 하나(다중 OS 창 M2, 2026-11-21). 렌더러 안에서만 씁니다(Renderer_window.cpp 가 만들고 · 없애고,
// Renderer_render.cpp 의 제출이 세마포어를 읽음). 바깥에는 번호(id)로만 보입니다.
struct WindowTarget {
    uint32_t                       id      = 0;
    IWindow*                       win     = nullptr;
    VkSurfaceKHR                   surface = VK_NULL_HANDLE;
    std::unique_ptr<GlfwSwapchain> swap;
    bool                           created = false;   // 스왑체인을 한 번이라도 만들었음(만들 때 넓이가 없으면 뒤로 미룸)
    bool                           failed  = false;   // 복구할 수 없는 오류(형식 불일치 · 표면 잃음 등) — 이 창은 더 그리지 않음
    std::array<VkSemaphore, MAX_FRAMES> imageAvail{};   // 슬롯별 acquire 세마포어
    std::vector<VkSemaphore>       renderDone;         // 이미지별 표시 세마포어
    std::vector<VkFence>           imagesInFlight;     // 이미지별로 마지막에 쓴 프레임 펜스
    std::vector<VkFramebuffer>     framebuffers;       // 이미지별 UI 프레임버퍼(HSlateRenderer::windowRenderPass)
    HSlateContext*                 ui = nullptr;
    HSlateDrawList                 list;               // 이번 프레임 목록(prepare 가 만들고 record 가 씀)
    uint64_t                       hash     = 0;       // 이번 프레임 목록의 해시
    uint64_t                       lastHash = 0;       // 마지막으로 표시한 목록의 해시
    bool                           drawn    = false;   // 지금 스왑체인에 그려 표시한 적이 있음
    bool                           forced   = true;    // 해시가 같아도 다음에 그림
    bool                           acquired = false;   // 이번 프레임에 이미지를 받음
    uint32_t                       imgIdx   = 0;
    Renderer::WindowTargetStats    stats;
};
