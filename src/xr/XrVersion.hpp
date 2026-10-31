#pragma once

// ── OpenXR 버전 → Vulkan 버전 (DEF-97) ────────────────────────────────────────────────────────
// xrGetVulkanGraphicsRequirements2KHR 는 Vulkan 범위를 XrVersion 으로 줍니다. XrVersion 은 major 16 · minor 16 ·
// patch 32 비트이고, Vulkan 버전은 variant 3 · major 7 · minor 10 · patch 12 비트라 그대로 비교하면 틀립니다.
// patch 는 버리고(범위 비교는 major · minor 만, VulkanBaseline.hpp 의 checkApiRange) Vulkan 칸을 넘는 값은 칸의 최댓값으로
// 자릅니다. 최댓값을 아주 크게 알려 주는 런타임이 있어도 "제한 없음" 으로 남습니다.
//
// OpenXR 헤더 없이 비트 연산만 하므로 시험(vulkan_baseline)이 OpenXR 없이 포함합니다.

#include <vulkan/vulkan.h>

#include <cstdint>

namespace hxr {

constexpr uint32_t vkApiFromXrVersion(uint64_t v) {
    const uint64_t major = v >> 48;
    const uint64_t minor = (v >> 32) & 0xFFFFull;
    return VK_MAKE_API_VERSION(0, (uint32_t)(major > 0x7F ? 0x7F : major), (uint32_t)(minor > 0x3FF ? 0x3FF : minor), 0);
}

} // namespace hxr
