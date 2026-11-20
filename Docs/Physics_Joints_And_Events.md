# 물리 조인트 · 충돌 이벤트 · 레이어 (2026-11-20)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 8위 후보입니다. 지금 물리(Jolt 5.1)는 레이캐스트 · 구 겹침 · 힘 · 충격량 ·
캐릭터 컨트롤러가 있지만, 물체를 서로 잇는 조인트, 부딪힘 · 들어감을 게임 코드에 알리는 이벤트, 무엇과 무엇이 부딪힐지 정하는 레이어 표,
모양을 훑는 질의(스윕)가 없습니다. 이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고, 항목을 모두 만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-20)

| 곳 | 지금 | 문제 |
|---|---|---|
| `PhysicsSystem.cpp` 레이어 | 오브젝트 레이어 STATIC · DYNAMIC 둘, 넓은 단계 레이어 둘 | 게임이 "이것끼리는 부딪히지 않음"을 정할 길이 없음 |
| `RigidBodyComponent` | 셰이프 · 모션 · 마찰 · 반발 · 질량 · 크기 · 중심 · CCD · 보간 | 트리거(센서) · 레이어 · 이벤트 켬 속성이 없음 |
| 접촉 알림 | 없음(Jolt `ContactListener` 를 등록하지 않음) | 문 앞 감지 · 충돌 소리 · 피해 판정을 게임이 직접 겹침 질의로 매 틱 확인해야 함 |
| 조인트 | 없음(`kMaxConstraints = 2048` 만 잡아 둠) | 경첩 문 · 서랍 · 사슬을 만들 수 없음 |
| 질의 | `raycast` · `raycastBody` · `overlapSphere` · `capsulePenetration` | 두께 있는 모양을 훑는 스윕이 없고, 레이어로 거를 수 없음 |
| 이벤트 버스 | `World::events()`(D4, 메인 스레드 전용) | 물리 스레드에서 바로 보낼 수 없음 |
| 틱 순서 | 월드 틱 → 게임 `onTick` → 모듈(물리 · 소리) → `World::tickPostPhysics` | 물리 결과 이벤트는 물리 틱 끝에서 보내면 PostPhysics 컴포넌트가 같은 프레임에 봄 |

### 1-2. 다른 엔진

- **Unreal (Chaos):** 오브젝트 채널(최대 18개 사용자 채널) + 채널별 응답(Block · Overlap · Ignore) 표, 프로필. `bGenerateOverlapEvents` ·
  `OnComponentBeginOverlap` · `EndOverlap`(액터가 지워져도 End 를 보냄), `bNotifyRigidBodyCollision` 을 켠 몸만 `OnComponentHit`.
  `UPhysicsConstraintComponent`(두 컴포넌트, 선형 · 각 한계 3축씩, 끊어짐 힘), `SweepSingleByChannel` · `SweepMultiByChannel`.
- **Unity (PhysX):** 레이어 32개 + Physics 설정의 레이어 충돌 행렬(체크 상자), `Collider.isTrigger` → `OnTriggerEnter` · `Stay` · `Exit`,
  `OnCollisionEnter` · `Stay` · `Exit`(한쪽이 Rigidbody 여야 함). 지워진 물체에는 `OnTriggerExit` 가 오지 않는 것이 오래된 불만입니다.
  조인트는 `FixedJoint` · `HingeJoint`(limits) · `ConfigurableJoint` · `SpringJoint`, `connectedBody` 가 비면 월드에 고정.
  `Physics.SphereCast` · `BoxCast` · `CapsuleCast`(layerMask 인자), `queriesHitTriggers`.
- **Godot (Jolt 모듈 포함):** `collision_layer` · `collision_mask` 비트 32개, 트리거는 별도 노드 `Area3D`(`body_entered` · `body_exited`),
  `RigidBody3D.contact_monitor` + `max_contacts_reported` 를 켠 몸만 `body_entered`. `HingeJoint3D` · `SliderJoint3D` · `PinJoint3D` ·
  `Generic6DOFJoint3D`(node_a · node_b 경로). 질의 `PhysicsDirectSpaceState3D.cast_motion`. 레이캐스트는 기본으로 Area 를 맞추지 않음.
