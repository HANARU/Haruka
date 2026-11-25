#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <volk.h>
#include <vector>
#include <optional>
#include <string>
#include <memory>
#include <array>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "Vertex.hpp"
#include "MeshHandle.hpp"
#include "MaterialHandle.hpp"
#include "ISwapchain.hpp"
#include "RenderContext.hpp"
#include "RenderTypes.hpp"
#include "RenderSceneInput.hpp"
#include "FrameResources.hpp"
#include "GpuScene.hpp"
#include "passes/BloomPass.hpp"
#include "passes/AOPass.hpp"
#include "passes/ShadowPass.hpp"
#include "passes/PostPass.hpp"
#include "passes/GeometryPass.hpp"
#include "passes/UIPanelPass.hpp"
#include "../platform/IWindow.hpp"
#include "../memory/HarukaAllocator.hpp"
#include "VulkanDeviceHooks.hpp"
#include "VulkanBaseline.hpp"
#include "RenderExtensionRegistry.hpp"
#include "WindowTargetLogic.hpp"
#include "../core/EngineSettings.hpp"

class HSlateRenderer;
class HSlateContext;
struct TexSourceData;
struct WindowTarget;   // 보조 OS 창 출력 하나(Renderer_window.cpp, 다중 OS 창 M2)

inline const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
// 장치 확장 목록은 장치마다 hvk::planDevice 가 정합니다(VulkanBaseline.hpp, 2026-10-31). 창이 없으면 VK_KHR_swapchain 을
// 켜지 않습니다.
#ifdef NDEBUG
    constexpr bool kEnableValidation = false;
#else
    constexpr bool kEnableValidation = true;
#endif

struct SwapChainSupport {
    VkSurfaceCapabilitiesKHR        caps;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   modes;
};

// IRenderExtensionHost: 게임 모듈이 EngineContext 로 받는 렌더 확장 등록 통로(DEF-18).
class Renderer : public IRenderExtensionHost {
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
    // dropped = 카메라에 보이지만 행 예산(render.maxDrawRows)이 차서 그리지 못한 메시 수. culled 와 따로
    // 셉니다. 예전에는 조용히 잘린 메시가 culled 에 섞여 보였습니다(DEF-10).
    // drawCalls 는 눈 하나가 내는 불투명 드로우 수이고, instances 는 그 드로우들이 그리는 메시 수입니다.
    // 같은 지오메트리 · 머티리얼의 메시는 드로우 하나로 묶이므로(DEF-16) drawCalls <= instances 입니다.
    // lights 는 이번 프레임에 올린 라이트 수, lightsTotal 은 켜진 라이트 수입니다. 그 차이는 어느 눈에도 영향이
    // 없어 거른 것과 상한을 넘어 뺀 것입니다(DEF-33).
    struct FrameStats  { uint32_t drawCalls=0, instances=0, culled=0, transparentDraws=0, dropped=0,
                         lights=0, lightsTotal=0; };

