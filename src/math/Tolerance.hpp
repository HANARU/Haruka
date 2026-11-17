#pragma once

// ── 허용 오차 정책 (B1, 2026-11-17) ──────────────────────────────────────────────
// 기능마다 따로 적던 리터럴(1e-6f · 1e-8f · 1e-12f …)을 뜻에 따라 이름을 붙여 한 곳에 둡니다. 새 판정은 여기의 이름을
// 쓰고, 값을 바꿀 때는 그 이름을 쓰는 모든 판정이 함께 바뀝니다. 단위는 엔진 단위(m)이며, 값은 float 정밀도
// (상대 오차 약 6e-8)와 엔진이 다루는 크기(수 mm ~ 수 km)에 맞춰 골랐습니다.
//
// 기준(다른 엔진):
//   · UE: KINDA_SMALL_NUMBER 1e-4, SMALL_NUMBER 1e-8, THRESH_POINT_ON_PLANE 0.10 cm, THRESH_PARALLEL 0.02(코사인)
//   · Godot: CMP_EPSILON 1e-5, UNIT_EPSILON 1e-3
// 같은 생각을 따르되, 판정 종류마다 하나씩만 둡니다.

namespace hmath::tol {

// 방향 벡터의 길이 제곱이 이보다 작으면 방향이 없다고 봅니다(정규화 불가). 길이 1e-6 m 에 해당합니다.
inline constexpr float kDirLen2   = 1e-12f;
// 정규화한 벡터의 길이가 이보다 작으면 방향이 없다고 봅니다(길이로 나누기 전 확인).
inline constexpr float kDirLen    = 1e-6f;
// 광선 방향과 평면 법선의 내적(정규화하지 않은 값도 포함)이 이보다 작으면 나란하다고 봅니다.
inline constexpr float kParallel  = 1e-8f;
// 동차 좌표 w 의 절댓값이 이보다 작으면 무한 원점(역투영 실패)으로 봅니다.
inline constexpr float kHomogW    = 1e-6f;
// 점이 평면 · 면 위에 있다고 보는 거리(m). 0.1 mm.
inline constexpr float kOnPlane   = 1e-4f;
// 두 단위 쿼터니언의 내적 절댓값이 1 − 이 값보다 크면 같은 회전으로 봅니다.
inline constexpr float kSameRot   = 1e-6f;

} // namespace hmath::tol
