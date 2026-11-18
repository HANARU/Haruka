# 엔진 구조 안정화 (2026-11-18 ~)

기능이 빠르게 늘면서 모듈 사이의 의존이 "갖다 얽어 놓은" 모양이 되어(사용자 2026-11-18), 구조를 단계적으로 정리합니다.
이 문서는 **모듈 의존 방향의 정본**입니다. 기계가 읽는 규칙은 [`tests/ArchitectureRules.txt`](../tests/ArchitectureRules.txt)이고,
시험 `architecture_deps`(HarukaArchTest)가 src 아래 모든 `#include "..."` 를 그 표와 맞춰 봅니다.
Layer 1(코어)과 도메인의 경계는 [Layer_Architecture.md](Layer_Architecture.md)가 다루고, 이 문서는 코어 안의 모듈 구조를 다룹니다.

모든 작업은 **동작을 바꾸지 않는 리팩터링**입니다. 작업마다 빌드 · ctest · 기준 PNG 로 확인합니다.

## 1. 진단 (2026-11-18 실측)

src 약 43,000줄, 모듈 17개(분할 전). include 를 모듈 단위로 세어 찾은 문제입니다.

| # | 문제 | 근거 | 다루는 단계 |
|---|---|---|---|
| 1 | core 가 바닥이자 꼭대기 | 기반(로그 · 파일 · 경로), 장면 모델(World · Actor · Component), 조립부(Engine)가 한 폴더. 모두가 core 를 include 하고 core 도 11개 모듈을 include → 순환 | 1단계(S7 일부 끝남) |
| 2 | `Engine::run` 약 410줄에 관심사 약 열 가지 | XR 세션 복구 · 리그 원점 · 청취자 · Play 전이 · 틱 순서 · 패널 · UI 포인터 · 렌더 분기 · 통계. 프레임 단계 순서가 코드 위치로만 정해짐 | 1단계 S2(끝남, 7절) |
| 3 | 시스템 접근 방식 다섯 가지 | `EngineContext` · `ModuleManager::get<T>` · 전역 싱글턴 · 정적 포인터 `PhysicsSystem::current()` · Engine 이 직접 든 멤버 | 1단계 S3(끝남, 8절) |
| 4 | 렌더러가 장면 모델을 직접 읽음 | Renderer_render.cpp 가 World 와 컴포넌트 6종을 include, GPU 핸들을 컴포넌트에 씀 | 2단계 S5 |
| 5 | Component 기반 클래스가 모든 계층의 일을 가짐 | 에디터(아이콘 · 범위 표시 · 속성 칸) · 물리(collisionMesh) · XR(blocksFloor) · 직렬화 | 2단계 S4(끝남, 9절) |
| 6 | 에디터 한 클래스 | EditorApp.cpp 4,492줄, 함수 85개 | 3단계 S6 |
| 7 | World 가 에디터 상태를 앎 | `editorCam_` · `playing_` | 1단계 S3(끝남, 8절) |

그대로 둘 것: 시스템이 `world_->componentsOf<T>()` 로 끌어오는 방식, `IEngineFrontend` 로 에디터 · 게임 분리(DEF-12),
렌더 확장 레지스트리(DEF-17 · 18), ComponentRegistry 직렬화(DEF-22).

## 2. 모듈 층과 의존 방향

아래로만 include 합니다. 같은 모듈 안은 늘 됩니다. 표에 없는 방향은 시험이 막습니다(허용 목록 제외, 4절).

