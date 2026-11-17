#pragma once
#include <vulkan/vulkan.h>
#include "../memory/HarukaAllocator.hpp"

struct TexSlot {
    VkImage          image     = VK_NULL_HANDLE;
    VkImageView      imageView = VK_NULL_HANDLE;
    HarukaAllocation alloc;
    // 여러 머티리얼이 함께 쓰는 기본값 1x1 텍스처(2026-11-18, 성능 안정화 1-6). 머티리얼을 지울 때 놓지 않고 GpuScene 이 지웁니다.
    bool             shared    = false;
};

// PBR material: 5 texture slots (albedo, normal, metalRough, AO, emissive)
struct MaterialHandle {
    TexSlot albedo;
    TexSlot normalMap;
    TexSlot metalRough;
    TexSlot ao;
    TexSlot emissive;
};
