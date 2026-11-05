#pragma once
#include <vulkan/vulkan.h>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include "HPass.hpp"
#include "../RenderContext.hpp"
#include "../RenderTypes.hpp"

// ── AOPass ────────────────────────────────────────────────────────────────────
// 3-스테이지 앰비언트 오클루전(눈마다):
//   1) linDepth — MSAA 깊이를 half-res R32F 선형 깊이로 변환
//   2) SSAO    — 선형 깊이 + 노이즈 + 헤미스피어 커널로 half-res AO 생성
//   3) 블러    — 4x4 깊이 인식 블러로 노이즈 타일 무늬를 지움(2026-11-02, DEF-163, ssao_blur.frag)
// 결과(블러 뒤)는 ssaoView()로 PostPass가 합성한다.
class AOPass : public HPass {
public:
    static constexpr int kFramesInFlight = 2;   // == MAX_FRAMES

    // 프레임별 카메라 파라미터 (RenderView 추출 시점에 채움)
    struct CameraParams {
        float     zNear = 0.1f, zFar = 100.f;
        glm::mat4 invProj{1.f};
        glm::mat4 proj{1.f};      // SSAO 샘플을 화면으로 투영(예전에는 셰이더가 픽셀마다 inverse(invProj) 를 계산)
        bool      valid = false;
    };

    void create(RenderContext& rc);
    void destroy() override;
    void releaseSizeDependent() override;
    void acquireSizeDependent() override;
    void rebuildPipelines() override;

    // viewIndex 는 눈 번호입니다. 눈마다 투영이 달라(비대칭 시야) invProj 를 눈별 UBO 에 둡니다.
    // 깊이는 이 패스의 NEAREST 샘플러로 읽습니다(DEF-156, 예전에는 렌더러의 LINEAR clampSampler 를 받았음).
    void record(VkCommandBuffer cmd, uint32_t frameIdx, uint32_t viewIndex,
                VkImageView msaaDepthView, const CameraParams& cam);

    // 톤매핑이 읽는 AO(블러 결과). 블러 전 이미지는 이 패스 안에서만 씁니다.
    VkImageView ssaoView() const { return blurView_; }

    // 블러의 깊이 허용치(예측 깊이와의 차이 / 중심 깊이). 크면 윤곽 너머로 AO 가 번지고, 작으면 비스듬한 바닥에 무늬가 남습니다.
    static constexpr float kBlurRelDepthTol = 0.04f;

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

    // 블러(위 SSAO 결과 → 같은 크기 · 형식의 다른 이미지. 읽는 이미지에 쓸 수 없어 한 장 더 둡니다). 렌더 패스는
    // 같은 형식이라 ssaoPass_ 를 함께 씁니다.
    VkImage          blurImage_      = VK_NULL_HANDLE;
    VkImageView      blurView_       = VK_NULL_HANDLE;
    HarukaAllocation blurAlloc_;
    VkFramebuffer    blurFramebuf_   = VK_NULL_HANDLE;
    VkPipeline       blurPipeline_   = VK_NULL_HANDLE;
    VkPipelineLayout blurPipeLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout blurSetLayout_ = VK_NULL_HANDLE;

    static constexpr int kDataBufs = kFramesInFlight * (int)kMaxViews;   // 프레임 슬롯 × 눈
    VkBuffer         dataBufs_[kDataBufs]   = {};
    HarukaAllocation dataAllocs_[kDataBufs] = {};
    VkImage          noiseTex_     = VK_NULL_HANDLE;
    VkImageView      noiseView_    = VK_NULL_HANDLE;
    // 4x4 노이즈를 화면에 타일링하는 REPEAT+NEAREST 전용 샘플러(D-3). clampSampler 로 읽으면
    // 회전이 상수가 되어 구조적 밴딩이 생깁니다.
    VkSampler        noiseSampler_ = VK_NULL_HANDLE;
    // 깊이(MSAA 깊이 · R32_SFLOAT 선형 깊이)를 보간 없이(NEAREST, CLAMP_TO_EDGE) 읽는 샘플러(2026-11-01, DEF-156). R32_SFLOAT 의
    // 선형 필터링은 선택 기능이라 Android 기준선과 Quest 의 Adreno 에는 없을 수 있고, 깊이를 실루엣 너머로 섞는 것도 틀립니다.
    VkSampler        depthSampler_ = VK_NULL_HANDLE;
    HarukaAllocation noiseAlloc_;
};
