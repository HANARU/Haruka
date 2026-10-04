#pragma once
#include "../core/Component.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class LightType : uint32_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
};

// UE 어휘를 채택한 라이트 컴포넌트입니다. 값은 물리 단위가 아니라 상대값(무단위)이며,
// 이름·드롭다운만 UE 를 따릅니다. 프로퍼티 행은 buildProps()(LightComponent.cpp)에서
// lightType 에 따라 조건부로 구성합니다.
class LightComponent : public Component {
public:
    LightType lightType    = LightType::Directional;
    // 라이트의 **로컬** 방향(방향광=태양 쪽, 스팟=조사 방향). 실제 월드 방향은
    // worldDirection() 이 액터 회전을 곱해 구합니다(유니티: 라이트 방향 = Transform).
    // 회전이 없으면(기본 Sun) 월드 방향 == 이 로컬 값이라 기존 씬 조명이 그대로 유지됩니다.
    // Sun 을 회전 기즈모로 돌리면 그림자·조명이 따라 바뀝니다.
    glm::vec3 direction    = glm::normalize(glm::vec3(1.0f, 2.0f, 1.5f));
    glm::vec3 color        = {1.0f, 1.0f, 1.0f};   // 선형 RGB 틴트
    // 0.85 이던 값. 그때 하늘의 등가 조도는 pi * 0.6017 = 1.890 이어서 하늘이
    // 태양보다 2.2 배 밝았다. 물리적으로 흐린 날이고, 흐린 날에는 정의상 음영이
    // 생기지 않는다. 3.0 이면 Renderer.cpp 의 kSkyRadianceScale 0.30 과 맞물려
    // 직사광 대 천공광이 5.3 : 1 이 되고, 이는 맑은 날의 범위(5~10 : 1)에 든다.
    // 이 값만 올리고 노출을 그대로 두면 ACES 숄더에 올라타 더 밝기만 하고
    // 대비는 늘지 않으니, PostPass.cpp 의 exposure 와 함께 움직여야 한다.
    float     intensity    = 3.0f;

    // UE: Use Temperature / Temperature(K). 켜지면 흑체 색온도에서 유도한 틴트를
    // color 에 곱합니다(effectiveColor). 기본은 꺼짐이라 기존 씬의 렌더 결과가 그대로
    // 유지되고, 6500K 근처가 중성(백색)입니다. 노출·강도와는 직교합니다.
    bool      useTemperature = false;
    float     temperatureK   = 6500.f;

    // UE: Attenuation Radius — Point/Spot 의 도달 반경. Directional 은 무시.
    // 2026-10-04 부터 밝기는 실제 점광원처럼 거리의 역제곱으로 줄고(intensity = 1m 거리의 밝기,
    // 2m 면 1/4), 이 반경은 밝기를 정하지 않고 꼬리를 0 으로 부드럽게 자르는 지점입니다(pbr.frag).
    // 0 이하는 허용하지 않습니다. 예전에는 0 이면 라이트가 꺼지고 음수면 감쇠 없이 무한히
    // 비췄는데, 주석은 "0=무한" 이라고 적혀 있었습니다(DEF-35). UE·Unity·Godot 모두 반경을
    // 0 보다 크게 강제합니다. UI·씬 불러오기·GPU 업로드 세 곳이 이 최솟값을 씁니다.
    static constexpr float kMinAttenuationRadius = 0.01f;   // 1 cm
    float     attenuationRadius = 10.f;
    // UE: Inner/Outer Cone Angle(도) — Spot 원뿔. Inner 안쪽은 최대, Outer 바깥은 0 으로 페이드.
    float     innerConeAngle = 15.f;
    float     outerConeAngle = 30.f;

    // UE: Cast Shadows(per-light). 현재 섀도맵은 방향광 하나만 지원하므로, 실질적으로는
    // **첫 Directional 라이트 중 이 값이 켜진 것**이 캐스터로 선택됩니다(Renderer::updateLightSSBO).
    // 기본 true 라 기존 씬은 그대로 그림자를 던집니다. UI 에는 Directional 일 때만 노출합니다.
    bool      castShadows = true;

    bool editorIcon(glm::vec4& c) const override { c = {1.f, 0.85f, 0.2f, 1.f}; return true; }

    // 월드 공간 라이트 방향 = 액터 회전 × 로컬 direction. owner 가 없으면 로컬 값 그대로.
    // 정의는 LightComponent.cpp (Actor 완전정의가 필요하므로).
    glm::vec3 worldDirection() const;
    // color 에 색온도 틴트를 접은 유효 색(intensity 는 곱하지 않음). useTemperature 가
    // 꺼져 있으면 color 그대로라 렌더가 불변입니다. 정의는 LightComponent.cpp.
    glm::vec3 effectiveColor() const;

    const char*           typeName()   const override { return "LightComponent"; }
    std::vector<PropDesc> buildProps()       override;
};
