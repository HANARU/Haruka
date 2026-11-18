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
| 3 | 시스템 접근 방식 다섯 가지 | `EngineContext` · `ModuleManager::get<T>` · 전역 싱글턴 · 정적 포인터 `PhysicsSystem::current()` · Engine 이 직접 든 멤버 | 1단계 S3 |
| 4 | 렌더러가 장면 모델을 직접 읽음 | Renderer_render.cpp 가 World 와 컴포넌트 6종을 include, GPU 핸들을 컴포넌트에 씀 | 2단계 S5 |
| 5 | Component 기반 클래스가 모든 계층의 일을 가짐 | 에디터(아이콘 · 범위 표시 · 속성 칸) · 물리(collisionMesh) · XR(blocksFloor) · 직렬화 | 2단계 S4 |
| 6 | 에디터 한 클래스 | EditorApp.cpp 4,492줄, 함수 85개 | 3단계 S6 |
| 7 | World 가 에디터 상태를 앎 | `editorCam_` · `playing_` | 1단계 S3 |

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
| 시스템 | `physics` | Jolt 물리 | core, math, scene, geometry |
| 시스템 | `audio` | miniaudio · 차폐 · 잔향 · HRTF | core, math, vfs, scene, physics |
| 시스템 | `renderer` | Vulkan 렌더러 · 패스 | core, math, vfs, memory, platform, ui, geometry |
| 시스템 | `xr` | OpenXR · 손 쥐기 풀이 | core, math, input, platform, renderer |
| 시스템 | `assets` | 에셋 · 씬 · 프리팹 불러오기 | core, math, vfs, geometry, scene, renderer |
| 연결 | `components` | 장면 모델과 시스템을 잇는 컴포넌트 | 위의 모든 모듈 |
| 조립 | `runtime` | Engine(루프) · 실행 옵션 · 앞단 · 게임 모듈 인터페이스 · ModuleManager · 셰이더 묶음 짝 확인 | components 와 그 아래 |
| 앞단 | `game` · `editor` | 게임 실행 파일 · 에디터 | runtime 과 그 아래(서로는 안 됨) |
| 도구 | `tools` | HarukaPak 등 | core, vfs |
| 진입점 | `main`(src/main.cpp) | 실행 파일 진입점 | 모두 |

규칙의 근거:
- **기반은 장면을 모릅니다.** 설정 파일 해석이 컴포넌트 필드 헤더를 쓰던 것을 `core/FieldParse.hpp` 로 떼어 냈습니다(2026-11-18).
- **장면은 시스템을 모릅니다.** 시스템(물리 · 소리 · 렌더)이 장면을 끌어다 씁니다(`componentsOf<T>`).
- **렌더러는 장면을 모르는 것이 목표입니다.** 컴포넌트에서 렌더 데이터를 뽑는 추출 단계를 렌더러 밖으로 옮깁니다(2단계 S5,
  UE 의 SceneProxy · Godot 의 RenderingServer 와 같은 생각). 그때까지는 허용 목록에 있습니다.
- **조립부(runtime)는 앞단을 모릅니다.** 게임 · 에디터가 `IEngineFrontend` 로 Engine 에 붙습니다.

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
- `HarukaArchTest --list`: 지금 위반을 허용 목록 형식으로 출력합니다.
- 변이 확인(2026-11-18): 기반 → 에디터 include 추가, 필요 없는 허용 항목, 표의 순환(math ↔ core), 규칙 없는 새 폴더, 필요한
  허용 항목 삭제 다섯 가지를 모두 잡았습니다.

## 5. 남은 위반 (허용 목록, 2026-11-18 시작 36개)

| 방향 | 수 | 원인 | 없앨 단계 |
|---|---|---|---|
| scene → components | 3 | World 의 카메라 · 메시 · 라이트 찾기 | 1단계 S3 |
| runtime → game | 1 | 기본 게임 모듈을 Engine 이 직접 만듦 | 1단계 S3 |
| platform → runtime | 3 | Android 진입점(AndroidMain.cpp)이 platform 안에 있음 | 1단계(진입점을 main 자리로) |
| assets → audio · components | 6 | 씬 직렬화가 컴포넌트 형을 직접 앎 | 2단계 S4 · S7 |
| audio → components | 3 | 소리 컴포넌트 · 청취자 카메라 | 2단계 S7 |
| physics → components | 1 | RigidBodyComponent | 2단계 S7 |
| geometry → assets · components | 2 | 메시 데이터 자리, 절차 도형 설명이 충돌체 형을 앎 | 2단계 S4 · S5 · S7 |
| renderer → assets · components · scene | 17 | 렌더 데이터 추출이 렌더러 안 | 2단계 S5 |

## 6. 단계와 진행

| 단계 | 작업 | 상태 |
|---|---|---|
| 1 | S0 미커밋 작업 주제별 커밋 | 끝남(2026-11-18, 52671ed ~ 368be4d) |
| 1 | S1 이 문서 + 아키텍처 시험 | 끝남(2026-11-18) |
| 1 | S7 일부 core → core · scene · runtime | 끝남(2026-11-18) |
| 1 | S2 `Engine::run` → 명시적 프레임 단계, XR 복구 · 리그 원점 · UI 포인터 · 프레임 통계를 각자 객체로 | 끝남(2026-11-18, 7절) |
| 1 | S3 시스템 접근 통일(엔진 단위 · 월드 단위 서비스), 정적 포인터 · 흩어진 전역 정리, World 의 에디터 상태 | 다음 |
| 2 | S4 Component 기반 정리(에디터 함수 → 등록식 visualizer, collisionMesh · blocksFloor → 질의) | |
| 2 | S7 나머지: 컴포넌트를 소유 시스템 옆으로 | |
| 2 | S5 렌더 경계(추출을 렌더러 밖으로, GPU 핸들 → 렌더러 표, AssetManager → 업로드 인터페이스) | |
| 3 | S6 EditorApp 분해(아웃라이너 · 디테일 · 로그 · 도킹 · 메뉴와 툴바 · 뷰포트 컨트롤러) | |

완료 기준:
- 1단계: 1단계 몫의 허용 목록 항목 0(core · scene · runtime 순환 0), `Engine::run` 100줄 이하(단계 호출만), 정적 시스템 포인터 0.
- 2단계: renderer 가 components 헤더를 include 하는 곳 0, Component 기반 클래스의 에디터 함수 0, 허용 목록의 컴포넌트 관련 항목 0.
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

## 8. 다른 엔진 비교 (2026-11-18 보고)

| 주제 | 다른 엔진 | Haruka 적용 |
|---|---|---|
| 프레임 순서 | Unity PlayerLoop, UE TickGroup, Bevy schedule | `Engine::run` 의 순서를 단계 목록으로(S2) |
| 시스템 접근 | UE Subsystem(엔진 단위 · 월드 단위), Bevy Resource | 엔진 단위 · 월드 단위 두 갈래(S3) |
| 렌더 경계 | UE SceneProxy + FScene, Godot RenderingServer + RID | 추출을 렌더러 밖으로, GPU 핸들은 렌더러 표(S5) |
| 에디터 기능 | UE FComponentVisualizer, Unity Editor 어셈블리 | 에디터 가상 함수 → 등록식 visualizer(S4) |
| 모듈 배치 | Godot · Bevy: 기능 모듈이 자기 컴포넌트 소유 | 컴포넌트를 소유 시스템 옆으로(S7 나머지) |
| 의존 시험 | ArchUnit(Java) · NetArchTest(.NET)의 계층 규칙 시험 | include 방향 시험 + 줄기만 하는 허용 목록(S1) |
