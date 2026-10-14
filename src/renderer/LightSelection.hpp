#pragma once

// ── 라이트 선택 (DEF-33) ───────────────────────────────────────────────────────
// 이번 프레임에 GPU 로 올릴 라이트를 고릅니다. 예전에는 액터가 생성된 순서대로 앞에서부터 올리고 상한에서
// 잘라, 화면 밖 라이트도 픽셀마다 계산했고 상한에 걸리면 가까운 조명이 꺼지고 먼 조명이 켜질 수 있었습니다.
//
// 규칙(사용자 결정):
//   · 점광원 · 스팟은 영향 구(위치, Attenuation Radius)가 어느 눈의 절두체와도 겹치지 않으면 올리지 않습니다.
//     겹치지 않으면 보이는 픽셀에 기여할 수 없으므로 화면은 그대로이고 셰이더 루프만 짧아집니다.
//   · 방향광은 거르지 않습니다(어디에나 닿음).
//   · 상한 안이면 순서를 바꾸지 않습니다(남은 라이트의 원래 순서). 픽셀 합산 순서가 그대로라 화면이 같습니다.
//   · 상한을 넘으면 방향광을 먼저(원래 순서) 넣고, 나머지는 두 눈 가운데에서 라이트까지의 거리를 반경으로 나눈
//     값이 작은 순(영향 구 안쪽에 가까운 순), 같으면 밝은 순으로 채웁니다. 고른 집합은 원래 순서로 올립니다.
//     그래서 그림자를 던지는 라이트(첫 방향광)의 선택이 바뀌지 않습니다.
//   Unity URP Forward 는 넘치면 덜 밝은 라이트부터, Godot Mobile 은 메시당 8+8 로 자르며, 두 엔진 모두 넘치는
//   경계에서 깜빡일 수 있다고 적습니다. 상한(256)이 커서 실제로 걸리는 일은 드뭅니다.
//
// Vulkan 에 의존하지 않아 HarukaLightTest(light_selection)가 화면 없이 검산합니다.

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "FrustumCull.hpp"

namespace hlight {

struct Candidate {
    bool      directional = false;
    glm::vec3 position{ 0.f };   // 점광원 · 스팟의 월드 위치
    float     radius     = 10.f; // Attenuation Radius(최솟값을 적용한 값)
    float     brightness = 1.f;  // 색 × 세기의 가장 큰 성분
};

struct Result {
    std::vector<uint32_t> selected;   // 올릴 라이트의 원래 번호(원래 순서)
    uint32_t culled     = 0;          // 어느 눈에도 영향이 없어 거른 수
    uint32_t overBudget = 0;          // 보이지만 상한을 넘어 뺀 수
};

// views 가 null 이거나 viewCount 가 0 이면(카메라 없음) 거르지 않습니다.
inline Result select(const std::vector<Candidate>& lights, const std::array<FrustumPlane, 6>* views,
                     uint32_t viewCount, const glm::vec3& refPos, uint32_t maxLights) {
    Result r;
    std::vector<uint32_t> visible;
    visible.reserve(lights.size());
    for (uint32_t i = 0; i < (uint32_t)lights.size(); ++i) {
        const Candidate& l = lights[i];
        if (l.directional || !views || viewCount == 0 ||
            sphereInAnyView(views, viewCount, l.position, l.radius))
            visible.push_back(i);
        else
            ++r.culled;
    }
    if (visible.size() <= maxLights) {
        r.selected = std::move(visible);
        return r;
    }

    std::vector<uint32_t> keep, locals;
    for (uint32_t i : visible) {
        if (lights[i].directional) { if (keep.size() < maxLights) keep.push_back(i); }
        else locals.push_back(i);
    }
    auto key = [&](uint32_t i) {
        const Candidate& l = lights[i];
        return glm::length(l.position - refPos) / std::max(l.radius, 1e-4f);
    };
    std::stable_sort(locals.begin(), locals.end(), [&](uint32_t a, uint32_t b) {
        const float ka = key(a), kb = key(b);
        if (ka != kb) return ka < kb;
        return lights[a].brightness > lights[b].brightness;
    });
    for (uint32_t i : locals) {
        if (keep.size() >= maxLights) break;
        keep.push_back(i);
    }
    r.overBudget = (uint32_t)(visible.size() - keep.size());
    std::sort(keep.begin(), keep.end());   // 원래 순서
    r.selected = std::move(keep);
    return r;
}

} // namespace hlight
