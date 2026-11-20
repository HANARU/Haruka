#include "Renderer.hpp"
#include "../ui/slate/HSlateRenderer.hpp"
#include "../geometry/MeshSource.hpp"
#include "../math/Projection.hpp"
#include "../core/FrameTimings.hpp"
#include "RendererLog.hpp"
#include "DrawBatching.hpp"
#include "LightSelection.hpp"
#include "SpotCone.hpp"
#include "ShadowFit.hpp"
#include "ScreenSize.hpp"
#include "LightingCalibration.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#define GLM_FORCE_RADIANS
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <array>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include "../core/HCheck.hpp"
#include <random>
#include <functional>

// ── createCommandPool ─────────────────────────────────────────────────────────

void Renderer::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = queueFamilies_.graphics.value();
    if (vkCreateCommandPool(device, &ci, nullptr, &cmdPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool!");
}

// ── createSamplers ────────────────────────────────────────────────────────────

void Renderer::createSamplers() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physDevice, &props);
    float maxA = props.limits.maxSamplerAnisotropy;

    auto make = [&](VkSampler& out, VkFilter filter, VkSamplerAddressMode addr,
                    bool aniso, VkBorderColor border, float maxLod) {
        VkSamplerCreateInfo si{};
        si.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter        = filter; si.minFilter = filter;
        si.addressModeU     = addr; si.addressModeV = addr; si.addressModeW = addr;
        si.anisotropyEnable = aniso ? VK_TRUE : VK_FALSE;
        si.maxAnisotropy    = aniso ? maxA : 1.f;
        si.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.borderColor      = border;
        si.maxLod           = maxLod;
        if (vkCreateSampler(device, &si, nullptr, &out) != VK_SUCCESS)
            throw std::runtime_error("Failed to create sampler!");
    };

    // 이방성은 장치 계획을 직접 읽습니다(지원하지 않는 장치는 끔). rc_ 를 채우기 전에 불리므로 rc_ 를 거치지 않습니다.
    make(textureSampler, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
         devicePlan_.samplerAnisotropy, VK_BORDER_COLOR_INT_OPAQUE_BLACK, 16.f);
    make(clampSampler_,  VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         false, VK_BORDER_COLOR_INT_OPAQUE_BLACK,    16.f);
    // 월드 UI 패널(3D UI P1b): 가장자리를 고정해 반대쪽 테두리가 번지지 않게 하고, 비스듬히 볼 때 글자가 흐려지지 않게
    // 이방성을 켭니다(Research_3D_UI_Panel: UE Widget Component 의 흐림 · 반짝임을 밉 + 이방성으로 피함).
    make(panelSampler_,  VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         devicePlan_.samplerAnisotropy, VK_BORDER_COLOR_INT_OPAQUE_BLACK, 16.f);
    // (shadow sampler는 ShadowPass가 소유)
    make(iblSampler_,    VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         false, VK_BORDER_COLOR_INT_OPAQUE_BLACK,    16.f);
    make(brdfSampler_,   VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         false, VK_BORDER_COLOR_INT_OPAQUE_BLACK,     0.f);
}

// ── updateLightSSBO ───────────────────────────────────────────────────────────

uint32_t Renderer::updateLightSSBO(const std::vector<RenderLight>& lights) {
    auto* gpuLights = static_cast<GpuLight*>(frames_.lightPtr(frameIdx));
    uint32_t count  = 0;
    shadowCasterSlot_ = -1;                     // 이 프레임의 섀도 캐스터를 재선정
    contactLength_    = 0.f;

    // 올릴 라이트를 고릅니다(DEF-33, LightSelection.hpp). 어느 눈에도 영향이 없는 점광원 · 스팟은 거르고,
    // 상한(render.maxLights)을 넘으면 방향광을 먼저, 나머지는 가까운 순으로 고릅니다. 상한 안이면 순서를
    // 바꾸지 않아 픽셀 합산 순서가 그대로입니다. 예전에는 생성 순서대로 앞에서부터 올리고 상한에서 잘랐습니다.
    std::vector<hlight::Candidate> lightCands;
    lightCands.reserve(lights.size());
    for (const RenderLight& rl : lights) {
        hlight::Candidate c;
        c.directional = (rl.kind == RenderLightKind::Directional);
        c.position    = rl.position;
        c.radius      = rl.radius;
        c.brightness  = std::max({ rl.color.r, rl.color.g, rl.color.b });
        lightCands.push_back(c);
    }
    std::array<std::array<FrustumPlane, 6>, kMaxViews> lightViewPlanes{};
    if (hasCamera_)
        for (uint32_t i = 0; i < viewCount_; ++i) {
            lightViewPlanes[i] = extractFrustumPlanes(views_[i].proj * views_[i].view);
            replaceInfinitePlane(lightViewPlanes[i], views_[i].view, views_[i].zFar);   // 무한 원평면(C1) → 카메라 zFar
        }
    // 품질 단계의 라이트 상한(render.lightLimit, 2026-11-18)이 있으면 버퍼 상한(render.maxLights)과 둘 중 작은 값입니다.
    const bool     qualityCap = quality_.lightLimit > 0 && quality_.lightLimit < maxLights_;
    const uint32_t lightCap   = qualityCap ? quality_.lightLimit : maxLights_;
    const hlight::Result chosen = hlight::select(lightCands, hasCamera_ ? lightViewPlanes.data() : nullptr,
                                                 hasCamera_ ? viewCount_ : 0, viewCenter_, lightCap);
    if (chosen.overBudget && !lightBudgetWarned_) {
        lightBudgetWarned_ = true;
        HLOG(LogRenderer, Warning) << "Light budget " << (qualityCap ? "render.lightLimit=" : "render.maxLights=") << lightCap
            << " exceeded: " << chosen.overBudget << " visible light(s) were dropped, keeping directional lights and the nearest ones"
            << (qualityCap ? " (warned once; render.lightLimit is a quality setting in assets/project.ini)"
                           : " (warned once; raise render.maxLights in Config/engine.ini)");
    }

    for (uint32_t li : chosen.selected) {
        const RenderLight& rl = lights[li];
        const uint32_t slot = count;
        GpuLight& gl = gpuLights[count++];
        const glm::vec3 col = rl.color;   // 색온도 틴트 포함
        // 섀도 캐스터 = 첫 Directional 이면서 castShadows 가 켜진 라이트. 선택과 SSBO 채우기가
        // 같은 루프 안이라 슬롯 번호가 어긋날 수 없습니다(DEF-26). 캐스터가 없으면 -1 로 남습니다.
        if (rl.kind == RenderLightKind::Directional && rl.castShadows && shadowCasterSlot_ < 0) {
            shadowCasterSlot_ = (int)slot;
            shadowCasterDir_  = -rl.emitDirection;   // 빛을 향하는 벡터(표면 → 태양)
            contactLength_    = quality_.contactShadows ? rl.contactShadowLength : 0.f;
        }
        if (rl.kind == RenderLightKind::Directional) {
            // 방향의 부호는 emitDirection()(발광 방향) 하나로 정합니다. 저장 형식은 그대로입니다.
            const glm::vec3 toLight = -rl.emitDirection;   // 액터 회전 반영
            gl.posOrDir  = glm::vec4(-toLight, 0.f);   // w=0 → directional. 셰이더가 다시 뒤집어 L = toLight 로 읽음
            gl.colorRange= glm::vec4(col, 0.f);
            gl.spotDir   = glm::vec4(toLight, 0.f);    // Directional 에서는 셰이더가 읽지 않음(SSBO 바이트 유지)
            gl.spotParams= glm::vec4(0.f, 0.f, (float)rl.channels, 0.f);
        } else if (rl.kind == RenderLightKind::Point) {
            gl.posOrDir  = glm::vec4(rl.position, 1.f);    // w=1 → point
            gl.colorRange= glm::vec4(col, rl.radius);
            gl.spotDir   = glm::vec4(0.f);
            gl.spotParams= glm::vec4(0.f, 0.f, (float)rl.channels, 0.f);
        } else { // Spot
            gl.posOrDir  = glm::vec4(rl.position, 2.f);    // w=2 → spot
            gl.colorRange= glm::vec4(col, rl.radius);
            gl.spotDir   = glm::vec4(rl.emitDirection, 0.f);   // pbr.frag 가 dot(-L, spotDir) 로 읽음
            const glm::vec2 cone = hlight::spotConeParams(rl.innerCone, rl.outerCone);
            gl.spotParams= glm::vec4(cone.x, cone.y, (float)rl.channels, 0.f);
        }
    }
    frameStats_.lights      = count;
    frameStats_.lightsTotal = (uint32_t)lights.size();
    return count;   // 실제로 업로드한 수. updateUBO가 이 값을 그대로 쓴다.
}

// ── updateUBO ─────────────────────────────────────────────────────────────────

