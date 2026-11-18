#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// ── 렌더 입력 스냅샷 (엔진 구조 안정화 S5b, 2026-11-18) ────────────────────────────
// 추출 단계(components/RenderExtract)가 장면(World · Actor · 컴포넌트)에서 값을 뽑아
// 이 형에 담아 넘기며, 렌더러는 장면 모듈을 보지 않고 이 값만 소비합니다.

struct RenderCamera {
    bool      present    = false;   // 활성 카메라(CameraComponent::activeIn). 없으면 false
    glm::mat4 view{ 1.f };          // viewMatrix()
    glm::mat4 xrOrigin{ 1.f };      // xrOrigin()
    float     fovY       = 45.f;
    float     zNear      = 0.1f;
    float     zFar       = 100.f;
    float     exposureEv = 0.f;
};

enum class RenderLightKind : uint8_t { Directional, Point, Spot };

struct RenderLight {                // LightComponent::enabledIn 순서 그대로
    RenderLightKind kind = RenderLightKind::Directional;
    glm::vec3       position{ 0.f };        // owner ? worldMatrix()[3] : 0
    glm::vec3       emitDirection{ 0.f, 0.f, -1.f };   // emitDirection()
    glm::vec3       color{ 1.f };           // effectiveColor() * intensity
    float           radius      = 10.f;     // max(attenuationRadius, kMinAttenuationRadius)
    float           innerCone   = 15.f;     // innerConeAngle (도)
    float           outerCone   = 30.f;     // outerConeAngle (도)
    float           contactShadowLength = 0.f;
    uint32_t        channels    = 0x1u;     // lightChannels
    bool            castShadows = false;
};

struct RenderFog {                  // 켜져 있고 활성인 첫 HeightFogComponent. 없으면 present = false
    bool      present       = false;
    glm::vec3 color{ 0.f };
    float     maxOpacity    = 0.f;
    float     density       = 0.f;
    float     heightFalloff = 0.f;
    float     height        = 0.f;
    float     startDistance = 0.f;
};

struct RenderSceneInput {
    RenderCamera             camera;
    std::vector<RenderLight> lights;
    RenderFog                fog;
};