    // 보조 창 목록(WindowTarget)이 불완전 형이라 생성 · 소멸을 Renderer_window.cpp 에 둡니다.
    Renderer();
    ~Renderer();
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // deviceHooks 가 있으면 Vulkan 인스턴스 · 장치를 그쪽(OpenXR)에 맡깁니다. 헤드셋이 고른 GPU 가
    // 창에 그릴 수 없으면 직접 고르고 xrCompatible() 이 false 가 됩니다.
    // 그리기 행 · 라이트 상한(DEF-10, Config/engine.ini). init 전에 부릅니다. 부르지 않으면 기본값입니다.
    void setLimits(uint32_t maxDrawRows, uint32_t maxLights) { maxDrawRows_ = maxDrawRows; maxLights_ = maxLights; }
    // --vk-no-sync2: 장치가 synchronization2 를 지원해도 1.0 배리어로 기록합니다(Quest 의 대체 경로를 PC 에서 시험, DEF-04).
    // init 전에 부릅니다.
    void setForceNoSync2(bool on) { forceNoSync2_ = on; }
    // 켤 블록 압축 기능(Docs/Texture_Compression.md D10). 그 실행의 텍스처 대상이 쓰는 것만 켭니다. init 전에 부릅니다.
    void setTextureCompression(bool bc, bool astc) { wantTexBC_ = bc; wantTexASTC_ = astc; }
    // --vk-dedicated-msaa: MSAA 컬러를 핵심 힙 밖 별도 할당으로(지연 할당 타입이 없는 데스크톱에서 DEF-06 경로 시험).
    void setForceDedicatedMsaa(bool on) { forceDedicatedMsaa_ = on; }
    // DEF-183 진단 인자: --debug-post · --no-lazy-msaa · --msaa=1|2|4
    void setDebugPost(const std::string& mode);
    void setNoLazyMsaa(bool on) { noLazyMsaa_ = on; }
    void setForcedMsaa(int samples) { forcedMsaa_ = samples; }
    // --gpu=N: vkEnumeratePhysicalDevices 순서의 N 번째 GPU 를 씁니다(-1 = 처음 쓸 수 있는 것). init 전에 부릅니다.
    void setPreferredGpu(int index) { preferredGpu_ = index; }
    // --no-multiview: 두 눈의 메인 패스를 멀티뷰로 한 번에 그리지 않고 눈마다 그립니다(진단 · 비교용, 2026-11-18).
    // 장치의 멀티뷰 기능은 그래도 켭니다(셰이더가 gl_ViewIndex 를 씀). 언제든 부를 수 있고 다음 프레임부터 반영됩니다.
    void setMultiview(bool on) { multiview_ = on; }
    bool multiview() const     { return multiview_; }
    // 렌더 티어(M1): init 전에 한 번 설정하고 실행 중에는 바꾸지 않습니다.
    void       setRenderTier(RenderTier tier) { renderTier_ = tier; }
    RenderTier renderTier() const             { return renderTier_; }
    // 후처리 패스 켬/끔(project.ini render.ao · render.bloom, --ssao · --no-ssao). 끈 패스는 기록하지 않습니다.
    // 프레임마다 읽으므로 init 앞뒤 어느 때 불러도 됩니다. 프레임 단위 값입니다(블룸 이미지 한 벌을 두 눈이
    // 함께 쓰므로 눈마다 다르게 두지 않음).
    void setPostFeatures(bool ao, bool bloom) { aoOn_ = ao; bloomOn_ = bloom; }
    // 품질 단계(2026-11-18, 성능 안정화 2-2). project.ini 의 render.* 품질 키(플랫폼 접미사 .android 로 Quest 값)에서 옵니다.
    // init 전에 부릅니다(섀도 단 수 · 거리는 섀도 패스를 만들 때 정해짐). 기본값은 PC 화면 그대로입니다.
    struct Quality {
        int      csmCascades       = 2;      // 1 = 먼 단만(섀도 패스 하나)
        float    shadowDistance    = 20.f;   // 먼 단 반지름(m, hshadow::kRadius)
        bool     contactShadows    = true;   // 끄면 라이트의 Contact Shadow Length 를 무시
        bool     heightFog         = true;   // 끄면 HeightFogComponent 를 무시
        bool     reflectionCapture = true;   // 끄면 반사 캡처를 찍지 않고 하늘 IBL 만
        uint32_t lightLimit        = 0;      // 0 = render.maxLights 그대로, 아니면 둘 중 작은 값
    };
    void setQuality(const Quality& q);
    const Quality& quality() const { return quality_; }
    void setAoEnabled(bool on) { aoOn_ = on; }
    void setBloomEnabled(bool on) { bloomOn_ = on; }
    bool aoEnabled() const     { return aoOn_ && msaaSamples_ != VK_SAMPLE_COUNT_1_BIT && renderTier_ != RenderTier::Mobile; }
    bool bloomEnabled() const  { return bloomOn_; }
    int  msaaSampleCount() const { return static_cast<int>(msaaSamples_); }
    struct StartupTimings {
        double deviceMs    = 0.0;
        double allocMs     = 0.0;
        double swapchainMs = 0.0;
        double poolCacheMs = 0.0;
        double layoutsMs   = 0.0;
        double shadowMs    = 0.0;
        double sceneMs     = 0.0;
        double aoMs        = 0.0;
        double iblMs       = 0.0;
        double errMatMs    = 0.0;
        double geomMs      = 0.0;
        double postMs      = 0.0;
        double pipelinesMs = 0.0;
        double framesMs    = 0.0;
    };
    const StartupTimings& startupTimings() const { return startupTimings_; }

    void init(IWindow* window, IVulkanDeviceHooks* deviceHooks = nullptr);
    void precommitHeap() { allocator.precommit(); }
    void cleanup();
    // xr 이 있으면 두 눈을 XR 스왑체인의 두 층에 그리고, 에디터 뷰포트에는 왼쪽 눈을 비춥니다.
    // 그 프레임에 XR 이미지에 실제로 그렸으면 true(엔진이 투영 레이어를 제출할지 정함).
    bool drawFrame(const RenderSceneInput& scene, const XrFrameTarget* xr = nullptr);

    // ── XR 연결 ───────────────────────────────────────────────────────────────
    bool             xrCompatible() const     { return xrCompatible_; }
    VkInstance       getInstance() const      { return instance; }
    // 장치를 고를 때 한 번 정한 값입니다. 표면 조회를 다시 하지 않습니다(창이 없으면 표면 함수를 부를 수 없음).
    uint32_t         graphicsQueueFamily() const { return queueFamilies_.graphics.value(); }
    // XR 스왑체인(두 층 배열)의 이미지를 렌더 대상으로 붙입니다. 세션을 만든 뒤 한 번. 씬 타깃을
    // 눈 크기로 다시 만듭니다.
    void attachXrSwapchain(const std::vector<VkImage>& images, VkFormat format, VkExtent2D eyeExtent);
    // 떼고 씬 타깃을 뷰포트 크기로 되돌립니다. 장치가 쉬기를 기다린 뒤 이미지 뷰를 지우므로, 그 뒤에
    // XR 스왑체인을 지워도 됩니다.
    void detachXrSwapchain(bool restoreTargets = true);
    bool xrAttached() const { return xrActive_; }

    // ── 창 없는 실행 (2026-10-31, 패키징 2단계) ───────────────────────────────
    // 창이 표면을 주지 않으면(--headless · Quest) 스왑체인 없이 XR 이미지에만 그립니다.
    bool headless() const { return headless_; }
    // 검증 레이어가 보고한 ERROR 수(Debug 빌드에서 레이어가 켜졌을 때만 증가). 창 없는 실행의 종료 코드에 씁니다.
    static uint32_t validationErrorCount();
    // 이번 실행에서 검증 레이어가 실제로 활성화되었는지(Debug라도 시스템에 레이어가 없으면 false).
    static bool     isValidationActive();
    // 마지막 drawFrame 이 데스크톱 창에 그렸는지.
    bool lastFrameDesktop() const { return lastFrameDesktop_; }
    // 헤드셋 없이 XR 프레임 경로를 돌리는 합성 눈(--synthetic-eyes). 눈 크기 · 두 층 이미지 count 장을 만들어 XR 스왑체인처럼
    // 붙입니다. 엔진은 이 이미지 번호와 hxr::syntheticEyes 의 두 눈으로 drawFrame 을 부릅니다. XR 세션과 함께 쓰지 않습니다.
    bool     attachSyntheticEyes(VkExtent2D eyeExtent, uint32_t count = 3);
    void     detachSyntheticEyes();
    uint32_t syntheticImageCount() const { return (uint32_t)synthImages_.size(); }
    // 합성 눈 이미지 하나의 눈 층을 RGBA8 로 읽어 옵니다(장치가 쉬기를 기다림). 실제 XR 이미지는 읽을 수 없습니다.
    bool     captureXrEye(uint32_t image, uint32_t layer, std::vector<uint8_t>& rgba, VkExtent2D& extent);
    // 마지막으로 그린 데스크톱 스왑체인 이미지를 RGBA8(sRGB 바이트 그대로)로 읽어 옵니다.
    // 창을 닫기 전 마지막 프레임의 present 직전에 복사해 둔 결과를 돌려줍니다.
    void     setCaptureNextSwapchain(bool on) { captureNextSwapchain_ = on; }
    bool     captureSwapchain(std::vector<uint8_t>& rgba, VkExtent2D& extent, bool& unsupported);

