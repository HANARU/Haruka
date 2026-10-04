#pragma once
#include <vulkan/vulkan.h>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include "HPass.hpp"
#include "../RenderContext.hpp"
#include "../RenderTypes.hpp"

// ── AOPass ────────────────────────────────────────────────────────────────────
// 2-스테이지 앰비언트 오클루전:
//   1) linDepth — MSAA 깊이를 half-res R32F 선형 깊이로 변환
//   2) SSAO    — 선형 깊이 + 노이즈 + 헤미스피어 커널로 half-res AO 생성
// 결과는 ssaoView()로 PostPass가 합성한다.
class AOPass : public HPass {
public:
    static constexpr int kFramesInFlight = 2;   // == MAX_FRAMES

    // 프레임별 카메라 파라미터 (RenderView 추출 시점에 채움)
    struct CameraParams {
        float     zNear = 0.1f, zFar = 100.f;
        glm::mat4 invProj{1.f};
        bool      valid = false;
    };

    void create(RenderContext& rc);
    void destroy() override;
    void releaseSizeDependent() override;
    void acquireSizeDependent() override;
    void rebuildPipelines() override;

    // viewIndex 는 눈 번호입니다. 눈마다 투영이 달라(비대칭 시야) invProj 를 눈별 UBO 에 둡니다.
    void record(VkCommandBuffer cmd, uint32_t frameIdx, uint32_t viewIndex,
                VkImageView msaaDepthView, VkSampler clampSampler,
                const CameraParams& cam);

    VkImageView ssaoView() const { return ssaoView_; }

private:
    void createPipelines();
    void destroyPipelines();
    void createDataBuffers();   // 커널 UBO + 노이즈 텍스처 (1회)

    RenderContext* rc_ = nullptr;

    // linDepth
    VkImage          linDepthImage_     = VK_NULL_HANDLE;
    VkImageView      linDepthView_      = VK_NULL_HANDLE;
    HarukaAllocation linDepthAlloc_;
    VkRenderPass     linDepthPass_      = VK_NULL_HANDLE;
    VkFramebuffer    linDepthFramebuf_  = VK_NULL_HANDLE;
    VkPipeline       linDepthPipeline_  = VK_NULL_HANDLE;
    VkPipelineLayout linDepthPipeLayout_= VK_NULL_HANDLE;
    VkDescriptorSetLayout linDepthSetLayout_ = VK_NULL_HANDLE;

    // SSAO
    VkImage          ssaoImage_      = VK_NULL_HANDLE;
    VkImageView      ssaoView_       = VK_NULL_HANDLE;
    HarukaAllocation ssaoAlloc_;
    VkRenderPass     ssaoPass_       = VK_NULL_HANDLE;
    VkFramebuffer    ssaoFramebuf_   = VK_NULL_HANDLE;
    VkPipeline       ssaoPipeline_   = VK_NULL_HANDLE;
    VkPipelineLayout ssaoPipeLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout ssaoSetLayout_ = VK_NULL_HANDLE;

    static constexpr int kDataBufs = kFramesInFlight * (int)kMaxViews;   // 프레임 슬롯 × 눈
    VkBuffer         dataBufs_[kDataBufs]   = {};
    HarukaAllocation dataAllocs_[kDataBufs] = {};
    VkImage          noiseTex_     = VK_NULL_HANDLE;
    VkImageView      noiseView_    = VK_NULL_HANDLE;
    // 4x4 노이즈를 화면에 타일링하는 REPEAT+NEAREST 전용 샘플러(D-3). clampSampler 로 읽으면
    // 회전이 상수가 되어 구조적 밴딩이 생깁니다.
    VkSampler        noiseSampler_ = VK_NULL_HANDLE;
    HarukaAllocation noiseAlloc_;
};
