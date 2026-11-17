#pragma once
#include "../scene/Component.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>

// ── 지수 고도 안개 (라이팅 Phase 6, 2026-11-17) ───────────────────────────────────────
// UE 의 Exponential Height Fog 와 같은 모델입니다. 높이 h 의 안개 밀도는 density · exp(−heightFalloff · (h − 액터 높이))이고, 눈에서
// 화소까지의 광선을 따라 이 밀도를 해석적으로 적분해(지수 함수의 선적분) 안개 양 = 1 − exp(−적분)을 구합니다. 화소 색은 그 양만큼
// 안개 색으로 섞입니다(최대 maxOpacity). 셰이더는 pbr.frag 의 applyHeightFog 이고, 값은 FrameUBO.fogColor · fogParams 로 넘깁니다.
//
//  - 월드에 켜진 안개 컴포넌트가 여럿이면 처음 만난 것 하나만 씁니다(UE 와 같음).
//  - 화면의 빈 곳(물체가 없는 배경 색)에는 걸리지 않습니다. 하늘 안개는 하늘 렌더를 만들 때 함께 넣습니다.
//  - 색은 HDR 선형값입니다(빛을 받은 안개의 밝기). 노출 1 에서 하늘 반사 정도면 0.3 ~ 0.6 입니다.
class HeightFogComponent : public Component {
public:
    float     density       = 0.02f;                    // 액터 높이에서의 밀도(1/m)
    float     heightFalloff = 0.2f;                     // 높이가 1 m 오를 때 밀도가 줄어드는 지수(1/m)
    glm::vec3 color         { 0.45f, 0.52f, 0.6f };     // 안개 색(선형 HDR)
    float     startDistance = 0.f;                      // 이 거리 안에는 안개가 없음(m)
    float     maxOpacity    = 1.f;                      // 안개 양의 상한(0 ~ 1)

    static constexpr float kMaxDensity = 10.f;
    static constexpr float kMaxFalloff = 10.f;

    bool editorIcon(glm::vec4& c) const override { c = { 0.7f, 0.75f, 0.8f, 1.f }; return true; }
    int  editorIconShape() const override { return 5; }
    const char*           typeName() const override { return "HeightFogComponent"; }
    void                  writeFields(ComponentFieldWriter& w) const override;
    bool                  readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps() override;
};

namespace hfog {
// pbr.frag 의 applyHeightFog 와 같은 식(시험용 CPU 판). eye · p 는 월드 위치, 반환은 안개 양(0 ~ maxOpacity).
inline float amount(const glm::vec3& eye, const glm::vec3& p, float density, float falloff, float fogHeight, float startDistance,
                    float maxOpacity) {
    if (!(density > 0.f)) return 0.f;
    const glm::vec3 d = p - eye;
    const float dist = glm::length(d);
    if (!(dist > startDistance)) return 0.f;
    const float rayLen = dist - startDistance;
    const float startY = eye.y + d.y * (startDistance / dist);
    const float dy     = d.y * (rayLen / dist);
    const float k      = std::max(falloff, 1e-4f);
    const float fexp   = k * dy;
    const float shape  = std::fabs(fexp) > 1e-4f ? (1.f - std::exp(-fexp)) / fexp : 1.f;
    const float integral = density * std::exp(-k * (startY - fogHeight)) * shape * rayLen;
    return std::min(1.f - std::exp(-integral), maxOpacity);
}
} // namespace hfog
