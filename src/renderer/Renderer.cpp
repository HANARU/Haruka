#include "Renderer.hpp"
#include "LightingCalibration.hpp"
#include "SkyIbl.hpp"
#include "../core/World.hpp"
#include "../core/AppPaths.hpp"
#include "../ui/slate/HSlateRenderer.hpp"
#include "../assets/GltfLoader.hpp"
#include "../assets/MeshSource.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/packing.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <set>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <atomic>

#ifndef ANDROID
#include "../platform/glfw/GlfwWindow.hpp"
#endif
#include "GlfwSwapchain.hpp"
#include "PipelineCacheFile.hpp"
#include "RendererLog.hpp"
#include "../core/FileUtil.hpp"

// 검증 레이어 메시지. 렌더러 로그와 따로 끌 수 있게 분리합니다.
static constinit HLogCategory LogVulkan{ "Vulkan", ELogVerbosity::Log };

// readFile defined as inline in Renderer.hpp

// ── Debug callback ────────────────────────────────────────────────────────────

// 검증 레이어 ERROR 수(2026-10-31). 창 없는 실행이 종료 코드로 알립니다. 메신저는 Debug 빌드에만 있습니다.
static std::atomic<uint32_t> sValidationErrors{ 0 };

uint32_t Renderer::validationErrorCount() { return sValidationErrors.load(); }

static VkResult CreateDebugMessenger(VkInstance inst,
    const VkDebugUtilsMessengerCreateInfoEXT* ci,
    const VkAllocationCallbacks* alloc, VkDebugUtilsMessengerEXT* msg) {
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT");
    return fn ? fn(inst, ci, alloc, msg) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* d, void*) {
    // 메신저는 WARNING|ERROR 만 요청합니다. 그 외 심각도가 오면 Verbose 로 내립니다.
    const ELogVerbosity v =
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   ? ELogVerbosity::Error   :
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? ELogVerbosity::Warning :
                                                                       ELogVerbosity::Verbose;
    if (v == ELogVerbosity::Error) sValidationErrors.fetch_add(1);
    if (LogVulkan.isEnabled(v)) HLog::submit(LogVulkan, v, d->pMessage);
    return VK_FALSE;
}

// ── init / cleanup ────────────────────────────────────────────────────────────

void Renderer::init(IWindow* win, IVulkanDeviceHooks* deviceHooks) {
    window       = win;
    deviceHooks_ = deviceHooks;
    // 창이 표면을 주지 않으면(--headless · Quest) 표면 · 스왑체인 없이 XR 이미지에만 그립니다(2026-10-31).
    headless_    = !window->providesSurface();
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    allocator.setForceDedicated(forceDedicatedMsaa_);
    allocator.init(device, physDevice, deviceCaps_.maxMemoryAllocationSize);

    if (headless_) {
        // 이미지 0장 · 넓이 없음. 명목 포맷은 톤매핑 렌더 패스와 XR 선호 포맷, 명목 크기는 크기 의존 타깃에 씁니다.
        swapchain_ = std::make_unique<NullSwapchain>(VK_FORMAT_R8G8B8A8_SRGB, VkExtent2D{ 64, 64 });
        HLOG(LogRenderer, Log) << "Renderer: no surface (headless); drawing only to XR or synthetic eye images";
    } else {
        auto swap = std::make_unique<GlfwSwapchain>(
            window, surface, physDevice, device,
            queueFamilies_.graphics.value(), queueFamilies_.present.value());
        // XR 중에는 헤드셋이 루프 속도를 정합니다. 데스크톱 표시가 모니터 주사율로 기다리면 헤드셋
        // 프레임을 놓치므로 기다리지 않는 표시 방식을 고릅니다.
        swap->setPreferNonBlocking(xrCompatible_);
        swapchain_ = std::move(swap);
    }
    swapchain_->create();

    depthFormat = findDepthFormat();
    createCommandPool();
    createSamplers();
    createPipelineCache();

    // RenderContext 채우기 — 이후 모든 헬퍼/패스가 이 번들 경유
    rc_.device        = device;
    rc_.physDev       = physDevice;
    rc_.gfxQueue      = graphicsQueue;
    rc_.cmdPool       = cmdPool;
    rc_.allocator     = &allocator;
    rc_.pipelineCache = pipelineCache_;
    rc_.msaaSamples   = msaaSamples_;
    rc_.swapFormat    = swapchain_->format();
    rc_.depthFormat   = depthFormat;
    rc_.extent        = swapchain_->extent();
    // 뷰포트 미지정 시 씬은 창 전체로 렌더(오프셋 0, 단안이면 renderExtent == extent).
    rc_.viewportOffset = { 0, 0 };
    rc_.viewportExtent = rc_.extent;
    rc_.renderExtent   = desiredRenderExtent();

    // 기본 패스의 수명주기 목록(DEF-17). 프레임 순서대로이고, 확장은 등록할 때 뒤에 붙습니다.
    passRegistry_.setBuiltins({ &shadow_, &geometry_, &ao_, &bloom_, &post_ });

    // 셰이더 계약 (set 0~3 + PBR 파이프라인 레이아웃) — 패스 생성보다 선행
    frames_.createLayouts(rc_);

    // Permanent Zone B resources first: shadow map, mega buffers, IBL/BRDF.
    // Allocating these before swapchain images ensures that on every resize,
    // all freed swapchain images coalesce into one large contiguous block
    // rather than being split around the permanent allocations.
    shadow_.create(rc_, frames_.pbrLayout());  // 해상도 · 형식에 따라 다름(PC 4096² D32 64 MB, Quest 2048² D32 16 MB · D16 8 MB), no swapchain dependency
    scene_.create(rc_);                        // 192 MB mega buffers (128 vert + 64 idx)
    ao_.create(rc_);               // 노이즈 텍스처/커널 UBO 등 영구 자원만
    generateBRDFLUT();             // ~1 MB — needs cmdPool + pipelineCache only
    generateIBLCubemaps();         // ~7 MB
    // 오류 머티리얼(DEF-28): 머티리얼이 없는 메시를 자홍색으로 그립니다. 알베도와 이미시브를
    // 모두 자홍색으로 두어 조명과 상관없이 눈에 띕니다. 영구 자원이라 크기 의존 할당보다 먼저.
    {
        TexSourceData magenta;
        magenta.data    = { 255, 0, 255, 255 };
        magenta.w       = magenta.h = 1;
        magenta.hasData = true;
        errorMaterial_ = scene_.uploadMaterial(magenta, {}, {}, {}, magenta);
    }
    geometry_.create(rc_, frames_.pbrLayout(), frames_.frameLayout());  // main pass + 라인 버퍼

    // Swapchain-size resources (allocated above the permanent block)
    geometry_.acquireSizeDependent();   // depth + MSAA + HDR + 메인 프레임버퍼
    ao_.acquireSizeDependent();
    bloom_.create(rc_);

    post_.create(rc_);
    post_.acquire(swapchain_->imageViews());
    createAllPipelines();

    // 프레임별 버퍼/디스크립터/커맨드/동기화 (Zone C)
    frames_.setCapacities(maxDrawRows_, maxLights_);   // DEF-10
    frames_.createPerFrame(rc_, (uint32_t)swapchain_->imageCount());
    createTimestampPool();

    // 씬 세트 와이어링 — 섀도/IBL 뷰가 모두 준비된 뒤
    frames_.writeSceneSets(shadow_.sampler(), shadow_.shadowView(),
                           iblSampler_, iblIrradianceView_, iblPrefilterView_,
                           brdfSampler_, brdfLutView_);
    deviceHooks_ = nullptr;   // 장치를 만든 뒤에는 쓰지 않습니다
}

