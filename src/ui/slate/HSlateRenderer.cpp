#include "HSlateRenderer.hpp"
#include "SlateLog.hpp"
#include "HPaintContext.hpp"
#include "HGeometry.hpp"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>

// ── 헬퍼 ──────────────────────────────────────────────────────────────────────
uint32_t HSlateRenderer::findMemType(VkMemoryRequirements reqs,
                                     VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physDevice_, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((reqs.memoryTypeBits >> i & 1) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("[HSlateRenderer] No suitable memory type!");
}

VkShaderModule HSlateRenderer::loadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        throw std::runtime_error("[HSlateRenderer] Cannot open shader: " + path);
    size_t size = f.tellg();
    f.seekg(0);
    std::vector<char> buf(size);
    f.read(buf.data(), size);

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode    = reinterpret_cast<const uint32_t*>(buf.data());
    VkShaderModule mod;
    vkCreateShaderModule(device_, &ci, nullptr, &mod);
    return mod;
}

// ── init ───────────────────────────────────────────────────────────────────────
void HSlateRenderer::init(VkDevice device, VkPhysicalDevice physDevice,
                          VkFormat swapFormat,
                          CmdBeginFn beginCmd, CmdEndFn endCmd,
                          const std::string& fontPath)
{
    device_     = device;
    physDevice_ = physDevice;
    colorFmt_   = swapFormat;

    createUIRenderPass();
    createPipeline();
    createWhiteTexture(beginCmd, endCmd);
    createBuffers(beginCmd, endCmd);
    fonts_.init(device, physDevice, beginCmd, endCmd, fontPath);
}

// ── createUIRenderPass ─────────────────────────────────────────────────────────
void HSlateRenderer::createUIRenderPass() {
    VkAttachmentDescription att{};
    att.format         = colorFmt_;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;   // preserve 3D scene
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // External dependency: wait for 3D pass color writes, then read/write for UI
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &att;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;
    if (vkCreateRenderPass(device_, &ci, nullptr, &uiRenderPass_) != VK_SUCCESS)
        throw std::runtime_error("[HSlateRenderer] Failed to create UI render pass!");
}

// ── rebuildFramebuffers ────────────────────────────────────────────────────────
void HSlateRenderer::destroyFramebuffers() {
    for (auto fb : framebuffers_)
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
}

void HSlateRenderer::rebuildFramebuffers(VkExtent2D extent,
                                          const std::vector<VkImageView>& imageViews) {
    destroyFramebuffers();
    framebuffers_.resize(imageViews.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < imageViews.size(); i++) {
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = uiRenderPass_;
        ci.attachmentCount = 1;
        ci.pAttachments    = &imageViews[i];
        ci.width           = extent.width;
        ci.height          = extent.height;
        ci.layers          = 1;
        if (vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]) != VK_SUCCESS)
            throw std::runtime_error("[HSlateRenderer] Failed to create UI framebuffer!");
    }
}

// ── cleanup ────────────────────────────────────────────────────────────────────
void HSlateRenderer::cleanup() {
    fonts_.cleanup();
    destroyFramebuffers();
    if (uiRenderPass_) { vkDestroyRenderPass(device_, uiRenderPass_, nullptr); uiRenderPass_ = VK_NULL_HANDLE; }

    vkDestroyPipeline           (device_, pipeline_,       nullptr);
    vkDestroyPipelineLayout     (device_, pipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, descSetLayout_,  nullptr);

    vkDestroySampler  (device_, sampler_,    nullptr);
    vkDestroyImageView(device_, whiteView_,  nullptr);
    vkDestroyImage    (device_, whiteImage_, nullptr);
    vkFreeMemory      (device_, whiteMem_,   nullptr);

    // vertexMem_ is persistently mapped — unmap before free
    vkUnmapMemory  (device_, vertexMem_);
    vkDestroyBuffer(device_, vertexBuf_, nullptr);
    vkFreeMemory   (device_, vertexMem_, nullptr);
    vkDestroyBuffer(device_, indexBuf_,  nullptr);
    vkFreeMemory   (device_, indexMem_,  nullptr);
}

