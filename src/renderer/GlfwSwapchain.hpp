#pragma once
#include "ISwapchain.hpp"
#include "../platform/IWindow.hpp"
#include <vector>

// GLFW/surface-backed swapchain for the PC editor path.
// Wraps VkSwapchainKHR creation, image-view management, acquire, and present.
// Owns nothing from the Vulkan instance layer — caller manages VkSurfaceKHR.
class GlfwSwapchain final : public ISwapchain {
public:
    GlfwSwapchain(IWindow* window, VkSurfaceKHR surface,
                  VkPhysicalDevice physDevice, VkDevice device,
                  uint32_t graphicsFamily, uint32_t presentFamily);
    ~GlfwSwapchain() override { destroy(); }

    // 참이면 표시가 모니터 주사율을 기다리지 않게 MAILBOX → IMMEDIATE → FIFO 순으로 고릅니다(XR 중에는
    // 헤드셋이 루프 속도를 정하므로). 거짓이면 MAILBOX → FIFO. create() 전에 정합니다.
    void setPreferNonBlocking(bool on) { preferNonBlocking_ = on; }

    void create()   override;
    void destroy()  override;
    void recreate() override;

    VkResult acquireNextImage(VkSemaphore signal, uint32_t& outIndex) override;
    VkResult present(VkQueue queue, uint32_t imageIndex, VkSemaphore wait) override;

    bool needsRecreation() const override { return dirty_ || window_->wasResized(); }

    const std::vector<VkImage>&     images()     const override { return images_;     }
    const std::vector<VkImageView>& imageViews() const override { return imageViews_; }
    VkFormat   format() const override { return format_; }
    VkExtent2D extent() const override { return extent_; }

private:
    IWindow*         window_;
    VkSurfaceKHR     surface_;
    VkPhysicalDevice physDevice_;
    VkDevice         device_;
    uint32_t         graphicsFamily_;
    uint32_t         presentFamily_;

    VkSwapchainKHR           handle_     = VK_NULL_HANDLE;
    std::vector<VkImage>     images_;
    std::vector<VkImageView> imageViews_;
    VkFormat                 format_     = VK_FORMAT_UNDEFINED;
    VkExtent2D               extent_     = {};
    bool                     dirty_      = false;
    bool                     preferNonBlocking_ = false;

    VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>&) const;
    VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>&) const;
    VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR&) const;
    void               buildImageViews();
};