// 이번 프레임의 합성 값. 프로젝트 · 씬 · 카메라 EV 와 배율은 여기서 곱합니다(exposure_wiring-4 · 5 · 6 · 8).
PostPass::Params Renderer::postParams() const {
    PostPass::Params p{ lighting_calibration::kBaseAoStrength, lighting_calibration::kBaseBloomStrength,
                        lighting_calibration::kBaseExposure };
    // 접촉 그림자는 AO 패스에서 계산해 AO 와 함께 합성합니다. AO 를 끄고 접촉 그림자만 켜면 AO 패스를 돌리되 세기 1 로 그 결과만 곱합니다.
    const bool contact = shadowCasterSlot_ >= 0 && contactLength_ > 0.f;
    p.aoOn    = aoOn_ || contact;
    if (!aoOn_ && contact) p.aoStrength = 1.f;
    p.bloomOn = bloomOn_;
    // 카메라 노출 보정(EV, 2026-11-17 노출 배선): 기준 노출 × 2^EV. 카메라가 없거나 0 이면 기준 그대로.
    p.exposure *= std::exp2(cameraExposureEv_);
    p.vignette    = cameraVignette_;      // 접근성(Docs/Accessibility.md D6 · D9)
    p.colorFilter = cameraColorFilter_;
    return p;
}

void Renderer::updateUBO(const RenderFog& fog, uint32_t lightCount, const std::vector<RenderDecal>& decals) {
    FrameUBO ubo{};
    const PostPass::Params post = postParams();

    // 눈별 행렬과 위치(DEF-09). 단안이면 [1] 에 [0] 을 그대로 둡니다. 카메라가 없으면 views_ 가
    // 단위 행렬 · 원점이라 예전과 같습니다.
    for (uint32_t i = 0; i < kMaxViews; ++i) {
        const EyeView& v = views_[std::min(i, viewCount_ - 1)];
        ubo.viewProj[i] = hasCamera_ ? v.proj * v.view : glm::mat4(1.f);
        ubo.eyePos[i]   = glm::vec4(v.pos, v.zNear);
    }
    ubo.screenParams = glm::vec4((float)rc_.renderExtent.width, (float)rc_.renderExtent.height,
                                 views_[0].zFar, post.aoStrength);

    // Directional light shadow camera: 머리(viewCenter_) 중심 반지름 kRadius 구를 덮는 정사영(ShadowFit.hpp, 2026-11-02).
    // 캐스터 선택은 updateLightSSBO 가 이미 끝냈습니다(shadowCasterSlot_/Dir_). 여기서 다시
    // 고르지 않으므로 선택 기준이 갈릴 여지가 없습니다(DEF-26). 캐스터가 없으면 이전 VP 를 유지.
    // 행렬 계산과 단위 시험은 ShadowFit.hpp · HarukaLightTest 6절에 있습니다.
    if (shadowCasterSlot_ >= 0) {  // 액터 회전 반영 → Sun 을 돌리면 그림자도 회전
        const hshadow::DirectionalFit fit = hshadow::fitDirectional(viewCenter_, shadowCasterDir_, shadow_.farRadius(),
                                                                    shadow_.resolution(), rc_.vk.depthClamp);
        lightViewProj_     = fit.viewProj;
        lightCullViewProj_ = fit.cullViewProj;   // 먼 단의 컬링 부피가 가까운 단을 품으므로 캐스터 목록은 하나
        if (shadow_.cascades() >= 2) {
            const hshadow::DirectionalFit nearFit = hshadow::fitDirectional(viewCenter_, shadowCasterDir_, hshadow::kNearRadius,
                                                                            shadow_.resolution(), rc_.vk.depthClamp);
            lightViewProjNear_ = nearFit.viewProj;
        }
    }
    ubo.lightViewProj     = lightViewProj_;
    ubo.lightViewProjNear = lightViewProjNear_;
    if (shadow_.cascades() < 2) {
        ubo.cascadeNear = glm::vec4(0.f);   // 1 단(품질 단계): 페이드 끝 0 이라 셰이더가 늘 먼 단을 읽음
    } else {   // 가까운 캐스케이드 상수(CSM): 먼 단과 같은 규칙을 그 단의 텍셀 · 깊이 범위로 계산합니다.
        const float nearTexel = 2.f * hshadow::kNearRadius / (float)shadow_.resolution();
        const float nearRange = hshadow::depthRangeFor(hshadow::kNearRadius, rc_.vk.depthClamp);
        ubo.cascadeNear = glm::vec4(hshadow::kFadeStartFrac * hshadow::kNearRadius,
                                    hshadow::kNearRadius - hshadow::kFadeEndTexels * nearTexel,
                                    hshadow::receiverBiasNdc(ShadowPass::kReceiverBiasTexels * nearTexel, nearRange),
                                    ShadowPass::kNormalOffsetTexels * nearTexel);
    }
    // 캐스터가 없어도 늘 채웁니다(셰이더는 miscParams.w < 0 이면 섀도맵을 읽지 않음). 값은 반지름 · 해상도 ·
    // depthClamp 로만 정해지므로 fitDirectional 의 결과와 같습니다.
    const float shadowRes        = (float)shadow_.resolution();
    const float shadowRadius     = shadow_.farRadius();
    const float shadowTexelWorld = 2.f * shadowRadius / shadowRes;
    const float shadowDepthRange = hshadow::depthRangeFor(shadowRadius, rc_.vk.depthClamp);
    ubo.shadowParams = glm::vec4(shadowTexelWorld, 1.f / shadowRes,
                                 hshadow::receiverBiasNdc(ShadowPass::kReceiverBiasTexels * shadowTexelWorld, shadowDepthRange),
                                 ShadowPass::kNormalOffsetTexels * shadowTexelWorld);
    ubo.shadowFade = glm::vec4(hshadow::kFadeStartFrac * shadowRadius,
                               shadowRadius - hshadow::kFadeEndTexels * shadowTexelWorld, 0.f, 0.f);

    ubo.timeParams  = glm::vec4(shaderTime(), (float)frameNum_, lighting_calibration::kBloomThreshold,
                                post.exposure);
    // 반사 캡처(2026-11-17): 위치 · 반경(0 = 없음) · 밝기
    ubo.capturePos    = capturePos_;
    ubo.captureParams = glm::vec4(captureIntensity_, 0.f, 0.f, 0.f);
    // 상자 투영 데칼(2026-11-20, D11): 두 눈 가운데에서 가까운 것부터 HARUKA_MAX_DECALS 개(같은 거리는 입력 순서).
    {
        std::vector<std::pair<float, uint32_t>> order;
        order.reserve(decals.size());
        for (uint32_t i = 0; i < (uint32_t)decals.size(); ++i) {
            const glm::vec3 d = decals[i].center - viewCenter_;
            order.push_back({ glm::dot(d, d), i });
        }
        if (order.size() > (size_t)HARUKA_MAX_DECALS)
            std::sort(order.begin(), order.end());
        const uint32_t n = (uint32_t)std::min(order.size(), (size_t)HARUKA_MAX_DECALS);
        for (uint32_t k = 0; k < n; ++k) {
            const RenderDecal& d = decals[order[k].second];
            GpuDecal& g  = ubo.decals[k];
            g.worldToBox = d.worldToBox;
            g.color      = glm::vec4(d.color, d.opacity);
            g.axis       = glm::vec4(d.axis, d.roughness);
            g.mode       = glm::ivec4(d.blend, d.shape, 0, 0);
        }
        ubo.decalInfo = glm::ivec4((int)n, 0, 0, 0);
        decalCount_   = n;
    }
    // 지수 고도 안개(2026-11-17): 켜진 첫 HeightFogComponent. 없으면 밀도 0(셰이더가 건너뜀).
    ubo.fogColor  = glm::vec4(0.f);
    ubo.fogParams = glm::vec4(0.f);
    if (quality_.heightFog && fog.present) {
        ubo.fogColor  = glm::vec4(fog.color, fog.maxOpacity);
        ubo.fogParams = glm::vec4(fog.density, fog.heightFalloff, fog.height, fog.startDistance);
    }
    // lights 개수는 updateLightSSBO가 실제로 채운 수를 쓴다(범위 밖 읽기 방지).
    // miscParams.w 는 이제 **섀도 캐스터의 슬롯 번호**다(<0 이면 섀도 없음). 셰이더가 슬롯 0 을
    // 가정하지 않고, 그 라이트의 기여에만 그림자를 곱하도록 바뀌었다(DEF-26).
    const int shadowSlot = (shadowCasterSlot_ >= 0 && shadow_.valid()) ? shadowCasterSlot_ : -1;
    // screenParams.w(AO 세기) · miscParams.z(AO 켜기)는 지금 셰이더가 읽지 않습니다. 간접광에 AO 를
    // 적용할 때(ssao_place-7) pbr.frag 가 읽을 자리라 PostPass 와 같은 값을 채워 둡니다.
    ubo.miscParams  = glm::ivec4((int)lightCount, (int)frameNum_, post.aoOn ? 1 : 0, shadowSlot);

    memcpy(frames_.uboPtr(frameIdx), &ubo, sizeof(FrameUBO));
}

// ── buildDrawCommands ─────────────────────────────────────────────────────────