// ── createPipeline ─────────────────────────────────────────────────────────────
void HSlateRenderer::createPipeline() {
    // Descriptor set layout — push descriptor, binding 0 = combined image sampler
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dslci.bindingCount = 1;
    dslci.pBindings    = &binding;
    vkCreateDescriptorSetLayout(device_, &dslci, nullptr, &descSetLayout_);

    // Push constant: vec2 screenSize (vertex shader)
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset     = 0;
    pc.size       = sizeof(glm::vec2);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &descSetLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pc;
    vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_);

    // Shaders
    VkShaderModule vertMod = loadSpv("shaders/ui.vert.spv");
    VkShaderModule fragMod = loadSpv("shaders/ui.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    // Vertex input: UIVertex { vec2 pos, vec2 uv, vec4 color }
    VkVertexInputBindingDescription vbind{};
    vbind.binding   = 0;
    vbind.stride    = sizeof(UIVertex);
    vbind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(UIVertex, pos)   };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(UIVertex, uv)    };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, color) };

    VkPipelineVertexInputStateCreateInfo visci{};
    visci.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    visci.vertexBindingDescriptionCount   = 1;
    visci.pVertexBindingDescriptions      = &vbind;
    visci.vertexAttributeDescriptionCount = 3;
    visci.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo iasci{};
    iasci.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iasci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpsci{};
    vpsci.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpsci.viewportCount = 1;
    vpsci.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasci{};
    rasci.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasci.polygonMode = VK_POLYGON_MODE_FILL;
    rasci.cullMode    = VK_CULL_MODE_NONE;
    rasci.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rasci.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo msci{};
    msci.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dssci{};
    dssci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // depth test / write both off (zero-initialized = false)

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable         = VK_TRUE;
    blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cbsci{};
    cbsci.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbsci.attachmentCount = 1;
    cbsci.pAttachments    = &blendAtt;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsci{};
    dsci.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsci.dynamicStateCount = 2;
    dsci.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo gpci{};
    gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.renderPass          = uiRenderPass_;
    gpci.subpass             = 0;
    gpci.stageCount          = 2;
    gpci.pStages             = stages;
    gpci.pVertexInputState   = &visci;
    gpci.pInputAssemblyState = &iasci;
    gpci.pViewportState      = &vpsci;
    gpci.pRasterizationState = &rasci;
    gpci.pMultisampleState   = &msci;
    gpci.pDepthStencilState  = &dssci;
    gpci.pColorBlendState    = &cbsci;
    gpci.pDynamicState       = &dsci;
    gpci.layout              = pipelineLayout_;

    vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, vertMod, nullptr);
    vkDestroyShaderModule(device_, fragMod, nullptr);
}

// ── createWhiteTexture ─────────────────────────────────────────────────────────
void HSlateRenderer::createWhiteTexture(CmdBeginFn beginCmd, CmdEndFn endCmd) {
    // Sampler (linear, clamp)
    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_LINEAR;
    sci.minFilter    = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device_, &sci, nullptr, &sampler_);

    // 1×1 RGBA image
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent        = { 1, 1, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(device_, &ici, nullptr, &whiteImage_);

    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(device_, whiteImage_, &reqs);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = findMemType(reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &ai, nullptr, &whiteMem_);
    vkBindImageMemory(device_, whiteImage_, whiteMem_, 0);

    VkImageViewCreateInfo vci{};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = whiteImage_;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(device_, &vci, nullptr, &whiteView_);

    // Upload white pixel via staging buffer
    uint8_t white[4] = { 255, 255, 255, 255 };
    VkBuffer stageBuf; VkDeviceMemory stageMem;
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = 4;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device_, &bi, nullptr, &stageBuf);
    vkGetBufferMemoryRequirements(device_, stageBuf, &reqs);
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = findMemType(reqs,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &ai, nullptr, &stageMem);
    vkBindBufferMemory(device_, stageBuf, stageMem, 0);
    void* ptr;
    vkMapMemory(device_, stageMem, 0, 4, 0, &ptr);
    memcpy(ptr, white, 4);
    vkUnmapMemory(device_, stageMem);

    auto cmd = beginCmd();

    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = whiteImage_;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    b.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0,nullptr,0,nullptr,1,&b);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { 1, 1, 1 };
    vkCmdCopyBufferToImage(cmd, stageBuf, whiteImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0,nullptr,0,nullptr,1,&b);

    endCmd(cmd);
    vkDestroyBuffer(device_, stageBuf, nullptr);
    vkFreeMemory   (device_, stageMem, nullptr);
}

