#pragma once
#include <vulkan/vulkan.h>
#include <functional>
#include <vector>
#include "HPass.hpp"
#include "../RenderContext.hpp"
#include "../RenderTypes.hpp"
#include "../ParticleOrder.hpp"

struct MaterialHandle;

// ── GeometryPass ──────────────────────────────────────────────────────────────
// 메인 3-서브패스 (depth pre → opaque PBR → transparent PBR + 라인 오버레이).
// MSAA 컬러/깊이 + 프레임별 HDR resolve 타깃을 소유하고,
// 깊이 뷰(AO 입력)와 HDR 뷰(블룸/포스트 입력)를 노출한다.
//
// 멀티뷰(2026-11-18, DEF-09 둘째 단계). 렌더 패스와 파이프라인을 두 벌 둡니다. 단안 패스는 한 층에 눈 하나를 그리고
// (단안 · 반사 캡처 · --no-multiview), 멀티뷰 패스는 viewMask 0b11 로 두 층에 두 눈을 한 번에 그립니다(Quest 의 장면 패스
// 드로 · CPU 기록이 절반). 렌더 패스 호환성에 viewMask 가 들어가 파이프라인을 같이 쓸 수 없습니다. 셰이더는 한 벌이고
// 눈 번호를 푸시 상수 + gl_ViewIndex 로 고릅니다(HARUKA_EYE_INDEX). 장면 타깃(깊이 · MSAA · HDR)은 스테레오가 가능한 동안
// (rc.sceneLayers == 2) 2층 배열이고, 층 v 가 눈 v 입니다. AO · 블룸 · 톤매핑은 층별 뷰를 읽어 지금처럼 눈마다 돕니다(UE 의
// Mobile Multi-View 도 장면 패스만 멀티뷰). 단안이면 1층이라 메모리는 예전과 같습니다.
class GeometryPass : public HPass {
public:
    static constexpr uint32_t kMaxLineVerts = 2048;
    // 메인 패스 색 · HDR resolve 포맷. 렌더 확장도 이 값으로 파이프라인을 만듭니다.
    static constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

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
        bool     mirrored = false;          // 거울 변환: 앞면이 시계 방향인 파이프라인으로 그림(DEF-170)
        bool     index16  = false;          // 16비트 인덱스 구역(O3): 인덱스 버퍼를 그 구역 · 형식으로 바인딩
        uint8_t  lightChannels = 1;         // 메시 라이트 채널(바뀔 때만 푸시 상수를 다시 넣음)
    };

    // 월드 UI 패널 하나(3D UI P1b). model 은 액터 월드 행렬에 패널 크기(m)를 곱한 것이고, 패널은 로컬 XY 평면의
    // 1×1 사각형(가운데 원점, 앞면 +Z)입니다. view 는 알파를 곱한 sRGB 텍스처(UIPanelPass)입니다.
    struct PanelDraw {
        glm::mat4   model;
        VkImageView view     = VK_NULL_HANDLE;
        bool        twoSided = false;   // 거짓이면 뒷면을 그리지 않음(UE Widget Component 의 기본과 같음)
        float       brightness = 1.f;   // 색에 곱함(알파는 그대로)
    };

    // 빌보드 파티클(2026-11-20, Docs/Particles_And_Decals.md D7). data 는 월드 공간 인스턴스를 그릴 순서대로 이은 것이고(ParticleOrder),
    // ranges 가 드로 단위(섞기 방식)입니다. 첫 눈을 기록할 때 이 프레임 슬롯의 인스턴스 버퍼로 복사하고 두 눈이 함께 읽습니다.
    struct ParticleList {
        const vfx::ParticleInstance*      data    = nullptr;
        uint32_t                          count   = 0;
        const std::vector<ParticleRange>* ranges  = nullptr;
        VkImageView                       atlas   = VK_NULL_HANDLE;   // 모양 아틀라스(선형 RGBA8)
        VkSampler                         sampler = VK_NULL_HANDLE;
    };
    static constexpr uint32_t kMaxParticleInstances = vfx::kMaxParticles;

    struct Inputs {
        VkDescriptorSet frameSet, sceneSet, transformSet;
        VkBuffer        vertexBuffer, indexBuffer;
        VkSampler       textureSampler, clampSampler;
        VkSampler       panelSampler = VK_NULL_HANDLE;   // 패널: 가장자리 고정 · 밉 선형 · 이방성
        const std::vector<PanelDraw>* panels = nullptr;  // 반투명 뒤에 그림
        const ParticleList*           particles = nullptr;   // 반투명 메시 뒤 · 패널 앞에 그림(반사 캡처는 넘기지 않음)
        // CPU 드로우 (subpass 0/1 은 opaqueItems, subpass 2 는 transparentItems)
        const std::vector<DrawItem>* opaqueItems      = nullptr;
        const std::vector<DrawItem>* transparentItems = nullptr;
        // 렌더 확장 지점(DEF-17). 불투명 서브패스 끝(인자 1)과 반투명 서브패스의 반투명 뒤 · 기즈모 앞
        // (인자 2)에서 부릅니다. 확장이 바꾼 상태는 이 패스가 뒤에서 다시 설정합니다.
        const std::function<void(uint32_t subpass)>* subpassHook = nullptr;
        bool overlays = true;   // 에디터 선 · 기즈모 · 월드 UI 포인터를 그림(반사 캡처는 끔)
        // 그릴 영역(왼쪽 위 원점). 0 이면 렌더 크기 전체. 반사 캡처가 면마다 작은 정사각형만 그리는 데 씁니다(2026-11-17).
        VkExtent2D area{ 0, 0 };
    };

    // tsPool 이 유효하면 렌더패스 전체가 끝난 뒤 tsQueryAfterMain 에 타임스탬프를 기록합니다.
    // resolve 까지 포함해야 "Main" 이라는 이름이 사실이 됩니다.
    // viewIndex 는 눈 번호입니다(단안 0, 스테레오 0=왼쪽 · 1=오른쪽). 단안 패스로 눈 하나를 layerFor(viewIndex) 층에
    // 그리고, 셰이더가 FrameUBO 의 그 눈 행렬과 위치를 씁니다(DEF-09).
    void record(VkCommandBuffer cmd, uint32_t frameIdx, uint32_t viewIndex, const Inputs& in,
                VkQueryPool tsPool, uint32_t tsQueryAfterMain);
    // 두 눈을 멀티뷰 패스로 한 번에 그립니다(층 0 = 왼눈, 층 1 = 오른눈). multiviewReady() 일 때만 부릅니다.
    void recordMultiview(VkCommandBuffer cmd, uint32_t frameIdx, const Inputs& in,
                         VkQueryPool tsPool, uint32_t tsQueryAfterMain);

    // 에디터 라인 오버레이 (다음 record에서 그려짐)
    void setLines(std::vector<LineVertex> lines) { pendingLines_ = std::move(lines); }
    // 월드 UI 포인터(레이저 · 커서, 3D UI 구간 6). 에디터 선 뒤에 이어 그립니다. 엔진이 프레임마다 넣습니다(게임 실행 파일 포함).
    void setPointerLines(std::vector<LineVertex> lines) { pendingPointerLines_ = std::move(lines); }

    // 기즈모 삼각형 오버레이 (색을 입힌 솔리드 핸들, 다음 record에서 그려짐).
    // 라인과 같은 정점 형식·셰이더를 쓰되 토폴로지만 삼각형입니다. 깊이 테스트가
    // 꺼져 있어 물체에 가려지지 않고 항상 잡을 수 있습니다.
    void setGizmoTris(std::vector<LineVertex> tris) { pendingTris_ = std::move(tris); }

    // pbr.frag 섀도 커널 등급(HARUKA_SHADOW_FILTER_*, 특수화 상수). create 전에 부릅니다. 실행 중에 바꾸면
    // rebuildPipelines 를 불러야 반영됩니다. 지금은 부르는 곳이 없습니다(프로젝트 설정 연결은 뒤 단계).
    void setShadowFilter(int32_t v) { shadowFilter_ = v; }

    VkRenderPass mainPass() const          { return pass_; }     // 렌더 확장의 파이프라인 호환용(단안 패스)
    VkRenderPass mainPassMultiview() const { return mvPass_; }   // 같은 구조에 viewMask 0b11
    // 장면 타깃의 층 수(1 또는 2)와 눈 v 를 그리는 층. 층이 하나면 두 눈이 그 층을 번갈아 씁니다.
    uint32_t    layerCount() const               { return layers_; }
    uint32_t    layerFor(uint32_t view) const    { return view < layers_ ? view : layers_ - 1; }
    bool        multiviewReady() const           { return layers_ == 2 && mvPass_ && mv_.depth && !mvFramebufs_.empty(); }
    VkImageView depthView(uint32_t layer = 0) const          { return depthLayerView_[layer]; }
    VkImageView hdrView(uint32_t i, uint32_t layer = 0) const { return hdrLayerView_[i][layer]; }
    VkImage     hdrImage(uint32_t i) const                    { return hdrImage_[i]; }   // 층 전체(층 번호는 layerFor)