void Renderer::buildDrawCommands(const RenderSceneInput& scene,
                                  std::vector<GpuDrawCmd>& opaqueOut,
                                  std::vector<GpuDrawCmd>& transpOut,
                                  std::vector<const MaterialHandle*>& materialRowsOut,
                                  std::vector<uint8_t>& rowFlagsOut,
                                  std::vector<GpuDrawCmd>& shadowOut) {
    // 눈마다 절두체를 뽑아, 하나라도 통과하면 그립니다(DEF-31). 예전에는 단안 절두체 하나로
    // 걸러 스테레오에서는 한쪽 눈에만 보이는 물체가 사라질 수 있었습니다. 같은 목록을 두 눈이
    // 함께 그리고, 각 눈의 절두체 밖 부분은 GPU 가 잘라 냅니다.
    // 컬링과 렌더와 제출이 이 프레임의 같은 views_ 를 쓰므로 재투영 여유분(DEF-32)은 필요 없습니다.
    // 늦은 자세 갱신(late latching)이나 과주사(overscan)를 넣으면 그때 평면을 바깥으로 돌려야 합니다.
    std::array<std::array<FrustumPlane, 6>, kMaxViews> planes{};
    const bool hasCam = hasCamera_;
    if (hasCam)
        for (uint32_t i = 0; i < viewCount_; ++i) {
            planes[i] = extractFrustumPlanes(views_[i].proj * views_[i].view);
            replaceInfinitePlane(planes[i], views_[i].view, views_[i].zFar);   // 무한 원평면(C1) → 카메라 zFar 까지만 그림
        }

    // 섀도(라이트) 프러스텀 — 캐스터가 있을 때만. lightCullViewProj_ 는 updateUBO 가 이미
    // 채웠다(호출 순서: updateLightSSBO→updateUBO→buildDrawCommands). 이 ortho 박스로 캐스터를
    // 걸러, 카메라 밖이지만 라이트 안에 있는 오브젝트도 그림자를 던지게 한다(DEF-27).
    // 컬링 near 는 투영과 달리 늘 빛 쪽으로 100 m 넓어져 있어(hshadow::kCasterReach) 그림자 구보다 높은
    // 캐스터도 그림자를 던진다(depthClamp 가 있으면 깊이 0 에 눌러 담김). near 평면을 빼지 않고 늘린 이유는
    // depthClamp 가 없을 때 투영도 같은 near 를 써서 둘이 같아야 하기 때문입니다.
    const bool hasShadow = (shadowCasterSlot_ >= 0) && shadow_.valid();
    std::array<FrustumPlane, 6> lightPlanes{};
    if (hasShadow)
        lightPlanes = extractFrustumPlanes(lightCullViewProj_);

    auto* tfData = static_cast<GpuTransform*>(frames_.transformPtr(frameIdx));

    uint32_t totalCnt = (uint32_t)scene.meshes.size();

    // LOD 와 투명 정렬의 기준점은 두 눈의 가운데입니다. 눈마다 다르게 고르면 두 눈이 다른 LOD 나
    // 다른 그리기 순서를 보게 됩니다. 단안이면 카메라 위치 그대로입니다.
    const glm::vec3 camPos = viewCenter_;

    // 화면 크기(수학 보강 O6 · O7, ScreenSize.hpp): 눈 가운데에서 잰 거리와, 눈 중 가장 크게 보는 투영의 y 배율로 셉니다.
    // 카메라가 없으면 재지 않습니다(가장 고운 LOD, 작은 물체 컬링 없음).
    float projY = 0.f;
    if (hasCam)
        for (uint32_t i = 0; i < viewCount_; ++i) projY = std::max(projY, std::fabs(views_[i].proj[1][1]));
    const bool  sizeOk   = hasCam && projY > 0.f && rc_.renderExtent.height > 0;
    const float heightPx = (float)rc_.renderExtent.height;
    auto sphereScreenSize = [&](const glm::vec3& wCenter, float wRadius) {
        return sizeOk ? hscreen::screenSize(wRadius, glm::length(wCenter - camPos), projY) : 1e9f;
    };
    auto maxAxisScale = [](const glm::mat4& m) {
        return std::max({ glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])) });
    };

    opaqueOut.clear();
    transpOut.clear();
    materialRowsOut.clear();
    rowFlagsOut.clear();
    shadowOut.clear();

    // 1) 컬링과 LOD 선택: 카메라나 라이트에 보이는 메시만 후보로 모읍니다. 행은 여기서 배정하지 않습니다.
    // 투명 정렬 키(거리 제곱)도 여기서 한 번만 계산합니다. 예전에는 정렬 비교마다 매핑된 transform
    // SSBO(비디오 메모리)를 다시 읽었습니다(DEF-30).
    std::vector<hbatch::Candidate>     cands;
    std::vector<glm::mat4>             models;      // cands 와 같은 순서
    std::vector<const MaterialHandle*> materials;   // cands 와 같은 순서
    cands.reserve(scene.meshes.size());
    models.reserve(scene.meshes.size());
    materials.reserve(scene.meshes.size());

    // 머티리얼이 없는 메시는 오류 머티리얼(자홍색)로 그리고 액터마다 한 번 경고합니다(DEF-28).
    // 건너뛰지 않는 이유: 빠진 메시가 화면에서 보여야 알아챌 수 있습니다. 또 materialRowsOut
    // 에 nullptr 이 들어가지 않는다는 불변식을 이 대체가 지킵니다. null 이면(오류 머티리얼을 만들기 전, 초기화 중) 건너뜁니다.
    auto resolveMaterial = [&](const MaterialHandle* material, uint64_t actorId, const char* debugName) -> const MaterialHandle* {
        if (material) return material;
        if (noMaterialWarned_.insert(actorId).second) {
            HLOG(LogRenderer, Warning) << "Mesh on actor '"
                << (debugName ? debugName : "?")
                << "' has no material; drawing it with the magenta error material";
        }
        return errorMaterial_;
    };

    // 월드 공간 바운딩 구를 여기서 한 번만 만들어 카메라·라이트 판정에 함께 쓰고, 보이면 후보로 넣습니다.
    // 스태틱 메시와 스킨 메시(2026-11-07)가 함께 씁니다.
    auto pushCandidate = [&](const MaterialHandle* material, const glm::mat4& model, const glm::vec3& boundCenter,
                             float boundRadius, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset,
                             float distSq, bool transparent, bool index16, uint8_t lightChannels) {
        const glm::vec3 wCenter = glm::vec3(model * glm::vec4(boundCenter, 1.f));
        const float     wRadius = boundRadius * maxAxisScale(model);

        // 카메라 가시성과 라이트(섀도) 가시성을 따로 판정한다. 카메라 쪽은 화소 하나보다 작게 보이는 물체도 뺍니다(O7).
        // 그림자 목록은 그대로라 작은 캐스터의 그림자는 남습니다.
        const bool inCam   = !hasCam || (sphereInAnyView(planes.data(), viewCount_, wCenter, wRadius) &&
                                         !(sizeOk && hscreen::tooSmall(sphereScreenSize(wCenter, wRadius), heightPx)));
        const bool inLight = hasShadow && sphereInFrustum(lightPlanes, wCenter, wRadius);
        if (!inCam && !inLight) return;

        hbatch::Candidate c;
        c.material     = material;
        c.indexCount   = indexCount;
        c.firstIndex   = firstIndex;
        c.vertexOffset = vertexOffset;
        c.distSq       = distSq;
        c.inCam        = inCam;
        c.inLight      = inLight;
        c.transparent  = transparent;
        c.index16      = index16;
        c.lightChannels = lightChannels;
        // 거울 변환(음수 크기 · 뒤집힌 노드)은 감기 방향이 뒤집힙니다. 앞면 방향이 반대인 파이프라인으로 그립니다(DEF-170).
        c.mirrored     = glm::determinant(glm::mat3(model)) < 0.f;
        cands.push_back(c);
        models.push_back(model);
        materials.push_back(material);
    };

    ++lodStamp_;
    for (const RenderMesh& rm : scene.meshes) {
        if (!rm.mesh) continue;
        const MaterialHandle* material = resolveMaterial(rm.material, rm.actorId, rm.debugName);
        if (!material) continue;

        // LOD: 가장 고운 메시(rm.mesh)의 바운딩 구가 화면에서 차지하는 크기로 고릅니다(O6). distSq 는 투명 정렬 키입니다.
        const float distSq = glm::length2(camPos - glm::vec3(rm.model[3]));
        const MeshHandle* mesh = rm.mesh;
        if (rm.lodMeshCount > 0) {
            LodState& st = lodStates_[rm.key];
            if (st.mesh != rm.mesh) {
                st.mesh    = rm.mesh;
                st.current = 0;
            }
            st.stamp = lodStamp_;
            const float ss = sphereScreenSize(glm::vec3(rm.model * glm::vec4(rm.mesh->boundCenter, 1.f)),
                                              rm.mesh->boundRadius * maxAxisScale(rm.model));
            const hscreen::LodPick pick = hscreen::pickLodMesh(
                ss, rm.mesh, rm.lodMeshes, rm.lodMeshCount,
                rm.lodScreenSizes, rm.lodScreenSizeCount, st.current, rm.autoLod);
            st.current = pick.level;
            mesh       = pick.mesh;
            if (frameNum_ == 0 && mesh) {
                HLOG(LogRenderer, Log) << "LOD pick: " << (rm.debugName ? rm.debugName : "?")
                                       << " lod=" << st.current << "/" << rm.lodMeshCount
                                       << " screenSize=" << ss << " tris=" << (mesh->indexCount / 3);
            }
        }
        if (!mesh) continue;

        pushCandidate(material, rm.model, mesh->boundCenter, mesh->boundRadius, mesh->indexCount, mesh->firstIndex,
                      (int32_t)mesh->firstVertex, distSq, rm.transparent, mesh->index16, rm.lightChannels);
    }
    if (!lodStates_.empty()) {
        std::erase_if(lodStates_, [&](const auto& kv) { return kv.second.stamp != lodStamp_; });
    }

    // 스킨 메시(2026-11-07, Hand_Grasp_Design 4-1): 이 프레임 슬롯의 스킨 구간에 지금 자세로 CPU 스키닝한 정점을 직접 쓰고,
    // 그 자리를 정점 오프셋으로 그립니다. 이 함수는 슬롯 펜스를 기다린 뒤(extract 구간)에 불리므로 GPU 가 이 슬롯의 구간을
    // 읽고 있지 않습니다. Zone B 는 HOST_COHERENT 라 제출 시점에 쓰기가 보입니다. 인덱스는 쉬는 자세로 올린 메시의 것을 씁니다.
    {
        uint32_t      arenaFirst = 0, used = 0;
        PackedVertex* arena      = nullptr;
        bool     asked      = false;
        for (const RenderSkinnedMesh& sk : scene.skinned) {
            if (!asked) { arena = scene_.skinArena(frameIdx, arenaFirst); asked = true; }
            if (!arena) break;
            ++totalCnt;
            const uint32_t n = sk.vertCount;
            if (used + n > GpuScene::kSkinArenaVerts) {
                if (!skinOverflowWarned_) {
                    skinOverflowWarned_ = true;
                    HLOG(LogRenderer, Warning) << "Skinned meshes need more than " << GpuScene::kSkinArenaVerts
                        << " vertices this frame; '" << (sk.debugName ? sk.debugName : "?") << "' and later ones are not drawn (warned once)";
                }
                continue;
            }
            const MaterialHandle* material = resolveMaterial(sk.material, sk.actorId, sk.debugName);
            if (!material) continue;
            glm::vec3 bmin, bmax;
            // CPU 스키닝은 Vertex(MeshSourceVertex)로 하고 GPU 형식으로 옮깁니다(QTangent, C2).
            skinScratch_.resize(n);
            if (!sk.source || !sk.source->skinInto(reinterpret_cast<MeshSourceVertex*>(skinScratch_.data()), bmin, bmax)) continue;
            hqtan::packRange(skinScratch_.data(), arena + used, n);
            const float distSq = glm::length2(camPos - glm::vec3(sk.model[3]));
            pushCandidate(material, sk.model, 0.5f * (bmin + bmax), 0.5f * glm::length(bmax - bmin),
                          sk.baseMesh->indexCount, sk.baseMesh->firstIndex, (int32_t)(arenaFirst + used),
                          distSq, sk.transparent, sk.baseMesh->index16, sk.lightChannels);
            used += n;
        }
    }

    // 2) 묶기와 행 배정(DEF-16). transform SSBO 행은 hbatch::build 한 곳에서만 정해집니다(DEF-01 원칙).
    // 같은 지오메트리 · 머티리얼의 불투명 메시는 연속된 행을 받아 드로우 하나(instanceCount = N)가
    // 됩니다. 카메라 쪽이 예산을 먼저 쓰고, 라이트에만 보이는 캐스터는 남은 행만 씁니다(불변식: 라이트
    // 전용 행은 섀도맵에만 영향). 넘친 메시는 Dropped 로 셉니다(DEF-10).
    const hbatch::Result batch = hbatch::build(cands, maxDrawRows_, hasCam);

    // 3) 행마다 트랜스폼과 머티리얼을 기록합니다. 행 번호와 push 순서가 1:1이므로 materialRowsOut[row]
    // 가 곧 그 행의 머티리얼입니다. 섀도 전용 행에도 기록해 materialForRow 의 범위 계약을 지킵니다.
    materialRowsOut.reserve(batch.rowSource.size());
    rowFlagsOut.reserve(batch.rowSource.size());
    for (uint32_t row = 0; row < (uint32_t)batch.rowSource.size(); ++row) {
        const uint32_t src = batch.rowSource[row];
        packAffine(models[src], tfData[row]);
        materialRowsOut.push_back(materials[src]);
        rowFlagsOut.push_back((uint8_t)((cands[src].mirrored ? kRowMirrored : 0) | (cands[src].index16 ? kRowIndex16 : 0) |
                                        ((cands[src].lightChannels & 7u) << kRowChannelShift)));
    }

    auto toCmds = [](const std::vector<hbatch::Run>& runs, std::vector<GpuDrawCmd>& out) {
        out.reserve(runs.size());
        for (const auto& r : runs)
            out.push_back({ r.indexCount, r.instanceCount, r.firstIndex, r.vertexOffset, r.firstInstance });
    };
    toCmds(batch.opaque, opaqueOut);
    toCmds(batch.transparent, transpOut);   // 뒤에서 앞으로 정렬된 순서
    toCmds(batch.shadow, shadowOut);

    frameStats_.instances = batch.opaqueInstances;
    frameStats_.dropped   = batch.droppedCam;
    frameStats_.culled    = totalCnt - (batch.opaqueInstances + (uint32_t)transpOut.size()) - batch.droppedCam;
    frameStats_.transparentDraws = (uint32_t)transpOut.size();
    if ((batch.droppedCam || batch.droppedShadow) && !dropWarned_) {
        dropWarned_ = true;
        HLOG(LogRenderer, Warning) << "Draw row budget render.maxDrawRows=" << maxDrawRows_ << " exceeded: "
            << batch.droppedCam << " visible mesh(es) and " << batch.droppedShadow
            << " shadow-only caster(s) were not drawn (warned once; see the Dropped stat, raise render.maxDrawRows in Config/engine.ini)";
    }
}

