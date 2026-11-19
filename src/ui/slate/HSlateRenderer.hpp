#pragma once
#include "HSlateContext.hpp"
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

    // 월드 패널(3D UI) 텍스처 형식. sRGB 라 에디터 스왑체인과 같은 색으로 저장되고, 블렌딩은 선형에서 합니다.
    static constexpr VkFormat kPanelFormat = VK_FORMAT_R8G8B8A8_SRGB;

    // pushDescriptor 는 렌더러가 장치에서 받은 vkCmdPushDescriptorSetKHR 입니다(Renderer::pushDescriptorFn, DEF-97). UI 는
    // 렌더러 헤더에 기대지 않는 별도 모듈이라 함수 포인터만 받습니다.
    // swapFormat 이 VK_FORMAT_UNDEFINED 이면 스왑체인 출력(에디터 오버레이)을 만들지 않고 패널 출력만 둡니다
    // (에디터가 없는 게임 실행 파일, 2026-11-16 3D UI P1a).
    void init(VkDevice device, VkPhysicalDevice physDevice,
              VkFormat swapFormat, PFN_vkCmdPushDescriptorSetKHR pushDescriptor,
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
    void setRootWidget(std::shared_ptr<HWidget> root) { ctx_.setRootWidget(std::move(root)); }

    // Per-frame: records UI render pass into cmd. Uses framebuffers_[imgIdx].
    // UI 렌더 패스를 실제로 기록했으면 true 입니다. 그 패스의 finalLayout 이 스왑체인 이미지를 표시 레이아웃으로 바꾸므로,
    // false 면(루트가 없거나 접힘, 프레임버퍼 없음, 그릴 것 없음) 렌더러가 대신 바꿔야 합니다(DEF-120).
    bool record(VkCommandBuffer cmd, uint32_t imgIdx,
                VkExtent2D extent, uint32_t frameIdx);

    // 배치만 다시 합니다. record 가 그리기 전에 부르고, 에디터가 입력을 보내기 전에도
    // 부릅니다. 같은 프레임에 리빌드된 위젯은 아직 배치되지 않아 크기가 0 이라, 입력보다
    // 먼저 배치하지 않으면 그 프레임의 클릭을 받지 못합니다(DEF-84).
    void arrange(glm::vec2 size) { ctx_.arrange(size); }

    HReply handlePointerEvent(HPointerEvent e) { return ctx_.handlePointerEvent(e); }
    HFontAtlas& fontAtlas() { return fonts_; }
    VkImageView whiteView() const { return whiteView_; }
    HSlateContext& context() { return ctx_; }

    // CPU 그리기 목록 생성(테스트 및 P0 분리용)
    bool buildDrawList(glm::vec2 size, VkImageView whiteView, HSlateDrawList& out) {
        return ctx_.buildDrawList(size, fonts_, whiteView, kMaxVerts, out);
    }
    // S6-0: 에디터 UI HDrawElement 목록을 텍스트 파일로 덤프합니다.
    bool dumpElements(glm::vec2 size, const std::string& path);
    static constexpr uint32_t maxVerts() { return kMaxVerts; }

    // 슬롯 정점 오프셋을 0 으로 돌립니다. 한 프레임에 에디터 오버레이와 패널 여럿이 같은 슬롯 버퍼에
    // 이어 쓰므로, 렌더러가 프레임마다 그 슬롯의 첫 UI 기록보다 먼저 한 번 부릅니다(P1a 에서 record 밖으로 옮김).
    void beginFrame(uint32_t slot) { slotVertOffset_[slot % kFramesInFlight] = 0; }
    uint32_t slotVertexOffset(uint32_t slot) const { return slotVertOffset_[slot % kFramesInFlight]; }

    // 슬롯 정점 오프셋 계산: 호출할 때마다 이전 오프셋 뒤에 이어 씁니다.
    static uint32_t advanceSlotVertexOffset(uint32_t& slotOffset, uint32_t vertCount) {
        const uint32_t base = slotOffset;
        slotOffset += vertCount;
        return base;
    }

    // 지정된 렌더 패스 · 프레임버퍼에 그리기 목록을 기록합니다(P1 패널 출력 공용).
    // 정점 예산을 넘기면 경고를 한 번 남기고 렌더 패스를 열지 않은 채 false 를 돌려줍니다.
    bool recordDrawList(VkCommandBuffer cmd, VkRenderPass renderPass,
                        VkFramebuffer framebuffer, VkExtent2D extent,
                        const HSlateDrawList& drawList, uint32_t frameIdx);

    // ── 패널 출력(3D UI P1a) ──────────────────────────────────────────────────
    // 패널 렌더 패스: kPanelFormat 1샘플, 투명(0,0,0,0)으로 지우고 시작해 끝나면 TRANSFER_SRC_OPTIMAL 로 둡니다.
    // 렌더러가 이어서 밉을 만들고 셰이더가 읽는 배치로 옮깁니다. 프레임버퍼는 이 패스로 렌더러가 만듭니다.
    VkRenderPass panelRenderPass() const { return panelRenderPass_; }

    // 문맥 ctx 의 위젯 트리를 extent 크기로 배치해 그리기 목록을 만듭니다(GPU 기록 없음). 새 글자가 생겼으면 아틀라스를
    // 올립니다. 그릴 것이 없으면 false 이고 out 은 빈 목록입니다. 렌더러는 이 목록의 해시(drawListHash)를 직전과 비교해
    // 바뀐 패널만 recordPanel 로 다시 그립니다(3D UI 구간 2: 자동 다시 그리기).
    bool buildPanel(HSlateContext& ctx, VkExtent2D extent, HSlateDrawList& out);
    // buildPanel 이 만든 목록을 패널 프레임버퍼에 그립니다. 결과 색은 알파를 곱한 값(premultiplied)이라 월드에서는
    // ONE · ONE_MINUS_SRC_ALPHA 로 섞고, 밉으로 줄여도 가장자리가 어두워지지 않습니다. 목록이 비었거나 정점 예산을
    // 넘어도 렌더 패스는 열어 투명하게 지웁니다(이미지 배치가 늘 TRANSFER_SRC 로 끝나야 하므로). 목록대로 그렸으면(빈 목록 포함)
    // true, 정점 예산을 넘어 지우기만 했으면 false(다음 프레임에 다시 그려야 함).
    bool recordPanel(VkCommandBuffer cmd, const HSlateDrawList& drawList, VkFramebuffer framebuffer,
                     VkExtent2D extent, uint32_t frameIdx);

private:
    // 그리기 목록 기록 공용 본문. clear 가 참이면 패스의 지우기 값(투명)을 넘깁니다.
    bool recordPass(VkCommandBuffer cmd, VkRenderPass renderPass, VkPipeline pipeline,
                    VkFramebuffer framebuffer, VkExtent2D extent, bool clear,
                    const HSlateDrawList& drawList, uint32_t slot);
    VkPipeline buildPipeline(VkRenderPass renderPass, bool premultipliedTarget);
    void createPanelRenderPass();

    static constexpr uint32_t kMaxVerts = 65536;
    static constexpr uint32_t kMaxQuads = kMaxVerts / 4;

    VkDevice         device_     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    VkFormat         colorFmt_   = VK_FORMAT_UNDEFINED;
    PFN_vkCmdPushDescriptorSetKHR pushDescriptorFn_ = nullptr;

    // UI-dedicated render pass (LOAD_OP_LOAD, finalLayout=PRESENT_SRC_KHR)
    VkRenderPass               uiRenderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    // 패널 출력 렌더 패스 · 파이프라인(알파를 덧쌓는 블렌딩)
    VkRenderPass panelRenderPass_ = VK_NULL_HANDLE;
    VkPipeline   panelPipeline_   = VK_NULL_HANDLE;

    // 2D pipeline
    VkPipeline            pipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descSetLayout_  = VK_NULL_HANDLE;

    // Sampler + 1×1 white texture (solid-color rect fallback)
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

    HFontAtlas    fonts_;
    HSlateContext ctx_;
    uint32_t      slotVertOffset_[kFramesInFlight] = {};

    void createUIRenderPass();
    void destroyFramebuffers();
    void createPipeline();
    void createBuffers(CmdBeginFn beginCmd, CmdEndFn endCmd);
    void createWhiteTexture(CmdBeginFn beginCmd, CmdEndFn endCmd);

    void     pushDescriptor(VkCommandBuffer cmd, VkImageView view);
    uint32_t findMemType(VkMemoryRequirements reqs, VkMemoryPropertyFlags flags);
    VkShaderModule loadSpv(const std::string& path);
};
