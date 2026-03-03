#include "Renderer.hpp"
#include "../core/FrameTimings.hpp"
#include "../ui/slate/HSlateRenderer.hpp"
#include <stdexcept>
#include <algorithm>
#include <limits>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <array>
#include <cstring>

// ── Depth format ──────────────────────────────────────────────────────────────

VkFormat Renderer::findDepthFormat() {
    for (auto fmt : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }) {
        VkFormatProperties p;
        vkGetPhysicalDeviceFormatProperties(physDevice, fmt, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
    throw std::runtime_error("No supported depth format!");
}


// ── Pipeline creation helpers ──────────────────────────────────────────────────

// ── All pipelines ─────────────────────────────────────────────────────────────

void Renderer::createAllPipelines() {
    shadow_.rebuildPipelines();
    geometry_.rebuildPipelines();
    ao_.rebuildPipelines();
    bloom_.rebuildPipelines();
    post_.rebuildPipelines();
    cull_.rebuildPipelines();
}




// ── cleanupSwapChain / recreateSwapChain ──────────────────────────────────────

void Renderer::cleanupSwapChain() {
    // Bloom (size-dependent only — pass/PSO/layout은 각 패스 destroy()가 정리)
    bloom_.releaseSizeDependent();

    // AO (linDepth + SSAO)
    ao_.releaseSizeDependent();

    // Geometry (메인 프레임버퍼 + MSAA + depth + HDR)
    geometry_.releaseSizeDependent();

    // Post framebuffers + TAA history
    post_.releaseSizeDependent();

    swapchain_->destroy();
}

void Renderer::recreateSwapChain() {
    // 이 함수가 매 프레임 도는지가 프레임 시간 조사의 1 순위 가설입니다.
    // vkDeviceWaitIdle 과 리소스 전량 재생성이라 어떤 GPU 타임스탬프에도 잡히지 않습니다.
    ++gFrameTimings().recreateCount;
    vkDeviceWaitIdle(device);
    cleanupSwapChain();   // destroys dependent renderer resources + swapchain
    swapchain_->recreate(); // waits for non-zero extent, then rebuilds swapchain

    rc_.extent     = swapchain_->extent();
    rc_.swapFormat = swapchain_->format();
    // 창 전체 renderExtent 로 리셋합니다. 에디터가 다음 프레임 setViewportRect 로 다시
    // 뷰포트를 지정하면 그때 renderExtent 타깃이 재생성됩니다.
    rc_.renderExtent   = rc_.extent;
    rc_.viewportOffset = { 0, 0 };

    geometry_.acquireSizeDependent();
    ao_.acquireSizeDependent();
    bloom_.acquireSizeDependent();
    post_.acquire(swapchain_->imageViews());

    frames_.onSwapchainRecreated((uint32_t)swapchain_->imageCount());

    if (uiRenderer_)
        uiRenderer_->rebuildFramebuffers(swapchain_->extent(), swapchain_->imageViews());
}

void Renderer::recreateRenderTargets() {
    vkDeviceWaitIdle(device);
    // 씬 타깃만 재생성합니다(스왑체인·UI 는 그대로). post 는 프레임버퍼가 스왑체인 뷰를
    // 다시 가리키지만 renderExtent 가 바뀐 TAA 히스토리는 새로 만듭니다.
    geometry_.releaseSizeDependent();
    ao_.releaseSizeDependent();
    bloom_.releaseSizeDependent();
    post_.releaseSizeDependent();

    geometry_.acquireSizeDependent();
    ao_.acquireSizeDependent();
    bloom_.acquireSizeDependent();
    post_.acquire(swapchain_->imageViews());
}

void Renderer::setViewportRect(int x, int y, int w, int h) {
    // 스왑체인 범위로 클램프합니다. 폭/높이는 최소 1.
    const int sw = (int)rc_.extent.width, sh = (int)rc_.extent.height;
    int cx = std::clamp(x, 0, std::max(0, sw - 1));
    int cy = std::clamp(y, 0, std::max(0, sh - 1));
    int cw = std::clamp(w, 1, sw - cx);
    int ch = std::clamp(h, 1, sh - cy);

    const VkExtent2D newExt{ (uint32_t)cw, (uint32_t)ch };
    const VkOffset2D newOff{ cx, cy };
    if (newExt.width  == rc_.renderExtent.width  &&
        newExt.height == rc_.renderExtent.height &&
        newOff.x == rc_.viewportOffset.x && newOff.y == rc_.viewportOffset.y)
        return;   // 변화 없음 — 재생성 안 함

    rc_.renderExtent   = newExt;
    rc_.viewportOffset = newOff;
    recreateRenderTargets();
}
