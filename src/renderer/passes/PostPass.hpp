#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "HPass.hpp"
#include "../RenderContext.hpp"

// ── PostPass ──────────────────────────────────────────────────────────────────
// ACES 톤맵 + TAA resolve + bloom/SSAO 합성 → 스왑체인 이미지.
// TAA 히스토리 이미지(스왑체인 크기)와 히스토리 blit도 여기서 소유한다.
// 타임스탬프 의미("post 이후" vs "UI 포함") 보존을 위해
// recordTonemap / recordHistoryBlit 두 단계로 분리되어 있다.
class PostPass : public HPass {
public:
    void create(RenderContext& rc);   // pass + 레이아웃 (크기 무관)
    void destroy() override;
    void releaseSizeDependent() override;                      // framebuf + taaHist
    void acquire(const std::vector<VkImageView>& swapViews);   // framebuf + taaHist
    void rebuildPipelines() override;

    void recordTonemap(VkCommandBuffer cmd, uint32_t imgIdx,
                       VkImageView hdrView, VkImageView ssaoView,
                       VkImageView bloomView, VkSampler clampSampler,
                       bool taaEnabled);
    // hdr → taaHist 복사 (post 이후, UI 전에 호출)
    void recordHistoryBlit(VkCommandBuffer cmd, VkImage hdrImage);

private:
    RenderContext* rc_ = nullptr;

    VkRenderPass               pass_       = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebufs_;   // 스왑체인 이미지당 1개
    VkDescriptorSetLayout      setLayout_  = VK_NULL_HANDLE;
    VkPipelineLayout           pipeLayout_ = VK_NULL_HANDLE;
    VkPipeline                 pipeline_   = VK_NULL_HANDLE;

    // TAA history (단일 영속 이미지 — 프레임 인플라이트와 무관)
    VkImage          taaHistImage_ = VK_NULL_HANDLE;
    VkImageView      taaHistView_  = VK_NULL_HANDLE;
    HarukaAllocation taaHistAlloc_;
    bool             taaHistValid_ = false;
};
