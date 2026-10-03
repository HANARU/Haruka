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

    // 프레임 슬롯 수. RenderTypes.hpp 의 MAX_FRAMES 와 같아야 합니다(Renderer_render.cpp 가
    // static_assert 로 확인합니다). record 의 frameIdx 로 슬롯별 정점 버퍼를 고릅니다.
    static constexpr uint32_t kFramesInFlight = 2;

    void init(VkDevice device, VkPhysicalDevice physDevice,
              VkFormat swapFormat,
              CmdBeginFn beginCmd, CmdEndFn endCmd,
              const std::string& fontPath = "");
    void cleanup();

    // Called by Renderer::setUIRenderer and recreateSwapChain
    // Creates/recreates per-swapchain-image framebuffers for the UI pass.
    void rebuildFramebuffers(VkExtent2D extent,
                             const std::vector<VkImageView>& imageViews);

    // 루트를 갈아 끼우면 이전 트리의 위젯은 곧 파괴됩니다(uiScale 변경 → buildWidgetTree
    // 경로). 누르는 도중이었다면 캡처는 주인 없는 상태로 남겨, 그 누름의 뗌이 새 트리의
    // 엉뚱한 위젯에 가지 않게 합니다.
    void setRootWidget(std::shared_ptr<HWidget> root) {
        root_ = std::move(root);
        captured_.reset();
    }

    // Per-frame: records UI render pass into cmd. Uses framebuffers_[imgIdx].
    void record(VkCommandBuffer cmd, uint32_t imgIdx,
                VkExtent2D extent, uint32_t frameIdx);

    // 배치만 다시 합니다. record 가 그리기 전에 부르고, 에디터가 입력을 보내기 전에도
    // 부릅니다. 같은 프레임에 리빌드된 위젯은 아직 배치되지 않아 크기가 0 이라, 입력보다
    // 먼저 배치하지 않으면 그 프레임의 클릭을 받지 못합니다(DEF-84).
    void arrange(glm::vec2 size);

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

    // Dynamic vertex buffer (HOST_VISIBLE). 프레임 슬롯마다 하나씩 둡니다. 하나를 같이 쓰면,
    // 렌더 루프는 지금 슬롯의 펜스만 기다리므로 직전 프레임의 UI 패스가 아직 읽는 정점을
    // 덮어씁니다(DEF-93, GeometryPass 의 DEF-40 과 같은 유형).
    VkBuffer       vertexBuf_[kFramesInFlight] = {};
    VkDeviceMemory vertexMem_[kFramesInFlight] = {};
    UIVertex*      vertexPtr_[kFramesInFlight] = {};

    // Fixed index buffer (quad pattern: 0,1,2, 0,2,3 repeated)
    VkBuffer       indexBuf_   = VK_NULL_HANDLE;
    VkDeviceMemory indexMem_   = VK_NULL_HANDLE;

    HFontAtlas               fonts_;
    std::shared_ptr<HWidget> root_;
    // 포인터 캡처. 위젯은 약한 참조로만 붙잡습니다. 캡처 중에 리빌드가 그 위젯을 파괴하는
    // 경로가 있어서(숫자 칸을 누른 채 Delete, 플레이 중 선택 액터 파괴), raw 포인터면 다음
    // 이벤트가 해제된 위젯을 호출합니다(DEF-76). captureActive_ 는 "누름이 아직 끝나지
    // 않았다"는 뜻이라, 위젯이 사라져도 그 누름의 뗌까지는 참으로 남습니다.
    std::weak_ptr<HWidget>   captured_;
    bool                     captureActive_ = false;
    void releaseCapture() { captured_.reset(); captureActive_ = false; }

    void createUIRenderPass();
    void destroyFramebuffers();
    void createPipeline();
    void createBuffers(CmdBeginFn beginCmd, CmdEndFn endCmd);
    void createWhiteTexture(CmdBeginFn beginCmd, CmdEndFn endCmd);

    void flush(VkCommandBuffer cmd, uint32_t imgIdx, VkExtent2D extent,
               std::vector<HDrawElement>& elements, uint32_t slot);

    void     pushDescriptor(VkCommandBuffer cmd, VkImageView view);
    uint32_t findMemType(VkMemoryRequirements reqs, VkMemoryPropertyFlags flags);
    VkShaderModule loadSpv(const std::string& path);
};
