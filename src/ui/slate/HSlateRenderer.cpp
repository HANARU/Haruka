#include "HSlateRenderer.hpp"
#include "SlateLog.hpp"
#include "HPaintContext.hpp"
#include "HGeometry.hpp"
#include "../../vfs/Vfs.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <chrono>
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
    // 논리 경로로 가상 파일 시스템에서 읽습니다(실행 폴더의 낱개 파일 또는 묶음 파일, 2026-10-24).
    std::vector<uint8_t> buf;
    std::string err;
    if (!hvfs::readAll(path, buf, &err))
        throw std::runtime_error("[HSlateRenderer] Cannot open shader: " + err);
    const size_t size = buf.size();

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
                          VkFormat swapFormat, PFN_vkCmdPushDescriptorSetKHR pushDescriptor,
                          CmdBeginFn beginCmd, CmdEndFn endCmd,
                          const std::string& fontPath)
{
    if (!pushDescriptor) throw std::runtime_error("HSlateRenderer needs vkCmdPushDescriptorSetKHR");
    device_            = device;
    physDevice_        = physDevice;
    colorFmt_          = swapFormat;
    pushDescriptorFn_  = pushDescriptor;

    if (colorFmt_ != VK_FORMAT_UNDEFINED) { createUIRenderPass(); createWindowRenderPass(); }
    createPanelRenderPass();
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

// ── createWindowRenderPass (다중 OS 창 M2) ─────────────────────────────────────
// 보조 창은 앞에 3D 패스가 없으므로 이전 내용을 버리고(UNDEFINED) 지우며 시작합니다. 메인 오버레이 패스(LOAD)를 이 방식으로
// 바꾸면 3D 화면이 지워지므로 따로 둡니다. 형식 · 샘플 수 · 서브패스가 메인 패스와 같아 pipeline_ 을 그대로 씁니다
// (렌더 패스 호환성은 load · store · 배치를 따지지 않음, 의존은 같게 둠).
void HSlateRenderer::createWindowRenderPass() {
    VkAttachmentDescription att{};
    att.format         = colorFmt_;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // 외부 의존은 메인 오버레이 패스(createUIRenderPass)와 **똑같이** 둡니다. 파이프라인을 같이 쓰려면 두 패스가 호환이어야 하는데,
    // 검증 계층은 서브패스 의존(접근 마스크 · BY_REGION)까지 같아야 호환으로 봅니다(VUID-vkCmdDrawIndexed-renderPass-02684,
    // M2 Debug 실행에서 확인). 단계가 COLOR_ATTACHMENT_OUTPUT 이라 제출이 기다리는 acquire 세마포어 단계와도 맞습니다(배치 전환 ·
    // 지우기가 표시 엔진이 이미지를 놓은 뒤). 나갈 때의 표시는 신호 세마포어가 맡습니다.
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
    if (vkCreateRenderPass(device_, &ci, nullptr, &windowRenderPass_) != VK_SUCCESS)
        throw std::runtime_error("[HSlateRenderer] Failed to create window render pass!");
}

// ── createPanelRenderPass (3D UI P1a) ──────────────────────────────────────────
void HSlateRenderer::createPanelRenderPass() {
    VkAttachmentDescription att{};
    att.format         = kPanelFormat;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;     // 다시 그릴 때마다 처음부터(리테인드라 바뀐 프레임에만)
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;       // 이전 내용은 버립니다
    att.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;   // 렌더러가 이어서 밉을 블릿합니다

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // 들어올 때: 앞 프레임이 이 텍스처를 조각 셰이더로 읽고(월드 패널) 블릿으로 밉을 쓴 것이 끝난 뒤 지웁니다.
    // 나갈 때: 렌더러의 밉 블릿(전송)이나 셰이더 읽기가 이 패스의 쓰기를 봅니다.
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &att;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 2;
    ci.pDependencies   = deps;
    if (vkCreateRenderPass(device_, &ci, nullptr, &panelRenderPass_) != VK_SUCCESS)
        throw std::runtime_error("[HSlateRenderer] Failed to create panel render pass!");
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
    if (uiRenderPass_ == VK_NULL_HANDLE) return;   // 패널 전용(스왑체인 출력 없음)
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
    // 정점 예산의 근거(다중 OS 창 M2): 한 프레임 슬롯에 쓴 정점 수의 최댓값과 목록 하나의 최댓값.
    if (peakSlotVerts_ > 0) {
        HLOG(LogUI, Log) << "UI vertices: peak per frame slot " << peakSlotVerts_ << ", largest list " << peakListVerts_
                         << ", slot capacity " << slotVerts_ << ", list limit " << kMaxVerts;
    }
    fonts_.cleanup();
    destroyFramebuffers();
    vkDestroyPipeline(device_, panelPipeline_, nullptr);
    panelPipeline_ = VK_NULL_HANDLE;
    vkDestroyRenderPass(device_, panelRenderPass_, nullptr);
    panelRenderPass_ = VK_NULL_HANDLE;
    if (uiRenderPass_) { vkDestroyRenderPass(device_, uiRenderPass_, nullptr); uiRenderPass_ = VK_NULL_HANDLE; }
    if (windowRenderPass_) { vkDestroyRenderPass(device_, windowRenderPass_, nullptr); windowRenderPass_ = VK_NULL_HANDLE; }

    vkDestroyPipeline           (device_, pipeline_,       nullptr);
    vkDestroyPipelineLayout     (device_, pipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, descSetLayout_,  nullptr);

    vkDestroySampler  (device_, sampler_,    nullptr);
    vkDestroyImageView(device_, whiteView_,  nullptr);
    vkDestroyImage    (device_, whiteImage_, nullptr);
    vkFreeMemory      (device_, whiteMem_,   nullptr);

    // vertexMem_ is persistently mapped — unmap before free
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vertexMem_[i]) vkUnmapMemory(device_, vertexMem_[i]);   // init 이 중간에 실패했으면 없음
        vkDestroyBuffer(device_, vertexBuf_[i], nullptr);
        vkFreeMemory   (device_, vertexMem_[i], nullptr);
    }
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

    if (uiRenderPass_) pipeline_ = buildPipeline(uiRenderPass_, false);
    panelPipeline_ = buildPipeline(panelRenderPass_, true);
}

