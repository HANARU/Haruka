#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <string>
#include <memory>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <unordered_set>

#include "Vertex.hpp"
#include "MeshHandle.hpp"
#include "MaterialHandle.hpp"
#include "ISwapchain.hpp"
#include "RenderContext.hpp"
#include "RenderTypes.hpp"
#include "FrameResources.hpp"
#include "GpuScene.hpp"
#include "passes/BloomPass.hpp"
#include "passes/AOPass.hpp"
#include "passes/ShadowPass.hpp"
#include "passes/PostPass.hpp"
#include "passes/GeometryPass.hpp"
#include "../platform/IWindow.hpp"
#include "../memory/HarukaAllocator.hpp"

class World;
class HSlateRenderer;
struct TexSourceData;

inline const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
inline const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
};
#ifdef NDEBUG
    constexpr bool kEnableValidation = false;
#else
    constexpr bool kEnableValidation = true;
#endif

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool isComplete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapChainSupport {
    VkSurfaceCapabilitiesKHR        caps;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   modes;
};

class Renderer {
public:
    // 정의는 RenderTypes.hpp — 기존 Renderer::LineVertex API 호환용 별칭
    using LineVertex = ::LineVertex;

    // 라벨이 실제 측정 구간과 어긋나 있었습니다. postMs 라고 부르던 값은 사실
    // 투명 서브패스 + MSAA resolve + AO + Bloom 이었고, uiMs 는 톤매핑 단독이었으며,
    // UI 패스는 아예 측정되지 않고 있었습니다 (ts[5] 를 기록만 하고 읽지 않았습니다).
    struct GpuTimings {
        float shadowMs=0.f, mainMs=0.f, aoMs=0.f, bloomMs=0.f,
              tonemapMs=0.f, uiMs=0.f, totalMs=0.f;
    };
    // dropped = 카메라에 보이지만 행 예산(kMaxDraws)이 차서 그리지 못한 메시 수. culled 와 따로
    // 셉니다. 예전에는 조용히 잘린 메시가 culled 에 섞여 보였습니다(DEF-10).
    struct FrameStats  { uint32_t drawCalls=0, culled=0, transparentDraws=0, dropped=0; };

    void init(IWindow* window);
    void cleanup();
    void drawFrame(World& world);

    // Hot-reload shaders
    void reloadPipelines();

    // Asset upload — called by AssetManager (GpuScene으로 위임)
    MeshHandle* uploadMesh(const std::vector<Vertex>& verts,
                           const std::vector<uint32_t>& indices,
                           const MeshBounds* precomputed = nullptr) {
        return scene_.uploadMesh(verts, indices, precomputed);
    }
    GpuScene::MeshBudget meshBudget() const { return scene_.budget(); }
    MaterialHandle* uploadMaterial(
        const TexSourceData& albedo, const TexSourceData& normalMap,
        const TexSourceData& metalRough, const TexSourceData& ao,
        const TexSourceData& emissive) {
        return scene_.uploadMaterial(albedo, normalMap, metalRough, ao, emissive);
    }
    void freeMesh(MeshHandle* h)         { scene_.freeMesh(h); }
    void freeMaterial(MaterialHandle* h) { scene_.freeMaterial(h); }

    IWindow* getWindow()     const { return window;     }
    VkDevice getDevice()     const { return device;     }
    VkPhysicalDevice getPhysDevice() const { return physDevice; }
    VkFormat getSwapFormat() const { return swapchain_->format(); }

    void setUIRenderer(HSlateRenderer* r);

    // 씬을 그릴 3D 뷰포트 rect(프레임버퍼 픽셀)를 지정합니다. 에디터가 매 프레임 호출하며,
    // 값이 바뀔 때만 renderExtent 타깃을 재생성합니다(Viewport_Render_Design.md). 지정하지
    // 않으면 창 전체로 렌더합니다(게임 빌드). drawFrame 이전(에디터 tick)에서 호출해야 합니다.
    void setViewportRect(int x, int y, int w, int h);

