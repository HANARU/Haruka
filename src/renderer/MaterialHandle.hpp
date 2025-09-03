#pragma once
#include <vulkan/vulkan.h>
#include "../memory/HarukaAllocator.hpp"

struct TexSlot {
    VkImage          image     = VK_NULL_HANDLE;
    VkImageView      imageView = VK_NULL_HANDLE;
    HarukaAllocation alloc;
};

// PBR material: 5 texture slots (albedo, normal, metalRough, AO, emissive)
struct MaterialHandle {
    TexSlot albedo;
    TexSlot normalMap;
    TexSlot metalRough;
    TexSlot ao;
    TexSlot emissive;
};
