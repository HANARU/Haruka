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

// 기본 패스(프레임 순서) → 렌더 확장(등록 순서). 셰이더 핫 리로드도 이 함수를 부릅니다(DEF-17).
void Renderer::createAllPipelines() {
    passRegistry_.forEachForward([](HPass& p) { p.rebuildPipelines(); });
    pipelineCacheDirty_ = true;   // 다음 프레임 끝에서 캐시 저장(DEF-153)
}




// ── cleanupSwapChain / recreateSwapChain ──────────────────────────────────────

void Renderer::cleanupSwapChain() {
    // 크기 의존 자원만 놓습니다(pass/PSO/layout 은 각 패스 destroy() 가 정리). 확장 → 기본 패스
    // 역순입니다(DEF-17). shadow 는 크기와 무관해 할 일이 없습니다.
    passRegistry_.forEachReverse([](HPass& p) { p.releaseSizeDependent(); });

    swapchain_->destroy();
}

void Renderer::recreateSwapChain() {
    // 최소화 중(표면 크기 0)에는 만들 수 없습니다. 다시 만들 일은 표시해 둔 채 남아, 복원된 뒤 첫
    // 프레임이 합니다(DEF-106).
    if (!swapchain_->hasArea()) return;
    // 이 함수가 매 프레임 도는지가 프레임 시간 조사의 1 순위 가설입니다.
    // vkDeviceWaitIdle 과 리소스 전량 재생성이라 어떤 GPU 타임스탬프에도 잡히지 않습니다.
    ++gFrameTimings().recreateCount;
    vkDeviceWaitIdle(device);
    cleanupSwapChain();   // destroys dependent renderer resources + swapchain
    swapchain_->recreate();

    rc_.extent     = swapchain_->extent();
    rc_.swapFormat = swapchain_->format();
    // 창 전체 뷰포트로 리셋합니다. 에디터가 다음 프레임 setViewportRect 로 다시
    // 뷰포트를 지정하면 그때 renderExtent 타깃이 재생성됩니다.
    rc_.viewportOffset = { 0, 0 };
    rc_.viewportExtent = rc_.extent;
    rc_.renderExtent   = desiredRenderExtent();

    // 기본 패스(프레임 순서) → 렌더 확장. post 는 스왑체인 이미지 뷰가 필요해 따로 붙입니다.
    passRegistry_.forEachForward([](HPass& p) { p.acquireSizeDependent(); });
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
    passRegistry_.forEachReverse([](HPass& p) { p.releaseSizeDependent(); });
    passRegistry_.forEachForward([](HPass& p) { p.acquireSizeDependent(); });
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
    // 스왑체인 범위로 클램프합니다. 폭/높이는 최소 1. 스왑체인 크기가 0 이면 클램프 범위가 뒤집혀(하한 1 > 상한 0)
    // 미정의 동작이므로 아무것도 하지 않습니다(창 없는 실행은 명목 크기라 평소에는 오지 않음, 2026-10-31).
    const int sw = (int)rc_.extent.width, sh = (int)rc_.extent.height;
    if (sw <= 0 || sh <= 0) return;
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
    pipelineCacheDirty_ = true;   // 이 포맷의 톤매핑 파이프라인을 새로 만들었을 수 있음(DEF-153)
    HLOG(LogRenderer, Log) << "XR output attached: " << images.size() << " images, "
                           << eyeExtent.width << "x" << eyeExtent.height << " per eye";
    const VkExtent2D newExt = desiredRenderExtent();
    if (newExt.width != rc_.renderExtent.width || newExt.height != rc_.renderExtent.height) {
        rc_.renderExtent = newExt;
        recreateRenderTargets();
    }
}

// 창이 없으면 뗀 뒤의 크기 의존 타깃은 명목 크기(64x64)로 작게 다시 만들어집니다. 재연결은 드물어 따로 두지 않습니다.
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