    // Hot-reload shaders
    void reloadPipelines();

    // Asset upload — called by AssetManager (GpuScene으로 위임)
    MeshHandle* uploadMesh(const std::vector<Vertex>& verts,
                           const std::vector<uint32_t>& indices,
                           const MeshBounds* precomputed = nullptr, bool preOrdered = false) {
        return scene_.uploadMesh(verts, indices, precomputed, preOrdered);
    }
    MeshHandle* uploadLodIndices(const MeshHandle& base, const std::vector<uint32_t>& indices, bool preOrdered = false) {
        return scene_.uploadLodIndices(base, indices, preOrdered);
    }
    GpuScene::MeshBudget meshBudget() const { return scene_.budget(); }
    MaterialHandle* uploadMaterial(
        const TexSourceData& albedo, const TexSourceData& normalMap,
        const TexSourceData& metalRough, const TexSourceData& ao,
        const TexSourceData& emissive) {
        return scene_.uploadMaterial(albedo, normalMap, metalRough, ao, emissive);
    }
    // 지연 해제(비동기 로딩 D4): 그리던 프레임이 끝난 뒤(MAX_FRAMES 뒤) GpuScene 이 지웁니다.
    void freeMesh(MeshHandle* h)         { scene_.retireMesh(h, frameNum_); }
    void freeMaterial(MaterialHandle* h) { scene_.retireMaterial(h, frameNum_); }
    size_t retiredResources() const { return scene_.retiredCount(); }
    size_t pendingUploads() const   { return scene_.pendingUploads(); }

    IWindow* getWindow()     const { return window;     }
    VkDevice getDevice()     const { return device;     }
    VkPhysicalDevice getPhysDevice() const { return physDevice; }
    // 장치에서 받은 vkCmdPushDescriptorSetKHR(DEF-97). 렌더러 밖의 UI(HSlateRenderer)가 씁니다.
    PFN_vkCmdPushDescriptorSetKHR pushDescriptorFn() const { return rc_.vk.push; }
    // 창이 없으면 빈 스왑체인의 명목 포맷(R8G8B8A8_SRGB)입니다. XR 세션의 선호 포맷으로도 씁니다.
    VkFormat getSwapFormat() const { return swapchain_->format(); }

    void setUIRenderer(HSlateRenderer* r);

    // ── 보조 OS 창 출력 (다중 OS 창 M2, 2026-11-21, Docs/Editor_MultiWindow_Design.md D2 · D3) ─────────────
    // 3D 화면 없이 HSlate UI 만 그리는 창입니다. 메인 창 경로(swapchain_)는 그대로이고, 보조 창은 이 목록으로 따로 다룹니다.
    // 목록이 비어 있으면 drawFrame 의 보조 창 코드는 모두 건너뜁니다(게임 실행 파일 · Android · 창 없는 실행 · XR 은 늘 비어 있음).
    // 에디터만 부릅니다. 그리기는 setUIRenderer 로 받은 HSlateRenderer(글꼴 아틀라스 · 정점 버퍼 · 파이프라인 공유)가 합니다.
    //  - 프레임 펜스 · 제출은 메인과 하나를 같이 씁니다. 창마다 acquire 세마포어(슬롯별) · 표시 세마포어 · 이미지별 펜스를 둡니다.
    //  - acquire 는 타임아웃 0 입니다. 준비된 이미지가 없으면 그 창은 그 프레임을 건너뜁니다(메인 루프를 그 창 주사율에 묶지 않음).
    //  - 표시 방식은 MAILBOX → IMMEDIATE → FIFO, 표시는 창마다 따로(메인 먼저), 실패하면 그 창만 다시 만듭니다.
    //  - 그리기 목록 해시가 직전에 표시한 것과 같으면 acquire · 기록 · 표시를 모두 건너뜁니다. 최소화한 창도 건너뜁니다.
    //  - 크기가 바뀌면 그 창의 스왑체인 · UI 프레임버퍼만 다시 만듭니다(장치 대기). 메인 recreateSwapChain 은 쓰지 않습니다.
    // 표면이 메인 표시 큐 패밀리로 표시할 수 없거나, 스왑체인 형식이 UI 렌더러 형식과 다르면 만들지 않고 경고한 뒤 0 을
    // 돌려줍니다. 창 없는 실행 · UI 렌더러가 없을 때도 0 입니다.
    uint32_t addWindowTarget(IWindow* win);
    uint32_t windowTargetCount() const;
    // 장치가 쉬기를 기다린 뒤 프레임버퍼 · 세마포어 → 스왑체인 → 표면 순으로 없앱니다. OS 창(IWindow)은 부른 쪽이 **그 뒤에**
    // 없앱니다(표면이 창보다 먼저 사라져야 함). 이벤트 펌프 밖(프레임 사이)에서 부릅니다. 없는 번호는 무시합니다.
    void     removeWindowTarget(uint32_t id);
    // 그릴 문맥. nullptr 이면 그 창은 그리지 않습니다(이미 표시한 화면이 남음). 문맥은 창보다 오래 살아야 합니다.
    void     setWindowUi(uint32_t id, HSlateContext* ctx);
    // 해시가 같아도 다음 프레임에 다시 그리게 합니다(예: 창이 가려졌다 드러남을 OS 가 알려 줄 때).
    void     requestWindowRedraw(uint32_t id);
    struct WindowTargetStats {
        uint64_t presented = 0;          // 표시 요청이 받아들여진 횟수
        uint64_t acquired  = 0;          // 이미지를 받은 횟수
        uint64_t skippedUnchanged = 0;   // 해시가 같아 건너뜀
        uint64_t skippedNotReady  = 0;   // 타임아웃 0 acquire 가 이미지를 못 받음
        uint64_t skippedNoArea    = 0;   // 최소화
        uint64_t recreated        = 0;   // 스왑체인을 다시 만든 횟수(처음 만들기 제외)
        uint64_t budgetCleared    = 0;   // 정점 예산 때문에 지우기만 함
    };
    // 없는 번호면 모두 0 입니다.
    WindowTargetStats windowTargetStats(uint32_t id) const;

