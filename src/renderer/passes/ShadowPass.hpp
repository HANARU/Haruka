#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "HPass.hpp"
#include "../RenderContext.hpp"
#include "../RenderTypes.hpp"

// ── ShadowPass ────────────────────────────────────────────────────────────────
// 2048² depth-only 섀도맵. 크기 고정이라 리사이즈와 무관한 영구 자원.
// PSO는 공유 PBR 파이프라인 레이아웃(set 0~3)을 사용한다.
class ShadowPass : public HPass {
public:
    static constexpr uint32_t kResolution = 2048;

    void create(RenderContext& rc, VkPipelineLayout pbrLayout);
    void destroy() override;
    void rebuildPipelines() override;

    // 메인 패스와 공유하는 디스크립터/버퍼 바인딩 입력
    struct DrawInputs {
        VkDescriptorSet frameSet, sceneSet, transformSet;
        VkBuffer        vertexBuffer, indexBuffer;
    };
    // CPU 드로우 리스트로 섀도맵 기록. draws가 비어 있으면 no-op.
    void record(VkCommandBuffer cmd, const DrawInputs& in,
                const std::vector<GpuDrawCmd>& draws);

    bool        valid() const       { return image_ != VK_NULL_HANDLE; }
    VkImageView shadowView() const  { return view_; }
    VkSampler   sampler() const     { return sampler_; }

private:
    RenderContext*   rc_        = nullptr;
    VkPipelineLayout pbrLayout_ = VK_NULL_HANDLE;   // FrameResources 소유 (참조만)

    VkImage          image_       = VK_NULL_HANDLE;
    VkImageView      view_        = VK_NULL_HANDLE;
    HarukaAllocation alloc_;
    VkSampler        sampler_     = VK_NULL_HANDLE;
    VkRenderPass     pass_        = VK_NULL_HANDLE;
    VkFramebuffer    framebuffer_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_    = VK_NULL_HANDLE;
};
