#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "HPass.hpp"
#include "../RenderContext.hpp"
#include "../RenderTypes.hpp"

struct MaterialHandle;

// ── GeometryPass ──────────────────────────────────────────────────────────────
// 메인 3-서브패스 (depth pre → opaque PBR → transparent PBR + 라인 오버레이).
// MSAA 컬러/깊이 + 프레임별 HDR resolve 타깃을 소유하고,
// 깊이 뷰(AO 입력)와 HDR 뷰(블룸/포스트 입력)를 노출한다.
class GeometryPass : public HPass {
public:
    static constexpr uint32_t kMaxLineVerts = 2048;

    void create(RenderContext& rc, VkPipelineLayout pbrLayout,
                VkDescriptorSetLayout frameSetLayout);
    void destroy() override;
    void releaseSizeDependent() override;   // msaa/depth/hdr + 프레임버퍼
    void acquireSizeDependent() override;
    void rebuildPipelines() override;       // depth/opaque/transparent/line PSO

    // CPU 드로우 항목 (머티리얼 푸시 디스크립터 포함)
    struct DrawItem {
        // 항상 유효합니다. 머티리얼이 없는 메시는 Renderer 가 오류 머티리얼로 바꿔 넣습니다.
        // 그래도 null 이 오면 세 서브패스 모두 그 항목을 건너뜁니다(DEF-28).
        const MaterialHandle* material;
        uint32_t indexCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance;             // transform SSBO row (묶음이면 첫 행)
        uint32_t instanceCount = 1;         // 같은 지오메트리 · 머티리얼 묶음의 크기(DEF-16)
    };

    struct Inputs {
        VkDescriptorSet frameSet, sceneSet, transformSet;
        VkBuffer        vertexBuffer, indexBuffer;
        VkSampler       textureSampler, clampSampler;
        // CPU 드로우 (subpass 0/1 은 opaqueItems, subpass 2 는 transparentItems)
        const std::vector<DrawItem>* opaqueItems      = nullptr;
        const std::vector<DrawItem>* transparentItems = nullptr;
    };

    // tsPool 이 유효하면 렌더패스 전체가 끝난 뒤 tsQueryAfterMain 에 타임스탬프를 기록합니다.
    // resolve 까지 포함해야 "Main" 이라는 이름이 사실이 됩니다.
    // viewIndex 는 눈 번호입니다(단안 0, 스테레오 0=왼쪽 · 1=오른쪽). 눈마다 한 번씩 부르고,
    // 셰이더가 FrameUBO 의 그 눈 행렬과 위치를 씁니다(DEF-09).
    void record(VkCommandBuffer cmd, uint32_t frameIdx, uint32_t viewIndex, const Inputs& in,
                VkQueryPool tsPool, uint32_t tsQueryAfterMain);

    // 에디터 라인 오버레이 (다음 record에서 그려짐)
    void setLines(std::vector<LineVertex> lines) { pendingLines_ = std::move(lines); }

    // 기즈모 삼각형 오버레이 (색을 입힌 솔리드 핸들, 다음 record에서 그려짐).
    // 라인과 같은 정점 형식·셰이더를 쓰되 토폴로지만 삼각형입니다. 깊이 테스트가
    // 꺼져 있어 물체에 가려지지 않고 항상 잡을 수 있습니다.
    void setGizmoTris(std::vector<LineVertex> tris) { pendingTris_ = std::move(tris); }

    VkImageView depthView() const          { return depthView_; }
    VkImageView hdrView(uint32_t i) const  { return hdrView_[i]; }
    VkImage     hdrImage(uint32_t i) const { return hdrImage_[i]; }

private:
    void createPipelines();
    void destroyPipelines();
    void createLineBuffer();

    RenderContext*        rc_             = nullptr;
    VkPipelineLayout      pbrLayout_      = VK_NULL_HANDLE;   // FrameResources 소유 (참조)
    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;   // 라인 PSO용 (참조)

    VkRenderPass               pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebufs_;   // MAX_FRAMES (hdr[i]별)

    // MSAA color + depth
    VkImage          msaaImage_ = VK_NULL_HANDLE;
    VkImageView      msaaView_  = VK_NULL_HANDLE;
    HarukaAllocation msaaAlloc_;
    VkImage          depthImage_ = VK_NULL_HANDLE;
    VkImageView      depthView_  = VK_NULL_HANDLE;
    HarukaAllocation depthAlloc_;

    // HDR resolve (per frame-in-flight)
    VkImage          hdrImage_[MAX_FRAMES] = {};
    VkImageView      hdrView_[MAX_FRAMES]  = {};
    HarukaAllocation hdrAlloc_[MAX_FRAMES] = {};

    // PSOs
    VkPipeline depthPipeline_       = VK_NULL_HANDLE;   // subpass 0
    VkPipeline opaquePipeline_      = VK_NULL_HANDLE;   // subpass 1
    // A1: 투명 볼록 메시 2패스 — 뒷면 먼저(CULL_FRONT), 앞면 나중(CULL_BACK). subpass 2.
    VkPipeline transparentBackPipeline_  = VK_NULL_HANDLE;
    VkPipeline transparentFrontPipeline_ = VK_NULL_HANDLE;

    // Line overlay. 정점 버퍼는 프레임 슬롯마다 따로 둡니다. 하나를 같이 쓰면, 렌더 루프는
    // 지금 슬롯의 펜스만 기다리므로 직전 프레임이 아직 읽는 버퍼를 덮어씁니다(DEF-40).
    VkPipeline       linePipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout linePipelineLayout_ = VK_NULL_HANDLE;
    VkBuffer         lineVertBuf_[MAX_FRAMES] = {};
    VkDeviceMemory   lineVertMem_[MAX_FRAMES] = {};
    void*            lineVertPtr_[MAX_FRAMES] = {};
    std::vector<LineVertex> pendingLines_;

    // 기즈모 삼각형 오버레이 (linePipelineLayout_ 을 공유, 토폴로지만 삼각형). 버퍼는 라인과
    // 같은 이유로 프레임 슬롯마다 둡니다.
    static constexpr uint32_t kMaxTriVerts = 8192;
    VkPipeline       triPipeline_ = VK_NULL_HANDLE;
    VkBuffer         triVertBuf_[MAX_FRAMES] = {};
    VkDeviceMemory   triVertMem_[MAX_FRAMES] = {};
    void*            triVertPtr_[MAX_FRAMES] = {};
    std::vector<LineVertex> pendingTris_;

    // 호스트에서 쓰는 정점 버퍼 하나를 만들고 매핑합니다(라인·삼각형 공용).
    void createHostVertexBuffer(VkDeviceSize size, const char* what,
                                VkBuffer& buf, VkDeviceMemory& mem, void*& ptr);
};
