// 반사 캡처(Reflection Capture, 2026-11-17). ReflectionCaptureComponent.hpp 주석 참고.
//
// 흐름: drawFrame 이 슬롯 펜스를 기다린 뒤 updateReflectionCapture 를 부릅니다. 켜진 첫 캡처 컴포넌트의 (액터, 위치, 반경, 버전)이
// 지난 캡처와 다르면 장치를 멈추고 6면을 차례로 그립니다. 면마다 그 방향을 보는 정사각 시야(90°)의 눈 하나로 extract →
// 섀도 · 메인 패스만 기록하고(톤매핑 전 HDR, 에디터 선 · 기즈모 제외), HDR 결과의 가운데 정사각형을 버퍼로 읽어 kCaptureRes² 로
// 줄입니다. 그다음 하늘 IBL 과 같은 CPU GGX 프리필터(hibl::prefilterGGX)로 밉 사슬을 굽고 scene set binding 5 에 올립니다.
// 캡처 중에는 이전 캡처를 반사에 쓰지 않습니다(되먹임 없음). 캡처는 드물게(Play 시작 · 값 변경) 일어나 장치를 멈춰도 됩니다.

#include "Renderer.hpp"
#include "SkyIbl.hpp"
#include "RendererLog.hpp"
#include "../math/Projection.hpp"
#include "../components/ReflectionCaptureComponent.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace {
// half → float(비정규 · 무한대 포함). glm::unpackHalf1x16 을 성분마다 부르는 것보다 훨씬 빠릅니다.
inline float halfToFloat(uint16_t h) {
    const uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t       exp  = (h >> 10) & 0x1Fu;
    uint32_t       man  = h & 0x3FFu;
    uint32_t       bits;
    if (exp == 0) {
        if (man == 0) bits = sign;
        else {                                   // 비정규: 정규화
            exp = 127 - 15 + 1;
            while (!(man & 0x400u)) { man <<= 1; --exp; }
            man &= 0x3FFu;
            bits = sign | (exp << 23) | (man << 13);
        }
    } else if (exp == 31) bits = sign | 0x7F800000u | (man << 13);
    else                  bits = sign | ((exp + 127 - 15) << 23) | (man << 13);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
} // namespace

void Renderer::updateReflectionCapture(World& world, const RenderSceneInput& scene) {
    const ReflectionCaptureComponent* rc = nullptr;
    if (quality_.reflectionCapture)   // 품질 단계에서 끄면 캡처 없음(하늘 IBL 만, 2026-11-18)
    for (const ReflectionCaptureComponent* c : world.componentsOf<ReflectionCaptureComponent>()) {
        if (!c->owner->activeInHierarchy()) continue;
        if (c->isEnabled()) { rc = c; break; }
    }
    if (!rc || !rc->owner) {
        capturePos_ = glm::vec4(0.f);   // 반경 0 = 캡처 없음(셰이더가 하늘만)
        captureKey_ = {};
        return;
    }
    CaptureKey key{ rc->owner->id, rc->owner->worldPosition(), rc->radius, rc->version(), &world, true };
    captureIntensity_ = rc->intensity;
    if (key == captureKey_) {
        capturePos_ = glm::vec4(key.pos, rc->radius);
        return;
    }
    performReflectionCapture(world, scene, key.pos);
    captureKey_ = key;
    capturePos_ = glm::vec4(key.pos, rc->radius);
}

