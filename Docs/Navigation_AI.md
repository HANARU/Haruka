# 내비게이션 · AI (2026-11-21)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 11위 후보입니다. 지금 엔진에는 내비 메시 · 경로 찾기 · AI 코드가 전혀 없어,
게임이 적 · 동료 캐릭터를 움직이려면 매번 직접 길을 계산해야 합니다. 이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고,
항목을 모두 만족하면 끝입니다. 외부 라이브러리는 사용자가 허용한 recastnavigation(zlib)만 씁니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-21)

| 곳 | 지금 | 내비 · AI 에 쓸 수 있는 점 |
|---|---|---|
| 물리 정적 충돌체 | `RigidBodyComponent`(Box · Sphere · Capsule · ConvexHull · Mesh, `center` · 액터 크기), `ICollisionMeshSource` | 걸을 수 있는 면의 입력. 렌더 메시와 달리 "막는 것"만 들어 있음 |
| 캐릭터 컨트롤러 | `PhysicsSystem::createCharacter` · `moveCharacter`(Jolt CharacterVirtual, 미끄러지며 막히기 · 계단 · 경사) | 에이전트 이동을 그대로 맡길 수 있음(XR 리그가 이미 씀) |
| 타이머 · 이벤트 버스 · 태그 | `World::timers()` · `events()` · `actorsWithTag()`([Gameplay_Tools.md](Gameplay_Tools.md)) | AI 의 대기 시간 · 감지 주기 · 상태 알림 · 순찰점 찾기 |
| 상태 기계 | `AnimStateMachine`(애니메이션 전이 · 조건 변수, [Animation_System.md](Animation_System.md)) | 구조는 참고하되 AI 상태는 코드 상태(아래 D8) |
| 콘솔 명령 | `CVarRegistry::addCommand`, 에디터 콘솔 줄 · `--console-at` | 내비 메시 굽기 명령을 에디터 · 창 없는 실행에서 같이 씀 |
| 묶음 | `Packaging/game.pack.txt` 가 `/Game` 아래 모든 파일을 넣음 | 씬 옆 `.hnav` 는 따로 적지 않아도 묶음에 들어감 |
| 디버그 선 | `core/DebugDraw.hpp`(선 · 상자 · 원) | 내비 메시 표시(선택 항목) |

### 1-2. 다른 엔진

- **Unreal:** `ANavMeshBoundsVolume` 이 범위를 정하고 `RecastNavMesh` 액터가 에이전트 반경 · 높이 · 경사 · 턱 높이를 듭니다. 에디터에서 미리 굽고
  레벨 파일에 저장합니다(정적), 실행 중 다시 만들기는 선택(Dynamic). 질의는 `UNavigationSystemV1`(월드 서브시스템)의 `FindPathSync` ·
  `ProjectPointToNavigation`. 이동은 `UPathFollowingComponent` 가 경로 점을 따라 `CharacterMovement` 에 속도를 넣고, 회피는 RVO 또는
  DetourCrowd 를 고릅니다. 도달 불가는 부분 경로(`bAllowPartialPaths`, 기본 켬)로 가장 가까운 곳까지 갑니다. AI 는 `AAIController` +
  행동 트리 · 블랙보드, 또는 State Tree.
- **Unity:** `NavMeshSurface` 컴포넌트(에이전트 종류별 반경 · 높이 · 경사 · 계단)로 굽고 씬 옆 에셋(`NavMesh.asset`)으로 저장합니다.
  `NavMesh.CalculatePath` · `SamplePosition`, `NavMeshAgent` 가 이동 · 회피(자체 RVO) · 회전 · 정지 거리를 맡습니다. `NavMeshObstacle` 은 구멍 내기.
  내부 구현은 Recast · Detour 계열입니다. AI 상태 기계는 엔진이 주지 않고 게임이 씁니다.
- **Godot:** `NavigationRegion3D` + `NavigationMesh` 리소스(굽기 = Recast), `NavigationServer3D`(서버 단위 질의), `NavigationAgent3D`
  (경로 따라가기 · RVO 회피, 이동은 사용자의 `CharacterBody3D.move_and_slide`). AI 는 스크립트.
- **recastnavigation 1.6.0:** Recast(복셀화 → 영역 → 윤곽 → 다각형 메시 → 높이 상세), Detour(타일 메시 · 질의 `findPath` · `findStraightPath` ·
  `findNearestPoly` · `moveAlongSurface`), DetourCrowd(군중 회피), DetourTileCache(동적 장애물). 같은 입력이면 같은 결과입니다(난수 없음).

### 1-3. 공통 구조

