#include "GpuScene.hpp"
#include "RendererLog.hpp"
#include "../assets/GltfLoader.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

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
    if (megaVertBuf_) { vkDestroyBuffer(rc_->device, megaVertBuf_, nullptr); rc_->allocator->freeB(megaVertAlloc_); megaVertBuf_ = VK_NULL_HANDLE; }
    if (megaIdxBuf_)  { vkDestroyBuffer(rc_->device, megaIdxBuf_,  nullptr); rc_->allocator->freeB(megaIdxAlloc_);  megaIdxBuf_  = VK_NULL_HANDLE; }
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
    if ((megaVertCount_ + vCount) * sizeof(Vertex)   > kMegaVertBytes ||
        (megaIdxCount_  + iCount) * sizeof(uint32_t) > kMegaIdxBytes)
        throw std::runtime_error("Mega buffer overflow! Too many vertices/indices.");

    auto* h = new MeshHandle();
    h->firstVertex = megaVertCount_;
    h->firstIndex  = megaIdxCount_;
    h->indexCount  = iCount;
    h->vertexCount = vCount;

    // Copy into mega buffers (Zone B mapped via auxMem for images — buffers in heap)
    void* vertDst = static_cast<uint8_t*>(megaVertAlloc_.mappedPtr) + megaVertCount_ * sizeof(Vertex);
    memcpy(vertDst, verts.data(), vCount * sizeof(Vertex));
    void* idxDst  = static_cast<uint8_t*>(megaIdxAlloc_.mappedPtr) + megaIdxCount_ * sizeof(uint32_t);
    memcpy(idxDst, indices.data(), iCount * sizeof(uint32_t));

    megaVertCount_ += vCount;
    megaIdxCount_  += iCount;

    // ── 바운드 ────────────────────────────────────────────────────────────────
    // 메가버퍼는 해제 없는 범프 할당자입니다. 여기서 소모한 공간은 영영
    // 돌아오지 않으므로, 넘치기 전에 미리 알려 주는 것이 사후 throw 보다 낫습니다.
    const auto bud = budget();
    if (bud.vertPct >= 80.f || bud.idxPct >= 80.f)
        HLOG(LogRenderer, Warning) << "메가버퍼 사용률 경고: 정점 " << bud.vertPct
                                   << " %, 인덱스 " << bud.idxPct
                                   << " %. 이 버퍼는 회수되지 않습니다.";

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
            HLOG(LogRenderer, Warning) << "해석 바운드가 실제 정점을 담지 못합니다. "
                                          "컬링 경계에서 도형이 깜빡입니다.";
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

GpuScene::MeshBudget GpuScene::budget() const {
    MeshBudget b{};
    b.verts   = megaVertCount_;
    b.indices = megaIdxCount_;
    b.vertPct = 100.f * (float)((double)megaVertCount_ * sizeof(Vertex)   / (double)kMegaVertBytes);
    b.idxPct  = 100.f * (float)((double)megaIdxCount_  * sizeof(uint32_t) / (double)kMegaIdxBytes);
    return b;
}

void GpuScene::freeMesh(MeshHandle* h) {
    delete h;  // Mega buffer data is never individually freed
}

// ── Texture slot upload ───────────────────────────────────────────────────────

TexSlot GpuScene::uploadTexture(const uint8_t* pixels, int w, int h, VkFormat fmt) {
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

    slot.image = rc_->createImage(w, h, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        fmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        0, slot.alloc);

    auto cmd = rc_->beginOneTimeCmd();
    RenderContext::transitionImage(cmd, slot.image,
        VK_IMAGE_LAYOUT_UNDEFINED,                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE,                 VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COPY_BIT,             VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuf, slot.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    RenderContext::transitionImage(cmd, slot.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT,               VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    rc_->endOneTimeCmd(cmd);

    vkDestroyBuffer(rc_->device, stagingBuf, nullptr);
    rc_->allocator->freeB(stagingAlloc);

    slot.imageView = rc_->createImageView(slot.image, fmt, VK_IMAGE_ASPECT_COLOR_BIT);
    return slot;
}

void GpuScene::freeTexSlot(TexSlot slot) {
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

    auto upload = [&](const TexSourceData& tex, const uint8_t* fallback,
                      VkFormat fmt) -> TexSlot {
        if (tex.hasData)
            return uploadTexture(tex.data.data(), tex.w, tex.h, fmt);
        return uploadTexture(fallback, 1, 1, fmt);
    };

    mat->albedo    = upload(albedo,    kWhite,    VK_FORMAT_R8G8B8A8_SRGB);
    mat->normalMap = upload(normalMap, kFlatNorm, VK_FORMAT_R8G8B8A8_UNORM);
    mat->metalRough= upload(metalRough,kMR,       VK_FORMAT_R8G8B8A8_UNORM);
    mat->ao        = upload(ao,        kWhite,    VK_FORMAT_R8G8B8A8_UNORM);
    mat->emissive  = upload(emissive,  kBlack,    VK_FORMAT_R8G8B8A8_SRGB);

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
