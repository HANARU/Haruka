#include "LauncherHost.hpp"
#include "core/HLog.hpp"
#include "core/HText.hpp"
#include "platform/glfw/GlfwPlatform.hpp"
#include "platform/glfw/GlfwWindow.hpp"
#include "vfs/PathUtf8.hpp"
#include "vfs/Vfs.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kLauncherLogCat = "Launcher";
inline constinit HLogCategory LogLauncher{ kLauncherLogCat, ELogVerbosity::Log };

uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

} // namespace

LauncherHost::LauncherHost() = default;
LauncherHost::~LauncherHost() {
    cleanup();
}

IWindow* LauncherHost::window() const {
    return window_.get();
}

bool LauncherHost::init(const LauncherHostOptions& opts) {
    opts_ = opts;
    const auto t0 = std::chrono::steady_clock::now();

    hvfs::reset();
    std::error_code ec;
    if (!opts_.exeDir.empty() && fs::is_directory(opts_.exeDir / "shaders", ec)) {
        hvfs::mountDirectory(opts_.exeDir, "shaders", 10);
    } else if (!opts_.engineRoot.empty() && fs::is_directory(opts_.engineRoot / "build" / "Release" / "shaders", ec)) {
        hvfs::mountDirectory(opts_.engineRoot / "build" / "Release", "shaders", 10);
    }
    if (!opts_.engineRoot.empty() && fs::is_directory(opts_.engineRoot / "Engine" / "Content", ec)) {
        hvfs::mountDirectoryAs(opts_.engineRoot / "Engine" / "Content", "/Engine/", 10);
    }

    if (!opts_.culture.empty()) {
        std::vector<std::string> errs;
        const size_t n = htext::useCulture(opts_.culture, &errs);
        for (const auto& e : errs) {
            HLOG(LogLauncher, Warning) << "Localization: " << e;
        }
        HLOG(LogLauncher, Log) << "Culture: " << opts_.culture << " (" << n << " entries)";
    }

    if (!GlfwPlatform::acquire()) {
        HLOG(LogLauncher, Error) << "GlfwPlatform::acquire failed";
        return false;
    }
    glfwAcquired_ = true;

    window_ = std::make_unique<GlfwWindow>(opts_.width, opts_.height, "Haruka Launcher", opts_.hiddenWindow);
    try {
        window_->init();
    } catch (const std::exception& e) {
        HLOG(LogLauncher, Error) << "Window init failed: " << e.what();
        return false;
    }

    if (!createInstance()) return false;
    try {
        surface_ = window_->createSurface(instance_);
    } catch (const std::exception& e) {
        HLOG(LogLauncher, Error) << "Surface creation failed: " << e.what();
        return false;
    }
    if (surface_ == VK_NULL_HANDLE) return false;

    if (!pickPhysicalDevice()) return false;
    if (!createDevice()) return false;
    if (!createCommandResources()) return false;
    if (!createSwapchain()) return false;

    slate_.fontAtlas().setAtlasHeight(1024);
    slate_.setSlotVertexCapacity(65536 * 2);
    auto beginCmd = [this]() { return beginOneTimeCmd(); };
    auto endCmd = [this](VkCommandBuffer cmd) { endOneTimeCmd(cmd); };
    try {
        slate_.init(device_, physDevice_, swapFormat_, pushDescriptorFn_, beginCmd, endCmd,
                    "/Engine/Fonts/Pretendard-Regular.ttf");
    } catch (const std::exception& e) {
        HLOG(LogLauncher, Error) << "HSlateRenderer::init failed: " << e.what();
        return false;
    }
    slate_.fontAtlas().loadBoldFont("/Engine/Fonts/Pretendard-SemiBold.ttf");

    // Create framebuffers for windowRenderPass
    framebuffers_.resize(swapViews_.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < swapViews_.size(); ++i) {
        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = slate_.windowRenderPass();
        fbci.attachmentCount = 1;
        fbci.pAttachments = &swapViews_[i];
        fbci.width = swapExtent_.width;
        fbci.height = swapExtent_.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(device_, &fbci, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            HLOG(LogLauncher, Error) << "vkCreateFramebuffer failed";
            return false;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    startupMs_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
    HLOG(LogLauncher, Log) << "Launcher ready in " << static_cast<int>(startupMs_) << " ms";
    return true;
}

void LauncherHost::cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        slate_.cleanup();
        destroySwapchain();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            if (imageAvail_[i] != VK_NULL_HANDLE) vkDestroySemaphore(device_, imageAvail_[i], nullptr);
            if (inFlightFences_[i] != VK_NULL_HANDLE) vkDestroyFence(device_, inFlightFences_[i], nullptr);
            imageAvail_[i] = VK_NULL_HANDLE;
            inFlightFences_[i] = VK_NULL_HANDLE;
        }
        if (cmdPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, cmdPool_, nullptr);
            cmdPool_ = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    if (window_) {
        window_->cleanup();
        window_.reset();
    }
    if (glfwAcquired_) {
        GlfwPlatform::release();
        glfwAcquired_ = false;
    }
}

