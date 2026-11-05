#include "ShadowPass.hpp"
#include "../Vertex.hpp"
#include "../RendererLog.hpp"
#include "../VulkanBaseline.hpp"
#include "../ShadowFit.hpp"
#include <stdexcept>
#include <string>

void ShadowPass::create(RenderContext& rc, VkPipelineLayout pbrLayout, uint32_t resolution) {
    rc_        = &rc;
    pbrLayout_ = pbrLayout;

    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(rc.physDev, &props);
        const bool pow2 = resolution != 0 && (resolution & (resolution - 1)) == 0;
        if (!pow2 || resolution < 256 || resolution > 4096 || resolution > props.limits.maxImageDimension2D) {
            HLOG(LogRenderer, Warning) << "Shadow map resolution " << resolution
                << " rejected (power of two in 256..4096 within the device limit); using " << kDefaultResolution;
            resolution = kDefaultResolution;
        }
        resolution_ = resolution;
    }

    // 깊이 형식은 조회해서 고릅니다(D32 → D16 → X8_D24, LINEAR 비교 필터가 되는 형식 먼저).
    const hvk::ShadowFormatChoice fmt = hvk::chooseShadowFormat([&](VkFormat f) {
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(rc.physDev, f, &p);
        return p.optimalTilingFeatures;
    });
    if (fmt.format == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("No shadow map depth format: none of D32_SFLOAT, D16_UNORM, X8_D24_UNORM_PACK32 "
                                 "supports depth attachment + sampled image with optimal tiling");
    format_        = fmt.format;
    linearCompare_ = fmt.linearCompare;
    HLOG(LogRenderer, Log) << "Shadow map format: " << hvk::shadowFormatName(format_)
                           << " (linear compare filter: " << (linearCompare_ ? "yes" : "no") << ")";
    if (!linearCompare_)
        HLOG(LogRenderer, Warning) << "Shadow map format has no SAMPLED_IMAGE_FILTER_LINEAR: hardware 2x2 PCF "
                                      "weights are not guaranteed (implementation-defined filtering)";
    {
        const float texelMm = 2000.f * hshadow::kRadius / (float)resolution_;
        HLOG(LogRenderer, Log) << "Shadow: " << resolution_ << "^2 over a " << hshadow::kRadius
                               << " m radius around the head (texel " << texelMm << " mm), depth clamp "
                               << (rc.vk.depthClamp ? "on" : "off");
        if (!rc.vk.depthClamp)
            HLOG(LogRenderer, Log) << "Shadow: no depthClamp, the projection near is extended " << hshadow::kCasterReach
                                   << " m toward the sun instead";
        HLOG(LogRenderer, Log) << "Shadow bias: back-face culling, slope " << kSlopeBiasFactor << " (clamp "
                               << kSlopeBiasClampTexels * texelMm << " mm), receiver " << kReceiverBiasTexels * texelMm
                               << " mm, normal offset " << kNormalOffsetTexels * texelMm << " mm";
        if (!rc.vk.depthBiasClamp)
            HLOG(LogRenderer, Warning) << "Device has no depthBiasClamp: the shadow slope bias is unclamped, so surfaces "
                                          "nearly parallel to the sun may lose their shadow near the silhouette";
    }

    // Shadow map: resolution_² depth-only(PC 4096², Quest 2048²)
    image_ = rc.createImage(resolution_, resolution_, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        format_, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        0, alloc_);
    view_ = rc.createImageView(image_, format_, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Shadow sampler: depth comparison required for sampler2DShadow PCF
    {
        VkSamplerCreateInfo si{};
        si.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter        = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        si.compareEnable    = VK_TRUE;
        // sampler2DShadow는 D_ref <compareOp> D_texel 을 평가해 통과하면 1.0을 돌려준다.
        // 엔진 전체가 정방향 Z(가까울수록 작은 값)이므로, "내가 빛이 처음 만난 그 표면이다"는
        // D_ref <= D_texel 이다. 따라서 LESS_OR_EQUAL이 밝음(1.0)을 뜻한다.
        //
        // 이전 판은 GREATER_OR_EQUAL이었다. 그것은 "가려졌을 때 밝음"이라는 반대 의미이며,
        // 아래 FRONT_BIT 컬링(섀도맵에 뒷면 깊이를 저장) 때문에 빛을 향한 모든 표면에서
        // 비교가 실패해 shadow=0이 되었다. pbr.frag의 `Lo *= shadow`가 그 0을 모든 라이트
        // 기여에 곱해서 직접광이 통째로 사라졌다. (DEF-36)
        //
        // pbr.frag sampleShadow 의 `proj.z -= ubo.shadowParams.z` 바이어스와 아래 borderColor(흰색=가장 먼 곳=항상 밝음)가
        // 둘 다 이 LESS_OR_EQUAL 규약을 전제하고 있다. 셋 중 하나만 뒤집으면 다시 깨진다.
        si.compareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;    // 1.0=lit, 0.0=shadow
        si.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.maxLod           = 0.f;
        if (vkCreateSampler(rc.device, &si, nullptr, &sampler_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shadow sampler!");
    }

    VkAttachmentDescription att{};
    att.format = format_; att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference depthRef{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency deps[2] = {};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1; rpci.pAttachments = &att;
    rpci.subpassCount = 1; rpci.pSubpasses = &sp;
    rpci.dependencyCount = 2; rpci.pDependencies = deps;
    if (vkCreateRenderPass(rc.device, &rpci, nullptr, &pass_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow render pass!");

    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = pass_; fci.attachmentCount = 1; fci.pAttachments = &view_;
    fci.width = resolution_; fci.height = resolution_; fci.layers = 1;
    if (vkCreateFramebuffer(rc.device, &fci, nullptr, &framebuffer_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow framebuffer!");
}

void ShadowPass::rebuildPipelines() {
    // 새 파이프라인을 먼저 만들고 성공했을 때만 옛 것을 지웁니다. 핫 리로드가 실패해도
    // 이전 파이프라인으로 계속 그립니다.
    auto vert = rc_->createShaderModule(readFile("shaders/shadow.vert.spv"));
    auto frag = rc_->createShaderModule(readFile("shaders/shadow.frag.spv"));

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

    auto binding    = Vertex::bindingDescription();
    auto attributes = Vertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = (uint32_t)attributes.size(); vi.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vs{};
    vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1; vs.scissorCount = 1;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.f;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;   // 메인 패스와 같은 면. 앞면 컬링을 버린 이유는 ShadowPass.hpp kReceiverBiasTexels 주석
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    // 구보다 빛 쪽의 캐스터를 깊이 0 에 눌러 담습니다(pancaking, ShadowFit.hpp). 기능이 없으면 투영 near 를 늘린 행렬을 씁니다.
    rs.depthClampEnable = rc_->vk.depthClamp ? VK_TRUE : VK_FALSE;
    rs.depthBiasEnable = VK_TRUE;
    // **이 값은 D32_SFLOAT 를 고른 경우 사실상 아무 일도 하지 않습니다.**
    // 부동소수 깊이 포맷의 최소 표현 단위 r 은 프리미티브의 최대 깊이 지수에서 유도되고(z_ndc 0.5 이상이면 2^-24),
    // 깊이 범위 40 m(depthClamp, 없으면 140 m)에서 1.25 x 5.96e-8 x 40 m ≈ 0.003 mm 입니다. 무시할 만합니다.
    // 경사 바이어스는 NDC 기울기에 비례하므로 월드 크기가 깊이 범위와 무관합니다(텍셀 × tanθ × 배율).
    // 상한(depthBiasClamp)은 NDC 깊이 단위라 깊이 범위로 나눠 넣습니다.
    //
    // 남겨 두는 이유는 정수 깊이 포맷(D16/D24)으로 바꿀 때 되살아나기 때문입니다. 형식 조회가
    // D16 으로 내려가면 1.25 x 2^-16 x 40 m ≈ 0.76 mm(140 m 면 약 2.7 mm, depthBiasExact 가 아니면 그 두 배까지)가 됩니다.
    // 다만 **이 값이 상수 바이어스를 담당한다고 믿지 마십시오.** 실제 담당은
    // FrameUBO.shadowParams.z(ShadowPass::kReceiverBiasTexels 를 NDC 로 바꾼 값)입니다.
    rs.depthBiasConstantFactor = 1.25f;
    rs.depthBiasSlopeFactor    = kSlopeBiasFactor;
    const float texelWorld = 2.f * hshadow::kRadius / (float)resolution_;
    rs.depthBiasClamp = rc_->vk.depthBiasClamp
        ? kSlopeBiasClampTexels * texelWorld / hshadow::depthRangeFor(hshadow::kRadius, rc_->vk.depthClamp) : 0.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2; ci.pStages = stages;
    ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vs; ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms; ci.pColorBlendState = &cb;
    ci.pDepthStencilState = &ds; ci.pDynamicState = &dyn;
    ci.layout = pbrLayout_; ci.renderPass = pass_; ci.subpass = 0;
    VkPipeline fresh = VK_NULL_HANDLE;
    const VkResult res = vkCreateGraphicsPipelines(rc_->device, rc_->pipelineCache, 1, &ci, nullptr, &fresh);
    if (res != VK_SUCCESS) {
        vkDestroyShaderModule(rc_->device, vert, nullptr);
        vkDestroyShaderModule(rc_->device, frag, nullptr);
        throw std::runtime_error("Failed to create shadow pipeline!");
    }
    if (pipeline_) vkDestroyPipeline(rc_->device, pipeline_, nullptr);
    pipeline_ = fresh;

    vkDestroyShaderModule(rc_->device, vert, nullptr);
    vkDestroyShaderModule(rc_->device, frag, nullptr);
}

void ShadowPass::destroy() {
    if (!rc_) return;
    if (view_)        { vkDestroyImageView(rc_->device, view_, nullptr);   view_  = VK_NULL_HANDLE; }
    if (image_)       { vkDestroyImage(rc_->device, image_, nullptr);      image_ = VK_NULL_HANDLE; }
    if (alloc_.isValid()) { rc_->allocator->freeB(alloc_); alloc_ = {}; }
    if (sampler_)     { vkDestroySampler(rc_->device, sampler_, nullptr);          sampler_     = VK_NULL_HANDLE; }
    if (framebuffer_) { vkDestroyFramebuffer(rc_->device, framebuffer_, nullptr);  framebuffer_ = VK_NULL_HANDLE; }
    if (pass_)        { vkDestroyRenderPass(rc_->device, pass_, nullptr);          pass_        = VK_NULL_HANDLE; }
    if (pipeline_)    { vkDestroyPipeline(rc_->device, pipeline_, nullptr);        pipeline_    = VK_NULL_HANDLE; }
    layoutReady_ = false;   // 다시 만들면 새 이미지는 UNDEFINED 부터 시작합니다
}

// ── record ─────────────────────────────────────────────────────────────────────
void ShadowPass::record(VkCommandBuffer cmd, const DrawInputs& in,
                        const std::vector<GpuDrawCmd>& draws, bool casterActive) {
    if (!pass_) return;
    if (!casterActive && layoutReady_) return;   // 읽히지 않고 레이아웃도 이미 준비됨

    VkClearValue depthClear{ .depthStencil = {1.f, 0} };
    VkRenderPassBeginInfo rpbi{};
    rpbi.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass  = pass_;
    rpbi.framebuffer = framebuffer_;
    rpbi.renderArea  = {{0,0},{resolution_,resolution_}};
    rpbi.clearValueCount = 1; rpbi.pClearValues = &depthClear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    layoutReady_ = true;

    // 그릴 것이 없으면 비우기(loadOp CLEAR)와 finalLayout 전환만 합니다. 씬이 비어 정점 버퍼가
    // 없을 수도 있으므로 바인딩도 하지 않습니다.
    if (draws.empty()) { vkCmdEndRenderPass(cmd); return; }

    VkViewport vp{0, 0, (float)resolution_, (float)resolution_, 0.f, 1.f};
    VkRect2D   sc{{0,0},{resolution_,resolution_}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor (cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pbrLayout_, 0, 1, &in.frameSet, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pbrLayout_, 2, 1, &in.transformSet, 0, nullptr);
    // 섀도 셰이더는 set 1을 읽지 않지만 레이아웃 호환을 위해 바인딩
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pbrLayout_, 1, 1, &in.sceneSet, 0, nullptr);

    VkDeviceSize offV = 0, offI = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &in.vertexBuffer, &offV);
    vkCmdBindIndexBuffer(cmd, in.indexBuffer, offI, VK_INDEX_TYPE_UINT32);

    for (const auto& dc : draws)
        vkCmdDrawIndexed(cmd, dc.indexCount, dc.instanceCount, dc.firstIndex,
                         dc.vertexOffset, dc.firstInstance);

    vkCmdEndRenderPass(cmd);
}
