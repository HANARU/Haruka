#include "PostPass.hpp"
#include <stdexcept>

void PostPass::create(RenderContext& rc) {
    rc_ = &rc;

    // Post render pass: writes directly to swapchain image
    {
        VkAttachmentDescription att{};
        att.format = rc.swapFormat; att.samples = VK_SAMPLE_COUNT_1_BIT;
        // CLEAR: 씬은 뷰포트 서브렉트에만 그려지므로, 뷰포트 밖(패널 밑)을 매 프레임 검게
        // 지워 미정의 픽셀을 없앱니다. 그 위를 UI 패널이 덮습니다.
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1; rpci.pAttachments = &att;
        rpci.subpassCount = 1; rpci.pSubpasses = &sp;
        rpci.dependencyCount = 1; rpci.pDependencies = &dep;
        if (vkCreateRenderPass(rc.device, &rpci, nullptr, &pass_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post render pass!");
    }

    // Descriptor set layout: push descriptor — 0=HDR, 1=taaHist, 2=ssao, 3=bloom
    {
        VkDescriptorSetLayoutBinding bindings[4] = {};
        for (int i = 0; i < 4; i++) {
            bindings[i].binding = (uint32_t)i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dslci.bindingCount = 4; dslci.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(rc.device, &dslci, nullptr, &setLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post desc layout!");
    }

    // Pipeline layout — push constants: aoStrength, bloomStrength, exposure, taaBlend, flags
    {
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; pcr.offset = 0; pcr.size = 32;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &setLayout_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        if (vkCreatePipelineLayout(rc.device, &pli, nullptr, &pipeLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post pipeline layout!");
    }
}

void PostPass::acquire(const std::vector<VkImageView>& swapViews) {
    framebufs_.resize(swapViews.size());
    for (size_t i = 0; i < swapViews.size(); i++) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = pass_; fci.attachmentCount = 1; fci.pAttachments = &swapViews[i];
        fci.width = rc_->extent.width; fci.height = rc_->extent.height; fci.layers = 1;
        if (vkCreateFramebuffer(rc_->device, &fci, nullptr, &framebufs_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post framebuffer!");
    }

    // TAA history — HDR(renderExtent)과 1:1로 섞으므로 renderExtent 크기로 재생성
    VkFormat hdrFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
    taaHistImage_ = rc_->createImage(rc_->renderExtent.width, rc_->renderExtent.height, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        hdrFmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        0, taaHistAlloc_);
    taaHistView_  = rc_->createImageView(taaHistImage_, hdrFmt, VK_IMAGE_ASPECT_COLOR_BIT);
    taaHistValid_ = false;
}

void PostPass::releaseSizeDependent() {
    if (!rc_) return;
    for (auto fb : framebufs_) vkDestroyFramebuffer(rc_->device, fb, nullptr);
    framebufs_.clear();

    if (taaHistView_)  { vkDestroyImageView(rc_->device, taaHistView_, nullptr); taaHistView_  = VK_NULL_HANDLE; }
    if (taaHistImage_) { vkDestroyImage(rc_->device, taaHistImage_, nullptr);    taaHistImage_ = VK_NULL_HANDLE; }
    if (taaHistAlloc_.isValid()) { rc_->allocator->freeB(taaHistAlloc_); taaHistAlloc_ = {}; }
    taaHistValid_ = false;
}

void PostPass::rebuildPipelines() {
    if (pipeline_) { vkDestroyPipeline(rc_->device, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    pipeline_ = rc_->createFullscreenPipeline(pass_, 0, "shaders/post.frag.spv", pipeLayout_);
}

void PostPass::destroy() {
    if (!rc_) return;
    releaseSizeDependent();
    if (pipeline_)   { vkDestroyPipeline(rc_->device, pipeline_, nullptr);             pipeline_   = VK_NULL_HANDLE; }
    if (pipeLayout_) { vkDestroyPipelineLayout(rc_->device, pipeLayout_, nullptr);     pipeLayout_ = VK_NULL_HANDLE; }
    if (setLayout_)  { vkDestroyDescriptorSetLayout(rc_->device, setLayout_, nullptr); setLayout_  = VK_NULL_HANDLE; }
    if (pass_)       { vkDestroyRenderPass(rc_->device, pass_, nullptr);               pass_       = VK_NULL_HANDLE; }
}

// ── recordTonemap ──────────────────────────────────────────────────────────────
void PostPass::recordTonemap(VkCommandBuffer cmd, uint32_t imgIdx,
                             VkImageView hdrView, VkImageView ssaoView,
                             VkImageView bloomView, VkSampler clampSampler,
                             bool taaEnabled) {
    // First frame: transition taaHist to readable layout
    if (!taaHistValid_) {
        RenderContext::transitionImage(cmd, taaHistImage_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE,  VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        taaHistValid_ = true;
    }

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = pass_; rpbi.framebuffer = framebufs_[imgIdx];
    // renderArea 는 스왑체인 전체입니다(뷰포트 밖은 CLEAR 로 검게, 그 위를 UI 가 덮음).
    rpbi.renderArea = {{0,0}, rc_->extent};
    VkClearValue clear{}; clear.color = {{0.f, 0.f, 0.f, 1.f}};
    rpbi.clearValueCount = 1; rpbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    // 씬(renderExtent HDR)을 스왑체인의 뷰포트 서브렉트에 합성합니다. HDR UV[0,1] → 서브렉트.
    VkViewport vp{ (float)rc_->viewportOffset.x, (float)rc_->viewportOffset.y,
                   (float)rc_->renderExtent.width, (float)rc_->renderExtent.height, 0.f, 1.f };
    VkRect2D   sc{ rc_->viewportOffset, rc_->renderExtent };
    vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkDescriptorImageInfo hdrI  { clampSampler, hdrView,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo taaI  { clampSampler, taaHistView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo ssaoI { clampSampler, ssaoView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo blmI  { clampSampler, bloomView,    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

    VkWriteDescriptorSet ws[4] = {};
    for (int i = 0; i < 4; i++) {
        ws[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i].dstBinding = (uint32_t)i;
        ws[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[i].descriptorCount = 1;
    }
    ws[0].pImageInfo = &hdrI;
    ws[1].pImageInfo = &taaI;
    ws[2].pImageInfo = &ssaoI;
    ws[3].pImageInfo = &blmI;
    vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 4, ws);

    // Push constants: aoStrength, bloomStrength, exposure, taaBlend, enabled flags
    struct PostPC { float aoStr, bloomStr, exposure, taaBlend; float enableAO, enableBloom, enableTAA, pad; };
    // exposure 는 1.0 이었다. 태양 0.85 + 하늘 1.0 배 조합에서 흰 표면의 선형값이
    // 0.72 여서 ACES 숄더에 얹혀 있었고, 코드값 221 부근에 몰려 평탄해 보였다.
    // 태양 3.0 / 하늘 0.30 배에 맞춘 값이 0.4 이고, 이때 평면이 코드값 169 로
    // 중간톤에 앉는다. 셋은 한 세트라서 따로 움직이면 대비가 다시 죽는다.
    PostPC ppc{ 0.8f, 0.04f, 0.4f, 0.1f, 1.f, 1.f, taaEnabled ? 1.f : 0.f, 0.f };
    vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, &ppc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

// ── recordHistoryBlit ──────────────────────────────────────────────────────────
// hdrImage는 main pass finalLayout인 SHADER_READ_ONLY 상태로 들어온다.
void PostPass::recordHistoryBlit(VkCommandBuffer cmd, VkImage hdrImage) {
    RenderContext::transitionImage(cmd, hdrImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    RenderContext::transitionImage(cmd, taaHistImage_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkImageBlit region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.srcOffsets[1]  = { (int32_t)rc_->renderExtent.width, (int32_t)rc_->renderExtent.height, 1 };
    region.dstSubresource = region.srcSubresource;
    region.dstOffsets[1]  = region.srcOffsets[1];
    vkCmdBlitImage(cmd,
        hdrImage,      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        taaHistImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region, VK_FILTER_LINEAR);

    RenderContext::transitionImage(cmd, taaHistImage_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
