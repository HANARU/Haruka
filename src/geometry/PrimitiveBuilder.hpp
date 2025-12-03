#pragma once
#include "PrimitiveDesc.hpp"
#include "../assets/MeshSource.hpp"
#include "../core/Bounds.hpp"

#include <string>

// ── 절차 도형 생성기 ──────────────────────────────────────────────────────────
// Vulkan 의존이 0입니다. 순수 생성과 업로드를 분리한 이유는 넷입니다.
//   1. 디바이스 없이 테스트할 수 있습니다. 탄젠트 handedness, 와인딩, UV 이음매는
//      정확히 이 방식으로만 잡히는 종류의 결함입니다 (DEF-39 참조).
//   2. estimateVertexCount 로 업로드 전에 예산을 검사할 수 있습니다.
//   3. 나중에 워커 스레드로 옮길 수 있습니다. 업로드는 메인 스레드여야 합니다.
//   4. 물리와 컬링이 지오메트리 없이 파라미터만 소비합니다.
namespace PrimitiveBuilder {

// 단위 도형을 생성합니다. AABB 는 [-0.5, +0.5]^3 입니다 (Arrow 만 예외).
// 미구현 셰이프는 빈 메시를 반환합니다. 조용히 다른 도형으로 대체하지 않습니다.
MeshSourceData build(const PrimitiveDesc& desc);

// 정점을 만들지 않고 답합니다.
MeshBounds analyticBounds(const PrimitiveDesc& desc);
uint32_t   estimateVertexCount(const PrimitiveDesc& desc);
uint32_t   estimateIndexCount (const PrimitiveDesc& desc);

// 규칙 T3. 생성 결과를 규약과 대조합니다. 디버그 빌드에서 호출합니다.
//   - 인덱스 범위
//   - 노멀의 단위 길이
//   - 와인딩 (바깥에서 CCW)
//   - 탄젠트 handedness 대 UV 유도 bitangent
//   - 정점 순회 바운드 대 해석 바운드
// 위반이 있으면 false 를 반환하고 report 에 사람이 읽을 설명을 채웁니다.
bool validate(const PrimitiveDesc& desc, const MeshSourceData& mesh, std::string* report);

} // namespace PrimitiveBuilder
