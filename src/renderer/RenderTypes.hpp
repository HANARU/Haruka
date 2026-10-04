#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>

// 셰이더와 같이 쓰는 구조체(FrameUBO·GpuTransform·GpuLight)는 공용 헤더 한 곳에 있습니다.
#include "../../shaders/include/haruka_shared.h"

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

// C++ 배치가 셰이더(std140/std430) 배치와 같은지 컴파일할 때 확인합니다.
static_assert(sizeof(FrameUBO) == 256,                   "FrameUBO layout changed: update shaders/include/haruka_shared.h rules");
static_assert(offsetof(FrameUBO, lightViewProj) == 128,  "FrameUBO.lightViewProj offset");
static_assert(offsetof(FrameUBO, cameraPos)     == 192,  "FrameUBO.cameraPos offset");
static_assert(offsetof(FrameUBO, screenParams)  == 208,  "FrameUBO.screenParams offset");
static_assert(offsetof(FrameUBO, timeParams)    == 224,  "FrameUBO.timeParams offset");
static_assert(offsetof(FrameUBO, miscParams)    == 240,  "FrameUBO.miscParams offset");
static_assert(sizeof(GpuTransform) == 64,                "GpuTransform must be one mat4");
static_assert(sizeof(GpuLight)     == 64,                "GpuLight must be four vec4");

// Frustum plane for CPU culling
struct FrustumPlane {
    glm::vec3 normal;
    float     d;
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