| 층 | 모듈 | 하는 일 | include 해도 되는 모듈 |
|---|---|---|---|
| 바닥 | `math` | 공통 수학(도형 · 판정 · 감쇠 · 곡선 · 난수 · 필터 · IK) | 없음 |
| 바닥 | `vfs` | 가상 파일 시스템 · 묶음(.hpak) 형식 | 없음 |
| 기반 | `core` | 로그(HLog) · 파일 · 경로 · 텍스트 도우미 · 값 해석(FieldParse) · 설정 파일(engine.ini · project.ini) · 계측(FrameTimings · FramePacing) · 디버그 선 · Transform · 델리게이트 · 모듈 인터페이스(IEngineModule) | math |
| 기반 | `memory` | 존 기반 할당자 | core |
| 기반 | `platform` | 창 · 표면 · 플랫폼 문맥 | core |
| 기반 | `input` | 입력 장치 · 바인딩 | core, math, platform |
| 기반 | `ui` | HSlate UI | core, math, vfs, input |
| 장면 | `scene` | Actor · Component · World · 컴포넌트 필드 · 레지스트리 · Reflect | core, math |
| 시스템 | `geometry` | 절차 도형 · 메시 처리 | core, math |
| 시스템 | `physics` | Jolt 물리, RigidBodyComponent(2026-11-18 S7 에 components 에서 옮김) | core, math, scene, geometry |
| 시스템 | `audio` | miniaudio · 차폐 · 잔향 · HRTF, AudioComponent · ReverbZoneComponent(2026-11-18 S7 에 옮김) | core, math, vfs, scene, physics |
| 시스템 | `renderer` | Vulkan 렌더러 · 패스 | core, math, vfs, memory, platform, ui, geometry |
| 시스템 | `xr` | OpenXR · 손 쥐기 풀이 | core, math, input, platform, renderer |
| 시스템 | `assets` | 에셋(메시 · 텍스처 · glTF · 캐시) 불러오기 | core, math, vfs, geometry, scene, renderer |
| 연결 | `components` | 장면 모델과 시스템을 잇는 컴포넌트 | 위의 모든 모듈 |
| 씬 파일 | `sceneio` | 씬 · 프리팹 읽기 · 쓰기(SceneSerializer · ComponentSerializer · PrefabManager, 2026-11-18 S7 에 assets 에서 옮김) | components 와 그 아래(renderer · xr · ui 등은 쓰지 않음) |
| 조립 | `runtime` | Engine(루프) · 실행 옵션 · 앞단 · 게임 모듈 인터페이스 · ModuleManager · 셰이더 묶음 짝 확인 | sceneio 와 그 아래 |
| 앞단 | `game` · `editor` | 게임 실행 파일 · 에디터 | runtime 과 그 아래(서로는 안 됨) |
| 도구 | `tools` | HarukaPak 등 | core, vfs |
| 진입점 | `main`(src/main.cpp · src/AndroidMain.cpp) | 실행 파일 진입점 | 모두 |

규칙의 근거:
- **기반은 장면을 모릅니다.** 설정 파일 해석이 컴포넌트 필드 헤더를 쓰던 것을 `core/FieldParse.hpp` 로 떼어 냈습니다(2026-11-18).
- **장면은 시스템을 모릅니다.** 시스템(물리 · 소리 · 렌더)이 장면을 끌어다 씁니다(`componentsOf<T>`).
- **렌더러는 장면을 모르는 것이 목표입니다.** 컴포넌트에서 렌더 데이터를 뽑는 추출 단계를 렌더러 밖으로 옮깁니다(2단계 S5,
  UE 의 SceneProxy · Godot 의 RenderingServer 와 같은 생각). 그때까지는 허용 목록에 있습니다.
- **조립부(runtime)는 앞단을 모릅니다.** 게임 · 에디터가 `IEngineFrontend` 로 Engine 에 붙습니다.
- **씬 파일은 컴포넌트 위에 있습니다.** 옛 v3 씬 형식(LIGHT: · CAMERA: · RIGID: 줄)을 읽으려면 컴포넌트 형을 알아야 해서, 에셋 층이
  아니라 컴포넌트 위 `sceneio` 에 둡니다(UE 도 레벨 직렬화는 컴포넌트를 아는 Engine 모듈에 있음). v4 는 레지스트리로 씁니다(DEF-22).

## 3. core 분할 (S7 일부, 2026-11-18)

