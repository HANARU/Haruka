#pragma once
// 내비 메시 입력 모으기(2026-11-21, Docs/Navigation_AI.md D2): 활성 액터의 Static RigidBodyComponent 충돌체(트리거 제외)를
// 월드 삼각형으로 바꿉니다. Box = 상자, Sphere · Capsule = 감싸는 상자, ConvexHull · Mesh = 같은 액터의 메시 형상.
#include "NavMeshBuild.hpp"

class World;

namespace hnav {

// bmin ~ bmax(월드 축 정렬 상자)와 겹치는 충돌체만 넣습니다. 넣은 충돌체 수를 돌려줍니다. 순서는 월드의 액터 순서입니다(결정성).
size_t collectStaticGeometry(const World& world, const glm::vec3& bmin, const glm::vec3& bmax, NavInputGeometry& out);

// 상자 하나(로컬 center ± half 를 m 으로 옮김)를 바깥을 향한 삼각형 12개로 더합니다(시험 지형에도 씀).
void addBox(NavInputGeometry& out, const glm::mat4& m, const glm::vec3& center, const glm::vec3& half);

} // namespace hnav