- **Jolt 자체:** `ContactListener`(`OnContactAdded` · `Persisted` · `Removed`)는 물리 작업 스레드에서 바디가 잠긴 채 불립니다. 몸이 잠들면
  그 몸의 접촉에 `OnContactRemoved` 가 옵니다. 센서(`mIsSensor`)는 충돌 반응 없이 접촉만 알리고, 정적 센서는 깨어 있는 몸만 봅니다
  (키네마틱으로 깨워 두면 잠든 몸도 봄). 조인트는 `TwoBodyConstraint`(`Body::sFixedToWorld` 로 월드 고정), 몸을 지우기 전에 지워야 합니다.

### 1-3. 공통 구조

1. 레이어는 **이름 붙은 작은 정수 + 쌍별 충돌 여부 표**이고 프로젝트 설정에 있습니다(셋 다 같음). 질의는 레이어 마스크를 받습니다.
2. 트리거는 **충돌 반응 없는 충돌체**이고 들어감 · 나감만 알립니다. 일반 접촉 알림은 비용 때문에 **켠 몸만** 보냅니다(UE · Godot).
3. 알림은 물리 단계 뒤 **게임 스레드**에서 보냅니다(셋 다 그렇고, Jolt 를 쓰는 Godot 모듈도 큐에 모았다가 보냄).
4. 조인트는 **한쪽 몸의 컴포넌트 + 상대 참조(없으면 월드)**, 축 · 앵커는 로컬, 한계는 각도(도) 또는 거리(m)입니다.

## 2. 결정

- **D1 레이어 표(physics/PhysicsLayers.hpp):** 슬롯 16개(0 = `Default` 고정, 1 ~ 15 는 기본 이름 `Layer1` ~ `Layer15`), 쌍별 충돌 여부는
  16 × 16 대칭 비트 표이고 **기본은 모두 부딪힘**(지금 동작과 같음). `project.ini` 에 `physics.layer.<1~15> = 이름`,
  `physics.ignore = 이름, 이름`(여러 줄)로 적습니다. 근거: Unity · Godot 처럼 "다 부딪히는 것에서 빼는" 쪽이 기본 표와 지금 동작을 같게 둡니다.
  UE 의 Block/Overlap/Ignore 3단은 트리거 속성(D2)이 Overlap 자리를 맡으므로 2단으로 충분합니다. 슬롯 수는 Jolt 오브젝트 레이어(16비트)
  안에서 정적/움직임 비트와 합쳐 32 개로 충분하고, 마스크가 `uint16_t` 하나에 들어갑니다.
  Jolt 오브젝트 레이어 = `레이어 × 2 + (움직이는 몸이면 1)`. 정적끼리는 언제나 부딪히지 않습니다(지금과 같음).
- **D2 RigidBodyComponent 속성:** `layer`(0 ~ 15, 씬 파일 `layer = N`, 0 이면 쓰지 않음 → 옛 씬은 Default), `trigger`(센서),
  `contactEvents`(일반 접촉 알림 켬, UE `bNotifyRigidBodyCollision` · Godot `contact_monitor` 자리). 씬 파일에는 번호로 씁니다(이름을 바꿔도
  씬이 깨지지 않게, Unity 와 같음). 디테일 패널의 Layer 칸은 프로젝트의 레이어 이름을 보여 줍니다. layer · contactEvents 는 살아 있는
  몸에 바로 쓰고(`SetObjectLayer`), trigger 를 바꾸면 몸을 다시 만듭니다.
- **D3 트리거:** Jolt 센서입니다. 모션이 Static 인 트리거는 **키네마틱 센서 + 잠들지 않음**으로 만듭니다. 근거: 정적 센서는 잠든 몸을 보지
  못해, 트리거 안에서 상자가 잠들면 나감이 옵니다(Jolt 문서). 트리거는 레이캐스트 · 스윕 · 겹침 · 캐릭터 이동에 맞지 않습니다
  (Godot 의 Area 기본, Unity `queriesHitTriggers` 를 끈 것과 같음). 소리 차폐 · 텔레포트 광선 · 잡기 찾기가 트리거에 막히지 않게 하려는 것입니다.