예전 `src/core` 를 셋으로 나눴습니다. 파일 이동과 include 경로 · CMake 목록만 바꾼 커밋이라 `git log --follow` 로 이력을 따라갈 수 있습니다.

| 폴더 | 파일 |
|---|---|
| `src/core`(기반) | AppPaths · Bounds · DebugDraw · EngineSettings · FieldParse(새) · FileUtil · FramePacing · FrameTimings · HDelegate · HLog · IEngineModule · ProjectPaths · ProjectSettings · TextFloat · TextUtf · Transform |
| `src/scene`(장면) | Actor · Component · ComponentFields · ComponentRegistry · Reflect · SceneLog · UnknownComponent · World |
| `src/runtime`(런타임) | Engine · EngineOptions · IEngineFrontend · IGameModule · ModuleManager · ShaderManifest |

`IEngineModule` 은 물리 · 소리가 구현하는 인터페이스라 기반에, 모듈을 들고 돌리는 `ModuleManager` 는 런타임에 두었습니다.

## 4. 아키텍처 시험

- 위치: `tests/ArchitectureTest.cpp`(HarukaArchTest, ctest 이름 `architecture_deps`), 규칙 `tests/ArchitectureRules.txt`.
- 방법: src 아래 `.cpp · .hpp · .h · .inl` 의 `#include "..."` 를 파일 기준 → src 기준 순서로 실제 파일에 풉니다(못 푸는 것은 생성
  파일 · 외부 헤더라 건너뜀). 모듈은 src 아래 첫 폴더이고, src 바로 아래 파일은 `main` 입니다.
- 검사:
  1. 규칙 파일에 모르는 줄 · 중복이 없음
  2. src 아래 모든 폴더에 규칙이 있음(새 모듈은 규칙부터)
  3. 규칙의 모듈 이름이 실제 모듈
  4. 허용 방향 표에 순환이 없음
  5. 모듈마다 모든 include 가 허용 방향 안이거나 허용 목록에 있음
  6. 허용 목록의 항목이 모두 아직 남은 위반임(고친 것 · 원래 허용되는 것은 지워야 통과) → 목록은 줄기만 합니다
  7. 정적 시스템 포인터가 없음(S3, 8절): `static inline T*` · `static T* current()` · `instance()`
  8. 전역 싱글턴(`static T& get()`)이 정한 목록(`HInputSystem` · `ComponentRegistry`)과 같음(S3, 8절)
- `HarukaArchTest --list`: 지금 위반을 허용 목록 형식으로 출력합니다.
- 변이 확인(2026-11-18): 기반 → 에디터 include 추가, 필요 없는 허용 항목, 표의 순환(math ↔ core), 규칙 없는 새 폴더, 필요한
  허용 항목 삭제 다섯 가지를 모두 잡았습니다.

## 5. 남은 위반 (허용 목록, 2026-11-18 시작 36개 → S3 뒤 29개 → S4 뒤 28개 → S7 씬 파일 이동 뒤 22개 → 컴포넌트 이동 뒤 19개, 모두 2단계 몫)

| 방향 | 수 | 원인 | 없앨 단계 |
|---|---|---|---|
| audio → components | 1 | 청취자 카메라 | 2단계 S7 |
| geometry → assets | 1 | 메시 데이터(MeshSource)의 자리 | 2단계 S5 |
| renderer → assets · components · scene | 17 | 렌더 데이터 추출이 렌더러 안 | 2단계 S5 |

## 6. 단계와 진행

