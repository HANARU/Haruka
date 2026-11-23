#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cstdint>

namespace HSlateTheme {

namespace detail {
inline constexpr double kLn2 = 0.69314718055994530942;

constexpr double expSeries(double y) {
    int m = 0;
    while (y >  0.5 * kLn2) { y -= kLn2; ++m; }
    while (y < -0.5 * kLn2) { y += kLn2; --m; }
    double term = 1.0;
    double sum  = 1.0;
    for (int n = 1; n <= 16; ++n) {
        term *= y / (double)n;
        sum  += term;
    }
    while (m > 0) { sum *= 2.0; --m; }
    while (m < 0) { sum *= 0.5; ++m; }
    return sum;
}

constexpr double lnSeries(double x) {
    int k = 0;
    while (x < 0.5) { x *= 2.0; --k; }
    while (x > 1.0) { x *= 0.5; ++k; }
    const double u  = (x - 1.0) / (x + 1.0);
    const double u2 = u * u;
    double p   = u;
    double sum = 0.0;
    for (int n = 1; n <= 17; n += 2) {
        sum += p / (double)n;
        p   *= u2;
    }
    return 2.0 * sum + (double)k * kLn2;
}

constexpr float srgbByteToLinear(uint8_t byte) {
    const double c = (double)byte / 255.0;
    if (c <= 0.04045) return (float)(c / 12.92);
    return (float)expSeries(2.4 * lnSeries((c + 0.055) / 1.055));
}
} // namespace detail

constexpr glm::vec4 srgb(uint32_t rgbHex, float alpha = 1.f) {
    return {
        detail::srgbByteToLinear((uint8_t)((rgbHex >> 16) & 0xFFu)),
        detail::srgbByteToLinear((uint8_t)((rgbHex >>  8) & 0xFFu)),
        detail::srgbByteToLinear((uint8_t)( rgbHex        & 0xFFu)),
        alpha
    };
}

inline constexpr glm::vec4 bgWindow      = srgb(0x151516);
inline constexpr glm::vec4 bgPanel       = srgb(0x1E1E1F);
inline constexpr glm::vec4 bgHeader      = srgb(0x262628);
inline constexpr glm::vec4 bgInput       = srgb(0x111112);

inline constexpr glm::vec4 ctlNormal     = srgb(0x2B2B2E);
inline constexpr glm::vec4 ctlHover      = srgb(0x38383C);
inline constexpr glm::vec4 ctlPressed    = srgb(0x45454A);

inline constexpr glm::vec4 lineDivider   = srgb(0x303033);

inline constexpr glm::vec4 textPrimary   = srgb(0xE2E2E4);
inline constexpr glm::vec4 textSecondary = srgb(0xA3A3A8);
inline constexpr glm::vec4 textDisabled  = srgb(0x6A6A70);

inline constexpr glm::vec4 accent        = srgb(0x2D6BD6);
inline constexpr glm::vec4 accentHover   = srgb(0x4A84E8);
inline constexpr glm::vec4 accentPressed = srgb(0x1E4B99);
inline constexpr glm::vec4 selectRow     = srgb(0x24466E);
inline constexpr glm::vec4 selectRowHover= srgb(0x2D5787);

inline constexpr glm::vec4 ok            = srgb(0x3FA463);
inline constexpr glm::vec4 warn          = srgb(0xE0B34A);
inline constexpr glm::vec4 error         = srgb(0xE0525A);

inline constexpr glm::vec4 transparent   { 0.f, 0.f, 0.f, 0.f };
inline constexpr glm::vec4 textOnAccent  = srgb(0xFFFFFF);
inline constexpr glm::vec4 borderPopup   = srgb(0x46464C);

inline constexpr float radiusControl = 4.f;
inline constexpr float radiusInput   = 3.f;
inline constexpr float radiusPanel   = 6.f;

} // namespace HSlateTheme
