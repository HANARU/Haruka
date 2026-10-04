#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>

// 셰이더와 같이 쓰는 구조체(FrameUBO·GpuTransform·GpuLight)는 공용 헤더 한 곳에 있습니다.
#include "../../shaders/include/haruka_shared.h"
#include "FrustumCull.hpp"

// ── 공용 GPU 데이터 구조 + 프레임 상수 ─────────────────────────────────────────
// Renderer/패스/FrameResources가 공유하는 셰이더 계약 타입들.

constexpr int      MAX_FRAMES = 2;
// 프레임당 transform SSBO 행 수. 카메라에 보이는 불투명·투명 메시와 라이트에만 보이는 그림자
// 캐스터가 이 예산을 함께 씁니다(카메라 쪽이 먼저). 넘친 메시는 Dropped 통계로 셉니다(DEF-10).
constexpr uint32_t kMaxDraws  = 1024;

// 라이트 SSBO 슬롯 수.
// 하드웨어 제약이 아니라 **메모리 예산 상한**일 뿐이다. pbr.frag의 lights[]는 크기 없는
// SSBO이고 디스크립터도 VK_WHOLE_SIZE로 바인딩하므로 셰이더 쪽에는 상한이 없다.
// 비용은 kMaxLights * sizeof(GpuLight)(64B) * MAX_FRAMES 이므로 256개에 32KB이고,
// Zone C(64MB, 실사용 약 0.2MB) 기준으로 무시할 수준이다.
// 실제 병목은 개수가 아니라 pbr.frag의 픽셀당 루프다. 기기별 실용 상한은 Quest 실기에서
// 프레임 타임을 측정한 뒤 런타임 설정으로 낮추는 것이 맞고, 이 상수를 조이는 것은 아니다.
constexpr uint32_t kMaxLights = 256;

using hgpu::FrameUBO;
using hgpu::GpuTransform;
using hgpu::GpuLight;

// 한 프레임에 그리는 뷰(눈)의 최대 수. 셰이더와 같은 값입니다.
constexpr uint32_t kMaxViews = HARUKA_MAX_VIEWS;

// C++ 배치가 셰이더(std140/std430) 배치와 같은지 컴파일할 때 확인합니다.
static_assert(sizeof(FrameUBO) == 272,                   "FrameUBO layout changed: update shaders/include/haruka_shared.h rules");
static_assert(offsetof(FrameUBO, lightViewProj) == 128,  "FrameUBO.lightViewProj offset");
static_assert(offsetof(FrameUBO, eyePos)        == 192,  "FrameUBO.eyePos offset");
static_assert(offsetof(FrameUBO, screenParams)  == 224,  "FrameUBO.screenParams offset");
static_assert(offsetof(FrameUBO, timeParams)    == 240,  "FrameUBO.timeParams offset");
static_assert(offsetof(FrameUBO, miscParams)    == 256,  "FrameUBO.miscParams offset");
static_assert(sizeof(GpuTransform) == 64,                "GpuTransform must be one mat4");
static_assert(sizeof(GpuLight)     == 64,                "GpuLight must be four vec4");

// 셰이더의 HARUKA_VIEW_PUSH_CONSTANT 와 같은 배치입니다. PBR · 깊이 · 라인 파이프라인 레이아웃이
// 이 크기의 푸시 상수 범위를 둡니다(정점 · 조각 단계).
struct GpuViewPush { int32_t viewIndex; };
static_assert(sizeof(GpuViewPush) == 4, "GpuViewPush must match HarukaViewPush");

// 그릴 뷰(눈) 하나. view 는 월드 → 눈, proj 는 눈 → 클립(Vulkan: 깊이 [0,1], Y 아래).
// 단안은 카메라 하나, 스테레오는 눈 둘입니다(DEF-09).
struct EyeView {
    glm::mat4 view{ 1.f };
    glm::mat4 proj{ 1.f };
    glm::vec3 pos{ 0.f };     // 눈의 월드 위치
    float     zNear = 0.1f;
    float     zFar  = 100.f;
};

// XR 런타임이 준 눈 하나(원점 공간 기준 자세와 네 방향 시야각, 라디안). 렌더러는 이것을 활성
// 카메라 원점과 합성해 EyeView 를 만듭니다. OpenXR 형식을 그대로 옮긴 값이라 렌더러는 OpenXR 를 모릅니다.
struct XrEyePose {
    glm::vec3 position{ 0.f };
    float     qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;   // 방향(쿼터니언)
    float     angleLeft = 0.f, angleRight = 0.f, angleUp = 0.f, angleDown = 0.f;
};

// XR 프레임 하나에 렌더러가 받는 것: 이번에 받은 스왑체인 이미지와 두 눈.
struct XrFrameTarget {
    uint32_t                           imageIndex = 0;
    std::array<XrEyePose, kMaxViews>   eyes{};
};

// Draw command (mirrors VkDrawIndexedIndirectCommand)
struct GpuDrawCmd {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;    // = (int32_t)mesh->firstVertex
    uint32_t firstInstance;   // → gl_InstanceIndex → transform SSBO row
};
static_assert(sizeof(GpuDrawCmd) == sizeof(VkDrawIndexedIndirectCommand), "GpuDrawCmd layout");

// Editor line overlay vertex (gizmo/wire box)
struct LineVertex {
    glm::vec3 pos;
    glm::vec4 color;
};