    // ── 월드 UI 패널 출력 (3D UI P1a, UIPanelPass.hpp) ─────────────────────────
    // 월드의 UIPanelComponent 는 렌더러가 프레임마다 모아 텍스처를 맞추고 그립니다(gatherUIPanels). 아래는 그 밖의
    // 용도(시험 · 진단)입니다. 패널 UI 는 에디터가 있으면 에디터의 HSlateRenderer 를 같이 쓰고(글꼴 아틀라스 · 정점 버퍼
    // 공유), 없으면(게임 실행 파일) 처음 쓸 때 하나 만듭니다.
    uint32_t    createUIPanelTarget(uint32_t width, uint32_t height);   // 실패 · 패널 UI 없음이면 0
    void        destroyUIPanelTarget(uint32_t handle);
    const UIPanelPass& uiPanels() const { return panels_; }

    // ── 빌보드 파티클 (2026-11-20, Docs/Particles_And_Decals.md) ──────────────────────────
    // 직전 프레임에 그린 파티클 수(상한에서 잘린 뒤)와 드로 수. 시험 · 측정용입니다.
    uint32_t    particleCount() const     { return particleList_.count; }
    uint32_t    particleDrawCount() const { return (uint32_t)particleRanges_.size(); }
    uint32_t    decalCount() const        { return decalCount_; }

    // 씬을 그릴 3D 뷰포트 rect(프레임버퍼 픽셀)를 지정합니다. 에디터가 매 프레임 호출하며,
    // 값이 바뀔 때만 renderExtent 타깃을 재생성합니다(Viewport_Render_Design.md). 지정하지
    // 않으면 창 전체로 렌더합니다(게임 빌드). drawFrame 이전(에디터 tick)에서 호출해야 합니다.
    void setViewportRect(int x, int y, int w, int h);

    // 스테레오 흉내 모드(헤드셋 없이 두 눈 경로를 확인하는 용도, UE 의 -emulatestereo 와 같음).
    // 켜면 뷰포트를 좌우로 나눠 왼쪽 눈 · 오른쪽 눈을 그립니다. 두 눈은 카메라 좌우로 64mm 떨어진
    // 평행한 눈이고 시야는 비대칭입니다. 에디터의 피킹 · 기즈모는 단안 기준이라 이 모드에서는
    // 맞지 않습니다. drawFrame 이전(에디터 tick)에서 바꿔야 합니다.
    void setStereoEmulation(bool on);
    bool stereoEmulation() const { return emulateStereo_; }

    void setEditorLines(std::vector<LineVertex> lines) { geometry_.setLines(std::move(lines)); }
    void setGizmoTris  (std::vector<LineVertex> tris)  { geometry_.setGizmoTris(std::move(tris)); }
    // 월드 UI 포인터의 레이저 · 커서(3D UI 구간 6). 엔진이 프레임마다 넣고, 없으면 빈 목록을 넣습니다.
    void setPointerLines(std::vector<LineVertex> lines) { geometry_.setPointerLines(std::move(lines)); }

    // 셰이더 시간(FrameUBO.timeParams.x)을 엔진 틱 시간만큼 진행합니다(수학 보강 A2, 2026-11-17). 엔진이 drawFrame 전에
    // 루프마다 부릅니다. 예전에는 프레임 번호 ÷ 60 이라 90 Hz 헤드셋에서 1.5배 빨리 흘렀습니다. double 로 쌓고 셰이더에는
    // kShaderTimeWrap 초로 접어 넘깁니다(float 간격이 접기 직전 약 0.24 ms, 접는 순간 한 번 0 으로 돌아감).
    static constexpr double kShaderTimeWrap = 3600.0;
    void   advanceTime(float dt) { if (dt > 0.f) timeSec_ += dt; }
    double timeSeconds() const { return timeSec_; }
    float  shaderTime() const { return (float)std::fmod(timeSec_, kShaderTimeWrap); }

    VkCommandBuffer beginOneTimeCmd()                { return rc_.beginOneTimeCmd(); }
    void            endOneTimeCmd(VkCommandBuffer c) { rc_.endOneTimeCmd(c); }

