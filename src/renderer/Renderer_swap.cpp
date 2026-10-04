#include "Renderer.hpp"
#include "../core/FrameTimings.hpp"
#include "../ui/slate/HSlateRenderer.hpp"
#include "RendererLog.hpp"
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
}




// ── cleanupSwapChain / recreateSwapChain ──────────────────────────────────────

void Renderer::cleanupSwapChain() {
    // Bloom (size-dependent only — pass/PSO/layout은 각 패스 destroy()가 정리)
    bloom_.releaseSizeDependent();

    // AO (linDepth + SSAO)
    ao_.releaseSizeDependent();

    // Geometry (메인 프레임버퍼 + MSAA + depth + HDR)
    geometry_.releaseSizeDependent();

    // Post framebuffers
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
    // 창 전체 뷰포트로 리셋합니다. 에디터가 다음 프레임 setViewportRect 로 다시
    // 뷰포트를 지정하면 그때 renderExtent 타깃이 재생성됩니다.
    rc_.viewportOffset = { 0, 0 };
    rc_.viewportExtent = rc_.extent;
    rc_.renderExtent   = desiredRenderExtent();

    geometry_.acquireSizeDependent();
    ao_.acquireSizeDependent();
    bloom_.acquireSizeDependent();
    post_.acquire(swapchain_->imageViews());

    frames_.onSwapchainRecreated((uint32_t)swapchain_->imageCount());

    if (uiRenderer_)
        uiRenderer_->rebuildFramebuffers(swapchain_->extent(), swapchain_->imageViews());
}

void Renderer::recreateRenderTargets() {
    // 스왑체인 재생성과 같은 카운터로 셉니다. 비용(장치 대기·재할당)이 같고 어떤 GPU
    // 타임스탬프에도 잡히지 않는 점도 같습니다. 예전에는 세지 않아, 뷰포트 크기 변경이
    // 매 프레임 재생성을 일으켜도 통계에 0 으로 보였습니다(DEF-68).
    ++gFrameTimings().recreateCount;
    vkDeviceWaitIdle(device);
    // 씬 타깃만 재생성합니다(스왑체인·UI 는 그대로). post 프레임버퍼는 스왑체인 뷰를 다시 가리킵니다.
    geometry_.releaseSizeDependent();
    ao_.releaseSizeDependent();
    bloom_.releaseSizeDependent();
    post_.releaseSizeDependent();

    geometry_.acquireSizeDependent();
    ao_.acquireSizeDependent();
    bloom_.acquireSizeDependent();
    post_.acquire(swapchain_->imageViews());
}

VkExtent2D Renderer::desiredRenderExtent() const {
    // XR 이 붙어 있으면 런타임이 권하는 눈 크기로 그립니다. 스테레오 흉내 모드는 뷰포트를 좌우로 나눠
    // 눈 하나가 절반을 씁니다. 폭은 최소 1.
    if (xrActive_)
        return xrEyeExtent_;
    if (emulateStereo_)
        return { std::max(1u, rc_.viewportExtent.width / 2), rc_.viewportExtent.height };
    return rc_.viewportExtent;
}

void Renderer::setViewportRect(int x, int y, int w, int h) {
    // 스왑체인 범위로 클램프합니다. 폭/높이는 최소 1.
    const int sw = (int)rc_.extent.width, sh = (int)rc_.extent.height;
    int cx = std::clamp(x, 0, std::max(0, sw - 1));
    int cy = std::clamp(y, 0, std::max(0, sh - 1));
    int cw = std::clamp(w, 1, sw - cx);
    int ch = std::clamp(h, 1, sh - cy);

    // 오프셋은 합성할 때마다 읽어 위치로만 씁니다. 눈 하나의 렌더 크기가 같으면 여기서 끝냅니다.
    // 예전에는 오프셋만 달라져도 씬 타깃을 전부 다시 만들어, 스플리터를 끄는 동안 프레임마다
    // vkDeviceWaitIdle 이 일어났습니다(DEF-59).
    rc_.viewportOffset = { cx, cy };
    rc_.viewportExtent = { (uint32_t)cw, (uint32_t)ch };
    const VkExtent2D newExt = desiredRenderExtent();
    if (newExt.width  == rc_.renderExtent.width &&
        newExt.height == rc_.renderExtent.height)
        return;

    rc_.renderExtent = newExt;
    recreateRenderTargets();
}

void Renderer::attachXrSwapchain(const std::vector<VkImage>& images, VkFormat format,
                                 VkExtent2D eyeExtent) {
    detachXrSwapchain(false);
    // 톤매핑 패스가 이 포맷의 렌더 패스를 준비합니다(데스크톱과 같으면 그대로 씀).
    const VkRenderPass rp = post_.renderPassFor(format);
    for (VkImage img : images) {
        for (uint32_t e = 0; e < kMaxViews; ++e) {
            VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vi.image            = img;
            vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;   // 눈 하나 = 배열의 층 하나
            vi.format           = format;
            vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, e, 1 };
            VkImageView view = VK_NULL_HANDLE;
            if (vkCreateImageView(device, &vi, nullptr, &view) != VK_SUCCESS)
                throw std::runtime_error("Failed to create XR swapchain image view!");
            xrViews_.push_back(view);

            VkFramebufferCreateInfo fci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fci.renderPass      = rp;
            fci.attachmentCount = 1;
            fci.pAttachments    = &view;
            fci.width           = eyeExtent.width;
            fci.height          = eyeExtent.height;
            fci.layers          = 1;
            VkFramebuffer fb = VK_NULL_HANDLE;
            if (vkCreateFramebuffer(device, &fci, nullptr, &fb) != VK_SUCCESS)
                throw std::runtime_error("Failed to create XR framebuffer!");
            xrFramebufs_.push_back(fb);
        }
    }
    xrFormat_    = format;
    xrEyeExtent_ = eyeExtent;
    xrActive_    = true;
    HLOG(LogRenderer, Log) << "XR output attached: " << images.size() << " images, "
                           << eyeExtent.width << "x" << eyeExtent.height << " per eye";
    const VkExtent2D newExt = desiredRenderExtent();
    if (newExt.width != rc_.renderExtent.width || newExt.height != rc_.renderExtent.height) {
        rc_.renderExtent = newExt;
        recreateRenderTargets();
    }
}

void Renderer::detachXrSwapchain(bool restoreTargets) {
    if (!xrActive_ && xrViews_.empty()) return;
    vkDeviceWaitIdle(device);
    for (VkFramebuffer fb : xrFramebufs_) vkDestroyFramebuffer(device, fb, nullptr);
    for (VkImageView v : xrViews_)        vkDestroyImageView(device, v, nullptr);
    xrFramebufs_.clear();
    xrViews_.clear();
    xrActive_ = false;
    HLOG(LogRenderer, Log) << "XR output detached";
    if (!restoreTargets) return;
    const VkExtent2D newExt = desiredRenderExtent();
    if (newExt.width != rc_.renderExtent.width || newExt.height != rc_.renderExtent.height) {
        rc_.renderExtent = newExt;
        recreateRenderTargets();
    }
}

void Renderer::setStereoEmulation(bool on) {
    if (emulateStereo_ == on) return;
    emulateStereo_ = on;
    HLOG(LogRenderer, Log) << "Stereo emulation " << (on ? "on" : "off")
                           << " (two parallel eyes, 64 mm apart, side by side in the viewport)";
    const VkExtent2D newExt = desiredRenderExtent();
    if (newExt.width == rc_.renderExtent.width && newExt.height == rc_.renderExtent.height)
        return;
    rc_.renderExtent = newExt;
    recreateRenderTargets();
}