// ── createBuffers ──────────────────────────────────────────────────────────────
void HSlateRenderer::createBuffers(CmdBeginFn /*beginCmd*/, CmdEndFn /*endCmd*/) {
    // Dynamic vertex buffer: HOST_VISIBLE | HOST_COHERENT, persistently mapped
    VkDeviceSize vbSize = kMaxVerts * sizeof(UIVertex);
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = vbSize;
    bi.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device_, &bi, nullptr, &vertexBuf_);

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device_, vertexBuf_, &reqs);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = findMemType(reqs,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &ai, nullptr, &vertexMem_);
    vkBindBufferMemory(device_, vertexBuf_, vertexMem_, 0);
    vkMapMemory(device_, vertexMem_, 0, VK_WHOLE_SIZE, 0,
                reinterpret_cast<void**>(&vertexPtr_));

    // Fixed index buffer: pre-filled quad pattern (0,1,2,0,2,3 per quad)
    VkDeviceSize ibSize = kMaxQuads * 6 * sizeof(uint32_t);
    bi.size  = ibSize;
    bi.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    vkCreateBuffer(device_, &bi, nullptr, &indexBuf_);
    vkGetBufferMemoryRequirements(device_, indexBuf_, &reqs);
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = findMemType(reqs,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &ai, nullptr, &indexMem_);
    vkBindBufferMemory(device_, indexBuf_, indexMem_, 0);

    uint32_t* idxPtr;
    vkMapMemory(device_, indexMem_, 0, VK_WHOLE_SIZE, 0,
                reinterpret_cast<void**>(&idxPtr));
    for (uint32_t q = 0; q < kMaxQuads; ++q) {
        idxPtr[q*6+0] = q*4+0; idxPtr[q*6+1] = q*4+1; idxPtr[q*6+2] = q*4+2;
        idxPtr[q*6+3] = q*4+0; idxPtr[q*6+4] = q*4+2; idxPtr[q*6+5] = q*4+3;
    }
    vkUnmapMemory(device_, indexMem_);
}

// ── record ─────────────────────────────────────────────────────────────────────
void HSlateRenderer::record(VkCommandBuffer cmd, uint32_t imgIdx,
                             VkExtent2D extent, uint32_t /*frameIdx*/) {
    if (!root_ || root_->isCollapsed()) return;
    if (imgIdx >= framebuffers_.size() || framebuffers_[imgIdx] == VK_NULL_HANDLE) return;

    HGeometry fullGeo;
    fullGeo.position = { 0.f, 0.f };
    fullGeo.size     = { (float)extent.width, (float)extent.height };
    fullGeo.scale    = 1.f;

    root_->computeDesiredSize();
    root_->onArrangeChildren(fullGeo);

    HPaintContext ctx(&fonts_);
    root_->paint(ctx, fullGeo);

    std::stable_sort(ctx.elements.begin(), ctx.elements.end(),
        [](const HDrawElement& a, const HDrawElement& b) {
            return a.layer < b.layer;
        });

    flush(cmd, imgIdx, extent, ctx.elements);
}

