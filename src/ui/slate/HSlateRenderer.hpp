#pragma once
#include "HWidget.hpp"
#include "HFontAtlas.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <functional>

// ── HSlateRenderer ────────────────────────────────────────────────────────────
// Vulkan 2D pipeline — renders HWidget tree via its own VkRenderPass.
// Renderer::drawFrame calls record() after the 3D render pass ends.
class HSlateRenderer {
public:
    using CmdBeginFn = std::function<VkCommandBuffer()>;
    using CmdEndFn   = std::function<void(VkCommandBuffer)>;

    void init(VkDevice device, VkPhysicalDevice physDevice,
              VkFormat swapFormat,
              CmdBeginFn beginCmd, CmdEndFn endCmd,
              const std::string& fontPath = "");
    void cleanup();

    // Called by Renderer::setUIRenderer and recreateSwapChain
    // Creates/recreates per-swapchain-image framebuffers for the UI pass.
    void rebuildFramebuffers(VkExtent2D extent,
                             const std::vector<VkImageView>& imageViews);

    // 루트를 갈아 끼우면 이전 트리의 위젯은 곧 파괴됩니다. capturedWidget_ 는 raw
    // 포인터라 그대로 두면 해제된 위젯을 가리킵니다(uiScale 변경 → buildWidgetTree 경로).
    void setRootWidget(std::shared_ptr<HWidget> root) {
        root_           = std::move(root);
        capturedWidget_ = nullptr;
    }

    // Per-frame: records UI render pass into cmd. Uses framebuffers_[imgIdx].
    void record(VkCommandBuffer cmd, uint32_t imgIdx,
                VkExtent2D extent, uint32_t frameIdx);

    HReply handlePointerEvent(HPointerEvent e);
    HFontAtlas& fontAtlas() { return fonts_; }

private:
    static constexpr uint32_t kMaxVerts = 65536;
    static constexpr uint32_t kMaxQuads = kMaxVerts / 4;

    struct UIVertex { glm::vec2 pos, uv; glm::vec4 color; };

    VkDevice         device_     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    VkFormat         colorFmt_   = VK_FORMAT_UNDEFINED;

    // UI-dedicated render pass (LOAD_OP_LOAD, finalLayout=PRESENT_SRC_KHR)
    VkRenderPass               uiRenderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    // 2D pipeline
    VkPipeline            pipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descSetLayout_  = VK_NULL_HANDLE;

    // Sampler + 1×1 white texture (solid-colour rect fallback)
    VkSampler      sampler_    = VK_NULL_HANDLE;
    VkImage        whiteImage_ = VK_NULL_HANDLE;
    VkDeviceMemory whiteMem_   = VK_NULL_HANDLE;
    VkImageView    whiteView_  = VK_NULL_HANDLE;

    // Dynamic vertex buffer (HOST_VISIBLE, per-frame overwrite)
    VkBuffer       vertexBuf_  = VK_NULL_HANDLE;
    VkDeviceMemory vertexMem_  = VK_NULL_HANDLE;
    UIVertex*      vertexPtr_  = nullptr;

    // Fixed index buffer (quad pattern: 0,1,2, 0,2,3 repeated)
    VkBuffer       indexBuf_   = VK_NULL_HANDLE;
    VkDeviceMemory indexMem_   = VK_NULL_HANDLE;

    HFontAtlas               fonts_;
    std::shared_ptr<HWidget> root_;
    HWidget*                 capturedWidget_ = nullptr;

    void createUIRenderPass();
    void destroyFramebuffers();
    void createPipeline();
    void createBuffers(CmdBeginFn beginCmd, CmdEndFn endCmd);
    void createWhiteTexture(CmdBeginFn beginCmd, CmdEndFn endCmd);

    void flush(VkCommandBuffer cmd, uint32_t imgIdx, VkExtent2D extent,
               std::vector<HDrawElement>& elements);

    void     pushDescriptor(VkCommandBuffer cmd, VkImageView view);
    uint32_t findMemType(VkMemoryRequirements reqs, VkMemoryPropertyFlags flags);
    VkShaderModule loadSpv(const std::string& path);
};