// ── gatherViews ────────────────────────────────────────────────────────────────
// 이번 프레임에 그릴 눈을 정합니다. 단안은 활성 카메라 하나, 스테레오 흉내 모드는 카메라 좌우의
// 두 눈입니다(DEF-09). 눈 위치는 뷰 행렬의 역에서 읽습니다(예전 cameraPos 와 같은 식).
void Renderer::gatherViews(const RenderCamera& cam, const XrFrameTarget* xr) {
    cameraExposureEv_  = cam.present ? cam.exposureEv : 0.f;
    cameraVignette_    = cam.present ? std::clamp(cam.vignette, 0.f, 1.f) : 0.f;
    cameraColorFilter_ = cam.present ? cam.colorFilter : 0;
    if (captureOverride_) {   // 반사 캡처 중: 큐브 한 면의 눈 하나(Renderer_capture.cpp)
        hasCamera_ = true;
        viewCount_ = 1;
        views_[0]  = captureEye_;
        return;
    }

    // XR: 런타임이 준 두 눈 자세(원점 공간)를 활성 카메라의 XR 원점(위치 · 요)에 올립니다. 시야는
    // 런타임의 네 방향 각도로 눈마다 따로 만듭니다(DEF-21). 카메라가 없으면 월드 원점에 둡니다.
    if (xr && xrActive_) {
        const glm::mat4 origin = cam.present ? cam.xrOrigin : glm::mat4(1.f);
        const float     zNear  = cam.present ? cam.zNear : 0.1f;
        const float     zFar   = cam.present ? cam.zFar  : 100.f;
        hasCamera_ = true;
        viewCount_ = kMaxViews;
        for (uint32_t e = 0; e < kMaxViews; ++e) {
            const XrEyePose& p = xr->eyes[e];
            const glm::mat4 eyeWorld = origin * glm::translate(glm::mat4(1.f), p.position)
                                     * glm::mat4_cast(glm::quat(p.qw, p.qx, p.qy, p.qz));
            EyeView& v = views_[e];
            v.view  = glm::inverse(eyeWorld);
            v.pos   = glm::vec3(eyeWorld[3]);
            v.zNear = zNear;
            v.zFar  = zFar;
            v.proj  = hmath::perspectiveFromFov(p.angleLeft, p.angleRight, p.angleUp, p.angleDown, zNear);
        }
        return;
    }

    hasCamera_ = cam.present;
    if (!cam.present) {
        viewCount_ = 1;
        views_[0]  = EyeView{};   // 단위 행렬 · 원점 · near 0.1 · far 100 (예전 기본값)
        return;
    }
    const glm::mat4 camView = cam.view;

    // 단안. 투영 비율은 합성할 뷰포트의 비율입니다. 평소에는 씬 타깃과 같은 크기이고, XR 이 붙어 있는데
    // 이번 프레임 헤드셋에 그리지 않을 때(헤드셋을 벗음 등)는 씬 타깃이 눈 크기라 비율이 다릅니다.
    if (!emulateStereo_ || xrActive_) {
        const float aspect = (float)rc_.viewportExtent.width / (float)rc_.viewportExtent.height;
        viewCount_ = 1;
        EyeView& v = views_[0];
        v.view  = camView;
        v.proj  = hmath::perspectiveReverseZ(cam.fovY, aspect, cam.zNear);
        v.pos   = glm::vec3(glm::inverse(camView)[3]);
        v.zNear = cam.zNear;
        v.zFar  = cam.zFar;
        return;
    }

    // 스테레오 흉내: 카메라 좌우로 눈 사이 거리(64mm)의 절반씩 옮긴 평행한 두 눈입니다(Quest 처럼
    // 기울어지지 않음). 세로 시야는 카메라 FovY 를 그대로 쓰고, 가로는 눈 하나의 화면 비율로 정한 뒤
    // 코 쪽 45% · 바깥쪽 55% 로 나눠 실제 헤드셋처럼 비대칭 투영을 씁니다.
    const float     aspect  = (float)rc_.renderExtent.width / (float)rc_.renderExtent.height;
    constexpr float kIpd    = 0.064f;
    const float tanUp       = std::tan(glm::radians(cam.fovY) * 0.5f);
    const float tanWidth    = 2.f * tanUp * aspect;
    const float tanOuter    = 0.55f * tanWidth;
    const float tanInner    = 0.45f * tanWidth;
    viewCount_ = 2;
    for (uint32_t e = 0; e < 2; ++e) {
        const float side = (e == 0) ? -1.f : 1.f;   // 왼쪽 눈은 카메라의 -X 쪽
        EyeView& v = views_[e];
        // 카메라 공간에서 X 로 옮긴 눈: 월드 → 눈 = T(-offset) · 월드 → 카메라.
        v.view  = glm::translate(glm::mat4(1.f), glm::vec3(-side * kIpd * 0.5f, 0.f, 0.f)) * camView;
        v.pos   = glm::vec3(glm::inverse(v.view)[3]);
        v.zNear = cam.zNear;
        v.zFar  = cam.zFar;
        const float tanLeft  = (e == 0) ? -tanOuter : -tanInner;
        const float tanRight = (e == 0) ?  tanInner :  tanOuter;
        v.proj = hmath::perspectiveFromFov(std::atan(tanLeft), std::atan(tanRight),
                                           std::atan(tanUp), -std::atan(tanUp), cam.zNear);
    }
}