// 에디터 오버레이(premultipliedTarget = false)와 패널(true)은 색 블렌딩이 같고 알파만 다릅니다.
// 에디터 스왑체인은 불투명 대상이므로 그림자·둥근 모서리 반투명 픽셀이 스왑체인 알파를 깎아 캡처(PNG)나
// 합성기에서 투명 구멍이 나지 않게 대상 알파를 보존합니다(ZERO + ONE). 패널은 투명으로 지운 오프스크린
// 대상 위라 알파를 "over" 로 덧쌓습니다(ONE + ONE_MINUS_SRC_ALPHA).
VkPipeline HSlateRenderer::buildPipeline(VkRenderPass renderPass, bool premultipliedTarget) {
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

    // Vertex input: UIVertex { vec2 pos, vec2 uv, vec4 color, vec2 localPos, vec2 halfSize, vec4 params, vec4 borderColor }
    VkVertexInputBindingDescription vbind{};
    vbind.binding   = 0;
    vbind.stride    = sizeof(UIVertex);
    vbind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[7]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(UIVertex, pos)         };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(UIVertex, uv)          };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, color)       };
    attrs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(UIVertex, localPos)    };
    attrs[4] = { 4, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(UIVertex, halfSize)    };
    attrs[5] = { 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, params)      };
    attrs[6] = { 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, borderColor) };

    VkPipelineVertexInputStateCreateInfo visci{};
    visci.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    visci.vertexBindingDescriptionCount   = 1;
    visci.pVertexBindingDescriptions      = &vbind;
    visci.vertexAttributeDescriptionCount = 7;
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
    blendAtt.srcAlphaBlendFactor = premultipliedTarget ? VK_BLEND_FACTOR_ONE
                                                       : VK_BLEND_FACTOR_ZERO;
    blendAtt.dstAlphaBlendFactor = premultipliedTarget ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
                                                       : VK_BLEND_FACTOR_ONE;
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
    gpci.renderPass          = renderPass;
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

    VkPipeline pipe = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipe);
    vkDestroyShaderModule(device_, vertMod, nullptr);
    vkDestroyShaderModule(device_, fragMod, nullptr);
    return pipe;
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
    // Dynamic vertex buffers: HOST_VISIBLE | HOST_COHERENT, persistently mapped, 프레임 슬롯마다 하나.
    // 크기는 슬롯 정점 수(slotVerts_)입니다. 인덱스 버퍼는 배치 하나가 목록 상한(kMaxQuads)을 넘지 않으므로 그대로 둡니다
    // (그리기는 vertexOffset 으로 쿼드 번호를 밀어 같은 인덱스를 씀).
    VkDeviceSize vbSize = (VkDeviceSize)slotVerts_ * sizeof(UIVertex);
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = vbSize;
    bi.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkMemoryRequirements reqs;
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        vkCreateBuffer(device_, &bi, nullptr, &vertexBuf_[i]);
        vkGetBufferMemoryRequirements(device_, vertexBuf_[i], &reqs);
        ai.allocationSize  = reqs.size;
        ai.memoryTypeIndex = findMemType(reqs,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device_, &ai, nullptr, &vertexMem_[i]);
        vkBindBufferMemory(device_, vertexBuf_[i], vertexMem_[i], 0);
        vkMapMemory(device_, vertexMem_[i], 0, VK_WHOLE_SIZE, 0,
                    reinterpret_cast<void**>(&vertexPtr_[i]));
    }

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
bool HSlateRenderer::record(VkCommandBuffer cmd, uint32_t imgIdx,
                             VkExtent2D extent, uint32_t frameIdx) {
    // 슬롯 정점 오프셋은 렌더러가 프레임 시작에서 beginFrame 으로 돌려 둡니다. 같은 프레임의 패널이 먼저
    // 그 슬롯에 썼다면 그 뒤에 이어 씁니다(3D UI P1a).
    if (uiRenderPass_ == VK_NULL_HANDLE) return false;
    if (imgIdx >= framebuffers_.size() || framebuffers_[imgIdx] == VK_NULL_HANDLE) return false;

    const glm::vec2 size{ (float)extent.width, (float)extent.height };
    HSlateDrawList drawList;
    if (!ctx_.buildDrawList(size, fonts_, whiteView_, kMaxVerts, drawList))
        return false;

    // 글리프 확장이 끝난 **뒤에** 업로드해야 합니다. 위에서 getGlyph 가 새 글리프를
    // 래스터라이즈하므로, 확장 전에 업로드하면 그 프레임에 처음 나온 글자가 빈 텍셀로 그려집니다.
    if (fonts_.isDirty()) fonts_.upload();

    return recordDrawList(cmd, uiRenderPass_, framebuffers_[imgIdx], extent, drawList, frameIdx);
}

