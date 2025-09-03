#include "RenderContext.hpp"

VkShaderModule RenderContext::createShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module!");
    return mod;
}

VkImage RenderContext::createImage(uint32_t w, uint32_t h, uint32_t mips,
                                   uint32_t layers, VkSampleCountFlagBits samples,
                                   VkFormat fmt, VkImageTiling tiling,
                                   VkImageUsageFlags usage, VkImageCreateFlags flags,
                                   HarukaAllocation& outAlloc) const {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.flags         = flags;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = fmt;
    ci.extent        = { w, h, 1 };
    ci.mipLevels     = mips;
    ci.arrayLayers   = layers;
    ci.samples       = samples;
    ci.tiling        = tiling;
    ci.usage         = usage;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage img;
    if (vkCreateImage(device, &ci, nullptr, &img) != VK_SUCCESS)
        throw std::runtime_error("createImage failed!");
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(device, img, &reqs);
    outAlloc = allocator->allocB(reqs, ResourceType::Image);
    if (!outAlloc.isValid()) throw std::runtime_error("Zone B: image alloc failed!");
    if (allocator->bind(img, outAlloc) != VK_SUCCESS)
        throw std::runtime_error("Image bind failed!");
    return img;
}

VkImageView RenderContext::createImageView(VkImage img, VkFormat fmt,
                                           VkImageAspectFlags aspect,
                                           uint32_t mips, uint32_t layers,
                                           VkImageViewType viewType) const {
    VkImageViewCreateInfo ci{};
    ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image                           = img;
    ci.viewType                        = viewType;
    ci.format                          = fmt;
    ci.subresourceRange.aspectMask     = aspect;
    ci.subresourceRange.baseMipLevel   = 0;
    ci.subresourceRange.levelCount     = mips;
    ci.subresourceRange.baseArrayLayer = 0;
    ci.subresourceRange.layerCount     = layers;
    VkImageView view;
    if (vkCreateImageView(device, &ci, nullptr, &view) != VK_SUCCESS)
        throw std::runtime_error("createImageView failed!");
    return view;
}

VkPipeline RenderContext::createFullscreenPipeline(VkRenderPass rp, uint32_t subpass,
                                                   const char* fragSpv,
                                                   VkPipelineLayout layout,
                                                   bool enableBlend) const {
    auto vert = createShaderModule(readFile("shaders/post.vert.spv"));
    auto frag = createShaderModule(readFile(fragSpv));

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.f; rs.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (enableBlend) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2; ci.pStages = stages;
    ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vs; ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms; ci.pColorBlendState = &cb;
    ci.pDepthStencilState = &ds; ci.pDynamicState = &dyn;
    ci.layout = layout; ci.renderPass = rp; ci.subpass = subpass;

    VkPipeline pipe;
    if (vkCreateGraphicsPipelines(device, pipelineCache, 1, &ci, nullptr, &pipe) != VK_SUCCESS)
        throw std::runtime_error("Failed to create fullscreen pipeline!");

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return pipe;
}

uint32_t RenderContext::findMemType(uint32_t filter, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physDev, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((filter & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("No suitable memory type!");
}

void RenderContext::transitionImage(VkCommandBuffer cmd, VkImage img,
                                    VkImageLayout oldLayout, VkImageLayout newLayout,
                                    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                                    VkImageAspectFlags aspect, uint32_t mips, uint32_t layers) {
    VkImageMemoryBarrier2 b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask        = srcStage;  b.srcAccessMask = srcAccess;
    b.dstStageMask        = dstStage;  b.dstAccessMask = dstAccess;
    b.oldLayout           = oldLayout; b.newLayout     = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = { aspect, 0, mips, 0, layers };
    VkDependencyInfo di{};
    di.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers    = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}

VkCommandBuffer RenderContext::beginOneTimeCmd() const {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = cmdPool;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &ai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void RenderContext::endOneTimeCmd(VkCommandBuffer cmd) const {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(gfxQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(gfxQueue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}
