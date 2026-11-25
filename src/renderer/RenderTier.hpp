#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

// ── 렌더 티어 (Docs/Quest_Rendering_Design.md 5.1절) ────────────────────────────
// Desktop: 기존 깊이 전처리 + 후처리(AO · 블룸) 경로.
// Mobile : Quest(Android) 기본 경로. 깊이 전처리를 끄고(Adreno LRZ 활용), 깊이 첨부를
//          타일 안(DONT_CARE · TRANSIENT · LazyPreferred)으로 제한하며 AO · 기본 블룸을 끕니다.
enum class RenderTier : uint8_t {
    Desktop = 0,
    Mobile  = 1,
};

inline constexpr RenderTier defaultRenderTier(bool isAndroid) {
    return isAndroid ? RenderTier::Mobile : RenderTier::Desktop;
}

inline constexpr RenderTier platformDefaultRenderTier() {
#ifdef __ANDROID__
    return defaultRenderTier(true);
#else
    return defaultRenderTier(false);
#endif
}

inline bool parseRenderTier(std::string_view s, RenderTier& out) {
    if (s == "desktop") { out = RenderTier::Desktop; return true; }
    if (s == "mobile")  { out = RenderTier::Mobile;  return true; }
    return false;
}

inline const char* renderTierName(RenderTier t) {
    return t == RenderTier::Mobile ? "mobile" : "desktop";
}

inline RenderTier resolveRenderTier(const std::optional<RenderTier>& cliOverride, bool isAndroid) {
    return cliOverride.has_value() ? *cliOverride : defaultRenderTier(isAndroid);
}

// 메인 렌더 패스 안의 확장 지점(OpaqueEnd · TransparentEnd)에 넘길 실제 Vulkan 서브패스 번호.
// Mobile 은 서브패스 하나(0)만 쓰며, Desktop 은 깊이 전처리(0) 다음 서브패스(1, DEF-183 이후 불투명 · 반투명 공용)입니다.
inline constexpr uint32_t mainPassSubpass(RenderTier tier, bool transparentEnd) {
    (void)transparentEnd;
    return (tier == RenderTier::Mobile) ? 0u : 1u;
}