    AllocStats getAllocStats() const { return allocator.getStats(); }
    uint32_t   coreHeapCommittedChunks() const { return allocator.committedChunkCount(); }
    uint64_t   coreHeapCommittedBytes()  const { return allocator.allocatedSize(); }
    // 머티리얼 텍스처 메모리(Docs/Texture_Compression.md D12). rgbaBytes = 같은 텍스처를 RGBA8 + 밉으로 올렸을 때.
    struct TextureMemory {
        uint32_t textures = 0, compressed = 0;
        uint64_t bytes = 0, rgbaBytes = 0, zoneBUsed = 0, zoneBTotal = 0;
    };
    TextureMemory textureMemory() const {
        const GpuScene::TexStats& t = scene_.texStats();
        return { t.textures, t.compressed, t.bytes, t.rgbaBytes, allocator.zoneBUsed(), kZoneBSize };
    }
    GpuTimings getTimings()   const { return timings_;    }
    FrameStats getFrameStats()const { return frameStats_; }

    // ── 렌더 확장 (DEF-17 · 18, RenderExtension.hpp) ─────────────────────────
    void addRenderExtension(std::shared_ptr<IRenderExtension> ext) override;
    void removeRenderExtension(IRenderExtension* ext) override;

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
    VkQueue                  presentQueue   = VK_NULL_HANDLE;   // 창이 없으면 VK_NULL_HANDLE

    // ── 장치 계획 (VulkanBaseline.hpp, 2026-10-31) ────────────────────────────
    bool               headless_ = false;   // 창이 표면을 주지 않음
    bool               forceNoSync2_ = false;   // --vk-no-sync2
    bool               wantTexBC_    = false;
    bool               wantTexASTC_  = false;
    bool               forceDedicatedMsaa_ = false;   // --vk-dedicated-msaa
    int                preferredGpu_ = -1;            // --gpu=N
    bool               multiview_ = true;             // --no-multiview 면 거짓(setMultiview)
    RenderTier         renderTier_ = RenderTier::Desktop;
    bool               afterSceneMobileWarned_ = false;
    int                stereoMainPassMode_ = -1;      // 직전 두 눈 프레임의 메인 패스(1 멀티뷰 · 0 눈마다, 로그용)
    Quality            quality_;                      // 품질 단계(setQuality)
    // SSAO · 블룸 켜기의 유일한 출처. postParams() 가 PostPass push 상수와 FrameUBO(screenParams.w ·
    // miscParams.z)에 같은 값을 넣습니다.
    bool               aoOn_    = true;
    bool               bloomOn_ = true;
    PostPass::Params   postParams() const;   // 이번 프레임의 합성 값(세기 · 노출 · 켬/끔)
    float              cameraExposureEv_ = 0.f;   // 활성 카메라의 노출 보정(gatherViews 가 채움)
    float              cameraVignette_    = 0.f;  // 접근성 비네트 · 색 보정(gatherViews 가 채움, Docs/Accessibility.md)
    int                cameraColorFilter_ = 0;
    float              contactLength_    = 0.f;   // 섀도 캐스터 방향광의 접촉 그림자 길이(updateLightSSBO 가 채움, 0 = 끔)
    hvk::DeviceCaps    deviceCaps_;         // 고른 장치에서 읽은 값
    hvk::DevicePlan    devicePlan_;         // 켤 확장 · 기능(장치 생성과 샘플러가 함께 읽음)
    hvk::QueueFamilies queueFamilies_;      // 고를 때 한 번 정함

    // ── Swapchain (platform-abstracted) ──────────────────────────────────────
    std::unique_ptr<ISwapchain> swapchain_;
    bool     lastFrameDesktop_           = false;
    bool     captureNextSwapchain_       = false;
    bool     lastSwapCaptured_           = false;
    bool     lastSwapUnsupported_        = false;
    bool     swapCaptureUnsupportedLogged_ = false;
    VkExtent2D           lastSwapExtent_ = {};
    std::vector<uint8_t> lastSwapRgba_;
    bool     nothingToDrawLogged_        = false;   // 창도 XR 대상도 없음(한 번만 알림)

    // ── 합성 눈 (--synthetic-eyes) ────────────────────────────────────────────
    std::vector<VkImage>          synthImages_;
    std::vector<HarukaAllocation> synthAllocs_;

    // ── MSAA 샘플 수 (rc_로 전파) ─────────────────────────────────────────────
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_4_BIT;
    VkFormat              depthFormat  = VK_FORMAT_UNDEFINED;
    int                   debugPostMode_ = 0;   // DEF-183: 0=보통, 1=solid, 2=raw, 3=nobloom, 4=nan
    bool                  noLazyMsaa_    = false;
    int                   forcedMsaa_    = 0;   // 0=자동, 1|2|4=강제

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

    // ── 패스 레지스트리 (DEF-17) ──────────────────────────────────────────────
    // 기본 패스 다섯(프레임 순서: shadow → geometry → ao → bloom → post)과 등록된 확장을 한 목록으로
    // 다룹니다. 크기 변경 · 핫 리로드 · 파괴는 이 목록을 돌며, 경로마다 패스를 손으로 열거하지 않습니다.
    RenderExtensionRegistry passRegistry_;

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

