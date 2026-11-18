#include "GpuScene.hpp"
#include "MipChain.hpp"
#include "RendererLog.hpp"
#include "RenderTypes.hpp"
#include "../geometry/MeshSource.hpp"
#include "../geometry/MeshOptimize.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>

// CPU 스키닝(hskin::skinVertices)은 MeshSourceVertex 로 쓰고 렌더러는 같은 바이트를 Vertex 로 읽습니다(2026-11-07).
static_assert(sizeof(MeshSourceVertex) == sizeof(Vertex), "MeshSourceVertex and Vertex must share a layout");
static_assert(offsetof(MeshSourceVertex, pos)     == offsetof(Vertex, pos),     "layout");
static_assert(offsetof(MeshSourceVertex, normal)  == offsetof(Vertex, normal),  "layout");
static_assert(offsetof(MeshSourceVertex, uv)      == offsetof(Vertex, uv),      "layout");
static_assert(offsetof(MeshSourceVertex, tangent) == offsetof(Vertex, tangent), "layout");

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

void GpuScene::create(RenderContext& rc) {
    rc_ = &rc;

    auto mkBuf = [&](VkBuffer& buf, HarukaAllocation& alloc,
                     VkDeviceSize sz, VkBufferUsageFlags usage) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = sz; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(rc.device, &bi, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error("Failed to create mega buffer!");
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(rc.device, buf, &reqs);
        alloc = rc.allocator->allocB(reqs);
        if (!alloc.isValid()) throw std::runtime_error("Zone B: mega buffer alloc failed!");
        if (rc.allocator->bind(buf, alloc) != VK_SUCCESS)
            throw std::runtime_error("Failed to bind mega buffer!");
    };

    mkBuf(megaVertBuf_, megaVertAlloc_, kMegaVertBytes,
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    mkBuf(megaIdxBuf_,  megaIdxAlloc_,  kMegaIdxBytes,
          VK_BUFFER_USAGE_INDEX_BUFFER_BIT  | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void GpuScene::destroy() {
    if (!rc_) return;
    if (fallbackReady_) {
        for (TexSlot& t : fallback_) { t.shared = false; freeTexSlot(t); t = {}; }
        fallbackReady_ = false;
    }
    if (megaVertBuf_) { vkDestroyBuffer(rc_->device, megaVertBuf_, nullptr); rc_->allocator->freeB(megaVertAlloc_); megaVertBuf_ = VK_NULL_HANDLE; }
    if (megaIdxBuf_)  { vkDestroyBuffer(rc_->device, megaIdxBuf_,  nullptr); rc_->allocator->freeB(megaIdxAlloc_);  megaIdxBuf_  = VK_NULL_HANDLE; }
}

// ── 스킨 메시 정점 구간 (2026-11-07) ─────────────────────────────────────────

PackedVertex* GpuScene::skinArena(uint32_t frameSlot, uint32_t& firstVertex) {
    firstVertex = 0;
    if (frameSlot >= (uint32_t)MAX_FRAMES || skinArenaFailed_ || !megaVertAlloc_.mappedPtr) return nullptr;
    if (!skinArenaReserved_) {
        const uint64_t need = (uint64_t)kSkinArenaVerts * MAX_FRAMES;
        if ((megaVertCount_ + need) * sizeof(PackedVertex) > kMegaVertBytes) {
            skinArenaFailed_ = true;
            HLOG(LogRenderer, Warning) << "mega vertex buffer has no room for the skinned-mesh area ("
                                       << need << " vertices); skinned meshes are not drawn";
            return nullptr;
        }
        skinArenaFirst_    = megaVertCount_;
        megaVertCount_    += (uint32_t)need;
        skinArenaReserved_ = true;
        HLOG(LogRenderer, Log) << "Skinned-mesh area reserved: " << kSkinArenaVerts << " vertices x "
                               << MAX_FRAMES << " frame slot(s) at vertex " << skinArenaFirst_;
    }
    firstVertex = skinArenaFirst_ + frameSlot * kSkinArenaVerts;
    return reinterpret_cast<PackedVertex*>(static_cast<uint8_t*>(megaVertAlloc_.mappedPtr)) + firstVertex;
}

// ── Mesh upload (mega buffer) ─────────────────────────────────────────────────

MeshHandle* GpuScene::uploadMesh(const std::vector<Vertex>& verts,
                                 const std::vector<uint32_t>& indices,
                                 const MeshBounds* precomputed) {
    const uint32_t vCount = (uint32_t)verts.size();
    const uint32_t iCount = (uint32_t)indices.size();

    // DEF-45: 예전에는 여기서 먼저 new MeshHandle() 을 하고 그 다음에 용량을
    // 검사해 throw 했습니다. 즉 초과할 때마다 핸들이 하나씩 새어 나갔습니다.
    // 검사를 앞으로 옮기면 그 경로 자체가 없어집니다.
    // 16비트로 넣을 수 있는가: 인덱스는 메시 안 번호(정점 오프셋은 그릴 때 더함)이므로 정점 수만 보면 됩니다.
    const bool use16 = vCount <= 65536u;
    if ((megaVertCount_ + vCount) * sizeof(PackedVertex) > kMegaVertBytes ||
        (use16 ? (uint64_t)(megaIdx16Count_ + iCount) * sizeof(uint16_t) > kMegaIdxBytes - kIdx16Base
               : (uint64_t)(megaIdxCount_ + iCount) * sizeof(uint32_t) > kIdx16Base))
        throw std::runtime_error("Mega buffer overflow! Too many vertices/indices.");

    // 정점 캐시 순서(O3): 삼각형 순서만 바꿉니다(집합 · 감기 방향 그대로).
    std::vector<uint32_t> ordered(indices);
    hmesh::optimizeVertexCache(ordered, vCount);

    auto* h = new MeshHandle();
    h->firstVertex = megaVertCount_;
    h->firstIndex  = use16 ? megaIdx16Count_ : megaIdxCount_;
    h->indexCount  = iCount;
    h->vertexCount = vCount;
    h->index16     = use16;

    // Copy into mega buffers (Zone B mapped via auxMem for images — buffers in heap)
    // QTangent(C2): 올리면서 28 B 정점으로 줄입니다.
    auto* vertDst = reinterpret_cast<PackedVertex*>(static_cast<uint8_t*>(megaVertAlloc_.mappedPtr)) + megaVertCount_;
    hqtan::packRange(verts.data(), vertDst, vCount);
    uint8_t* idxBase = static_cast<uint8_t*>(megaIdxAlloc_.mappedPtr);
    if (use16) {
        uint16_t* dst = reinterpret_cast<uint16_t*>(idxBase + kIdx16Base) + megaIdx16Count_;
        for (uint32_t i = 0; i < iCount; ++i) dst[i] = (uint16_t)ordered[i];
        megaIdx16Count_ += iCount;
    } else {
        memcpy(idxBase + (size_t)megaIdxCount_ * sizeof(uint32_t), ordered.data(), iCount * sizeof(uint32_t));
        megaIdxCount_ += iCount;
    }

    megaVertCount_ += vCount;

    // ── 바운드 ────────────────────────────────────────────────────────────────
    // 메가버퍼는 해제 없는 범프 할당자입니다. 여기서 소모한 공간은 영영
    // 돌아오지 않으므로, 넘치기 전에 미리 알려 주는 것이 사후 throw 보다 낫습니다.
    const auto bud = budget();
    if (bud.vertPct >= 80.f || bud.idxPct >= 80.f)
        HLOG(LogRenderer, Warning) << "mega buffer usage high: vertices " << bud.vertPct
                                   << " %, indices " << bud.idxPct
                                   << " %. This space is never reclaimed";

    auto traverse = [&](glm::vec3& lo, glm::vec3& hi) {
        lo = glm::vec3( FLT_MAX);
        hi = glm::vec3(-FLT_MAX);
        for (const auto& v : verts) {
            const glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
    };

    if (precomputed) {
        h->aabbMin     = precomputed->aabbMin;
        h->aabbMax     = precomputed->aabbMax;
        h->boundCenter = precomputed->center;
        h->boundRadius = precomputed->radius;
#ifndef NDEBUG
        // 생성기의 축 규약 실수를 즉시 잡습니다. 곡면은 정점이 내접하므로
        // 순회 결과가 해석값보다 작은 것은 정상이고, 넘치는 것만 결함입니다.
        glm::vec3 lo, hi;
        traverse(lo, hi);
        const float eps = 1e-3f;
        if (glm::any(glm::lessThan(lo, h->aabbMin - eps)) ||
            glm::any(glm::greaterThan(hi, h->aabbMax + eps)))
            HLOG(LogRenderer, Warning) << "analytic bounds do not contain the actual vertices; "
                                          "the shape will flicker at culling boundaries";
#endif
    } else {
        glm::vec3 lo, hi;
        traverse(lo, hi);
        h->aabbMin     = lo;
        h->aabbMax     = hi;
        h->boundCenter = (lo + hi) * 0.5f;
        h->boundRadius = glm::length(hi - h->boundCenter);
    }

    HLOG(LogRenderer, Verbose) << "Mesh uploaded: " << vCount << " verts, "
                               << "firstVtx=" << h->firstVertex;
    return h;
}

MeshHandle* GpuScene::uploadLodIndices(const MeshHandle& base, const std::vector<uint32_t>& indices) {
    if (indices.empty() || base.vertexCount == 0) return nullptr;
    const uint32_t iCount = (uint32_t)indices.size();
    const bool     use16  = base.index16;
    if (use16 ? (uint64_t)(megaIdx16Count_ + iCount) * sizeof(uint16_t) > kMegaIdxBytes - kIdx16Base
              : (uint64_t)(megaIdxCount_ + iCount) * sizeof(uint32_t) > kIdx16Base)
        throw std::runtime_error("Mega buffer overflow! Too many LOD indices.");

    std::vector<uint32_t> ordered(indices);
    hmesh::optimizeVertexCache(ordered, base.vertexCount);

    auto* h = new MeshHandle();
    h->firstVertex = base.firstVertex;
    h->firstIndex  = use16 ? megaIdx16Count_ : megaIdxCount_;
    h->indexCount  = iCount;
    h->vertexCount = 0;   // 정점은 base 가 소유
    h->index16     = use16;
    h->aabbMin     = base.aabbMin;
    h->aabbMax     = base.aabbMax;
    h->boundCenter = base.boundCenter;
    h->boundRadius = base.boundRadius;

    uint8_t* idxBase = static_cast<uint8_t*>(megaIdxAlloc_.mappedPtr);
    if (use16) {
        uint16_t* dst = reinterpret_cast<uint16_t*>(idxBase + kIdx16Base) + megaIdx16Count_;
        for (uint32_t i = 0; i < iCount; ++i) dst[i] = (uint16_t)ordered[i];
        megaIdx16Count_ += iCount;
    } else {
        memcpy(idxBase + (size_t)megaIdxCount_ * sizeof(uint32_t), ordered.data(), iCount * sizeof(uint32_t));
        megaIdxCount_ += iCount;
    }
    return h;
}

GpuScene::MeshBudget GpuScene::budget() const {
    MeshBudget b{};
    b.verts   = megaVertCount_;
    b.indices = megaIdxCount_ + megaIdx16Count_;
    b.vertPct = 100.f * (float)((double)megaVertCount_ * sizeof(PackedVertex) / (double)kMegaVertBytes);
    // 두 구역 중 더 찬 쪽(어느 한쪽이 차면 그 형식의 메시를 더 올릴 수 없음)
    b.idxPct  = 100.f * (float)std::max((double)megaIdxCount_ * sizeof(uint32_t) / (double)kIdx16Base,
                                        (double)megaIdx16Count_ * sizeof(uint16_t) / (double)(kMegaIdxBytes - kIdx16Base));
    return b;
}

void GpuScene::freeMesh(MeshHandle* h) {
    delete h;  // Mega buffer data is never individually freed
}

// ── Texture slot upload ───────────────────────────────────────────────────────

TexSlot GpuScene::uploadTexture(const uint8_t* pixels, int w, int h, VkFormat fmt) {
    std::vector<Staging> staging;
    auto cmd = rc_->beginOneTimeCmd();
    TexSlot slot = recordTextureUpload(cmd, pixels, w, h, fmt, staging);
    rc_->endOneTimeCmd(cmd);
    freeStaging(staging);
    return slot;
}

void GpuScene::freeStaging(std::vector<Staging>& staging) {
    for (Staging& s : staging) {
        vkDestroyBuffer(rc_->device, s.buf, nullptr);
        rc_->allocator->freeB(s.alloc);
    }
    staging.clear();
}

TexSlot GpuScene::recordTextureUpload(VkCommandBuffer cmd, const uint8_t* pixels, int w, int h, VkFormat fmt,
                                      std::vector<Staging>& staging) {
    TexSlot slot;
    VkDeviceSize imageSize = (VkDeviceSize)w * h * 4;

    VkBufferCreateInfo sbi{};
    sbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbi.size  = imageSize; sbi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    sbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer stagingBuf;
    if (vkCreateBuffer(rc_->device, &sbi, nullptr, &stagingBuf) != VK_SUCCESS)
        throw std::runtime_error("uploadTexture: staging buffer failed!");
    VkMemoryRequirements sreqs;
    vkGetBufferMemoryRequirements(rc_->device, stagingBuf, &sreqs);
    auto stagingAlloc = rc_->allocator->allocB(sreqs);
    if (!stagingAlloc.isValid()) throw std::runtime_error("Zone B: staging alloc failed!");
    rc_->allocator->bind(stagingBuf, stagingAlloc);
    memcpy(stagingAlloc.mappedPtr, pixels, imageSize);
    staging.push_back({ stagingBuf, stagingAlloc });

    // 밉 사슬(DEF-165, MipChain.hpp). 0단계는 스테이징에서 복사하고, 나머지는 바로 위 단계에서 선형 블릿으로 줄입니다.
    const uint32_t mips = (w > 1 || h > 1) && canBlitMips(fmt) ? hmip::levelCount((uint32_t)w, (uint32_t)h) : 1u;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mips > 1) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    slot.image = rc_->createImage(w, h, mips, 1, VK_SAMPLE_COUNT_1_BIT,
        fmt, VK_IMAGE_TILING_OPTIMAL, usage, 0, slot.alloc);

    // 한 단계만 옮기는 배리어. transitionImage 는 0단계부터 세므로 단계를 고르려면 직접 만듭니다.
    auto levelBarrier = [&](VkCommandBuffer c, uint32_t level,
                            VkImageLayout oldL, VkImageLayout newL,
                            VkPipelineStageFlags2 srcS, VkAccessFlags2 srcA,
                            VkPipelineStageFlags2 dstS, VkAccessFlags2 dstA) {
        VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        b.srcStageMask        = srcS;  b.srcAccessMask = srcA;
        b.dstStageMask        = dstS;  b.dstAccessMask = dstA;
        b.oldLayout           = oldL;  b.newLayout     = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = slot.image;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1 };
        VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers    = &b;
        rc_->pipelineBarrier(c, di);
    };

    rc_->transitionImage(cmd, slot.image,
        VK_IMAGE_LAYOUT_UNDEFINED,                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE,                 VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, mips);
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuf, slot.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    for (uint32_t i = 1; i < mips; ++i) {
        // 위 단계(i-1): 방금 쓴 것(복사 또는 블릿)을 읽을 수 있게 TRANSFER_SRC 로.
        levelBarrier(cmd, i - 1,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
        blit.srcOffsets[1]  = { (int32_t)hmip::extent((uint32_t)w, i - 1), (int32_t)hmip::extent((uint32_t)h, i - 1), 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
        blit.dstOffsets[1]  = { (int32_t)hmip::extent((uint32_t)w, i), (int32_t)hmip::extent((uint32_t)h, i), 1 };
        vkCmdBlitImage(cmd, slot.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       slot.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        // 다 읽은 위 단계는 셰이더가 읽는 배치로.
        levelBarrier(cmd, i - 1,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    // 마지막 단계(밉이 없으면 0단계)는 쓰기만 했으므로 TRANSFER_DST 에서 바로 옮깁니다.
    levelBarrier(cmd, mips - 1,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    slot.imageView = rc_->createImageView(slot.image, fmt, VK_IMAGE_ASPECT_COLOR_BIT, mips, 1);
    return slot;
}

bool GpuScene::canBlitMips(VkFormat fmt) {
    for (const auto& s : blitSupport_)
        if (s.fmt == fmt) return s.ok;
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(rc_->physDev, fmt, &fp);
    const bool ok = hmip::canBlitMips(fp.optimalTilingFeatures);
    blitSupport_.push_back({ fmt, ok });
    // 형식마다 한 번 남깁니다. 지원하지 않으면 텍스처가 밉 없이 올라가 멀리서 반짝일 수 있습니다.
    if (ok) HLOG(LogRenderer, Log)     << "Texture mipmaps: GPU blit chain for format " << (int)fmt;
    else    HLOG(LogRenderer, Warning) << "Texture mipmaps: format " << (int)fmt
                                       << " cannot be blitted with linear filtering; uploading a single level";
    return ok;
}

void GpuScene::freeTexSlot(TexSlot slot) {
    if (slot.shared) return;   // 기본값 1x1(fallback_)은 destroy 가 지움
    if (slot.imageView) { vkDestroyImageView(rc_->device, slot.imageView, nullptr); slot.imageView = VK_NULL_HANDLE; }
    if (slot.image)     { vkDestroyImage(rc_->device, slot.image, nullptr);         slot.image     = VK_NULL_HANDLE; }
    if (slot.alloc.isValid()) { rc_->allocator->freeB(slot.alloc); slot.alloc = {}; }
}

// ── Material upload (5 PBR textures) ─────────────────────────────────────────

static const uint8_t kWhite[4]    = { 255, 255, 255, 255 };
static const uint8_t kFlatNorm[4] = { 128, 128, 255, 255 };  // tangent-space up
static const uint8_t kBlack[4]    = {   0,   0,   0, 255 };
static const uint8_t kMR[4]       = {   0, 128,   0, 255 };  // metallic=0, roughness=0.5

MaterialHandle* GpuScene::uploadMaterial(
    const TexSourceData& albedo, const TexSourceData& normalMap,
    const TexSourceData& metalRough, const TexSourceData& ao,
    const TexSourceData& emissive)
{
    auto* mat = new MaterialHandle();

    // 텍스처를 모두 한 명령 버퍼에 기록하고 한 번 제출합니다(2026-11-18, 성능 안정화 1-6). 기본값 1x1 은 처음 한 번만 만들고
    // 모든 머티리얼이 함께 씁니다(fallback_, 내용 · 형식은 예전 머티리얼마다 올리던 것과 같음).
    std::vector<Staging> staging;
    auto cmd = rc_->beginOneTimeCmd();
    if (!fallbackReady_) {
        fallback_[kFbAlbedo]     = recordTextureUpload(cmd, kWhite,    1, 1, VK_FORMAT_R8G8B8A8_SRGB,  staging);
        fallback_[kFbNormal]     = recordTextureUpload(cmd, kFlatNorm, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, staging);
        fallback_[kFbMetalRough] = recordTextureUpload(cmd, kMR,       1, 1, VK_FORMAT_R8G8B8A8_UNORM, staging);
        fallback_[kFbAo]         = recordTextureUpload(cmd, kWhite,    1, 1, VK_FORMAT_R8G8B8A8_UNORM, staging);
        fallback_[kFbEmissive]   = recordTextureUpload(cmd, kBlack,    1, 1, VK_FORMAT_R8G8B8A8_SRGB,  staging);
        for (TexSlot& t : fallback_) t.shared = true;
        fallbackReady_ = true;
    }
    auto upload = [&](const TexSourceData& tex, FallbackTex fb, VkFormat fmt) -> TexSlot {
        if (tex.hasData)
            return recordTextureUpload(cmd, tex.data.data(), tex.w, tex.h, fmt, staging);
        return fallback_[fb];
    };

    mat->albedo    = upload(albedo,     kFbAlbedo,     VK_FORMAT_R8G8B8A8_SRGB);
    mat->normalMap = upload(normalMap,  kFbNormal,     VK_FORMAT_R8G8B8A8_UNORM);
    mat->metalRough= upload(metalRough, kFbMetalRough, VK_FORMAT_R8G8B8A8_UNORM);
    mat->ao        = upload(ao,         kFbAo,         VK_FORMAT_R8G8B8A8_UNORM);
    mat->emissive  = upload(emissive,   kFbEmissive,   VK_FORMAT_R8G8B8A8_SRGB);
    rc_->endOneTimeCmd(cmd);
    freeStaging(staging);

    return mat;
}

void GpuScene::freeMaterial(MaterialHandle* h) {
    if (!h) return;
    vkDeviceWaitIdle(rc_->device);
    freeTexSlot(h->albedo);
    freeTexSlot(h->normalMap);
    freeTexSlot(h->metalRough);
    freeTexSlot(h->ao);
    freeTexSlot(h->emissive);
    delete h;
}
