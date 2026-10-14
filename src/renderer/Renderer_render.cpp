#include "Renderer.hpp"
#include "../ui/slate/HSlateRenderer.hpp"
#include "../core/World.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/LightComponent.hpp"
#include "../core/FrameTimings.hpp"
#include "RendererLog.hpp"
#include "DrawBatching.hpp"
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
#include <cassert>
#include <random>

// Cross-frame state for shadow VP
static glm::mat4 sCurLightViewProj{ 1.f };

// ── createCommandPool ─────────────────────────────────────────────────────────

void Renderer::createCommandPool() {
    auto idx = findQueueFamilies(physDevice);
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = idx.graphics.value();
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

    make(textureSampler, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
         true,  VK_BORDER_COLOR_INT_OPAQUE_BLACK,    16.f);
    make(clampSampler_,  VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         false, VK_BORDER_COLOR_INT_OPAQUE_BLACK,    16.f);
    // (shadow sampler는 ShadowPass가 소유)
    make(iblSampler_,    VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         false, VK_BORDER_COLOR_INT_OPAQUE_BLACK,    16.f);
    make(brdfSampler_,   VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         false, VK_BORDER_COLOR_INT_OPAQUE_BLACK,     0.f);
}

// ── updateLightSSBO ───────────────────────────────────────────────────────────

uint32_t Renderer::updateLightSSBO(World& world) {
    auto* gpuLights = static_cast<GpuLight*>(frames_.lightPtr(frameIdx));
    uint32_t count  = 0;
    shadowCasterSlot_ = -1;                     // 이 프레임의 섀도 캐스터를 재선정
    for (auto* lc : world.getLights()) {
        if (count >= kMaxLights) break;
        const uint32_t slot = count;
        GpuLight& gl = gpuLights[count++];
        glm::vec3 col = lc->effectiveColor() * lc->intensity;   // 색온도 틴트 포함
        // 섀도 캐스터 = 첫 Directional 이면서 castShadows 가 켜진 라이트. 선택과 SSBO 채우기가
        // 같은 루프 안이라 슬롯 번호가 어긋날 수 없습니다(DEF-26). 캐스터가 없으면 -1 로 남습니다.
        if (lc->lightType == LightType::Directional && lc->castShadows && shadowCasterSlot_ < 0) {
            shadowCasterSlot_ = (int)slot;
            shadowCasterDir_  = lc->worldDirection();
        }
        if (lc->lightType == LightType::Directional) {
            glm::vec3 dir = lc->worldDirection();   // 액터 회전 반영
            gl.posOrDir  = glm::vec4(-dir, 0.f);   // w=0 → directional
            gl.colorRange= glm::vec4(col, 0.f);
            gl.spotDir   = glm::vec4(dir, 0.f);
            gl.spotParams= glm::vec4(0.f);
        } else if (lc->lightType == LightType::Point) {
            glm::vec3 pos = lc->owner ? glm::vec3(lc->owner->worldMatrix()[3]) : glm::vec3(0.f);
            gl.posOrDir  = glm::vec4(pos, 1.f);    // w=1 → point
            gl.colorRange= glm::vec4(col, std::max(lc->attenuationRadius, LightComponent::kMinAttenuationRadius));
            gl.spotDir   = glm::vec4(0.f);
            gl.spotParams= glm::vec4(0.f);
        } else { // Spot
            glm::vec3 pos = lc->owner ? glm::vec3(lc->owner->worldMatrix()[3]) : glm::vec3(0.f);
            glm::vec3 dir = lc->worldDirection();   // 액터 회전 반영
            gl.posOrDir  = glm::vec4(pos, 2.f);    // w=2 → spot
            gl.colorRange= glm::vec4(col, std::max(lc->attenuationRadius, LightComponent::kMinAttenuationRadius));
            gl.spotDir   = glm::vec4(dir, 0.f);
            gl.spotParams= glm::vec4(
                std::cos(glm::radians(lc->innerConeAngle)),
                std::cos(glm::radians(lc->outerConeAngle)),
                0.f, 0.f);
        }
    }
    return count;   // 실제로 업로드한 수. updateUBO가 이 값을 그대로 쓴다.
}