세 엔진 모두 (1) 범위 볼륨 + 에이전트 설정을 가진 컴포넌트 · 액터가 있고, (2) 에디터에서 정적 형상을 Recast 로 미리 구워 씬 옆 데이터로 두며,
(3) 질의는 월드 단위 시스템이 맡고, (4) 이동 컴포넌트가 경로 점을 따라가며 실제 이동은 캐릭터 이동기에 맡기고, (5) AI 판단은 그 위에 따로
얹습니다. 도달 불가는 "가장 가까운 곳까지의 부분 경로"가 기본입니다.

## 2. 결정

- **D1 라이브러리 · 범위:** recastnavigation 1.6.0 의 Recast · Detour 소스만 받습니다(`FetchContent` URL + SHA256, 저장소의 CMake ·
  데모 · 시험은 쓰지 않음). DetourCrowd · DetourTileCache · DebugUtils 는 넣지 않습니다(D6 · 남음). 정적 라이브러리 `haruka_recast` 를
  엔진(데스크톱 · Android)에 링크하고, Recast · Detour 형은 `src/nav` 의 .cpp 안에만 둡니다(헤더는 엔진 형만 보임).
- **D2 입력 형상:** 활성 액터의 **Static** `RigidBodyComponent` 충돌체만 씁니다(트리거 제외). 렌더 메시가 아니라 충돌체인 까닭은
  캐릭터가 실제로 막히는 형상과 같아야 하고, 장식 메시가 길을 막지 않아야 하기 때문입니다(Unreal 의 "collision 으로 굽기" 와 같음).
  Box 는 상자 12삼각형, ConvexHull · Mesh 는 같은 액터의 `ICollisionMeshSource` 형상, Sphere · Capsule 은 감싸는 상자로 근사합니다
  (장애물로는 충분하고, 그 위를 걷는 경우는 드묾). 모두 액터 월드 행렬(크기 포함)을 곱하고, 크기 부호가 거울이면 감기 방향을 뒤집습니다(물리와 같음).
  Dynamic · Kinematic 몸은 넣지 않습니다(움직이므로 D6 의 회피 대상).
- **D3 설정 자리 = 내비 볼륨 컴포넌트:** `NavMeshVolumeComponent` 가 범위(액터 위치 · 크기 × `halfExtent`, 축 정렬 상자), 에이전트 반경 ·
  높이 · 최대 경사 · 턱 높이, 셀 크기 · 높이, 데이터 경로(`data=/Game/scenes/<씬>.hnav`)를 듭니다. project.ini 가 아니라 컴포넌트에 둔
  까닭은 Unity `NavMeshSurface` · Godot `NavigationRegion3D` 처럼 씬마다 범위와 에이전트가 다르기 때문입니다. 한 씬에 볼륨이 여럿이면
  각자 따로 굽고, 질의는 시작점을 담은 볼륨을 씁니다(볼륨 사이 이어 걷기는 남음).
- **D4 미리 굽기 · 게임은 읽기만:** 콘솔 명령 `nav.build` 가 월드의 모든 볼륨을 구워 데이터 경로의 디스크 파일(`hvfs::diskPath`)에 쓰고 곧바로
  다시 읽습니다. 에디터 콘솔 줄과 창 없는 실행(`--console-at=N:nav.build`)에서 같이 씁니다. 게임은 씬을 읽을 때 볼륨이 `.hnav` 를 읽기만
  하고, 파일이 없으면 경고하고 그 볼륨은 비어 있습니다(질의는 NoPath). 실행 중 굽기는 `NavSystem::build` 로 코드에서 부를 수 있지만 엔진이
  스스로 하지는 않습니다(Quest 에서 굽기 비용을 피함, Unreal 의 Static 과 같음). `.hnav` 는 `/Game` 아래라 묶음에 그대로 들어갑니다.
- **D5 `.hnav` 형식:** 머리(`HNAV` · 판 1 · 설정 값 · 입력 해시 64비트 · 범위 · 다각형 수) + Detour 타일 데이터(`dtCreateNavMeshData` 결과,
  타일 하나 = solo 메시) 그대로입니다. 리틀 엔디언 · 같은 구조체 배치(PC x64 · Quest arm64 모두 맞음)를 전제로 하고, 읽을 때 마법 값 · 판 ·
  크기 · Detour 머리(`DT_NAVMESH_MAGIC` · 판)를 확인합니다. 입력 해시는 입력 삼각형 · 설정의 FNV-1a 로, 같은 입력이면 같은 파일(결정성)이고
  에디터가 오래된 파일을 알아보는 데 씁니다(`nav.info`). 큰 지형용 타일 나누기는 남음입니다.