// ── 합성 눈 (--synthetic-eyes, 2026-10-31) ────────────────────────────────────
// 헤드셋 없이 XR 프레임 경로(두 눈 · 배열 층 · 표시 없음)를 돌립니다. Quest 와 같은 경로를 PC 에서 창 없이 그리고, 마지막
// 이미지를 PNG 로 남겨 고치기 전후를 비교합니다. 실제 XR 이미지는 읽기 용도가 없어 이 이미지만 TRANSFER_SRC 를 둡니다.
bool Renderer::attachSyntheticEyes(VkExtent2D eyeExtent, uint32_t count) {
    detachSyntheticEyes();
    // 포맷은 창이 있든 없든 R8G8B8A8_SRGB 로 고정합니다. 캡처가 화소당 4바이트 RGBA 를 전제하고, 창 없는 실행과 창 있는
    // 실행의 PNG 가 같아야 비교할 수 있습니다. 톤매핑 패스는 포맷마다 렌더 패스를 따로 둡니다(Quest 런타임의 선호 포맷과도 같음).
    const VkFormat fmt = VK_FORMAT_R8G8B8A8_SRGB;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(physDevice, fmt, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((fp.optimalTilingFeatures & need) != need) {
        HLOG(LogRenderer, Warning) << "Synthetic eyes: format " << (int)fmt << " cannot be a color attachment and copy source";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        HarukaAllocation alloc;
        synthImages_.push_back(rc_.createImage(eyeExtent.width, eyeExtent.height, 1, kMaxViews, VK_SAMPLE_COUNT_1_BIT, fmt,
                                               VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0,
                                               alloc));
        synthAllocs_.push_back(alloc);
    }
    attachXrSwapchain(synthImages_, fmt, eyeExtent);
    return true;
}

void Renderer::detachSyntheticEyes() {
    if (synthImages_.empty()) return;
    detachXrSwapchain(false);   // 장치가 쉬기를 기다린 뒤 뷰 · 프레임버퍼를 지움
    for (size_t i = 0; i < synthImages_.size(); ++i) {
        vkDestroyImage(device, synthImages_[i], nullptr);
        if (synthAllocs_[i].isValid()) allocator.freeB(synthAllocs_[i]);
    }
    synthImages_.clear();
    synthAllocs_.clear();
}

bool Renderer::captureXrEye(uint32_t image, uint32_t layer, std::vector<uint8_t>& rgba, VkExtent2D& extent) {
    if (image >= synthImages_.size() || layer >= kMaxViews || !xrActive_) return false;
    vkDeviceWaitIdle(device);
    extent = xrEyeExtent_;
    const VkDeviceSize bytes = (VkDeviceSize)extent.width * extent.height * 4;

    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size        = bytes;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buf = VK_NULL_HANDLE;
    if (vkCreateBuffer(device, &bi, nullptr, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device, buf, &reqs);
    HarukaAllocation alloc = allocator.allocB(reqs);
    if (!alloc.isValid() || allocator.bind(buf, alloc) != VK_SUCCESS) {
        vkDestroyBuffer(device, buf, nullptr);
        if (alloc.isValid()) allocator.freeB(alloc);
        return false;
    }

    // 개발용 읽기라 1.0 배리어를 그대로 씁니다(어느 장치에서도 유효). 톤매핑 패스가 남긴 COLOR_ATTACHMENT_OPTIMAL 에서
    // 복사용으로 바꿨다가 되돌리고, 복사 결과가 호스트에 보이도록 HOST 단계까지 묶습니다.
    const VkCommandBuffer cmd = beginOneTimeCmd();
    VkImageMemoryBarrier ib{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image               = synthImages_[image];
    ib.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1 };
    ib.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ib.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ib.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ib.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &ib);
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1 };
    region.imageExtent      = { extent.width, extent.height, 1 };
    vkCmdCopyImageToBuffer(cmd, synthImages_[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);
    ib.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ib.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ib.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ib.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkMemoryBarrier hostRead{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0,
                         1, &hostRead, 0, nullptr, 1, &ib);
    endOneTimeCmd(cmd);

    rgba.resize((size_t)bytes);
    std::memcpy(rgba.data(), alloc.mappedPtr, (size_t)bytes);
    vkDestroyBuffer(device, buf, nullptr);
    allocator.freeB(alloc);

    // BGRA 포맷(창이 있을 때의 스왑체인 포맷)이면 RGBA 로 바꿉니다.
    const VkFormat f = xrFormat_;
    if (f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM)
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
    return true;
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