// ── updateUBO ─────────────────────────────────────────────────────────────────

void Renderer::updateUBO(World& /*world*/, uint32_t lightCount) {
    FrameUBO ubo{};

    // 눈별 행렬과 위치(DEF-09). 단안이면 [1] 에 [0] 을 그대로 둡니다. 카메라가 없으면 views_ 가
    // 단위 행렬 · 원점이라 예전과 같습니다.
    for (uint32_t i = 0; i < kMaxViews; ++i) {
        const EyeView& v = views_[std::min(i, viewCount_ - 1)];
        ubo.viewProj[i] = hasCamera_ ? v.proj * v.view : glm::mat4(1.f);
        ubo.eyePos[i]   = glm::vec4(v.pos, v.zNear);
    }
    ubo.screenParams = glm::vec4((float)rc_.renderExtent.width, (float)rc_.renderExtent.height,
                                 views_[0].zFar, 0.8f);

    // Directional light shadow camera (fixed 40m×40m orthographic).
    // 캐스터 선택은 updateLightSSBO 가 이미 끝냈습니다(shadowCasterSlot_/Dir_). 여기서 다시
    // 고르지 않으므로 선택 기준이 갈릴 여지가 없습니다(DEF-26). 캐스터가 없으면 이전 VP 를 유지.
    if (shadowCasterSlot_ >= 0) {
        glm::vec3 dir = shadowCasterDir_;             // 액터 회전 반영 → Sun 을 돌리면 그림자도 회전
        glm::vec3 up  = (std::abs(dir.y) < 0.99f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
        glm::mat4 lv  = glm::lookAt(dir * 25.f, glm::vec3(0.f), up);
        glm::mat4 lp  = glm::ortho(-20.f, 20.f, -20.f, 20.f, 0.1f, 50.f);
        lp[1][1]     *= -1.f; // Vulkan Y flip
        sCurLightViewProj = lp * lv;
    }
    ubo.lightViewProj = sCurLightViewProj;

    ubo.timeParams  = glm::vec4(time_, (float)frameNum_, 1.0f, 1.0f);
    // lights 개수는 updateLightSSBO가 실제로 채운 수를 쓴다(범위 밖 읽기 방지).
    // miscParams.w 는 이제 **섀도 캐스터의 슬롯 번호**다(<0 이면 섀도 없음). 셰이더가 슬롯 0 을
    // 가정하지 않고, 그 라이트의 기여에만 그림자를 곱하도록 바뀌었다(DEF-26).
    const int shadowSlot = (shadowCasterSlot_ >= 0 && shadow_.valid()) ? shadowCasterSlot_ : -1;
    ubo.miscParams  = glm::ivec4((int)lightCount, (int)frameNum_, 1, shadowSlot);

    memcpy(frames_.uboPtr(frameIdx), &ubo, sizeof(FrameUBO));
}

// ── buildDrawCommands ─────────────────────────────────────────────────────────

void Renderer::buildDrawCommands(World& world,
                                  std::vector<GpuDrawCmd>& opaqueOut,
                                  std::vector<GpuDrawCmd>& transpOut,
                                  std::vector<const MaterialHandle*>& materialRowsOut,
                                  std::vector<GpuDrawCmd>& shadowOut) {
    // 눈마다 절두체를 뽑아, 하나라도 통과하면 그립니다(DEF-31). 예전에는 단안 절두체 하나로
    // 걸러 스테레오에서는 한쪽 눈에만 보이는 물체가 사라질 수 있었습니다. 같은 목록을 두 눈이
    // 함께 그리고, 각 눈의 절두체 밖 부분은 GPU 가 잘라 냅니다.
    // 컬링과 렌더와 제출이 이 프레임의 같은 views_ 를 쓰므로 재투영 여유분(DEF-32)은 필요 없습니다.
    // 늦은 자세 갱신(late latching)이나 과주사(overscan)를 넣으면 그때 평면을 바깥으로 돌려야 합니다.
    std::array<std::array<FrustumPlane, 6>, kMaxViews> planes{};
    const bool hasCam = hasCamera_;
    if (hasCam)
        for (uint32_t i = 0; i < viewCount_; ++i)
            planes[i] = extractFrustumPlanes(views_[i].proj * views_[i].view);

    // 섀도(라이트) 프러스텀 — 캐스터가 있을 때만. sCurLightViewProj 는 updateUBO 가 이미
    // 채웠다(호출 순서: updateLightSSBO→updateUBO→buildDrawCommands). 이 ortho 박스로 캐스터를
    // 걸러, 카메라 밖이지만 라이트 안에 있는 오브젝트도 그림자를 던지게 한다(DEF-27).
    const bool hasShadow = (shadowCasterSlot_ >= 0) && shadow_.valid();
    std::array<FrustumPlane, 6> lightPlanes{};
    if (hasShadow)
        lightPlanes = extractFrustumPlanes(sCurLightViewProj);

    auto* tfData = static_cast<GpuTransform*>(frames_.transformPtr(frameIdx));

    const auto& allMeshes   = world.getVisibleMeshes();
    uint32_t totalCnt = (uint32_t)allMeshes.size();

    // LOD 와 투명 정렬의 기준점은 두 눈의 가운데입니다. 눈마다 다르게 고르면 두 눈이 다른 LOD 나
    // 다른 그리기 순서를 보게 됩니다. 단안이면 카메라 위치 그대로입니다.
    glm::vec3 camPos(0.f);
    if (hasCam) {
        for (uint32_t i = 0; i < viewCount_; ++i) camPos += views_[i].pos;
        camPos /= (float)viewCount_;
    }

    opaqueOut.clear();
    transpOut.clear();
    materialRowsOut.clear();
    shadowOut.clear();

    // 1) 컬링과 LOD 선택: 카메라나 라이트에 보이는 메시만 후보로 모읍니다. 행은 여기서 배정하지 않습니다.
    // 투명 정렬 키(거리 제곱)도 여기서 한 번만 계산합니다. 예전에는 정렬 비교마다 매핑된 transform
    // SSBO(비디오 메모리)를 다시 읽었습니다(DEF-30).
    std::vector<hbatch::Candidate>     cands;
    std::vector<glm::mat4>             models;      // cands 와 같은 순서
    std::vector<const MaterialHandle*> materials;   // cands 와 같은 순서
    cands.reserve(allMeshes.size());
    models.reserve(allMeshes.size());
    materials.reserve(allMeshes.size());

    for (auto* mc : allMeshes) {
        if (!mc->visible || !mc->mesh) continue;
        // 머티리얼이 없는 메시는 오류 머티리얼(자홍색)로 그리고 액터마다 한 번 경고합니다(DEF-28).
        // 건너뛰지 않는 이유: 빠진 메시가 화면에서 보여야 알아챌 수 있습니다. 또 materialRowsOut
        // 에 nullptr 이 들어가지 않는다는 불변식을 이 대체가 지킵니다.
        const MaterialHandle* material = mc->material;
        if (!material) {
            material = errorMaterial_;
            const uint64_t actorId = mc->owner ? mc->owner->id : 0;
            if (noMaterialWarned_.insert(actorId).second)
                HLOG(LogRenderer, Warning) << "Mesh on actor '"
                    << (mc->owner ? mc->owner->name : std::string("?"))
                    << "' has no material; drawing it with the magenta error material";
            if (!material) continue;   // 오류 머티리얼을 만들기 전(초기화 중)에만
        }

        glm::mat4 model = mc->owner ? mc->owner->worldMatrix() : glm::mat4(1.f);

        // LOD selection
        float distSq = glm::length2(camPos - glm::vec3(model[3]));
        MeshHandle* mesh = mc->getMeshForDistanceSq(distSq);
        if (!mesh) continue;

        // 월드 공간 바운딩 구를 여기서 한 번만 만들어 카메라·라이트 판정에 함께 쓴다.
        const float sx = glm::length(glm::vec3(model[0]));
        const float sy = glm::length(glm::vec3(model[1]));
        const float sz = glm::length(glm::vec3(model[2]));
        const float maxScale = std::max({sx, sy, sz});
        const glm::vec3 wCenter = glm::vec3(model * glm::vec4(mesh->boundCenter, 1.f));
        const float     wRadius = mesh->boundRadius * maxScale;

        // 카메라 가시성과 라이트(섀도) 가시성을 따로 판정한다.
        const bool inCam   = !hasCam   || sphereInAnyView(planes.data(), viewCount_, wCenter, wRadius);
        const bool inLight = hasShadow && sphereInFrustum(lightPlanes, wCenter, wRadius);
        if (!inCam && !inLight) continue;

        hbatch::Candidate c;
        c.material     = material;
        c.indexCount   = mesh->indexCount;
        c.firstIndex   = mesh->firstIndex;
        c.vertexOffset = (int32_t)mesh->firstVertex;
        c.distSq       = distSq;
        c.inCam        = inCam;
        c.inLight      = inLight;
        c.transparent  = mc->transparent;
        cands.push_back(c);
        models.push_back(model);
        materials.push_back(material);
    }

    // 2) 묶기와 행 배정(DEF-16). transform SSBO 행은 hbatch::build 한 곳에서만 정해집니다(DEF-01 원칙).
    // 같은 지오메트리 · 머티리얼의 불투명 메시는 연속된 행을 받아 드로우 하나(instanceCount = N)가
    // 됩니다. 카메라 쪽이 예산을 먼저 쓰고, 라이트에만 보이는 캐스터는 남은 행만 씁니다(불변식: 라이트
    // 전용 행은 섀도맵에만 영향). 넘친 메시는 Dropped 로 셉니다(DEF-10).
    const hbatch::Result batch = hbatch::build(cands, kMaxDraws, hasCam);

    // 3) 행마다 트랜스폼과 머티리얼을 기록합니다. 행 번호와 push 순서가 1:1이므로 materialRowsOut[row]
    // 가 곧 그 행의 머티리얼입니다. 섀도 전용 행에도 기록해 materialForRow 의 범위 계약을 지킵니다.
    materialRowsOut.reserve(batch.rowSource.size());
    for (uint32_t row = 0; row < (uint32_t)batch.rowSource.size(); ++row) {
        const uint32_t src = batch.rowSource[row];
        tfData[row].model = models[src];
        materialRowsOut.push_back(materials[src]);
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
        HLOG(LogRenderer, Warning) << "Draw row budget kMaxDraws=" << kMaxDraws << " exceeded: "
            << batch.droppedCam << " visible mesh(es) and " << batch.droppedShadow
            << " shadow-only caster(s) were not drawn (warned once; see the Dropped stat)";
    }
}

// ── gatherViews ────────────────────────────────────────────────────────────────
// 이번 프레임에 그릴 눈을 정합니다. 단안은 활성 카메라 하나, 스테레오 흉내 모드는 카메라 좌우의
// 두 눈입니다(DEF-09). 눈 위치는 뷰 행렬의 역에서 읽습니다(예전 cameraPos 와 같은 식).
void Renderer::gatherViews(World& world, const XrFrameTarget* xr) {
    CameraComponent* cam = world.getActiveCamera();

    // XR: 런타임이 준 두 눈 자세(원점 공간)를 활성 카메라의 XR 원점(위치 · 요)에 올립니다. 시야는
    // 런타임의 네 방향 각도로 눈마다 따로 만듭니다(DEF-21). 카메라가 없으면 월드 원점에 둡니다.
    if (xr && xrActive_) {
        const glm::mat4 origin = cam ? cam->xrOrigin() : glm::mat4(1.f);
        const float     zNear  = cam ? cam->zNear : 0.1f;
        const float     zFar   = cam ? cam->zFar  : 100.f;
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
            v.proj  = CameraComponent::projFromFov(p.angleLeft, p.angleRight, p.angleUp, p.angleDown,
                                                   zNear, zFar);
        }
        return;
    }

    hasCamera_ = (cam != nullptr);
    if (!cam) {
        viewCount_ = 1;
        views_[0]  = EyeView{};   // 단위 행렬 · 원점 · near 0.1 · far 100 (예전 기본값)
        return;
    }
    const glm::mat4 camView = cam->viewMatrix();

    // 단안. 투영 비율은 합성할 뷰포트의 비율입니다. 평소에는 씬 타깃과 같은 크기이고, XR 이 붙어 있는데
    // 이번 프레임 헤드셋에 그리지 않을 때(헤드셋을 벗음 등)는 씬 타깃이 눈 크기라 비율이 다릅니다.
    if (!emulateStereo_ || xrActive_) {
        const float aspect = (float)rc_.viewportExtent.width / (float)rc_.viewportExtent.height;
        viewCount_ = 1;
        EyeView& v = views_[0];
        v.view  = camView;
        v.proj  = cam->projMatrix(aspect);
        v.pos   = glm::vec3(glm::inverse(camView)[3]);
        v.zNear = cam->zNear;
        v.zFar  = cam->zFar;
        return;
    }

    // 스테레오 흉내: 카메라 좌우로 눈 사이 거리(64mm)의 절반씩 옮긴 평행한 두 눈입니다(Quest 처럼
    // 기울어지지 않음). 세로 시야는 카메라 FovY 를 그대로 쓰고, 가로는 눈 하나의 화면 비율로 정한 뒤
    // 코 쪽 45% · 바깥쪽 55% 로 나눠 실제 헤드셋처럼 비대칭 투영을 씁니다.
    const float     aspect  = (float)rc_.renderExtent.width / (float)rc_.renderExtent.height;
    constexpr float kIpd    = 0.064f;
    const float tanUp       = std::tan(glm::radians(cam->fovY) * 0.5f);
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
        v.zNear = cam->zNear;
        v.zFar  = cam->zFar;
        const float tanLeft  = (e == 0) ? -tanOuter : -tanInner;
        const float tanRight = (e == 0) ?  tanInner :  tanOuter;
        v.proj = CameraComponent::projFromFov(std::atan(tanLeft), std::atan(tanRight),
                                              std::atan(tanUp), -std::atan(tanUp),
                                              cam->zNear, cam->zFar);
    }
}