// ── gatherUIPanels (3D UI P1b) ─────────────────────────────────────────────────
// 패널마다 GPU 핸들과 크기를 렌더러 표(panelStates_)에 둡니다. 이번 프레임에 쓰지 않은 키는
// 사라진 패널(액터 삭제 · 비활성 · 월드 교체)의 것이라 핸들을 해제하고 지웁니다.
void Renderer::gatherUIPanels(const RenderSceneInput& scene, std::vector<GeometryPass::PanelDraw>& out) {
    out.clear();
    ++panelStamp_;
    for (const RenderUIPanel& rp : scene.panels) {
        if (!rp.source) continue;
        // 키가 컴포넌트 주소로 고유하므로, 예전 값 복사 패널의 핸들 중복 검사는 불필요합니다.
        PanelState& st = panelStates_[rp.key];
        st.stamp = panelStamp_;
        const glm::uvec2 want = rp.textureSize;
        if (st.handle && (!panels_.valid(st.handle) || st.size != want)) {
            panels_.release(st.handle);   // 무효 핸들이면 아무것도 하지 않음
            st.handle = 0;
        }
        if (!st.handle) {
            st.handle = createUIPanelTarget(want.x, want.y);
            if (!st.handle) continue;
            st.size = want;
            rp.source->markDirty();
        }

        // 더러움 표시는 추출 단계가 아니라 여기서(drawFrame 이 실제로 그려질 때) 소비합니다.
        HSlateContext& ctx = rp.source->slate();   // 처음이면 트리를 만들고 더럽게 표시
        if (rp.source->takeDirty()) panels_.requestRedraw(st.handle);
        if (HSlateRenderer* ui = panelUi()) panels_.update(st.handle, *ui, ctx);

        GeometryPass::PanelDraw d;
        d.model      = rp.model;
        d.view       = panels_.view(st.handle);
        d.twoSided   = rp.twoSided;
        d.brightness = rp.brightness;
        if (d.view) out.push_back(d);
    }
    // panelStates_ 가 패널 핸들의 유일한 소유자이므로 표에 없는 키를 지우면서 핸들을 해제합니다.
    for (auto it = panelStates_.begin(); it != panelStates_.end(); ) {
        if (it->second.stamp != panelStamp_) {
            if (it->second.handle) panels_.release(it->second.handle);
            it = panelStates_.erase(it);
        } else {
            ++it;
        }
    }
}

// ── 빌보드 파티클 (2026-11-20, Docs/Particles_And_Decals.md D9 · D10) ─────────────────────────────────────
void Renderer::gatherParticles(const RenderSceneInput& scene) {
    HPROF_SCOPE("Renderer.Particles");
    particleList_ = GeometryPass::ParticleList{};
    if (scene.particles.empty()) { particleInst_.clear(); particleRanges_.clear(); return; }
    if (!particleAtlas_.imageView) {
        std::vector<uint8_t> px;
        vfx::buildSpriteAtlas(px);
        particleAtlas_ = scene_.uploadTexture(px.data(), vfx::kAtlasSize, vfx::kAtlasSize, VK_FORMAT_R8G8B8A8_UNORM);
    }
    buildParticleStream(scene.particles, viewCenter_, GeometryPass::kMaxParticleInstances, particleInst_, particleRanges_,
                        particleScratch_);
    particleList_.data    = particleInst_.data();
    particleList_.count   = (uint32_t)particleInst_.size();
    particleList_.ranges  = &particleRanges_;
    particleList_.atlas   = particleAtlas_.imageView;
    particleList_.sampler = panelSampler_;
}

// ── extractRenderView ──────────────────────────────────────────────────────────
// 렌더러는 RenderSceneInput 만 소비합니다(추출은 components/RenderExtract, S5b~d). 이후 패스 기록에서도
// World/Actor/Component 를 참조하지 않습니다.
// v1: 프레임별 mapped 버퍼(UBO/라이트/트랜스폼) 업로드를 추출 단계에서 수행.

Renderer::RenderView Renderer::extractRenderView(const RenderSceneInput& scene, const XrFrameTarget* xr) {
    RenderView v;

    // 이번 프레임의 눈을 먼저 정합니다. 아래 UBO · 컬링 · AO 가 모두 이 한 벌을 씁니다.
    gatherViews(scene.camera, xr);
    // 두 눈의 가운데(단안이면 카메라 위치, 카메라가 없으면 원점). 라이트 선택 · LOD · 투명 정렬이 함께 씁니다.
    viewCenter_ = glm::vec3(0.f);
    if (hasCamera_) {
        for (uint32_t i = 0; i < viewCount_; ++i) viewCenter_ += views_[i].pos;
        viewCenter_ /= (float)viewCount_;
    }

    // 순서 주의: 라이트 SSBO를 먼저 채워 실제 개수를 확정한 뒤 UBO에 싣는다.
    const uint32_t lightCount = updateLightSSBO(scene.lights);
    updateUBO(scene.fog, lightCount, scene.decals);
    buildDrawCommands(scene, v.opaqueDraws, v.transpDraws, v.materialRows, v.rowFlags, v.shadowDraws);
    frameStats_.drawCalls = (uint32_t)v.opaqueDraws.size();
    gatherUIPanels(scene, v.panels);
    gatherParticles(scene);

    if (hasCamera_) {
        for (uint32_t i = 0; i < viewCount_; ++i) {
            v.aoCam[i].zNear   = views_[i].zNear;
            v.aoCam[i].zFar    = views_[i].zFar;
            v.aoCam[i].invProj = glm::inverse(views_[i].proj);
            v.aoCam[i].proj    = views_[i].proj;
            v.aoCam[i].valid   = true;
            // 접촉 그림자(2026-11-17): 섀도 캐스터 방향광의 길이와 이 눈의 뷰 공간 태양 방향
            v.aoCam[i].contactLength = shadowCasterSlot_ >= 0 ? contactLength_ : 0.f;
            v.aoCam[i].sunView       = glm::normalize(glm::mat3(views_[i].view) * shadowCasterDir_);
            v.aoCam[i].aoTerm        = aoOn_;
        }
    }

    // CPU 드로우 항목 — buildDrawCommands가 만든 드로우 커맨드에서 그대로 파생시킨다.
    // World를 다시 순회하지 않으므로 인덱스가 어긋날 수 없다. firstInstance가 곧
    // transform SSBO 행이자 materialRows의 인덱스이므로 역참조는 O(1)이다.
    // (이전 판은 World를 재순회하며 별도 카운터를 돌려, 컬링된 메시나 투명 메시가
    //  하나라도 있으면 모든 항목의 firstInstance가 어긋났다.)
    // row < materialRows.size()는 buildDrawCommands의 불변식이므로 항상 참이다(assert).
    // 그래도 nullptr 이 나오면 GeometryPass 가 그 항목을 세 서브패스 모두에서 건너뛴다.
    // 예전에는 머티리얼 푸시만 건너뛰고 그려서, 직전 메시의 텍스처로 그려졌다(DEF-28).
    auto materialForRow = [&v](uint32_t row) -> const MaterialHandle* {
        HCHECK_SLOW(row < v.materialRows.size() && "firstInstance가 materialRows 범위를 벗어났습니다");
        return row < v.materialRows.size() ? v.materialRows[row] : nullptr;
    };
    auto toItems = [&](const std::vector<GpuDrawCmd>& draws,
                       std::vector<GeometryPass::DrawItem>& itemsOut) {
        itemsOut.reserve(draws.size());
        for (const auto& dc : draws) {
            const uint8_t f = dc.firstInstance < v.rowFlags.size() ? v.rowFlags[dc.firstInstance] : 0;
            itemsOut.push_back({ materialForRow(dc.firstInstance), dc.indexCount,
                                 dc.firstIndex, dc.vertexOffset, dc.firstInstance,
                                 dc.instanceCount, (f & kRowMirrored) != 0, (f & kRowIndex16) != 0,
                                 (uint8_t)((f >> kRowChannelShift) & 7u) });
        }
    };
    toItems(v.opaqueDraws, v.opaqueItems);
    toItems(v.transpDraws, v.transparentItems);   // 정렬 완료 순서를 그대로 따른다

    return v;
}