| 단계 | 작업 | 상태 |
|---|---|---|
| 1 | S0 미커밋 작업 주제별 커밋 | 끝남(2026-11-18, 52671ed ~ 368be4d) |
| 1 | S1 이 문서 + 아키텍처 시험 | 끝남(2026-11-18) |
| 1 | S7 일부 core → core · scene · runtime | 끝남(2026-11-18) |
| 1 | S2 `Engine::run` → 명시적 프레임 단계, XR 복구 · 리그 원점 · UI 포인터 · 프레임 통계를 각자 객체로 | 끝남(2026-11-18, 7절) |
| 1 | S3 시스템 접근 통일(엔진 단위 · 월드 단위 서비스), 정적 포인터 · 흩어진 전역 정리, World 의 에디터 상태 | 끝남(2026-11-18, 8절) |
| 2 | S4 Component 기반 정리(에디터 함수 → 등록식 visualizer, collisionMesh · blocksFloor → 질의) | 끝남(2026-11-18, 9절) |
| 2 | S7 나머지: 컴포넌트를 소유 시스템 옆으로 | 다음 |
| 2 | S5 렌더 경계(추출을 렌더러 밖으로, GPU 핸들 → 렌더러 표, AssetManager → 업로드 인터페이스) | |
| 3 | S6 EditorApp 분해(아웃라이너 · 디테일 · 로그 · 도킹 · 메뉴와 툴바 · 뷰포트 컨트롤러) | |

완료 기준:
- 1단계: 1단계 몫의 허용 목록 항목 0(core · scene · runtime 순환 0), `Engine::run` 100줄 이하(단계 호출만), 정적 시스템 포인터 0. → **채움(2026-11-18, S3 뒤)**
- 2단계: renderer 가 components 헤더를 include 하는 곳 0, Component 기반 클래스의 에디터 그리기 · 시스템별 질의 함수 0(S4 에서 채움.
  속성 목록 · 편집 확정 알림은 리플렉션으로 남김, 9절), 허용 목록의 컴포넌트 관련 항목 0.
  매번 기준 PNG 전부(왼눈 · 오른눈 · Quest 값 · LightingProbe · UIPanelProbe) + 검증 레이어 0.
- 3단계: EditorApp.cpp 1,000줄 이하, 함수 150줄 이하, 창 에디터 확인 통과.

## 7. 프레임 단계 (S2, 2026-11-18)

예전 `Engine::run` 은 약 410줄 한 함수에 XR 세션 복구 · 리그 원점 · 청취자 · Play 전이 · 틱 · 패널 · 포인터 · 렌더 분기 · 통계가
섞여 있어, 프레임 안의 순서가 코드 위치로만 정해졌습니다. 이제 `run` 은 아래 단계를 차례로 부르기만 합니다(20줄). 로직은 그대로
옮겼고, 단계 사이에 넘기는 값은 루프 1 회의 `FrameState`, 루프 사이에 이어지는 값은 `LoopState` 에 둡니다(Engine.cpp).

| # | 단계 | 하는 일 |
|---|---|---|
| 1 | `beginLoop` | 창 닫기 요청(앞단에 물음) · 창 이벤트 · 최소화 판정 |
| 2 | `beginXrFrame` | Play 에 세션 맞춤 · 세션 없을 때 이벤트 · 다시 열기 · 화면 카메라와 리그 공간 · XR 프레임 시작(입력) |
| 3 | `applyViewPose` | 화면 카메라의 XR 원점 · 소리 청취자 자세 |
| 4 | `readDesktopInput` | 프레임 시간 · 창 크기 · 마우스 · 오른쪽 버튼 카메라 모드 |
| 5 | `updatePlayState` | 헤드셋 초점 일시정지 · Play 시작 · 끝 · 일시정지 알림 |
| 6 | `tickFrame` | 앞단 tick · 디버그 선 비우기 · 입력 갱신 · 월드 · 게임 · 모듈 · 따라오는 패널 · 월드 UI 포인터 |
| 7 | `frontendInput` | 앞단 입력(에디터 클릭 처리) |
| 8 | `renderFrame` | XR · 합성 눈 · 데스크톱 중 하나 |
| 9 | `endLoop` | 자동 실행 끝내기 · FPS 제목 · 프레임 통계 · 최소화 중 쉼 |

상태를 가진 관심사는 각자 객체로 옮겼습니다(모두 `src/runtime`).

