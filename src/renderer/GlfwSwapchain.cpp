#include "GlfwSwapchain.hpp"
#include "../core/FrameTimings.hpp"
#include <iostream>
#include <algorithm>
#include <limits>
#include <stdexcept>

GlfwSwapchain::GlfwSwapchain(IWindow* window, VkSurfaceKHR surface,
                              VkPhysicalDevice physDevice, VkDevice device,
                              uint32_t graphicsFamily, uint32_t presentFamily)
    : window_(window), surface_(surface)
    , physDevice_(physDevice), device_(device)
    , graphicsFamily_(graphicsFamily), presentFamily_(presentFamily)
{}

// ── Format / present-mode / extent selection ──────────────────────────────────

VkSurfaceFormatKHR GlfwSwapchain::chooseFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) const
{
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
    return formats[0];
}

VkPresentModeKHR GlfwSwapchain::choosePresentMode(
    const std::vector<VkPresentModeKHR>& modes) const
{
    for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D GlfwSwapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& caps) const {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;
    int w, h;
    window_->getFramebufferSize(w, h);
    return {
        std::clamp((uint32_t)w, caps.minImageExtent.width,  caps.maxImageExtent.width),
        std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

// ── create / destroy / recreate ───────────────────────────────────────────────

void GlfwSwapchain::create() {
    // Query surface capabilities
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice_, surface_, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice_, surface_, &fmtCount, formats.data());

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice_, surface_, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> modes(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice_, surface_, &pmCount, modes.data());

    auto fmt = chooseFormat(formats);
    auto pm  = choosePresentMode(modes);
    auto ext = chooseExtent(caps);

    uint32_t imgCnt = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCnt > caps.maxImageCount)
        imgCnt = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = imgCnt;
    ci.imageFormat      = fmt.format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t qfi[] = { graphicsFamily_, presentFamily_ };
    if (graphicsFamily_ != presentFamily_) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = qfi;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform   = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = pm;
    ci.clipped        = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &handle_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain!");

    vkGetSwapchainImagesKHR(device_, handle_, &imgCnt, nullptr);
    images_.resize(imgCnt);
    vkGetSwapchainImagesKHR(device_, handle_, &imgCnt, images_.data());
    format_ = fmt.format;
    extent_ = ext;

    // 실행 중에 프레젠트 모드와 이미지 수를 확인할 방법이 전혀 없었습니다.
    // 이 둘을 모르면 프레임 페이싱 논의가 전부 추측이 됩니다.
    {
        auto& ft = gFrameTimings();
        ft.presentMode = (int)pm;
        ft.imageCount  = (int)images_.size();
        const char* pmName =
            pm == VK_PRESENT_MODE_IMMEDIATE_KHR    ? "IMMEDIATE"    :
            pm == VK_PRESENT_MODE_MAILBOX_KHR      ? "MAILBOX"      :
            pm == VK_PRESENT_MODE_FIFO_KHR         ? "FIFO(vsync)"  :
            pm == VK_PRESENT_MODE_FIFO_RELAXED_KHR ? "FIFO_RELAXED" : "UNKNOWN";
        std::cout << "[Swapchain] presentMode=" << pmName
                  << " images=" << images_.size()
                  << " extent=" << ext.width << "x" << ext.height << std::endl;
    }

    buildImageViews();
    dirty_ = false;
}

void GlfwSwapchain::buildImageViews() {
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image    = images_[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format   = format_;
        ci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(device_, &ci, nullptr, &imageViews_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create swapchain image view!");
    }
}

void GlfwSwapchain::destroy() {
    for (auto iv : imageViews_) vkDestroyImageView(device_, iv, nullptr);
    imageViews_.clear();
    images_.clear();
    if (handle_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
}

void GlfwSwapchain::recreate() {
    // Wait until the window has a non-zero framebuffer (e.g. un-minimized)
    int w = 0, h = 0;
    window_->getFramebufferSize(w, h);
    while (w == 0 || h == 0) {
        window_->waitEvents();
        window_->getFramebufferSize(w, h);
    }
    destroy();
    create();
    window_->resetResizedFlag();
}

// ── Acquire / present ─────────────────────────────────────────────────────────

VkResult GlfwSwapchain::acquireNextImage(VkSemaphore signal, uint32_t& outIndex) {
    VkResult res = vkAcquireNextImageKHR(device_, handle_, UINT64_MAX,
                                          signal, VK_NULL_HANDLE, &outIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR)
        dirty_ = true;
    return res;
}

VkResult GlfwSwapchain::present(VkQueue queue, uint32_t imageIndex, VkSemaphore wait) {
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &wait;
    pi.swapchainCount     = 1; pi.pSwapchains     = &handle_;
    pi.pImageIndices      = &imageIndex;
    VkResult res = vkQueuePresentKHR(queue, &pi);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR ||
        window_->wasResized())
        dirty_ = true;
    return res;
}