- **D6 이동 · 회피:** `NavAgentComponent` 가 경로 점(`findStraightPath`)을 따라갑니다(속도 · 회전 속도 · 도착 반경 · 모서리 도달 거리). 물리가
  있고 `useCharacter` 가 켜져 있으면 Jolt CharacterVirtual 로 움직여(중력 · 미끄러지며 막히기) 동적 몸에 막히고, 아니면 Detour
  `moveAlongSurface` 로 메시 위를 미끄러집니다. 동적 장애물 회피는 **간단한 회피**로 정했습니다: 다른 에이전트와의 분리 힘(반경 합 안이면
  밀어냄) + 캐릭터 이동기의 미끄러짐. DetourCrowd 를 쓰지 않은 까닭은 군중 객체가 에이전트 위치를 스스로 적분해, 캐릭터 이동기가 정한
  실제 위치와 어긋나기 때문입니다(Unreal 도 Crowd 를 쓸 때 위치를 되먹이는 별도 경로가 필요). 큰 군중 최적화 · RVO 는 범위 밖입니다.
  경로를 벗어나면(옆으로 밀림 1 m 이상) 또는 쫓는 액터가 움직이면 `repathInterval` 마다 다시 찾습니다.
- **D7 질의 = 월드 서비스 `NavSystem`:** 엔진이 하나 소유하고 `world.setService<NavSystem>` 로 넣습니다(물리와 같은 자리). `findPath`(완전 ·
  부분 · 없음), `projectPoint`(가장 가까운 메시 위 점), `moveAlongSurface`, `build`, 콘솔 `nav.build` · `nav.info`. 도달 불가는 Unreal 처럼
  **부분 경로**(끝에 가장 가까운 닿을 수 있는 점까지, `Partial`)를 돌려주고, 끝점 근처에 메시가 아예 없거나 시작점이 메시 밖이면 `NoPath` 입니다.
  에이전트는 `allowPartial` 칸으로 부분 경로를 따라갈지 정합니다(기본 켬, 끄면 실패 이벤트).
- **D8 AI = 작은 코드 상태 기계:** `AIBrainComponent` 의 상태는 대기(Wait) · 순찰(Patrol) · 추적(Chase) 셋입니다. 순찰점은 태그(`patrolTag`)를
  가진 액터를 이름순으로 돌고, 대상은 태그(`targetTag`) 액터 중 감지 반경 안의 가장 가까운 것입니다(잃는 반경은 따로, 떨림 방지). 감지는
  타이머(`every(senseInterval)`), 순찰점 도착 뒤 대기는 타이머(`after(waitTime)`), 상태가 바뀌면 이벤트 버스에 `AIStateChanged` 를 보내고,
  이동 결과는 에이전트가 보내는 `NavMoveFinished` 를 이벤트 버스로 받습니다. 행동 트리 · 블랙보드는 범위가 커서 남음입니다(Unity · Godot 처럼
  게임이 쓰는 몫이 큼). `AnimStateMachine` 을 그대로 쓰지 않은 까닭은 전이 조건이 애니메이션 변수(float · bool)라 "경로 끝 · 대상 보임" 같은
  사건을 담기 어렵기 때문입니다.
- **D9 모듈:** 새 폴더 `src/nav`(module `nav: core, math, vfs, scene, geometry, physics`)입니다. 컴포넌트 세 개도 물리 컴포넌트처럼
  모듈 안에 두고, 등록은 `components/CoreComponents.cpp` 가 합니다(components → nav). runtime · game · editor · main 도 nav 를 봅니다.

## 3. 체크리스트

### N1 내비 메시 생성 · 저장 (nav)
- [x] `haruka_recast`(Recast · Detour 소스, URL + SHA256, 고지 파일), 데스크톱 · Android 빌드
- [x] 입력 모으기: Static 충돌체 → 월드 삼각형(Box · 근사 Sphere · Capsule · 메시 형상, 거울 크기 감기 뒤집기, 트리거 · 꺼진 액터 제외)
- [x] Recast 굽기(반경 · 높이 · 경사 · 턱 · 셀), 다각형 수 · 걸린 시간
- [x] `.hnav` 쓰기 · 읽기(머리 확인, 깨진 파일 거부), 같은 입력 = 같은 바이트
- [x] `NavMeshVolumeComponent`(씬 파일 왕복, 읽을 때 `.hnav` 읽기), 콘솔 `nav.build` · `nav.info`

### N2 경로 찾기 API (nav)
- [x] `NavSystem` 월드 서비스: `findPath`(Complete · Partial · NoPath), `projectPoint`, `moveAlongSurface`

