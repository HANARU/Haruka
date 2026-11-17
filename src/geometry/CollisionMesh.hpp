#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// ── 충돌용 CPU 형상 (수학 보강 P2, 2026-11-17) ──────────────────────────────────────
// 렌더 메시는 정점을 GPU 메가버퍼에만 올리므로, 물리가 볼록 껍질 · 삼각형 메시 충돌체를 만들 수 있게 위치와 인덱스만 CPU 에
// 남깁니다(정점당 12 B + 인덱스당 4 B). 메시의 로컬 공간 그대로이고(액터 크기를 곱하기 전), 에셋 관리자가 메시마다 한 번
// 만들어 공유합니다(불변). 컴포넌트는 Component::collisionMesh 로 내놓고, 물리는 RigidBody 의 셰이프가 ConvexHull · Mesh 일 때
// 같은 액터에서 처음 찾은 것을 씁니다.
struct CollisionMesh {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t>  indices;   // 삼각형 목록(3개씩)

    bool empty() const { return positions.empty(); }
};
