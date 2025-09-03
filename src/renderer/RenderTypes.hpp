#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>

// ── 공용 GPU 데이터 구조 + 프레임 상수 ─────────────────────────────────────────
// Renderer/패스/FrameResources가 공유하는 셰이더 계약 타입들.

constexpr int      MAX_FRAMES = 2;
constexpr uint32_t kMaxDraws  = 1024;    // max draws per frame (opaque or transparent)

// 라이트 SSBO 슬롯 수.
// 하드웨어 제약이 아니라 **메모리 예산 상한**일 뿐이다. pbr.frag의 lights[]는 크기 없는
// SSBO이고 디스크립터도 VK_WHOLE_SIZE로 바인딩하므로 셰이더 쪽에는 상한이 없다.
// 비용은 kMaxLights * sizeof(GpuLight)(64B) * MAX_FRAMES 이므로 256개에 32KB이고,
// Zone C(64MB, 실사용 약 0.5MB) 기준으로 무시할 수준이다.
// 실제 병목은 개수가 아니라 pbr.frag의 픽셀당 루프다. 기기별 실용 상한은 Quest 실기에서
// 프레임 타임을 측정한 뒤 런타임 설정으로 낮추는 것이 맞고, 이 상수를 조이는 것은 아니다.
constexpr uint32_t kMaxLights = 256;

// Per-frame uniform data — matches pbr.vert / pbr.frag set 0
struct FrameUBO {
    glm::mat4  viewProj[2];     // [0]=left/mono, [1]=right (OpenXR stereo)
    glm::mat4  lightViewProj;   // shadow camera VP
    glm::mat4  prevViewProj;    // previous frame VP (TAA)
    glm::vec4  cameraPos;       // xyz=world pos, w=nearPlane
    glm::vec4  screenParams;    // xy=screenSize, z=farPlane, w=aoStrength
    glm::vec4  timeParams;      // x=time, y=frame, z=bloomThreshold, w=exposure
    glm::ivec4 miscParams;      // x=lightCount, y=taaFrame, z=ssaoEnabled, w=shadowLightIndex(<0=none)
};

// Per-mesh world matrix in SSBO
struct GpuTransform {
    glm::mat4 model;
};

// Frustum plane for CPU culling
struct FrustumPlane {
    glm::vec3 normal;
    float     d;
};

// Light uploaded to GPU SSBO — matches pbr.frag set 1 binding 4
struct GpuLight {
    glm::vec4 posOrDir;    // xyz=pos or dir, w=type (0=dir, 1=point, 2=spot)
    glm::vec4 colorRange;  // xyz=effectiveColor*intensity, w=attenuationRadius (0=infinite)
    glm::vec4 spotDir;     // xyz=spot forward direction
    glm::vec4 spotParams;  // x=cosInner, y=cosOuter, zw=reserved
};

// GPU-driven draw command (mirrors VkDrawIndexedIndirectCommand)
struct GpuDrawCmd {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;    // = (int32_t)mesh->firstVertex
    uint32_t firstInstance;   // → gl_InstanceIndex → transform SSBO row
};

// Bounding sphere passed to culling compute shader (world space)
struct GpuBoundSphere {
    glm::vec4 centerRadius;   // xyz=world center, w=world radius
};

// Editor line overlay vertex (gizmo/wire box)
struct LineVertex {
    glm::vec3 pos;
    glm::vec4 color;
};