| 객체 | 맡는 일 | 예전 자리 |
|---|---|---|
| `XrSessionController` | Play 에 맞춘 세션 열기 · 닫기, 손실 이유별 처리(데스크톱으로 계속 · 다시 열기 · 앱 끝내기), XR 프레임 시작 · 입력 · 물리 스텝 맞춤 | `syncXrWithPlay` · `startXrSession` · `endXr` · `onXrInstanceLost` + 루프의 XR 구간 |
| `XrRecovery`(순수 헤더) | 다시 열기 시각: 2초에서 30초까지 두 배, 처음 잃은 때부터 30초 마감, 다시 연 세션이 5초 이어서 돌면 복구 끝(DEF-155) | 루프 안의 시각 변수 다섯 개 |
| `XrViewOrigin` | 화면 카메라의 XR 원점(씬 리그 · 추적 부모 DEF-144 · 보이지 않는 리그)과 리그 공간 | 루프 안 + 멤버 셋 |
| `WorldUiDriver` | 따라오는 패널, 마우스 · XR 두 손 포인터 | `updateFollowPanels` · `updateWorldUiPointers` + 멤버 다섯 |
| `FrameStats` | FPS 제목, 프레임 시간 분포 · 끊김 로그, 시작 시간 | `logFramePacing` + 루프 끝 + 멤버 아홉 |

세션이 끝날 때의 후처리(프레임 시간 분포 로그, 원점 초기화)는 `XrSessionController::Deps::onSessionEnded` 로 Engine 이 넘깁니다.

확인: 헤드셋 없이는 XR 경로를 돌릴 수 없으므로, 옛 코드에 이름 바꾸기만 적용해 새 코드와 줄 단위로 대조했고(남은 차이는
다시 열기 계산을 `XrRecovery` 로 바꾼 것 · 콜백 · 함수 머리), 다시 열기 시각에 검사 16개를 붙였습니다(`headless_render`, 변이
간격 3배 · 또 잃을 때 마감 다시 잡기 · 복구 판정 1초를 모두 잡음). 기준 PNG 전부 그대로, Debug · Quest Pro 형 프로필 검증 0.
헤드셋 확인(세션 시작 · Stop · 헤드셋 벗기 · 연결 끊김 다시 열기)은 다음 헤드셋 점검 때 함께 봅니다.

## 8. 시스템 접근 (S3, 2026-11-18)

예전에는 시스템에 닿는 길이 다섯 가지였습니다(1절 3번). 이제 아래 세 가지로 정했고, 아키텍처 시험이 셋째(전역 싱글턴)의 목록과
정적 시스템 포인터 0 을 지킵니다.

| 범위 | 찾는 길 | 들어 있는 것 |
|---|---|---|
| 엔진 단위 | `EngineContext`(게임 모듈이 받음). 동적 모듈은 `ctx.modules.get<T>()` | 월드 · 에셋 · 창 · 모듈 · 렌더 확장 등록 · 컴포넌트 형 등록 |
| 월드 단위 | `World::service<T>()`. 컴포넌트는 `owner->world()` 로 자기 월드를 찾음 | `PhysicsSystem`(startup ~ shutdown 사이), `ViewCameraOverride`(에디터) |
| 프로세스 전역 | `T::get()`, 정한 목록만 | `HInputSystem`(장치 입력은 프로세스에 하나), `ComponentRegistry`(컴포넌트 형 표) |

Engine 이 직접 든 시스템 멤버(`physics_` · `audio_`)는 조립부 안의 배선이라 그대로 둡니다(바깥에 열지 않음). 월드 단위 서비스는
UE 의 UWorldSubsystem 과 같은 자리이며, 월드(scene)는 형을 모른 채 포인터만 듭니다. 넣은 쪽이 소유하고, 없어지기 전에 뺍니다.

바꾼 것:
- **정적 포인터 `PhysicsSystem::current()` → 월드 서비스.** 물리 시스템은 startup 과 setWorld 가 모두 끝난 때 그 월드에 자신을 넣고,
  shutdown 하거나 다른 월드로 옮길 때 뺍니다. 리그 이동 · 텔레포트(`XrComponents.cpp`)는 자기 월드의 물리를 씁니다. 예전에는
  프로세스에 하나뿐인 포인터라 다른 월드의 액터도 같은 물리를 봤습니다.
