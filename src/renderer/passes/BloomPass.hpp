#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include "HPass.hpp"
#include "../RenderContext.hpp"

constexpr int kBloomLevels = 5;

// ── BloomPass ─────────────────────────────────────────────────────────────────
// HDR → threshold → 다운샘플 체인(1/2 … 1/32) → 업샘플 누적 체인.
// 결과는 resultView()(half-res)로 PostPass가 합성한다.
class BloomPass : public HPass {
public:
    void create(RenderContext& rc);     // 크기 의존 자원 + 패스/레이아웃 + PSO
    void destroy() override;
    void releaseSizeDependent() override;
    void acquireSizeDependent() override;
    void rebuildPipelines() override;

    void record(VkCommandBuffer cmd, VkImageView hdrView, VkSampler clampSampler);

    VkImageView resultView() const { return upViews_[kBloomLevels - 2]; }

private:
    void createPipelines();
    void destroyPipelines();

    RenderContext* rc_ = nullptr;

    // down[0..N-1]: half, quarter, ..., 1/32 — up[0..N-2]: 작은 것부터 누적
    VkImage          downImages_[kBloomLevels]   = {};
    VkImageView      downViews_[kBloomLevels]    = {};
    HarukaAllocation downAllocs_[kBloomLevels]   = {};
    VkImage          upImages_[kBloomLevels - 1] = {};
    VkImageView      upViews_[kBloomLevels - 1]  = {};
    HarukaAllocation upAllocs_[kBloomLevels - 1] = {};
    VkExtent2D       extents_[kBloomLevels]      = {};

    VkRenderPass          pass_       = VK_NULL_HANDLE;
    std::array<VkFramebuffer, kBloomLevels + (kBloomLevels - 1)> framebufs_{};
    VkDescriptorSetLayout setLayout_  = VK_NULL_HANDLE;
    VkPipelineLayout      pipeLayout_ = VK_NULL_HANDLE;
    VkPipeline            threshPipe_ = VK_NULL_HANDLE;
    VkPipeline            downPipe_   = VK_NULL_HANDLE;
    VkPipeline            upPipe_     = VK_NULL_HANDLE;
};
