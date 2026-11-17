#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <algorithm>
#include <span>

// ── 화면 크기 · LOD 선택 · 작은 물체 컬링 (수학 보강 O6 · O7, 2026-11-17) ─────────────────────────────
// glm 만 쓰는 헤더라 단위 시험(HarukaStereoTest)에서 장치 없이 검산합니다.
//
// 화면 크기(screen size) = 바운딩 구의 투영된 지름 ÷ 화면 높이(UE 의 LOD Screen Size 와 같은 정의). 거리만 보던 예전
// LOD 문턱은 시야각 · 해상도와 무관해 헤드셋(좁은 시야 · 높은 해상도)과 데스크톱에서 같은 물체가 다른 LOD 로 보였습니다.
//   투영 행렬의 y 배율 projY = proj[1][1] = 2 / (tanUp + tanDown)(대칭이면 1 / tan(fovY/2))이고, 거리 d 의 반지름 r 은
//   NDC 에서 반지름 r · projY / d(NDC 높이 2)이므로 지름 ÷ 높이 = r · projY / d 입니다(시선 가까이의 근사).
namespace hscreen {

// LOD 경계의 이력 띠(±10 %). 경계에서 크기가 조금씩 오가도 LOD 가 프레임마다 바뀌어 깜빡이지 않습니다.
constexpr float kLodBand = 0.1f;
// 이보다 작게(지름, 화소) 보이는 물체는 카메라 목록에서 뺍니다(그림자 목록은 그대로). 래스터화로 화소 하나를 덮을까
// 말까 한 크기라 그려도 깜빡이는 점일 뿐입니다.
constexpr float kMinScreenPixels = 1.f;

// 구의 화면 크기. 카메라가 구 안이면(거리 < 반지름) 거리를 반지름으로 보아 projY 이상(화면을 덮음)이 됩니다.
inline float screenSize(float radius, float distance, float projY) {
    if (radius <= 0.f) return 0.f;
    return radius * projY / std::max(distance, radius);
}

// LOD 고르기. thresholds[i] 는 단계 i 에서 i+1(더 거친 것)로 넘어가는 화면 크기이고 내림차순입니다. 크기가
// thresholds[i] × (1 - 띠) 아래로 내려가야 거칠어지고, × (1 + 띠) 위로 올라가야 다시 고와집니다. 그 사이면 current 를
// 유지합니다. 결과는 [0, thresholds.size()] 입니다.
inline int selectLod(float size, std::span<const float> thresholds, int current, float band = kLodBand) {
    int lo = 0, hi = 0;   // 확실히 넘은 경계 수 · 넘었을 수도 있는 경계 수
    for (float t : thresholds) {
        if (size < t * (1.f - band)) ++lo;
        if (size < t * (1.f + band)) ++hi;
    }
    return std::clamp(current, lo, hi);
}

// 화면 높이 heightPx 화소에서 지름이 minPx 보다 작게 보이면 true.
inline bool tooSmall(float size, float heightPx, float minPx = kMinScreenPixels) {
    return size * heightPx < minPx;
}

} // namespace hscreen
