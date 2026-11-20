#pragma once
#include "../scene/Component.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>

// ── 상자 투영 데칼 (2026-11-20, Docs/Particles_And_Decals.md D11) ──────────────────────────────────────────
// 포워드 데칼입니다(Godot Forward+ · DOOM 2016 과 같은 방식). 불투명 셰이더(pbr.frag 의 applyDecals)가 조명 전에 화소의 월드
// 위치를 데칼 상자 로컬로 옮겨, 상자 안이면 모양 마스크만큼 알베도를 바꿉니다. 그래서 데칼도 조명 · 그림자 · 안개를 그대로 받습니다.
//
//  - 상자는 액터 월드 행렬 × size(m)의 로컬 [-0.5, 0.5]³ 이고, 로컬 −Y 방향으로 투영합니다(Godot Decal 과 같음). 회전 없는 데칼은
//    아래(바닥)로 찍힙니다. 무늬의 가로는 로컬 X, 세로는 로컬 Z 입니다.
//  - 표면 법선이 상자 +Y 와 이루는 각이 클수록 옅어집니다(옆면이 늘어나 보이지 않게, cos 0.1 ~ 0.5 사이).
//  - 한 프레임 최대 kMaxDecals 개. 넘으면 렌더러가 두 눈 가운데에서 가까운 것부터 고릅니다.
//  - 텍스처는 아직 없고 모양은 셰이더가 계산합니다(원 · 부드러운 원 · 고리 · 사각).
class DecalComponent : public Component {
public:
    enum class Blend : uint8_t { Replace = 0, Multiply = 1 };
    enum class Shape : uint8_t { Circle = 0, SoftCircle = 1, Ring = 2, Square = 3 };
    static constexpr uint32_t kMaxDecals = 8;   // shaders/include/haruka_shared.h 의 HARUKA_MAX_DECALS

    glm::vec3 size{ 1.f, 0.5f, 1.f };          // 상자 크기(m): 가로 · 투영 깊이 · 세로
    glm::vec3 color{ 0.6f, 0.08f, 0.05f };     // 선형 알베도
    float     opacity   = 1.f;                 // 0 ~ 1
    float     roughness = -1.f;                // 0 ~ 1 이면 거칠기도 바꿈, 음수면 그대로
    Blend     blend     = Blend::Replace;
    Shape     shape     = Shape::Circle;

    const char*           typeName() const override { return "DecalComponent"; }
    void                  writeFields(ComponentFieldWriter& w) const override;
    bool                  readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps() override;
    bool                  localBounds(glm::vec3& outMin, glm::vec3& outMax) const override {
        outMin = -0.5f * size; outMax = 0.5f * size; return true;
    }
};

namespace hdecal {
// 셰이더(pbr.frag 의 decalMask)와 같은 모양 마스크. uv ∈ [0, 1]², 가운데 (0.5, 0.5).
inline float shapeMask(int shape, glm::vec2 uv) {
    const glm::vec2 c = uv * 2.f - 1.f;
    const float r = std::sqrt(c.x * c.x + c.y * c.y);
    auto smooth = [](float e0, float e1, float x) {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    switch (shape) {
        case 1:  { const float k = std::max(0.f, 1.f - r * r); return k * k; }                  // 부드러운 원
        case 2:  return smooth(0.55f, 0.62f, r) * (1.f - smooth(0.88f, 0.95f, r));              // 고리
        case 3:  return 1.f - smooth(0.96f, 1.f, std::max(std::fabs(c.x), std::fabs(c.y)));     // 사각
        default: return 1.f - smooth(0.94f, 1.f, r);                                            // 원
    }
}
// 투영 방향과 표면 법선의 각 페이드. ndotAxis = dot(법선, 상자 +Y).
inline float angleFade(float ndotAxis) {
    const float t = std::clamp((ndotAxis - 0.1f) / 0.4f, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
// 월드 위치 p · 법선 n 에서 데칼 하나의 가중치(0 ~ 1, 불투명도 포함 전). worldToBox 는 (액터 월드 × scale(size))의 역행렬,
// axis 는 상자 +Y 의 월드 단위 방향. 셰이더 applyDecals 와 같은 식입니다.
inline float weight(const glm::mat4& worldToBox, const glm::vec3& axis, const glm::vec3& p, const glm::vec3& n, int shape) {
    const glm::vec3 l = glm::vec3(worldToBox * glm::vec4(p, 1.f));
    if (std::fabs(l.x) > 0.5f || std::fabs(l.y) > 0.5f || std::fabs(l.z) > 0.5f) return 0.f;
    return shapeMask(shape, glm::vec2(l.x + 0.5f, l.z + 0.5f)) * angleFade(glm::dot(n, axis));
}
} // namespace hdecal
