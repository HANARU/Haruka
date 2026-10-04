#include "PostPass.hpp"
#include <stdexcept>

// 저장할 때 GPU 가 선형 → sRGB 로 바꾸는 포맷인지.
static bool isSrgbFormat(VkFormat f) {
    switch (f) {
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        case VK_FORMAT_R8G8B8_SRGB:
        case VK_FORMAT_B8G8R8_SRGB:
            return true;
        default:
            return false;
    }
}

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

    // Descriptor set layout: push descriptor — 0=HDR, 1=ssao, 2=bloom
    {
        VkDescriptorSetLayoutBinding bindings[3] = {};
        for (int i = 0; i < 3; i++) {
            bindings[i].binding = (uint32_t)i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dslci.bindingCount = 3; dslci.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(rc.device, &dslci, nullptr, &setLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post desc layout!");
    }

    // Pipeline layout — push constants: aoStrength, bloomStrength, exposure, flags
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
}

void PostPass::releaseSizeDependent() {
    if (!rc_) return;
    for (auto fb : framebufs_) vkDestroyFramebuffer(rc_->device, fb, nullptr);
    framebufs_.clear();
}

void PostPass::rebuildPipelines() {
    // 새 파이프라인을 먼저 만들고 성공했을 때만 옛 것을 지웁니다(핫 리로드 실패 시 이전 것 유지).
    VkPipeline fresh = rc_->createFullscreenPipeline(pass_, 0, "shaders/post.frag.spv", pipeLayout_);
    if (pipeline_) vkDestroyPipeline(rc_->device, pipeline_, nullptr);
    pipeline_ = fresh;
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
                             VkImageView bloomView, VkSampler clampSampler) {
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

    VkDescriptorImageInfo hdrI  { clampSampler, hdrView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo ssaoI { clampSampler, ssaoView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo blmI  { clampSampler, bloomView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

    VkWriteDescriptorSet ws[3] = {};
    for (int i = 0; i < 3; i++) {
        ws[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i].dstBinding = (uint32_t)i;
        ws[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[i].descriptorCount = 1;
    }
    ws[0].pImageInfo = &hdrI;
    ws[1].pImageInfo = &ssaoI;
    ws[2].pImageInfo = &blmI;
    vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 3, ws);

    // Push constants: aoStrength, bloomStrength, exposure, enabled flags, encodeSrgb
    struct PostPC { float aoStr, bloomStr, exposure, enableAO; float enableBloom, encodeSrgb, pad0, pad1; };
    // sRGB 스왑체인이면 GPU 가 저장할 때 선형 → sRGB 로 바꿉니다. 그때 셰이더가 또 바꾸면 두 번 들어가
    // 화면이 밝고 흐려졌습니다(DEF-96). UNORM 으로 떨어진 기기에서만 셰이더가 곡선을 적용합니다.
    const float encodeSrgb = isSrgbFormat(rc_->swapFormat) ? 0.f : 1.f;
    // exposure 는 1.0 이었다. 태양 0.85 + 하늘 1.0 배 조합에서 흰 표면의 선형값이
    // 0.72 여서 ACES 숄더에 얹혀 있었고, 코드값 221 부근에 몰려 평탄해 보였다.
    // 태양 3.0 / 하늘 0.30 배에 맞춘 값이 0.4 였고, 이때 평면이 코드값 169 로 중간톤에 앉았다.
    // 다만 그 0.4 는 감마가 두 번 들어간 화면을 보며 맞춘 값이었다(DEF-96). 감마를 바로잡은 뒤
    // 같은 목표(흰 평면이 중간톤)로 돌아가는 값이 약 1.0 이라 1.0 으로 둔다(2026-10-04 사용자 결정).
    // 태양·하늘·노출은 한 세트라서 따로 움직이면 대비가 다시 죽는다.
    PostPC ppc{ 0.8f, 0.04f, 1.0f, 1.f, 1.f, encodeSrgb, 0.f, 0.f };
    vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, &ppc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}