- **World 의 에디터 상태와 컴포넌트 찾기 → 컴포넌트 쪽.** `editorCam_` · `playing_` · `getActiveCamera` · `getVisibleMeshes` ·
  `getLights` 를 지우고 `CameraComponent::activeIn` · `StaticMeshComponent::visibleIn` · `LightComponent::enabledIn` 으로 옮겼습니다.
  에디터는 `ViewCameraOverride{camera, preferred}` 를 월드 서비스로 넣습니다(편집 중 preferred = 참, Play 중 거짓). 고르는 규칙은
  예전과 같습니다. 게임 실행 파일은 넣지 않으므로 첫 primary 카메라입니다(예전에도 에디터 카메라가 없어 같았음). scene → components 3개.
- **게임 모듈:** Engine 이 `DefaultGameModule` 을 직접 만들던 것을 `createGameModule()`(`runtime/IGameModule.hpp`, 앞단의
  `createEngineFrontend` 처럼 링크한 쪽이 정의)로 바꿨습니다. runtime → game 1개.
- **Android 진입점:** `platform/android/AndroidMain.cpp` → `src/AndroidMain.cpp`(main.cpp 와 같은 자리, 모듈 main). platform → runtime 3개.
- **아키텍처 시험 검사 2개:** 정적 시스템 포인터(`static inline T*` · `static T* current()` · `instance()`) 0, 전역 싱글턴
  (`static T& get()`)이 위 목록과 같음(늘어도 없어져도 실패). 임시 파일에 셋을 다 넣으면 둘 다 실패하는 것을 확인했습니다.

남긴 것(2단계 S7 몫): `GrabbableComponent::live()`(잡을 수 있는 것의 정적 목록, 월드를 가리지 않음)와 `TrackedPoseComponent` 의
`g_liveHands` 같은 컴포넌트 안의 정적 값은 컴포넌트를 소유 시스템 옆으로 옮길 때 월드 단위로 바꿉니다. 지금 `live()` 를 월드 목록으로
바꾸면 잡기 후보의 거리 동점 순서가 켜진 순서에서 액터 순서로 바뀌어, 동작 보존 리팩터링이 아닙니다.

확인: 시험 27 · 검사 1,947(아키텍처 +2, 물리의 월드 서비스 +3), 기준 PNG 전부 그대로(왼눈 · 오른눈 · UIPanelProbe), Debug · Quest Pro 형
프로필 검증 0, Android 빌드 통과. 에디터의 화면 카메라(편집 중 에디터 카메라 · Play 중 플레이어 카메라 · Stop 뒤 되돌아옴)는 창 에디터
확인 때 봅니다.

1단계 완료 기준을 모두 채웠습니다: 1단계 몫 허용 항목 0(36 → 29, 남은 것은 모두 2단계 몫), `Engine::run` 20줄, 정적 시스템 포인터 0.

## 9. Component 기반 정리 (S4, 2026-11-18)

예전 Component 기반 클래스는 장면 모델의 일 말고도 에디터 그리기 · 물리 질의 · XR 질의를 가상 함수로 들고 있었습니다(1절 5번).
다른 엔진에서는 이 일들이 각각 다른 자리에 있습니다: 에디터 그리기는 에디터 모듈에 형별로 등록하고(UE FComponentVisualizer, Godot
3D 기즈모 플러그인), 시스템별 질의는 그 질의를 받는 형만 인터페이스로 구현합니다(UE UINTERFACE). 반면 편집할 수 있는 속성 목록과
편집 확정 알림은 UE(UPROPERTY · PostEditChangeProperty) · Godot(_get_property_list · _validate_property) 모두 객체 쪽에 둡니다.

