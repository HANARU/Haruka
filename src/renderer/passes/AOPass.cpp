#include "AOPass.hpp"
#include <algorithm>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

// SSAO 커널 UBO — ssao.frag set 0 binding 2와 일치
struct SSAOData {
    glm::mat4 invProj;
    glm::vec4 samples[64];
    glm::vec2 noiseScale;
    float     radius;
    float     bias;
    float     power;
    float     zFar;       // ssao.frag 의 배경 조기 탈출용
    float     pad[2];
};

// 단일 컬러 어태치먼트 + shader-read 종료 렌더패스 (linDepth/SSAO 공통 형태)
static VkRenderPass makeColorPass(VkDevice device, VkFormat fmt) {
    VkAttachmentDescription att{};
    att.format = fmt; att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0; dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1; rpci.pAttachments = &att;
    rpci.subpassCount = 1; rpci.pSubpasses = &sp;
    rpci.dependencyCount = 1; rpci.pDependencies = &dep;
    VkRenderPass rp;
    if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
        throw std::runtime_error("Failed to create AO render pass!");
    return rp;
}

static VkFramebuffer makeFramebuffer(VkDevice device, VkRenderPass rp,
                                     VkImageView view, uint32_t w, uint32_t h) {
    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = rp; fci.attachmentCount = 1; fci.pAttachments = &view;
    fci.width = w; fci.height = h; fci.layers = 1;
    VkFramebuffer fb;
    if (vkCreateFramebuffer(device, &fci, nullptr, &fb) != VK_SUCCESS)
        throw std::runtime_error("Failed to create AO framebuffer!");
    return fb;
}

