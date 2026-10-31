#pragma once

// ── 배리어 기록 분기 (DEF-04) ─────────────────────────────────────────────────────────────────
// 엔진은 배리어를 synchronization2 형식(VkDependencyInfo)으로 만듭니다. 장치가 sync2 를 켰으면 KHR 함수
// (vkCmdPipelineBarrier2KHR)로 그대로 넘기고, 없으면 Vulkan 1.0 vkCmdPipelineBarrier 로 바꿔 기록합니다
// (사용자 결정 2026-10-31: sync2 가 있는 장치는 KHR, 없을 때만 1.0). 분기를 이 한곳에 모아 호출부가 늘어도 빠지는
// 곳이 없게 합니다.
//
// 1.0 변환표(SDK vulkan_core.h 의 VK_PIPELINE_STAGE_2_* · VK_ACCESS_2_* 와 대조):
//   단계
//     · 0~16번 비트(0x1FFFF, TOP ~ ALL_COMMANDS)는 1.0 과 값 · 뜻이 같아 그대로 둡니다.
//     · COPY · RESOLVE · BLIT · CLEAR → TRANSFER. 범위를 넓히기만 합니다.
//     · INDEX_INPUT · VERTEX_ATTRIBUTE_INPUT → VERTEX_INPUT.
//     · PRE_RASTERIZATION_SHADERS → VERTEX_SHADER. 엔진이 tessellation · geometry 기능을 켜지 않으므로 그 비트를
//       넣으면 1.0 에서 오히려 위반입니다.
//     · NONE 은 첫 범위에서 TOP_OF_PIPE, 둘째 범위에서 BOTTOM_OF_PIPE 입니다. 1.0 은 sync2 기능 없이 단계 0 을
//       금지합니다(VUID-vkCmdPipelineBarrier-srcStageMask-03937 · dstStageMask-03937). 규격상 첫 범위의 TOP 과 둘째
//       범위의 BOTTOM 은 NONE 과 같은 뜻이라 정확한 변환입니다.
//     · 그 밖의 비트(비디오 · 가속 구조 · 벤더)는 ALL_COMMANDS 로 바꾸고 정확하지 않다고 표시합니다.
//   접근
//     · 0~16번 비트는 그대로, NONE → 0.
//     · SHADER_SAMPLED_READ · SHADER_STORAGE_READ → SHADER_READ, SHADER_STORAGE_WRITE → SHADER_WRITE.
//     · 그 밖은 MEMORY_READ | MEMORY_WRITE(모든 단계가 지원)로 바꾸고 정확하지 않다고 표시합니다.
//     · 바꾼 접근은 바꾼 단계 중 하나 이상이 지원합니다(VUID-vkCmdPipelineBarrier-srcAccessMask-02815 · 02816).
//   레이아웃: ATTACHMENT_OPTIMAL · READ_ONLY_OPTIMAL 은 sync2 전용이라 바꾸지 않고 거부합니다.
//   단계 합치기: 1.0 은 호출 하나에 src · dst 단계 마스크가 하나씩이라 배리어별 단계를 OR 로 합칩니다(범위를 넓히기만
//     하므로 안전). NONE 은 합칠 때 0 으로 두고, 합이 0 일 때만 TOP 또는 BOTTOM 으로 바꿉니다.
//
// Vulkan 함수를 직접 부르지 않고 DeviceFns 의 포인터로만 부르므로 링크 없이 HarukaBarrierTest(barrier_compat)가
// 창 · 장치 없이 검산합니다.

#include "VulkanBaseline.hpp"

#include <array>
#include <cstdint>

