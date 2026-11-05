#pragma once
// 스팟 라이트 원뿔 계수. Vulkan 에 의존하지 않는 헤더라 단위 시험에서 바로 씁니다.
//
//  - 각은 반각(도)입니다. Inner 안쪽은 최대, Outer 바깥은 0, 그 사이는 cos 에 대해 선형입니다.
//  - inner > outer 로 저장된 값은 GPU 에 올리는 값에서만 inner = outer 로 봅니다. 저장값은 바꾸지
//    않습니다(Filament · Bevy 와 같은 방식).
//  - 분모 하한 0.001 은 예전 셰이더 값이고 Unity URP · glTF KHR_lights_punctual 과 같습니다. inner > outer
//    이면 예전 max(음수, 0.001) 과 지금 max(0, 0.001) 이 모두 0.001 이라 결과가 같습니다.
//  - 셰이더(pbr.frag)는 t = clamp(cos · scale + offset, 0, 1) 만 계산합니다. 예전에는 픽셀마다 spotDir 을
//    다시 정규화하고 (cosInner - cosOuter) 로 나눴습니다. 곡선(제곱 · smoothstep)은 아직 정하지 않았습니다.

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace hlight {

// 반환 {scale, offset}. GpuLight::spotParams.xy 에 그대로 올립니다.
inline glm::vec2 spotConeParams(float innerDeg, float outerDeg) {
    const float inner  = std::min(innerDeg, outerDeg);
    const float cosIn  = std::cos(glm::radians(inner));
    const float cosOut = std::cos(glm::radians(outerDeg));
    const float scale  = 1.f / std::max(cosIn - cosOut, 0.001f);
    return { scale, -cosOut * scale };
}

} // namespace hlight
