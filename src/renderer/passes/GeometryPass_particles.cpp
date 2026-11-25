#include "GeometryPass.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>

// ── 빌보드 파티클 (subpass 2, 2026-11-20, Docs/Particles_And_Decals.md D7) ─────────────────────────────────
// 인스턴스 정점 입력 하나(vfx::ParticleInstance, 48 B), 정점 번호 6개로 사각형을 만듭니다. 깊이 시험만 하고 쓰지 않으며, 가산 ·
// 알파 두 섞기를 만듭니다. HDR 알파(후처리 가중치)는 (ZERO, ONE) 으로 그대로 둡니다. 색은 알파를 곱한 값(particle.frag)입니다.
void GeometryPass::createParticlePipelines(VkRenderPass rp, PipelineSet& ps) {
    if (!particleLayout_) {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = 0;
        b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dl{};
        dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dl.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dl.bindingCount = 1;
        dl.pBindings    = &b;
        if (vkCreateDescriptorSetLayout(rc_->device, &dl, nullptr, &particleSetLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create particle set layout!");
        const VkDescriptorSetLayout sets[2] = { frameSetLayout_, particleSetLayout_ };
        VkPushConstantRange range{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GpuParticlePush) };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 2; pli.pSetLayouts = sets;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &range;
        if (vkCreatePipelineLayout(rc_->device, &pli, nullptr, &particleLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create particle pipeline layout!");
    }

    auto vert = rc_->createShaderModule(readFile("shaders/particle.vert.spv"));
    auto frag = rc_->createShaderModule(readFile("shaders/particle.frag.spv"));
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

    VkVertexInputBindingDescription bind{ 0, (uint32_t)sizeof(vfx::ParticleInstance), VK_VERTEX_INPUT_RATE_INSTANCE };
    VkVertexInputAttributeDescription attrs[3] = {
        { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(vfx::ParticleInstance, posSize) },
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(vfx::ParticleInstance, color) },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(vfx::ParticleInstance, params) },
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1; vi.pVertexBindingDescriptions   = &bind;
    vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = attrs;
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
    rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = rc_->msaaSamples;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp  = VK_COMPARE_OP_GREATER_OR_EQUAL;   // Reverse-Z(C1)

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;   // 후처리 가중치는 그대로
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkGraphicsPipelineCreateInfo ci{};

    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2; ci.pStages = stages;
    ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vs; ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms; ci.pColorBlendState = &cb;
    ci.pDepthStencilState = &ds; ci.pDynamicState = &dyn;
    ci.layout = particleLayout_; ci.renderPass = rp;
    ci.subpass = (rc_->renderTier == RenderTier::Mobile) ? 0u : 1u;

    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;                       // 가산
    VkResult r = vkCreateGraphicsPipelines(rc_->device, rc_->pipelineCache, 1, &ci, nullptr, &ps.particleAdd);
    if (r == VK_SUCCESS) {
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;   // 알파(알파를 곱한 색)
        r = vkCreateGraphicsPipelines(rc_->device, rc_->pipelineCache, 1, &ci, nullptr, &ps.particleAlpha);
    }
    vkDestroyShaderModule(rc_->device, vert, nullptr);
    vkDestroyShaderModule(rc_->device, frag, nullptr);
    if (r != VK_SUCCESS) throw std::runtime_error("Failed to create particle pipeline!");
}

void GeometryPass::recordParticles(VkCommandBuffer cmd, uint32_t frameIdx, uint32_t viewIndex, const PipelineSet& ps,
                                   const Inputs& in, const VkViewport& vp, const VkRect2D& sc) {
    const ParticleList& pl = *in.particles;
    if (!pl.data || !pl.count || !pl.ranges || !pl.atlas || !pl.sampler || !ps.particleAdd || !ps.particleAlpha) return;
    if (!particleBuf_[frameIdx]) {
        // 파티클을 처음 그릴 때 만듭니다(파티클이 없는 게임은 버퍼를 잡지 않음).
        for (int i = 0; i < MAX_FRAMES; ++i)
            if (!particleBuf_[i])
                createHostVertexBuffer((VkDeviceSize)kMaxParticleInstances * sizeof(vfx::ParticleInstance), "particle",
                                       particleBuf_[i], particleMem_[i], particlePtr_[i]);
    }
    const uint32_t count = std::min(pl.count, kMaxParticleInstances);
    // 첫 눈을 기록할 때 한 번 복사합니다(두 눈 · 멀티뷰가 같은 슬롯 버퍼를 읽음, 라인과 같은 방식).
    if (viewIndex == 0) std::memcpy(particlePtr_[frameIdx], pl.data, (size_t)count * sizeof(vfx::ParticleInstance));

    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor (cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particleLayout_, 0, 1, &in.frameSet, 0, nullptr);
    VkDescriptorImageInfo img{ pl.sampler, pl.atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &img;
    rc_->vk.push(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particleLayout_, 1, 1, &w);
    const GpuParticlePush push{ (int32_t)viewIndex };
    vkCmdPushConstants(cmd, particleLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &particleBuf_[frameIdx], &off);
    VkPipeline bound = VK_NULL_HANDLE;
    for (const ParticleRange& r : *pl.ranges) {
        if (r.first >= count) break;
        const uint32_t n = std::min(r.count, count - r.first);
        const VkPipeline want = r.additive ? ps.particleAdd : ps.particleAlpha;
        if (want != bound) { vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, want); bound = want; }
        vkCmdDraw(cmd, 6, n, 0, r.first);
    }
}

void GeometryPass::destroyParticleResources() {
    if (particleLayout_)    { vkDestroyPipelineLayout(rc_->device, particleLayout_, nullptr);         particleLayout_    = VK_NULL_HANDLE; }
    if (particleSetLayout_) { vkDestroyDescriptorSetLayout(rc_->device, particleSetLayout_, nullptr); particleSetLayout_ = VK_NULL_HANDLE; }
    for (int i = 0; i < MAX_FRAMES; ++i) {
        if (particlePtr_[i]) { vkUnmapMemory(rc_->device, particleMem_[i]); particlePtr_[i] = nullptr; }
        if (particleBuf_[i]) { vkDestroyBuffer(rc_->device, particleBuf_[i], nullptr); particleBuf_[i] = VK_NULL_HANDLE; }
        if (particleMem_[i]) { vkFreeMemory(rc_->device, particleMem_[i], nullptr);    particleMem_[i] = VK_NULL_HANDLE; }
    }
}
