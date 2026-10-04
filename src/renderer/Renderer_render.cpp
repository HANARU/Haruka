#include "Renderer.hpp"
#include "../ui/slate/HSlateRenderer.hpp"
#include "../core/World.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/LightComponent.hpp"
#include "../core/FrameTimings.hpp"
#include "RendererLog.hpp"
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

// ── Frustum culling helpers ───────────────────────────────────────────────────

// Gribb-Hartmann plane extraction (Vulkan NDC z=[0,1])
static std::array<FrustumPlane, 6> extractFrustumPlanes(const glm::mat4& vp) {
    auto row = [&](int r) {
        return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
    };
    auto makePlane = [](glm::vec4 v) -> FrustumPlane {
        float len = glm::length(glm::vec3(v));
        return { glm::vec3(v) / len, v.w / len };
    };
    auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    return {
        makePlane(r3 + r0),   // Left
        makePlane(r3 - r0),   // Right
        makePlane(r3 + r1),   // Bottom
        makePlane(r3 - r1),   // Top
        makePlane(r2),         // Near (Vulkan z=0)
        makePlane(r3 - r2),   // Far  (Vulkan z=1)
    };
}

// 월드 공간 바운딩 구 대 절두체 판정. 깊이 프리패스와 컬러 패스가 이 한 판정으로 만든 같은
// 목록을 그립니다(GPU 컬링은 A-4 에서 없앴고 2026-10-04 에 코드도 지웠습니다).
//
//   예전에는 CPU가 AABB를, GPU가 구를 써서 두 판정이 갈렸다. 회전한 메시에서 여덟 꼭짓점을
//   변환해 다시 축 정렬 상자로 감싸면 그 상자가 구보다 커질 수 있기 때문이다. 구는 회전에
//   불변이므로 이 흔들림이 없다. GPU 컬링을 되살린다면 GPU 쪽도 같은 구 판정을 써야 한다.
//
// ★ VR 확장 지점: 스테레오에서는 두 눈의 절두체를 각각 판정해 하나라도 통과하면 그린다.
//   Quest 2·Pro·3 의 두 눈 시야는 평행하고 비대칭이다(기울어진 canted 화면이 아님).
//   컬링·렌더·제출이 한 프레임의 같은 자세를 쓰는 동안에는 재투영 여유분이 필요 없다(DEF-32).
static bool sphereInFrustum(const std::array<FrustumPlane, 6>& planes,
                            const glm::vec3& center, float radius) {
    for (const auto& p : planes)
        if (glm::dot(p.normal, center) + p.d < -radius) return false;
    return true;
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

void Renderer::updateUBO(World& world, uint32_t lightCount) {
    CameraComponent* cam = world.getActiveCamera();
    FrameUBO ubo{};

    glm::mat4 curVP{1.f};
    if (cam) {
        float aspect = (float)rc_.renderExtent.width / (float)rc_.renderExtent.height;
        curVP = cam->projMatrix(aspect) * cam->viewMatrix();
        glm::vec3 camPos = glm::vec3(glm::inverse(cam->viewMatrix())[3]);
        ubo.cameraPos   = glm::vec4(camPos, cam->zNear);
        ubo.screenParams= glm::vec4((float)rc_.renderExtent.width, (float)rc_.renderExtent.height,
                                     cam->zFar, 0.8f);
    } else {
        ubo.cameraPos   = glm::vec4(0.f, 0.f, 0.f, 0.1f);
        ubo.screenParams= glm::vec4((float)rc_.renderExtent.width, (float)rc_.renderExtent.height, 100.f, 0.8f);
    }
    ubo.viewProj[0] = curVP;
    ubo.viewProj[1] = curVP;

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
    CameraComponent* cam = world.getActiveCamera();
    std::array<FrustumPlane, 6> planes{};
    bool hasCam = (cam != nullptr);
    if (hasCam) {
        float aspect = (float)rc_.renderExtent.width / (float)rc_.renderExtent.height;
        planes = extractFrustumPlanes(cam->projMatrix(aspect) * cam->viewMatrix());
    }

    // 섀도(라이트) 프러스텀 — 캐스터가 있을 때만. sCurLightViewProj 는 updateUBO 가 이미
    // 채웠다(호출 순서: updateLightSSBO→updateUBO→buildDrawCommands). 이 ortho 박스로 캐스터를
    // 걸러, 카메라 밖이지만 라이트 안에 있는 오브젝트도 그림자를 던지게 한다(DEF-27).
    const bool hasShadow = (shadowCasterSlot_ >= 0) && shadow_.valid();
    std::array<FrustumPlane, 6> lightPlanes{};
    if (hasShadow)
        lightPlanes = extractFrustumPlanes(sCurLightViewProj);

    auto* tfData = static_cast<GpuTransform*>(frames_.transformPtr(frameIdx));

    uint32_t ssboIdx  = 0;
    const auto& allMeshes   = world.getVisibleMeshes();
    uint32_t totalCnt = (uint32_t)allMeshes.size();

    glm::vec3 camPos = hasCam ? glm::vec3(glm::inverse(cam->viewMatrix())[3]) : glm::vec3(0.f);

    opaqueOut.clear();
    transpOut.clear();
    materialRowsOut.clear();
    shadowOut.clear();
    materialRowsOut.reserve(allMeshes.size());

    // 투명 정렬 키. 거리 제곱을 행을 배정할 때 한 번만 계산해 둡니다. 예전에는 정렬 비교마다
    // 매핑된 transform SSBO(비디오 메모리)를 다시 읽었습니다(DEF-30).
    struct TranspKeyed { float distSq; GpuDrawCmd cmd; };
    std::vector<TranspKeyed> transpKeyed;
    uint32_t droppedCam = 0;   // 카메라에 보이지만 행 예산이 차서 못 그린 메시(DEF-10)

    // 라이트 전용 캐스터(inLight && !inCam)는 카메라 드로우가 행 예산을 먼저 가져간 뒤에
    // 붙입니다. 그래야 카메라∪라이트 합집합이 kMaxDraws 를 넘겨도, 화면 밖 캐스터의 행이
    // 컬러 패스의 카메라 드로우를 밀어내지 못합니다(불변식: 라이트 전용 행은 섀도맵에만 영향).
    // 합집합이 예산 안이면 순서는 무의미하고, 넘칠 때 밀려나는 것은 안 보이는 캐스터의 그림자뿐.
    struct PendingCaster {
        glm::mat4             model;
        const MaterialHandle* material;
        uint32_t              indexCount, firstIndex;
        int32_t               vertexOffset;
    };
    std::vector<PendingCaster> lightOnly;

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
        const bool inCam   = !hasCam   || sphereInFrustum(planes,      wCenter, wRadius);
        const bool inLight = hasShadow && sphereInFrustum(lightPlanes, wCenter, wRadius);

        // 카메라에 안 보이면 여기서 행을 배정하지 않는다. 라이트 안이면 뒤(2단계)로 미뤄,
        // 카메라 드로우가 예산을 먼저 확보하게 한다. 둘 다 아니면 그냥 버린다.
        if (!inCam) {
            if (inLight)
                lightOnly.push_back({ model, material,
                    mesh->indexCount, mesh->firstIndex, (int32_t)mesh->firstVertex });
            continue;
        }

        // 여기부터 inCam. transform SSBO 행은 이 한 곳(과 2단계)에서만 배정한다(DEF-01 원칙).
        // 카메라 행이 예산을 채우면 그 뒤의 메시는 세기만 하고 그리지 않는다. 예전에는 조용히
        // 잘라 culled 에 섞였다(DEF-10).
        if (ssboIdx >= kMaxDraws) { ++droppedCam; continue; }
        uint32_t idx = ssboIdx++;
        tfData[idx].model = model;
        // 행 번호와 push 순서가 1:1이므로 materialRowsOut[idx]가 곧 이 행의 머티리얼이다.
        materialRowsOut.push_back(material);

        GpuDrawCmd cmd{};
        cmd.indexCount    = mesh->indexCount;
        cmd.instanceCount = 1;
        cmd.firstIndex    = mesh->firstIndex;
        cmd.vertexOffset  = (int32_t)mesh->firstVertex;
        cmd.firstInstance = idx;  // → gl_InstanceIndex → transform SSBO row

        if (mc->transparent)
            transpKeyed.push_back({ distSq, cmd });
        else
            opaqueOut.push_back(cmd);
        // 카메라에도 보이면서 라이트 안이면 그림자도 던진다(같은 행 재사용).
        if (inLight)
            shadowOut.push_back(cmd);
    }

    // 2단계: 카메라 예산을 채운 뒤 남은 행에만 라이트 전용 캐스터를 붙인다. 행-머티리얼
    // 1:1 불변식을 위해 여기서도 materialRow 를 함께 push 한다(섀도는 머티리얼을 안 쓰지만
    // materialForRow 의 범위 계약을 지키기 위함). opaque 목록은 건드리지 않는다.
    uint32_t droppedShadow = 0;
    for (const auto& lo : lightOnly) {
        if (ssboIdx >= kMaxDraws) { ++droppedShadow; continue; }
        uint32_t idx = ssboIdx++;
        tfData[idx].model = lo.model;
        materialRowsOut.push_back(lo.material);

        GpuDrawCmd cmd{};
        cmd.indexCount    = lo.indexCount;
        cmd.instanceCount = 1;
        cmd.firstIndex    = lo.firstIndex;
        cmd.vertexOffset  = lo.vertexOffset;
        cmd.firstInstance = idx;
        shadowOut.push_back(cmd);
    }

    // Sort transparent back-to-front for correct blending. 키는 위에서 LOD 선택에 쓴 것과 같은
    // 피벗 거리 제곱이라 순서는 예전과 같고, 같은 거리면 행 번호 순으로 정해 결과가 흔들리지 않는다.
    if (hasCam) {
        std::sort(transpKeyed.begin(), transpKeyed.end(),
            [](const TranspKeyed& a, const TranspKeyed& b) {
                if (a.distSq != b.distSq) return a.distSq > b.distSq;
                return a.cmd.firstInstance < b.cmd.firstInstance;
            });
    }
    transpOut.reserve(transpKeyed.size());
    for (const auto& t : transpKeyed) transpOut.push_back(t.cmd);

    frameStats_.dropped = droppedCam;
    frameStats_.culled  = totalCnt - (uint32_t)(opaqueOut.size() + transpOut.size()) - droppedCam;
    frameStats_.transparentDraws = (uint32_t)transpOut.size();
    if ((droppedCam || droppedShadow) && !dropWarned_) {
        dropWarned_ = true;
        HLOG(LogRenderer, Warning) << "Draw row budget kMaxDraws=" << kMaxDraws << " exceeded: "
            << droppedCam << " visible mesh(es) and " << droppedShadow
            << " shadow-only caster(s) were not drawn (warned once; see the Dropped stat)";
    }
}