    void setEditorLines(std::vector<LineVertex> lines) { geometry_.setLines(std::move(lines)); }
    void setGizmoTris  (std::vector<LineVertex> tris)  { geometry_.setGizmoTris(std::move(tris)); }

    VkCommandBuffer beginOneTimeCmd()                { return rc_.beginOneTimeCmd(); }
    void            endOneTimeCmd(VkCommandBuffer c) { rc_.endOneTimeCmd(c); }

    AllocStats getAllocStats() const { return allocator.getStats(); }
    GpuTimings getTimings()   const { return timings_;    }
    FrameStats getFrameStats()const { return frameStats_; }

private:
    IWindow*        window    = nullptr;
    HarukaAllocator allocator;

    // 디바이스 번들 + 저수준 헬퍼 — 패스/모듈 추출의 공용 기반 (init에서 채움)
    RenderContext   rc_;

    // ── Vulkan core ───────────────────────────────────────────────────────────
    VkInstance               instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         physDevice     = VK_NULL_HANDLE;
    VkDevice                 device         = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue  = VK_NULL_HANDLE;
    VkQueue                  presentQueue   = VK_NULL_HANDLE;

    // ── Swapchain (platform-abstracted) ──────────────────────────────────────
    std::unique_ptr<ISwapchain> swapchain_;

    // ── MSAA 샘플 수 (rc_로 전파) ─────────────────────────────────────────────
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_4_BIT;
    VkFormat              depthFormat  = VK_FORMAT_UNDEFINED;

    // ── Geometry (main 3-subpass + MSAA/depth/HDR + 라인 오버레이) ────────────
    GeometryPass geometry_;

    // ── Shadow map ────────────────────────────────────────────────────────────
    ShadowPass shadow_;

    // ── AO (linDepth + SSAO) ──────────────────────────────────────────────────
    AOPass ao_;

    // ── Bloom ─────────────────────────────────────────────────────────────────
    BloomPass bloom_;

    // ── Post-process (ACES 톤매핑 + AO/블룸 합성) ─────────────────────────────
    PostPass post_;

    // ── IBL ───────────────────────────────────────────────────────────────────
    VkImage          iblIrradianceImage_ = VK_NULL_HANDLE;
    VkImageView      iblIrradianceView_  = VK_NULL_HANDLE;
    HarukaAllocation iblIrradianceAlloc_;
    VkImage          iblPrefilterImage_  = VK_NULL_HANDLE;
    VkImageView      iblPrefilterView_   = VK_NULL_HANDLE;
    HarukaAllocation iblPrefilterAlloc_;
    VkImage          brdfLutImage_       = VK_NULL_HANDLE;
    VkImageView      brdfLutView_        = VK_NULL_HANDLE;
    HarukaAllocation brdfLutAlloc_;
    VkSampler        iblSampler_         = VK_NULL_HANDLE;
    VkSampler        brdfSampler_        = VK_NULL_HANDLE;

    // ── 자산 GPU 자원 (메가버퍼 + 텍스처 슬롯) ────────────────────────────────
    GpuScene scene_;

    // 머티리얼이 없는 메시를 그릴 때 쓰는 오류 머티리얼(자홍색, 스스로 빛남). 빠진 메시가 눈에
    // 띄도록 일부러 튀는 색입니다. 엔진이 그럴듯한 값을 대신 채우지 않는다는 정책과 같은
    // 방향입니다(DEF-28). 액터마다 한 번만 경고합니다.
    MaterialHandle*              errorMaterial_ = nullptr;
    std::unordered_set<uint64_t> noMaterialWarned_;
    bool                         dropWarned_    = false;   // kMaxDraws 초과 경고(한 번)

    // ── FFR (Fixed Foveated Rendering) ────────────────────────────────────────
    bool             ffrEnabled_    = false;
    VkImage          ffrRateImage_  = VK_NULL_HANDLE;
    VkImageView      ffrRateView_   = VK_NULL_HANDLE;
    HarukaAllocation ffrRateAlloc_;

    // ── 프레임별 GPU 상태 (레이아웃/버퍼/디스크립터/커맨드/동기화) ────────────
    FrameResources frames_;

