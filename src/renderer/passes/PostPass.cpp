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

void PostPass::createVariantPasses(Variant& v) {
    // clear: 이번 프레임 이 이미지에 처음 쓸 때. 씬은 서브렉트에만 그려지므로 그 밖(패널 밑)을
    //        검게 지워 미정의 픽셀을 없앱니다. 그 위를 UI 패널이 덮습니다.
    // load : 같은 프레임 두 번째 합성(스테레오 흉내 모드의 오른쪽 눈). 앞의 결과를 남깁니다.
    for (int k = 0; k < 2; ++k) {
        const bool clear = (k == 0);
        VkAttachmentDescription att{};
        att.format = v.format; att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp  = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
        VkSubpassDependency deps[2] = {};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        // 같은 이미지에 앞서 쓴 결과(같은 프레임의 앞 눈)와 스왑체인 획득 세마포어(대기 단계가
        // COLOR_ATTACHMENT_OUTPUT)에 이어지도록 컬러 출력 단계끼리도 묶습니다.
        deps[1].srcSubpass = VK_SUBPASS_EXTERNAL; deps[1].dstSubpass = 0;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1; rpci.pAttachments = &att;
        rpci.subpassCount = 1; rpci.pSubpasses = &sp;
        rpci.dependencyCount = 2; rpci.pDependencies = deps;
        VkRenderPass& out = clear ? v.clearPass : v.loadPass;
        if (vkCreateRenderPass(rc_->device, &rpci, nullptr, &out) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post render pass!");
    }
}

void PostPass::destroyVariant(Variant& v) {
    if (v.pipeline)  { vkDestroyPipeline(rc_->device, v.pipeline, nullptr);    v.pipeline  = VK_NULL_HANDLE; }
    if (v.clearPass) { vkDestroyRenderPass(rc_->device, v.clearPass, nullptr); v.clearPass = VK_NULL_HANDLE; }
    if (v.loadPass)  { vkDestroyRenderPass(rc_->device, v.loadPass, nullptr);  v.loadPass  = VK_NULL_HANDLE; }
}

PostPass::Variant& PostPass::variantFor(VkFormat fmt) {
    for (auto& v : variants_)
        if (v.format == fmt) return v;
    Variant v;
    v.format = fmt;
    try {
        createVariantPasses(v);
        // 첫 변형(데스크톱)은 create 가 패스만 만들고, 파이프라인은 createAllPipelines 가
        // rebuildPipelines 로 만듭니다. 나중에 생기는 변형(XR 스왑체인 포맷)은 여기서 바로 만듭니다.
        if (!variants_.empty())
            v.pipeline = rc_->createFullscreenPipeline(v.clearPass, 0, "shaders/post.frag.spv", pipeLayout_);
    } catch (...) {
        destroyVariant(v);
        throw;
    }
    variants_.push_back(v);
    return variants_.back();
}

VkRenderPass PostPass::renderPassFor(VkFormat fmt) {
    return variantFor(fmt).clearPass;
}

void PostPass::create(RenderContext& rc) {
    rc_ = &rc;
    variants_.reserve(4);

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

    // Pipeline layout — push constants: 강도 · 노출 · 플래그 · UV 범위(PostPC, 40바이트)
    {
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; pcr.offset = 0; pcr.size = 40;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &setLayout_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        if (vkCreatePipelineLayout(rc.device, &pli, nullptr, &pipeLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post pipeline layout!");
    }

    // 데스크톱 스왑체인 포맷 변형([0]).
    variantFor(rc.swapFormat);
}

void PostPass::acquire(const std::vector<VkImageView>& swapViews) {
    framebufs_.resize(swapViews.size());
    for (size_t i = 0; i < swapViews.size(); i++) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = variants_[0].clearPass; fci.attachmentCount = 1; fci.pAttachments = &swapViews[i];
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
    // 새 파이프라인을 모두 만든 뒤에만 옛 것을 지웁니다(핫 리로드 실패 시 이전 것 유지).
    std::vector<VkPipeline> fresh;
    try {
        for (auto& v : variants_)
            fresh.push_back(rc_->createFullscreenPipeline(v.clearPass, 0, "shaders/post.frag.spv", pipeLayout_));
    } catch (...) {
        for (VkPipeline p : fresh) vkDestroyPipeline(rc_->device, p, nullptr);
        throw;
    }
    for (size_t i = 0; i < variants_.size(); ++i) {
        if (variants_[i].pipeline) vkDestroyPipeline(rc_->device, variants_[i].pipeline, nullptr);
        variants_[i].pipeline = fresh[i];
    }
}

void PostPass::destroy() {
    if (!rc_) return;
    releaseSizeDependent();
    for (auto& v : variants_) destroyVariant(v);
    variants_.clear();
    if (pipeLayout_) { vkDestroyPipelineLayout(rc_->device, pipeLayout_, nullptr);     pipeLayout_ = VK_NULL_HANDLE; }
    if (setLayout_)  { vkDestroyDescriptorSetLayout(rc_->device, setLayout_, nullptr); setLayout_  = VK_NULL_HANDLE; }
}

// ── recordTonemap ──────────────────────────────────────────────────────────────
void PostPass::recordTonemap(VkCommandBuffer cmd, const Output& out,
                             VkImageView hdrView, VkImageView ssaoView,
                             VkImageView bloomView, VkSampler clampSampler,
                             const OverlayHook* overlay) {
    const Variant& v = variantFor(out.format);
    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass  = out.clearAll ? v.clearPass : v.loadPass;
    rpbi.framebuffer = out.fb;
    // 처음 쓸 때는 renderArea 가 이미지 전체입니다(서브렉트 밖을 CLEAR 로 검게, 그 위를 UI 가 덮음).
    // 두 번째부터는 이번 사각형만 건드립니다.
    rpbi.renderArea = out.clearAll ? VkRect2D{ {0, 0}, out.extent } : out.rect;
    VkClearValue clear{}; clear.color = {{0.f, 0.f, 0.f, 1.f}};
    rpbi.clearValueCount = out.clearAll ? 1u : 0u;
    rpbi.pClearValues    = out.clearAll ? &clear : nullptr;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    // 씬(눈 하나의 HDR)을 출력 이미지의 사각형에 합성합니다. HDR UV[0,1] → 사각형.
    VkViewport vp{ (float)out.rect.offset.x, (float)out.rect.offset.y,
                   (float)out.rect.extent.width, (float)out.rect.extent.height, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &out.rect);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, v.pipeline);

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

    // Push constants: 강도 · 노출 · 플래그 · encodeSrgb · UV 범위 (post.frag 의 Params 와 같은 배치)
    struct PostPC { float aoStr, bloomStr, exposure, enableAO, enableBloom, encodeSrgb;
                    float uvScale[2], uvOffset[2]; };
    static_assert(sizeof(PostPC) == 40, "PostPC must match post.frag Params");
    // sRGB 이미지면 GPU 가 저장할 때 선형 → sRGB 로 바꿉니다. 그때 셰이더가 또 바꾸면 두 번 들어가
    // 화면이 밝고 흐려졌습니다(DEF-96). UNORM 이미지에서만 셰이더가 곡선을 적용합니다.
    const float encodeSrgb = isSrgbFormat(out.format) ? 0.f : 1.f;
    // exposure 는 1.0 이었다. 태양 0.85 + 하늘 1.0 배 조합에서 흰 표면의 선형값이
    // 0.72 여서 ACES 숄더에 얹혀 있었고, 코드값 221 부근에 몰려 평탄해 보였다.
    // 태양 3.0 / 하늘 0.30 배에 맞춘 값이 0.4 였고, 이때 평면이 코드값 169 로 중간톤에 앉았다.
    // 다만 그 0.4 는 감마가 두 번 들어간 화면을 보며 맞춘 값이었다(DEF-96). 감마를 바로잡은 뒤
    // 같은 목표(흰 평면이 중간톤)로 돌아가는 값이 약 1.0 이라 1.0 으로 둔다(2026-10-04 사용자 결정).
    // 태양·하늘·노출은 한 세트라서 따로 움직이면 대비가 다시 죽는다.
    PostPC ppc{ 0.8f, 0.04f, 1.0f, 1.f, 1.f, encodeSrgb,
                { out.uvScale[0], out.uvScale[1] }, { out.uvOffset[0], out.uvOffset[1] } };
    vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPC), &ppc);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    // 렌더 확장: 톤매핑 뒤(DEF-17). 같은 렌더 패스 · 서브패스 0 에서 그 사각형 위에 그립니다.
    if (overlay && *overlay) {
        vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &out.rect);
        (*overlay)(rpbi.renderPass);
    }
    vkCmdEndRenderPass(cmd);
}