void Renderer::cleanup() {
    vkDeviceWaitIdle(device);
    savePipelineCache();
    detachXrSwapchain(false);   // 보통은 엔진이 세션을 지우기 전에 이미 뗐습니다
    detachSyntheticEyes();      // Zone B 할당이라 할당자를 지우기 전에 놓습니다

    // Timestamp
    if (timestampPool_) { vkDestroyQueryPool(device, timestampPool_, nullptr); timestampPool_ = VK_NULL_HANDLE; }

    // IBL
    freeTexSlot(TexSlot{ brdfLutImage_, brdfLutView_, brdfLutAlloc_ });
    freeTexSlot(TexSlot{ iblIrradianceImage_, iblIrradianceView_, iblIrradianceAlloc_ });
    freeTexSlot(TexSlot{ iblPrefilterImage_,  iblPrefilterView_,  iblPrefilterAlloc_  });
    if (iblSampler_)  { vkDestroySampler(device, iblSampler_, nullptr);  iblSampler_  = VK_NULL_HANDLE; }
    if (brdfSampler_) { vkDestroySampler(device, brdfSampler_, nullptr); brdfSampler_ = VK_NULL_HANDLE; }

    // 렌더 확장을 먼저 정리합니다(기본 패스의 HDR · 깊이를 참조할 수 있으므로). 그다음 기본 패스를
    // 프레임 역순(post → bloom → ao → geometry → shadow)으로 파괴합니다(DEF-17).
    for (auto& ext : passRegistry_.takeExtensionsReversed()) ext->destroy();
    passRegistry_.forEachReverse([](HPass& p) { p.destroy(); });

    // Error material + mega buffers
    if (errorMaterial_) { scene_.freeMaterial(errorMaterial_); errorMaterial_ = nullptr; }
    scene_.destroy();

    cleanupSwapChain();
    swapchain_.reset();    // explicitly destroy before vkDestroyDevice
    if (textureSampler) { vkDestroySampler(device, textureSampler, nullptr); }
    if (clampSampler_)  { vkDestroySampler(device, clampSampler_,  nullptr); }

    frames_.destroy();

    if (pipelineCache_) { vkDestroyPipelineCache(device, pipelineCache_, nullptr); }

    vkDestroyCommandPool(device, cmdPool, nullptr);
    allocator.destroy();
    vkDestroyDevice(device, nullptr);
    if (kEnableValidation) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(instance, debugMessenger, nullptr);
    }
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);   // 표면 확장은 창이 있을 때만 켬
    vkDestroyInstance(instance, nullptr);
    window->cleanup();
}

// ── Low-level helpers ─────────────────────────────────────────────────────────

void Renderer::freeTexSlot(TexSlot slot) {
    if (slot.imageView) { vkDestroyImageView(device, slot.imageView, nullptr); slot.imageView = VK_NULL_HANDLE; }
    if (slot.image)     { vkDestroyImage(device, slot.image, nullptr);         slot.image     = VK_NULL_HANDLE; }
    if (slot.alloc.isValid()) { allocator.freeB(slot.alloc); slot.alloc = {}; }
}

// ── BRDF LUT generation (GPU) ─────────────────────────────────────────────────

void Renderer::generateBRDFLUT() {
    constexpr uint32_t kSize = 512;
    VkFormat fmt = VK_FORMAT_R16G16_SFLOAT;

    // Create image
    brdfLutImage_ = createImage(kSize, kSize, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        fmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        0, brdfLutAlloc_);
    brdfLutView_ = createImageView(brdfLutImage_, fmt, VK_IMAGE_ASPECT_COLOR_BIT);

    // Temporary render pass
    VkAttachmentDescription att{};
    att.format         = fmt;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription  sp{};
    sp.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0; dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1; rpci.pAttachments    = &att;
    rpci.subpassCount    = 1; rpci.pSubpasses      = &sp;
    rpci.dependencyCount = 1; rpci.pDependencies   = &dep;
    VkRenderPass tmpRp;
    vkCreateRenderPass(device, &rpci, nullptr, &tmpRp);

    // Temporary framebuffer
    VkFramebufferCreateInfo fci{};
    fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass      = tmpRp;
    fci.attachmentCount = 1; fci.pAttachments = &brdfLutView_;
    fci.width = kSize; fci.height = kSize; fci.layers = 1;
    VkFramebuffer tmpFb;
    vkCreateFramebuffer(device, &fci, nullptr, &tmpFb);

    // Pipeline layout (no descriptors)
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPipelineLayout tmpLayout;
    vkCreatePipelineLayout(device, &pli, nullptr, &tmpLayout);

    // Pipeline
    VkPipeline tmpPipeline = createFullscreenPipeline(tmpRp, 0,
        "shaders/brdf_lut.frag.spv", tmpLayout);

    // Record + submit
    auto cmd = beginOneTimeCmd();
    VkRenderPassBeginInfo rpbi{};
    rpbi.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass  = tmpRp;
    rpbi.framebuffer = tmpFb;
    rpbi.renderArea  = { {0,0}, {kSize, kSize} };
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{ 0, 0, (float)kSize, (float)kSize, 0.f, 1.f };
    VkRect2D   sc{ {0,0}, {kSize, kSize} };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tmpPipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    endOneTimeCmd(cmd);

    // Cleanup temporaries
    vkDestroyPipeline(device, tmpPipeline, nullptr);
    vkDestroyPipelineLayout(device, tmpLayout, nullptr);
    vkDestroyFramebuffer(device, tmpFb, nullptr);
    vkDestroyRenderPass(device, tmpRp, nullptr);

    HLOG(LogRenderer, Log) << "BRDF LUT generated (" << kSize << "x" << kSize << " R16G16)";
}