- **D4 이벤트 전달:** `ContactListener` 는 물리 스레드에서 (바디 쌍, 하위 셰이프 쌍, 점 · 법선)만 잠금 있는 큐에 넣습니다. 메인 스레드는
  스텝마다 큐를 비워 **바디 쌍 단위**로 접촉 수를 셉니다(하위 셰이프가 여럿인 메시 · 껍질도 한 번). 0 → 1 이면 시작, 1 → 0 이면 끝입니다.
  알림은 그 틱의 스텝이 모두 끝난 뒤(물리 틱 끝, PostPhysics 앞) 순서대로 보냅니다.
  - 시작 · 끝(접촉) / 들어감 · 나감(트리거)은 쌍마다 한 번입니다. 유지(접촉만)는 **틱마다 한 번**(스텝마다가 아님)이고, 그 틱에 시작한 쌍은 빼냅니다.
  - 몸이 **잠들어서** 온 `OnContactRemoved` 는 끝으로 치지 않고 쌍을 "잠듦"으로 둡니다. 다시 접촉이 오면 이어지고, 두 몸이 깨었는데 그 스텝에
    접촉이 다시 오지 않으면 끝을 보냅니다. Unity · UE 처럼 잠든 상자가 트리거 안에 있으면 계속 "안"입니다.
  - 몸을 지울 때(액터 삭제 · 끄기 · 다시 만들기) 그 몸의 쌍은 모두 끝을 보냅니다(UE 와 같음, Unity 와 다름). 지워진 쪽은 `other == nullptr`.
  - 보내는 곳: 양쪽 `RigidBodyComponent` 의 함수 칸(`onTriggerEnter` · `onTriggerExit` · `onContactBegin` · `onContactStay` · `onContactEnd`)과
    이벤트 버스 `PhysicsContactEvent`(쌍마다 한 번). 액터는 보낼 때마다 id 로 다시 찾으므로, 앞 알림에서 액터를 지워도 안전합니다.
  - 일반 접촉은 두 몸 중 하나라도 `contactEvents` 를 켰을 때만 큐에 넣습니다. 트리거는 언제나 넣습니다.
- **D5 조인트(physics/JointComponent):** 한 액터의 컴포넌트, 종류 `fixed` · `hinge` · `slider` · `distance`, 상대 `target`(`HActorRef`,
  비면 월드에 고정). `anchor` · `axis` 는 소유 액터 로컬, `connectedAnchor` 는 상대 로컬(거리 조인트의 상대 쪽 점, 상대가 없으면 월드 좌표).
  `limits` 를 켜면 경첩은 `limitMin` · `limitMax`(도, −180 ~ 180), 슬라이더는 m, 거리 조인트는 최소 · 최대 거리(m). 끄면 경첩 · 슬라이더는
  자유, 거리 조인트는 만들 때의 거리로 고정합니다. 각도 0 은 **조인트를 만든 순간의 자세**(Jolt WorldSpace 설정, UE 와 같음)입니다.
  - 물리 틱이 몸을 만든 뒤 조인트를 만듭니다(양쪽 몸이 생긴 뒤). 상대를 지정했는데 찾지 못하면 만들지 않고 기다립니다.
  - 한쪽 몸이 지워지면(`removeBody`) 그 몸의 조인트를 먼저 지웁니다(Jolt 는 몸보다 조인트를 먼저 지워야 함). 조인트 기록은 물리 시스템이
    핸들로 갖고, 컴포넌트는 핸들만 듭니다(월드가 onDestroy 없이 지워져도 매달린 포인터가 없게).
  - 조인트 속성을 바꾸면 확정할 때 다시 만듭니다. 그 순간의 자세가 새 0 입니다.
- **D6 셰이프 캐스트:** `PhysicsSystem::shapeCast(SweepShape, 시작 위치, 회전, 방향, 최대 거리, out, layerMask, ignore)`.
  `SweepShape::sphere(r)` · `box(반변)` · `capsule(반지름, 원통 반높이)`(캡슐은 Y 축, 기존 충돌체 규약과 같음). 가장 가까운 첫 충돌의
  거리 · 점 · 법선 · 액터를 돌려줍니다. 시작부터 겹치면 거리 0 입니다. `raycast` · `overlapSphere` 에도 `layerMask`(기본 모두)를 더합니다.