bool LauncherHost::createInstance() {
    if (volkInitialize() != VK_SUCCESS) {
        HLOG(LogLauncher, Error) << "volkInitialize failed";
        return false;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "HarukaLauncher";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "Haruka";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char*> exts = window_->getRequiredInstanceExtensions();
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
        HLOG(LogLauncher, Error) << "vkCreateInstance failed";
        return false;
    }
    volkLoadInstanceOnly(instance_);
    return true;
}

bool LauncherHost::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        HLOG(LogLauncher, Error) << "No Vulkan physical devices found";
        return false;
    }
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance_, &count, devs.data());

    auto checkDevice = [&](VkPhysicalDevice dev, uint32_t& outGfx, uint32_t& outPres) -> bool {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qProps.data());

        uint32_t gfx = UINT32_MAX;
        uint32_t pres = UINT32_MAX;
        for (uint32_t i = 0; i < qCount; ++i) {
            if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfx = i;
            VkBool32 sup = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &sup);
            if (sup) pres = i;
            if (gfx != UINT32_MAX && pres != UINT32_MAX) break;
        }
        if (gfx == UINT32_MAX || pres == UINT32_MAX) return false;

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
        bool hasSwap = false, hasPush = false;
        for (const auto& e : exts) {
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) hasSwap = true;
            if (std::strcmp(e.extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0) hasPush = true;
        }
        if (!hasSwap || !hasPush) return false;

        outGfx = gfx;
        outPres = pres;
        return true;
    };

    if (opts_.preferredGpu >= 0 && opts_.preferredGpu < static_cast<int>(devs.size())) {
        uint32_t g = 0, p = 0;
        if (checkDevice(devs[static_cast<size_t>(opts_.preferredGpu)], g, p)) {
            physDevice_ = devs[static_cast<size_t>(opts_.preferredGpu)];
            graphicsFamily_ = g;
            presentFamily_ = p;
            return true;
        }
    }

    for (auto dev : devs) {
        uint32_t g = 0, p = 0;
        if (checkDevice(dev, g, p)) {
            physDevice_ = dev;
            graphicsFamily_ = g;
            presentFamily_ = p;
            return true;
        }
    }
    HLOG(LogLauncher, Error) << "No suitable Vulkan device found";
    return false;
}

bool LauncherHost::createDevice() {
    std::vector<uint32_t> uniqueFamilies = { graphicsFamily_ };
    if (presentFamily_ != graphicsFamily_) uniqueFamilies.push_back(presentFamily_);

    const float prio = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    for (uint32_t f : uniqueFamilies) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = f;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;
        qcis.push_back(qci);
    }

    const char* devExts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    };

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
    dci.pQueueCreateInfos = qcis.data();
    dci.enabledExtensionCount = 2;
    dci.ppEnabledExtensionNames = devExts;

    if (vkCreateDevice(physDevice_, &dci, nullptr, &device_) != VK_SUCCESS) {
        HLOG(LogLauncher, Error) << "vkCreateDevice failed";
        return false;
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
    pushDescriptorFn_ = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(
        vkGetDeviceProcAddr(device_, "vkCmdPushDescriptorSetKHR"));
    return pushDescriptorFn_ != nullptr;
}

bool LauncherHost::createCommandResources() {
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = graphicsFamily_;
    if (vkCreateCommandPool(device_, &cpci, nullptr, &cmdPool_) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = kFramesInFlight;
    if (vkAllocateCommandBuffers(device_, &cbai, cmds_) != VK_SUCCESS) return false;

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateFence(device_, &fci, nullptr, &inFlightFences_[i]) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(device_, &sci, nullptr, &imageAvail_[i]) != VK_SUCCESS) return false;
    }
    return true;
}