// ── flush ──────────────────────────────────────────────────────────────────────
void HSlateRenderer::flush(VkCommandBuffer cmd, uint32_t imgIdx,
                            VkExtent2D extent,
                            std::vector<HDrawElement>& elements) {
    // Expand Text elements into per-glyph Texture elements
    std::vector<HDrawElement> flat;
    flat.reserve(elements.size() * 4);
    for (const auto& e : elements) {
        if (e.type != HDrawElement::Type::Text) {
            flat.push_back(e);
            continue;
        }
        float penX  = e.rect.x;
        float baseY = e.rect.y + e.fontSize; // approximate baseline
        for (char c : e.text) {
            // 개행. 없으면 여러 줄 문자열이 한 줄로 이어붙어 그려집니다
            // (뷰포트 통계 오버레이 4줄이 실제로 그랬습니다).
            if (c == '\n') {
                penX   = e.rect.x;
                baseY += e.fontSize * 1.2f;
                continue;
            }
            auto g = fonts_.getGlyph(c, e.fontSize);
            if (g.pixelSize.x < 1.f || g.pixelSize.y < 1.f) {
                penX += g.advance;
                continue; // space or empty glyph
            }
            HDrawElement glyph{};
            glyph.type    = HDrawElement::Type::Texture;
            glyph.texture = fonts_.atlasView();
            glyph.color   = e.color;
            glyph.layer   = e.layer;
            glyph.rect    = { penX + g.bearing.x,
                              baseY - g.bearing.y,
                              g.pixelSize.x,
                              g.pixelSize.y };
            glyph.uvRect  = { g.uvMin.x, g.uvMin.y, g.uvMax.x, g.uvMax.y };
            flat.push_back(glyph);
            penX += g.advance;
        }
    }

    if (flat.empty()) return;

    // 글리프 확장이 끝난 **뒤에** 업로드해야 합니다. 위에서 getGlyph 가 새 글리프를
    // 래스터라이즈하므로, 확장 전에 업로드하면 그 프레임에 처음 나온 글자가 빈 텍셀로 그려집니다.
    if (fonts_.isDirty()) fonts_.upload();

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = uiRenderPass_;
    rpbi.framebuffer       = framebuffers_[imgIdx];
    rpbi.renderArea.extent = extent;
    // No clear values — LOAD_OP_LOAD preserves the 3D scene
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport vp{ 0.f, 0.f, (float)extent.width, (float)extent.height, 0.f, 1.f };
    VkRect2D   sc{ {0, 0}, extent };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor (cmd, 0, 1, &sc);

    glm::vec2 screenSize{ (float)extent.width, (float)extent.height };
    vkCmdPushConstants(cmd, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2), &screenSize);

    VkDeviceSize vbOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuf_, &vbOffset);
    vkCmdBindIndexBuffer  (cmd, indexBuf_, 0, VK_INDEX_TYPE_UINT32);

    uint32_t vertCount  = 0; // total vertices written this frame
    uint32_t batchStart = 0; // quad index where current batch begins

    auto emitQuad = [&](const glm::vec4& rect, const glm::vec4& uv,
                        const glm::vec4& color) -> bool {
        if (vertCount + 4 > kMaxVerts) return false; // overflow guard
        float x = rect.x, y = rect.y, w = rect.z, h = rect.w;
        float u0 = uv.x, v0 = uv.y, u1 = uv.z, v1 = uv.w;
        UIVertex* v = vertexPtr_ + vertCount;
        v[0] = {{ x,     y     }, { u0, v0 }, color };
        v[1] = {{ x + w, y     }, { u1, v0 }, color };
        v[2] = {{ x + w, y + h }, { u1, v1 }, color };
        v[3] = {{ x,     y + h }, { u0, v1 }, color };
        vertCount += 4;
        return true;
    };

    auto flushBatch = [&]() {
        uint32_t quadCount = (vertCount / 4) - batchStart;
        if (quadCount == 0) return;
        // vertexOffset shifts pre-generated indices so quad 0 maps to batchStart*4
        vkCmdDrawIndexed(cmd, quadCount * 6, 1, 0,
                         static_cast<int32_t>(batchStart * 4), 0);
        batchStart = vertCount / 4;
    };

    VkImageView curTex = VK_NULL_HANDLE;

    for (const auto& e : flat) {
        VkImageView tex    = (e.texture != VK_NULL_HANDLE) ? e.texture : whiteView_;
        glm::vec4   uvRect = e.uvRect;
        if (e.type == HDrawElement::Type::Rect) {
            tex    = whiteView_;
            uvRect = { 0.f, 0.f, 1.f, 1.f };
        }

        if (tex != curTex) {
            flushBatch();
            curTex = tex;
            pushDescriptor(cmd, curTex);
        }

        if (!emitQuad(e.rect, uvRect, e.color)) {
            // 예산을 넘으면 나머지가 조용히 사라집니다. 화면만 보고는 원인을 알 수 없으므로
            // 한 번은 알립니다(매 프레임 찍으면 로그 패널이 잠깁니다).
            static bool warned = false;
            if (!warned) {
                warned = true;
                HLOG(LogUI, Warning) << "UI vertex budget exceeded (" << kMaxVerts
                                     << ") - remaining draw elements dropped this frame";
            }
            break;
        }
    }
    flushBatch();

    vkCmdEndRenderPass(cmd);
}

// ── pushDescriptor ─────────────────────────────────────────────────────────────
void HSlateRenderer::pushDescriptor(VkCommandBuffer cmd, VkImageView view) {
    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler     = sampler_;
    imgInfo.imageView   = view;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imgInfo;

    vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipelineLayout_, 0, 1, &write);
}

// ── handlePointerEvent ─────────────────────────────────────────────────────────
HReply HSlateRenderer::handlePointerEvent(HPointerEvent e) {
    if (!root_ || !root_->isVisible()) return HReply::Unhandled();

    if (capturedWidget_) {
        HReply r;
        if (e.button >= 0) {
            r = capturedWidget_->onMouseButton(e);
            if (!e.down || r.mouseRelease) capturedWidget_ = nullptr;
        } else {
            r = capturedWidget_->onMouseMove(e);
        }
        return r;
    }

    HReply r;
    if (e.wheelDelta != 0.f) r = root_->onScroll(e);
    else if (e.button >= 0)  r = root_->onMouseButton(e);
    else                     r = root_->onMouseMove(e);

    if (r.mouseCapture) capturedWidget_ = r.mouseCapture;
    if (r.mouseRelease) capturedWidget_ = nullptr;
    return r;
}