private:
    // 한 렌더 패스용 파이프라인 묶음(단안 · 멀티뷰 각 한 벌).
    struct PipelineSet {
        VkPipeline depth = VK_NULL_HANDLE;            // subpass 0
        VkPipeline opaque = VK_NULL_HANDLE;           // subpass 1
        // A1: 투명 볼록 메시 2패스 — 뒷면 먼저(CULL_FRONT), 앞면 나중(CULL_BACK). subpass 2.
        VkPipeline transparentBack = VK_NULL_HANDLE;
        VkPipeline transparentFront = VK_NULL_HANDLE;
        // 거울 변환(행렬식 < 0) 메시용 변형: 앞면 시계 방향(DEF-170). 컬링 면은 위 짝과 같습니다.
        VkPipeline depthMirrored = VK_NULL_HANDLE;
        VkPipeline opaqueMirrored = VK_NULL_HANDLE;
        VkPipeline transparentBackMirrored = VK_NULL_HANDLE;
        VkPipeline transparentFrontMirrored = VK_NULL_HANDLE;
        VkPipeline line = VK_NULL_HANDLE;             // 에디터 선 · 월드 UI 포인터(subpass 2)
        VkPipeline tri = VK_NULL_HANDLE;              // 기즈모 삼각형(subpass 2)
        VkPipeline panel = VK_NULL_HANDLE;            // 월드 UI 패널(subpass 2)
        VkPipeline particleAdd   = VK_NULL_HANDLE;    // 빌보드 파티클 가산(subpass 2)
        VkPipeline particleAlpha = VK_NULL_HANDLE;    // 빌보드 파티클 알파 섞기(subpass 2)
        template <class F> void forEach(F&& f) {
            for (VkPipeline* p : { &depth, &opaque, &transparentBack, &transparentFront, &depthMirrored, &opaqueMirrored,
                                   &transparentBackMirrored, &transparentFrontMirrored, &line, &tri, &panel,
                                   &particleAdd, &particleAlpha })
                f(*p);
        }
    };

    VkRenderPass createMainRenderPass(bool multiview) const;
    void createPipelines();
    void createPipelineSet(VkRenderPass rp, PipelineSet& out);
    void destroyPipelines();
    void destroySet(PipelineSet& set);
    void createLineBuffer();
    void recordImpl(VkCommandBuffer cmd, uint32_t frameIdx, VkRenderPass rp, VkFramebuffer fb, const PipelineSet& ps,
                    uint32_t viewIndex, const Inputs& in, VkQueryPool tsPool, uint32_t tsQueryAfterMain);
    VkImageView createLayerView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect, uint32_t baseLayer,
                                uint32_t layerCount) const;

    RenderContext*        rc_             = nullptr;
    VkPipelineLayout      pbrLayout_      = VK_NULL_HANDLE;   // FrameResources 소유 (참조)
    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;   // 라인 PSO용 (참조)

    VkRenderPass               pass_   = VK_NULL_HANDLE;   // 단안(눈 하나, 층 하나)
    VkRenderPass               mvPass_ = VK_NULL_HANDLE;   // 멀티뷰(viewMask 0b11, 두 층)
    std::vector<VkFramebuffer> framebufs_;     // 단안: [프레임 슬롯 * kMaxViews + 층] (층은 layers_ 개만)
    std::vector<VkFramebuffer> mvFramebufs_;   // 멀티뷰: [프레임 슬롯] (layers_ == 2 일 때만)
    uint32_t                   layers_ = 1;    // 장면 타깃 층 수(acquireSizeDependent 때 rc.sceneLayers)

    // MSAA color + depth. 층별 뷰는 2D, 배열 뷰는 멀티뷰 프레임버퍼용 2D_ARRAY(두 층일 때만)입니다.
    VkImage          msaaImage_ = VK_NULL_HANDLE;
    VkImageView      msaaLayerView_[kMaxViews] = {};
    VkImageView      msaaArrayView_ = VK_NULL_HANDLE;
    HarukaAllocation msaaAlloc_;
    VkImage          depthImage_ = VK_NULL_HANDLE;
    VkImageView      depthLayerView_[kMaxViews] = {};
    VkImageView      depthArrayView_ = VK_NULL_HANDLE;
    HarukaAllocation depthAlloc_;

    // HDR resolve (per frame-in-flight)
    VkImage          hdrImage_[MAX_FRAMES] = {};
    VkImageView      hdrLayerView_[MAX_FRAMES][kMaxViews] = {};
    VkImageView      hdrArrayView_[MAX_FRAMES] = {};
    HarukaAllocation hdrAlloc_[MAX_FRAMES] = {};

    // PSOs
    PipelineSet mono_;   // pass_ 용
    PipelineSet mv_;     // mvPass_ 용
    int32_t    shadowFilter_ = HARUKA_SHADOW_FILTER_LEGACY9;   // PBR 파이프라인의 특수화 상수

    // Line overlay. 정점 버퍼는 프레임 슬롯마다 따로 둡니다. 하나를 같이 쓰면, 렌더 루프는
    // 지금 슬롯의 펜스만 기다리므로 직전 프레임이 아직 읽는 버퍼를 덮어씁니다(DEF-40).
    VkPipelineLayout linePipelineLayout_ = VK_NULL_HANDLE;
    VkBuffer         lineVertBuf_[MAX_FRAMES] = {};
    VkDeviceMemory   lineVertMem_[MAX_FRAMES] = {};
    void*            lineVertPtr_[MAX_FRAMES] = {};
    std::vector<LineVertex> pendingLines_;
    std::vector<LineVertex> pendingPointerLines_;   // 월드 UI 포인터(에디터 선 뒤에 이어 올림)

    // 기즈모 삼각형 오버레이 (linePipelineLayout_ 을 공유, 토폴로지만 삼각형). 버퍼는 라인과
    // 같은 이유로 프레임 슬롯마다 둡니다.
    static constexpr uint32_t kMaxTriVerts = 8192;
    VkBuffer         triVertBuf_[MAX_FRAMES] = {};
    VkDeviceMemory   triVertMem_[MAX_FRAMES] = {};
    void*            triVertPtr_[MAX_FRAMES] = {};
    std::vector<LineVertex> pendingTris_;

    // 월드 UI 패널(3D UI P1b, subpass 2). 정점 버퍼 없이 정점 번호로 사각형을 만들고, 텍스처는 set 1 푸시 디스크립터입니다.
    VkPipelineLayout      panelLayout_    = VK_NULL_HANDLE;
    VkDescriptorSetLayout panelSetLayout_ = VK_NULL_HANDLE;

    // 빌보드 파티클(subpass 2). set 1 은 아틀라스 푸시 디스크립터, 인스턴스 버퍼는 프레임 슬롯마다 하나(처음 그릴 때 만듦).
    VkPipelineLayout      particleLayout_    = VK_NULL_HANDLE;
    VkDescriptorSetLayout particleSetLayout_ = VK_NULL_HANDLE;
    VkBuffer              particleBuf_[MAX_FRAMES] = {};
    VkDeviceMemory        particleMem_[MAX_FRAMES] = {};
    void*                 particlePtr_[MAX_FRAMES] = {};
    void createParticlePipelines(VkRenderPass rp, PipelineSet& ps);   // GeometryPass_particles.cpp
    void destroyParticleResources();
    void recordParticles(VkCommandBuffer cmd, uint32_t frameIdx, uint32_t viewIndex, const PipelineSet& ps,
                         const Inputs& in, const VkViewport& vp, const VkRect2D& sc);

    // 호스트에서 쓰는 정점 버퍼 하나를 만들고 매핑합니다(라인·삼각형 공용).
    void createHostVertexBuffer(VkDeviceSize size, const char* what,
                                VkBuffer& buf, VkDeviceMemory& mem, void*& ptr);
};