bool LauncherHost::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice_, surface_, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice_, surface_, &fmtCount, fmts.data());
    if (fmts.empty()) return false;

    VkSurfaceFormatKHR chosen = fmts[0];
    for (const auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    swapFormat_ = chosen.format;

    if (caps.currentExtent.width != UINT32_MAX) {
        swapExtent_ = caps.currentExtent;
    } else {
        int fw = 0, fh = 0;
        window_->getFramebufferSize(fw, fh);
        swapExtent_.width = std::clamp(static_cast<uint32_t>(fw), caps.minImageExtent.width, caps.maxImageExtent.width);
        swapExtent_.height = std::clamp(static_cast<uint32_t>(fh), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (swapExtent_.width == 0 || swapExtent_.height == 0) return true;

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapSupportsTransferSrc_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (swapSupportsTransferSrc_) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface_;
    sci.minImageCount = imgCount;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = swapExtent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = usage;
    uint32_t qf[] = { graphicsFamily_, presentFamily_ };
    if (graphicsFamily_ != presentFamily_) {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = qf;
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_) != VK_SUCCESS) return false;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, nullptr);
    swapImages_.resize(actualCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, swapImages_.data());

    swapViews_.resize(actualCount, VK_NULL_HANDLE);
    renderDone_.resize(actualCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semCi{};
    semCi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = swapImages_[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = swapFormat_;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &ivci, nullptr, &swapViews_[i]) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(device_, &semCi, nullptr, &renderDone_[i]) != VK_SUCCESS) return false;
    }
    return true;
}

void LauncherHost::destroySwapchain() {
    for (auto fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();
    for (auto s : renderDone_) {
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(device_, s, nullptr);
    }
    renderDone_.clear();
    for (auto v : swapViews_) {
        if (v != VK_NULL_HANDLE) vkDestroyImageView(device_, v, nullptr);
    }
    swapViews_.clear();
    swapImages_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool LauncherHost::recreateSwapchain() {
    int fw = 0, fh = 0;
    window_->getFramebufferSize(fw, fh);
    if (fw <= 0 || fh <= 0) return true;

    vkDeviceWaitIdle(device_);
    destroySwapchain();
    if (!createSwapchain()) return false;
    framebuffers_.resize(swapViews_.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < swapViews_.size(); ++i) {
        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = slate_.windowRenderPass();
        fbci.attachmentCount = 1;
        fbci.pAttachments = &swapViews_[i];
        fbci.width = swapExtent_.width;
        fbci.height = swapExtent_.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(device_, &fbci, nullptr, &framebuffers_[i]) != VK_SUCCESS) return false;
    }
    window_->resetResizedFlag();
    return true;
}

VkCommandBuffer LauncherHost::beginOneTimeCmd() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool = cmdPool_;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &ai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void LauncherHost::endOneTimeCmd(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
}

bool LauncherHost::captureImageToPng(uint32_t imageIndex, const std::string& outPath) {
    if (!swapSupportsTransferSrc_ || imageIndex >= swapImages_.size()) return false;
    const uint32_t w = swapExtent_.width;
    const uint32_t h = swapExtent_.height;
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(w) * h * 4;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = byteSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(device_, &bci, nullptr, &staging) != VK_SUCCESS) return false;

    VkMemoryRequirements memReqs{};
    vkGetBufferMemoryRequirements(device_, staging, &memReqs);
    const uint32_t memIdx = findMemoryType(physDevice_, memReqs.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memIdx == UINT32_MAX) {
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memReqs.size;
    mai.memoryTypeIndex = memIdx;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(device_, &mai, nullptr, &mem) != VK_SUCCESS) {
        vkDestroyBuffer(device_, staging, nullptr);
        return false;
    }
    vkBindBufferMemory(device_, staging, mem, 0);

    VkCommandBuffer cmd = beginOneTimeCmd();
    VkImageMemoryBarrier toSrc{};
    toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image = swapImages_[imageIndex];
    toSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toSrc.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toSrc);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { w, h, 1 };
    vkCmdCopyImageToBuffer(cmd, swapImages_[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

    VkImageMemoryBarrier toPres = toSrc;
    toPres.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPres.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPres.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPres.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toPres);
    endOneTimeCmd(cmd);

    void* mapped = nullptr;
    vkMapMemory(device_, mem, 0, byteSize, 0, &mapped);
    std::vector<uint8_t> rgba(static_cast<size_t>(byteSize));
    std::memcpy(rgba.data(), mapped, static_cast<size_t>(byteSize));
    vkUnmapMemory(device_, mem);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, mem, nullptr);

    if (swapFormat_ == VK_FORMAT_B8G8R8A8_SRGB || swapFormat_ == VK_FORMAT_B8G8R8A8_UNORM) {
        for (size_t i = 0; i < rgba.size(); i += 4) {
            std::swap(rgba[i], rgba[i + 2]);
        }
    }

    fs::path out;
    if (!hpak::pathFromUtf8(outPath, out)) out = fs::path(outPath);
    std::error_code ec;
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
    const std::string outUtf8 = hpak::utf8Of(out);
    const int ok = stbi_write_png(outUtf8.c_str(), static_cast<int>(w), static_cast<int>(h), 4, rgba.data(), static_cast<int>(w * 4));
    return ok != 0;
}

