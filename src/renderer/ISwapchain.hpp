#pragma once
#include <vulkan/vulkan.h>
#include <vector>

// Abstract swapchain interface for the desktop window.
//   GlfwSwapchain — VkSwapchainKHR via VkSurfaceKHR (editor and game window)
//   NullSwapchain — no window (--headless, Quest): zero images, never has area
//
// Renderer holds one ISwapchain and never touches VkSwapchainKHR directly. XR output does not go
// through this interface: the OpenXR swapchain images are attached with Renderer::attachXrSwapchain.
// imageCount() == 0 is a valid state (no window); the renderer then draws only XR or synthetic eyes.
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

// 창이 없을 때의 스왑체인(2026-10-31, 패키징 2단계). 이미지가 0장이고 언제나 넓이가 없어서, 렌더러는 최소화와 같은 경로
// (DEF-106)로 데스크톱 화면을 건너뛰고 XR 이미지에만 그립니다. 포맷 · 크기는 명목값입니다: 톤매핑 패스가 이 포맷으로 렌더
// 패스를 미리 만들고(UNDEFINED 로는 만들 수 없음) XR 세션의 선호 포맷이 되며, 크기 의존 타깃이 0 크기가 되지 않게 합니다.
class NullSwapchain final : public ISwapchain {
public:
    NullSwapchain(VkFormat format, VkExtent2D extent) : format_(format), extent_(extent) {}

    void create()   override {}
    void destroy()  override {}
    void recreate() override {}
    bool hasArea() const override { return false; }
    // 넓이가 없으므로 불리지 않습니다. 혹시 불리면 렌더러가 예외로 알립니다.
    VkResult acquireNextImage(VkSemaphore, uint32_t& outIndex) override { outIndex = 0; return VK_ERROR_SURFACE_LOST_KHR; }
    VkResult present(VkQueue, uint32_t, VkSemaphore) override          { return VK_ERROR_SURFACE_LOST_KHR; }
    bool needsRecreation() const override { return false; }

    const std::vector<VkImage>&     images()     const override { return images_; }
    const std::vector<VkImageView>& imageViews() const override { return views_; }
    VkFormat   format() const override { return format_; }
    VkExtent2D extent() const override { return extent_; }

private:
    VkFormat                 format_;
    VkExtent2D               extent_;
    std::vector<VkImage>     images_;
    std::vector<VkImageView> views_;
};
