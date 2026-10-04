#pragma once
#include <vulkan/vulkan.h>

// ── IVulkanDeviceHooks ────────────────────────────────────────────────────────
// 렌더러가 Vulkan 인스턴스 · 물리 장치 · 논리 장치를 만들 때 바깥(OpenXR 런타임)에 맡기는 자리입니다.
// OpenXR(XR_KHR_vulkan_enable2)은 헤드셋이 연결된 GPU 를 골라 주고, 런타임이 필요한 확장을 덧붙여
// 인스턴스와 장치를 직접 만듭니다. 그래서 XR 시스템을 렌더러보다 먼저 만들고 이 연결부를 넘깁니다
// (UE · Unity · Godot 모두 XR 모듈이 렌더러보다 먼저 장치를 정합니다). 연결부가 없으면 렌더러가
// 지금처럼 직접 만듭니다. 렌더러는 OpenXR 를 모릅니다.
class IVulkanDeviceHooks {
public:
    virtual ~IVulkanDeviceHooks() = default;

    // ci 로 인스턴스를 만듭니다. 런타임이 자기 확장을 덧붙입니다.
    virtual VkResult createVulkanInstance(const VkInstanceCreateInfo& ci, VkInstance& out) = 0;

    // 헤드셋이 연결된 물리 장치입니다. 고를 수 없으면 VK_NULL_HANDLE.
    virtual VkPhysicalDevice vulkanPhysicalDevice(VkInstance instance) = 0;

    // ci 로 장치를 만듭니다. 런타임이 자기 확장을 덧붙입니다.
    virtual VkResult createVulkanDevice(VkPhysicalDevice physicalDevice,
                                        const VkDeviceCreateInfo& ci, VkDevice& out) = 0;
};