int LauncherHost::run(const std::function<void(float dt)>& onTick,
                      const std::function<bool()>& shouldFinishEarly) {
    uint32_t frameIdx = 0;
    uint32_t drawnFrames = 0;
    uint32_t lastImageIndex = 0;
    auto prevTime = std::chrono::steady_clock::now();

    while (!window_->shouldClose()) {
        GlfwPlatform::pumpEvents(false);
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - prevTime).count();
        prevTime = now;

        const bool wasAsyncDone = (!shouldFinishEarly || shouldFinishEarly());
        if (onTick) onTick(dt);

        if (window_->isIconified()) {
            continue;
        }
        if (window_->wasResized() || swapchain_ == VK_NULL_HANDLE) {
            if (!recreateSwapchain()) return 1;
            if (swapchain_ == VK_NULL_HANDLE) continue;
        }

        vkWaitForFences(device_, 1, &inFlightFences_[frameIdx], VK_TRUE, UINT64_MAX);

        uint32_t imgIdx = 0;
        const VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                                   imageAvail_[frameIdx], VK_NULL_HANDLE, &imgIdx);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!recreateSwapchain()) return 1;
            continue;
        }

        vkResetFences(device_, 1, &inFlightFences_[frameIdx]);
        VkCommandBuffer cmd = cmds_[frameIdx];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &bi);

        slate_.beginFrame(frameIdx);
        HSlateDrawList drawList;
        slate_.buildWindow(slate_.context(), swapExtent_, drawList);
        slate_.flushFontUploads();
        slate_.recordWindow(cmd, drawList, framebuffers_[imgIdx], swapExtent_, frameIdx);
        vkEndCommandBuffer(cmd);

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &imageAvail_[frameIdx];
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &renderDone_[imgIdx];

        if (vkQueueSubmit(graphicsQueue_, 1, &si, inFlightFences_[frameIdx]) != VK_SUCCESS) {
            return 1;
        }

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &renderDone_[imgIdx];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain_;
        pi.pImageIndices = &imgIdx;
        const VkResult pres = vkQueuePresentKHR(presentQueue_, &pi);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain();
        }

        lastImageIndex = imgIdx;
        frameIdx = (frameIdx + 1) % kFramesInFlight;
        ++drawnFrames;

        const bool frameLimitHit = (opts_.maxFrames > 0 && drawnFrames >= opts_.maxFrames);
        if (frameLimitHit && wasAsyncDone) {
            break;
        }
    }

    vkDeviceWaitIdle(device_);

    if (!opts_.uiDumpPath.empty() && swapExtent_.width > 0 && swapExtent_.height > 0) {
        slate_.dumpElements(glm::vec2(static_cast<float>(swapExtent_.width),
                                      static_cast<float>(swapExtent_.height)),
                            opts_.uiDumpPath);
    }
    if (!opts_.capturePath.empty() && drawnFrames > 0) {
        captureImageToPng(lastImageIndex, opts_.capturePath);
    }

    return 0;
}
