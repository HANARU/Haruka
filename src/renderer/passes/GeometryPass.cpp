#include "GeometryPass.hpp"
#include "../Vertex.hpp"
#include "../MaterialHandle.hpp"
#include <cassert>
#include <cstring>
#include <iterator>
#include <stdexcept>

// ── Helper: push material descriptor (set 3) ─────────────────────────────────
static void pushMaterialDesc(VkCommandBuffer cmd, VkPipelineLayout layout,
                              const MaterialHandle* mat,
                              VkSampler texS, VkSampler clampS) {
    VkDescriptorImageInfo imgs[5] = {
        { texS,   mat->albedo.imageView,    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { texS,   mat->normalMap.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { clampS, mat->metalRough.imageView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { clampS, mat->ao.imageView,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { texS,   mat->emissive.imageView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    VkWriteDescriptorSet ws[5] = {};
    for (int i = 0; i < 5; i++) {
        ws[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i].dstBinding = (uint32_t)i;
        ws[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[i].descriptorCount = 1; ws[i].pImageInfo = &imgs[i];
    }
    vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 3, 5, ws);
}

// PBR opaque/transparent 공용 PSO 빌더
static VkPipeline buildPBRPipeline(VkDevice device, VkPipelineCache cache,
                                   VkPipelineLayout layout, VkRenderPass rp, uint32_t subpass,
                                   VkSampleCountFlagBits msaa, bool transparent,
                                   VkShaderModule vert, VkShaderModule frag,
                                   VkCullModeFlags cullOverride = VK_CULL_MODE_FLAG_BITS_MAX_ENUM) {
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
    // A1: 투명 볼록 메시는 뒷면(CULL_FRONT)→앞면(CULL_BACK) 2패스로 셸 순서를 맞추므로
    // 호출자가 cullOverride 로 면을 지정합니다. 지정 없으면 기존 규칙(불투명 BACK, 투명 NONE).
    rs.cullMode = (cullOverride != VK_CULL_MODE_FLAG_BITS_MAX_ENUM) ? cullOverride
                : (transparent ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = msaa;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (transparent) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;   // both subpasses: depth read-only (pre-pass가 기록)
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2; ci.pStages = stages;
    ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vs; ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms; ci.pColorBlendState = &cb;
    ci.pDepthStencilState = &ds; ci.pDynamicState = &dyn;
    ci.layout = layout; ci.renderPass = rp; ci.subpass = subpass;

    VkPipeline pipe;
    if (vkCreateGraphicsPipelines(device, cache, 1, &ci, nullptr, &pipe) != VK_SUCCESS)
        throw std::runtime_error("Failed to create PBR pipeline!");
    return pipe;
}

// ── create ─────────────────────────────────────────────────────────────────────
// att[0]: MSAA color  (R16G16B16A16_SFLOAT, CLEAR → DONT_CARE)
// att[1]: MSAA depth  (CLEAR → DEPTH_STENCIL_READ_ONLY for linDepth pass)
// att[2]: HDR resolve (DONT_CARE → SHADER_READ_ONLY for post pass)
void GeometryPass::create(RenderContext& rc, VkPipelineLayout pbrLayout,
                          VkDescriptorSetLayout frameSetLayout) {
    rc_             = &rc;
    pbrLayout_      = pbrLayout;
    frameSetLayout_ = frameSetLayout;

    const VkFormat hdrFmt = kHdrFormat;

    VkAttachmentDescription atts[3] = {};
    atts[0].format = hdrFmt; atts[0].samples = rc.msaaSamples;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    atts[1].format = rc.depthFormat; atts[1].samples = rc.msaaSamples;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    atts[2].format = hdrFmt; atts[2].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef  { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthWrite{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRead { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    VkAttachmentReference resolveRef{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpasses[3] = {};
    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].pDepthStencilAttachment = &depthWrite;
    subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].colorAttachmentCount = 1; subpasses[1].pColorAttachments = &colorRef;
    subpasses[1].pDepthStencilAttachment = &depthRead;
    subpasses[2].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[2].colorAttachmentCount = 1; subpasses[2].pColorAttachments = &colorRef;
    subpasses[2].pDepthStencilAttachment = &depthRead;
    subpasses[2].pResolveAttachments = &resolveRef;

    VkSubpassDependency deps[4] = {};
    // External → subpass 0 (wait for shadow pass to finish writing depth)
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = 0; deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // Subpass 0 → 1: depth pre-pass done
    deps[1].srcSubpass = 0; deps[1].dstSubpass = 1;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    // Subpass 1 → 2: opaque done, continue transparent
    deps[2].srcSubpass = 1; deps[2].dstSubpass = 2;
    deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    // Subpass 2 → External: HDR resolve ready for post pass
    deps[3].srcSubpass = 2; deps[3].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[3].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 3; rpci.pAttachments = atts;
    rpci.subpassCount = 3; rpci.pSubpasses = subpasses;
    rpci.dependencyCount = 4; rpci.pDependencies = deps;
    if (vkCreateRenderPass(rc.device, &rpci, nullptr, &pass_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create main render pass!");

    createLineBuffer();
}

void GeometryPass::createHostVertexBuffer(VkDeviceSize size, const char* what,
                                          VkBuffer& buf, VkDeviceMemory& mem, void*& ptr) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(rc_->device, &bci, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error(std::string("Failed to create ") + what + " vertex buffer!");

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(rc_->device, buf, &reqs);
    uint32_t memType = rc_->findMemType(reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = reqs.size; mai.memoryTypeIndex = memType;
    if (vkAllocateMemory(rc_->device, &mai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error(std::string("Failed to allocate ") + what + " vertex memory!");
    vkBindBufferMemory(rc_->device, buf, mem, 0);
    vkMapMemory(rc_->device, mem, 0, VK_WHOLE_SIZE, 0, &ptr);
}

void GeometryPass::createLineBuffer() {
    // 라인과 기즈모 삼각형 버퍼를 프레임 슬롯마다 만듭니다(DEF-40).
    for (int i = 0; i < MAX_FRAMES; ++i) {
        createHostVertexBuffer(kMaxLineVerts * sizeof(LineVertex), "line",
                               lineVertBuf_[i], lineVertMem_[i], lineVertPtr_[i]);
        createHostVertexBuffer(kMaxTriVerts * sizeof(LineVertex), "gizmo tri",
                               triVertBuf_[i], triVertMem_[i], triVertPtr_[i]);
    }
}

void GeometryPass::acquireSizeDependent() {
    const VkFormat hdrFmt = kHdrFormat;
    uint32_t w = rc_->renderExtent.width, h = rc_->renderExtent.height;

    // Depth (MSAA)
    depthImage_ = rc_->createImage(w, h, 1, 1, rc_->msaaSamples,
        rc_->depthFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        0, depthAlloc_);
    depthView_ = rc_->createImageView(depthImage_, rc_->depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    auto cmd = rc_->beginOneTimeCmd();
    RenderContext::transitionImage(cmd, depthImage_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    rc_->endOneTimeCmd(cmd);

    // MSAA color (transient)
    msaaImage_ = rc_->createImage(w, h, 1, 1, rc_->msaaSamples,
        hdrFmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        0, msaaAlloc_);
    msaaView_ = rc_->createImageView(msaaImage_, hdrFmt, VK_IMAGE_ASPECT_COLOR_BIT);

    // HDR resolve targets (per frame-in-flight)
    for (int i = 0; i < MAX_FRAMES; i++) {
        hdrImage_[i] = rc_->createImage(w, h, 1, 1, VK_SAMPLE_COUNT_1_BIT,
            hdrFmt, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            0, hdrAlloc_[i]);
        hdrView_[i] = rc_->createImageView(hdrImage_[i], hdrFmt, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // Framebuffers (hdr[i]별 1개)
    framebufs_.resize(MAX_FRAMES);
    for (int i = 0; i < MAX_FRAMES; i++) {
        VkImageView atts[] = { msaaView_, depthView_, hdrView_[i] };
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = pass_;
        fci.attachmentCount = 3; fci.pAttachments = atts;
        fci.width = w; fci.height = h; fci.layers = 1;
        if (vkCreateFramebuffer(rc_->device, &fci, nullptr, &framebufs_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create main framebuffer!");
    }
}

void GeometryPass::releaseSizeDependent() {
    if (!rc_) return;
    for (auto fb : framebufs_) vkDestroyFramebuffer(rc_->device, fb, nullptr);
    framebufs_.clear();

    auto freeSlot = [&](VkImage& img, VkImageView& view, HarukaAllocation& alloc) {
        if (view) { vkDestroyImageView(rc_->device, view, nullptr); view = VK_NULL_HANDLE; }
        if (img)  { vkDestroyImage(rc_->device, img, nullptr);      img  = VK_NULL_HANDLE; }
        if (alloc.isValid()) { rc_->allocator->freeB(alloc); alloc = {}; }
    };
    freeSlot(msaaImage_, msaaView_, msaaAlloc_);
    freeSlot(depthImage_, depthView_, depthAlloc_);
    for (int i = 0; i < MAX_FRAMES; i++)
        freeSlot(hdrImage_[i], hdrView_[i], hdrAlloc_[i]);
}

void GeometryPass::createPipelines() {
    // Depth pre-pass (subpass 0)
    {
        auto vert = rc_->createShaderModule(readFile("shaders/depth.vert.spv"));
        auto frag = rc_->createShaderModule(readFile("shaders/depth.frag.spv"));

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
        rs.cullMode = VK_CULL_MODE_BACK_BIT; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = rc_->msaaSamples;

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
        if (vkCreateGraphicsPipelines(rc_->device, rc_->pipelineCache, 1, &ci, nullptr, &depthPipeline_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create depth pipeline!");

        vkDestroyShaderModule(rc_->device, vert, nullptr);
        vkDestroyShaderModule(rc_->device, frag, nullptr);
    }

    // Opaque + transparent PBR (subpass 1/2)
    {
        auto vert = rc_->createShaderModule(readFile("shaders/pbr.vert.spv"));
        auto frag = rc_->createShaderModule(readFile("shaders/pbr.frag.spv"));
        opaquePipeline_ = buildPBRPipeline(rc_->device, rc_->pipelineCache, pbrLayout_,
                                           pass_, 1, rc_->msaaSamples, false, vert, frag);
        transparentBackPipeline_  = buildPBRPipeline(rc_->device, rc_->pipelineCache, pbrLayout_,
                                                pass_, 2, rc_->msaaSamples, true, vert, frag,
                                                VK_CULL_MODE_FRONT_BIT);   // 뒷면 먼저
        transparentFrontPipeline_ = buildPBRPipeline(rc_->device, rc_->pipelineCache, pbrLayout_,
                                                pass_, 2, rc_->msaaSamples, true, vert, frag,
                                                VK_CULL_MODE_BACK_BIT);    // 앞면 나중
        vkDestroyShaderModule(rc_->device, vert, nullptr);
        vkDestroyShaderModule(rc_->device, frag, nullptr);
    }

    // Line overlay (subpass 2)
    {
        auto vert = rc_->createShaderModule(readFile("shaders/line.vert.spv"));
        auto frag = rc_->createShaderModule(readFile("shaders/line.frag.spv"));

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0; binding.stride = sizeof(LineVertex); binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[2] = {};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(LineVertex, pos);
        attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[1].offset = offsetof(LineVertex, color);

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1; vs.scissorCount = 1;

        VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = rc_->msaaSamples;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_FALSE; ds.depthWriteEnable = VK_FALSE;

        if (!linePipelineLayout_) {
            // 라인 · 기즈모도 눈 번호로 행렬을 고릅니다(line.vert, DEF-09).
            VkPushConstantRange viewRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GpuViewPush) };
            VkPipelineLayoutCreateInfo pli{};
            pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.setLayoutCount = 1; pli.pSetLayouts = &frameSetLayout_;
            pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &viewRange;
            if (vkCreatePipelineLayout(rc_->device, &pli, nullptr, &linePipelineLayout_) != VK_SUCCESS)
                throw std::runtime_error("Failed to create line pipeline layout!");
        }

        VkGraphicsPipelineCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.stageCount = 2; ci.pStages = stages;
        ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vs; ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms; ci.pColorBlendState = &cb;
        ci.pDepthStencilState = &ds; ci.pDynamicState = &dyn;
        ci.layout = linePipelineLayout_; ci.renderPass = pass_; ci.subpass = 2;
        if (vkCreateGraphicsPipelines(rc_->device, rc_->pipelineCache, 1, &ci, nullptr, &linePipeline_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create line pipeline!");

        // 기즈모 삼각형 파이프라인 — 라인과 동일하되 토폴로지만 삼각형입니다.
        // 같은 셰이더(line.vert/frag), 같은 레이아웃, 깊이 off, 블렌드 on 을 씁니다.
        VkPipelineInputAssemblyStateCreateInfo iaTri = ia;
        iaTri.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkGraphicsPipelineCreateInfo ciTri = ci;
        ciTri.pInputAssemblyState = &iaTri;
        if (vkCreateGraphicsPipelines(rc_->device, rc_->pipelineCache, 1, &ciTri, nullptr, &triPipeline_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create gizmo tri pipeline!");

        vkDestroyShaderModule(rc_->device, vert, nullptr);
        vkDestroyShaderModule(rc_->device, frag, nullptr);
    }
}

void GeometryPass::destroyPipelines() {
    if (!rc_) return;
    if (depthPipeline_)       { vkDestroyPipeline(rc_->device, depthPipeline_, nullptr);       depthPipeline_       = VK_NULL_HANDLE; }
    if (opaquePipeline_)      { vkDestroyPipeline(rc_->device, opaquePipeline_, nullptr);      opaquePipeline_      = VK_NULL_HANDLE; }
    if (transparentBackPipeline_)  { vkDestroyPipeline(rc_->device, transparentBackPipeline_,  nullptr); transparentBackPipeline_  = VK_NULL_HANDLE; }
    if (transparentFrontPipeline_) { vkDestroyPipeline(rc_->device, transparentFrontPipeline_, nullptr); transparentFrontPipeline_ = VK_NULL_HANDLE; }
    if (linePipeline_)        { vkDestroyPipeline(rc_->device, linePipeline_, nullptr);        linePipeline_        = VK_NULL_HANDLE; }
    if (triPipeline_)         { vkDestroyPipeline(rc_->device, triPipeline_, nullptr);         triPipeline_         = VK_NULL_HANDLE; }
}

void GeometryPass::rebuildPipelines() {
    // 새 파이프라인을 다 만든 뒤에만 옛 것을 지웁니다. 하나라도 실패하면 새로 만든 것만 지우고
    // 옛 것을 되돌려, 핫 리로드가 실패해도 이전 파이프라인으로 계속 그립니다.
    VkPipeline* slots[] = { &depthPipeline_, &opaquePipeline_, &transparentBackPipeline_,
                            &transparentFrontPipeline_, &linePipeline_, &triPipeline_ };
    VkPipeline old[std::size(slots)];
    for (size_t i = 0; i < std::size(slots); ++i) { old[i] = *slots[i]; *slots[i] = VK_NULL_HANDLE; }
    try { createPipelines(); }
    catch (...) {
        destroyPipelines();
        for (size_t i = 0; i < std::size(slots); ++i) *slots[i] = old[i];
        throw;
    }
    for (VkPipeline p : old)
        if (p) vkDestroyPipeline(rc_->device, p, nullptr);
}

void GeometryPass::destroy() {
    if (!rc_) return;
    releaseSizeDependent();
    destroyPipelines();
    if (linePipelineLayout_) { vkDestroyPipelineLayout(rc_->device, linePipelineLayout_, nullptr); linePipelineLayout_ = VK_NULL_HANDLE; }
    for (int i = 0; i < MAX_FRAMES; ++i) {
        if (lineVertPtr_[i]) { vkUnmapMemory(rc_->device, lineVertMem_[i]); lineVertPtr_[i] = nullptr; }
        if (lineVertBuf_[i]) { vkDestroyBuffer(rc_->device, lineVertBuf_[i], nullptr); lineVertBuf_[i] = VK_NULL_HANDLE; }
        if (lineVertMem_[i]) { vkFreeMemory(rc_->device, lineVertMem_[i], nullptr);    lineVertMem_[i] = VK_NULL_HANDLE; }
        if (triVertPtr_[i])  { vkUnmapMemory(rc_->device, triVertMem_[i]); triVertPtr_[i] = nullptr; }
        if (triVertBuf_[i])  { vkDestroyBuffer(rc_->device, triVertBuf_[i], nullptr); triVertBuf_[i] = VK_NULL_HANDLE; }
        if (triVertMem_[i])  { vkFreeMemory(rc_->device, triVertMem_[i], nullptr);    triVertMem_[i] = VK_NULL_HANDLE; }
    }
    if (pass_)        { vkDestroyRenderPass(rc_->device, pass_, nullptr);    pass_        = VK_NULL_HANDLE; }
}

// ── record ─────────────────────────────────────────────────────────────────────
void GeometryPass::record(VkCommandBuffer cmd, uint32_t frameIdx, uint32_t viewIndex,
                          const Inputs& in, VkQueryPool tsPool, uint32_t tsQueryAfterMain) {
    const GpuViewPush viewPush{ (int32_t)viewIndex };
    VkClearValue clearVals[3];
    clearVals[0].color        = {{0.18f, 0.20f, 0.25f, 1.0f}};
    clearVals[1].depthStencil = {1.0f, 0};
    clearVals[2].color        = {{0.f, 0.f, 0.f, 0.f}};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass      = pass_;
    rpbi.framebuffer     = framebufs_[frameIdx];
    rpbi.renderArea      = {{0,0}, rc_->renderExtent};
    rpbi.clearValueCount = 3; rpbi.pClearValues = clearVals;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{ 0, 0, (float)rc_->renderExtent.width, (float)rc_->renderExtent.height, 0.f, 1.f };
    VkRect2D   sc{ {0,0}, rc_->renderExtent };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor (cmd, 0, 1, &sc);

    // — Subpass 0: Depth pre-pass (CPU draws) ——————————————————————————————————
    // A-4: CPU 가 buildDrawCommands 에서 이미 프러스텀 컬링한 opaqueItems 를 깊이 프리패스도
    // 그대로 그립니다. 예전엔 GPU 컬 결과를 indirect 로 그렸으나, 입력이 이미 CPU 사전 컬링을
    // 통과한 집합이라 GPU 컬이 하나도 못 걸러 순수 오버헤드였습니다(dispatch·배리어·atomic).
    // 불투명 컬러(서브패스 1)와 같은 CPU 목록을 그리므로 '프리패스 집합==컬러 집합'이 자명합니다.
    // 깊이 전용이라 머티리얼 푸시는 필요 없습니다.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pbrLayout_, 0, 1, &in.frameSet, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pbrLayout_, 2, 1, &in.transformSet, 0, nullptr);
    // 눈 번호. 같은 레이아웃(pbrLayout_)의 파이프라인끼리는 바꿔 끼워도 푸시 상수가 남으므로
    // 세 서브패스에 한 번이면 됩니다.
    vkCmdPushConstants(cmd, pbrLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(viewPush), &viewPush);

    {
        VkDeviceSize offV = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &in.vertexBuffer, &offV);
        vkCmdBindIndexBuffer(cmd, in.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    }

    // 머티리얼이 null 인 항목은 세 서브패스 모두에서 건너뜁니다. 컬러 패스에서만 빼면 깊이만
    // 남아 뒤에 있는 물체를 가립니다(DEF-28). Renderer 가 오류 머티리얼로 바꿔 넣으므로 정상
    // 경로에서는 일어나지 않습니다.
    if (in.opaqueItems) {
        for (const auto& item : *in.opaqueItems) {
            assert(item.material && "DrawItem without material");
            if (!item.material) continue;
            vkCmdDrawIndexed(cmd, item.indexCount, item.instanceCount, item.firstIndex,
                             item.vertexOffset, item.firstInstance);
        }
    }

    // — Subpass 1: Opaque PBR (CPU draws with push descriptors) ———————————————
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipeline_);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor (cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pbrLayout_, 0, 1, &in.frameSet, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pbrLayout_, 2, 1, &in.transformSet, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pbrLayout_, 1, 1, &in.sceneSet, 0, nullptr);

    if (in.opaqueItems) {
        for (const auto& item : *in.opaqueItems) {
            if (!item.material) continue;
            pushMaterialDesc(cmd, pbrLayout_, item.material,
                             in.textureSampler, in.clampSampler);
            vkCmdDrawIndexed(cmd, item.indexCount, item.instanceCount, item.firstIndex,
                             item.vertexOffset, item.firstInstance);
        }
    }

    // 렌더 확장: 불투명 끝(DEF-17). 확장은 자기 파이프라인 · 버퍼를 바인딩하므로, 반투명 서브패스가
    // 다시 설정하지 않는 것(눈 번호 푸시 상수, 정점 · 인덱스 버퍼)을 여기서 되돌립니다.
    if (in.subpassHook && *in.subpassHook) {
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor (cmd, 0, 1, &sc);
        (*in.subpassHook)(1);
        vkCmdPushConstants(cmd, pbrLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(viewPush), &viewPush);
        VkDeviceSize offV = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &in.vertexBuffer, &offV);
        vkCmdBindIndexBuffer(cmd, in.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    }

    // — Subpass 2: Transparent PBR + line overlay ———————————————————————————
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
    if (in.transparentItems && !in.transparentItems->empty()) {
        // A1: 볼록 메시의 앞/뒤 셸 블렌드 순서를 맞추려면 뒷면을 먼저, 앞면을 나중에
        // 그립니다. 파이프라인 두 개가 depthWrite=FALSE·같은 pbrLayout_ 을 공유하므로,
        // 디스크립터 세트는 한 번만 바인딩하고 파이프라인만 바꿔 목록을 두 번 그립니다.
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor (cmd, 0, 1, &sc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pbrLayout_, 0, 1, &in.frameSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pbrLayout_, 2, 1, &in.transformSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pbrLayout_, 1, 1, &in.sceneSet, 0, nullptr);

        auto drawAll = [&]{
            for (const auto& item : *in.transparentItems) {
                assert(item.material && "DrawItem without material");
                if (!item.material) continue;
                pushMaterialDesc(cmd, pbrLayout_, item.material,
                                 in.textureSampler, in.clampSampler);
                vkCmdDrawIndexed(cmd, item.indexCount, item.instanceCount, item.firstIndex,
                                 item.vertexOffset, item.firstInstance);
            }
        };
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentBackPipeline_);
        drawAll();   // 뒷면
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentFrontPipeline_);
        drawAll();   // 앞면
    }

    // 렌더 확장: 반투명 끝(DEF-17). 아래 기즈모 · 라인은 파이프라인 · 뷰포트 · 디스크립터 · 정점 버퍼를
    // 모두 다시 설정하므로 되돌릴 것이 없습니다.
    if (in.subpassHook && *in.subpassHook) {
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor (cmd, 0, 1, &sc);
        (*in.subpassHook)(2);
    }

    // 기즈모 삼각형 오버레이 (subpass 2). 라인보다 먼저 그려서, 와이어 박스와
    // 회전 원호 같은 라인 핸들이 솔리드 화살표 위에 겹쳐 보이게 합니다.
    // 정점은 첫 눈에서만 복사합니다. 두 눈이 같은 프레임 슬롯의 같은 버퍼를 읽습니다.
    if (triVertPtr_[frameIdx] && !pendingTris_.empty()) {
        uint32_t count = std::min((uint32_t)pendingTris_.size(), kMaxTriVerts);
        if (viewIndex == 0)
            memcpy(triVertPtr_[frameIdx], pendingTris_.data(), count * sizeof(LineVertex));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipeline_);
        vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            linePipelineLayout_, 0, 1, &in.frameSet, 0, nullptr);
        vkCmdPushConstants(cmd, linePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(viewPush), &viewPush);
        VkDeviceSize offT = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &triVertBuf_[frameIdx], &offT);
        vkCmdDraw(cmd, count, 1, 0, 0);
    }

    // Line overlay (in subpass 2)
    if (lineVertPtr_[frameIdx] && !pendingLines_.empty()) {
        uint32_t count = std::min((uint32_t)pendingLines_.size(), kMaxLineVerts);
        if (viewIndex == 0)
            memcpy(lineVertPtr_[frameIdx], pendingLines_.data(), count * sizeof(LineVertex));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline_);
        vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            linePipelineLayout_, 0, 1, &in.frameSet, 0, nullptr);
        vkCmdPushConstants(cmd, linePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(viewPush), &viewPush);
        VkDeviceSize offV = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &lineVertBuf_[frameIdx], &offV);
        vkCmdDraw(cmd, count, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);

    // 렌더패스 전체(3 서브패스 + MSAA resolve) 가 끝난 시점입니다.
    // 예전에는 불투명 서브패스 직후에 찍혀 있어서, 투명 서브패스와 라인 오버레이와
    // resolve 가 전부 다음 구간(AO+Bloom)으로 넘어가 라벨이 거짓말을 하고 있었습니다.
    if (tsPool)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool, tsQueryAfterMain);
}
