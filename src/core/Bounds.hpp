#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// 로컬 공간 바운드입니다. 렌더러와 지오메트리 생성기가 공유하므로
// 어느 한쪽에 두지 않고 core 에 둡니다. Vulkan 의존이 0입니다.
//
// radius 는 AABB 모서리까지의 거리가 아니라 '최소 외접구'입니다.
// GpuScene 이 정점을 순회해 만드는 값(AABB 모서리)은 테셀레이션된 곡면에서
// 과소 추정이고, 납작한 형상에서는 반대로 헐거워 컬링 효율을 떨어뜨립니다.
struct MeshBounds {
    glm::vec3 aabbMin{ 0.f };
    glm::vec3 aabbMax{ 0.f };
    glm::vec3 center { 0.f };
    float     radius = 0.f;
};