// ── IBL cubemap generation (CPU sky gradient) ─────────────────────────────────

// 하늘 복사휘도 배율과 근거는 LightingCalibration.hpp 에 있습니다.
using lighting_calibration::kSkyRadianceScale;

static glm::vec3 skyColor(glm::vec3 dir) {
    dir = glm::normalize(dir);
    float t = dir.y * 0.5f + 0.5f;
    glm::vec3 top     = { 0.17f, 0.40f, 0.80f };
    glm::vec3 horizon = { 0.82f, 0.78f, 0.70f };
    glm::vec3 ground  = { 0.38f, 0.32f, 0.24f };
    glm::vec3 c = (t > 0.5f) ? glm::mix(horizon, top, (t - 0.5f) * 2.0f)
                             : glm::mix(ground, horizon, t * 2.0f);
    return c * kSkyRadianceScale;
}

using hibl::cubeDir;   // 큐브맵 면 · UV → 월드 방향(SkyIbl.hpp)

void Renderer::generateIBLCubemaps() {
    // ── Irradiance: 32×32 per face, 1 mip ────────────────────────────────────
    constexpr uint32_t kIrrSize = 32;
    constexpr uint32_t kPrefSize = 64;
    constexpr uint32_t kPrefMips = 5;
    constexpr uint32_t kPrefSamples = 64;   // GGX 프리필터 샘플 수(4096 샘플 기준과 상대 RMS 1% 이내, 시험 7절)

    // IEEE 반정밀도로 바꿉니다. 예전의 비트 조작은 지수 범위를 처리하지 않아 아주 작거나 큰
    // 값이 엉뚱한 수가 됐고, float 를 정수 포인터로 읽어 aliasing 규칙도 어겼습니다(DEF-86).
    // 음수·NaN 은 0, 반정밀도로 담을 수 없는 큰 값은 최댓값(65504)으로 둡니다.
    auto genHalfFloat = [](float f) -> uint16_t {
        if (!(f > 0.f)) return 0;
        return glm::packHalf1x16(std::min(f, 65504.f));
    };

    // Generate irradiance cubemap data (SH9 투영 → 평가, SkyIbl.hpp)
    const auto bakeStart = std::chrono::steady_clock::now();
    const hibl::SH9Rgb sh = hibl::projectSH9(skyColor, 32);
    std::vector<uint16_t> irrData(kIrrSize * kIrrSize * 6 * 4);
    for (int face = 0; face < 6; face++) {
        for (uint32_t iy = 0; iy < kIrrSize; iy++) {
            for (uint32_t ix = 0; ix < kIrrSize; ix++) {
                float u = (ix + 0.5f) / kIrrSize * 2.f - 1.f;
                float v = (iy + 0.5f) / kIrrSize * 2.f - 1.f;
                glm::vec3 N = glm::normalize(cubeDir(face, u, v));

                // SH 대역 가중 π · 2π/3 · π/4 를 곱한 뒤 π 로 나눠 평균 복사휘도로 둔다. 예전 몬테카를로
                // 64샘플은 텍셀 노이즈 2.7%(최대 10%)였다(그 전에는 cos 를 두 번 곱해 2.09배 과다, DEF-37).
                // PI 를 곱하지 않는다. 저장되는 값의 의미를 "그 방향에서 오는 평균 복사휘도 L"로
                // 두면 pbr.frag 의 `kD * irradiance * albedo` 를 그대로 쓸 수 있다.
                // PI/N 로 두면 조도가 되어 셰이더에서 PI 로 나눠야 하고, 그러면 프리필터
                // 큐브맵과 스페큘러 IBL 경로까지 함께 맞춰야 한다.
                glm::vec3 irr = hibl::shMeanRadiance(sh, N);

                size_t idx = ((face * kIrrSize + iy) * kIrrSize + ix) * 4;
                irrData[idx+0] = genHalfFloat(irr.r);
                irrData[idx+1] = genHalfFloat(irr.g);
                irrData[idx+2] = genHalfFloat(irr.b);
                irrData[idx+3] = genHalfFloat(1.f);
            }
        }
    }

    const auto irrDone = std::chrono::steady_clock::now();

    // Generate prefilter data (GGX prefilter)
    VkDeviceSize prefBytes = 0;
    for (uint32_t m = 0; m < kPrefMips; m++) {
        uint32_t sz = kPrefSize >> m;
        prefBytes += sz * sz * 6 * 8;
    }
    std::vector<uint8_t> prefData(prefBytes);
    VkDeviceSize prefOff = 0;
    // pbr.frag 는 textureQueryLevels 로 mip 수를 읽으므로 kPrefMips 만 바꾸면 됩니다.
    for (uint32_t mip = 0; mip < kPrefMips; mip++) {
        float roughness = (float)mip / (kPrefMips - 1);
        uint32_t sz = kPrefSize >> mip;
        for (int face = 0; face < 6; face++) {
            for (uint32_t iy = 0; iy < sz; iy++) {
                for (uint32_t ix = 0; ix < sz; ix++) {
                    float u = (ix + 0.5f) / sz * 2.f - 1.f;
                    float v = (iy + 0.5f) / sz * 2.f - 1.f;
                    glm::vec3 R = glm::normalize(cubeDir(face, u, v));

                    // GGX(α = roughness²) 적분, N = V = R 가정. mip 0 은 정확히 skyColor(R) 입니다(배경을 이
                    // 값으로 그릴 수 있음). 예전에는 skyColor 와 연결되지 않은 평균색 상수와 섞어, 거친 mip 의
                    // 위쪽 반사가 붉게(R +40%, B -7 ~ -27%), 아래쪽 반사가 12% 밝게 치우쳤습니다.
                    glm::vec3 col = (mip == 0) ? skyColor(R)
                                               : hibl::prefilterGGX(skyColor, R, roughness, kPrefSamples);

                    uint16_t* px = reinterpret_cast<uint16_t*>(prefData.data() + prefOff);
                    px[0] = genHalfFloat(col.r);
                    px[1] = genHalfFloat(col.g);
                    px[2] = genHalfFloat(col.b);
                    px[3] = genHalfFloat(1.f);
                    prefOff += 8;
                }
            }
        }
    }
    {
        using ms = std::chrono::duration<double, std::milli>;
        const auto prefDone = std::chrono::steady_clock::now();
        HLOG(LogRenderer, Log) << "IBL bake on CPU: SH9 irradiance " << ms(irrDone - bakeStart).count()
                               << " ms, GGX prefilter (" << kPrefSamples << " samples) " << ms(prefDone - irrDone).count() << " ms";
    }

    // Upload irradiance cubemap
    VkFormat cubeFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
    iblIrradianceImage_ = createImage(kIrrSize, kIrrSize, 1, 6,
        VK_SAMPLE_COUNT_1_BIT, cubeFmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, iblIrradianceAlloc_);
    iblIrradianceView_ = createImageView(iblIrradianceImage_, cubeFmt,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, 6, VK_IMAGE_VIEW_TYPE_CUBE);

    // Upload prefilter cubemap
    iblPrefilterImage_ = createImage(kPrefSize, kPrefSize, kPrefMips, 6,
        VK_SAMPLE_COUNT_1_BIT, cubeFmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, iblPrefilterAlloc_);
    iblPrefilterView_ = createImageView(iblPrefilterImage_, cubeFmt,
        VK_IMAGE_ASPECT_COLOR_BIT, kPrefMips, 6, VK_IMAGE_VIEW_TYPE_CUBE);

    // Staging buffers and upload via one-time commands
    VkDeviceSize irrBytes = irrData.size() * 2;  // uint16_t = 2 bytes

    auto makeStaging = [&](VkDeviceSize size) -> std::pair<VkBuffer, HarukaAllocation> {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = size; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buf;
        vkCreateBuffer(device, &bi, nullptr, &buf);
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(device, buf, &reqs);
        auto alloc = allocator.allocB(reqs);
        allocator.bind(buf, alloc);
        return { buf, alloc };
    };

    auto [irrStaging, irrStagingAlloc] = makeStaging(irrBytes);
    auto [prefStaging, prefStagingAlloc] = makeStaging(prefBytes);
    memcpy(irrStagingAlloc.mappedPtr,  irrData.data(), irrBytes);
    memcpy(prefStagingAlloc.mappedPtr, prefData.data(), prefBytes);

    auto cmd = beginOneTimeCmd();

    // Transition to transfer dst
    transitionImage(cmd, iblIrradianceImage_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, 6);
    transitionImage(cmd, iblPrefilterImage_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, kPrefMips, 6);

    // Copy irradiance (6 faces, 1 mip)
    std::vector<VkBufferImageCopy> irrRegions(6);
    for (int face = 0; face < 6; face++) {
        irrRegions[face].bufferOffset     = (VkDeviceSize)face * kIrrSize * kIrrSize * 8;
        irrRegions[face].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, (uint32_t)face, 1 };
        irrRegions[face].imageExtent      = { kIrrSize, kIrrSize, 1 };
    }
    vkCmdCopyBufferToImage(cmd, irrStaging, iblIrradianceImage_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, irrRegions.data());

    // Copy prefilter (6 faces × kPrefMips mips)
    std::vector<VkBufferImageCopy> prefRegions;
    VkDeviceSize pOff = 0;
    for (uint32_t mip = 0; mip < kPrefMips; mip++) {
        uint32_t sz = kPrefSize >> mip;
        for (int face = 0; face < 6; face++) {
            VkBufferImageCopy r{};
            r.bufferOffset      = pOff;
            r.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, mip, (uint32_t)face, 1 };
            r.imageExtent       = { sz, sz, 1 };
            prefRegions.push_back(r);
            pOff += (VkDeviceSize)sz * sz * 8;
        }
    }
    vkCmdCopyBufferToImage(cmd, prefStaging, iblPrefilterImage_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        (uint32_t)prefRegions.size(), prefRegions.data());

    // Transition to shader read
    transitionImage(cmd, iblIrradianceImage_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, 6);
    transitionImage(cmd, iblPrefilterImage_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, kPrefMips, 6);

    endOneTimeCmd(cmd);

    vkDestroyBuffer(device, irrStaging, nullptr);  allocator.freeB(irrStagingAlloc);
    vkDestroyBuffer(device, prefStaging, nullptr); allocator.freeB(prefStagingAlloc);

    HLOG(LogRenderer, Log) << "IBL cubemaps generated (sky gradient, " << kPrefMips << " mip levels)";
}