- **D7 범위 줄임:** 조인트 모터 · 스프링 · 끊어짐 힘, 6자유도 조인트, 트리거 유지 알림, 에디터의 레이어 표 편집 화면은 이번에 하지 않습니다.
  근거: [필수] 를 2 ~ 3시간 안에 지키고, 레이어 표는 `project.ini` 로 충분합니다(문서 "남음").

## 3. 체크리스트

### J1 레이어 표
- [x] `PhysicsLayerTable`: 슬롯 16 · 이름 · 대칭 충돌 표 · 이름 찾기 · `project.ini` 줄 해석(잘못된 줄은 메시지).
- [x] `ProjectSettings` 가 `physics.` 줄을 모아 넘김(플랫폼 접미사는 거부), 엔진이 물리 시작 전에 표를 넣음.
- [x] Jolt 레이어 필터가 표를 따름(기본 표 = 지금 동작). `RigidBodyComponent.layer` 저장 · 읽기 · 디테일 칸(이름 목록).

### J2 트리거 · 접촉 이벤트
- [x] `trigger` · `contactEvents` 속성 저장 · 읽기 · 디테일 칸.
- [x] 접촉 큐(물리 스레드) → 바디 쌍 셈(메인 스레드) → 컴포넌트 함수 칸 + 이벤트 버스.
- [x] 잠듦은 끝이 아님, 몸 지우기는 끝을 보냄, 알림 중 액터 삭제 안전.

### J3 조인트
- [x] `JointComponent`(fixed · hinge · slider · distance, `HActorRef` 상대, 한계), 씬 파일 · 디테일 칸, 등록.
- [x] 물리 틱이 만들고, 몸을 지우면 먼저 지우고, 컴포넌트가 지워지면 지움.
- [x] [선택] 에디터 표시(`ComponentVisualizers`): 앵커 원 · 축 선 · 경첩 한계 부채꼴 · 슬라이더 범위 · 거리 줄 · 상대까지 선, 트리거 충돌체는 주황. 창 확인은 남김.

### J4 셰이프 캐스트
- [x] 구 · 상자 · 캡슐 스윕, 회전, 레이어 마스크, 무시 액터, 트리거 제외, 시작부터 겹치면 거리 0.

### J5 시험 · 확인 장면
- [x] `physics_joints` 시험: 경첩 문 한계 안에서 멈춤, 트리거 들어감 · 나감 1회씩, 레이어로 무시한 쌍은 통과 · 접촉 없음, 스윕 첫 충돌 거리,
      조인트 상대 삭제 뒤 크래시 없음, 기존 physics 시험 그대로.
- [x] `PhysicsJointProbe.hscene`(경첩 문 · 사슬 · 트리거 상자)을 읽어 300 프레임 돌린 뒤 자세 범위 확인(같은 시험 안).

### G 공통
- [x] 경고 R0/D0 · ctest 전부 · 아키텍처 PASS · base7 같음 · UI 덤프(새 기준) · Android.

## 4. 결과 (2026-11-20)

