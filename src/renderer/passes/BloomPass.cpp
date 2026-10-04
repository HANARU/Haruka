#include "BloomPass.hpp"
#include <algorithm>
#include <stdexcept>

void BloomPass::create(RenderContext& rc) {
    rc_ = &rc;

    // Render pass: 단일 컬러 어태치먼트, shader-read로 종료 (모든 레벨 공용)
    {
        VkAttachmentDescription att{};
        att.format = VK_FORMAT_R16G16B16A16_SFLOAT; att.samples = VK_SAMPLE_COUNT_1_BIT;
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
        if (vkCreateRenderPass(rc.device, &rpci, nullptr, &pass_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create bloom render pass!");
    }

    // Descriptor set layout: push descriptor. binding 0 = blur source, binding 1 = 대응 down 밉
    // (업샘플 스킵-커넥션용, D-4). threshold/downsample 셰이더는 binding 0 만 접근하므로
    // binding 1 이 미바인딩이어도 안전합니다(레이아웃에만 있고 접근 안 함).
    {
        VkDescriptorSetLayoutBinding b[2]{};
        b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[0].descriptorCount = 1; b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[1].descriptorCount = 1; b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dslci.bindingCount = 2; dslci.pBindings = b;
        if (vkCreateDescriptorSetLayout(rc.device, &dslci, nullptr, &setLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create bloom desc layout!");
    }

    // Pipeline layout: PSO와 달리 setLayout에만 의존하므로 1회 생성
    {
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; pcr.offset = 0; pcr.size = 16;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &setLayout_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        if (vkCreatePipelineLayout(rc.device, &pli, nullptr, &pipeLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create bloom pipeline layout!");
    }

    acquireSizeDependent();
    // PSO는 Renderer::createAllPipelines → rebuildPipelines() 경로에서 생성
}

void BloomPass::acquireSizeDependent() {
    VkFormat fmt = VK_FORMAT_R16G16B16A16_SFLOAT;

    for (int i = 0; i < kBloomLevels; i++) {
        uint32_t w = std::max(1u, rc_->renderExtent.width  >> (i + 1));
        uint32_t h = std::max(1u, rc_->renderExtent.height >> (i + 1));
        extents_[i] = { w, h };
        downImages_[i] = rc_->createImage(w, h, 1, 1, VK_SAMPLE_COUNT_1_BIT,
            fmt, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            0, downAllocs_[i]);
        downViews_[i] = rc_->createImageView(downImages_[i], fmt, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    for (int i = 0; i < kBloomLevels - 1; i++) {
        int level = kBloomLevels - 2 - i;  // up[0]=smallest, up[kBloomLevels-2]=1/2 res
        uint32_t w = std::max(1u, rc_->renderExtent.width  >> (level + 1));
        uint32_t h = std::max(1u, rc_->renderExtent.height >> (level + 1));
        upImages_[i] = rc_->createImage(w, h, 1, 1, VK_SAMPLE_COUNT_1_BIT,
            fmt, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            0, upAllocs_[i]);
        upViews_[i] = rc_->createImageView(upImages_[i], fmt, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // Framebuffers: [0..kBloomLevels-1] = down, [kBloomLevels..] = up
    for (int i = 0; i < kBloomLevels; i++) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = pass_; fci.attachmentCount = 1; fci.pAttachments = &downViews_[i];
        fci.width = extents_[i].width; fci.height = extents_[i].height; fci.layers = 1;
        if (vkCreateFramebuffer(rc_->device, &fci, nullptr, &framebufs_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create bloom down framebuffer!");
    }
    for (int i = 0; i < kBloomLevels - 1; i++) {
        int level = kBloomLevels - 2 - i;
        uint32_t w = std::max(1u, rc_->renderExtent.width  >> (level + 1));
        uint32_t h = std::max(1u, rc_->renderExtent.height >> (level + 1));
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = pass_; fci.attachmentCount = 1; fci.pAttachments = &upViews_[i];
        fci.width = w; fci.height = h; fci.layers = 1;
        if (vkCreateFramebuffer(rc_->device, &fci, nullptr, &framebufs_[kBloomLevels + i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create bloom up framebuffer!");
    }
}

void BloomPass::releaseSizeDependent() {
    if (!rc_) return;
    for (auto& fb : framebufs_)
        if (fb) { vkDestroyFramebuffer(rc_->device, fb, nullptr); fb = VK_NULL_HANDLE; }

    auto freeSlot = [&](VkImage& img, VkImageView& view, HarukaAllocation& alloc) {
        if (view) { vkDestroyImageView(rc_->device, view, nullptr); view = VK_NULL_HANDLE; }
        if (img)  { vkDestroyImage(rc_->device, img, nullptr);      img  = VK_NULL_HANDLE; }
        if (alloc.isValid()) { rc_->allocator->freeB(alloc); alloc = {}; }
    };
    for (int i = 0; i < kBloomLevels;     i++) freeSlot(downImages_[i], downViews_[i], downAllocs_[i]);
    for (int i = 0; i < kBloomLevels - 1; i++) freeSlot(upImages_[i],   upViews_[i],   upAllocs_[i]);
}

void BloomPass::createPipelines() {
    threshPipe_ = rc_->createFullscreenPipeline(pass_, 0,
        "shaders/bloom_threshold.frag.spv", pipeLayout_);
    downPipe_ = rc_->createFullscreenPipeline(pass_, 0,
        "shaders/bloom_downsample.frag.spv", pipeLayout_);
    upPipe_ = rc_->createFullscreenPipeline(pass_, 0,
        "shaders/bloom_upsample.frag.spv", pipeLayout_, true);
}

void BloomPass::destroyPipelines() {
    if (!rc_) return;
    if (threshPipe_) { vkDestroyPipeline(rc_->device, threshPipe_, nullptr); threshPipe_ = VK_NULL_HANDLE; }
    if (downPipe_)   { vkDestroyPipeline(rc_->device, downPipe_,   nullptr); downPipe_   = VK_NULL_HANDLE; }
    if (upPipe_)     { vkDestroyPipeline(rc_->device, upPipe_,     nullptr); upPipe_     = VK_NULL_HANDLE; }
}

void BloomPass::rebuildPipelines() {
    // 새 파이프라인을 다 만든 뒤에만 옛 것을 지웁니다(핫 리로드 실패 시 이전 것 유지).
    const VkPipeline oldT = threshPipe_, oldD = downPipe_, oldU = upPipe_;
    threshPipe_ = downPipe_ = upPipe_ = VK_NULL_HANDLE;
    try { createPipelines(); }
    catch (...) {
        destroyPipelines();
        threshPipe_ = oldT; downPipe_ = oldD; upPipe_ = oldU;
        throw;
    }
    for (VkPipeline p : { oldT, oldD, oldU })
        if (p) vkDestroyPipeline(rc_->device, p, nullptr);
}

void BloomPass::destroy() {
    if (!rc_) return;
    releaseSizeDependent();
    destroyPipelines();
    if (pipeLayout_) { vkDestroyPipelineLayout(rc_->device, pipeLayout_, nullptr); pipeLayout_ = VK_NULL_HANDLE; }
    if (setLayout_)  { vkDestroyDescriptorSetLayout(rc_->device, setLayout_, nullptr); setLayout_ = VK_NULL_HANDLE; }
    if (pass_)       { vkDestroyRenderPass(rc_->device, pass_, nullptr); pass_ = VK_NULL_HANDLE; }
}

// ── record ─────────────────────────────────────────────────────────────────────
// 1. Threshold: HDR → Down[0]
// 2. Downsample: Down[k-1] → Down[k]
// 3. Upsample:   Down[N-1] → Up[0] → Up[1] → ... → Up[N-2]
void BloomPass::record(VkCommandBuffer cmd, VkImageView hdrView, VkSampler clampSampler) {
    // downView 가 주어지면 binding 1 로 함께 푸시합니다(업샘플 스킵-커넥션, D-4).
    // threshold/downsample 은 downView 없이 binding 0 만 씁니다.
    auto runPass = [&](VkPipeline pipe, VkImageView srcView, VkFramebuffer fb,
                       VkExtent2D ext, float threshold,
                       VkImageView downView = VK_NULL_HANDLE) {
        VkRenderPassBeginInfo rpbi{};
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = pass_; rpbi.framebuffer = fb;
        rpbi.renderArea = {{0,0},{ext.width, ext.height}};
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport bvp{0,0,(float)ext.width,(float)ext.height,0.f,1.f};
        VkRect2D   bsc{{0,0},{ext.width,ext.height}};
        vkCmdSetViewport(cmd, 0, 1, &bvp); vkCmdSetScissor(cmd, 0, 1, &bsc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        VkDescriptorImageInfo srcI { clampSampler, srcView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo downI{ clampSampler, downView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet bw[2]{};
        bw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        bw[0].dstBinding=0; bw[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bw[0].descriptorCount=1; bw[0].pImageInfo=&srcI;
        uint32_t nWrites = 1;
        if (downView) {
            bw[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            bw[1].dstBinding=1; bw[1].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bw[1].descriptorCount=1; bw[1].pImageInfo=&downI;
            nWrites = 2;
        }
        vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, nWrites, bw);
        float pc[4] = {threshold, 0.f, 0.f, 0.f};
        vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    };

    // Threshold → Down[0]
    runPass(threshPipe_, hdrView, framebufs_[0], extents_[0], 1.0f);

    // Downsample chain
    for (int k = 1; k < kBloomLevels; k++)
        runPass(downPipe_, downViews_[k-1], framebufs_[k], extents_[k], 0.f);

    // Upsample chain with skip-connections (D-4): up[i] = tent-blur(prev) + down[level].
    // 각 레벨에서 대응 down 밉(같은 해상도)을 더해야 중간 주파수가 살아납니다. 안 그러면
    // 최종 블룸이 최소 밉 하나를 4번 확대한 저주파 덩어리가 되고 down[0..N-2]는 기여가 0입니다.
    int upCount = kBloomLevels - 1;
    VkImageView curSrc = downViews_[kBloomLevels - 1];
    for (int i = 0; i < upCount; i++) {
        int level = kBloomLevels - 2 - i;
        uint32_t uw = std::max(1u, rc_->renderExtent.width  >> (level + 1));
        uint32_t uh = std::max(1u, rc_->renderExtent.height >> (level + 1));
        // downViews_[level] 은 이 up 타깃과 정확히 같은 해상도(renderExtent >> (level+1))입니다.
        runPass(upPipe_, curSrc, framebufs_[kBloomLevels + i], {uw, uh}, 0.f, downViews_[level]);
        curSrc = upViews_[i];
    }
}