### N3 이동 컴포넌트
- [x] `NavAgentComponent`: 속도 · 회전 · 도착 반경, CharacterVirtual 또는 표면 미끄러짐, 에이전트끼리 회피, 다시 찾기, 막힘, `NavMoveFinished` 이벤트

### N4 AI 상태 기계
- [x] `AIBrainComponent`: 대기 · 순찰 · 추적, 타이머 · 이벤트 버스 사용, `AIStateChanged`

### N5 시험 · 확인 장면
- [x] 단위 시험 `navigation`: 바닥 + 벽 + 경사 지형의 경로 길이 · 도달, 부분 경로 · 없음, 생성 시간, 결정성, 저장 → 읽기 왕복, 에이전트 · AI
- [x] `NavProbe.hscene` + 구운 `NavProbe.hnav`, 창 없는 실행 N 프레임 뒤 에이전트가 목표 근처(시험 + 로그)

### 선택
- [x] 내비 메시 표시: 콘솔 변수 `nav.debug 1` 이 볼륨 범위 · 메시 변 · 에이전트 경로를 디버그 선(`hdebug`)으로 더하고, 에디터 뷰포트가 그립니다(창 확인은 남음).
- [ ] 실행 중 타일 다시 만들기(남음: 타일 메시 · DetourTileCache 가 필요)

## 4. 결과 (2026-11-21)

### 4-1. 만든 것

| 곳 | 내용 |
|---|---|
| `CMakeLists.txt` · `cmake/HarukaAndroid.cmake` | recastnavigation 1.6.0(URL + SHA256), `haruka_recast` 정적 라이브러리(Recast 10 · Detour 7 소스), 엔진에 PRIVATE 링크, 서드파티 고지 |
| `src/nav/NavMeshBuild` | `NavBuildSettings` · `NavInputGeometry`, `hnav::build`(Recast solo 메시 → Detour 타일, 셀 상한 1,600만), `inputHash`(FNV-1a), `.hnav` `writeFile` · `readFile` |
| `src/nav/NavMesh` | Detour 메시 + 질의 하나(pimpl): `findPath`(꺾이는 점, 부분 경로는 마지막 다각형의 가장 가까운 점까지) · `projectPoint` · `moveAlongSurface` · `edges` |
| `src/nav/NavGeometry` | `collectStaticGeometry`(D2), `addBox`(면 중심 → 바깥 방향으로 감기를 맞춰 거울 크기도 맞음) |
| `src/nav/NavSystem` | 월드 서비스(D7), `build`(굽기 · 저장 · 다시 읽기), `info`(볼륨 최신 여부 · 에이전트 상태), 콘솔 `nav.build [경로]` · `nav.info` · `nav.debug` |
| `src/nav/NavMeshVolumeComponent` | 범위 · 에이전트 설정 · `data` 경로, 씬을 읽을 때 `.hnav` 를 읽음(없으면 경고 · 빈 메시) |
| `src/nav/NavAgentComponent` | `moveTo` · `moveToActor` · `stop`, 캐릭터 / 표면 이동, 회피, 다시 찾기, 막힘(2 초), `NavMoveFinished` |
| `src/nav/AIBrainComponent` | 대기 · 순찰 · 추적, 감지 타이머 · 대기 타이머, `NavMoveFinished` 구독, `AIStateChanged` 발행 |
| 엔진 · 등록 | `Engine` 이 `NavSystem` 을 소유 · 월드 서비스로 넣고 명령을 등록, 프레임마다 `drawDebug`. 컴포넌트 세 개를 `registerCoreComponents` 에 등록 |
| `tests/NavigationTest.cpp` | 시험 `navigation` 64 검사. `--write-probe <Content>` 로 확인 장면 · `.hnav` 를 다시 씀 |
| `Projects/Samples/Content/scenes/NavProbe.hscene` · `.hnav` | 바닥 · 벽 · 경사 · 높은 판, Walker(에이전트 + AI, 순찰점 `NavGoal` 태그 둘), 볼륨(3,336 B, 다각형 20) |

### 4-2. 구현하며 정한 것 (결정 보충)

- **부분 경로 도착은 성공입니다.** 에이전트가 부분 경로의 끝에 닿으면 `NavMoveFinished.success = true` 이고 `path = Partial` 로 구분합니다(Unreal 의
  부분 경로 도착과 같음). 부분 경로를 원하지 않으면 `allowPartial = 0` 으로 시작부터 실패시킵니다.