static bool buildDrawListFromElements(const std::vector<HDrawElement>& elements,
                                      HFontAtlas& fonts, VkImageView whiteView,
                                      uint32_t maxVerts, HSlateDrawList& out) {
    out.vertices.clear();
    out.batches.clear();
    out.overflowed = false;

    // Expand Text elements into per-glyph Texture elements
    std::vector<HDrawElement> flat;
    flat.reserve(elements.size() * 4);
    for (const auto& e : elements) {
        if (e.type == HDrawElement::Type::Icon) {
            // 아이콘은 아틀라스 자리 하나짜리 텍스처 사각형입니다(D3). 아틀라스가 가득 차 자리가 없으면 그리지 않습니다.
            HGlyphInfo g{};
            if (!fonts.getIcon(e.text, (int)e.fontSize, g)) continue;
            HDrawElement icon{};
            icon.type    = HDrawElement::Type::Texture;
            icon.texture = fonts.pageView(g.page);
            icon.color   = e.color;
            icon.layer   = e.layer;
            icon.clip    = e.clip;
            icon.clipped = e.clipped;
            icon.rect    = e.rect;
            icon.uvRect  = { g.uvMin.x, g.uvMin.y, g.uvMax.x, g.uvMax.y };
            flat.push_back(icon);
            continue;
        }
        if (e.type != HDrawElement::Type::Text) {
            flat.push_back(e);
            continue;
        }
        float penX  = e.rect.x;
        float baseY = e.rect.y + e.fontSize; // approximate baseline
        // 글자는 UTF-8 코드 포인트로 읽습니다(D4, 한글 등).
        for (size_t ci = 0; ci < e.text.size();) {
            const uint32_t c = HFontAtlas::nextCodepoint(e.text, ci);
            // 개행. 없으면 여러 줄 문자열이 한 줄로 이어붙어 그려집니다
            // (뷰포트 통계 오버레이 4줄이 실제로 그랬습니다).
            if (c == '\n') {
                penX   = e.rect.x;
                baseY += e.fontSize * 1.2f;
                continue;
            }
            auto g = fonts.getGlyph(c, e.fontSize, e.fontFace);
            if (g.pixelSize.x < 1.f || g.pixelSize.y < 1.f) {
                penX += g.advance;
                continue; // space or empty glyph
            }
            HDrawElement glyph{};
            glyph.type    = HDrawElement::Type::Texture;
            glyph.texture = fonts.pageView(g.page);
            glyph.color   = e.color;
            glyph.layer   = e.layer;
            glyph.clip    = e.clip;
            glyph.clipped = e.clipped;
            glyph.rect    = { penX + g.bearing.x,
                              baseY - g.bearing.y,
                              g.pixelSize.x,
                              g.pixelSize.y };
            glyph.uvRect  = { g.uvMin.x, g.uvMin.y, g.uvMax.x, g.uvMax.y };
            flat.push_back(glyph);
            penX += g.advance;
        }
    }

    if (flat.empty()) return false;   // 렌더 패스를 열지 않음(표시 전환은 렌더러가 함, DEF-120)

    uint32_t vertCount  = 0;
    uint32_t batchStart = 0;
    VkImageView curTex  = VK_NULL_HANDLE;

    auto flushBatch = [&]() {
        uint32_t quadCount = (vertCount / 4) - batchStart;
        if (quadCount == 0) return;
        out.batches.push_back({ curTex, batchStart, quadCount });
        batchStart = vertCount / 4;
    };

    for (const auto& e : flat) {
        const bool  isShadow = (e.type == HDrawElement::Type::Shadow);
        VkImageView tex      = (e.texture != VK_NULL_HANDLE) ? e.texture : whiteView;
        glm::vec4   uvRect   = e.uvRect;
        if (e.type == HDrawElement::Type::Rect || isShadow) {
            tex    = whiteView;
            uvRect = { 0.f, 0.f, 1.f, 1.f };
        }
        const glm::vec2 halfSize{ e.rect.z * 0.5f, e.rect.w * 0.5f };
        const float     pad = isShadow ? (std::ceil(std::max(0.f, e.shadowBlur) * 1.5f) + 2.f) : 0.f;
        glm::vec4 rect{
            e.rect.x - pad,
            e.rect.y - pad,
            e.rect.z + 2.f * pad,
            e.rect.w + 2.f * pad
        };
        glm::vec4 localRect{
            -halfSize.x - pad,
            -halfSize.y - pad,
             halfSize.x + pad,
             halfSize.y + pad
        };
        // 클립은 사각형을 CPU 에서 잘라 적용합니다. 요소가 모두 축 정렬 사각형이라 정확하고,
        // 시저처럼 클립이 바뀔 때마다 배치를 끊지 않아도 됩니다(DEF-56).
        // localRect 도 같이 잘라 둥근 모서리 · 그림자가 클립 경계에서 찌그러지지 않게 합니다.
        if (e.clipped && !HClipQuad(rect, uvRect, localRect, e.clip)) continue;

        if (tex != curTex) {
            flushBatch();
            curTex = tex;
        }

        if (vertCount + 4 > maxVerts) {
            out.overflowed = true;
            break;
        }
        const float x = rect.x, y = rect.y, w = rect.z, h = rect.w;
        const float u0 = uvRect.x, v0 = uvRect.y, u1 = uvRect.z, v1 = uvRect.w;
        const glm::vec4 params{ e.cornerRadius, e.borderWidth, e.shadowBlur, isShadow ? 1.f : 0.f };
        // 정점 색: 보통은 하나, 그라데이션(H3b)은 잘린 자리의 모서리 색을 원래 사각형 안 비율로 구합니다(쌍선형).
        glm::vec4 cTL = e.color, cTR = e.color, cBR = e.color, cBL = e.color;
        if (e.gradient && !isShadow) {
            auto at = [&](float px, float py) {
                const float fx = e.rect.z > 0.f ? std::clamp((px - e.rect.x) / e.rect.z, 0.f, 1.f) : 0.f;
                const float fy = e.rect.w > 0.f ? std::clamp((py - e.rect.y) / e.rect.w, 0.f, 1.f) : 0.f;
                const glm::vec4 top = e.corner[0] + (e.corner[1] - e.corner[0]) * fx;
                const glm::vec4 bot = e.corner[3] + (e.corner[2] - e.corner[3]) * fx;
                return top + (bot - top) * fy;
            };
            cTL = at(x, y);
            cTR = at(x + w, y);
            cBR = at(x + w, y + h);
            cBL = at(x, y + h);
        }
        out.vertices.push_back({ { x,     y     }, { u0, v0 }, cTL, { localRect.x, localRect.y }, halfSize, params, e.borderColor });
        out.vertices.push_back({ { x + w, y     }, { u1, v0 }, cTR, { localRect.z, localRect.y }, halfSize, params, e.borderColor });
        out.vertices.push_back({ { x + w, y + h }, { u1, v1 }, cBR, { localRect.z, localRect.w }, halfSize, params, e.borderColor });
        out.vertices.push_back({ { x,     y + h }, { u0, v1 }, cBL, { localRect.x, localRect.w }, halfSize, params, e.borderColor });
        vertCount += 4;
    }
    flushBatch();
    return true;
}