// ── Vulkan instance / device setup ────────────────────────────────────────────

void Renderer::createInstance() {
    if (kEnableValidation && !checkValidationLayerSupport())
        throw std::runtime_error("Validation layers not available!");
    VkApplicationInfo ai{};
    ai.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "HarukaEngine";
    ai.applicationVersion= VK_MAKE_VERSION(0, 1, 0);
    ai.pEngineName      = "Haruka";
    ai.engineVersion    = VK_MAKE_VERSION(0, 1, 0);
    // Vulkan 1.1 기준선(DEF-97). PC 와 Quest 가 같은 경로(1.1 코어 + 확장)를 탑니다. Quest 2 · Pro 는 1.1 이고, Quest Link
    // 런타임은 1.0 ~ 1.2 를 알려 줍니다. 1.2 이상 코어 기능은 쓰지 않습니다(VulkanBaseline.hpp). XR 이면 아래에서 런타임
    // 최댓값까지 올립니다(DEF-164).
    ai.apiVersion       = hvk::kVulkanApiVersion;
    auto exts = getRequiredExtensions();
    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    VkDebugUtilsMessengerCreateInfoEXT dci{};
    if (kEnableValidation) {
        ci.enabledLayerCount   = (uint32_t)kValidationLayers.size();
        ci.ppEnabledLayerNames = kValidationLayers.data();
        dci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = debugCallback;
        ci.pNext            = &dci;
    }
    // XR 이면 런타임이 자기 인스턴스 확장을 덧붙여 만듭니다(XR_KHR_vulkan_enable2). 런타임이 실패하면
    // XR 없이 직접 만들어 데스크톱으로 계속합니다.
    VkResult r = VK_ERROR_INITIALIZATION_FAILED;
    // 런타임이 알려 준 범위 밖이면: 최솟값보다 낮으면 XR 을 끄고, 최댓값보다 높으면 경고만 합니다(사용자 결정 2026-10-31).
    uint32_t xrMin = 0, xrMax = 0;
    if (deviceHooks_ && deviceHooks_->vulkanApiRange(xrMin, xrMax)) {
        // 런타임이 엔진 장치에서 1.2 코어 이름(vkWaitSemaphores)을 찾을 수 있게 인스턴스를 런타임 최댓값까지 올립니다
        // (DEF-164). 1.1 이면 그 이름이 null 이라 Stop 때 런타임이 주소 0 을 불렀습니다. 엔진 코드는 그대로 1.1 경로입니다.
        uint32_t loader = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion(&loader) != VK_SUCCESS) loader = VK_API_VERSION_1_0;
        ai.apiVersion = hvk::xrInstanceApiVersion(hvk::kVulkanApiVersion, xrMax, loader);
        const std::string range = hvk::versionString(xrMin) + " to " + hvk::versionString(xrMax);
        switch (hvk::checkApiRange(ai.apiVersion, xrMin, xrMax)) {
            case hvk::ApiRange::BelowMin:
                HLOG(LogRenderer, Warning) << "Vulkan " << hvk::versionString(ai.apiVersion) << " is below the OpenXR runtime's "
                                              "range (" << range << "); XR is disabled for this run";
                deviceHooks_ = nullptr;
                break;
            case hvk::ApiRange::AboveMax:
                HLOG(LogRenderer, Warning) << "Vulkan " << hvk::versionString(ai.apiVersion) << " is above the range the OpenXR "
                                              "runtime was tested with (" << range << "); continuing";
                break;
            default:
                HLOG(LogRenderer, Log) << "Vulkan API " << hvk::versionString(ai.apiVersion) << " requested (OpenXR runtime "
                                          "supports " << range << ")";
                break;
        }
    }
    if (deviceHooks_) {
        r = deviceHooks_->createVulkanInstance(ci, instance);
        if (r != VK_SUCCESS) {
            HLOG(LogRenderer, Warning) << "The OpenXR runtime could not create the Vulkan instance (VkResult "
                                       << (int)r << "); XR is disabled for this run";
            deviceHooks_ = nullptr;
        }
    }
    if (!deviceHooks_) {
        ai.apiVersion = hvk::kVulkanApiVersion;   // XR 없이 만들면 기준선 그대로(위에서 올렸어도)
        r = vkCreateInstance(&ci, nullptr, &instance);
    }
    if (r != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan instance!");
}