    // ── Pipeline cache + timestamp ────────────────────────────────────────────
    VkPipelineCache pipelineCache_   = VK_NULL_HANDLE;
    // 프레임당 타임스탬프 슬롯 수입니다. 배치는 Renderer_render.cpp 의 drawFrame 참조.
    //   0 frame start / 1 shadow / 2 main(3서브패스+resolve) / 3 AO / 4 Bloom
    //   5 tonemap / 6 UI
    static constexpr uint32_t kTimestampsPerFrame = 7;
    VkQueryPool     timestampPool_   = VK_NULL_HANDLE;
    float           timestampPeriod_ = 1.f;
    GpuTimings      timings_;
    FrameStats      frameStats_;

    // ── Shared samplers ───────────────────────────────────────────────────────
    VkSampler textureSampler = VK_NULL_HANDLE;  // anisotropic, repeat
    VkSampler clampSampler_  = VK_NULL_HANDLE;  // linear, clamp to edge

    // ── Frame counters ────────────────────────────────────────────────────────
    VkCommandPool cmdPool   = VK_NULL_HANDLE;
    uint32_t      frameIdx  = 0;
    uint32_t      frameNum_ = 0;  // monotonically increasing frame counter
    float         time_     = 0.f;

    // ── 섀도 캐스터 선택 (DEF-26) ─────────────────────────────────────────────
    // updateLightSSBO 가 라이트를 채우면서 섀도를 던지는 라이트(첫 Directional +
    // castShadows)의 **SSBO 슬롯 번호**와 그 월드 방향을 여기 기록합니다. updateUBO 가
    // 같은 값으로 섀도 카메라 VP 와 miscParams.w(셰이더가 이 슬롯에만 그림자를 곱함)를
    // 만들어, 선택 지점과 채우기 지점이 어긋날 수 없습니다. 캐스터가 없으면 -1.
    int           shadowCasterSlot_ = -1;
    glm::vec3     shadowCasterDir_  = {0.f, -1.f, 0.f};

    // ── UI overlay ────────────────────────────────────────────────────────────
    HSlateRenderer* uiRenderer_ = nullptr;

    // ── Init helpers ──────────────────────────────────────────────────────────
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    VkFormat findDepthFormat();
    void createCommandPool();
    void createSamplers();
    void createPipelineCache();
    void savePipelineCache();
    void createTimestampPool();

    // IBL and one-time GPU passes
    void generateBRDFLUT();
    void generateIBLCubemaps();

    // Pipeline creation
    void createAllPipelines();

    // Resize
    void cleanupSwapChain();
    void recreateSwapChain();
    // 스왑체인은 그대로 두고 renderExtent 의존 씬 타깃(HDR·깊이·AO·Bloom)만
    // 재생성합니다. setViewportRect 가 뷰포트 변경을 감지했을 때 호출합니다.
    void recreateRenderTargets();

    // ── RenderView — 한 프레임의 렌더 입력 스냅샷 (docs/RendererRefactor.md §4) ──
    // extractRenderView()가 World를 읽는 유일한 지점. 이후 패스 기록은 view만 소비한다.
    // v1: 추출 과정에서 프레임별 mapped 버퍼(UBO/라이트/트랜스폼) 업로드까지 수행.
    //     시뮬레이션/렌더 스레드 분리 시 업로드를 기록 측으로 옮기면 된다.
    struct RenderView {
        std::vector<GpuDrawCmd>             opaqueDraws;
        std::vector<GpuDrawCmd>             transpDraws;       // CPU 정렬 완료
        // 섀도 캐스터 드로우 — **라이트 프러스텀**으로 걸러진 별도 목록(DEF-27). 카메라
        // 프러스텀 목록을 재사용하면 카메라 밖·라이트 안의 캐스터가 그림자를 못 던진다.
        // firstInstance 는 opaque/transp 와 같은 transform SSBO 행이라 그대로 그린다.
        std::vector<GpuDrawCmd>             shadowDraws;
        std::vector<GeometryPass::DrawItem> opaqueItems;       // 머티리얼 포함 CPU 드로우
        std::vector<GeometryPass::DrawItem> transparentItems;
        // transform SSBO 행 → 머티리얼. 인덱스가 곧 GpuDrawCmd::firstInstance이므로
        // 드로우 커맨드에서 O(1)로 머티리얼을 역참조할 수 있다. buildDrawCommands가
        // 행을 할당하는 바로 그 지점에서 채우므로 두 목록이 어긋날 수 없다.
        std::vector<const MaterialHandle*>  materialRows;
        AOPass::CameraParams                aoCam;
    };
    RenderView extractRenderView(World& world);