bool HSlateContext::collectElements(glm::vec2 size, HFontAtlas* fonts, std::vector<HDrawElement>& out) {
    out.clear();
    if (!root_ || root_->isCollapsed()) return false;

    HGeometry fullGeo;
    fullGeo.position = { 0.f, 0.f };
    fullGeo.size     = size;
    fullGeo.scale    = 1.f;

    arrange(fullGeo.size);

    HPaintContext ctx(fonts);
    root_->paint(ctx, fullGeo);
    // 팝업 층(H2a): 루트 위, 툴팁 아래. 예전 팝업 호스트는 루트 오버레이 안에 있었고 같은 순서로 그려졌습니다.
    popups_.paint(ctx);

    // 툴팁은 모든 위젯 위(맨 위 레이어)에 그립니다.
    if (isTooltipVisible()) {
        if (auto target = hoverTooltipTarget_.lock()) {
            const std::string tip = target->tooltip.get();
            if (!tip.empty()) {
                int maxLayer = 0;
                for (const auto& el : ctx.elements)
                    if (el.layer > maxLayer) maxLayer = el.layer;

                const float fsz  = tooltipStyle_.fontSize;
                const float tw   = fonts ? fonts->measureText(tip, fsz, 0) : (float)tip.size() * fsz * 0.6f;
                const float th   = fsz * 1.2f;
                const float bw   = tw + tooltipStyle_.padX * 2.f;
                const float bh   = th + tooltipStyle_.padY * 2.f;

                float tx = pointerPos_.x + tooltipStyle_.offset.x;
                float ty = pointerPos_.y + tooltipStyle_.offset.y;
                if (tx + bw > size.x) tx = std::max(0.f, pointerPos_.x - bw - 4.f);
                if (ty + bh > size.y) ty = std::max(0.f, pointerPos_.y - bh - 4.f);
                tx = std::clamp(tx, 0.f, std::max(0.f, size.x - bw));
                ty = std::clamp(ty, 0.f, std::max(0.f, size.y - bh));

                const HGeometry tipGeo{ { std::floor(tx), std::floor(ty) }, { std::ceil(bw), std::ceil(bh) }, 1.f };
                const HGeometry txtGeo = tipGeo.makeChild({ tooltipStyle_.padX, tooltipStyle_.padY }, { tw, th });

                ctx.pushLayer(maxLayer + 10);
                if (tooltipStyle_.shadowBlur > 0.f && tooltipStyle_.shadowColor.a > 0.f) {
                    ctx.drawShadow(tipGeo, tooltipStyle_.cornerRadius,
                                   tooltipStyle_.shadowOffset, tooltipStyle_.shadowBlur,
                                   tooltipStyle_.shadowColor);
                }
                ctx.drawRoundedRect(tipGeo, tooltipStyle_.bg, tooltipStyle_.cornerRadius,
                                    tooltipStyle_.border, tooltipStyle_.borderWidth);
                ctx.drawText(txtGeo, tip, fsz, tooltipStyle_.textColor, 0);
                ctx.popLayer(maxLayer + 10);
            }
        }
    }

    std::stable_sort(ctx.elements.begin(), ctx.elements.end(),
        [](const HDrawElement& a, const HDrawElement& b) {
            return a.layer < b.layer;
        });

    out = std::move(ctx.elements);
    return true;
}

bool HSlateContext::buildDrawList(glm::vec2 size, HFontAtlas& fonts,
                                  VkImageView whiteView, uint32_t maxVerts,
                                  HSlateDrawList& out) {
    std::vector<HDrawElement> elements;
    if (!collectElements(size, &fonts, elements)) return false;
    return buildDrawListFromElements(elements, fonts, whiteView, maxVerts, out);
}

bool HSlateRenderer::dumpElements(HSlateContext& ctx, glm::vec2 size, const std::string& path) {
    std::vector<HDrawElement> elements;
    if (!ctx.collectElements(size, &fonts_, elements)) return false;

    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;

    char buf[512];
    for (const HDrawElement& e : elements) {
        const char* kind = e.type == HDrawElement::Type::Rect    ? "Rect"
                         : e.type == HDrawElement::Type::Texture ? "Texture"
                         : e.type == HDrawElement::Type::Shadow  ? "Shadow"
                         : e.type == HDrawElement::Type::Icon    ? "Icon"
                                                                 : "Text";
        int n = std::snprintf(buf, sizeof(buf),
            "%s rect=(%.2f,%.2f,%.2f,%.2f) color=(%.3f,%.3f,%.3f,%.3f) layer=%d",
            kind, e.rect.x, e.rect.y, e.rect.z, e.rect.w,
            e.color.r, e.color.g, e.color.b, e.color.a, e.layer);
        if (n > 0) f.write(buf, n);
        if (e.clipped) {
            n = std::snprintf(buf, sizeof(buf), " clip=(%.2f,%.2f,%.2f,%.2f)",
                              e.clip.x, e.clip.y, e.clip.z, e.clip.w);
            if (n > 0) f.write(buf, n);
        }
        if (e.cornerRadius > 0.f) {
            n = std::snprintf(buf, sizeof(buf), " radius=%.2f", e.cornerRadius);
            if (n > 0) f.write(buf, n);
        }
        if (e.borderWidth > 0.f) {
            n = std::snprintf(buf, sizeof(buf), " border=(%.2f,%.3f,%.3f,%.3f,%.3f)",
                              e.borderWidth, e.borderColor.r, e.borderColor.g, e.borderColor.b, e.borderColor.a);
            if (n > 0) f.write(buf, n);
        }
        if (e.shadowBlur > 0.f) {
            n = std::snprintf(buf, sizeof(buf), " blur=%.2f", e.shadowBlur);
            if (n > 0) f.write(buf, n);
        }
        if (e.gradient) {   // H3b: 그라데이션 사각형만 붙습니다(예전 덤프 줄은 그대로)
            n = std::snprintf(buf, sizeof(buf), " grad=(%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f)",
                              e.corner[0].r, e.corner[0].g, e.corner[0].b, e.corner[1].r, e.corner[1].g, e.corner[1].b,
                              e.corner[2].r, e.corner[2].g, e.corner[2].b, e.corner[3].r, e.corner[3].g, e.corner[3].b);
            if (n > 0) f.write(buf, n);
        }
        if (e.type == HDrawElement::Type::Texture) {
            n = std::snprintf(buf, sizeof(buf), " tex uv=(%.3f,%.3f,%.3f,%.3f)",
                              e.uvRect.x, e.uvRect.y, e.uvRect.z, e.uvRect.w);
            if (n > 0) f.write(buf, n);
        } else if (e.type == HDrawElement::Type::Icon) {
            f << " icon=\"" << e.text << '"';
        } else if (e.type == HDrawElement::Type::Text) {
            n = std::snprintf(buf, sizeof(buf), "%s size=%.2f text=\"", e.fontFace ? " bold" : "", e.fontSize);
            if (n > 0) f.write(buf, n);
            for (unsigned char c : e.text) {
                if (c == '\\') f << "\\\\";
                else if (c == '"') f << "\\\"";
                else if (c == '\n') f << "\\n";
                else if (c == '\r') f << "\\r";
                else f << (char)c;
            }
            f << '"';
        }
        f << '\n';
    }
    return f.good();
}