void AOPass::create(RenderContext& rc) {
    rc_ = &rc;

    linDepthPass_ = makeColorPass(rc.device, VK_FORMAT_R32_SFLOAT);
    ssaoPass_     = makeColorPass(rc.device, VK_FORMAT_R8_UNORM);

    // linDepth set layout: push descriptor, binding 0 = MSAA depth
    {
        VkDescriptorSetLayoutBinding b0{};
        b0.binding = 0; b0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b0.descriptorCount = 1; b0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dslci.bindingCount = 1; dslci.pBindings = &b0;
        if (vkCreateDescriptorSetLayout(rc.device, &dslci, nullptr, &linDepthSetLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create linDepth desc layout!");
    }

    // SSAO set layout: 0=linDepth, 1=noise, 2=kernel UBO
    {
        VkDescriptorSetLayoutBinding bindings[3] = {};
        bindings[0].binding = 0; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1; bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1; bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1; bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].binding = 2; bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1; bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dslci.bindingCount = 3; dslci.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(rc.device, &dslci, nullptr, &ssaoSetLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create SSAO desc layout!");
    }

    // Pipeline layouts (PSO와 분리 — 핫리로드 시 재생성 안 함)
    {
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; pcr.offset = 0; pcr.size = 16;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &linDepthSetLayout_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        if (vkCreatePipelineLayout(rc.device, &pli, nullptr, &linDepthPipeLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create linDepth pipeline layout!");
    }
    {
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &ssaoSetLayout_;
        if (vkCreatePipelineLayout(rc.device, &pli, nullptr, &ssaoPipeLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create SSAO pipeline layout!");
    }

    createDataBuffers();
    // 크기 의존 이미지는 acquireSizeDependent()에서 — Renderer가 Zone B 코얼레싱
    // 순서(영구 자원 → 스왑체인 크기 자원)에 맞춰 별도 호출한다.
    // PSO는 Renderer::createAllPipelines → rebuildPipelines() 경로에서 생성.
}

void AOPass::acquireSizeDependent() {
    uint32_t w = std::max(1u, rc_->renderExtent.width  / 2);
    uint32_t h = std::max(1u, rc_->renderExtent.height / 2);

    linDepthImage_ = rc_->createImage(w, h, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        0, linDepthAlloc_);
    linDepthView_     = rc_->createImageView(linDepthImage_, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
    linDepthFramebuf_ = makeFramebuffer(rc_->device, linDepthPass_, linDepthView_, w, h);

    ssaoImage_ = rc_->createImage(w, h, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        0, ssaoAlloc_);
    ssaoView_     = rc_->createImageView(ssaoImage_, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    ssaoFramebuf_ = makeFramebuffer(rc_->device, ssaoPass_, ssaoView_, w, h);
}

void AOPass::releaseSizeDependent() {
    if (!rc_) return;
    auto freeSlot = [&](VkImage& img, VkImageView& view, HarukaAllocation& alloc) {
        if (view) { vkDestroyImageView(rc_->device, view, nullptr); view = VK_NULL_HANDLE; }
        if (img)  { vkDestroyImage(rc_->device, img, nullptr);      img  = VK_NULL_HANDLE; }
        if (alloc.isValid()) { rc_->allocator->freeB(alloc); alloc = {}; }
    };
    if (ssaoFramebuf_)     { vkDestroyFramebuffer(rc_->device, ssaoFramebuf_, nullptr);     ssaoFramebuf_     = VK_NULL_HANDLE; }
    freeSlot(ssaoImage_, ssaoView_, ssaoAlloc_);
    if (linDepthFramebuf_) { vkDestroyFramebuffer(rc_->device, linDepthFramebuf_, nullptr); linDepthFramebuf_ = VK_NULL_HANDLE; }
    freeSlot(linDepthImage_, linDepthView_, linDepthAlloc_);
}

void AOPass::createDataBuffers() {
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::uniform_real_distribution<float> d01(0.f, 1.f);

    // 64 hemisphere samples (tangent space, z >= 0)
    SSAOData ssaoData{};
    for (int i = 0; i < 64; i++) {
        glm::vec3 s = glm::normalize(glm::vec3(dist(rng), dist(rng), d01(rng)));
        float scale = float(i) / 64.f;
        s *= glm::mix(0.1f, 1.0f, scale * scale);
        ssaoData.samples[i] = glm::vec4(s, 0.f);
    }
    ssaoData.radius = 0.5f;
    ssaoData.bias   = 0.025f;
    ssaoData.power  = 1.5f;

    // Per-frame UBOs (invProj and noiseScale will be updated in record)
    for (int i = 0; i < kFramesInFlight; i++) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = sizeof(SSAOData); bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(rc_->device, &bi, nullptr, &dataBufs_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create SSAO data buffer!");
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(rc_->device, dataBufs_[i], &reqs);
        dataAllocs_[i] = rc_->allocator->allocC(reqs.size, reqs.alignment);
        if (!dataAllocs_[i].isValid()) throw std::runtime_error("Zone C: SSAO data alloc failed!");
        if (rc_->allocator->bind(dataBufs_[i], dataAllocs_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to bind SSAO data buffer!");
        memcpy(dataAllocs_[i].mappedPtr, &ssaoData, sizeof(SSAOData));
    }

    // 4×4 R8G8_UNORM noise texture (rotation vectors in XY plane)
    constexpr int kN = 4;
    std::vector<uint8_t> noisePixels(kN * kN * 2);
    for (int i = 0; i < kN * kN; i++) {
        noisePixels[i * 2 + 0] = (uint8_t)((dist(rng) * 0.5f + 0.5f) * 255.f);
        noisePixels[i * 2 + 1] = (uint8_t)((dist(rng) * 0.5f + 0.5f) * 255.f);
    }

    VkFormat noiseFmt = VK_FORMAT_R8G8_UNORM;
    noiseTex_ = rc_->createImage(kN, kN, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        noiseFmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        0, noiseAlloc_);
    noiseView_ = rc_->createImageView(noiseTex_, noiseFmt, VK_IMAGE_ASPECT_COLOR_BIT);

    // 4×4 노이즈를 화면에 타일링(REPEAT)하고 보간 없이(NEAREST) 읽습니다. clampSampler 로
    // 읽으면 inUV*noiseScale 이 1을 넘는 거의 전 화면에서 가장자리 텍셀로 클램프되어 회전이
    // 상수가 되고 SSAO 가 구조적으로 밴딩됩니다(D-3). 그래서 전용 샘플러가 필요합니다.
    VkSamplerCreateInfo nsci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    nsci.magFilter = nsci.minFilter = VK_FILTER_NEAREST;
    nsci.addressModeU = nsci.addressModeV = nsci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    nsci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    vkCreateSampler(rc_->device, &nsci, nullptr, &noiseSampler_);

    VkDeviceSize noiseBytes = kN * kN * 2;
    VkBufferCreateInfo sbi{};
    sbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbi.size  = noiseBytes; sbi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    sbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer stagingBuf;
    vkCreateBuffer(rc_->device, &sbi, nullptr, &stagingBuf);
    VkMemoryRequirements sreqs;
    vkGetBufferMemoryRequirements(rc_->device, stagingBuf, &sreqs);
    auto stagingAlloc = rc_->allocator->allocB(sreqs);
    rc_->allocator->bind(stagingBuf, stagingAlloc);
    memcpy(stagingAlloc.mappedPtr, noisePixels.data(), noiseBytes);

    auto cmd = rc_->beginOneTimeCmd();
    RenderContext::transitionImage(cmd, noiseTex_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { kN, kN, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuf, noiseTex_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    RenderContext::transitionImage(cmd, noiseTex_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    rc_->endOneTimeCmd(cmd);

    vkDestroyBuffer(rc_->device, stagingBuf, nullptr);
    rc_->allocator->freeB(stagingAlloc);
}

void AOPass::createPipelines() {
    linDepthPipeline_ = rc_->createFullscreenPipeline(linDepthPass_, 0,
        "shaders/linDepth.frag.spv", linDepthPipeLayout_);
    ssaoPipeline_ = rc_->createFullscreenPipeline(ssaoPass_, 0,
        "shaders/ssao.frag.spv", ssaoPipeLayout_);
}

void AOPass::destroyPipelines() {
    if (!rc_) return;
    if (linDepthPipeline_) { vkDestroyPipeline(rc_->device, linDepthPipeline_, nullptr); linDepthPipeline_ = VK_NULL_HANDLE; }
    if (ssaoPipeline_)     { vkDestroyPipeline(rc_->device, ssaoPipeline_, nullptr);     ssaoPipeline_     = VK_NULL_HANDLE; }
}

void AOPass::rebuildPipelines() {
    // 새 파이프라인을 다 만든 뒤에만 옛 것을 지웁니다. 하나라도 실패하면 새로 만든 것만 지우고
    // 옛 것을 되돌려, 핫 리로드가 실패해도 이전 파이프라인으로 계속 그립니다.
    const VkPipeline oldLin = linDepthPipeline_, oldSsao = ssaoPipeline_;
    linDepthPipeline_ = ssaoPipeline_ = VK_NULL_HANDLE;
    try { createPipelines(); }
    catch (...) {
        destroyPipelines();
        linDepthPipeline_ = oldLin; ssaoPipeline_ = oldSsao;
        throw;
    }
    if (oldLin)  vkDestroyPipeline(rc_->device, oldLin,  nullptr);
    if (oldSsao) vkDestroyPipeline(rc_->device, oldSsao, nullptr);
}

void AOPass::destroy() {
    if (!rc_) return;
    releaseSizeDependent();
    destroyPipelines();

    auto freeSlot = [&](VkImage& img, VkImageView& view, HarukaAllocation& alloc) {
        if (view) { vkDestroyImageView(rc_->device, view, nullptr); view = VK_NULL_HANDLE; }
        if (img)  { vkDestroyImage(rc_->device, img, nullptr);      img  = VK_NULL_HANDLE; }
        if (alloc.isValid()) { rc_->allocator->freeB(alloc); alloc = {}; }
    };
    freeSlot(noiseTex_, noiseView_, noiseAlloc_);
    if (noiseSampler_) { vkDestroySampler(rc_->device, noiseSampler_, nullptr); noiseSampler_ = VK_NULL_HANDLE; }
    for (int i = 0; i < kFramesInFlight; i++) {
        if (dataBufs_[i]) { vkDestroyBuffer(rc_->device, dataBufs_[i], nullptr); dataBufs_[i] = VK_NULL_HANDLE; }
        if (dataAllocs_[i].isValid()) { rc_->allocator->freeB(dataAllocs_[i]); dataAllocs_[i] = {}; }
    }

    if (linDepthPipeLayout_) { vkDestroyPipelineLayout(rc_->device, linDepthPipeLayout_, nullptr); linDepthPipeLayout_ = VK_NULL_HANDLE; }
    if (linDepthSetLayout_)  { vkDestroyDescriptorSetLayout(rc_->device, linDepthSetLayout_, nullptr); linDepthSetLayout_ = VK_NULL_HANDLE; }
    if (linDepthPass_)       { vkDestroyRenderPass(rc_->device, linDepthPass_, nullptr); linDepthPass_ = VK_NULL_HANDLE; }
    if (ssaoPipeLayout_)     { vkDestroyPipelineLayout(rc_->device, ssaoPipeLayout_, nullptr); ssaoPipeLayout_ = VK_NULL_HANDLE; }
    if (ssaoSetLayout_)      { vkDestroyDescriptorSetLayout(rc_->device, ssaoSetLayout_, nullptr); ssaoSetLayout_ = VK_NULL_HANDLE; }
    if (ssaoPass_)           { vkDestroyRenderPass(rc_->device, ssaoPass_, nullptr); ssaoPass_ = VK_NULL_HANDLE; }
}

// ── record ─────────────────────────────────────────────────────────────────────
void AOPass::record(VkCommandBuffer cmd, uint32_t frameIdx,
                    VkImageView msaaDepthView, VkSampler clampSampler,
                    const CameraParams& cam) {
    uint32_t hw = std::max(1u, rc_->renderExtent.width  / 2);
    uint32_t hh = std::max(1u, rc_->renderExtent.height / 2);

    // ── linDepth: MSAA 깊이 → half-res 선형 깊이 ──────────────────────────────
    {
        VkRenderPassBeginInfo rpbi{};
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = linDepthPass_; rpbi.framebuffer = linDepthFramebuf_;
        rpbi.renderArea = {{0,0},{hw, hh}};
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport lvp{0,0,(float)hw,(float)hh,0.f,1.f}; VkRect2D lsc{{0,0},{hw,hh}};
        vkCmdSetViewport(cmd, 0, 1, &lvp); vkCmdSetScissor(cmd, 0, 1, &lsc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, linDepthPipeline_);

        VkDescriptorImageInfo depthI{ clampSampler, msaaDepthView,
                                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet depthW{};
        depthW.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        depthW.dstBinding = 0; depthW.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        depthW.descriptorCount = 1; depthW.pImageInfo = &depthI;
        vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            linDepthPipeLayout_, 0, 1, &depthW);

        struct LinPC { float zNear, zFar, w, h; };
        LinPC lpc{ cam.zNear, cam.zFar, (float)rc_->renderExtent.width, (float)rc_->renderExtent.height };
        vkCmdPushConstants(cmd, linDepthPipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(LinPC), &lpc);

        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // ── SSAO: 선형 깊이 + 노이즈 + 커널 → half-res AO ────────────────────────
    {
        // invProj/noiseScale 프레임 갱신
        if (cam.valid) {
            SSAOData* ubo = static_cast<SSAOData*>(dataAllocs_[frameIdx].mappedPtr);
            ubo->invProj    = cam.invProj;
            ubo->zFar       = cam.zFar;
            // half-res SSAO 타깃 기준으로 스케일합니다(hw=renderExtent/2). 4x4 노이즈가
            // half-res 4픽셀마다 한 번 반복되어(REPEAT) 픽셀당 회전이 제대로 흩어집니다.
            ubo->noiseScale = glm::vec2((float)hw / 4.f, (float)hh / 4.f);
        }

        VkRenderPassBeginInfo rpbi{};
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = ssaoPass_; rpbi.framebuffer = ssaoFramebuf_;
        rpbi.renderArea = {{0,0},{hw, hh}};
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport lvp{0,0,(float)hw,(float)hh,0.f,1.f}; VkRect2D lsc{{0,0},{hw,hh}};
        vkCmdSetViewport(cmd, 0, 1, &lvp); vkCmdSetScissor(cmd, 0, 1, &lsc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipeline_);

        VkDescriptorImageInfo  ldI { clampSampler, linDepthView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo  noI { noiseSampler_, noiseView_,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorBufferInfo ubI { dataBufs_[frameIdx], 0, sizeof(SSAOData) };
        VkWriteDescriptorSet ws[3] = {};
        ws[0].sType = ws[1].sType = ws[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[0].dstBinding=0; ws[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].descriptorCount=1; ws[0].pImageInfo=&ldI;
        ws[1].dstBinding=1; ws[1].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[1].descriptorCount=1; ws[1].pImageInfo=&noI;
        ws[2].dstBinding=2; ws[2].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         ws[2].descriptorCount=1; ws[2].pBufferInfo=&ubI;
        vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            ssaoPipeLayout_, 0, 3, ws);

        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
}
