#include "UIPanelPass.hpp"
#include "../MipChain.hpp"
#include "../RendererLog.hpp"
#include "../../ui/slate/HSlateRenderer.hpp"
#include "../../ui/slate/HSlateContext.hpp"
#include <algorithm>

// ── 핸들 ──────────────────────────────────────────────────────────────────────
const UIPanelPass::Target* UIPanelPass::get(Handle h) const {
    const uint32_t slot = h & 0xFFFFu;
    if (slot == 0 || slot > targets_.size()) return nullptr;
    const Target& t = targets_[slot - 1];
    return (t.live && t.gen == (uint16_t)(h >> 16)) ? &t : nullptr;
}

std::vector<UIPanelPass::Handle> UIPanelPass::liveHandles() const {
    std::vector<Handle> out;
    for (size_t i = 0; i < targets_.size(); ++i)
        if (targets_[i].live) out.push_back(makeHandle(i, targets_[i].gen));
    return out;
}
UIPanelPass::Target* UIPanelPass::get(Handle h) {
    return const_cast<Target*>(static_cast<const UIPanelPass*>(this)->get(h));
}

VkImageView UIPanelPass::view(Handle h) const {
    const Target* t = get(h);
    if (!t) return VK_NULL_HANDLE;
    return (t->drawn || t->queued) ? t->sampleView : VK_NULL_HANDLE;
}
VkExtent2D UIPanelPass::extent(Handle h) const {
    const Target* t = get(h);
    return t ? t->extent : VkExtent2D{};
}
uint32_t UIPanelPass::mipLevels(Handle h) const {
    const Target* t = get(h);
    return t ? t->mips : 0u;
}
uint32_t UIPanelPass::liveCount() const {
    return (uint32_t)std::count_if(targets_.begin(), targets_.end(), [](const Target& t) { return t.live; });
}

bool UIPanelPass::canBlitMips() {
    if (canBlit_ < 0) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(rc_->physDev, HSlateRenderer::kPanelFormat, &fp);
        canBlit_ = hmip::canBlitMips(fp.optimalTilingFeatures) ? 1 : 0;
        if (!canBlit_)
            HLOG(LogRenderer, Warning) << "UI panel: format " << (int)HSlateRenderer::kPanelFormat
                                       << " cannot be blitted with linear filtering; panels have no mipmaps";
    }
    return canBlit_ == 1;
}