| 확인 | 결과 |
|---|---|
| 경첩 문(수직 축, −10 ~ 80도) | 한 번 세게 밀면 최대 80.5도 → 79.3도에서 멈춤, 반대로 밀면 최소 −10.1도. 경첩 점 이동 0.000 m |
| 확인 장면 | 5초 뒤 판(경첩, −60 ~ 0도)이 −60.07도, 사슬 고리 간격 0.301 ~ 0.303 m(0.3 m), 사슬 끝 y 1.56(고정점 2.5 아래), 트리거 상자 들어감 1 · 나감 0 |
| 트리거 | 지나가는 공 들어감 1 · 나감 1(양쪽 함수 칸 · 버스 같음), 안에서 잠든 상자 1 · 0, 지우면 나감 1(other = nullptr), 잠든 상자 둘레에 생긴 트리거도 들어감 1 |
| 접촉 알림 | 바닥에 떨어져 잠든 상자: 시작 1 · 유지 29 · 끝 0, 잠든 뒤에도 끝 0, 들어 올리면 끝 1. 시작 알림에서 상대를 지우면 시작 3 · 끝 3 · 크래시 없음 |
| 레이어 | Debris(Default 와 무시) 상자는 바닥을 통과(y −9.9)하고 알림 0, Default 상자는 바닥 위. 레이캐스트 마스크로 Debris 만 보면 맞추지 않음 |
| 조인트 종류 · 삭제 | 슬라이더 x 1.000 · y 1.000(X 로만, 1 m 한계), 거리 1.003 m, 고정 간격 0.500 m. 문틀 삭제 → 조인트 0 · 문이 떨어짐, 조인트를 든 쪽 삭제 → 0 |
| 셰이프 캐스트 | 구 4.500 · 상자 4.750 · 캡슐 4.300 · 벽 4.600(법선 +Z) · 회전한 판 4.500 m, 마스크 · 무시 액터는 건너뜀, 시작 겹침은 0 |
| 변이 확인 | 레이어 필터를 끄면 3 실패, 잠듦 규칙을 끄면 접촉 1 실패, Static 트리거를 정적 센서로 두면 "잠든 몸 둘레에 생긴 트리거" 1 실패, 조인트 몸 순서를 바꾸면 경첩 · 슬라이더 · 확인 장면 실패 |
| 공통 | 경고 R0 · D0, ctest 37/37(Release · Debug), 검사 2,940(2,885 + 55), 아키텍처 PASS, 기준 PNG 7장 같음, UI 덤프 28장면 다름(뷰포트는 모두 같음): Add Component 줄의 "+ Joint", RigidBody 디테일의 Layer · Trigger · Contact Events 세 줄, 콘텐츠 브라우저의 새 장면 → 새 기준 `b_02`. Android 통과(새 경고 없음) |

구현 중에 정한 것:
- Jolt 의 경첩 각도 · 슬라이더 위치는 "몸 2 를 몸 1 에 대해" 잰 값이므로 몸 1 = 상대(또는 월드), 몸 2 = 조인트를 든 액터로 만듭니다. 처음에 반대로
  만들어 한계가 뒤집혔고(밀면 −로 감), 시험이 잡았습니다.
- 셰이프 캐스트는 볼록 몸의 뒷면도 맞춥니다(`CollideWithBackFaces` · `mReturnDeepestPoint`). Jolt 기본은 겹친 채 나란히 움직이면 건너뛰어, "시작부터
  겹치면 거리 0" 이 지켜지지 않았습니다. 메시 삼각형의 뒷면은 예전처럼 건너뜁니다.
- 잠듦 규칙과 키네마틱 센서는 서로 다른 경우를 맡습니다. 정적 몸과의 접촉은 잠듦 규칙만으로 이어지고, 트리거 안에서 잠든 상자는 둘 중 하나로도
  이어지지만, 이미 잠든 몸 둘레에 트리거가 새로 생기는 경우는 키네마틱 센서만 잡습니다(시험으로 확인).
- 거리 조인트의 지금 길이는 Jolt 가 주지 않아, 두 끝점을 몸의 질량 중심 공간으로 기억해 계산합니다.
- 접촉 큐의 "빠짐" 콜백은 이미 세고 있는 쌍만 넣습니다(쌍 표는 스텝 동안 물리 스레드가 읽기만 함).

남음:
- 조인트 모터 · 스프링 · 끊어짐 힘, 6자유도 조인트, 조인트 축 · 한계를 기즈모로 끄는 편집.
- 트리거 유지 알림, 키네마틱 몸끼리 · 키네마틱과 정적 몸의 트리거 감지(지금은 동적 몸만 들어감).
- 레이어 표를 고치는 에디터 화면(지금은 `project.ini`), 레이어 이름을 바꿔도 씬은 번호라 그대로입니다.
- 손 쥐기의 두 손 잡기에 조인트를 쓰는 일(이번 범위 밖, [Hand_Grasp_Design.md](Hand_Grasp_Design.md) 쪽에서 정함).
- 창 확인: 에디터에서 `PhysicsJointProbe.hscene` 의 조인트 표시(부채꼴 · 축 · 줄)와 트리거 주황 충돌체, Play 에서 판 · 사슬 · 상자의 움직임.
- Android 빌드에 남은 `PhysicsSystem.cpp` 의 dangling-else 경고는 예전 코드(HLOG 매크로를 중괄호 없이 쓴 줄)이고, 헤더가 바뀌어 다시 컴파일되며 보였습니다.
