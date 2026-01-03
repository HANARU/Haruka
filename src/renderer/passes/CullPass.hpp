#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include "HPass.hpp"
#include "../RenderContext.hpp"
#include "../RenderTypes.hpp"

// ── CullPass ──────────────────────────────────────────────────────────────────
// GPU 프러스텀 컬링 컴퓨트. CPU가 채운 바운딩 스피어 + 드로우 커맨드 입력을
// 받아 생존 드로우를 indirect 버퍼(opaqueDrawBuf)와 카운트로 출력한다.
class CullPass : public HPass {
public:
    void create(RenderContext& rc);   // Zone C 버퍼 + 레이아웃 + 디스크립터 (1회)
    void destroy() override;
    void rebuildPipelines() override; // 컴퓨트 PSO만

    // 컬링 입력 업로드 (드로우 커맨드 + 바운딩 스피어).
    // 두 배열은 **같은 인덱스 규칙**(opaque 목록 위치)이어야 하고 크기도 같아야 한다.
    // cull.comp가 spheres[id]와 inDraws[id]를 같은 id로 읽기 때문이다
    // (shaders/cull.comp:54,57). 예전에는 스피어를 전역 transform 행 번호로 직접
    // 매핑 버퍼에 써서 두 규칙이 어긋났다. 그래서 스피어 쓰기 경로를 이 함수로 모았다.
    void uploadInput(uint32_t fi, const std::vector<GpuDrawCmd>& opaque,
                     const std::vector<GpuBoundSphere>& spheres);

    // count==0이면 no-op. fill→barrier→dispatch→barrier (indirect 소비 준비까지)
    void record(VkCommandBuffer cmd, uint32_t fi,
                const glm::mat4& viewProj, uint32_t count);

    VkBuffer opaqueDrawBuf(uint32_t fi) const  { return opaqueDrawBufs_[fi]; }
    VkBuffer opaqueCountBuf(uint32_t fi) const { return opaqueCountBufs_[fi]; }

private:
    RenderContext* rc_ = nullptr;

    VkBuffer         sphereBufs_[MAX_FRAMES]       = {};
    HarukaAllocation sphereAllocs_[MAX_FRAMES]     = {};
    VkBuffer         inDrawBufs_[MAX_FRAMES]       = {};
    HarukaAllocation inDrawAllocs_[MAX_FRAMES]     = {};
    VkBuffer         opaqueDrawBufs_[MAX_FRAMES]   = {};
    HarukaAllocation opaqueDrawAllocs_[MAX_FRAMES] = {};
    VkBuffer         opaqueCountBufs_[MAX_FRAMES]  = {};
    HarukaAllocation opaqueCountAllocs_[MAX_FRAMES]= {};

    VkPipeline            pipeline_   = VK_NULL_HANDLE;
    VkPipelineLayout      pipeLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_  = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_   = VK_NULL_HANDLE;
    VkDescriptorSet       descSets_[MAX_FRAMES] = {};
};