void Renderer::setupDebugMessenger() {
    if (!kEnableValidation) return;
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;
    if (CreateDebugMessenger(instance, &ci, nullptr, &debugMessenger) != VK_SUCCESS)
        throw std::runtime_error("Failed to setup debug messenger!");
}

void Renderer::createSurface() {
    if (headless_) { surface = VK_NULL_HANDLE; return; }
    surface = window->createSurface(instance);
    if (surface == VK_NULL_HANDLE)
        throw std::runtime_error("The window did not create a Vulkan surface");
}

void Renderer::pickPhysicalDevice() {
    // XR 이면 헤드셋이 연결된 GPU 를 씁니다. 창이 있으면 그 GPU 가 창에도 그릴 수 있어야 에디터 뷰포트에 비출 수
    // 있습니다. 못 그리면(노트북의 다른 GPU 등) 직접 고르고 XR 은 쓰지 않습니다. 창이 없으면 런타임이 고른 GPU 를
    // 그대로 씁니다(Quest · 창 없는 Quest Link).
    hvk::DevicePlan plan;
    std::string     why;
    if (deviceHooks_) {
        const VkPhysicalDevice xrDev = deviceHooks_->vulkanPhysicalDevice(instance);
        if (xrDev != VK_NULL_HANDLE && isDeviceSuitable(xrDev, true, plan, why)) {
            physDevice    = xrDev;
            xrCompatible_ = true;
        } else if (xrDev == VK_NULL_HANDLE) {
            HLOG(LogRenderer, Warning) << "The OpenXR runtime did not choose a GPU; XR is disabled for this run";
        } else {
            HLOG(LogRenderer, Warning) << "The GPU chosen by the OpenXR runtime cannot be used (" << why
                                       << "); XR is disabled for this run";
        }
    }
    if (physDevice == VK_NULL_HANDLE) {
        uint32_t cnt = 0;
        vkEnumeratePhysicalDevices(instance, &cnt, nullptr);
        if (!cnt) throw std::runtime_error("No Vulkan-capable GPU found!");
        std::vector<VkPhysicalDevice> devs(cnt);
        vkEnumeratePhysicalDevices(instance, &cnt, devs.data());
        std::string reasons;
        for (auto& d : devs) {
            if (isDeviceSuitable(d, false, plan, why)) { physDevice = d; break; }
            VkPhysicalDeviceProperties dp;
            vkGetPhysicalDeviceProperties(d, &dp);
            reasons += std::string(reasons.empty() ? "" : "; ") + dp.deviceName + ": " + why;
        }
        if (physDevice == VK_NULL_HANDLE)
            throw std::runtime_error("No suitable GPU found (" + reasons + ")");
    }
    devicePlan_    = plan;
    deviceCaps_    = queryDeviceCaps(physDevice);
    queueFamilies_ = findQueueFamilies(physDevice);
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(physDevice, &p);
    HLOG(LogRenderer, Log) << "GPU: " << p.deviceName;
    {
        std::string exts;
        for (const char* e : devicePlan_.extensions) exts += std::string(exts.empty() ? "" : ", ") + e;
        HLOG(LogRenderer, Log) << "Vulkan device " << hvk::versionString(deviceCaps_.apiVersion) << "; extensions: " << exts
                               << "; anisotropy " << (devicePlan_.samplerAnisotropy ? "on" : "off")
                               << "; depth bias clamp " << (devicePlan_.depthBiasClamp ? "on" : "off")
                               << "; depth clamp " << (devicePlan_.depthClamp ? "on" : "off");
    }
    VkSampleCountFlags counts = p.limits.framebufferColorSampleCounts &
                                p.limits.framebufferDepthSampleCounts;
    if      (counts & VK_SAMPLE_COUNT_4_BIT) msaaSamples_ = VK_SAMPLE_COUNT_4_BIT;
    else if (counts & VK_SAMPLE_COUNT_2_BIT) msaaSamples_ = VK_SAMPLE_COUNT_2_BIT;
    else                                      msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    HLOG(LogRenderer, Log) << "MSAA: " << (uint32_t)msaaSamples_ << "x";
    timestampPeriod_ = p.limits.timestampPeriod;
}