    // ── 반사 캡처(2026-11-17, Renderer_capture.cpp) ─────────────────────────────
    static constexpr uint32_t kCaptureRes       = 128;   // 면마다 읽어 오는 해상도
    // 면마다 그리는 정사각형 변(2×2 를 평균해 kCaptureRes). 예전에는 렌더 크기 전체(VR 은 눈마다 약 1800²)를 그리고 가운데를
    // 줄였습니다. 렌더 크기가 이보다 작으면 kCaptureRes 로 그립니다.
    static constexpr uint32_t kCaptureDrawRes   = 256;
    static constexpr uint32_t kCapturePrefSize  = 64;    // 프리필터 mip 0(하늘과 같음)
    static constexpr uint32_t kCapturePrefMips  = 5;
    static constexpr float    kCaptureNear      = 0.05f;
    static constexpr float    kCaptureFar       = 200.f;
    struct CaptureKey {
        uint64_t actor = 0; glm::vec3 pos{ 0.f }; float radius = 0.f; uint32_t version = 0; const void* world = nullptr; bool valid = false;
        bool operator==(const CaptureKey& o) const {
            return valid == o.valid && actor == o.actor && pos == o.pos && radius == o.radius && version == o.version && world == o.world;
        }
    };
    void updateReflectionCapture(const RenderSceneInput& scene);
    // 파티클: 이미터들을 눈 가운데 기준으로 정렬해 한 줄로 잇고(ParticleOrder), 모양 아틀라스를 처음 쓸 때 올립니다.
    void gatherParticles(const RenderSceneInput& scene);
    TexSlot                            particleAtlas_;
    std::vector<vfx::ParticleInstance> particleInst_;
    std::vector<ParticleRange>         particleRanges_;
    ParticleOrderScratch               particleScratch_;
    GeometryPass::ParticleList         particleList_;
    uint32_t                           decalCount_ = 0;
    void performReflectionCapture(const RenderSceneInput& scene, const glm::vec3& pos);
    CaptureKey       captureKey_;
    glm::vec4        capturePos_{ 0.f };      // xyz 위치, w 반경(0 = 없음) → FrameUBO.capturePos
    float            captureIntensity_ = 1.f;
    bool             captureOverride_  = false;   // 캡처 중: gatherViews 가 captureEye_ 하나만 씀
    EyeView          captureEye_;
    VkImage          captureImage_ = VK_NULL_HANDLE;
    VkImageView      captureView_  = VK_NULL_HANDLE;
    HarukaAllocation captureAlloc_;
    bool             captureEverUploaded_ = false;

    // ── 자산 GPU 자원 (메가버퍼 + 텍스처 슬롯) ────────────────────────────────
    GpuScene scene_;

    // 머티리얼이 없는 메시를 그릴 때 쓰는 오류 머티리얼(자홍색, 스스로 빛남). 빠진 메시가 눈에
    // 띄도록 일부러 튀는 색입니다. 엔진이 그럴듯한 값을 대신 채우지 않는다는 정책과 같은
    // 방향입니다(DEF-28). 액터마다 한 번만 경고합니다.
    MaterialHandle*              errorMaterial_ = nullptr;
    std::unordered_set<uint64_t> noMaterialWarned_;
    struct LodState {
        const MeshHandle* mesh    = nullptr;
        int               current = 0;
        uint64_t          stamp   = 0;
    };
    std::unordered_map<uint64_t, LodState> lodStates_;
    uint64_t                               lodStamp_ = 0;
    struct PanelState {
        uint32_t   handle = 0;
        glm::uvec2 size{ 0, 0 };
        uint64_t   stamp  = 0;
    };
    std::unordered_map<uint64_t, PanelState> panelStates_;
    uint64_t                                 panelStamp_ = 0;
    bool                         dropWarned_    = false;   // 행 예산 초과 경고(한 번)
    bool                         skinOverflowWarned_ = false;   // 스킨 메시 정점 구간 초과 경고(한 번, 2026-11-07)
    std::vector<Vertex>          skinScratch_;   // CPU 스키닝 결과(GPU 형식으로 옮기기 전, QTangent C2)
    bool                         lightBudgetWarned_ = false;   // 라이트 상한 초과 경고(한 번, DEF-33)
    uint32_t                     maxDrawRows_   = EngineLimits{}.maxDrawRows;   // setLimits(DEF-10)
    uint32_t                     maxLights_     = EngineLimits{}.maxLights;

    // FFR(고정 포비티드 렌더링) 멤버는 지웠습니다. 선언만 있고 쓰이지 않았고, OpenXR 에서는
    // 런타임이 밀도 맵 이미지를 주는 구조라 모양이 맞지 않습니다(DEF-09 조사). Quest 이식 때 새로 둡니다.

    // ── 이번 프레임에 그릴 뷰(눈) ─────────────────────────────────────────────
    // gatherViews 가 extractRenderView 첫머리에서 채웁니다. 단안 1개, 스테레오 2개(0=왼쪽).
    // 컬링 · UBO · 패스 기록이 모두 이 한 벌을 씁니다. 그래서 한 프레임 안에서 컬링 · 렌더 · 제출이
    // 같은 자세를 쓰고, 재투영 여유분(DEF-32)이 필요 없습니다.
    std::array<EyeView, kMaxViews> views_{};
    uint32_t viewCount_     = 1;
    bool     hasCamera_     = false;
    bool     emulateStereo_ = false;
    void       gatherViews(const RenderCamera& cam, const XrFrameTarget* xr);
    VkExtent2D desiredRenderExtent() const;   // 뷰포트와 모드로 정한 눈 하나의 렌더 크기
    // 장면 타깃 층 수(rc_.sceneLayers)를 XR · 스테레오 흉내 상태에 맞춥니다. 바뀌었으면 참(부른 쪽이 타깃을 다시 만듦).
    bool syncSceneLayers();

