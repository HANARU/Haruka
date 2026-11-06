#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdint>

// ── 텍스처 밉 사슬 계산 (2026-11-06, DEF-165) ─────────────────────────────────────
// 머티리얼 텍스처는 예전에 밉 1단계로만 올라가, 멀리 있거나 비스듬한 표면에서 화소가 반짝이고(에일리어싱) 큰 텍스처를
// 그대로 읽어 대역폭을 낭비했습니다. VR 은 머리가 늘 조금씩 움직여 이 반짝임이 특히 잘 보입니다. 샘플러는 이미 밉 선형
// 보간 · maxLod 16 이라, 이미지에 밉 단계만 있으면 셰이더를 바꾸지 않고 효과가 납니다.
//
// 만드는 방식은 업로드할 때 GPU 블릿입니다(Filament 의 glTF 로더 · Vulkan 예제와 같은 방식). UE · Unity · Godot 처럼 가져올
// 때 미리 만들어 압축과 함께 저장하는 방식은 텍스처 압축(ASTC · KTX2)을 들일 때 옮깁니다. 블릿은 sRGB 원본을 선형으로
// 바꿔 거른 뒤 다시 sRGB 로 쓰므로 기본색의 감마도 맞게 줄어듭니다. 노멀 맵은 UNORM 값의 평균이라 벡터가 조금 짧아지지만
// pbr.frag 가 정규화합니다.
//
// 이 헤더는 Vulkan 을 부르지 않습니다(형과 비트만 씀). 시험(RenderExtensionTest)이 장치 없이 검산합니다.
namespace hmip {

// 밉 단계 수: 큰 변을 1 이 될 때까지 반으로 줄인 횟수 + 1. 0 은 1 로 봅니다.
inline uint32_t levelCount(uint32_t w, uint32_t h) {
    uint32_t s = std::max(w, h);
    uint32_t n = 1;
    while (s > 1) { s >>= 1; ++n; }
    return n;
}

// level 단계의 한 변 크기. Vulkan 규칙(max(1, base >> level))과 같습니다.
inline uint32_t extent(uint32_t base, uint32_t level) {
    return level >= 32 ? 1u : std::max(1u, base >> level);
}

// 블릿으로 밉을 만들 수 있는 형식인지. 같은 이미지가 원본이자 대상이고 선형 필터로 줄이므로 세 비트가 모두 있어야 합니다.
// R8G8B8A8 UNORM · SRGB 는 Vulkan 필수 지원 형식이지만, 장치가 알려 준 값으로 확인하고 없으면 1단계로 올립니다.
inline bool canBlitMips(VkFormatFeatureFlags optimalTilingFeatures) {
    constexpr VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (optimalTilingFeatures & need) == need;
}

} // namespace hmip
