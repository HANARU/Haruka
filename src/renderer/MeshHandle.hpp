#pragma once
#include <cfloat>
#include "../core/Bounds.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

// Offsets into the renderer's mega vertex/index buffers.
// No per-mesh VkBuffer — all geometry lives in two global Zone B buffers.
struct MeshHandle {
    uint32_t firstVertex = 0;   // element offset into mega vertex buffer
    uint32_t firstIndex  = 0;   // element offset into mega index buffer
    uint32_t indexCount  = 0;
    // 지금 넣지 않으면 소급이 불가능합니다. 현재 구조는 메가버퍼를 "반납하지
    // 않는" 것이 아니라 반납에 필요한 정보를 기록조차 하지 않는 상태였습니다.
    uint32_t vertexCount = 0;

    // Local-space bounding sphere (used for GPU frustum culling)
    glm::vec3 boundCenter{ 0.f };
    float     boundRadius = 0.f;

    // Local-space AABB (used for CPU frustum culling fallback)
    glm::vec3 aabbMin{ FLT_MAX,  FLT_MAX,  FLT_MAX };
    glm::vec3 aabbMax{-FLT_MAX, -FLT_MAX, -FLT_MAX };
};