// ── extractRenderView ──────────────────────────────────────────────────────────
// World를 읽는 유일한 지점 (docs/RendererRefactor.md §4). 이후 패스 기록에서는
// World/Actor/Component 참조 금지 — 시뮬레이션/렌더 스레드 분리의 경계가 된다.
// v1: 프레임별 mapped 버퍼(UBO/라이트/트랜스폼) 업로드를 추출 단계에서 수행.

Renderer::RenderView Renderer::extractRenderView(World& world, const XrFrameTarget* xr) {
    RenderView v;

    // 이번 프레임의 눈을 먼저 정합니다. 아래 UBO · 컬링 · AO 가 모두 이 한 벌을 씁니다.
    gatherViews(world, xr);

    // 순서 주의: 라이트 SSBO를 먼저 채워 실제 개수를 확정한 뒤 UBO에 싣는다.
    const uint32_t lightCount = updateLightSSBO(world);
    updateUBO(world, lightCount);
    buildDrawCommands(world, v.opaqueDraws, v.transpDraws, v.materialRows, v.shadowDraws);
    frameStats_.drawCalls = (uint32_t)v.opaqueDraws.size();

    if (hasCamera_) {
        for (uint32_t i = 0; i < viewCount_; ++i) {
            v.aoCam[i].zNear   = views_[i].zNear;
            v.aoCam[i].zFar    = views_[i].zFar;
            v.aoCam[i].invProj = glm::inverse(views_[i].proj);
            v.aoCam[i].valid   = true;
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
        assert(row < v.materialRows.size() && "firstInstance가 materialRows 범위를 벗어났습니다");
        return row < v.materialRows.size() ? v.materialRows[row] : nullptr;
    };
    auto toItems = [&](const std::vector<GpuDrawCmd>& draws,
                       std::vector<GeometryPass::DrawItem>& itemsOut) {
        itemsOut.reserve(draws.size());
        for (const auto& dc : draws) {
            itemsOut.push_back({ materialForRow(dc.firstInstance), dc.indexCount,
                                 dc.firstIndex, dc.vertexOffset, dc.firstInstance,
                                 dc.instanceCount });
        }
    };
    toItems(v.opaqueDraws, v.opaqueItems);
    toItems(v.transpDraws, v.transparentItems);   // 정렬 완료 순서를 그대로 따른다

    return v;
}

// ── drawFrame ─────────────────────────────────────────────────────────────────

bool Renderer::drawFrame(World& world, const XrFrameTarget* xr) {
    const bool xrFrame = (xr != nullptr) && xrActive_;
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
    if (swapchain_->needsRecreation()) { recreateSwapChain(); return false; }

    uint32_t imgIdx;
    VkResult res;
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
    vkResetFences(device, 1, &frameFence);

    // ── Extract: World를 읽는 유일한 구간 — 이후 기록은 view만 소비 ──────────
    RenderView view;
    {
        ScopedCpuTimer t(ft.extract);
        view      = extractRenderView(world, xrFrame ? xr : nullptr);
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
    {
        VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        mb.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        mb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        mb.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        mb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        di.memoryBarrierCount = 1; di.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cmd, &di);
    }

    // ── Shadow pass ───────────────────────────────────────────────────────────
    // view.shadowDraws 는 buildDrawCommands 가 **라이트 프러스텀**으로 걸러 만든 캐스터
    // 목록이다(DEF-27). 불투명·투명을 모두 담고(투명은 알파 무시·실루엣 근사), firstInstance
    // 는 메인 패스와 공유하는 transform SSBO 의 유효한 행이라 그대로 그려진다. 카메라 밖·
    // 라이트 안의 캐스터도 여기 포함되므로 카메라를 돌려도 그림자가 사라지지 않는다.
    shadow_.record(cmd,
        { frames_.frameSet(fi), frames_.sceneSet(fi), frames_.transformSet(fi),
          scene_.vertexBuffer(), scene_.indexBuffer() },
        view.shadowDraws);

    // Timestamp 1: after shadow
    if (timestampPool_)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi*TS+1);

    // A-4: GPU 컬링 dispatch 제거(순수 오버헤드였음). 깊이 프리패스·컬러 모두 CPU opaqueItems 를
    // 그립니다. GPU 구동 경로가 필요해지면 스테레오·인스턴스 기준으로 새로 설계합니다.

    // ── 눈마다: 메인(3 서브패스) → AO → Bloom → 톤매핑 ─────────────────────────
    // 첫 단계는 눈마다 두 번 그립니다(DEF-09). 위험을 나누려는 선택이고, 셰이더와 UBO 는 처음부터
    // 두 눈 기준이라 멀티뷰로 옮길 때 뷰 번호만 gl_ViewIndex 로 바꾸면 됩니다. 섀도맵은 위에서 한 번
    // 그린 것을 두 눈이 함께 씁니다. 중간 타깃(깊이 · HDR · AO · Bloom)은 두 눈이 번갈아 씁니다.
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

    // XR 은 오른쪽 눈을 먼저 그립니다. 중간 타깃에 왼쪽 눈이 남아 있어야 아래에서 그것을 에디터
    // 뷰포트에 비출 수 있습니다.
    for (uint32_t k = 0; k < viewCount_; ++k) {
        const uint32_t v      = xrFrame ? viewCount_ - 1 - k : k;
        const uint32_t tsBase = fi * TS + 2 + kTsPerView * k;
        // 앞 눈이 쓰던 중간 타깃을 다음 눈이 다시 씁니다. 앞 눈의 읽기(AO 의 깊이, 톤매핑의 HDR ·
        // AO · Bloom)가 끝나고 쓰기가 보이도록 그래픽 단계 전체를 한 번 묶습니다. 눈이 둘뿐이라
        // 비용은 무시할 수준입니다.
        if (k > 0) {
            VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
            mb.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
            mb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            mb.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
            mb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            di.memoryBarrierCount = 1; di.pMemoryBarriers = &mb;
            vkCmdPipelineBarrier2(cmd, &di);
        }

        // ── Main render pass (3 subpasses) ────────────────────────────────────
        geometry_.record(cmd, fi, v, gin, timestampPool_, tsBase + 0);

        // ── AO (linDepth + SSAO, half-res) ───────────────────────────────────
        ao_.record(cmd, fi, v, geometry_.depthView(), clampSampler_, view.aoCam[v]);
        if (timestampPool_)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, tsBase + 1);

        // ── Bloom passes (threshold → down ×4 → up ×4) ───────────────────────
        bloom_.record(cmd, geometry_.hdrView(fi), clampSampler_);
        if (timestampPool_)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, tsBase + 2);

        // ── Post-process pass (ACES + bloom composite → swapchain) ───────────
        // T-c: 가짜 TAA 비활성. 지터·모션벡터·이웃클램프 없는 mix(hdr,history,0.1)는 AA 효과가
        // 0이면서 이동 시 고스팅과 노출 미적용(D-5) 밝기 오류만 냈습니다. AA 는 4x MSAA 가
        // 맡습니다. 진짜 TAA 는 OpenXR 이후 별도 트랙(Transparency_Temporal_Design.md 4절).
        post_.recordTonemap(cmd, outputFor(v), geometry_.hdrView(fi), ao_.ssaoView(),
                            bloom_.resultView(), clampSampler_);
        if (timestampPool_)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, tsBase + 3);
    }
    // XR: 왼쪽 눈(중간 타깃에 남아 있음)을 에디터 뷰포트에 한 번 더 톤매핑해 비춥니다. 뷰포트 비율에
    // 맞춰 가운데를 잘라 읽습니다(UE 의 기본 관전 화면과 같은 방식). 이 시간은 UI 칸에 들어갑니다.
    if (xrFrame) {
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
        VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        mb.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        mb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        mb.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        mb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        di.memoryBarrierCount = 1; di.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cmd, &di);
        post_.recordTonemap(cmd, mirror, geometry_.hdrView(fi), ao_.ssaoView(),
                            bloom_.resultView(), clampSampler_);
    }

    // 그리지 않은 눈의 칸도 찍어 둡니다. 비워 두면 결과 읽기가 통째로 VK_NOT_READY 가 됩니다.
    if (timestampPool_)
        for (uint32_t q = 2 + kTsPerView * viewCount_; q < TS - 1; ++q)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi * TS + q);

    // ── UI overlay ────────────────────────────────────────────────────────────
    // UI 정점 버퍼는 fi 슬롯별입니다. 슬롯 수가 어긋나면 두 프레임이 한 버퍼를 같이 씁니다(DEF-93).
    static_assert(HSlateRenderer::kFramesInFlight == MAX_FRAMES,
                  "HSlateRenderer vertex buffers must match MAX_FRAMES");
    if (uiRenderer_) {
        ScopedCpuTimer t(ft.uiRecord);
        uiRenderer_->record(cmd, imgIdx, swapchain_->extent(), fi);
    }

    // 마지막 칸: after UI
    if (timestampPool_)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi*TS + TS - 1);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer!");
    ft.record.add(std::chrono::duration<float, std::milli>(
                      std::chrono::steady_clock::now() - recordT0).count());

    // ── Submit ────────────────────────────────────────────────────────────────
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore waitSem   = frames_.imageAvailSem(fi);
    VkSemaphore signalSem = frames_.renderDoneSem(imgIdx);
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1; si.pWaitSemaphores   = &waitSem;
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1; si.pCommandBuffers   = &cmd;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &signalSem;
    {
        ScopedCpuTimer t(ft.submit);
        if (vkQueueSubmit(graphicsQueue, 1, &si, frameFence) != VK_SUCCESS)
            throw std::runtime_error("Failed to submit draw command!");
    }

    // ── Present ───────────────────────────────────────────────────────────────
    {
        ScopedCpuTimer t(ft.present);
        res = swapchain_->present(presentQueue, imgIdx, signalSem);
        if (swapchain_->needsRecreation()) recreateSwapChain();
        else if (res != VK_SUCCESS) throw std::runtime_error("Failed to present image!");
    }

    frameIdx  = (frameIdx + 1) % MAX_FRAMES;
    frameNum_++;
    time_     = (float)frameNum_ / 60.f;
    return xrFrame;
}