    // Per-frame logic (extractRenderView 내부에서만 호출)
    // 라이트 개수는 updateLightSSBO가 실제로 채운 값을 반환하고 updateUBO가 그것을 받는다.
    // 양쪽이 각자 세면 kMaxLights에서 잘린 실제 개수와 UBO가 알려주는 개수가 어긋나,
    // pbr.frag가 SSBO 범위 밖을 읽는다. 계산 지점을 하나로 두는 것이 이 구조의 요점이다.
    void     updateUBO(World& world, uint32_t lightCount);
    uint32_t updateLightSSBO(World& world);   // 실제로 업로드한 라이트 수를 반환
    // 드로우 커맨드와 transform SSBO 행의 유일한 생성 지점.
    // opaqueOut/transpOut의 firstInstance와 materialRowsOut의 인덱스는 같은 카운터에서
    // 나오므로 세 출력은 항상 정합한다. CPU 드로우 항목은 이 출력에서 파생시킬 것.
    void buildDrawCommands(World& world,
                          std::vector<GpuDrawCmd>& opaqueOut,
                          std::vector<GpuDrawCmd>& transpOut,
                          std::vector<const MaterialHandle*>& materialRowsOut,
                          std::vector<GpuDrawCmd>& shadowOut);

    // Low-level helpers — RenderContext로 위임 (1단계 마이그레이션)
    VkShaderModule createShaderModule(const std::vector<char>& code) {
        return rc_.createShaderModule(code);
    }
    VkPipeline createFullscreenPipeline(VkRenderPass rp, uint32_t subpass,
                                        const char* fragSpv,
                                        VkPipelineLayout layout,
                                        bool enableBlend = false) {
        return rc_.createFullscreenPipeline(rp, subpass, fragSpv, layout, enableBlend);
    }
    VkImage createImage(uint32_t w, uint32_t h, uint32_t mips,
                        uint32_t layers, VkSampleCountFlagBits samples,
                        VkFormat fmt, VkImageTiling tiling,
                        VkImageUsageFlags usage, VkImageCreateFlags flags,
                        HarukaAllocation& outAlloc) {
        return rc_.createImage(w, h, mips, layers, samples, fmt, tiling, usage, flags, outAlloc);
    }
    VkImageView createImageView(VkImage img, VkFormat fmt,
                                VkImageAspectFlags aspect,
                                uint32_t mips = 1, uint32_t layers = 1,
                                VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D) {
        return rc_.createImageView(img, fmt, aspect, mips, layers, viewType);
    }
    void transitionImage(VkCommandBuffer cmd, VkImage img,
                         VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                         VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                         VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                         uint32_t mips = 1, uint32_t layers = 1) {
        RenderContext::transitionImage(cmd, img, oldLayout, newLayout,
                                       srcStage, srcAccess, dstStage, dstAccess,
                                       aspect, mips, layers);
    }
    void            freeTexSlot(TexSlot slot);

    uint32_t findMemType(uint32_t filter, VkMemoryPropertyFlags flags) {
        return rc_.findMemType(filter, flags);
    }

    bool                     checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    bool                     isDeviceSuitable(VkPhysicalDevice dev);
    bool                     checkDeviceExtensionSupport(VkPhysicalDevice dev);
    QueueFamilyIndices       findQueueFamilies(VkPhysicalDevice dev);
    SwapChainSupport         querySwapChainSupport(VkPhysicalDevice dev);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT*, void*);
};
