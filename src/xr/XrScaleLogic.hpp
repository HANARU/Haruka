#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hxr {

struct EyeDimensions {
    uint32_t width  = 0;
    uint32_t height = 0;
};

inline uint32_t scaleEyeDimension(uint32_t recommended, uint32_t maxDim, float scale) {
    if (recommended == 0) return 0;
    const float clampedScale = std::clamp(scale, 0.5f, 1.5f);
    uint32_t scaled = static_cast<uint32_t>(std::lround(static_cast<double>(recommended) * static_cast<double>(clampedScale)));
    scaled = std::max(8u, scaled);
    if (maxDim > 0)
        scaled = std::min(scaled, maxDim);
    // 가장 가까운 8의 배수로 맞추되 maxDim 을 넘지 않게 합니다.
    uint32_t aligned = std::max(8u, ((scaled + 4u) / 8u) * 8u);
    if (maxDim >= 8u)
        aligned = std::min(aligned, (maxDim / 8u) * 8u);
    else if (maxDim > 0)
        aligned = maxDim;
    return aligned;
}

inline EyeDimensions scaleEyeExtent(uint32_t recW, uint32_t recH, uint32_t maxW, uint32_t maxH, float scale) {
    return { scaleEyeDimension(recW, maxW, scale), scaleEyeDimension(recH, maxH, scale) };
}

} // namespace hxr