    // ── XR 출력 ────────────────────────────────────────────────────────────────
    // XR 스왑체인 이미지 i 의 눈 e 층을 가리키는 뷰 · 프레임버퍼는 [i * kMaxViews + e] 에 있습니다.
    bool                       xrCompatible_ = false;   // 장치를 XR 런타임이 정했음
    bool                       xrActive_     = false;   // XR 스왑체인이 붙어 있음
    VkFormat                   xrFormat_     = VK_FORMAT_UNDEFINED;
    VkExtent2D                 xrEyeExtent_{};
    std::vector<VkImageView>   xrViews_;
    std::vector<VkFramebuffer> xrFramebufs_;

    // ── 프레임별 GPU 상태 (레이아웃/버퍼/디스크립터/커맨드/동기화) ────────────
    FrameResources frames_;

    // ── Pipeline cache + timestamp ────────────────────────────────────────────
    static constexpr const char* kPipelineCacheFile = "Cache/pipeline_cache.bin";   // 쓰기 루트 기준
    VkPipelineCache pipelineCache_   = VK_NULL_HANDLE;
    // 파이프라인을 새로 만들었음(처음 · 핫 리로드 · XR 출력 연결). 다음 프레임 끝에서 캐시를 저장합니다(DEF-153).
    bool            pipelineCacheDirty_     = true;
    uint64_t        pipelineCacheSavedHash_ = 0;   // 마지막으로 읽거나 쓴 캐시 데이터의 XXH3-64(같으면 쓰지 않음)
    // 프레임당 타임스탬프 슬롯 수입니다. 배치는 Renderer_render.cpp 의 drawFrame 참조.
    //   0 frame start / 1 shadow / 눈 v 마다 2+4v main · 3+4v AO · 4+4v Bloom · 5+4v tonemap /
    //   마지막 UI. 그리지 않은 눈의 칸은 같은 시점에 연달아 찍어 0ms 가 됩니다.
    static constexpr uint32_t kTsPerView         = 4;
    static constexpr uint32_t kTimestampsPerFrame = 2 + kTsPerView * kMaxViews + 1;
    VkQueryPool     timestampPool_   = VK_NULL_HANDLE;
    float           timestampPeriod_ = 1.f;
    GpuTimings      timings_;
    FrameStats      frameStats_;

    // ── Shared samplers ───────────────────────────────────────────────────────
    VkSampler textureSampler = VK_NULL_HANDLE;  // anisotropic, repeat
    VkSampler clampSampler_  = VK_NULL_HANDLE;  // linear, clamp to edge
    VkSampler panelSampler_  = VK_NULL_HANDLE;  // UI 패널: clamp, 밉 선형, 이방성(3D UI P1b)

    // ── Frame counters ────────────────────────────────────────────────────────
    VkCommandPool cmdPool   = VK_NULL_HANDLE;
    uint32_t      frameIdx  = 0;
    uint32_t      frameNum_ = 0;  // monotonically increasing frame counter
    double        timeSec_  = 0.0;   // advanceTime 으로 쌓은 엔진 시간(초)

    // ── 섀도 캐스터 선택 (DEF-26) ─────────────────────────────────────────────
    // updateLightSSBO 가 라이트를 채우면서 섀도를 던지는 라이트(첫 Directional +
    // castShadows)의 **SSBO 슬롯 번호**와 그 월드 방향을 여기 기록합니다. updateUBO 가
    // 같은 값으로 섀도 카메라 VP 와 miscParams.w(셰이더가 이 슬롯에만 그림자를 곱함)를
    // 만들어, 선택 지점과 채우기 지점이 어긋날 수 없습니다. 캐스터가 없으면 -1.
    int           shadowCasterSlot_ = -1;
    glm::vec3     shadowCasterDir_  = {0.f, -1.f, 0.f};
    glm::vec3     viewCenter_{ 0.f };      // 두 눈 위치 평균(단안이면 카메라, 없으면 원점). 라이트 선택 · LOD · 투명 정렬 공용
    glm::mat4     lightViewProj_{ 1.f };   // 섀도 VP. 캐스터가 없는 프레임은 이전 값을 유지(예전 파일 전역 static)
    glm::mat4     lightViewProjNear_{ 1.f };   // 가까운 캐스케이드 VP(CSM, 2026-11-17). 갱신 규칙은 lightViewProj_ 와 같음
    glm::mat4     lightCullViewProj_{ 1.f };   // 섀도 캐스터 컬링 VP(near 를 빛 쪽으로 늘림, ShadowFit.hpp). 갱신 규칙은 위와 같음

    // ── UI overlay ────────────────────────────────────────────────────────────
    HSlateRenderer* uiRenderer_ = nullptr;

    // ── 보조 OS 창 (다중 OS 창 M2, Renderer_window.cpp) ───────────────────────────
    std::vector<std::unique_ptr<WindowTarget>> windowTargets_;
    uint32_t nextWindowTargetId_ = 1;
    // 프레임마다 다시 쓰는 제출 세마포어 목록(메인 + acquire 한 보조 창, 할당은 처음 몇 프레임만).
    hwin::SubmitSemaphores submitSems_;
    WindowTarget* findWindowTarget(uint32_t id) const;
    // 스왑체인 만들기 · 다시 만들기(장치 대기 포함). 실패하면 그 창을 멈추고(failed) false.
    bool rebuildWindowTarget(WindowTarget& t, bool first);
    void destroyWindowTarget(WindowTarget& t);
    void destroyWindowFramebuffers(WindowTarget& t);
    bool buildWindowFramebuffers(WindowTarget& t);
    // drawFrame 안에서: 메인 acquire 뒤 · 프레임 펜스 리셋 전에 창마다 그리기 목록 → 해시 → acquire(타임아웃 0) → 이미지 펜스 대기.
    void prepareWindowTargets(uint32_t fi, VkFence frameFence);
    // UI 기록 자리에서: acquire 한 창을 그 창 프레임버퍼에 기록합니다(아틀라스는 그 전에 한 번 올림).
    void recordWindowTargets(VkCommandBuffer cmd, uint32_t fi);
    // 제출 뒤: acquire 한 창을 하나씩 표시합니다(메인 표시 뒤).
    void presentWindowTargets();
    bool anyWindowAcquired() const;
    // 멈추지 않았고 · 문맥이 있고 · 넓이가 있는 보조 창이 있는지(메인이 최소화된 프레임의 판정, M3a).
    bool anyWindowDrawable() const;
    // 메인 창이 최소화된 비 XR 프레임: 3D · 메인 UI 없이 보조 창 UI 만 기록 · 제출 · 표시합니다(M3a). 받은 창이 없으면 프레임 펜스를
    // 리셋하지 않고 돌아갑니다(다음 프레임이 같은 슬롯을 그대로 씀).
    void drawAuxOnlyFrame();

