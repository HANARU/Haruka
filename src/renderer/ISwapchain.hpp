#pragma once
#include <vulkan/vulkan.h>
#include <vector>

// Abstract swapchain interface.
//   GlfwSwapchain — VkSwapchainKHR via VkSurfaceKHR  (PC editor)
//   XrSwapchain   — XrSwapchain per-eye via OpenXR   (Quest Pro — future)
//
// Renderer holds one ISwapchain and never touches VkSwapchainKHR directly.
// XrSwapchain will override present() with a no-op (compositor handles it).
class ISwapchain {
public:
    virtual ~ISwapchain() = default;

    // (Re)build internal swapchain + image views from current surface/session state.
    virtual void create()   = 0;
    // Release all swapchain resources. Safe to call multiple times (idempotent).
    virtual void destroy()  = 0;
    // destroy() + create(). Call only while hasArea() is true.
    virtual void recreate() = 0;

    // False while the surface has no area (desktop window minimized). Nothing can be acquired or
    // created then; the renderer skips the desktop frame instead of waiting (DEF-106).
    virtual bool hasArea() const = 0;

    // Signal semaphore when the acquired image is ready to render into.
    // Returns VK_ERROR_OUT_OF_DATE_KHR when recreation is needed.
    virtual VkResult acquireNextImage(VkSemaphore signal, uint32_t& outIndex) = 0;

    // Wait on semaphore, then present imageIndex. XrSwapchain ignores this
    // (frame submission goes through xrEndFrame instead).
    virtual VkResult present(VkQueue queue, uint32_t imageIndex, VkSemaphore wait) = 0;

    // True when the swapchain can no longer produce frames and must be recreated
    // (window resize, surface lost, VK_ERROR_OUT_OF_DATE_KHR / SUBOPTIMAL, etc.)
    virtual bool needsRecreation() const = 0;

    virtual const std::vector<VkImage>&     images()     const = 0;
    virtual const std::vector<VkImageView>& imageViews() const = 0;
    virtual VkFormat   format()     const = 0;
    virtual VkExtent2D extent()     const = 0;

    uint32_t imageCount() const { return (uint32_t)images().size(); }
};
