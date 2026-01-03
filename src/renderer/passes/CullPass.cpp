#include "CullPass.hpp"
#include <cstring>
#include <stdexcept>
#include <cassert>
#include <algorithm>

void CullPass::create(RenderContext& rc) {
    rc_ = &rc;

    // ── Zone C 버퍼 ───────────────────────────────────────────────────────────
    auto mkBuf = [&](VkBuffer& buf, HarukaAllocation& alloc,
                     VkDeviceSize sz, VkBufferUsageFlags usage) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = sz; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(rc.device, &bi, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error("Failed to create cull buffer!");
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(rc.device, buf, &reqs);
        alloc = rc.allocator->allocC(reqs.size, reqs.alignment);
        if (!alloc.isValid()) throw std::runtime_error("Zone C: cull buffer alloc failed!");
        if (rc.allocator->bind(buf, alloc) != VK_SUCCESS)
            throw std::runtime_error("Failed to bind cull buffer!");
    };

    const VkDeviceSize sphereSz  = kMaxDraws * sizeof(GpuBoundSphere);
    const VkDeviceSize drawCmdSz = kMaxDraws * sizeof(GpuDrawCmd);
    const VkDeviceSize countSz   = sizeof(uint32_t);

    for (int i = 0; i < MAX_FRAMES; i++) {
        mkBuf(sphereBufs_[i],      sphereAllocs_[i],      sphereSz,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        mkBuf(inDrawBufs_[i],      inDrawAllocs_[i],      drawCmdSz,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        mkBuf(opaqueDrawBufs_[i],  opaqueDrawAllocs_[i],  drawCmdSz,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        mkBuf(opaqueCountBufs_[i], opaqueCountAllocs_[i], countSz,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
              VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    // ── 디스크립터: 4 storage buffers ────────────────────────────────────────
    {
        VkDescriptorSetLayoutBinding bindings[4] = {};
        for (int i = 0; i < 4; i++) {
            bindings[i].binding = (uint32_t)i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 4; dslci.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(rc.device, &dslci, nullptr, &setLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create cull desc layout!");
    }
    {
        // Push constant: mat4 viewProj + uint inputCount
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = 68;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &setLayout_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        if (vkCreatePipelineLayout(rc.device, &pli, nullptr, &pipeLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create cull pipeline layout!");
    }
    {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps.descriptorCount = MAX_FRAMES * 4;
        VkDescriptorPoolCreateInfo poolCI{};
        poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCI.poolSizeCount = 1; poolCI.pPoolSizes = &ps; poolCI.maxSets = MAX_FRAMES;
        if (vkCreateDescriptorPool(rc.device, &poolCI, nullptr, &descPool_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create cull descriptor pool!");

        VkDescriptorSetLayout layouts[MAX_FRAMES] = { setLayout_, setLayout_ };
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descPool_;
        ai.descriptorSetCount = MAX_FRAMES; ai.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(rc.device, &ai, descSets_) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate cull descriptor sets!");
    }

    // 바인딩 연결 (sphere, inDraw, opaqueOut, count)
    for (int i = 0; i < MAX_FRAMES; i++) {
        VkDescriptorBufferInfo sphereBI{ sphereBufs_[i],      0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo inBI    { inDrawBufs_[i],      0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo outBI   { opaqueDrawBufs_[i],  0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo cntBI   { opaqueCountBufs_[i], 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet ws[4] = {};
        for (int j = 0; j < 4; j++) {
            ws[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ws[j].dstSet = descSets_[i]; ws[j].dstBinding = (uint32_t)j;
            ws[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ws[j].descriptorCount = 1;
        }
        ws[0].pBufferInfo = &sphereBI;
        ws[1].pBufferInfo = &inBI;
        ws[2].pBufferInfo = &outBI;
        ws[3].pBufferInfo = &cntBI;
        vkUpdateDescriptorSets(rc.device, 4, ws, 0, nullptr);
    }
}

void CullPass::rebuildPipelines() {
    if (pipeline_) { vkDestroyPipeline(rc_->device, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }

    auto comp = rc_->createShaderModule(readFile("shaders/cull.comp.spv"));
    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = comp; ci.stage.pName = "main";
    ci.layout = pipeLayout_;
    if (vkCreateComputePipelines(rc_->device, rc_->pipelineCache, 1, &ci, nullptr, &pipeline_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create cull compute pipeline!");
    vkDestroyShaderModule(rc_->device, comp, nullptr);
}

void CullPass::destroy() {
    if (!rc_) return;
    if (pipeline_)   { vkDestroyPipeline(rc_->device, pipeline_, nullptr);             pipeline_   = VK_NULL_HANDLE; }
    if (pipeLayout_) { vkDestroyPipelineLayout(rc_->device, pipeLayout_, nullptr);     pipeLayout_ = VK_NULL_HANDLE; }
    if (setLayout_)  { vkDestroyDescriptorSetLayout(rc_->device, setLayout_, nullptr); setLayout_  = VK_NULL_HANDLE; }
    if (descPool_)   { vkDestroyDescriptorPool(rc_->device, descPool_, nullptr);       descPool_   = VK_NULL_HANDLE; }
    // Zone C는 arena — 개별 free 없이 버퍼 핸들만 파괴 (allocator.destroy()가 일괄 해제)
    for (int i = 0; i < MAX_FRAMES; i++) {
        auto freeBuf = [&](VkBuffer& b) {
            if (b) { vkDestroyBuffer(rc_->device, b, nullptr); b = VK_NULL_HANDLE; }
        };
        freeBuf(sphereBufs_[i]);
        freeBuf(inDrawBufs_[i]);
        freeBuf(opaqueDrawBufs_[i]);
        freeBuf(opaqueCountBufs_[i]);
    }
}

void CullPass::uploadInput(uint32_t fi, const std::vector<GpuDrawCmd>& opaque,
                           const std::vector<GpuBoundSphere>& spheres) {
    if (opaque.empty()) return;
    // 두 배열이 나란히 만들어졌다는 전제가 깨지면 컬링이 남의 스피어로 판정한다.
    assert(opaque.size() == spheres.size() &&
           "cull 입력의 draws와 spheres 크기가 다릅니다 (인덱스 규칙 불일치)");
    const size_t n = std::min(opaque.size(), spheres.size());
    memcpy(inDrawAllocs_[fi].mappedPtr, opaque.data(), n * sizeof(GpuDrawCmd));
    memcpy(sphereAllocs_[fi].mappedPtr, spheres.data(), n * sizeof(GpuBoundSphere));
}

// ── record ─────────────────────────────────────────────────────────────────────
void CullPass::record(VkCommandBuffer cmd, uint32_t fi,
                      const glm::mat4& viewProj, uint32_t count) {
    if (!count) return;

    // Reset atomic count
    vkCmdFillBuffer(cmd, opaqueCountBufs_[fi], 0, sizeof(uint32_t), 0);

    // Barrier: fill → SSBO write by compute
    VkBufferMemoryBarrier2 fillBarrier{};
    fillBarrier.sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    fillBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    fillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    fillBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    fillBarrier.buffer        = opaqueCountBufs_[fi];
    fillBarrier.size          = VK_WHOLE_SIZE;
    VkDependencyInfo fillDep{};
    fillDep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    fillDep.bufferMemoryBarrierCount = 1;
    fillDep.pBufferMemoryBarriers    = &fillBarrier;
    vkCmdPipelineBarrier2(cmd, &fillDep);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeLayout_, 0, 1, &descSets_[fi], 0, nullptr);

    struct CullPush { glm::mat4 vp; uint32_t count; };
    CullPush cp{ viewProj, count };
    vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPush), &cp);

    uint32_t groups = (count + 63) / 64;
    vkCmdDispatch(cmd, groups, 1, 1);

    // Barrier: compute SSBO write → indirect draw read
    VkBufferMemoryBarrier2 compBarriers[2] = {};
    compBarriers[0].sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    compBarriers[0].srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    compBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    compBarriers[0].dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    compBarriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    compBarriers[0].buffer        = opaqueDrawBufs_[fi];
    compBarriers[0].size          = VK_WHOLE_SIZE;
    compBarriers[1]               = compBarriers[0];
    compBarriers[1].buffer        = opaqueCountBufs_[fi];
    VkDependencyInfo compDep{};
    compDep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    compDep.bufferMemoryBarrierCount = 2;
    compDep.pBufferMemoryBarriers    = compBarriers;
    vkCmdPipelineBarrier2(cmd, &compDep);
}
