#include "FrameResources.hpp"
#include <stdexcept>

// ── createLayouts ──────────────────────────────────────────────────────────────
void FrameResources::createLayouts(RenderContext& rc) {
    rc_ = &rc;

    // Set 0: FrameUBO (vert + frag)
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0; b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b.descriptorCount = 1; b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1; ci.pBindings = &b;
        if (vkCreateDescriptorSetLayout(rc.device, &ci, nullptr, &frameLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create frame set layout!");
    }

    // Set 1: Scene — shadow, IBL, lights SSBO
    {
        VkDescriptorSetLayoutBinding bindings[5] = {};
        for (int i = 0; i < 4; i++) {
            bindings[i].binding = (uint32_t)i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        bindings[4].binding = 4; bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[4].descriptorCount = 1; bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 5; ci.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(rc.device, &ci, nullptr, &sceneLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create scene set layout!");
    }

    // Set 2: Transform SSBO (vert only)
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0; b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b.descriptorCount = 1; b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1; ci.pBindings = &b;
        if (vkCreateDescriptorSetLayout(rc.device, &ci, nullptr, &transformLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create transform set layout!");
    }

    // Set 3: Material (push descriptor) — 5 PBR textures
    {
        VkDescriptorSetLayoutBinding bindings[5] = {};
        for (int i = 0; i < 5; i++) {
            bindings[i].binding = (uint32_t)i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        ci.bindingCount = 5; ci.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(rc.device, &ci, nullptr, &materialLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create material set layout!");
    }

    // 공유 PBR 파이프라인 레이아웃 (sets 0~3) — 섀도/메인 PSO 공용
    {
        VkDescriptorSetLayout setLayouts[] = { frameLayout_, sceneLayout_,
                                               transformLayout_, materialLayout_ };
        // 눈 번호 푸시 상수(DEF-09). pbr.vert · pbr.frag · depth.vert 가 같은 선언을 씁니다
        // (haruka_shared.h 의 HARUKA_VIEW_PUSH_CONSTANT). 섀도 셰이더는 선언하지 않아도 됩니다.
        VkPushConstantRange viewRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(GpuViewPush) };
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 4; pli.pSetLayouts = setLayouts;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &viewRange;
        if (vkCreatePipelineLayout(rc.device, &pli, nullptr, &pbrPipelineLayout_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create main pipeline layout!");
    }
}

// ── createPerFrame ─────────────────────────────────────────────────────────────
void FrameResources::createPerFrame(RenderContext& rc, uint32_t swapImageCount) {
    // Zone C 버퍼 공용 생성기
    auto mkBuf = [&](VkBuffer& buf, HarukaAllocation& alloc,
                     VkDeviceSize sz, VkBufferUsageFlags usage, const char* what) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = sz; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(rc.device, &bi, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error(std::string("Failed to create ") + what);
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(rc.device, buf, &reqs);
        alloc = rc.allocator->allocC(reqs.size, reqs.alignment);
        if (!alloc.isValid()) throw std::runtime_error(std::string("Zone C alloc failed: ") + what);
        if (rc.allocator->bind(buf, alloc) != VK_SUCCESS)
            throw std::runtime_error(std::string("Failed to bind ") + what);
    };

    // UBO (minUniformBufferOffsetAlignment 정렬)
    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(rc.physDev, &devProps);
    const VkDeviceSize align   = devProps.limits.minUniformBufferOffsetAlignment;
    const VkDeviceSize uboSize = (sizeof(FrameUBO) + align - 1) & ~(align - 1);

    for (int i = 0; i < MAX_FRAMES; i++) {
        mkBuf(uboBufs_[i], uboAllocs_[i], uboSize,
              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "UBO buffer");
        mkBuf(transformBufs_[i], transformAllocs_[i], drawRows_ * sizeof(GpuTransform),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "transform SSBO");
        mkBuf(lightBufs_[i], lightAllocs_[i], lights_ * sizeof(GpuLight),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "light SSBO");
    }

    // 풀 + 세트 + 버퍼 바인딩 (frame: UBO / transform: SSBO)
    auto mkPoolAndSets = [&](VkDescriptorPool& pool, VkDescriptorSet* sets,
                             VkDescriptorSetLayout layout, VkDescriptorType type) {
        VkDescriptorPoolSize ps{ type, MAX_FRAMES };
        VkDescriptorPoolCreateInfo poolCI{};
        poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCI.poolSizeCount = 1; poolCI.pPoolSizes = &ps; poolCI.maxSets = MAX_FRAMES;
        if (vkCreateDescriptorPool(rc.device, &poolCI, nullptr, &pool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create descriptor pool!");

        VkDescriptorSetLayout layouts[MAX_FRAMES] = { layout, layout };
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = pool;
        ai.descriptorSetCount = MAX_FRAMES; ai.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(rc.device, &ai, sets) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate descriptor sets!");
    };

    mkPoolAndSets(framePool_,     frameSets_,     frameLayout_,     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    mkPoolAndSets(transformPool_, transformSets_, transformLayout_, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    for (int i = 0; i < MAX_FRAMES; i++) {
        VkDescriptorBufferInfo uboBI{ uboBufs_[i], 0, sizeof(FrameUBO) };
        VkDescriptorBufferInfo tfBI { transformBufs_[i], 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet ws[2] = {};
        ws[0].sType = ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[0].dstSet = frameSets_[i]; ws[0].dstBinding = 0;
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ws[0].descriptorCount = 1; ws[0].pBufferInfo = &uboBI;
        ws[1].dstSet = transformSets_[i]; ws[1].dstBinding = 0;
        ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[1].descriptorCount = 1; ws[1].pBufferInfo = &tfBI;
        vkUpdateDescriptorSets(rc.device, 2, ws, 0, nullptr);
    }

    // 커맨드 버퍼
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = rc.cmdPool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = MAX_FRAMES;
        if (vkAllocateCommandBuffers(rc.device, &ai, cmdBuffers_.data()) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate command buffers!");
    }

    // 동기화 객체
    {
        imagesInFlight_.assign(swapImageCount, VK_NULL_HANDLE);
        semRenderDone_.resize(swapImageCount);

        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES; i++) {
            if (vkCreateSemaphore(rc.device, &sci, nullptr, &semImageAvail_[i]) != VK_SUCCESS ||
                vkCreateFence(rc.device, &fci, nullptr, &fences_[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create sync objects!");
        }
        for (uint32_t i = 0; i < swapImageCount; i++) {
            if (vkCreateSemaphore(rc.device, &sci, nullptr, &semRenderDone_[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create render-done semaphore!");
        }
    }
}

// ── writeSceneSets ─────────────────────────────────────────────────────────────
void FrameResources::writeSceneSets(VkSampler shadowSampler, VkImageView shadowView,
                                    VkSampler iblSampler, VkImageView irradianceView,
                                    VkImageView prefilterView,
                                    VkSampler brdfSampler, VkImageView brdfView) {
    VkDescriptorPoolSize sizes[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * MAX_FRAMES },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1 * MAX_FRAMES },
    };
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.poolSizeCount = 2; poolCI.pPoolSizes = sizes; poolCI.maxSets = MAX_FRAMES;
    if (vkCreateDescriptorPool(rc_->device, &poolCI, nullptr, &scenePool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create scene descriptor pool!");

    VkDescriptorSetLayout layouts[MAX_FRAMES] = { sceneLayout_, sceneLayout_ };
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = scenePool_;
    ai.descriptorSetCount = MAX_FRAMES; ai.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(rc_->device, &ai, sceneSets_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate scene descriptor sets!");

    for (int i = 0; i < MAX_FRAMES; i++) {
        VkDescriptorImageInfo  shadowI{ shadowSampler, shadowView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo  irrI   { iblSampler,    irradianceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo  prefI  { iblSampler,    prefilterView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo  brdfI  { brdfSampler,   brdfView,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorBufferInfo lightI { lightBufs_[i], 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet ws[5] = {};
        for (int j = 0; j < 5; j++) {
            ws[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ws[j].dstSet = sceneSets_[i];
            ws[j].dstBinding = (uint32_t)j; ws[j].descriptorCount = 1;
        }
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[0].pImageInfo  = &shadowI;
        ws[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[1].pImageInfo  = &irrI;
        ws[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[2].pImageInfo  = &prefI;
        ws[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ws[3].pImageInfo  = &brdfI;
        ws[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         ws[4].pBufferInfo = &lightI;
        vkUpdateDescriptorSets(rc_->device, 5, ws, 0, nullptr);
    }
}

// ── onSwapchainRecreated ───────────────────────────────────────────────────────
void FrameResources::onSwapchainRecreated(uint32_t swapImageCount) {
    imagesInFlight_.assign(swapImageCount, VK_NULL_HANDLE);

    if (semRenderDone_.size() != swapImageCount) {
        for (auto s : semRenderDone_) vkDestroySemaphore(rc_->device, s, nullptr);
        semRenderDone_.resize(swapImageCount);
        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (auto& s : semRenderDone_) vkCreateSemaphore(rc_->device, &sci, nullptr, &s);
    }
}

// ── destroy ────────────────────────────────────────────────────────────────────
void FrameResources::destroy() {
    if (!rc_) return;
    VkDevice device = rc_->device;

    // Zone C는 arena — 버퍼 핸들만 파괴 (allocator.destroy()가 메모리 일괄 해제)
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (uboBufs_[i])       { vkDestroyBuffer(device, uboBufs_[i], nullptr);       uboBufs_[i]       = VK_NULL_HANDLE; }
        if (transformBufs_[i]) { vkDestroyBuffer(device, transformBufs_[i], nullptr); transformBufs_[i] = VK_NULL_HANDLE; }
        if (lightBufs_[i])     { vkDestroyBuffer(device, lightBufs_[i], nullptr);     lightBufs_[i]     = VK_NULL_HANDLE; }
    }
    if (framePool_)     { vkDestroyDescriptorPool(device, framePool_, nullptr);     framePool_     = VK_NULL_HANDLE; }
    if (transformPool_) { vkDestroyDescriptorPool(device, transformPool_, nullptr); transformPool_ = VK_NULL_HANDLE; }
    if (scenePool_)     { vkDestroyDescriptorPool(device, scenePool_, nullptr);     scenePool_     = VK_NULL_HANDLE; }

    if (frameLayout_)     { vkDestroyDescriptorSetLayout(device, frameLayout_, nullptr);     frameLayout_     = VK_NULL_HANDLE; }
    if (sceneLayout_)     { vkDestroyDescriptorSetLayout(device, sceneLayout_, nullptr);     sceneLayout_     = VK_NULL_HANDLE; }
    if (transformLayout_) { vkDestroyDescriptorSetLayout(device, transformLayout_, nullptr); transformLayout_ = VK_NULL_HANDLE; }
    if (materialLayout_)  { vkDestroyDescriptorSetLayout(device, materialLayout_, nullptr);  materialLayout_  = VK_NULL_HANDLE; }
    if (pbrPipelineLayout_) { vkDestroyPipelineLayout(device, pbrPipelineLayout_, nullptr);  pbrPipelineLayout_ = VK_NULL_HANDLE; }

    for (int i = 0; i < MAX_FRAMES; i++) {
        if (semImageAvail_[i]) { vkDestroySemaphore(device, semImageAvail_[i], nullptr); semImageAvail_[i] = VK_NULL_HANDLE; }
        if (fences_[i])        { vkDestroyFence(device, fences_[i], nullptr);            fences_[i]        = VK_NULL_HANDLE; }
    }
    for (auto s : semRenderDone_) vkDestroySemaphore(device, s, nullptr);
    semRenderDone_.clear();
    imagesInFlight_.clear();
}
