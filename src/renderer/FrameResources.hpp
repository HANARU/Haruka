#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include "RenderContext.hpp"
#include "RenderTypes.hpp"

// ── FrameResources ────────────────────────────────────────────────────────────
// 프레임별 GPU 상태의 단일 소유자 (RendererRefactor.md §3-②):
//   - 셰이더 계약: set 0~3 레이아웃 + 공유 PBR 파이프라인 레이아웃
//   - 프레임 인플라이트별: UBO/트랜스폼/라이트 버퍼, 디스크립터 세트,
//     커맨드 버퍼, imageAvail 세마포어, 펜스
//   - 스왑체인 이미지별: renderDone 세마포어, imagesInFlight 펜스 추적
class FrameResources {
public:
    // set 0~3 레이아웃 + PBR 파이프라인 레이아웃 (패스 PSO 생성 전에 필요)
    void createLayouts(RenderContext& rc);

    // 버퍼 + 디스크립터 풀/세트 + 커맨드 버퍼 + 동기화 객체
    void createPerFrame(RenderContext& rc, uint32_t swapImageCount);

    // 씬 세트(set 1) 할당 + 섀도/IBL 뷰 와이어링 — 해당 뷰 생성 후 1회 호출
    void writeSceneSets(VkSampler shadowSampler, VkImageView shadowView,
                        VkSampler iblSampler, VkImageView irradianceView,
                        VkImageView prefilterView,
                        VkSampler brdfSampler, VkImageView brdfView);

    // 리사이즈 후: imagesInFlight 리셋 + 이미지 수 변경 시 renderDone 재생성
    void onSwapchainRecreated(uint32_t swapImageCount);

    void destroy();

    // ── 셰이더 계약 ───────────────────────────────────────────────────────────
    VkDescriptorSetLayout frameLayout() const     { return frameLayout_; }
    VkDescriptorSetLayout sceneLayout() const     { return sceneLayout_; }
    VkDescriptorSetLayout transformLayout() const { return transformLayout_; }
    VkDescriptorSetLayout materialLayout() const  { return materialLayout_; }
    VkPipelineLayout      pbrLayout() const       { return pbrPipelineLayout_; }

    // ── 프레임 인플라이트별 접근자 ────────────────────────────────────────────
    VkCommandBuffer cmd(uint32_t fi) const          { return cmdBuffers_[fi]; }
    VkSemaphore     imageAvailSem(uint32_t fi) const{ return semImageAvail_[fi]; }
    VkFence         fence(uint32_t fi) const        { return fences_[fi]; }
    VkDescriptorSet frameSet(uint32_t fi) const     { return frameSets_[fi]; }
    VkDescriptorSet sceneSet(uint32_t fi) const     { return sceneSets_[fi]; }
    VkDescriptorSet transformSet(uint32_t fi) const { return transformSets_[fi]; }
    void* uboPtr(uint32_t fi) const       { return uboAllocs_[fi].mappedPtr; }
    void* transformPtr(uint32_t fi) const { return transformAllocs_[fi].mappedPtr; }
    void* lightPtr(uint32_t fi) const     { return lightAllocs_[fi].mappedPtr; }

    // ── 스왑체인 이미지별 ─────────────────────────────────────────────────────
    VkSemaphore           renderDoneSem(uint32_t imgIdx) const { return semRenderDone_[imgIdx]; }
    std::vector<VkFence>& imagesInFlight()                     { return imagesInFlight_; }

private:
    RenderContext* rc_ = nullptr;

    VkDescriptorSetLayout frameLayout_       = VK_NULL_HANDLE;  // set 0: FrameUBO
    VkDescriptorSetLayout sceneLayout_       = VK_NULL_HANDLE;  // set 1: shadow+IBL+lights
    VkDescriptorSetLayout transformLayout_   = VK_NULL_HANDLE;  // set 2: transform SSBO
    VkDescriptorSetLayout materialLayout_    = VK_NULL_HANDLE;  // set 3: material (PUSH)
    VkPipelineLayout      pbrPipelineLayout_ = VK_NULL_HANDLE;  // sets 0+1+2+3

    VkBuffer         uboBufs_[MAX_FRAMES]        = {};
    HarukaAllocation uboAllocs_[MAX_FRAMES]      = {};
    VkBuffer         transformBufs_[MAX_FRAMES]  = {};
    HarukaAllocation transformAllocs_[MAX_FRAMES]= {};
    VkBuffer         lightBufs_[MAX_FRAMES]      = {};
    HarukaAllocation lightAllocs_[MAX_FRAMES]    = {};

    VkDescriptorPool framePool_     = VK_NULL_HANDLE;
    VkDescriptorPool scenePool_     = VK_NULL_HANDLE;
    VkDescriptorPool transformPool_ = VK_NULL_HANDLE;
    VkDescriptorSet  frameSets_[MAX_FRAMES]     = {};
    VkDescriptorSet  sceneSets_[MAX_FRAMES]     = {};
    VkDescriptorSet  transformSets_[MAX_FRAMES] = {};

    std::array<VkCommandBuffer, MAX_FRAMES> cmdBuffers_{};
    std::array<VkSemaphore, MAX_FRAMES>     semImageAvail_{};
    std::array<VkFence, MAX_FRAMES>         fences_{};
    std::vector<VkSemaphore>                semRenderDone_;   // 스왑체인 이미지당 1개
    std::vector<VkFence>                    imagesInFlight_;
};