- **회피 = 분리 + 내다보기.** 처음에는 반경 합 안에서만 밀어내어, 마주 오는 두 에이전트가 0.3 m 까지 겹쳤습니다(시험으로 확인). 그래서 반경 합 +
  0.75 초 동안 갈 거리 안의 "앞에서 다가오는" 상대에게 미리 옆으로 비키는 힘을 더했습니다(상대가 오른쪽이면 왼쪽, 아니면 오른쪽 → 마주 오면 둘 다
  오른쪽으로 엇갈림). 시험에서 최소 거리 0.617 m(반경 합 0.6)로 둘 다 도착합니다. 회피를 끄면 0 m 로 겹칩니다.
- **막힘:** 원하는 속도의 1/10 도 못 내는 시간이 2 초 쌓이면 실패 이벤트를 보냅니다(동적 상자에 막힌 캐릭터가 끝없이 미는 것을 막음). AI 는 다음
  감지 · 대기에서 다시 시도합니다.
- **경로 이탈:** 지금 따라가는 선분에서 가로 1 m 넘게 벗어나면 `repathInterval`(기본 0.5 초)이 지난 뒤 다시 찾습니다.
- **데이터 경로:** 디테일 패널에는 글 칸이 없어(`PropDesc` 에 문자열 종류 없음) `data` 는 씬 파일 · 코드로 정하고, 비어 있으면 `nav.build /Game/...hnav`
  가 볼륨 하나에 그 경로를 줍니다(씬을 저장해야 남음). 디테일 패널은 읽기 전용 줄로 경로 · 상태를 보입니다.
- **볼륨 범위는 회전을 무시합니다**(월드 축 정렬 상자, Unreal NavMeshBoundsVolume 의 굽기 범위와 같음).

### 4-3. 검증

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0, Android 통과(nav · Recast · Detour 경고 없음) |
| 시험 | ctest 43 → 44, 검사 3,512 → 3,577(새 `navigation` 64, `architecture_deps` 29 → 30 = 새 모듈), Release · Debug 같음 |
| 굽기 시간 | 시험 지형(84 삼각형, 20 × 5 × 20 m, 셀 0.1 m) Release 10.7 ms · Debug 44 ms, 확인 장면 볼륨 8.8 ms |
| 결정성 | 같은 입력 두 번 같은 바이트, 저장소의 `NavProbe.hnav` = Release · Debug 시험이 다시 구운 바이트 |
| 변이 확인 | 형상 모으기에서 트리거 제외를 빼면 1개, 부분 경로를 완전으로 바꾸면 4개 실패 |
| 창 없는 실행 | `HarukaGame --headless --scene=/Game/scenes/NavProbe.hscene --fixed-dt=0.0166667 --console-at=600:nav.info`: `.hnav` 읽음(20 다각형, up to date), 600 프레임 뒤 `agent 'Walker': arrived at (7.26, 1.00, -7.45)`(목표 (7.5, 1, -7.5)), 검증 오류 0 |
| 기준 PNG | 7장 모두 기준값과 같음(검증 오류 0) |
| UI 덤프 | 새 기준 b_10(29장면 바뀜): 26장면은 디테일 패널의 컴포넌트 추가 버튼 3개(Nav Mesh Volume · Nav Agent · AI Brain)가 더해져 아래 버튼이 밀림, 3장면(`asset_actions` · `bottom_content` · `bottom_content_tall`)은 콘텐츠 목록에 새 샘플 `NavProbe.hscene` 이 보임. 나머지 29장면 같음, 뷰포트 덤프는 모두 같음 |
| Android 크기 | `libHarukaGame.so`(strip 전) 94.85 MB → 99.25 MB(+4.39 MB), `libharuka_recast.a` 2.07 MB |

### 4-4. 남음

- 창 확인: 에디터에서 `nav.debug 1` 선 모양, 컴포넌트 추가 · 디테일 칸, 에디터 콘솔 `nav.build` 로 다시 굽기, Play 에서 Walker 가 걷는 모습.
- 실행 중 타일 다시 만들기 · 큰 지형 타일 나누기(DetourTileCache), 동적 장애물 구멍 내기(Unity NavMeshObstacle 자리).
- 볼륨 사이 이어 걷기, 영역 종류 · 비용(물 · 문), 오프 메시 링크(범위 밖으로 정함), DetourCrowd · RVO · 큰 군중 최적화(범위 밖).
- 행동 트리 · 블랙보드, 시야각 · 가림 판정(지금 감지는 거리만), 에디터에서 볼륨 범위 상자 그리기(컴포넌트 시각화 등록).
- 헤드셋(Quest)에서 `.hnav` 읽기 · 에이전트 이동 확인.