// ── 만들기 · 지우기 ───────────────────────────────────────────────────────────
UIPanelPass::Handle UIPanelPass::create(HSlateRenderer& ui, uint32_t width, uint32_t height) {
    if (!rc_ || ui.panelRenderPass() == VK_NULL_HANDLE) return 0;
    const uint32_t w = std::clamp(width,  1u, kMaxSide);
    const uint32_t h = std::clamp(height, 1u, kMaxSide);

    Target t;
    t.extent = { w, h };
    t.mips   = canBlitMips() ? hmip::levelCount(w, h) : 1u;
    try {
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        t.image = rc_->createImage(w, h, t.mips, 1, VK_SAMPLE_COUNT_1_BIT, HSlateRenderer::kPanelFormat,
                                   VK_IMAGE_TILING_OPTIMAL, usage, 0, t.alloc);
        t.sampleView = rc_->createImageView(t.image, HSlateRenderer::kPanelFormat, VK_IMAGE_ASPECT_COLOR_BIT, t.mips);
        t.rtView     = rc_->createImageView(t.image, HSlateRenderer::kPanelFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        VkFramebufferCreateInfo fci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass      = ui.panelRenderPass();
        fci.attachmentCount = 1;
        fci.pAttachments    = &t.rtView;
        fci.width           = w;
        fci.height          = h;
        fci.layers          = 1;
        if (vkCreateFramebuffer(rc_->device, &fci, nullptr, &t.fb) != VK_SUCCESS)
            throw std::runtime_error("vkCreateFramebuffer failed");
    } catch (const std::exception& ex) {
        HLOG(LogRenderer, Error) << "UI panel texture " << w << "x" << h << " could not be created: " << ex.what();
        destroyTarget(t);
        return 0;
    }
    t.live = true;

    for (size_t i = 0; i < targets_.size(); ++i)
        if (!targets_[i].live) {
            t.gen = (uint16_t)(targets_[i].gen + 1);   // 옛 핸들을 무효로
            targets_[i] = t;
            return makeHandle(i, t.gen);
        }
    if (targets_.size() >= 0xFFFFu) { destroyTarget(t); return 0; }
    targets_.push_back(t);
    return makeHandle(targets_.size() - 1, t.gen);
}

void UIPanelPass::freeVulkan(Target& t) {
    if (!rc_) return;
    if (t.fb)         vkDestroyFramebuffer(rc_->device, t.fb, nullptr);
    if (t.rtView)     vkDestroyImageView(rc_->device, t.rtView, nullptr);
    if (t.sampleView) vkDestroyImageView(rc_->device, t.sampleView, nullptr);
    if (t.image)      vkDestroyImage(rc_->device, t.image, nullptr);
    if (t.alloc.isValid()) rc_->allocator->freeB(t.alloc);
    t.fb = VK_NULL_HANDLE; t.rtView = t.sampleView = VK_NULL_HANDLE; t.image = VK_NULL_HANDLE;
    t.alloc = HarukaAllocation{};
}

void UIPanelPass::destroyTarget(Target& t) {
    freeVulkan(t);
    const uint16_t gen = t.gen;   // 세대는 칸에 남깁니다(다음에 이 칸을 쓰면 하나 올림)
    t = Target{};
    t.gen = gen;
}

void UIPanelPass::release(Handle h) {
    Target* t = get(h);
    if (!t) return;
    // Vulkan 객체는 대기열로 옮기고 칸만 비웁니다. 이번 프레임(frame_)까지 이 텍스처를 읽었을 수 있습니다.
    Target gone = *t;
    gone.list = HSlateDrawList{};
    retired_.retire(std::move(gone), frame_);
    const uint16_t gen = t->gen;
    *t = Target{};
    t->gen = gen;
}

void UIPanelPass::beginFrame(uint64_t submitNum) {
    frame_ = submitNum;
    retired_.collect(submitNum, [this](Target& t) { freeVulkan(t); });
}

void UIPanelPass::destroy() {
    retired_.drain([this](Target& t) { freeVulkan(t); });
    for (Target& t : targets_) if (t.live) destroyTarget(t);
    targets_.clear();
}

// ── 그리기 ────────────────────────────────────────────────────────────────────
void UIPanelPass::requestRedraw(Handle h) {
    if (Target* t = get(h)) t->forced = true;
}

// 그리기 목록을 만들어(CPU) 해시를 직전에 그린 것과 비교하고, 다르거나 강제 표시가 있을 때만 이번 프레임의 record 에
// 넘깁니다. Attr 바인딩 글자 · 색처럼 markDirty 없이 바뀌는 내용도 이렇게 따라갑니다(3D UI 구간 2).
bool UIPanelPass::update(Handle h, HSlateRenderer& ui, HSlateContext& ctx) {
    Target* t = get(h);
    if (!t) return false;
    ui.buildPanel(ctx, t->extent, t->list);
    const uint64_t hash = drawListHash(t->list);
    if (t->drawn && !t->forced && hash == t->hash) {
        ++stats_.skipped;
        return false;
    }
    t->queued = true;
    t->forced = false;
    t->hash   = hash;
    return true;
}

bool UIPanelPass::hasQueued() const {
    return std::any_of(targets_.begin(), targets_.end(), [](const Target& t) { return t.live && t.queued; });
}

void UIPanelPass::record(VkCommandBuffer cmd, HSlateRenderer& ui, uint32_t frameIdx) {
    for (Target& t : targets_) {
        if (!t.live || !t.queued) continue;
        const bool complete = ui.recordPanel(cmd, t.list, t.fb, t.extent, frameIdx);   // 끝나면 0단계 = TRANSFER_SRC
        recordMips(cmd, t);                                                           // 끝나면 전 단계 = SHADER_READ_ONLY
        t.drawn  = true;
        t.queued = false;
        if (!complete) t.forced = true;   // 정점 예산 때문에 지우기만 했으면 다음 프레임에 다시
        ++stats_.redrawn;
    }
}

// 0단계(렌더 패스가 TRANSFER_SRC 로 남김)에서 한 단계씩 블릿으로 줄이고, 끝에 모든 단계를 셰이더가 읽는 배치로 옮깁니다.
// 1단계 이상은 매번 새로 쓰므로 UNDEFINED 에서 옮깁니다(이전 내용을 버림). 앞 프레임의 조각 셰이더 읽기가 끝난 뒤에
// 쓰도록 그 단계를 앞쪽 범위에 넣습니다.
void UIPanelPass::recordMips(VkCommandBuffer cmd, const Target& t) const {
    auto barrier = [&](uint32_t baseLevel, uint32_t levelCount, VkImageLayout oldL, VkImageLayout newL,
                       VkPipelineStageFlags2 srcS, VkAccessFlags2 srcA,
                       VkPipelineStageFlags2 dstS, VkAccessFlags2 dstA) {
        VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        b.srcStageMask        = srcS;  b.srcAccessMask = srcA;
        b.dstStageMask        = dstS;  b.dstAccessMask = dstA;
        b.oldLayout           = oldL;  b.newLayout     = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = t.image;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, baseLevel, levelCount, 0, 1 };
        VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers    = &b;
        rc_->pipelineBarrier(cmd, di);
    };

    if (t.mips > 1)
        barrier(1, t.mips - 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    for (uint32_t i = 1; i < t.mips; ++i) {
        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
        blit.srcOffsets[1]  = { (int32_t)hmip::extent(t.extent.width, i - 1), (int32_t)hmip::extent(t.extent.height, i - 1), 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
        blit.dstOffsets[1]  = { (int32_t)hmip::extent(t.extent.width, i), (int32_t)hmip::extent(t.extent.height, i), 1 };
        vkCmdBlitImage(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        // 방금 쓴 단계를 다음 블릿이 읽도록(마지막 단계도 아래 일괄 전환을 위해 같은 배치로 맞춤).
        barrier(i, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    }

    // 모든 단계: TRANSFER_SRC → SHADER_READ_ONLY. 0단계의 쓰기는 패널 렌더 패스의 나가는 의존성이 전송 · 조각 셰이더
    // 단계에 보이게 했고, 1단계 이상의 쓰기는 위 배리어가 보이게 했습니다. 여기서는 블릿 읽기가 끝난 뒤 옮깁니다.
    barrier(0, t.mips, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
