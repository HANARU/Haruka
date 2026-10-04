#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include "../memory/HarukaAllocator.hpp"

inline std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f.is_open()) throw std::runtime_error("Cannot open shader: " + path);
    size_t sz = (size_t)f.tellg();
    std::vector<char> buf(sz);
    f.seekg(0); f.read(buf.data(), sz);
    return buf;
}

// ── RenderContext ─────────────────────────────────────────────────────────────
// 디바이스 핸들 번들 + 저수준 생성/배리어 헬퍼 (RendererRefactor.md §3-①).
// Renderer가 init 중에 필드를 채우고 소유하며, 패스/모듈은 참조로 받아 쓴다.
// 핸들 필드는 init 이후 불변, extent/swapFormat만 리사이즈 시 갱신된다.
class RenderContext {
public:
    VkDevice         device    = VK_NULL_HANDLE;
    VkPhysicalDevice physDev   = VK_NULL_HANDLE;
    VkQueue          gfxQueue  = VK_NULL_HANDLE;
    VkCommandPool    cmdPool   = VK_NULL_HANDLE;
    HarukaAllocator* allocator = nullptr;
    VkPipelineCache  pipelineCache = VK_NULL_HANDLE;

    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_4_BIT;
    VkFormat   swapFormat  = VK_FORMAT_UNDEFINED;
    VkFormat   depthFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};   // 스왑체인(창) 크기 — UI·post 최종 합성 대상

    // 씬을 그리는 3D 뷰포트 크기와 스왑체인 내 오프셋입니다(Viewport_Render_Design.md).
    // 씬 타깃(HDR·깊이·AO·Bloom)과 세 투영은 renderExtent 를 기준으로 삼고,
    // post 는 renderExtent HDR 을 스왑체인의 viewportOffset+renderExtent 서브렉트에 합성합니다.
    // 기본값은 extent 와 같아(오프셋 0), 에디터가 setViewportRect 로 바꾸기 전까지 전체 렌더입니다.
    VkExtent2D renderExtent{};
    VkOffset2D viewportOffset{ 0, 0 };

    // ── 생성 헬퍼 ─────────────────────────────────────────────────────────────
    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    // Zone B에서 이미지 생성 + 바인드 (outAlloc으로 할당 반환)
    VkImage createImage(uint32_t w, uint32_t h, uint32_t mips,
                        uint32_t layers, VkSampleCountFlagBits samples,
                        VkFormat fmt, VkImageTiling tiling,
                        VkImageUsageFlags usage, VkImageCreateFlags flags,
                        HarukaAllocation& outAlloc) const;

    VkImageView createImageView(VkImage img, VkFormat fmt,
                                VkImageAspectFlags aspect,
                                uint32_t mips = 1, uint32_t layers = 1,
                                VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D) const;

    // post.vert + fragSpv 풀스크린 트라이앵글 PSO
    VkPipeline createFullscreenPipeline(VkRenderPass rp, uint32_t subpass,
                                        const char* fragSpv,
                                        VkPipelineLayout layout,
                                        bool enableBlend = false) const;

    uint32_t findMemType(uint32_t filter, VkMemoryPropertyFlags flags) const;

    // ── 배리어 ────────────────────────────────────────────────────────────────
    static void transitionImage(VkCommandBuffer cmd, VkImage img,
                                VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                                VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                uint32_t mips = 1, uint32_t layers = 1);

    // ── 1회성 커맨드 (제출 후 큐 idle 대기) ───────────────────────────────────
    VkCommandBuffer beginOneTimeCmd() const;
    void            endOneTimeCmd(VkCommandBuffer cmd) const;
};