// ── extractRenderView ──────────────────────────────────────────────────────────
// World를 읽는 유일한 지점 (docs/RendererRefactor.md §4). 이후 패스 기록에서는
// World/Actor/Component 참조 금지 — 시뮬레이션/렌더 스레드 분리의 경계가 된다.
// v1: 프레임별 mapped 버퍼(UBO/라이트/트랜스폼) 업로드를 추출 단계에서 수행.

Renderer::RenderView Renderer::extractRenderView(World& world) {
    RenderView v;

    // 순서 주의: 라이트 SSBO를 먼저 채워 실제 개수를 확정한 뒤 UBO에 싣는다.
    const uint32_t lightCount = updateLightSSBO(world);
    updateUBO(world, lightCount);
    buildDrawCommands(world, v.opaqueDraws, v.transpDraws, v.materialRows, v.shadowDraws);
    frameStats_.drawCalls = (uint32_t)v.opaqueDraws.size();

    if (auto* cam = world.getActiveCamera()) {
        v.aoCam.zNear = cam->zNear;
        v.aoCam.zFar  = cam->zFar;
        float aspect = (float)rc_.renderExtent.width / (float)rc_.renderExtent.height;
        v.aoCam.invProj = glm::inverse(cam->projMatrix(aspect));
        v.aoCam.valid   = true;
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
                                 dc.firstIndex, dc.vertexOffset, dc.firstInstance });
        }
    };
    toItems(v.opaqueDraws, v.opaqueItems);
    toItems(v.transpDraws, v.transparentItems);   // 정렬 완료 순서를 그대로 따른다

    return v;
}

