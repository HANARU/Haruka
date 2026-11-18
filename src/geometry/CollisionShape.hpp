#pragma once

// 충돌체 모양 · 움직임 종류 · 반 크기(2026-11-18, 엔진 구조 안정화 S4). 예전에는 components/RigidBodyComponent.hpp 에 있어
// 절차 도형 설명(geometry/PrimitiveDesc.hpp)이 컴포넌트를 include 했습니다. 물리 · 컴포넌트 · 씬 직렬화가 함께 쓰는 값이라
// 바닥 쪽(geometry)에 둡니다.

// 값은 옛 씬 파일의 `RIGID:` 순번과 같습니다. 지금은 이름으로 기록하지만(DEF-44) 옛 파일의
// 숫자도 읽으므로, 기존 항목의 값을 바꾸지 말고 새 항목은 끝에 명시값으로 붙이십시오.
// ConvexHull · Mesh(2026-11-17, 수학 보강 P2)는 같은 액터의 메시 형상(ICollisionMeshSource, geometry/CollisionMesh.hpp)으로
// 만듭니다. 크기는 액터 월드 크기를 점마다 곱해 비균등 크기도 정확합니다(halfExtent 는 쓰지 않음). Mesh(삼각형 그대로, 오목
// 가능)는 Static · Kinematic 전용이고, Dynamic 이면 경고하고 볼록 껍질로 만듭니다(Jolt · UE · Unity 모두 움직이는 오목 메시
// 충돌을 지원하지 않음).
enum class EPhysicsShape { Box = 0, Sphere = 1, Capsule = 2, ConvexHull = 3, Mesh = 4 };
enum class EMotionType   { Static = 0, Dynamic = 1, Kinematic = 2 };

struct HalfExt { float x = 0.5f, y = 0.5f, z = 0.5f; };