// ── recordDrawList ─────────────────────────────────────────────────────────────
bool HSlateRenderer::recordDrawList(VkCommandBuffer cmd, VkRenderPass renderPass,
                                    VkFramebuffer framebuffer, VkExtent2D extent,
                                    const HSlateDrawList& drawList, uint32_t frameIdx) {
    return recordPass(cmd, renderPass, pipeline_, framebuffer, extent, nullptr, drawList,
                      frameIdx % kFramesInFlight);
}

bool HSlateRenderer::buildPanel(HSlateContext& ctx, VkExtent2D extent, HSlateDrawList& out) {
    const glm::vec2 size{ (float)extent.width, (float)extent.height };
    const bool drew = ctx.buildDrawList(size, fonts_, whiteView_, kMaxVerts, out);
    if (fonts_.isDirty()) fonts_.upload();   // 글리프 확장 뒤(record 와 같은 이유)
    if (!drew) out = HSlateDrawList{};
    return drew;
}

bool HSlateRenderer::recordPanel(VkCommandBuffer cmd, const HSlateDrawList& list, VkFramebuffer framebuffer,
                                 VkExtent2D extent, uint32_t frameIdx) {
    const uint32_t slot = frameIdx % kFramesInFlight;
    bool drew = !list.batches.empty();
    static constexpr VkClearColorValue kTransparent = {{ 0.f, 0.f, 0.f, 0.f }};
    if (drew && !fitsSlot(slotVertOffset_[slot], (uint32_t)list.vertices.size(), slotVerts_)) {
        static bool panelBudgetWarned = false;
        if (!panelBudgetWarned) {
            panelBudgetWarned = true;
            HLOG(LogUI, Warning) << "UI slot vertex budget exceeded by a world panel (" << slotVertOffset_[slot]
                                 << " + " << list.vertices.size() << " > " << slotVerts_
                                 << ") - panel cleared this frame";
        }
        // 비운 목록으로 패스만 열어 투명하게 지웁니다. false 를 돌려 다음 프레임에 다시 그리게 합니다.
        recordPass(cmd, panelRenderPass_, panelPipeline_, framebuffer, extent, &kTransparent, HSlateDrawList{}, slot);
        return false;
    }
    recordPass(cmd, panelRenderPass_, panelPipeline_, framebuffer, extent, &kTransparent, list, slot);
    return true;
}

// ── 보조 OS 창 (다중 OS 창 M2) ──────────────────────────────────────────────────
bool HSlateRenderer::buildWindow(HSlateContext& ctx, VkExtent2D extent, HSlateDrawList& out) {
    const glm::vec2 size{ (float)extent.width, (float)extent.height };
    const bool drew = ctx.buildDrawList(size, fonts_, whiteView_, kMaxVerts, out);
    if (!drew) out = HSlateDrawList{};
    return drew;
}

bool HSlateRenderer::recordWindow(VkCommandBuffer cmd, const HSlateDrawList& list, VkFramebuffer framebuffer,
                                  VkExtent2D extent, uint32_t frameIdx) {
    if (windowRenderPass_ == VK_NULL_HANDLE || framebuffer == VK_NULL_HANDLE) return false;
    const uint32_t slot = frameIdx % kFramesInFlight;
    if (!list.batches.empty() && !fitsSlot(slotVertOffset_[slot], (uint32_t)list.vertices.size(), slotVerts_)) {
        static bool windowBudgetWarned = false;
        if (!windowBudgetWarned) {
            windowBudgetWarned = true;
            HLOG(LogUI, Warning) << "UI slot vertex budget exceeded by an aux window (" << slotVertOffset_[slot]
                                 << " + " << list.vertices.size() << " > " << slotVerts_
                                 << ") - window cleared this frame";
        }
        // 받은 이미지는 표시해야 하므로 패스를 열어 지우고 PRESENT_SRC 로 끝냅니다.
        recordPass(cmd, windowRenderPass_, pipeline_, framebuffer, extent, &kWindowClear, HSlateDrawList{}, slot);
        return false;
    }
    recordPass(cmd, windowRenderPass_, pipeline_, framebuffer, extent, &kWindowClear, list, slot);
    return true;
}

void HSlateRenderer::notePeak(uint32_t slotOffset, uint32_t listVerts) {
    peakSlotVerts_ = std::max(peakSlotVerts_, slotOffset);
    peakListVerts_ = std::max(peakListVerts_, listVerts);
}

