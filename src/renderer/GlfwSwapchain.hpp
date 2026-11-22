#pragma once
#include "ISwapchain.hpp"
#include "../platform/IWindow.hpp"
#include <vector>
#include <cstdint>

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
    // acquire 를 기다리는 최대 시간(ns). 기본은 무한(메인 창, 예전과 같음). 보조 창은 0 을 씁니다(다중 OS 창 M2 D3): 이미지가
    // 준비되지 않았으면 VK_NOT_READY · VK_TIMEOUT 이 돌아오고 신호 세마포어는 신호되지 않으므로, 그 창은 그 프레임을 건너뜁니다.
    // 무한이면 FIFO 보조 창 하나가 메인 루프를 그 모니터의 주사율로 묶습니다.
    void setAcquireTimeout(uint64_t ns) { acquireTimeoutNs_ = ns; }
    // 보조 창 스왑체인이면 참. 프레임 계측의 presentMode · imageCount(메인 창 값)를 덮어쓰지 않고, 로그 머리말만 바꿉니다.
    void setAuxiliary(bool on) { auxiliary_ = on; }

    void create()   override;
    void destroy()  override;
    void recreate() override;

    VkResult acquireNextImage(VkSemaphore signal, uint32_t& outIndex) override;
    VkResult present(VkQueue queue, uint32_t imageIndex, VkSemaphore wait) override;

    bool needsRecreation() const override { return dirty_ || window_->wasResized(); }
    bool hasArea() const override {
        int w = 0, h = 0;
        window_->getFramebufferSize(w, h);
        return w > 0 && h > 0;
    }

    const std::vector<VkImage>&     images()     const override { return images_;     }
    const std::vector<VkImageView>& imageViews() const override { return imageViews_; }
    VkFormat   format() const override { return format_; }
    VkExtent2D extent() const override { return extent_; }
    bool       supportsTransferSrc() const override { return supportsTransferSrc_; }

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
    bool                     supportsTransferSrc_ = false;
    bool                     auxiliary_  = false;
    uint64_t                 acquireTimeoutNs_ = UINT64_MAX;

    VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>&) const;
    VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>&) const;
    VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR&) const;
    void               buildImageViews();
};
