#pragma once

#include "ui/slate/HSlateRenderer.hpp"
#include "platform/IWindow.hpp"

#include <volk.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class GlfwWindow;

struct LauncherHostOptions {
    bool hiddenWindow = false;
    uint32_t width = 1040;
    uint32_t height = 680;
    int preferredGpu = -1;
    uint32_t maxFrames = 0; // 0 = run until window closed
    std::string uiDumpPath;
    std::string capturePath;
    std::string culture;
    std::filesystem::path engineRoot;
    std::filesystem::path exeDir;
};

class LauncherHost {
public:
    LauncherHost();
    ~LauncherHost();

    LauncherHost(const LauncherHost&) = delete;
    LauncherHost& operator=(const LauncherHost&) = delete;

    bool init(const LauncherHostOptions& opts);
    void cleanup();

    int run(const std::function<void(float dt)>& onTick,
            const std::function<bool()>& shouldFinishEarly = {});

    HSlateRenderer& slateRenderer() { return slate_; }
    HSlateContext& slateContext() { return slate_.context(); }
    IWindow* window() const;
    double startupMs() const { return startupMs_; }

private:
    bool createInstance();
    bool pickPhysicalDevice();
    bool createDevice();
    bool createCommandResources();
    bool createSwapchain();
    void destroySwapchain();
    bool recreateSwapchain();
    bool captureImageToPng(uint32_t imageIndex, const std::string& outPath);

    VkCommandBuffer beginOneTimeCmd();
    void endOneTimeCmd(VkCommandBuffer cmd);

    LauncherHostOptions opts_;
    bool glfwAcquired_ = false;
    std::unique_ptr<GlfwWindow> window_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t graphicsFamily_ = UINT32_MAX;
    uint32_t presentFamily_ = UINT32_MAX;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    PFN_vkCmdPushDescriptorSetKHR pushDescriptorFn_ = nullptr;

    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    static constexpr uint32_t kFramesInFlight = HSlateRenderer::kFramesInFlight;
    VkCommandBuffer cmds_[kFramesInFlight]{};
    VkFence inFlightFences_[kFramesInFlight]{};
    VkSemaphore imageAvail_[kFramesInFlight]{};

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent_{};
    bool swapSupportsTransferSrc_ = false;
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> renderDone_;

    HSlateRenderer slate_;
    double startupMs_ = 0.0;
};