void Renderer::createLogicalDevice() {
    const hvk::QueueFamilies& idx = queueFamilies_;
    std::set<uint32_t> uq = { idx.graphics.value() };
    if (idx.present) uq.insert(*idx.present);   // 창이 없으면 표시 큐가 없습니다
    float prio = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    for (auto qf : uq) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = qf; qci.queueCount = 1; qci.pQueuePriorities = &prio;
        qcis.push_back(qci);
    }
    // 멀티뷰는 켜지 않습니다. 지금 두 눈은 눈마다 따로 그립니다(DEF-09 첫 단계). 예전에는 기능만 켜고
    // 쓰는 경로가 없었습니다. 셰이더와 UBO 는 두 눈 기준이라, 멀티뷰로 옮길 때(Quest) 이 자리에서 켜고
    // 렌더 패스 viewMask · 배열 층 · gl_ViewIndex 를 함께 바꿉니다.
    // drawIndirectCount 는 요구하지 않습니다. GPU 컬링(유일한 사용처)을 지웠고, 이 기능이 없는
    // 기기에서는 장치 생성부터 실패합니다(DEF-03).
    // 기능은 Vulkan 1.1 기준선으로 켭니다(DEF-97, VulkanBaseline.hpp). push descriptor 는 확장만 켜면 되고 기능 구조체가
    // 없습니다. synchronization2 는 1.3 코어 기능(Vulkan13Features)이 아니라 VK_KHR_synchronization2 확장의 구조체 하나로
    // 켭니다(DEF-04). 장치가 지원하지 않거나 --vk-no-sync2 면 켜지 않고 배리어를 1.0 으로 기록합니다. Vulkan11~14Features
    // 는 잇지 않습니다(1.1 장치에는 없는 구조체이고, Vulkan13Features 와 개별 구조체를 함께 달면 VUID-...-pNext-06532 위반).
    // 이방성 필터링은 지원할 때만 켭니다(없으면 끄고 계속, 사용자 결정 2026-10-31). XR 장치에는 런타임이 자기 셰이더도
    // 만듭니다. Oculus 런타임의 셰이더는 다중 샘플 저장 이미지를 쓰는데, 런타임은 장치를 만들 때 확장만 덧붙이고 기능은
    // 켜지 않아 세션마다 검증 오류가 났습니다(DEF-110). GPU 가 지원하면 켜 둡니다. 데스크톱 경로는 바꾸지 않습니다.
    hvk::FeatureChain chain;
    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext                   = hvk::buildFeatureChain(devicePlan_, chain);
    ci.queueCreateInfoCount    = (uint32_t)qcis.size();
    ci.pQueueCreateInfos       = qcis.data();
    ci.pEnabledFeatures        = &chain.core;
    ci.enabledExtensionCount   = (uint32_t)devicePlan_.extensions.size();
    ci.ppEnabledExtensionNames = devicePlan_.extensions.data();
    // 장치 레이어는 지정하지 않습니다. 현재 사양은 enabledLayerCount 를 0 으로 두도록 정하고,
    // 검증 레이어는 인스턴스에서 켠 것이 장치에도 적용됩니다(DEF-29).
    // XR 이면 런타임이 자기 장치 확장을 덧붙여 만듭니다. 런타임이 실패하면 직접 만들고 XR 은 쓰지 않습니다.
    VkResult r = VK_ERROR_INITIALIZATION_FAILED;
    if (deviceHooks_ && xrCompatible_) {
        r = deviceHooks_->createVulkanDevice(physDevice, ci, device);
        if (r != VK_SUCCESS) {
            HLOG(LogRenderer, Warning) << "The OpenXR runtime could not create the Vulkan device (VkResult "
                                       << (int)r << "); XR is disabled for this run";
            xrCompatible_ = false;
        }
    }
    if (!xrCompatible_) r = vkCreateDevice(physDevice, &ci, nullptr, &device);
    if (r != VK_SUCCESS)
        throw std::runtime_error("Failed to create logical device!");
    vkGetDeviceQueue(device, idx.graphics.value(), 0, &graphicsQueue);
    if (idx.present) vkGetDeviceQueue(device, *idx.present, 0, &presentQueue);

    // 확장 함수는 장치에서 KHR 이름으로만 받습니다(VulkanBaseline.hpp). 1.0 배리어는 로더가 언제나 내보내는 코어 함수입니다.
    std::string err;
    if (!hvk::loadDeviceFns(device, &vkGetDeviceProcAddr, &vkCmdPipelineBarrier, devicePlan_, rc_.vk, &err))
        throw std::runtime_error("Vulkan device functions: " + err);
    if (rc_.vk.sync2) {
        HLOG(LogRenderer, Log) << "Barriers: " VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
    } else {
        HLOG(LogRenderer, Log) << "Barriers: Vulkan 1.0 ("
                               << (forceNoSync2_ ? "--vk-no-sync2"
                                   : devicePlan_.sync2 ? "vkCmdPipelineBarrier2KHR not returned by the driver"
                                                       : VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME " unavailable")
                               << ")";
    }
}

// ── Utilities ─────────────────────────────────────────────────────────────────