void Renderer::performReflectionCapture(World& world, const RenderSceneInput& scene, const glm::vec3& pos) {
    const auto t0 = std::chrono::steady_clock::now();
    vkDeviceWaitIdle(device);
    const uint32_t fi   = frameIdx;
    const uint32_t W    = rc_.renderExtent.width, H = rc_.renderExtent.height;
    if (std::min(W, H) < kCaptureRes) return;
    // 면마다 왼쪽 위 side² 영역에만 그립니다(90° 정사각 시야).
    const uint32_t side  = std::min(W, H) >= kCaptureDrawRes ? kCaptureDrawRes : kCaptureRes;
    const uint32_t x0 = 0, y0 = 0;
    const uint32_t block = side / kCaptureRes;   // 면 텍셀 하나에 모을 화소 변

    // 읽기 버퍼(HDR RGBA16F)
    VkBuffer         readBuf = VK_NULL_HANDLE;
    HarukaAllocation readAlloc;
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = (VkDeviceSize)side * side * 8;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bi, nullptr, &readBuf) != VK_SUCCESS) return;
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(device, readBuf, &reqs);
        readAlloc = allocator.allocB(reqs);
        if (!readAlloc.isValid() || !readAlloc.mappedPtr || allocator.bind(readBuf, readAlloc) != VK_SUCCESS) {
            vkDestroyBuffer(device, readBuf, nullptr);
            if (readAlloc.isValid()) allocator.freeB(readAlloc);
            HLOG(LogRenderer, Warning) << "Reflection capture skipped: no host-visible read buffer";
            return;
        }
    }

    const glm::vec4 savedCap = capturePos_;
    capturePos_      = glm::vec4(0.f);   // 이전 캡처를 반사에 쓰지 않음
    captureOverride_ = true;
    const float tanX = 1.f, tanY = 1.f;

    std::vector<glm::vec3> faces[6];
    std::vector<uint16_t>  hostPx;
    for (int f = 0; f < 6; ++f) {
        const glm::vec3 fwd   = glm::normalize(hibl::cubeDir(f, 0.f, 0.f));
        const glm::vec3 right = glm::normalize(hibl::cubeDir(f, 1.f, 0.f) - hibl::cubeDir(f, 0.f, 0.f));
        const glm::vec3 down  = glm::normalize(hibl::cubeDir(f, 0.f, 1.f) - hibl::cubeDir(f, 0.f, 0.f));
        const glm::vec3 up    = -down;
        const bool flipX = glm::dot(glm::cross(fwd, up), right) < 0.f;   // 큐브 텍셀의 오른쪽이 카메라 왼쪽이면 좌우를 뒤집음
        captureEye_.view  = glm::lookAt(pos, pos + fwd, up);
        captureEye_.proj  = hmath::perspectiveFromFov(-std::atan(tanX), std::atan(tanX), std::atan(tanY), -std::atan(tanY),
                                                      kCaptureNear);
        captureEye_.pos   = pos;
        captureEye_.zNear = kCaptureNear;
        captureEye_.zFar  = kCaptureFar;

        RenderView view = extractRenderView(world, scene, nullptr);
        VkCommandBuffer cmd = beginOneTimeCmd();
        rc_.memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                               VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        shadow_.record(cmd,
            { frames_.frameSet(fi), frames_.sceneSet(fi), frames_.transformSet(fi), scene_.vertexBuffer(), scene_.indexBuffer() },
            view.shadowDraws, view.rowFlags, shadowCasterSlot_ >= 0);
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
        gin.overlays         = false;
        gin.area             = { side, side };
        geometry_.record(cmd, fi, 0, gin, VK_NULL_HANDLE, 0);

        VkImage hdr = geometry_.hdrImage(fi);
        transitionImage(cmd, hdr, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageOffset      = { (int32_t)x0, (int32_t)y0, 0 };
        region.imageExtent      = { side, side, 1 };
        vkCmdCopyImageToBuffer(cmd, hdr, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readBuf, 1, &region);
        transitionImage(cmd, hdr, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        endOneTimeCmd(cmd);   // 제출하고 끝날 때까지 기다림

        // 읽은 정사각형 → kCaptureRes² (블록 평균). 매핑 메모리는 캐시되지 않아 화소마다 읽으면 느리므로 한 번에 복사해 옵니다
        // (예전에는 면마다 약 15 ms 가 여기서 들었습니다).
        hostPx.resize((size_t)side * side * 4);
        std::memcpy(hostPx.data(), readAlloc.mappedPtr, hostPx.size() * sizeof(uint16_t));
        const uint16_t* px = hostPx.data();
        faces[f].assign((size_t)kCaptureRes * kCaptureRes, glm::vec3(0.f));
        const float inv = 1.f / (float)(block * block);
        for (uint32_t iy = 0; iy < kCaptureRes; ++iy)
            for (uint32_t ix = 0; ix < kCaptureRes; ++ix) {
                glm::vec3 sum(0.f);
                for (uint32_t by = 0; by < block; ++by)
                    for (uint32_t bx = 0; bx < block; ++bx) {
                        const size_t o = ((size_t)(iy * block + by) * side + (ix * block + bx)) * 4;
                        sum += glm::vec3(halfToFloat(px[o]), halfToFloat(px[o + 1]), halfToFloat(px[o + 2]));
                    }
                const uint32_t dx = flipX ? kCaptureRes - 1 - ix : ix;
                faces[f][(size_t)iy * kCaptureRes + dx] = sum * inv;
            }
    }
    captureOverride_ = false;
    capturePos_      = savedCap;
    vkDestroyBuffer(device, readBuf, nullptr);
    allocator.freeB(readAlloc);
    const auto t1 = std::chrono::steady_clock::now();

    // GGX 프리필터 밉 사슬(하늘과 같은 크기 · 단계 · 표본 수, Renderer::generateIBLCubemaps)
    const hibl::CubeSampler cs{ faces, (int)kCaptureRes };
    VkDeviceSize bytes = 0;
    for (uint32_t m = 0; m < kCapturePrefMips; ++m) { const uint32_t s = kCapturePrefSize >> m; bytes += (VkDeviceSize)s * s * 6 * 8; }
    std::vector<uint8_t> data((size_t)bytes);
    // 면 하나를 스레드 하나가 맡습니다(6개). 면마다 쓰는 자리가 겹치지 않고 계산이 결정적이라 결과는 한 스레드와 같습니다.
    // 데이터 배치는 밉 우선 · 면 다음(아래 올리기 영역과 같은 순서)입니다.
    auto prefilterFace = [&](int face) {
        VkDeviceSize mipBase = 0;
        for (uint32_t mip = 0; mip < kCapturePrefMips; ++mip) {
            const float    rough = (float)mip / (float)(kCapturePrefMips - 1);
            const uint32_t sz    = kCapturePrefSize >> mip;
            VkDeviceSize   off   = mipBase + (VkDeviceSize)face * sz * sz * 8;
            for (uint32_t iy = 0; iy < sz; ++iy)
                for (uint32_t ix = 0; ix < sz; ++ix) {
                    const float u = (ix + 0.5f) / sz * 2.f - 1.f, v = (iy + 0.5f) / sz * 2.f - 1.f;
                    const glm::vec3 Rd  = glm::normalize(hibl::cubeDir(face, u, v));
                    const glm::vec3 col = mip == 0 ? cs(Rd) : hibl::prefilterGGX(cs, Rd, rough, 64);
                    uint16_t* o = reinterpret_cast<uint16_t*>(data.data() + off);
                    auto h = [](float x) { return x > 0.f ? glm::packHalf1x16(std::min(x, 65504.f)) : (uint16_t)0; };
                    o[0] = h(col.r); o[1] = h(col.g); o[2] = h(col.b); o[3] = h(1.f);
                    off += 8;
                }
            mipBase += (VkDeviceSize)sz * sz * 6 * 8;
        }
    };
    {
        std::vector<std::thread> workers;
        workers.reserve(5);
        for (int face = 1; face < 6; ++face) workers.emplace_back(prefilterFace, face);
        prefilterFace(0);
        for (auto& t : workers) t.join();
    }

    // 큐브 이미지(처음 한 번 만듦) · 올리기
    const VkFormat fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
    if (!captureImage_) {
        captureImage_ = createImage(kCapturePrefSize, kCapturePrefSize, kCapturePrefMips, 6, VK_SAMPLE_COUNT_1_BIT, fmt,
                                    VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                    VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, captureAlloc_);
        captureView_  = createImageView(captureImage_, fmt, VK_IMAGE_ASPECT_COLOR_BIT, kCapturePrefMips, 6, VK_IMAGE_VIEW_TYPE_CUBE);
    }
    VkBuffer         stage = VK_NULL_HANDLE;
    HarukaAllocation stageAlloc;
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = bytes; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device, &bi, nullptr, &stage);
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(device, stage, &reqs);
        stageAlloc = allocator.allocB(reqs);
        allocator.bind(stage, stageAlloc);
        std::memcpy(stageAlloc.mappedPtr, data.data(), (size_t)bytes);
    }
    VkCommandBuffer cmd = beginOneTimeCmd();
    transitionImage(cmd, captureImage_, captureEverUploaded_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, kCapturePrefMips, 6);
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize o2 = 0;
    for (uint32_t mip = 0; mip < kCapturePrefMips; ++mip) {
        const uint32_t sz = kCapturePrefSize >> mip;
        for (int face = 0; face < 6; ++face) {
            VkBufferImageCopy r{};
            r.bufferOffset     = o2;
            r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, (uint32_t)face, 1 };
            r.imageExtent      = { sz, sz, 1 };
            regions.push_back(r);
            o2 += (VkDeviceSize)sz * sz * 8;
        }
    }
    vkCmdCopyBufferToImage(cmd, stage, captureImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)regions.size(), regions.data());
    transitionImage(cmd, captureImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                    kCapturePrefMips, 6);
    endOneTimeCmd(cmd);
    vkDestroyBuffer(device, stage, nullptr);
    allocator.freeB(stageAlloc);
    captureEverUploaded_ = true;
    frames_.writeCaptureView(captureView_);

    using ms = std::chrono::duration<double, std::milli>;
    HLOG(LogRenderer, Log) << "Reflection capture at (" << pos.x << ", " << pos.y << ", " << pos.z << "): 6 faces "
                           << kCaptureRes << "^2 in " << ms(t1 - t0).count() << " ms, GGX prefilter "
                           << ms(std::chrono::steady_clock::now() - t1).count() << " ms";
}
