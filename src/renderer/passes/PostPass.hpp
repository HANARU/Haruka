#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "HPass.hpp"
#include "../RenderContext.hpp"

// ── PostPass ──────────────────────────────────────────────────────────────────
// ACES 톤맵 + bloom/SSAO 합성 → 출력 이미지(데스크톱 스왑체인, XR 스왑체인).
// 가짜 TAA(히스토리 이미지·blit)는 2026-10-04 에 지웠습니다(DEF-15). 진짜 TAA 나 Quest 의
// 프레임 합성(AppSW)은 모션 벡터와 눈별 이전 행렬이 필요해 모양이 달라서, 그때 새로 만듭니다.
//
// 한 프레임에 같은 이미지에 여러 번 합성할 수 있습니다(스테레오 흉내 모드의 좌우 절반). 그 이미지에
// 처음 쓸 때만 전체를 검게 지우고(clear 패스), 그다음은 앞의 결과를 남깁니다(load 패스).
// 출력 포맷마다 렌더 패스 두 개와 파이프라인 하나를 둡니다(XR 스왑체인 포맷이 다를 수 있음).
class PostPass : public HPass {
public:
    void create(RenderContext& rc);   // 데스크톱 스왑체인 포맷 변형 + 레이아웃 (크기 무관)
    void destroy() override;
    void releaseSizeDependent() override;                      // 데스크톱 framebuf
    void acquire(const std::vector<VkImageView>& swapViews);   // 데스크톱 framebuf
    void rebuildPipelines() override;                          // 모든 포맷 변형

    // 합성할 곳.
    struct Output {
        VkFramebuffer fb     = VK_NULL_HANDLE;   // 아래 format 의 렌더 패스로 만든 프레임버퍼
        VkFormat      format = VK_FORMAT_UNDEFINED;
        VkExtent2D    extent{};                  // 프레임버퍼 전체 크기
        VkRect2D      rect{};                    // 씬을 그릴 사각형
        bool          clearAll = true;           // 이번 프레임 이 이미지에 처음 쓰면 true
        float         uvScale[2]  = { 1.f, 1.f };   // HDR 에서 읽을 부분(거울 화면 자르기)
        float         uvOffset[2] = { 0.f, 0.f };
    };
    void recordTonemap(VkCommandBuffer cmd, const Output& out,
                       VkImageView hdrView, VkImageView ssaoView,
                       VkImageView bloomView, VkSampler clampSampler);

    // 데스크톱 스왑체인 이미지의 프레임버퍼(acquire 가 만든 것).
    VkFramebuffer swapFramebuffer(uint32_t imgIdx) const { return framebufs_[imgIdx]; }

    // fmt 포맷 이미지에 합성할 프레임버퍼를 만들 때 쓰는 렌더 패스입니다. 그 포맷의 변형이 없으면
    // 렌더 패스와 파이프라인을 만듭니다(XR 스왑체인 포맷이 데스크톱과 다를 때).
    VkRenderPass renderPassFor(VkFormat fmt);

private:
    struct Variant {
        VkFormat     format    = VK_FORMAT_UNDEFINED;
        VkRenderPass clearPass = VK_NULL_HANDLE;   // 처음 쓸 때: 전체를 검게 지움
        VkRenderPass loadPass  = VK_NULL_HANDLE;   // 같은 프레임 두 번째부터: 앞 결과 유지
        VkPipeline   pipeline  = VK_NULL_HANDLE;   // clearPass 로 만들고 loadPass 와도 호환
    };
    Variant& variantFor(VkFormat fmt);   // 없으면 만듦
    void     createVariantPasses(Variant& v);
    void     destroyVariant(Variant& v);

    RenderContext* rc_ = nullptr;

    std::vector<Variant>       variants_;    // [0] = 데스크톱 스왑체인 포맷
    std::vector<VkFramebuffer> framebufs_;   // 데스크톱 스왑체인 이미지당 1개
    VkDescriptorSetLayout      setLayout_  = VK_NULL_HANDLE;
    VkPipelineLayout           pipeLayout_ = VK_NULL_HANDLE;
};