bool HSlateRenderer::recordPass(VkCommandBuffer cmd, VkRenderPass renderPass, VkPipeline pipeline,
                                VkFramebuffer framebuffer, VkExtent2D extent, const VkClearColorValue* clear,
                                const HSlateDrawList& drawList, uint32_t slot) {
    const uint32_t count = static_cast<uint32_t>(drawList.vertices.size());
    if (!fitsSlot(slotVertOffset_[slot], count, slotVerts_)) {
        static bool budgetWarned = false;
        if (!budgetWarned) {
            budgetWarned = true;
            HLOG(LogUI, Warning) << "UI slot vertex budget exceeded (" << slotVertOffset_[slot]
                                 << " + " << count << " > " << slotVerts_
                                 << ") - skipping render pass";
        }
        return false;
    }

    const uint32_t baseVert = advanceSlotVertexOffset(slotVertOffset_[slot], count);
    notePeak(slotVertOffset_[slot], count);
    if (count > 0) {
        std::memcpy(vertexPtr_[slot] + baseVert, drawList.vertices.data(),
                    count * sizeof(UIVertex));
    }

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = renderPass;
    rpbi.framebuffer       = framebuffer;
    rpbi.renderArea.extent = extent;
    // 에디터 오버레이는 지우기 값이 없습니다(LOAD_OP_LOAD 로 3D 장면을 보존). 패널은 투명, 보조 창은 kWindowClear 로 지웁니다.
    VkClearValue clearValue{};
    if (clear) { clearValue.color = *clear; rpbi.clearValueCount = 1; rpbi.pClearValues = &clearValue; }
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    if (drawList.batches.empty()) {   // 지우기만(패널이 비었거나 예산 초과)
        vkCmdEndRenderPass(cmd);
        return true;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport vp{ 0.f, 0.f, (float)extent.width, (float)extent.height, 0.f, 1.f };
    VkRect2D   sc{ {0, 0}, extent };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor (cmd, 0, 1, &sc);

    glm::vec2 screenSize{ (float)extent.width, (float)extent.height };
    vkCmdPushConstants(cmd, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2), &screenSize);

    VkDeviceSize vbOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuf_[slot], &vbOffset);
    vkCmdBindIndexBuffer  (cmd, indexBuf_, 0, VK_INDEX_TYPE_UINT32);

    const uint32_t baseQuad = baseVert / 4;
    for (const auto& b : drawList.batches) {
        pushDescriptor(cmd, b.texture);
        // vertexOffset shifts pre-generated indices so quad 0 maps to (baseQuad + batch.firstQuad)*4
        vkCmdDrawIndexed(cmd, b.quadCount * 6, 1, 0,
                         static_cast<int32_t>((baseQuad + b.firstQuad) * 4), 0);
    }

    if (drawList.overflowed) {
        // 예산을 넘으면 나머지가 조용히 사라집니다. 화면만 보고는 원인을 알 수 없으므로
        // 한 번은 알립니다(매 프레임 찍으면 로그 패널이 잠깁니다).
        static bool warned = false;
        if (!warned) {
            warned = true;
            HLOG(LogUI, Warning) << "UI vertex budget exceeded (" << kMaxVerts
                                 << ") - remaining draw elements dropped this frame";
        }
    }

    vkCmdEndRenderPass(cmd);
    return true;
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

    pushDescriptorFn_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipelineLayout_, 0, 1, &write);
}

// ── arrange ────────────────────────────────────────────────────────────────────
namespace {
// 이벤트 전달 중 표시(H1b). 이 안에서는 flushDeferred 가 아무것도 하지 않습니다.
struct DispatchScope {
    int& depth;
    explicit DispatchScope(int& d) : depth(d) { ++depth; }
    ~DispatchScope() { --depth; }
};

// 건너뛰기를 판정할 때, 배치가 원하는 크기를 물을 위젯을 모두 잽니다. 배치는 forEachChild 가 넘기는 자식만
// 배치하므로(접힌 섹션의 body 는 빠짐) 같은 범위입니다. 이미 이번 패스에 잰 위젯은 캐시를 돌려줍니다.
void measureSubtree(const std::shared_ptr<HWidget>& w) {
    if (!w) return;
    w->desiredSize();
    w->forEachChild([](const std::shared_ptr<HWidget>& c) { measureSubtree(c); });
}

// 측정 패스 하나의 범위. 중첩된 arrange(다른 문맥)가 있어도 바깥 패스 값을 되돌립니다.
struct MeasurePassScope {
    hslate::LayoutCounters& lc;
    uint64_t savedPass, savedPrev;
    bool     savedChanged;
    MeasurePassScope(hslate::LayoutCounters& c, uint64_t prevPass)
        : lc(c), savedPass(c.pass), savedPrev(c.prevPass), savedChanged(c.changed) {
        lc.pass     = ++lc.nextPass;
        lc.prevPass = prevPass;
        lc.changed  = false;
    }
    ~MeasurePassScope() { lc.pass = savedPass; lc.prevPass = savedPrev; lc.changed = savedChanged; }
};
} // namespace

