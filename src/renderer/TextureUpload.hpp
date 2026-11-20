#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdint>

// ── 압축 텍스처 업로드 판단 (2026-11-21, Docs/Texture_Compression.md D10 · D11) ─────────────────────────
// 미리 압축한 KTX2 를 그대로 올릴지, RGBA8 원본(블릿 밉)으로 대체할지 정합니다. 장치 기능(textureCompressionBC ·
// textureCompressionASTC_LDR)이 켜졌고 형식이 샘플링 · 선형 필터를 지원해야 압축 경로입니다. Vulkan 함수를 부르지 않으므로
// 시험(HarukaTexTest)이 장치 없이 검산합니다.
namespace htexup {

enum class Family : uint8_t { None, BC, ASTC };

inline Family family(uint32_t vkFormat) {
    if (vkFormat >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && vkFormat <= VK_FORMAT_BC7_SRGB_BLOCK) return Family::BC;
    if (vkFormat >= VK_FORMAT_ASTC_4x4_UNORM_BLOCK && vkFormat <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK) return Family::ASTC;
    return Family::None;
}

// 장치 생성 때 켠 기능(bcEnabled · astcEnabled)과 그 형식의 optimalTilingFeatures 로 압축 경로를 쓸 수 있는지.
inline bool canSample(uint32_t vkFormat, bool bcEnabled, bool astcEnabled, VkFormatFeatureFlags optimalTilingFeatures) {
    const Family f = family(vkFormat);
    const bool feature = (f == Family::BC && bcEnabled) || (f == Family::ASTC && astcEnabled);
    constexpr VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return feature && (optimalTilingFeatures & need) == need;
}

// RGBA8 로 levels 단계를 올렸을 때의 바이트(메모리 비교 기준, D12).
inline uint64_t rgbaChainBytes(uint32_t w, uint32_t h, uint32_t levels) {
    uint64_t sum = 0;
    for (uint32_t i = 0; i < levels; ++i)
        sum += (uint64_t)std::max(1u, w >> i) * std::max(1u, h >> i) * 4u;
    return sum;
}

} // namespace htexup
