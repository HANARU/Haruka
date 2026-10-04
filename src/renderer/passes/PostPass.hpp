#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "HPass.hpp"
#include "../RenderContext.hpp"

// ── PostPass ──────────────────────────────────────────────────────────────────
// ACES 톤맵 + bloom/SSAO 합성 → 스왑체인 이미지.
// 가짜 TAA(히스토리 이미지·blit)는 2026-10-04 에 지웠습니다(DEF-15). 진짜 TAA 나 Quest 의
// 프레임 합성(AppSW)은 모션 벡터와 눈별 이전 행렬이 필요해 모양이 달라서, 그때 새로 만듭니다.
class PostPass : public HPass {
public:
    void create(RenderContext& rc);   // pass + 레이아웃 (크기 무관)
    void destroy() override;
    void releaseSizeDependent() override;                      // framebuf
    void acquire(const std::vector<VkImageView>& swapViews);   // framebuf
    void rebuildPipelines() override;

    void recordTonemap(VkCommandBuffer cmd, uint32_t imgIdx,
                       VkImageView hdrView, VkImageView ssaoView,
                       VkImageView bloomView, VkSampler clampSampler);

private:
    RenderContext* rc_ = nullptr;

    VkRenderPass               pass_       = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebufs_;   // 스왑체인 이미지당 1개
    VkDescriptorSetLayout      setLayout_  = VK_NULL_HANDLE;
    VkPipelineLayout           pipeLayout_ = VK_NULL_HANDLE;
    VkPipeline                 pipeline_   = VK_NULL_HANDLE;
};