// ── drawFrame ─────────────────────────────────────────────────────────────────

void Renderer::drawFrame(World& world) {
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
            // 슬롯 배치: 0 시작 / 1 섀도 / 2 메인(3서브패스+resolve) / 3 AO /
            //           4 Bloom / 5 톤매핑 / 6 UI
            // 예전에는 6 개를 기록하고 5 개만 읽어 UI 패스가 총합에서 빠져 있었고,
            // ts2 가 불투명 서브패스 직후에 찍혀 있어 라벨이 전부 어긋나 있었습니다.
            // 가짜 TAA 블릿 슬롯(빈 구간이라 늘 0ms)은 2026-10-04 에 지웠습니다(DEF-15).
            timings_.shadowMs  = float(ts[1] - ts[0]) * ns2ms;
            timings_.mainMs    = float(ts[2] - ts[1]) * ns2ms;   // 3 서브패스 + resolve
            timings_.aoMs      = float(ts[3] - ts[2]) * ns2ms;
            timings_.bloomMs   = float(ts[4] - ts[3]) * ns2ms;
            timings_.tonemapMs = float(ts[5] - ts[4]) * ns2ms;
            timings_.uiMs      = float(ts[6] - ts[5]) * ns2ms;
            timings_.totalMs   = float(ts[6] - ts[0]) * ns2ms;
        }
    }

    // ── Acquire swapchain image ───────────────────────────────────────────────
    // Pre-check: avoid acquiring into a stale swapchain, which would leave
    // semImageAvail_ signaled with no corresponding submit to consume it.
    if (swapchain_->needsRecreation()) { recreateSwapChain(); return; }

    uint32_t imgIdx;
    VkResult res;
    {
        ScopedCpuTimer t(ft.acquire);
        res = swapchain_->acquireNextImage(frames_.imageAvailSem(fi), imgIdx);
    }
    // VK_ERROR_OUT_OF_DATE_KHR: acquire failed, semaphore NOT signaled — safe to recreate now.
    // VK_SUBOPTIMAL_KHR: image acquired, semaphore IS signaled — finish the frame, recreate after present.
    if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapChain(); return; }
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
        view      = extractRenderView(world);
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

    // ── Main render pass (3 subpasses) ────────────────────────────────────────
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
    geometry_.record(cmd, fi, gin, timestampPool_, fi*TS+2);

    // ── AO (linDepth + SSAO, half-res) ───────────────────────────────────────
    ao_.record(cmd, fi, geometry_.depthView(), clampSampler_, view.aoCam);

    // Timestamp 3: after AO
    if (timestampPool_)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi*TS+3);

    // ── Bloom passes (threshold → down ×4 → up ×4) ───────────────────────────
    bloom_.record(cmd, geometry_.hdrView(fi), clampSampler_);

    // Timestamp 4: after Bloom
    if (timestampPool_)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi*TS+4);

    // ── Post-process pass (ACES + bloom composite → swapchain) ───────────────
    // T-c: 가짜 TAA 비활성. 지터·모션벡터·이웃클램프 없는 mix(hdr,history,0.1)는 AA 효과가
    // 0이면서 이동 시 고스팅과 노출 미적용(D-5) 밝기 오류만 냈습니다. AA 는 4x MSAA 가
    // 맡습니다. 진짜 TAA 는 OpenXR 이후 별도 트랙(Transparency_Temporal_Design.md 4절).
    post_.recordTonemap(cmd, imgIdx, geometry_.hdrView(fi), ao_.ssaoView(),
                        bloom_.resultView(), clampSampler_);

    // Timestamp 5: after tonemap
    if (timestampPool_)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi*TS+5);

    // ── UI overlay ────────────────────────────────────────────────────────────
    // UI 정점 버퍼는 fi 슬롯별입니다. 슬롯 수가 어긋나면 두 프레임이 한 버퍼를 같이 씁니다(DEF-93).
    static_assert(HSlateRenderer::kFramesInFlight == MAX_FRAMES,
                  "HSlateRenderer vertex buffers must match MAX_FRAMES");
    if (uiRenderer_) {
        ScopedCpuTimer t(ft.uiRecord);
        uiRenderer_->record(cmd, imgIdx, swapchain_->extent(), fi);
    }

    // Timestamp 6: after UI
    if (timestampPool_)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, fi*TS+6);

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
}