void HSlateContext::arrange(glm::vec2 size) {
    // 이벤트 전달 중에 맡긴 지연 작업은 배치 전에 실행합니다. 전달 밖(tick 등)에서 맡긴 것은 소유자의
    // flushDeferred 자리를 기다립니다(그 자리가 예전 지연 플래그의 소진 자리라 순서가 그대로임).
    if (deferredFromDispatch_) flushDeferred();
    if (dispatchDepth_ == 0) popups_.collect();
    popups_.setBounds({ { 0.f, 0.f }, size });
    ++layoutStats_.arrangeCalls;
    struct ArrangeTimer {
        double& ms;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~ArrangeTimer() { ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
    } timer{ layoutStats_.arrangeMs };
    if (!root_ || root_->isCollapsed()) { layoutValid_ = false; return; }
    HGeometry fullGeo;
    fullGeo.size = size;

    hslate::LayoutCounters& lc = hslate::layoutCounters();
    if (!lc.cacheEnabled) {
        // 예전 방식(계측 · 비교용): 캐시 없이 재고 늘 배치합니다.
        layoutValid_ = false;
        root_->desiredSize();
        root_->onArrangeChildren(fullGeo);
        popups_.arrange();
        ++layoutStats_.arrangeRuns;
        ++layoutGeneration_;
        return;
    }

    // 측정 전의 변경 횟수를 기억합니다. 측정 · 배치 도중에 변경이 생기면 다음 arrange 가 다시 배치합니다.
    const uint64_t mutation = lc.mutation;
    MeasurePassScope scope(lc, lastPass_);
    root_->desiredSize();   // 예전에도 배치 전에 루트를 한 번 쟀습니다(같은 순서로 잼)
    bool skip = layoutValid_ && size == lastLayoutSize_ && root_.get() == lastLayoutRoot_ &&
                mutation == lastMutation_;
    if (skip) {
        measureSubtree(root_);
        skip = !lc.changed;
    }
    if (!skip) {
        root_->onArrangeChildren(fullGeo);
        ++layoutStats_.arrangeRuns;
        ++layoutGeneration_;
    }
    // 팝업은 루트 배치를 건너뛰어도 늘 배치합니다(작고, 넣을 때 정한 자리라 루트와 무관).
    popups_.arrange();
    lastPass_       = lc.pass;
    lastLayoutSize_ = size;
    lastLayoutRoot_ = root_.get();
    lastMutation_   = mutation;
    layoutValid_    = true;
}

// ── 지연 변경 큐 (H1b) ────────────────────────────────────────────────────────
void HSlateContext::deferred(std::function<void()> fn) {
    if (!fn) return;
    deferred_.push_back(std::move(fn));
    if (dispatchDepth_ > 0) deferredFromDispatch_ = true;
}

void HSlateContext::flushDeferred() {
    if (dispatchDepth_ > 0) return;
    if (deferred_.empty()) { popups_.collect(); return; }
    std::vector<std::function<void()>> run;
    run.swap(deferred_);
    deferredFromDispatch_ = false;
    for (auto& fn : run) fn();
    popups_.collect();
}

// ── tickTooltip ───────────────────────────────────────────────────────────────
void HSlateContext::tickTooltip(float dt) {
    if (!root_ || !root_->isVisible() || !pointerValid_) {
        hoverTooltipTarget_.reset();
        hoverElapsed_      = 0.f;
        tooltipSuppressed_ = false;
        return;
    }
    // 팝업이 덮은 자리(영역 안 · Consume 배경)에서는 그 밑 루트 위젯의 툴팁을 띄우지 않습니다(H2b, H2a 판단 4).
    auto target = popups_.findTooltip(pointerPos_);
    if (!target && !popups_.overPopup(pointerPos_)) target = root_->findTooltipWidget(pointerPos_);
    auto prev   = hoverTooltipTarget_.lock();
    if (target != prev) {
        hoverTooltipTarget_ = target;
        hoverElapsed_       = 0.f;
        tooltipSuppressed_  = false;
    }
    if (target && !tooltipSuppressed_) {
        hoverElapsed_ += std::max(0.f, dt);
    }
}

bool HSlateContext::isTooltipVisible() const {
    if (tooltipSuppressed_ || hoverElapsed_ < tooltipStyle_.delaySec) return false;
    auto target = hoverTooltipTarget_.lock();
    return target && target->isVisible() && !target->tooltip.get().empty();
}

// ── handlePointerEvent ─────────────────────────────────────────────────────────
// 마우스 전달 자체(캡처 · 컨테이너가 자식에게 뿌리는 방식)는 routePointerEvent 가 예전 그대로
// 합니다. 여기서는 누름이 끝난 뒤 포커스 요청 · "누르면 포커스" 만 덧붙입니다(H1a-1).
HReply HSlateContext::handlePointerEvent(HPointerEvent e) {
    // 누름 · 뗌 · 휠은 위젯 상태(펼침 · 스크롤 · 리빌드 요청)를 바꾸는 일이 많으므로 다음 arrange 가 늘 배치합니다
    // (H1b, 보수적). 이동은 호버만 바꾸는 것이 보통이라 무효화하지 않고, 배치에 드러나는 변경은 원하는 크기 비교와
    // 배치 변경 알림이 잡습니다.
    if (e.wheelDelta != 0.f || e.button >= 0) layoutValid_ = false;
    HReply r;
    {
        DispatchScope ds(dispatchDepth_);
        r = routePointerEvent(e);
    }
    if (dispatchDepth_ == 0) popups_.collect();   // 전달 중 닫힌 팝업의 위젯을 이제 놓음
    const bool isPress = (e.wheelDelta == 0.f) && e.button >= 0 && e.down;
    if (!isPress) return r;
    if (r.hasFocusRequest()) {
        applyFocusRequest(r);
    } else if (clickToFocus_ && root_ && root_->isVisible()) {
        const HWidgetPath path = hitTest(e.position);
        for (size_t i = path.size(); i-- > 0;) {
            auto w = path.widgetAt(i);
            if (w && w->supportsKeyboardFocus()) { setFocus(w); break; }
        }
    }
    return r;
}

HReply HSlateContext::routePointerEvent(const HPointerEvent& e) {
    if (!root_ || !root_->isVisible()) return HReply::Unhandled();

    // 버튼 상태를 싣는 것은 이동 이벤트뿐입니다(e.down = 좌버튼이 지금 눌려 있음). 휠
    // 이벤트의 e.down 은 늘 거짓이라 "버튼이 떨어졌다"로 읽으면 안 됩니다.
    const bool isWheel  = (e.wheelDelta != 0.f);
    const bool isButton = !isWheel && e.button >= 0;
    const bool isMove   = !isWheel && e.button < 0;

    pointerPos_   = e.position;
    pointerValid_ = true;
    if ((isButton && e.down) || isWheel) {
        hoverElapsed_      = 0.f;
        tooltipSuppressed_ = true;
    } else if (isMove) {
        auto target = popups_.findTooltip(pointerPos_);
        if (!target && !popups_.overPopup(pointerPos_)) target = root_->findTooltipWidget(pointerPos_);   // H2b: 팝업 밑 툴팁 막기
        auto prev   = hoverTooltipTarget_.lock();
        if (target != prev) {
            hoverTooltipTarget_ = target;
            hoverElapsed_       = 0.f;
            tooltipSuppressed_  = false;
        }
    }

    if (captureActive_) {
        if (auto cap = captured_.lock()) {
            HReply r;
            if (isWheel)       r = cap->onScroll(e);   // 예전에는 onMouseMove 로 갔습니다(DEF-57 일부)
            else if (isButton) r = cap->onMouseButton(e);
            else               r = cap->onMouseMove(e);
            // 캡처는 버튼보다 오래 살지 않습니다. 뗌이 다른 처리에 소비되어 오지 않았더라도,
            // 버튼이 떨어졌다고 실린 이동 이벤트를 위젯에 전한 뒤 풉니다.
            if (r.mouseRelease || (!isWheel && !e.down)) releaseCapture();
            return r;
        }
        // 캡처한 위젯이 그사이 파괴됐습니다. 이 누름의 뗌은 주인이 없으므로 아무 데도 보내지
        // 않습니다. 보내면 누른 적 없는 위젯이 뗌만 받습니다(DEF-77 이 그 경로였습니다).
        if (isButton) {
            if (!e.down) releaseCapture();
            return HReply::Handled();
        }
        // 뗌을 받지 못한 채 버튼이 떨어졌으면 여기서 정리합니다. 이동과 휠은 아래 보통
        // 경로로 보내 호버가 갱신되게 합니다.
        if (isMove && !e.down) releaseCapture();
    }

    // 팝업 층이 먼저 받습니다(H2a). 누름 · 휠은 팝업 영역 · Consume 배경이면 루트로 가지 않고, 이동은 둘 다 받습니다.
    HReply r;
    bool   consumed = false;
    if (isWheel)       consumed = popups_.routeWheel(e, r);
    else if (isButton) consumed = popups_.routeButton(e, r);
    else               r = popups_.routeMove(e);
    if (!consumed) {
        if (isWheel)       r = root_->onScroll(e);
        else if (isButton) r = root_->onMouseButton(e);
        else               r.merge(root_->onMouseMove(e));
    }

    if (r.mouseCapture) {
        captured_      = r.mouseCapture->weak_from_this();
        captureActive_ = true;
    }
    if (r.mouseRelease) releaseCapture();
    return r;
}

// ── 히트 경로 · 포커스 (H1a-1) ────────────────────────────────────────────────
namespace hslate {

namespace {
bool hitTestRec(const std::shared_ptr<HWidget>& w, glm::vec2 pos, HWidgetPath& out) {
    if (!w || !w->isVisible()) return false;
    const HGeometry& geo = w->cachedGeometry();
    const bool inside = geo.contains(pos);
    if (w->clipsChildren() && !inside) return false;
    out.entries.push_back({ w, geo });
    std::vector<std::shared_ptr<HWidget>> kids;
    w->forEachChild([&kids](const std::shared_ptr<HWidget>& c) { kids.push_back(c); });
    for (size_t i = kids.size(); i-- > 0;)          // 위에 그려진 자식(나중 것)부터
        if (hitTestRec(kids[i], pos, out)) return true;
    if (inside && w->hitTestVisible()) return true;
    out.entries.pop_back();
    return false;
}

bool findPathRec(const std::shared_ptr<HWidget>& w, const HWidget* target, HWidgetPath& out) {
    if (!w) return false;
    out.entries.push_back({ w, w->cachedGeometry() });
    if (w.get() == target) return true;
    bool found = false;
    w->forEachChild([&](const std::shared_ptr<HWidget>& c) {
        if (!found) found = findPathRec(c, target, out);
    });
    if (!found) out.entries.pop_back();
    return found;
}

void collectFocusableRec(const std::shared_ptr<HWidget>& w, std::vector<std::shared_ptr<HWidget>>& out) {
    if (!w || !w->isVisible()) return;
    if (w->supportsKeyboardFocus()) out.push_back(w);
    w->forEachChild([&out](const std::shared_ptr<HWidget>& c) { collectFocusableRec(c, out); });
}
} // namespace

bool hitTest(const std::shared_ptr<HWidget>& root, glm::vec2 pos, HWidgetPath& out) {
    out.entries.clear();
    return hitTestRec(root, pos, out);
}

bool findPath(const std::shared_ptr<HWidget>& root, const HWidget* target, HWidgetPath& out) {
    out.entries.clear();
    if (!target) return false;
    return findPathRec(root, target, out);
}

void collectFocusable(const std::shared_ptr<HWidget>& root, std::vector<std::shared_ptr<HWidget>>& out) {
    out.clear();
    collectFocusableRec(root, out);
}

} // namespace hslate

HWidgetPath HSlateContext::hitTest(glm::vec2 pos) const {
    HWidgetPath path;
    if (root_ && root_->isVisible() && popups_.hitTest(pos, path)) return path;
    hslate::hitTest(root_, pos, path);
    return path;
}

void HSlateContext::applyFocusRequest(const HReply& r) {
    if (r.focusClear) { clearFocus(); return; }
    if (r.focusRequest) setFocus(r.focusRequest->weak_from_this().lock());
}

bool HSlateContext::validFocusPath(HWidgetPath& out) {
    out.entries.clear();
    auto f = focused_.lock();
    if (!f) { focused_.reset(); return false; }
    bool ok = root_ && (hslate::findPath(root_, f.get(), out) || popups_.findPath(f.get(), out));
    for (size_t i = 0; ok && i < out.size(); ++i) {
        auto w = out.widgetAt(i);
        ok = w && w->isVisible();
    }
    if (!ok) { out.entries.clear(); clearFocus(); }
    return ok;
}

HReply HSlateContext::handleKeyEvent(const HKeyEvent& e) {
    layoutValid_ = false;
    DispatchScope ds(dispatchDepth_);
    HWidgetPath path;
    if (!validFocusPath(path)) return HReply::Unhandled();
    for (size_t i = path.size(); i-- > 0;) {        // 포커스 위젯 → 루트
        auto w = path.widgetAt(i);
        if (!w) continue;
        HReply r = e.down ? w->onKeyDown(e) : w->onKeyUp(e);
        if (r.handled) {
            if (r.hasFocusRequest()) applyFocusRequest(r);
            return r;
        }
    }
    return HReply::Unhandled();
}

HReply HSlateContext::handleCharEvent(const HCharEvent& e) {
    layoutValid_ = false;
    DispatchScope ds(dispatchDepth_);
    HWidgetPath path;
    if (!validFocusPath(path)) return HReply::Unhandled();
    auto w = path.lastWidget();
    if (!w) return HReply::Unhandled();
    HReply r = w->onChar(e);
    if (r.hasFocusRequest()) applyFocusRequest(r);
    return r;
}