bool Renderer::checkValidationLayerSupport() {
    uint32_t cnt; vkEnumerateInstanceLayerProperties(&cnt, nullptr);
    std::vector<VkLayerProperties> avail(cnt);
    vkEnumerateInstanceLayerProperties(&cnt, avail.data());
    for (auto* name : kValidationLayers) {
        bool found = false;
        for (auto& lp : avail) if (strcmp(name, lp.layerName) == 0) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

std::vector<const char*> Renderer::getRequiredExtensions() {
    auto exts = window->getRequiredInstanceExtensions();
    if (kEnableValidation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    return exts;
}

// 물리 장치에서 판단에 쓸 값을 읽습니다. 기능 · 속성 2 조회(1.1 코어)는 장치가 1.1 이상일 때만 부르고, 확장 구조체는 그
// 확장이 있을 때만 잇습니다.
hvk::DeviceCaps Renderer::queryDeviceCaps(VkPhysicalDevice dev) {
    hvk::DeviceCaps c;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);
    c.apiVersion = props.apiVersion;

    uint32_t cnt = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &cnt, nullptr);
    std::vector<VkExtensionProperties> avail(cnt);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &cnt, avail.data());
    for (const auto& e : avail) {
        if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)         c.extSwapchain      = true;
        if (strcmp(e.extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0)   c.extPushDescriptor = true;
        if (strcmp(e.extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0) c.extSync2          = true;
    }

    VkPhysicalDeviceFeatures base{};
    vkGetPhysicalDeviceFeatures(dev, &base);
    c.featSamplerAnisotropy       = base.samplerAnisotropy == VK_TRUE;
    c.featStorageImageMultisample = base.shaderStorageImageMultisample == VK_TRUE;
    c.featDepthBiasClamp          = base.depthBiasClamp == VK_TRUE;
    c.featDepthClamp              = base.depthClamp == VK_TRUE;
    if (hvk::majorMinor(c.apiVersion) < hvk::majorMinor(VK_API_VERSION_1_1)) return c;

    VkPhysicalDeviceSynchronization2FeaturesKHR s2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
    VkPhysicalDeviceFeatures2 f2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    if (c.extSync2) f2.pNext = &s2;
    vkGetPhysicalDeviceFeatures2(dev, &f2);
    c.featSync2 = c.extSync2 && s2.synchronization2 == VK_TRUE;

    VkPhysicalDevicePushDescriptorPropertiesKHR push{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR };
    VkPhysicalDeviceMaintenance3Properties      m3{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES };
    VkPhysicalDeviceProperties2                 p2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    p2.pNext = &m3;
    if (c.extPushDescriptor) { m3.pNext = &push; }
    vkGetPhysicalDeviceProperties2(dev, &p2);
    c.maxMemoryAllocationSize = m3.maxMemoryAllocationSize;
    c.maxPushDescriptors      = c.extPushDescriptor ? push.maxPushDescriptors : 0;
    return c;
}

bool Renderer::isDeviceSuitable(VkPhysicalDevice dev, bool xrDevice, hvk::DevicePlan& plan, std::string& why) {
    plan = hvk::planDevice(queryDeviceCaps(dev), !headless_, xrDevice, forceNoSync2_);
    if (!plan.suitable) { why = plan.whyNot; return false; }
    const hvk::QueueFamilies q = findQueueFamilies(dev);
    if (!q.complete(!headless_)) {
        why = headless_ ? "no graphics queue" : "no graphics queue or no queue that can present to the window";
        return false;
    }
    // 창이 있으면 그 창에 그릴 수 있어야 합니다. 창이 없으면 표면 함수를 부르지 않습니다.
    if (!headless_) {
        const SwapChainSupport sup = querySwapChainSupport(dev);
        if (sup.formats.empty() || sup.modes.empty()) { why = "cannot present to the window"; return false; }
    }
    return true;
}

hvk::QueueFamilies Renderer::findQueueFamilies(VkPhysicalDevice dev) {
    uint32_t cnt = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &cnt, nullptr);
    std::vector<VkQueueFamilyProperties> fams(cnt);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &cnt, fams.data());
    // 표면이 없으면 표시 가능 여부를 묻지 않습니다(VK_KHR_surface 를 켜지 않은 인스턴스).
    std::vector<VkBool32> present;
    if (surface != VK_NULL_HANDLE) {
        present.resize(cnt, VK_FALSE);
        for (uint32_t i = 0; i < cnt; i++) vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present[i]);
    }
    return hvk::selectQueueFamilies(fams, present, surface != VK_NULL_HANDLE);
}

SwapChainSupport Renderer::querySwapChainSupport(VkPhysicalDevice dev) {
    SwapChainSupport s;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &s.caps);
    uint32_t cnt;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &cnt, nullptr);
    s.formats.resize(cnt);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &cnt, s.formats.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &cnt, nullptr);
    s.modes.resize(cnt);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &cnt, s.modes.data());
    return s;
}

// ── Pipeline cache ────────────────────────────────────────────────────────────

