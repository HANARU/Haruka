#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include "../memory/HarukaAllocator.hpp"
#include "../vfs/Vfs.hpp"
#include "BarrierCompat.hpp"
#include "VulkanBaseline.hpp"

// 셰이더 바이너리를 논리 경로("shaders/pbr.frag.spv")로 읽습니다. 가상 파일 시스템이 실행 폴더의 낱개 파일이나 묶음
// 파일(game.hpak)에서 찾습니다(2026-10-24). 예전에는 작업 폴더 기준 파일이었습니다.
inline std::vector<char> readFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    std::string err;
    if (!hvfs::readAll(path, bytes, &err)) throw std::runtime_error("Cannot open shader: " + err);
    return std::vector<char>(bytes.begin(), bytes.end());
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
    // 장치가 켠 기능과 확장 함수(2026-10-31, VulkanBaseline.hpp). 렌더러가 장치를 만든 뒤 한 번 채웁니다. push descriptor
    // 와 배리어는 이 포인터와 아래 래퍼로만 부릅니다. 1.2 이상 코어 이름(vkCmdPipelineBarrier2 · vkCmdPushDescriptorSet)은
    // 1.1 장치(Quest 2 · Pro)와 Android API 33 · 36 미만의 libvulkan 에 없습니다.
    hvk::DeviceFns   vk{};

    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_4_BIT;
    VkFormat   swapFormat  = VK_FORMAT_UNDEFINED;
    VkFormat   depthFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};   // 스왑체인(창) 크기 — UI·post 최종 합성 대상

    // 씬을 합성할 3D 뷰포트의 스왑체인 내 위치와 크기입니다(Viewport_Render_Design.md).
    // 기본값은 창 전체(오프셋 0)이고, 에디터가 setViewportRect 로 바꿉니다.
    VkOffset2D viewportOffset{ 0, 0 };
    VkExtent2D viewportExtent{};
    // 눈 하나의 씬 타깃(HDR·깊이·AO·Bloom) 크기이자 투영 비율의 기준입니다. 단안이면 뷰포트와
    // 같고, 스테레오 흉내 모드는 뷰포트의 좌우 절반, XR 은 런타임이 권하는 눈 크기입니다(DEF-09).
    VkExtent2D renderExtent{};

    // ── 생성 헬퍼 ─────────────────────────────────────────────────────────────
    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    // Zone B 에 이미지 생성 + 바인드 (outAlloc 으로 할당 반환). LazyPreferred 이면 지연 할당 메모리를 먼저 시도합니다
    // (TRANSIENT 첨부 용도 필수, DEF-06). 어느 쪽이든 freeB 로 돌려줍니다. 실패하면 만든 이미지를 지우고 던집니다.
    VkImage createImage(uint32_t w, uint32_t h, uint32_t mips,
                        uint32_t layers, VkSampleCountFlagBits samples,
                        VkFormat fmt, VkImageTiling tiling,
                        VkImageUsageFlags usage, VkImageCreateFlags flags,
                        HarukaAllocation& outAlloc, ImageMemory mem = ImageMemory::ZoneB) const;

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

    // ── 배리어 (DEF-04) ───────────────────────────────────────────────────────
    // synchronization2 형식으로 적으면 장치에 맞게 기록합니다: sync2 가 있으면 vkCmdPipelineBarrier2KHR, 없으면(또는
    // --vk-no-sync2) Vulkan 1.0 vkCmdPipelineBarrier 로 바꿔서(BarrierCompat.hpp). vkCmdPipelineBarrier2 를 직접 부르지
    // 마십시오. 1.0 로 바꿀 수 없는 배리어(sync2 전용 레이아웃, 종류마다 8개 초과)는 프로그래머 오류라 예외입니다.
    void transitionImage(VkCommandBuffer cmd, VkImage img,
                         VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                         VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                         VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                         uint32_t mips = 1, uint32_t layers = 1) const;
    void pipelineBarrier(VkCommandBuffer cmd, const VkDependencyInfo& di) const;
    void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                       VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) const;

    // ── 1회성 커맨드 (제출 후 큐 idle 대기) ───────────────────────────────────
    VkCommandBuffer beginOneTimeCmd() const;
    void            endOneTimeCmd(VkCommandBuffer cmd) const;
};