// ── drawFrame ─────────────────────────────────────────────────────────────────

bool Renderer::drawFrame(const RenderSceneInput& scene, const XrFrameTarget* xr) {
    const bool xrFrame = (xr != nullptr) && xrActive_;
    // 데스크톱 창이 최소화되면(표면 크기 0) 스왑체인 이미지를 받을 수 없습니다. 데스크톱 화면은 건너뛰고,
    // XR 프레임이면 두 눈만 그려 헤드셋은 계속 보이게 합니다(DEF-106). UE · Godot 도 최소화한 창은 그리지
    // 않고 루프는 계속 돌립니다. 창이 없는 실행(--headless · Quest)은 desktop 이 늘 거짓입니다.
    const bool desktop = swapchain_->hasArea();
    lastFrameDesktop_  = false;
    if (!desktop && !xrFrame) {
        // 창도 XR 이미지도 없습니다. Android 에서 세션이 READY 되기 전과 같은 정상 상태라 쉬어 가되, 한 번은 알립니다.
        if (headless_ && !nothingToDrawLogged_) {
            nothingToDrawLogged_ = true;
            HLOG(LogRenderer, Log) << "Nothing to draw: no surface and no XR target";
        }
        return false;
    }
    auto& ft = gFrameTimings();
    ScopedCpuTimer timerWhole(ft.drawFrame);
    const uint32_t fi = frameIdx;
    constexpr uint32_t TS = kTimestampsPerFrame;

    // ── Wait for this frame slot ──────────────────────────────────────────────
    VkFence frameFence = frames_.fence(fi);
    {
        ScopedCpuTimer t(ft.fenceWait);
        vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX);
    }
    // 이 슬롯을 마지막으로 쓴 제출(frameNum_ - MAX_FRAMES)이 끝났으니, 그 프레임까지만 쓰던 패널 텍스처를 지웁니다.
    panels_.beginFrame(frameNum_);
    // 은퇴한 메시 · 머티리얼(비동기 로딩 D4)과 끝난 업로드의 스테이징도 같은 자리에서 놓습니다.
    scene_.collectRetired(frameNum_, MAX_FRAMES);
    scene_.collectUploads(false);
    // 반사 캡처: 캡처 컴포넌트가 새로 생기거나 옮겨지면 이 자리에서 장치를 멈추고 6면을 찍습니다(드묾).
    {
        ScopedCpuTimer t(ft.reflCapture);
        updateReflectionCapture(scene);
    }

    // Read previous frame's timestamps — skip until each slot has been written at least once
    if (timestampPool_ && frameNum_ >= MAX_FRAMES) {
        uint64_t ts[TS] = {};
        if (vkGetQueryPoolResults(device, timestampPool_, fi * TS, TS,
                sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            float ns2ms = timestampPeriod_ * 1e-6f;
            // 슬롯 배치: 0 시작 / 1 섀도 / 눈 v 마다 2+4v 메인(3서브패스+resolve) · 3+4v AO ·
            //           4+4v Bloom · 5+4v 톤매핑 / 마지막 UI
            // 패스별 시간은 두 눈의 합입니다. 눈 사이의 배리어는 다음 눈의 메인에 들어갑니다.
            // 예전에는 6 개를 기록하고 5 개만 읽어 UI 패스가 총합에서 빠져 있었고,
            // ts2 가 불투명 서브패스 직후에 찍혀 있어 라벨이 전부 어긋나 있었습니다.
            // 가짜 TAA 블릿 슬롯(빈 구간이라 늘 0ms)은 2026-10-04 에 지웠습니다(DEF-15).
            timings_.shadowMs = float(ts[1] - ts[0]) * ns2ms;
            float mainMs = 0.f, aoMs = 0.f, bloomMs = 0.f, toneMs = 0.f;
            uint64_t prev = ts[1];
            for (uint32_t v = 0; v < kMaxViews; ++v) {
                const uint32_t b = 2 + kTsPerView * v;
                mainMs  += float(ts[b]     - prev)      * ns2ms;   // 3 서브패스 + resolve
                aoMs    += float(ts[b + 1] - ts[b])     * ns2ms;
                bloomMs += float(ts[b + 2] - ts[b + 1]) * ns2ms;
                toneMs  += float(ts[b + 3] - ts[b + 2]) * ns2ms;
                prev = ts[b + 3];
            }
            timings_.mainMs    = mainMs;
            timings_.aoMs      = aoMs;
            timings_.bloomMs   = bloomMs;
            timings_.tonemapMs = toneMs;
            timings_.uiMs      = float(ts[TS - 1] - prev)  * ns2ms;
            timings_.totalMs   = float(ts[TS - 1] - ts[0]) * ns2ms;
        }
    }

    // ── Acquire swapchain image ───────────────────────────────────────────────
    // Pre-check: avoid acquiring into a stale swapchain, which would leave
    // semImageAvail_ signaled with no corresponding submit to consume it.
    uint32_t imgIdx = 0;
    VkResult res    = VK_SUCCESS;
    if (desktop) {
        if (swapchain_->needsRecreation()) { recreateSwapChain(); return false; }

        {
            ScopedCpuTimer t(ft.acquire);
            res = swapchain_->acquireNextImage(frames_.imageAvailSem(fi), imgIdx);
        }
        // VK_ERROR_OUT_OF_DATE_KHR: acquire failed, semaphore NOT signaled — safe to recreate now.
        // VK_SUBOPTIMAL_KHR: image acquired, semaphore IS signaled — finish the frame, recreate after present.
        if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapChain(); return false; }
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("Failed to acquire swapchain image!");

        auto& imagesInFlight = frames_.imagesInFlight();
        {
            ScopedCpuTimer t(ft.imgFence);
            if (imagesInFlight[imgIdx] != VK_NULL_HANDLE)
                vkWaitForFences(device, 1, &imagesInFlight[imgIdx], VK_TRUE, UINT64_MAX);
        }
        imagesInFlight[imgIdx] = frameFence;
    }
    vkResetFences(device, 1, &frameFence);

    // ── Extract: World를 읽는 유일한 구간 — 이후 기록은 view만 소비 ──────────
    RenderView view;
    {
        ScopedCpuTimer t(ft.extract);
        view      = extractRenderView(scene, xrFrame ? xr : nullptr);
        // A-4: GPU 컬 입력 업로드 제거. 깊이 프리패스가 CPU opaqueItems 를 직접 그립니다.
    }

    // ── Record command buffer ─────────────────────────────────────────────────
    const auto recordT0 = std::chrono::steady_clock::now();
    VkCommandBuffer cmd = frames_.cmd(fi);
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo cbbi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    if (vkBeginCommandBuffer(cmd, &cbbi) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin command buffer!");

    // Timestamp 0: frame start
    if (timestampPool_) {
        vkCmdResetQueryPool(cmd, timestampPool_, fi * TS, TS);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampPool_, fi*TS+0);
    }

    // 깊이 · MSAA · AO · Bloom 중간 타깃은 프레임 슬롯끼리 같이 씁니다. 앞 프레임의 읽기(AO 가 깊이를,
    // 톤매핑이 AO · Bloom 을 읽음)가 끝난 뒤에 이번 프레임이 쓰도록 한 번 묶습니다. 렌더 패스의 외부 의존성은
    // 조각 셰이더의 읽기를 포함하지 않아, 예전에는 앞 프레임의 읽기와 이번 프레임의 쓰기가 겹칠 수 있었습니다
    // (DEF-104). 배리어의 앞쪽 범위는 같은 큐에 먼저 제출된 명령 전체라 앞 프레임까지 닿습니다.
    // 눈 사이(아래 k > 0)도 같은 배리어입니다. 배리어는 rc_ 래퍼로만 기록합니다(sync2 또는 1.0, DEF-04).
    constexpr VkAccessFlags2 kTargetWrites = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    constexpr VkAccessFlags2 kTargetReads  = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                             VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    rc_.memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, kTargetWrites,
                           VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, kTargetReads);

    // ── 월드 UI 패널 (3D UI P1a) ──────────────────────────────────────────────
    // UI 정점 버퍼의 이 슬롯을 비웁니다. 패널과 에디터 오버레이가 한 프레임에 같은 슬롯 버퍼에 이어 쓰므로 UI 를
    // 기록하기 전에 한 번입니다. 다시 그릴 패널은 메인 패스(두 눈이 함께 읽음)보다 먼저 그리고 밉을 만듭니다.
    if (uiRenderer_)                                   uiRenderer_->beginFrame(fi);
    if (ownedPanelUi_ && ownedPanelUi_ != uiRenderer_) ownedPanelUi_->beginFrame(fi);
    if (panels_.hasQueued())
        if (HSlateRenderer* ui = panelUi()) panels_.record(cmd, *ui, fi);

    // ── Shadow pass ───────────────────────────────────────────────────────────
    // view.shadowDraws 는 buildDrawCommands 가 **라이트 프러스텀**으로 걸러 만든 캐스터
    // 목록이다(DEF-27). 불투명·투명을 모두 담고(투명은 알파 무시·실루엣 근사), firstInstance
    // 는 메인 패스와 공유하는 transform SSBO 의 유효한 행이라 그대로 그려진다. 카메라 밖·
    // 라이트 안의 캐스터도 여기 포함되므로 카메라를 돌려도 그림자가 사라지지 않는다.
    // 캐스터 라이트가 있으면 목록이 비어도 맵을 비워 기록한다(앞 프레임 그림자가 남지 않게).
    {
        HPROF_SCOPE("Renderer.RecordShadow");
        shadow_.record(cmd,
            { frames_.frameSet(fi), frames_.sceneSet(fi), frames_.transformSet(fi),
              scene_.vertexBuffer(), scene_.indexBuffer() },
            view.shadowDraws, view.rowFlags, shadowCasterSlot_ >= 0);
    }

    // Timestamp 1: after shadow
    if (timestampPool_)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi*TS+1);

    // A-4: GPU 컬링 dispatch 제거(순수 오버헤드였음). 깊이 프리패스·컬러 모두 CPU opaqueItems 를
    // 그립니다. GPU 구동 경로가 필요해지면 스테레오·인스턴스 기준으로 새로 설계합니다.

    // ── 메인(3 서브패스) → 눈마다 AO → Bloom → 톤매핑 ───────────────────────────
    // 두 눈이면 메인 패스를 멀티뷰로 한 번 그립니다(2026-11-18, DEF-09 둘째 단계). 깊이 · HDR 은 2층 배열이고 층 v 가
    // 눈 v 입니다. AO · Bloom · 톤매핑은 눈마다 그 층을 읽고, 그 중간 타깃(AO · Bloom)은 두 눈이 번갈아 씁니다(UE 의
    // Mobile Multi-View 도 장면 패스만 멀티뷰). --no-multiview 이거나 멀티뷰를 모르는 렌더 확장이 메인 패스 안에 있으면
    // 예전처럼 눈마다 메인 패스를 그립니다(층 v 에). 섀도맵은 위에서 한 번 그린 것을 두 눈이 함께 씁니다.
    GeometryPass::Inputs gin{};
    gin.frameSet         = frames_.frameSet(fi);
    gin.sceneSet         = frames_.sceneSet(fi);
    gin.transformSet     = frames_.transformSet(fi);
    gin.vertexBuffer     = scene_.vertexBuffer();
    gin.indexBuffer      = scene_.indexBuffer();
    gin.textureSampler   = textureSampler;
    gin.clampSampler     = clampSampler_;
    gin.opaqueItems      = &view.opaqueItems;
    gin.transparentItems = &view.transparentItems;
    gin.panelSampler     = panelSampler_;
    gin.panels           = &view.panels;
    gin.particles        = particleList_.count ? &particleList_ : nullptr;

    // 단안은 뷰포트 전체, 스테레오 흉내는 뷰포트의 왼쪽 · 오른쪽 절반, XR 은 스왑체인의 그 눈 층에
    // 합성합니다.
    auto outputFor = [&](uint32_t v) {
        PostPass::Output out;
        if (xrFrame) {
            out.fb       = xrFramebufs_[xr->imageIndex * kMaxViews + v];
            out.format   = xrFormat_;
            out.extent   = xrEyeExtent_;
            out.rect     = { { 0, 0 }, xrEyeExtent_ };
            out.clearAll = true;   // 눈마다 다른 층이라 둘 다 처음 쓰는 것
            return out;
        }
        out.fb       = post_.swapFramebuffer(imgIdx);
        out.format   = rc_.swapFormat;
        out.extent   = rc_.extent;
        out.clearAll = (v == 0);
        out.rect     = { rc_.viewportOffset, rc_.viewportExtent };
        if (viewCount_ > 1) {
            out.rect.offset.x    += (int32_t)(v * rc_.renderExtent.width);
            out.rect.extent.width = rc_.renderExtent.width;
        }
        return out;
    };

    // 렌더 확장 지점(DEF-17). 그 지점에 등록된 확장이 있을 때만 부르므로, 확장이 없으면 기록하는 명령이
    // 예전과 똑같습니다. 확장이 그리는 시간은 그 지점이 속한 타임스탬프 칸(Main · AO · Tonemap)에 들어갑니다.
    const bool extGeo   = passRegistry_.any(HRenderStage::OpaqueEnd) ||
                          passRegistry_.any(HRenderStage::TransparentEnd);
    const bool extScene = passRegistry_.any(HRenderStage::AfterScene);
    const bool extPost  = passRegistry_.any(HRenderStage::AfterTonemap);
    // 합성 값은 프레임에 한 번 정합니다. 두 눈과 XR 거울 화면이 같은 값을 씁니다.
    const PostPass::Params post        = postParams();
    const float            bloomThresh = lighting_calibration::kBloomThreshold;
    auto stageCtx = [&](HRenderStage stage, uint32_t v) {
        HRenderStageContext c;
        c.stage      = stage;
        c.cmd        = cmd;
        c.frameIndex = fi;
        c.viewIndex  = v;
        c.view       = views_[v].view;
        c.proj       = views_[v].proj;
        c.eyePos     = views_[v].pos;
        c.frameSet   = frames_.frameSet(fi);
        return c;
    };
    // AfterScene 은 렌더 패스 밖이라 확장이 그래픽 · 컴퓨트 어느 쪽으로든 HDR 을 읽고 쓸 수 있습니다.
    // 메인 패스의 resolve 결과가 보이도록 앞에, 확장의 쓰기가 AO · Bloom · 톤매핑에 보이도록 뒤에 둡니다.
    auto fullBarrier = [this, &cmd]() {
        rc_.memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    };

    // 멀티뷰로 그릴지는 프레임마다 정합니다. XR 이 붙어 있어도 헤드셋에 그리지 않는 프레임은 단안입니다.
    const bool multiview = viewCount_ > 1 && multiview_ && geometry_.multiviewReady() &&
                           !passRegistry_.blocksMultiview();
    // 두 눈 프레임의 경로가 바뀔 때만 한 번 남깁니다(단안 프레임은 세지 않음).
    if (viewCount_ > 1 && (int)multiview != stereoMainPassMode_) {
        stereoMainPassMode_ = (int)multiview;
        HLOG(LogRenderer, Log) << "Main pass: " << (multiview ? "multiview (both eyes at once)" : "per eye")
                               << (multiview ? "" : !multiview_ ? " (--no-multiview)"
                                                  : !geometry_.multiviewReady() ? " (scene targets have one layer)"
                                                  : " (a render extension does not support multiview)");
    }

    // XR 은 오른쪽 눈을 먼저 그립니다. 중간 타깃에 왼쪽 눈이 남아 있어야 아래에서 그것을 에디터
    // 뷰포트에 비출 수 있습니다.
    for (uint32_t k = 0; k < viewCount_; ++k) {
        HPROF_SCOPE("Renderer.RecordView");   // 눈 하나: 메인 · AO · Bloom · 톤매핑
        const uint32_t v      = xrFrame ? viewCount_ - 1 - k : k;
        const uint32_t layer  = geometry_.layerFor(v);   // 이 눈의 장면 타깃 층
        const uint32_t tsBase = fi * TS + 2 + kTsPerView * k;
        // 앞 눈이 쓰던 중간 타깃을 다음 눈이 다시 씁니다. 앞 눈의 읽기(AO 의 깊이, 톤매핑의 HDR ·
        // AO · Bloom)가 끝나고 쓰기가 보이도록 그래픽 단계 전체를 한 번 묶습니다. 눈이 둘뿐이라
        // 비용은 무시할 수준입니다.
        if (k > 0)
            rc_.memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, kTargetWrites,
                                   VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, kTargetReads);

        // ── Main render pass (3 subpasses) ────────────────────────────────────
        // 불투명 끝(서브패스 1)과 반투명 끝(서브패스 2)에서 확장을 부릅니다. 멀티뷰면 첫 눈 차례에 두 눈을 한 번에 그리고,
        // 둘째 눈의 Main 칸에는 타임스탬프만 찍습니다(Main 시간 = 두 눈 합, 읽는 쪽은 그대로).
        if (!multiview || k == 0) {
            std::function<void(uint32_t)> geoHook;
            if (extGeo)
                geoHook = [&, v](uint32_t subpass) {
                    HRenderStageContext c = stageCtx(subpass == 1 ? HRenderStage::OpaqueEnd
                                                                   : HRenderStage::TransparentEnd, multiview ? 0 : v);
                    c.multiview   = multiview;
                    c.renderPass  = multiview ? geometry_.mainPassMultiview() : geometry_.mainPass();
                    c.subpass     = subpass;
                    c.colorFormat = GeometryPass::kHdrFormat;
                    c.samples     = rc_.msaaSamples;
                    c.rect        = { { 0, 0 }, rc_.renderExtent };
                    passRegistry_.dispatch(c);
                };
            gin.subpassHook = extGeo ? &geoHook : nullptr;
            if (multiview) geometry_.recordMultiview(cmd, fi, gin, timestampPool_, tsBase + 0);
            else           geometry_.record(cmd, fi, v, gin, timestampPool_, tsBase + 0);
            gin.subpassHook = nullptr;   // geoHook 은 이 블록 안에서만 삽니다
        } else if (timestampPool_) {
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, tsBase + 0);
        }

        // ── 렌더 확장: 메인 패스 뒤 · AO 앞(AfterScene) ─────────────────────────
        if (extScene) {
            fullBarrier();
            HRenderStageContext c = stageCtx(HRenderStage::AfterScene, v);
            c.hdrImage    = geometry_.hdrImage(fi);
            c.hdrLayer    = layer;
            c.hdrView     = geometry_.hdrView(fi, layer);
            c.depthView   = geometry_.depthView(layer);
            c.sceneExtent = rc_.renderExtent;
            passRegistry_.dispatch(c);
            fullBarrier();
        }

        // ── AO (linDepth + SSAO, half-res) ───────────────────────────────────
        // 끈 패스는 기록하지 않습니다. 타임스탬프는 조건 밖에서 늘 씁니다(비우면 결과 읽기가 VK_NOT_READY).
        if (post.aoOn)
            ao_.record(cmd, fi, v, geometry_.depthView(layer), view.aoCam[v]);
        if (timestampPool_)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, tsBase + 1);

        // ── Bloom passes (threshold → down ×4 → up ×4) ───────────────────────
        if (post.bloomOn)
            bloom_.record(cmd, geometry_.hdrView(fi, layer), clampSampler_, bloomThresh);
        if (timestampPool_)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, tsBase + 2);

        // ── Post-process pass (ACES + bloom composite → swapchain) ───────────
        // T-c: 가짜 TAA 비활성. 지터·모션벡터·이웃클램프 없는 mix(hdr,history,0.1)는 AA 효과가
        // 0이면서 이동 시 고스팅과 노출 미적용(D-5) 밝기 오류만 냈습니다. AA 는 4x MSAA 가
        // 맡습니다. 진짜 TAA 는 OpenXR 이후 별도 트랙(Transparency_Temporal_Design.md 4절).
        const PostPass::Output out = outputFor(v);
        PostPass::OverlayHook postHook;
        if (extPost)
            postHook = [&, v, out](VkRenderPass rp) {
                HRenderStageContext c = stageCtx(HRenderStage::AfterTonemap, v);
                c.renderPass  = rp;
                c.colorFormat = out.format;
                c.rect        = out.rect;
                passRegistry_.dispatch(c);
            };
        post_.recordTonemap(cmd, out, post, geometry_.hdrView(fi, layer), ao_.ssaoView(),
                            bloom_.resultView(), clampSampler_, extPost ? &postHook : nullptr);
        if (timestampPool_)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, tsBase + 3);
    }
    // XR: 왼쪽 눈(중간 타깃에 남아 있음)을 에디터 뷰포트에 한 번 더 톤매핑해 비춥니다. 뷰포트 비율에
    // 맞춰 가운데를 잘라 읽습니다(UE 의 기본 관전 화면과 같은 방식). 이 시간은 UI 칸에 들어갑니다.
    // 데스크톱 창이 최소화돼 있으면 비출 곳이 없습니다.
    if (xrFrame && desktop) {
        PostPass::Output mirror;
        mirror.fb       = post_.swapFramebuffer(imgIdx);
        mirror.format   = rc_.swapFormat;
        mirror.extent   = rc_.extent;
        mirror.rect     = { rc_.viewportOffset, rc_.viewportExtent };
        mirror.clearAll = true;
        const float eyeAspect = (float)xrEyeExtent_.width / (float)xrEyeExtent_.height;
        const float vpAspect  = (float)rc_.viewportExtent.width / (float)rc_.viewportExtent.height;
        if (vpAspect > eyeAspect) {   // 뷰포트가 더 넓음 → 위아래를 자름
            mirror.uvScale[1]  = eyeAspect / vpAspect;
            mirror.uvOffset[1] = 0.5f * (1.f - mirror.uvScale[1]);
        } else {                      // 뷰포트가 더 좁음 → 좌우를 자름
            mirror.uvScale[0]  = vpAspect / eyeAspect;
            mirror.uvOffset[0] = 0.5f * (1.f - mirror.uvScale[0]);
        }
        rc_.memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                               VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        // 거울 화면에도 톤매핑 뒤 확장을 부릅니다(헤드셋과 같은 화면 효과가 보이도록). 왼쪽 눈 기준입니다.
        PostPass::OverlayHook mirrorHook;
        if (extPost)
            mirrorHook = [&, mirror](VkRenderPass rp) {
                HRenderStageContext c = stageCtx(HRenderStage::AfterTonemap, 0);
                c.mirror      = true;
                c.renderPass  = rp;
                c.colorFormat = mirror.format;
                c.rect        = mirror.rect;
                passRegistry_.dispatch(c);
            };
        post_.recordTonemap(cmd, mirror, post, geometry_.hdrView(fi, geometry_.layerFor(0)), ao_.ssaoView(),
                            bloom_.resultView(), clampSampler_, extPost ? &mirrorHook : nullptr);
    }

    // 그리지 않은 눈의 칸도 찍어 둡니다. 비워 두면 결과 읽기가 통째로 VK_NOT_READY 가 됩니다.
    if (timestampPool_)
        for (uint32_t q = 2 + kTsPerView * viewCount_; q < TS - 1; ++q)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi * TS + q);

    // ── UI overlay ────────────────────────────────────────────────────────────
    // UI 정점 버퍼는 fi 슬롯별입니다. 슬롯 수가 어긋나면 두 프레임이 한 버퍼를 같이 씁니다(DEF-93).
    static_assert(HSlateRenderer::kFramesInFlight == MAX_FRAMES,
                  "HSlateRenderer vertex buffers must match MAX_FRAMES");
    bool uiPresented = false;
    if (uiRenderer_ && desktop) {
        ScopedCpuTimer t(ft.uiRecord);
        uiPresented = uiRenderer_->record(cmd, imgIdx, swapchain_->extent(), fi);
    }
    if (desktop && !uiPresented) {
        // UI 패스가 기록되지 않았으면(게임 실행 파일처럼 UI 가 없거나, UI 가 그릴 것 없이 일찍 돌아감) 표시 전 레이아웃
        // 전환을 여기서 합니다(2026-10-24, DEF-120 은 2026-10-31). 예전에는 UI 패스의 렌더 패스(finalLayout = PRESENT_SRC)가
        // 이 일을 맡아, UI 없이 표시하면 톤매핑이 남긴 COLOR_ATTACHMENT 레이아웃 그대로 표시해 프레임마다 검증 오류
        // (VUID-VkPresentInfoKHR-pImageIndices-01430)가 났습니다.
        transitionImage(cmd, swapchain_->images()[imgIdx],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    }

    // 마지막 칸: after UI
    if (timestampPool_)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi*TS + TS - 1);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer!");
    ft.record.addSpan(recordT0, std::chrono::steady_clock::now());

    // ── Submit ────────────────────────────────────────────────────────────────
    // 데스크톱을 건너뛴 XR 프레임은 받은 스왑체인 이미지가 없어 기다릴 것도 알릴 것도 없습니다. 이미지별 세마포어는 데스크톱을
    // 그린 프레임에서만 읽습니다. 창이 없으면 이미지가 0장이라 예전에는 여기서 빈 목록을 읽었습니다(DEF-118).
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore waitSem   = desktop ? frames_.imageAvailSem(fi)    : VK_NULL_HANDLE;
    VkSemaphore signalSem = desktop ? frames_.renderDoneSem(imgIdx) : VK_NULL_HANDLE;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = desktop ? 1u : 0u;
    si.pWaitSemaphores      = desktop ? &waitSem : nullptr;
    si.pWaitDstStageMask    = desktop ? &waitStage : nullptr;
    si.commandBufferCount   = 1; si.pCommandBuffers   = &cmd;
    si.signalSemaphoreCount = desktop ? 1u : 0u;
    si.pSignalSemaphores    = desktop ? &signalSem : nullptr;
    {
        ScopedCpuTimer t(ft.submit);
        if (vkQueueSubmit(graphicsQueue, 1, &si, frameFence) != VK_SUCCESS)
            throw std::runtime_error("Failed to submit draw command!");
    }

    // ── Capture before present (swapchain image is still acquired) ────────────
    if (desktop && captureNextSwapchain_) {
        captureNextSwapchain_ = false;
        lastSwapCaptured_     = false;
        lastSwapUnsupported_  = false;
        lastSwapExtent_       = {};
        lastSwapRgba_.clear();
        if (!swapchain_->supportsTransferSrc()) {
            lastSwapUnsupported_ = true;
            if (!swapCaptureUnsupportedLogged_) {
                swapCaptureUnsupportedLogged_ = true;
                HLOG(LogRenderer, Warning)
                    << "Swapchain capture skipped: surface does not support VK_IMAGE_USAGE_TRANSFER_SRC_BIT";
            }
        } else {
            lastSwapExtent_ = swapchain_->extent();
            lastSwapCaptured_ = readbackImageToRgba(
                swapchain_->images()[imgIdx], 0, lastSwapExtent_, swapchain_->format(),
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                lastSwapRgba_);
        }
    }

    // ── Present ───────────────────────────────────────────────────────────────
    if (desktop) {
        ScopedCpuTimer t(ft.present);
        res = swapchain_->present(presentQueue, imgIdx, signalSem);
        if (swapchain_->needsRecreation()) recreateSwapChain();
        else if (res != VK_SUCCESS) throw std::runtime_error("Failed to present image!");
    }

    frameIdx  = (frameIdx + 1) % MAX_FRAMES;
    frameNum_++;
    lastFrameDesktop_ = desktop;
    // 파이프라인을 새로 만든 뒤 처음 그린 프레임에서 캐시를 저장합니다(DEF-153). 정리 경로를 거치지 않고 끝나는 실행(Android
    // 백그라운드 종료)에서도 다음 실행이 다시 컴파일하지 않습니다.
    if (pipelineCacheDirty_) {
        pipelineCacheDirty_ = false;
        savePipelineCache();
    }
    return xrFrame;
}