// 파이프라인 캐시는 실행 중에 쓰는 파일이라 쓰기 루트에 둡니다(에디터는 실행 폴더, 게임은 사용자 폴더, 2026-10-19).
// 파일은 자체 머리로 감싸고(PipelineCacheFile.hpp), 읽을 때 잘림 · 손상 · 다른 장치의 캐시를 걸러 빈 캐시로 시작합니다(DEF-153).
void Renderer::createPipelineCache() {
    VkPipelineCacheCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    std::vector<uint8_t> file;
    {
        std::ifstream f(happ::inWriteRoot(kPipelineCacheFile), std::ios::binary | std::ios::ate);
        const std::streamoff n = f.is_open() ? (std::streamoff)f.tellg() : 0;
        if (n > 0) {
            file.resize((size_t)n);
            f.seekg(0);
            if (!f.read(reinterpret_cast<char*>(file.data()), n)) file.clear();
        }
    }
    if (!file.empty()) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physDevice, &props);
        size_t off = 0, size = 0;
        std::string why;
        if (hpcache::unwrap(file, props, off, size, why)) {
            ci.initialDataSize      = size;
            ci.pInitialData         = file.data() + off;
            pipelineCacheSavedHash_ = hpak::hash64(file.data() + off, size);   // 바뀌지 않았으면 다시 쓰지 않음
        } else {
            HLOG(LogRenderer, Warning) << kPipelineCacheFile << " ignored: " << why << "; starting with an empty pipeline cache";
        }
    }
    if (vkCreatePipelineCache(device, &ci, nullptr, &pipelineCache_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline cache!");
}

// 정상 종료뿐 아니라 파이프라인을 새로 만든 뒤 첫 프레임에도 저장합니다(DEF-153, drawFrame 끝). Android 는 백그라운드에서
// 프로세스가 끝나면 정리 경로를 거치지 않습니다. 내용이 앞서 읽거나 쓴 것과 같으면 쓰지 않고, 쓰기는 임시 파일 뒤 이름 바꾸기라
// 쓰는 도중 죽어도 앞의 파일이 남습니다.
void Renderer::savePipelineCache() {
    if (!pipelineCache_) return;
    size_t sz = 0;
    if (vkGetPipelineCacheData(device, pipelineCache_, &sz, nullptr) != VK_SUCCESS || !sz) return;
    std::vector<uint8_t> data(sz);
    if (vkGetPipelineCacheData(device, pipelineCache_, &sz, data.data()) != VK_SUCCESS) return;
    data.resize(sz);
    const uint64_t h = hpak::hash64(data.data(), data.size());
    if (h == pipelineCacheSavedHash_) return;
    const std::vector<uint8_t> file = hpcache::wrap(data.data(), data.size());
    std::string err;
    if (hfile::writeAtomicBinary(happ::inWriteRoot(kPipelineCacheFile), file.data(), file.size(), &err))
        pipelineCacheSavedHash_ = h;
    else
        HLOG(LogRenderer, Warning) << "Pipeline cache not saved: " << err;
}

// ── Timestamp query pool ──────────────────────────────────────────────────────

void Renderer::createTimestampPool() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physDevice, &props);
    if (!props.limits.timestampComputeAndGraphics) {
        HLOG(LogRenderer, Warning) << "GPU timestamp queries not supported";
        return;
    }
    VkQueryPoolCreateInfo ci{};
    ci.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    ci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    // 프레임당 kTimestampsPerFrame 개입니다(시작 · 섀도 · 눈마다 4칸 · UI). AO 와 Bloom 을 한
    // 덩어리로 재던 것을 쪼개고 UI 패스를 따로 볼 수 있게 늘렸습니다. 쓰지 않던 TAA 블릿 칸은
    // 2026-10-04 에 뺐습니다. 두 눈을 그리면서 눈마다 칸을 둡니다(DEF-09).
    ci.queryCount = MAX_FRAMES * kTimestampsPerFrame;
    if (vkCreateQueryPool(device, &ci, nullptr, &timestampPool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create timestamp pool!");
}

// ── UI renderer hook ──────────────────────────────────────────────────────────

void Renderer::setUIRenderer(HSlateRenderer* r) {
    uiRenderer_ = r;
    if (r) r->rebuildFramebuffers(swapchain_->extent(), swapchain_->imageViews());
}

// ── Hot reload ────────────────────────────────────────────────────────────────

// 다시 읽기 전에 .spv 가 온전한지 봅니다. 복사가 끝나기 전에 감시 스레드가 알리면 덜 쓴 파일이
// 들어오는데, 예전에는 그 파일로 파이프라인을 만들다 예외가 나 에디터가 종료됐습니다.
static bool spirvLooksValid(const std::filesystem::path& p, std::string& why) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(p, ec);
    if (ec || size < 20 || size % 4 != 0) {
        why = "size " + std::to_string(ec ? 0 : size);
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    uint32_t magic = 0;
    if (!f.read(reinterpret_cast<char*>(&magic), sizeof(magic)) || magic != 0x07230203u) {
        why = "bad magic";
        return false;
    }
    return true;
}

// ── 렌더 확장 (DEF-17 · 18) ─────────────────────────────────────────────────────
// 등록하면 기본 패스와 같은 수명주기를 바로 밟습니다(create → 크기 의존 자원 → 파이프라인). 그 뒤로는
// passRegistry_ 가 크기 변경 · 핫 리로드 · 파괴 때 기본 패스와 함께 부르고, drawFrame 이 지점마다 부릅니다.
void Renderer::addRenderExtension(std::shared_ptr<IRenderExtension> ext) {
    if (!ext) return;
    if (passRegistry_.contains(ext.get())) {
        HLOG(LogRenderer, Warning) << "Render extension is already registered; ignored";
        return;
    }
    HRenderDeviceInfo info;
    info.rc                 = &rc_;
    info.frameSetLayout     = frames_.frameLayout();
    info.mainPass           = geometry_.mainPass();
    info.opaqueSubpass      = 1;
    info.transparentSubpass = 2;
    info.hdrFormat          = GeometryPass::kHdrFormat;
    try {
        ext->create(info);
        ext->acquireSizeDependent();
        ext->rebuildPipelines();
    } catch (const std::exception& ex) {
        HLOG(LogRenderer, Error) << "Render extension failed to initialize and was not added: " << ex.what();
        ext->destroy();
        return;
    }
    passRegistry_.add(std::move(ext));
    HLOG(LogRenderer, Log) << "Render extension added (" << passRegistry_.extensionCount() << " registered)";
}

void Renderer::removeRenderExtension(IRenderExtension* ext) {
    if (!ext || !passRegistry_.contains(ext)) return;
    vkDeviceWaitIdle(device);   // 진행 중인 프레임이 확장의 자원을 다 쓰고 나서 지웁니다
    auto owned = passRegistry_.remove(ext);
    owned->destroy();
    HLOG(LogRenderer, Log) << "Render extension removed (" << passRegistry_.extensionCount() << " registered)";
}

void Renderer::reloadPipelines() {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(happ::inExeDir("shaders"), ec)) {
        if (e.path().extension() != ".spv") continue;
        std::string why;
        if (!spirvLooksValid(e.path(), why)) {
            HLOG(LogRenderer, Warning) << "Shader reload skipped: " << e.path().filename().string()
                                       << " is not valid SPIR-V yet (" << why << ")";
            return;
        }
    }
    vkDeviceWaitIdle(device);
    try {
        createAllPipelines();
        HLOG(LogRenderer, Log) << "Pipelines hot-reloaded.";
    } catch (const std::exception& ex) {
        // 각 패스는 새 파이프라인을 다 만든 뒤에만 옛 것을 지우므로, 실패한 패스는 이전
        // 파이프라인으로 계속 그립니다.
        HLOG(LogRenderer, Error) << "Shader reload failed, keeping previous pipelines: " << ex.what();
    }
}