namespace hsync {

enum class Scope { Src, Dst };

// 1.0 과 같은 비트(단계 · 접근 모두 0~16번).
inline constexpr uint64_t kSameBits = 0x1FFFFull;

namespace detail {

// NONE 을 0 으로 두는 변환(합치기용).
inline VkPipelineStageFlags stageBits(VkPipelineStageFlags2 s, bool& exact) {
    VkPipelineStageFlags  out  = (VkPipelineStageFlags)(s & kSameBits);
    VkPipelineStageFlags2 rest = s & ~(VkPipelineStageFlags2)kSameBits;
    constexpr VkPipelineStageFlags2 kTransfer = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                                                VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT;
    constexpr VkPipelineStageFlags2 kVertexIn = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                                                VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
    constexpr VkPipelineStageFlags2 kPreRast  = VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT;
    if (rest & kTransfer) { out |= VK_PIPELINE_STAGE_TRANSFER_BIT;      rest &= ~kTransfer; }
    if (rest & kVertexIn) { out |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;  rest &= ~kVertexIn; }
    if (rest & kPreRast)  { out |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT; rest &= ~kPreRast;  }
    exact = (rest == 0);
    if (rest) out |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    return out;
}

inline VkPipelineStageFlags noneToEnd(VkPipelineStageFlags s, Scope sc) {
    if (s != 0) return s;
    return sc == Scope::Src ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
}

} // namespace detail

inline VkPipelineStageFlags toStage1(VkPipelineStageFlags2 s, Scope sc, bool* exact = nullptr) {
    bool ok = true;
    const VkPipelineStageFlags out = detail::noneToEnd(detail::stageBits(s, ok), sc);
    if (exact) *exact = ok;
    return out;
}

inline VkAccessFlags toAccess1(VkAccessFlags2 a, bool* exact = nullptr) {
    VkAccessFlags  out  = (VkAccessFlags)(a & kSameBits);
    VkAccessFlags2 rest = a & ~(VkAccessFlags2)kSameBits;
    constexpr VkAccessFlags2 kRead  = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    constexpr VkAccessFlags2 kWrite = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    if (rest & kRead)  { out |= VK_ACCESS_SHADER_READ_BIT;  rest &= ~kRead;  }
    if (rest & kWrite) { out |= VK_ACCESS_SHADER_WRITE_BIT; rest &= ~kWrite; }
    if (exact) *exact = (rest == 0);
    if (rest) out |= VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    return out;
}

// sync2 전용 레이아웃이 아니면 true.
constexpr bool layoutOk1(VkImageLayout l) {
    return l != VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL && l != VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
}

// 호출 하나에 담을 수 있는 배리어 수(종류마다). 엔진은 지금 호출마다 1개를 씁니다.
inline constexpr uint32_t kMaxPerCall = 8;

struct Legacy {
    VkPipelineStageFlags srcStage = 0;
    VkPipelineStageFlags dstStage = 0;
    VkDependencyFlags    flags    = 0;
    uint32_t             memCount = 0, bufCount = 0, imgCount = 0;
    std::array<VkMemoryBarrier,       kMaxPerCall> mem{};
    std::array<VkBufferMemoryBarrier, kMaxPerCall> buf{};
    std::array<VkImageMemoryBarrier,  kMaxPerCall> img{};
    bool                 exact    = true;   // 표에 없는 비트를 넓혀 바꿨으면 false
};

enum class ConvertError { None, TooMany, LayoutNot1_0, NoFunction };

inline const char* toString(ConvertError e) {
    switch (e) {
        case ConvertError::None:         return "none";
        case ConvertError::TooMany:      return "too many barriers in one call";
        case ConvertError::LayoutNot1_0: return "image layout needs synchronization2";
        case ConvertError::NoFunction:   return "no barrier function";
    }
    return "unknown";
}

// 변환할 수 없으면 오류를 돌려주고 out 은 쓰지 않습니다.
inline ConvertError toLegacy(const VkDependencyInfo& di, Legacy& out) {
    if (di.memoryBarrierCount > kMaxPerCall || di.bufferMemoryBarrierCount > kMaxPerCall ||
        di.imageMemoryBarrierCount > kMaxPerCall)
        return ConvertError::TooMany;
    for (uint32_t i = 0; i < di.imageMemoryBarrierCount; ++i) {
        const VkImageMemoryBarrier2& b = di.pImageMemoryBarriers[i];
        if (!layoutOk1(b.oldLayout) || !layoutOk1(b.newLayout)) return ConvertError::LayoutNot1_0;
    }

    Legacy l;
    l.flags = di.dependencyFlags;
    bool ok = true;
    auto stages = [&](VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst) {
        bool e1 = true, e2 = true;
        l.srcStage |= detail::stageBits(src, e1);
        l.dstStage |= detail::stageBits(dst, e2);
        ok = ok && e1 && e2;
    };
    auto access = [&](VkAccessFlags2 a) {
        bool e = true;
        const VkAccessFlags r = toAccess1(a, &e);
        ok = ok && e;
        return r;
    };
    for (uint32_t i = 0; i < di.memoryBarrierCount; ++i) {
        const VkMemoryBarrier2& b = di.pMemoryBarriers[i];
        VkMemoryBarrier& o = l.mem[l.memCount++];
        o.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        o.pNext         = nullptr;
        o.srcAccessMask = access(b.srcAccessMask);
        o.dstAccessMask = access(b.dstAccessMask);
        stages(b.srcStageMask, b.dstStageMask);
    }
    for (uint32_t i = 0; i < di.bufferMemoryBarrierCount; ++i) {
        const VkBufferMemoryBarrier2& b = di.pBufferMemoryBarriers[i];
        VkBufferMemoryBarrier& o = l.buf[l.bufCount++];
        o.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        o.pNext               = nullptr;
        o.srcAccessMask       = access(b.srcAccessMask);
        o.dstAccessMask       = access(b.dstAccessMask);
        o.srcQueueFamilyIndex = b.srcQueueFamilyIndex;
        o.dstQueueFamilyIndex = b.dstQueueFamilyIndex;
        o.buffer              = b.buffer;
        o.offset              = b.offset;
        o.size                = b.size;
        stages(b.srcStageMask, b.dstStageMask);
    }
    for (uint32_t i = 0; i < di.imageMemoryBarrierCount; ++i) {
        const VkImageMemoryBarrier2& b = di.pImageMemoryBarriers[i];
        VkImageMemoryBarrier& o = l.img[l.imgCount++];
        o.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        o.pNext               = nullptr;
        o.srcAccessMask       = access(b.srcAccessMask);
        o.dstAccessMask       = access(b.dstAccessMask);
        o.oldLayout           = b.oldLayout;
        o.newLayout           = b.newLayout;
        o.srcQueueFamilyIndex = b.srcQueueFamilyIndex;
        o.dstQueueFamilyIndex = b.dstQueueFamilyIndex;
        o.image               = b.image;
        o.subresourceRange    = b.subresourceRange;
        stages(b.srcStageMask, b.dstStageMask);
    }
    l.srcStage = detail::noneToEnd(l.srcStage, Scope::Src);
    l.dstStage = detail::noneToEnd(l.dstStage, Scope::Dst);
    l.exact    = ok;
    out = l;
    return ConvertError::None;
}

// sync2 로 기록하면 di 를 그대로 넘기고, 아니면 1.0 으로 바꿔 기록합니다. 변환 오류면 아무것도 기록하지 않습니다.
// exact 가 있으면 변환이 정확했는지 담습니다(sync2 경로는 언제나 true).
inline ConvertError record(const hvk::DeviceFns& fns, VkCommandBuffer cmd, const VkDependencyInfo& di,
                           bool* exact = nullptr) {
    if (exact) *exact = true;
    if (fns.sync2 && fns.barrier2) {
        fns.barrier2(cmd, &di);
        return ConvertError::None;
    }
    if (!fns.barrier1) return ConvertError::NoFunction;
    Legacy l;
    const ConvertError e = toLegacy(di, l);
    if (e != ConvertError::None) return e;
    if (exact) *exact = l.exact;
    fns.barrier1(cmd, l.srcStage, l.dstStage, l.flags,
                 l.memCount, l.memCount ? l.mem.data() : nullptr,
                 l.bufCount, l.bufCount ? l.buf.data() : nullptr,
                 l.imgCount, l.imgCount ? l.img.data() : nullptr);
    return ConvertError::None;
}

} // namespace hsync