| 예전 Component 가상 함수 | 지금 자리 | 까닭 |
|---|---|---|
| `editorIcon` · `editorIconShape` · `drawEditorVisual` | `editor/ComponentVisualizers`(형별 등록, EditorApp 이 소유) | 에디터만 씀. 게임 실행 파일에 에디터 그리기 코드가 들어가지 않음 |
| `collisionMesh` | `ICollisionMeshSource`(`geometry/CollisionMesh.hpp`), StaticMeshComponent 가 함께 상속 | 물리만 묻는 질의 |
| `blocksFloor` | `INotFloorSurface`(`components/FloorSurface.hpp`), UIPanelComponent 가 함께 상속 | XR 리그 바닥 찾기만 묻는 질의 |
| `localBounds` | 그대로(Component) | 공간 표현. 에디터 픽 · XR 바닥 찾기가 함께 씀(Spatial_Representation_Design) |
| `buildProps` · `readOnlyInfo` · `onPropsCommitted` · `onTransformCommitted` | 그대로(Component) | 속성 리플렉션과 편집 확정 알림(다른 엔진도 객체 쪽) |
| `writeFields` · `readField` · `onFieldsLoaded` | 그대로(Component) | 직렬화(DEF-22) |

함께 옮긴 것: 충돌체 모양 열거형 `EPhysicsShape` · `EMotionType` · `HalfExt` 를 `components/RigidBodyComponent.hpp`(지금 `physics/`) 에서
`geometry/CollisionShape.hpp` 로 옮겨, 절차 도형 설명(`geometry/PrimitiveDesc.hpp`)이 컴포넌트를 include 하지 않습니다(허용 목록
geometry → components 1개, 29 → 28).

시각화는 형을 정확히 맞춰 찾습니다(`typeid`). 엔진 컴포넌트에는 시각화를 등록한 형을 상속한 형이 없어 예전 가상 함수와 결과가 같습니다.
게임 모듈의 컴포넌트에 아이콘을 붙이려면 에디터 쪽 등록 통로가 있어야 합니다(지금은 없음, 필요할 때 만듦).

확인: 옮긴 그리기 함수 여섯 개(소리 · 카메라 · 라이트 · 반사 캡처 · 잔향 구역 · 강체)와 아이콘 여섯 개(색 · 모양)를 옛 코드와 기계 대조해
차이 0(`build/chk/s4_cmp.py`, 멤버 앞 `c.` 와 색 변수 이름만 되돌림). 바닥 찾기 · 메시 충돌체 시험은 새 인터페이스로 그대로 통과합니다.
시험 27 · 검사 1,947, 기준 PNG 전부 그대로, Debug · Quest Pro 형 프로필 검증 0, Android 빌드 통과. 에디터의 아이콘 · 범위 표시는 창 에디터
확인 때 봅니다.

## 10. 다른 엔진 비교 (2026-11-18 보고)

| 주제 | 다른 엔진 | Haruka 적용 |
|---|---|---|
| 프레임 순서 | Unity PlayerLoop, UE TickGroup, Bevy schedule | `Engine::run` 의 순서를 단계 목록으로(S2) |
| 시스템 접근 | UE Subsystem(엔진 단위 · 월드 단위), Bevy Resource | 엔진 단위 · 월드 단위 두 갈래(S3) |
| 렌더 경계 | UE SceneProxy + FScene, Godot RenderingServer + RID | 추출을 렌더러 밖으로, GPU 핸들은 렌더러 표(S5) |
| 에디터 기능 | UE FComponentVisualizer, Unity Editor 어셈블리 | 에디터 가상 함수 → 등록식 visualizer(S4) |
| 모듈 배치 | Godot · Bevy: 기능 모듈이 자기 컴포넌트 소유 | 컴포넌트를 소유 시스템 옆으로(S7 나머지) |
| 의존 시험 | ArchUnit(Java) · NetArchTest(.NET)의 계층 규칙 시험 | include 방향 시험 + 줄기만 하는 허용 목록(S1) |