    // ── 월드 UI 패널 (3D UI P1a) ──────────────────────────────────────────────
    UIPanelPass     panels_;
    HSlateRenderer* panelUi_      = nullptr;   // 패널을 그리는 UI(처음 정한 뒤 바꾸지 않음)
    HSlateRenderer* ownedPanelUi_ = nullptr;   // 에디터가 없을 때 렌더러가 만든 것(cleanup 에서 지움)
    bool            panelUiFailed_ = false;    // 만들기 실패(글꼴 없음 등). 한 번만 알리고 패널을 끔
    HSlateRenderer* panelUi();

    // ── Init helpers ──────────────────────────────────────────────────────────
    IVulkanDeviceHooks* deviceHooks_ = nullptr;   // init 중에만 씁니다
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
    bool readbackImageToRgba(VkImage image, uint32_t layer, VkExtent2D extent, VkFormat format,
                             VkImageLayout currentLayout, VkAccessFlags srcAccess, VkPipelineStageFlags srcStage,
                             std::vector<uint8_t>& rgba);
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
        // 행을 할당하는 바로 그 지점에서 채우므로 두 목록이 어긋날 수 없다. 인스턴스 묶음의
        // 행은 모두 같은 머티리얼이라 첫 행(firstInstance)의 머티리얼이 묶음의 머티리얼이다.
        std::vector<const MaterialHandle*>  materialRows;
        // transform SSBO 행 → 그리기 상태 비트(kRowMirrored · kRowIndex16, RenderTypes.hpp). materialRows 와 같은 지점에서
        // 채우고, 묶음의 행은 모두 같은 값입니다.
        std::vector<uint8_t>                rowFlags;
        std::array<AOPass::CameraParams, kMaxViews> aoCam;   // 눈별(투영이 눈마다 다름)
        std::vector<GeometryPass::PanelDraw> panels;         // 월드 UI 패널(3D UI P1b)
    };
    // 월드의 UI 패널을 모아 텍스처를 맞추고(만들기 · 크기 변경 · 사라진 패널 정리) 더러운 패널을 다시 그리게 올립니다.
    void gatherUIPanels(const RenderSceneInput& scene, std::vector<GeometryPass::PanelDraw>& out);
    RenderView extractRenderView(const RenderSceneInput& scene, const XrFrameTarget* xr);

    // Per-frame logic (extractRenderView 내부에서만 호출)
    // 라이트 개수는 updateLightSSBO가 실제로 채운 값을 반환하고 updateUBO가 그것을 받는다.
    // 양쪽이 각자 세면 라이트 상한(render.maxLights)에서 잘린 실제 개수와 UBO가 알려주는 개수가 어긋나,
    // pbr.frag가 SSBO 범위 밖을 읽는다. 계산 지점을 하나로 두는 것이 이 구조의 요점이다.
    void     updateUBO(const RenderFog& fog, uint32_t lightCount, const std::vector<RenderDecal>& decals);
    uint32_t updateLightSSBO(const std::vector<RenderLight>& lights);   // 실제로 업로드한 라이트 수를 반환
    // 드로우 커맨드와 transform SSBO 행의 유일한 생성 지점.
    // opaqueOut/transpOut의 firstInstance와 materialRowsOut의 인덱스는 같은 배정(hbatch::build)
    // 에서 나오므로 세 출력은 항상 정합한다. CPU 드로우 항목은 이 출력에서 파생시킬 것.
    // 같은 지오메트리 · 머티리얼의 불투명 메시는 instanceCount = N 인 드로우 하나가 된다(DEF-16).
    void buildDrawCommands(const RenderSceneInput& scene,
                          std::vector<GpuDrawCmd>& opaqueOut,
                          std::vector<GpuDrawCmd>& transpOut,
                          std::vector<const MaterialHandle*>& materialRowsOut,
                          std::vector<uint8_t>& rowFlagsOut,
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
        rc_.transitionImage(cmd, img, oldLayout, newLayout,
                                       srcStage, srcAccess, dstStage, dstAccess,
                                       aspect, mips, layers);
    }
    void            freeTexSlot(TexSlot slot);

    uint32_t findMemType(uint32_t filter, VkMemoryPropertyFlags flags) {
        return rc_.findMemType(filter, flags);
    }

    bool                     checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    hvk::DeviceCaps          queryDeviceCaps(VkPhysicalDevice dev);
    // 맞으면 true 이고 plan 을 채웁니다. 아니면 why 에 이유를 적습니다.
    bool                     isDeviceSuitable(VkPhysicalDevice dev, bool xrDevice, hvk::DevicePlan& plan, std::string& why);
    hvk::QueueFamilies       findQueueFamilies(VkPhysicalDevice dev);
    SwapChainSupport         querySwapChainSupport(VkPhysicalDevice dev);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT*, void*);
    StartupTimings startupTimings_{};
};
