# Haruka 엔진 사양 명세서

> **문서 지위**: 엔진의 정체성 · 개발 이력 · 현재 구현된 기능 · 미구현 기능 · 우려되는 점을 한곳에 모은 **종합 요약 문서**입니다.
> 개별 항목의 정본은 각 설계 · 결함 문서이며, 이 문서와 정본이 어긋나면 정본을, 정본과 코드가 어긋나면 코드를 따릅니다.
>
> **기준 시점**: 커밋 `5a9cb51`(2026-10-15). 수치와 기능 서술은 이 시점의 `src/` · `shaders/` · `tests/` 코드와 빌드 결과를
> 직접 확인해 적었습니다. 이 문서를 갱신할 때도 문서가 아니라 코드를 먼저 확인하십시오.
>
> **갱신(2026-10-15)**: 에디터의 액터 생성 · 삭제 Undo 를 4-11 · 4-12 · 5-4 · 7장에, XR 머리 자세 청취자를 4-8 · 5-3 에, 프로젝트 폴더 에셋 루트를 4-6 에 반영하고, 1-3 · 3-2 의 소스 · 테스트 수치를 이
> 변경까지 다시 셌습니다. 커밋 수와 결함 수는 기준 시점 그대로입니다.

---

## 1. 개요

### 1-1. 정체성

**Haruka 는 Vulkan 기반 VR 엔진입니다.** 우주 · 무중력 기능은 그 코어 위에 얹을 첫 번째 도메인(Layer 2)이며, 엔진의 정의가
아닙니다. 코어(Layer 1)는 우주 도메인을 걷어내도 그 자체로 쓸 수 있는 범용 VR 엔진이어야 합니다. 정본은
[Layer_Architecture.md](Layer_Architecture.md) 와 [Engine_Strategy_Evaluation.md](Engine_Strategy_Evaluation.md) 입니다.

| 항목 | 내용 |
|---|---|
| 최종 목표 기기 | Meta Quest Pro(Snapdragon XR2+ Gen 1) 단독 실행 VR |
| 현재 실행 형태 | Windows 데스크톱 애플리케이션 하나. 엔진과 에디터가 같은 창에서 함께 돕니다 |
| VR 상태 | PC OpenXR 연결(`--xr`, Quest Link 등)과 두 눈 렌더가 들어 있으나 **헤드셋에서 한 번도 확인하지 않았습니다** |
| 언어 · API | C++20, Vulkan 1.4, GLSL(빌드 시 SPIR-V 로 컴파일) |
| 개발 형태 | 1인 개발. 외부 엔진 프레임워크 없이 렌더러 · GPU 메모리 할당자 · UI 위젯 시스템 · 에디터를 직접 구현 |
| 저장소 | `github.com/HANARU/Haruka`(공개). 라이선스 파일 없음 |

### 1-2. 설계 원칙

- **안전은 규율이 아니라 구조로 만듭니다.** "호출부가 잊지 않는 것"에 걸린 안전을 타입 · 이벤트 · 메커니즘으로 강제합니다
  (예: 월드 행렬 캐시 제거, `Transform` 필드 캡슐화, 커맨드 스택의 액터 참조 자동 제거). [Roadmap.md](Roadmap.md) 머리말.
- **고치기 전에 다른 엔진의 해법을 조사합니다.** UE · Unity · Godot · Bevy 등의 방법과 Haruka 에 맞는지를 먼저 확인하고, 사용자가
  결정한 뒤 구현합니다. 2026-10-04 의 사용자 지시 이후 결함 수정은 이 순서를 따랐습니다. 예외로, 2026-10-14 일괄 점검에서
  찾은 DEF-105 ~ 109 는 사용자 지시("내 판단 없이 스스로 작업 진행해")에 따라 조사한 뒤 사용자 결정 없이 바로 고쳤습니다.
- **모바일 XR 예산을 먼저 따집니다.** 포워드 렌더링(G-buffer 없음), 한 렌더 패스 안의 서브패스 3개(타일 GPU), 1GB 단일 힙은
  Quest 를 전제로 한 선택입니다.
- **단위는 1 unit = 1 m, 1 kg** 입니다(2026-08-25 확정). OpenXR 과 Jolt 가 미터 기준이기 때문입니다.
- **엔진 데이터는 메인 스레드가 소유합니다.** [Threading.md](Threading.md).
- **엔티티 모델은 Unity 식, UI 와 이름은 UE 식**입니다. Actor = GameObject, Component 조합, `activeSelf` / `activeInHierarchy` 는
  Unity 를 따르고, 에디터 UI(HSlate)와 명칭(Actor · tick · BeginPlay, 라이트 용어)은 UE 를 따릅니다.

### 1-3. 규모 (2026-10-15)

| 항목 | 값 |
|---|---|
| 소스 | `src/` 의 `.cpp` · `.hpp` 153 파일, 24,981 줄 |
| 셰이더 | GLSL 19 파일(셰이더 18 + 공용 헤더 1), 705 줄. 컴퓨트 셰이더 없음 |
| 자동 테스트 | 15개 실행 파일, 검사 540개(`ctest -C Release`) |
| 문서 | `Docs/` 활성 문서 25개(색인 `README.md` 제외, 이 문서 포함) + 아카이브 6개(외부 엔진 분석, 신뢰도 낮음) |
| 커밋 | 44개. 기준 시점에는 17개가 원격에 없었으나, 2026-10-15 에 그 뒤 커밋까지 모두 원격에 올렸습니다 |
| 결함 대장 | 109건 등재: 해결 94 · 열림 9 · 보류 3 · 닫음 3(자동 조작 환경의 착시) |

---

## 2. 개발 이력

### 2-1. 저장소 기록에 대한 주의

처음 13개 커밋(`762aa6f` ~ `fc9113b`)은 날짜가 2025-06-03 부터 2026-06-03 까지 **매월 3일로 고르게** 찍혀 있고, 빌드 · 할당자 ·
코어 · 렌더러 · 에셋처럼 구성 요소 단위로 나뉘어 있습니다. 이어지는 두 커밋도 같은 형식이고, 2026-07-03 커밋의 "도킹 Phase 0"은
문서상 2026-08-27 에 끝난 작업입니다. 따라서 초기 커밋은 실제 작업 순서와 날짜를 기록한 것이 아니라 저장소에 올리면서 정리한 것으로
보입니다. 아래 연표의 날짜는 커밋 날짜가 아니라 설계 · 결함 문서와 작업 기록에 적힌 날짜를 따릅니다.

### 2-2. 연표

| 시기 | 작업 | 결과 |
|---|---|---|
| 초기(저장소 기록상 2025-06 ~ 2026-06) | 빌드 체계, GPU 코어 힙 할당자, 창 · 입력 추상화, 씬 모델(World · Actor · Component), Vulkan 렌더러 기반, glTF 가져오기와 바이너리 캐시, 오디오 · 물리 모듈, 절차 도형, 렌더 파이프라인, HSlate UI, 씬 직렬화, UE5 식 에디터 셸, 첫 단위 테스트 | 엔진의 뼈대 |
| 2026-05-12 ~ 05-14 | HSlate Phase 1 · 2, 에디터의 컴포넌트 UI · 삭제 · 빈 액터 · 기즈모 · F2 리네임 | [CHANGES.md](CHANGES.md) |
| 2026-06-11 | 렌더러 패스 구조 리팩터링(RenderContext · GpuScene · passes · HPass 수명주기 · RenderView), 스레딩 규칙 명문화 | [RendererRefactor.md](RendererRefactor.md), [Threading.md](Threading.md) |
| 2026-08-24 | 계층 아키텍처 기준 문서, 외부 엔진(Carbon) 분석 문서 정정, 결함 대장 DEF-01 ~ 22 | Layer 1 / Layer 2 경계 확정 |
| 2026-08-25 | 결함 11건 해결(섀도 비교 규약 반전으로 직접광이 사라지던 DEF-36, glTF 팩터 미적용 DEF-47 등), 단위 = 미터 확정 | 화면이 처음으로 의도대로 보임 |
| 2026-08-26 | GameObject 견고화(파괴 멱등 · 커맨드 스택 정리 · 활성 모델 · 계층 직렬화), 트랜스폼 설계(DEF-49 ~ 54), 엔티티 공간 표현 | [Transform_Design.md](Transform_Design.md), [Spatial_Representation_Design.md](Spatial_Representation_Design.md) |
| 2026-08-27 | 렌더링 결함 묶음(SSAO 밴딩 · 블룸 · 가짜 TAA 비활성 · GPU 컬링 중단), 뷰포트 렌더 분리, 라이팅 UE 용어 Phase 0 ~ 2(색온도 · 라이트별 그림자), `Engine` 클래스 부트스트랩, 에디터 Phase 0(도킹) | [Lighting_UE_Design.md](Lighting_UE_Design.md) |
| 2026-09-03 ~ 09-09 | 에디터 Phase 2(뷰포트 툴바 · 스냅) · Phase 3(아웃라이너 트리) · Phase 1(상태바), ECS 도입 보류 결정, HSlate 위젯 결함 수정 | [Editor_Layout_Design.md](Editor_Layout_Design.md), [ECS_Decision.md](ECS_Decision.md) |
| 2026-09-17 ~ 09-25 | 로그 카테고리 체계 L0 ~ L4(기록 수준과 표시 필터 분리, Output Log 필터, UTF-8 로그 파일), 실행 파일 아이콘 | [Log_Category_Design.md](Log_Category_Design.md) |
| 2026-10-02 ~ 10-03 | 절차 도형 직렬화, 사운드 기초, **종합 오류 검출**(DEF-76 ~ 94 등재, 잠재 DEF-91 을 뺀 전부 수정), 기반 공사(클리핑 · 위젯 휠 · 키 반복 · 글자 폭 실측) | [Audio_Design.md](Audio_Design.md), [Editor_QA_Checklist.md](Editor_QA_Checklist.md) |
| 2026-10-04 | **남은 결함 정리** 1 · 2단계(셰이더 의존성 · 감마 이중 적용 · 깊이 규약 · 역제곱 감쇠 · 숫자 칸 정밀도), **VR 첫걸음**(OpenXR 연결 · 두 눈 렌더 · 두 눈 컬링 · VR 입력 · 물리 API) | 커밋 `cbae320`, `5d3a86b` |
| 2026-10-14 ~ 10-15 | 남은 결함 정리 "나머지": 자동 인스턴싱(DEF-16), 렌더 확장 지점(DEF-17 · 18), 직렬화 타입 등록과 씬 v4(DEF-22), 라이트 선택(DEF-33) · 상한 설정 파일(DEF-10), **일괄 점검**과 그 과정에서 찾은 DEF-105 ~ 109 수정 | 커밋 `744cc69` ~ `5a9cb51` |

### 2-3. 주요 결정 기록

| 결정 | 시기 | 내용 | 정본 |
|---|---|---|---|
| Vulkan 단일 RHI | 초기 | 다른 그래픽 API 를 추상화하지 않습니다 | [Vulkan_Single_RHI_Decision.md](Vulkan_Single_RHI_Decision.md) |
| 자체 메모리 할당자 | 초기, 결정은 보류 | VMA 도입 여부는 아직 보류입니다. 그동안 1GB 단일 힙을 세 구역으로 나눈 `HarukaAllocator`(옵션 B)를 운용합니다 | [Memory_Allocator_Decision.md](Memory_Allocator_Decision.md) |
| 포워드 렌더링 | 초기 | 디퍼드는 MSAA 와 맞지 않고 모바일 타일 GPU 의 대역폭을 많이 써서 배제합니다 | [Architecture_Overview.md](Architecture_Overview.md), [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md) 2-K |
| 단위 = 미터 | 2026-08-25 | 1cm 단위 안을 검토한 뒤 미터를 유지했습니다 | [Layer_Architecture.md](Layer_Architecture.md) §2 |
| 라이팅은 "UE 명칭만" | 2026-08-27 | UE 이름 · 드롭다운만 빌리고 값은 상대값입니다. 물리 단위 · 노출계는 쓰지 않습니다. 점광원 감쇠 모양만 역제곱으로 바꿨습니다(2026-10-04) | [Lighting_UE_Design.md](Lighting_UE_Design.md) |
| ECS 도입 보류 | 2026-09-08 | Actor + Component 를 유지합니다. 측정상 프레임은 후처리(AO · Bloom)가 지배하는 GPU 바운드였고, 대규모 파편장을 막던 것은 데이터 배치가 아니라 GPU 제출 경로(인스턴싱 부재)였습니다. 재검토 트리거를 수치로 정해 두었습니다 | [ECS_Decision.md](ECS_Decision.md) |
| 물리 규칙은 UE 식 | 2026-10-04 | 힘은 스텝에 나눠 넣고, 위치를 바꾸면 순간 이동 + 속도 유지, XR 중 물리 스텝 = 헤드셋 주사율 | [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) "3단계 VR 첫걸음" |
| 렌더 확장은 고정 지점 | 2026-10-14 | 렌더 그래프 대신 고정 지점 4곳 + 좁은 인터페이스 | 같은 문서 "DEF-17 · 18" |
| 컴포넌트 명시 등록 | 2026-10-14 | 전역 객체 자동 등록 대신 시작할 때 명시적으로 등록합니다(정적 라이브러리에서 링커가 버릴 수 있음) | 같은 문서 "DEF-22" |
| 자동 인스턴싱만 | 2026-10-14 | 명시적 인스턴싱 컴포넌트는 파편장 착수 때 만듭니다 | 같은 문서 "DEF-16" |
| 3D UI 는 render-to-texture | 2026-08-26 (2026-10-15 재확인) | `UIPanelComponent` 와 기존 `HWidget` 트리, HSlate 오프스크린 렌더. 미구현 | [Spatial_Representation_Design.md](Spatial_Representation_Design.md) 10절 |
| 라이트 방향은 회전에서만 | 2026-10-15 | `direction` 필드를 없애고 액터 회전의 -Z 를 씁니다. 다음 라이팅 작업 때 구현 | [Lighting_UE_Design.md](Lighting_UE_Design.md) 6절 4번 |
| 할당자 · 런타임 분리는 이식 때 조사 | 2026-10-15 | VMA 대 자체 할당자, 런타임 · 에디터 분리 방식은 Quest 이식 착수 때 다른 엔진을 조사한 뒤 정합니다 | [Roadmap.md](Roadmap.md) |

---

## 3. 구조

### 3-1. 계층

```
[Layer 2] 우주 · 무중력 도메인 (미착수: ZeroGController · DebrisField · 생존 규칙 · 우주 비주얼 · 정적 기지 건설)
            │ 단방향 의존 (EngineContext 로만 코어에 접근)
            v
[Layer 1] Haruka Core
   Engine(부트스트랩 · 메인 루프)
   ├─ 코어 서비스(고정 순서): 창 → (XR) → 렌더러 → 에셋 관리자
   ├─ 동적 모듈(ModuleManager): AudioSystem, PhysicsSystem
   ├─ 에디터(EditorApp, HSlate)
   └─ 게임 모듈(IGameModule, 기본은 DefaultGameModule: 시작 씬 불러오기)
```

Layer 2 는 `EngineContext` 로 월드 · 에셋 · 창 · 모듈 · 렌더 확장 등록(`render`) · 컴포넌트 등록(`components`)에만 닿습니다.

### 3-2. 소스 구성

| 디렉터리 | 파일 · 줄 | 내용 |
|---|---|---|
| `src/`(루트) | 1 · 105 | `main.cpp`(진입점: 콘솔 없이 직접 실행할 때 C stdio 출력을 `Logs/stdout.txt` 로 돌린 뒤 `Engine` 을 만들고 돌림) |
| `src/core/` | 25 · 2,334 | `Engine`, `World` · `Actor` · `Component`, `ModuleManager`, `HLog`, `ComponentRegistry`, `EngineSettings`, 에셋 루트(`ProjectPaths.hpp`), 리플렉션(`Reflect.hpp`) |
| `src/renderer/` | 24 · 4,204 | `Renderer`(프레임 구성), `RenderContext`, `GpuScene`, `FrameResources`, 스왑체인, 인스턴싱 묶기 · 라이트 선택 · 절두체 컬링 헤더, 렌더 확장 |
| `src/renderer/passes/` | 11 · 2,106 | Shadow · Geometry · AO · Bloom · Post 패스(`HPass` 수명주기) |
| `src/memory/` | 2 · 388 | `HarukaAllocator`(1GB 단일 힙, 세 구역) |
| `src/assets/` | 15 · 1,838 | `AssetManager`, glTF 로더, `.gasset` 캐시, 씬 · 프리팹 직렬화, 컴포넌트 블록 직렬화, 회전 글자 표현 |
| `src/geometry/` | 5 · 1,321 | 절차 도형 기술자와 메시 생성기 |
| `src/components/` | 12 · 1,024 | Light · Camera · StaticMesh · Audio · RigidBody, 기본 컴포넌트 등록 |
| `src/physics/` | 2 · 674 | `PhysicsSystem`(Jolt) |
| `src/audio/` | 5 · 453 | `AudioSystem`(miniaudio), 3D 감쇠 설정 |
| `src/input/` | 3 · 236 | `HInputSystem`(액션 · 축 바인딩), XR 입력 상태 |
| `src/xr/` | 2 · 746 | `XrSystem`(OpenXR 인스턴스 · 세션 · 스왑체인 · 입력) |
| `src/platform/` | 5 · 591 | `IWindow`, GLFW 구현, Android 스텁 |
| `src/ui/slate/` | 26 · 3,157 | HSlate: 위젯 14종, 배치 · 페인트 · 폰트 아틀라스 · 렌더러 |
| `src/editor/` | 13 · 5,761 | `EditorApp`, 기즈모, 커맨드 스택, 액터 생성 · 삭제 Undo 스냅샷, 도킹, 셰이더 핫 리로드 |
| `src/game/` | 2 · 43 | `DefaultGameModule` |

### 3-3. 부팅 · 메인 루프 · 종료

**부팅 순서**(`Engine::Engine`):
1. 기본 컴포넌트 5종 등록 → `Config/engine.ini` 읽기(없으면 기본값으로 생성).
2. GLFW 창(1600x900) → `--xr` 이면 OpenXR 초기화(렌더러보다 먼저, OpenXR 이 GPU 를 고름) → 렌더러 → XR 세션 · 스왑체인 연결 →
   `--emulate-stereo` 처리 → 에셋 관리자.
3. 동적 모듈(Audio, Physics) 등록 → 에디터 초기화 → 입력 바인딩(현재 비어 있음) → 게임 모듈 `onStart`(시작 씬 불러오기).

**메인 루프**(`Engine::run`, 프레임마다): 창 이벤트 → 최소화 확인 → XR 프레임 시작(세션이 돌면 헤드셋이 루프 속도를 정함) →
Play 상태 전이(BeginPlay · EndPlay · 일시정지 통지) → 에디터 tick · 입력 시스템 갱신 → **Play 중에만** 월드 · 게임 모듈 · 동적 모듈
tick → 에디터 입력 처리 → 렌더(XR 이면 두 눈 + 거울 화면) → 제목 표시줄 FPS. 최소화 중에는 에디터 UI 를 건너뛰고 약 60Hz 로 쉽니다.

**종료**는 대체로 부팅의 역순입니다. 실제 순서는 게임 모듈 → 동적 모듈 → 에셋 → 에디터 → XR 세션 → 렌더러 → XR 인스턴스이며,
에디터는 부팅 때 동적 모듈보다 늦게 만들지만 정리는 에셋 뒤에 합니다.

### 3-4. 스레드

| 스레드 | 역할 | 엔진 데이터 접근 |
|---|---|---|
| 메인 | 입력 · 틱 · 에디터 · 렌더 전체 | 전부(소유자) |
| 셰이더 감시 | `.spv` 변경 감시(`ReadDirectoryChangesW`) | atomic 플래그와 로그만 |
| Jolt 작업자 | 물리 스텝 내부 병렬화 | 없음(스텝 안에서 합류) |
| 오디오 | miniaudio 장치 콜백 믹싱 | 없음 |

에셋 로딩은 메인 스레드에서 동기로 합니다.

### 3-5. 외부 의존성

라이브러리는 모두 CMake FetchContent 로 구성 시점에 내려받습니다. 미리 설치할 도구는 Visual Studio(MSVC), CMake 3.21 이상,
Git(FetchContent 가 저장소를 받을 때 씀), Vulkan SDK(1.4.x, `glslc` 포함)입니다.

| 라이브러리 | 버전 | 용도 |
|---|---|---|
| GLFW | 3.4 | 창 · 입력(데스크톱) |
| glm | 1.0.1 | 수학(`GLM_FORCE_DEPTH_ZERO_TO_ONE` 전역) |
| tinygltf | v2.9.3 | glTF 2.0 읽기 |
| stb | `master`(고정되지 않음) | 폰트 래스터라이즈(stb_truetype)에만 씁니다. 이미지 디코드는 tinygltf 에 들어 있는 `stb_image.h` 가 맡으므로 tinygltf 버전과 함께 고정됩니다 |
| miniaudio | 0.11.21 | 오디오 장치 · 디코드(OGG 는 함께 들어 있는 stb_vorbis) |
| JoltPhysics | v5.1.0 | 물리 |
| OpenXR-SDK | release-1.1.63 | OpenXR 로더(정적 링크) |

---

## 4. 구현된 기능

### 4-1. 플랫폼 · 빌드

- Windows x64 + MSVC(Visual Studio 18 2026, 없으면 2022). CMake 3.21 이상(셰이더 include 추적에 depfile 사용).
- `build.bat [Debug|Release] [clean]`, `run.bat [Config] [--xr | --emulate-stereo]`. 빌드 로그는 `Logs/build_log.txt`.
- 셰이더는 빌드 시 `glslc` 로 SPIR-V 컴파일, include 의존성 추적, 목록 밖 셰이더 파일은 구성 단계에서 경고합니다.
- 실행 파일은 콘솔 창 없이(`/SUBSYSTEM:WINDOWS`) 뜨고, 콘솔이 필요하면 `-DHARUKA_CONSOLE=ON` 으로 구성합니다. 실행 파일 · 창
  아이콘이 리소스로 들어 있습니다.
- 경고 수준 `/W4`, 빌드 경고 0 을 유지합니다.

### 4-2. 코어

| 기능 | 내용 |
|---|---|
| 엔티티 모델 | `World` 가 `Actor` 를, `Actor` 가 `Component` 를 소유(`unique_ptr`). 컴포넌트 조회는 `type_index` 해시 캐시 |
| 활성 모델 | `activeSelf` / `activeInHierarchy`(부모 체인), 전이할 때만 서브트리에 `onEnable` · `onDisable` |
| 계층 | 부모 · 자식(`attachTo` · `detach`, 사이클 방지), 재귀 파괴, 틱 중 파괴는 지연 큐 |
| 트랜스폼 | 로컬 위치 · 쿼터니언 회전 · 스케일, 필드 비공개(세터로만 변경), 월드 행렬은 캐시 없이 매번 계산 |
| 파괴 감시 | `World::onActorDestroyed` 이벤트. 에디터가 구독해 커맨드 스택 · 선택 · 디테일 패널을 정리 |
| 모듈 | `IEngineModule`(startup · tick · BeginPlay · EndPlay · 일시정지 통지 · shutdown)을 `ModuleManager` 가 관리 |
| 게임 모듈 | `IGameModule`(onStart · onBeginPlay · onTick · onEndPlay · onShutdown), `EngineContext` 로 코어 접근 |
| 컴포넌트 등록 | `ComponentRegistry`(타입 이름 → 생성 · 찾기 함수, 옛 이름, 에디터 버튼 라벨). Layer 2 는 `EngineContext::components` |
| 리플렉션 | 디테일 패널용 속성 기술자(`Reflect.hpp`), 단위 종류(거리 · 크기 · 각도 · 비율)별 드래그 스텝 |
| 로그 | 카테고리별 기록 수준(Off · Error · Warning · Log · Verbose), `Config/log_levels.txt` 로 덮어쓰기, UTF-8 파일 `Logs/HarukaEngine.log`(줄마다 flush), 어느 스레드에서나 안전 |
| 상한 설정 | `Config/engine.ini`: `render.maxDrawRows`(기본 16384, 1024 ~ 131072) · `render.maxLights`(256, 1 ~ 4096) · `physics.maxBodies`(4096, 256 ~ 65536). 시작할 때 한 번 읽고, 범위 밖 · 깨진 값은 경고 |
| 계측 | CPU 구간 타이머(폴링 · 틱 · 입력 · 기록 · 제출 · 표시 등)와 GPU 타임스탬프(Shadow · Main · AO · Bloom · Tonemap · UI) |

### 4-3. 메모리

- 장치 메모리를 `vkAllocateMemory` 한 번으로 1GB 선점하고 전 범위를 지속 매핑합니다. 구역은 A 64MB(엔진 구조체, bump) ·
  B 896MB(에셋, free-list. 이미지는 시작 주소를 `bufferImageGranularity` 에 맞춰 정렬) · C 64MB(프레임별 동적 데이터, ring)이고 합이 1GB 임을 `static_assert` 로
  강제합니다.
- 메모리 타입은 `DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`(UMA · ReBAR)를 먼저 고르고, 없으면 `HOST_VISIBLE | HOST_COHERENT` 로
  내려갑니다(이때 이미지는 별도 `DEVICE_LOCAL` 할당). 힙의 75% 까지만 잡고, 실패하면 절반씩 줄여 256MB 까지 다시 시도합니다.
- 1GB 힙이 엔진 장치 메모리의 전부는 아닙니다. HSlate 의 폰트 아틀라스 · 흰 텍스처 · 정점/인덱스 버퍼(와 업로드용 스테이징),
  라인 · 기즈모 정점 버퍼, 위의 대체 경로 이미지는 힙 밖에서 `vkAllocateMemory` 로 따로 할당합니다.

### 4-4. 렌더러

**프레임 구성**: 섀도는 프레임에 한 번 그리고, 눈마다 메인 패스(서브패스 3개: 깊이 프리패스 → 불투명 PBR → 반투명 PBR ·
라인 · 기즈모, MSAA 를 HDR 로 resolve) → SSAO → Bloom → 톤매핑을 반복합니다. XR 이면 왼쪽 눈을 에디터 뷰포트에 거울 화면으로
한 번 더 톤매핑하고, 마지막에 UI 를 그립니다.

| 기능 | 사양 |
|---|---|
| 장치 요구 | Vulkan 1.4, 확장 `VK_KHR_swapchain` · `VK_KHR_push_descriptor`, 기능 `synchronization2` · `pushDescriptor` · `samplerAnisotropy` |
| 프레임 | 프레임 인 플라이트 2, MSAA 4x(장치가 못 하면 2x → 1x), HDR `R16G16B16A16_SFLOAT` |
| 셰이딩 | 메탈릭 · 러프니스 PBR(GGX · Smith · Schlick), 텍스처 5종(알베도 · 노멀 · 금속/거칠기 · AO · 이미시브) |
| 라이트 | Directional · Point · Spot, 색온도(Kelvin) 틴트, 라이트별 Cast Shadows. 점광원 · 스팟 감쇠는 역제곱 × 창 함수 `(1-(d/r)⁴)²`(UE · Frostbite · Filament) |
| 라이트 선택 | 어느 눈의 절두체와도 영향 구가 겹치지 않는 점광원 · 스팟은 올리지 않습니다(화면 불변). 상한 초과 시 방향광 우선, 나머지는 거리/반경 순 |
| 그림자 | `castShadows` 가 켜진 첫 Directional 라이트 하나, 2048² 섀도맵 1장, 40m × 40m 고정 직교 상자, 3x3 PCF. 라이트 절두체로 캐스터를 따로 고르므로 화면 밖 물체도 그림자를 던집니다 |
| 간접광 | IBL: 하늘 그라디언트로 CPU 에서 절차 생성한 조도 큐브맵 · 프리필터 큐브맵(5 밉) + GPU 로 구운 BRDF LUT(512², R16G16) |
| SSAO | 반해상도, 선형 깊이 → 반구 커널 64 샘플 |
| Bloom | 문턱값(1/2 해상도) → 다운샘플 4회(1/32 까지, 5레벨) → 업샘플 4회(스킵 연결). 결과는 1/2 해상도 |
| 톤매핑 | ACES(Narkowicz 근사), 노출 1.0, sRGB 인코딩(스왑체인 형식에 따라 셰이더 또는 GPU) |
| 반투명 | 메인 패스 세 번째 서브패스. 뒤에서 앞으로 정렬한 목록 전체를 뒷면만 한 번, 앞면만 한 번 그립니다(볼록 메시 하나의 앞뒤를 맞추는 방식이며, 메시마다 묶지 않으므로 서로 겹친 반투명 물체 사이의 순서는 어긋날 수 있음). 반투명도 그림자를 던집니다(알파 무시) |
| 컬링 | CPU 에서 바운딩 구 대 6평면. 두 눈 각각 판정해 하나라도 통과하면 그림 |
| 인스턴싱 | 같은 지오메트리 · 머티리얼의 불투명 메시를 자동으로 묶어 `instanceCount = N` 드로우 하나로(셰이더는 `gl_InstanceIndex` 로 행을 읽음). 반투명은 묶지 않음 |
| 오류 머티리얼 | 머티리얼이 없는 메시는 자홍색으로 그리고 액터마다 한 번 경고 |
| 렌더 확장 | 고정 지점 4곳(불투명 끝 · 반투명 끝 · 메인 패스 뒤 · 톤매핑 뒤)에 Layer 2 가 그리기를 끼워 넣음. 기본 패스 5개와 확장을 한 목록(패스 레지스트리)으로 수명주기 관리 |
| 뷰포트 | 씬 렌더 해상도(`renderExtent`)를 스왑체인과 분리하고 에디터 뷰포트 사각형에 합성 |
| 스테레오 | 눈마다 따로 그림(멀티뷰 아님). 셰이더 · UBO 는 두 눈 기준. `--emulate-stereo` 는 헤드셋 없이 뷰포트를 좌우로 나눠 두 눈을 그림 |
| 핫 리로드 | 실행 폴더 `shaders/` 의 `.spv` 가 바뀌면 파이프라인을 다시 만듦 |
| 최소화 | 창이 최소화되면 데스크톱 화면만 건너뛰고(XR 프레임이면 두 눈은 그림) 스왑체인은 복원 뒤 다시 만듦 |
| 통계 | 뷰포트 오버레이(FPS · Draw · Inst · Culled · Dropped · Transp · 재생성 횟수 · GPU/CPU/LOOP 구간)와 GPU Stats 패널(Lights 올린 수 / 켜진 수 포함) |

### 4-5. VR (OpenXR)

| 기능 | 사양 |
|---|---|
| 런타임 연결 | `--xr` 로 켬. `XR_KHR_vulkan_enable2` 필수, `XR_FB_display_refresh_rate` 있으면 사용. 런타임 · 헤드셋이 없으면 경고 하나 남기고 데스크톱으로 계속 |
| 공간 · 원점 | LOCAL 기준 공간, 원점 = 활성 카메라의 위치 + 요(yaw) |
| 프레임 | `xrWaitFrame` 이 루프 속도를 정함. 한 프레임에 자세 하나로 컬링 · 렌더 · 제출. 헤드셋이 화면을 보이지 않으면 빈 프레임 제출 |
| 스왑체인 | 두 층 배열 스왑체인 하나(눈마다 한 층). 오른쪽 눈을 먼저 그리고 왼쪽 눈을 에디터 뷰포트에 거울 화면으로 비춤 |
| 입력 | 기본 액션 세트 하나. Touch 컨트롤러 · simple 컨트롤러 프로필. 머리 · 양손 자세, 트리거 · 그립 · 스틱 · 버튼 → `HInputSystem` 의 XR 버튼 · 축 |
| 물리 | XR 세션 동안 물리 스텝을 헤드셋 주사율에 맞춤(30 ~ 240Hz 범위) |
| 시각화 | 씬의 라인 오버레이로 그려 헤드셋 두 눈과 거울 화면에 모두 보입니다. 손 자세에 10cm 축 세 개, 가리키는 방향에 선 하나(트리거 → 색, 그립 → 길이) |

### 4-6. 에셋 · 씬

| 기능 | 사양 |
|---|---|
| 에셋 루트 | 에셋은 `assets/...` 논리 경로로 가리키고 씬 · 프리팹에도 논리 경로를 저장합니다. 에디터 빌드는 프로젝트 폴더(구성할 때 넣은 소스 트리 경로)의 `assets/` 를 직접 읽고 씁니다(2026-10-15, UE · Unity · Godot 과 같음). 그 폴더가 없으면 실행 파일 옆 복사본을 씁니다. 캐시는 실행 폴더에 둡니다 |
| glTF | `.glb` · `.gltf` 정적 메시, 텍스처 5종. 스칼라 팩터는 읽을 때 텍스처에 구워 넣음. `alphaMode = BLEND` → 반투명 |
| 메시 캐시 | `.gasset` 바이너리 캐시(실행 폴더의 `assets/cache/`, 소스 트리에 쓰지 않음). 원본 크기와 수정 시각 또는 내용 해시(FNV-1a)가 같으면 재사용 |
| 절차 도형 | Box · Plane · Sphere · Cylinder · Cone · Capsule · Torus · Arrow · Icosphere 9종. 머티리얼은 체커 또는 단색(색 · 금속도 · 거칠기) |
| 씬 형식 | 텍스트 `.hscene` v4. 액터마다 `POS` · `ROT`(오일러 각) · `QUAT`(정확한 회전) · `SCALE` · `ACTIVE` · `PARENT`, 컴포넌트마다 `COMPONENT:타입` … `END_COMPONENT` 블록에 `이름=값`. 실수는 다시 읽으면 같은 값이 되는 가장 짧은 십진수로 씀 |
| 호환 | v3 이하 씬 · v1 프리팹을 계속 읽음. 등록되지 않은 컴포넌트 타입은 원문 그대로 보존해 다시 저장할 때 씀 |
| 프리팹 | `.hprefab` v2, 액터 하나(같은 형식). 에디터에서 저장 · 콘텐츠 브라우저에서 꺼내기 |
| 시작 씬 | `assets/scenes/LightingProbe.hscene`(라이팅 확인용 절차 도형 씬, v3 형식으로 유지) |
| 추적 에셋 | 모델 1(`model.glb`), 시험 음원 3, 시작 씬 1, 프리팹 1 |

**컴포넌트 5종**

| 컴포넌트 | 주요 값 |
|---|---|
| Light | type · color · intensity · attenuationRadius · inner/outerConeAngle · direction · useTemperature · temperatureK · castShadows |
| Camera | fovY · zNear · zFar · primary. 방향은 액터 회전 |
| StaticMesh | visible · transparent · 절차 도형(prim · primMaterial) 또는 모델(model · meshIndex) |
| Audio | file · volume · loop · autoPlay · attenuation(공간화 · 감쇠 모델 · 최소/최대 거리 · 롤오프) |
| RigidBody | shape(Box · Sphere · Capsule) · motion(Static · Dynamic · Kinematic) · friction · restitution · mass · halfExtent |

### 4-7. 물리 (Jolt)

- 고정 스텝 1/60 초(XR 중에는 헤드셋 주사율), 정적 · 동적 두 레이어, 바디 상한은 설정 파일.
- 콜라이더 크기는 월드 스케일을 반영합니다. 구 · 캡슐은 균등 스케일만 지원(비균등이면 가장 큰 값으로 근사하고 경고). 얇은 상자는
  모서리 반경을 두께에 맞춰 줄이고, 만들 수 없는 크기는 경고 후 바디를 만들지 않습니다.
- API: 힘 · 위치 지정 힘 · 토크 · 충격량 · 선속도 · 각속도 · 중력 설정, 레이캐스트, 구 겹침 질의, 바디 상태 조회, 마찰 · 반발 즉시 반영.
- 바디는 Play 중 지연 생성되고, 끄면 지우며 주변 잠든 바디를 깨웁니다. 편집 확정 때만 다시 만듭니다.

### 4-8. 오디오 (miniaudio)

- 48kHz 스테레오, WAV · FLAC · MP3(miniaudio 내장) · OGG Vorbis.
- 3D 공간화: 청취자 = 활성 카메라의 위치 · 방향(XR 세션 중에는 헤드셋 머리 자세, 2026-10-15), 감쇠 모델 Inverse · Linear · Exponential, 최소/최대 거리 · 롤오프.
- 에디터 Pause 는 월드 소리만 멈추고, 컴포넌트를 끄면 멈추고 되감습니다. 장치 없이 출력 샘플을 만들어 검산하는 헤드리스 경로가 있습니다.

### 4-9. 입력

- 키보드 · 마우스(창), XR 컨트롤러. 키 · 버튼은 직전 폴링 이후 눌린 적이 있으면 눌림으로 봅니다(짧은 클릭 유실 방지). OS 자동 반복 횟수를 셉니다.
- `HInputSystem`: 액션 이름에 키 · XR 버튼을, 축 이름에 XR 축을 묶고 Pressed · Released · Held 를 구독합니다. 텍스트 입력 중에는 게임 입력이 모든 키를 뗀 것으로 봅니다.

### 4-10. UI (HSlate)

- UE Slate 를 본뜬 리테인드 모드 위젯 시스템. 외부 의존은 stb_truetype 하나입니다.
- 위젯 14종: SBox · SBorder · SButton · SCollapseSection · SHorizontalBox · SVerticalBox · SImage · SNumberField · SOverlay ·
  SScrollBox · SSearchBox · STabBar · STextBlock · SToggleButton.
- 2패스 배치(desired size → arrange), `Attr<T>` 지연 바인딩, 클립 스택(사각형 자르기), 컨테이너의 휠 전달, 프레스 추적, 마우스 캡처.
- 폰트 아틀라스 2048×512(ASCII 32 ~ 127), 글자 폭 실측. 전용 `VkRenderPass`, 프레임 슬롯별 정점 버퍼, 프레임당 정점 65,536개 상한.

### 4-11. 에디터

| 영역 | 기능 |
|---|---|
| 레이아웃 | 도크 트리(분할 + 탭), 스플리터, 탭 끌어 다시 도킹, 탭 닫기와 Window 메뉴로 다시 열기, 레이아웃 저장 · 복원(`Config/editor_layout.txt`), Reset Layout, 창 높이에 따른 UI 배율(1 ~ 2.5배) |
| 메뉴 · 툴바 | File(New · Save · Load Scene), Edit(Undo · Redo · Delete), Actor(Add Empty · 직전 선택에 붙이기 · 떼기), View, Window. Play · Pause · Stop · Save · Load · Refresh |
| 뷰포트 | 기즈모(Q 선택 · W 이동 · E 회전 · R 스케일. 이동 · 회전은 World/Local, 스케일은 언제나 로컬, 스냅), 액터 픽(아이콘 → 로컬 바운드 광선-OBB), 우클릭 비행 카메라(WASD · Q/E, 비행 중 휠은 속도 단계) · 좌클릭 끌기 이동 · 휠 전후 이동, 라이트 · 카메라 · 오디오 아이콘, 선택한 액터의 바운드 상자 |
| 아웃라이너 | 계층 트리(들여쓰기 · 펼침), 검색(Ctrl+F), 가시성 토글, F2 리네임, 삭제, 타입 열, 액터 수 |
| 디테일 | 트랜스폼, 컴포넌트별 속성(UE 식 숫자 칸: 드래그 · Shift 10배 · Ctrl 0.1배 · 직접 입력), 켜기/끄기 · 제거, 컴포넌트 추가(레지스트리 기반, 폭에 맞춰 줄 나눔), Save as Prefab |
| 하단 | Content Browser(모델 · 오디오 · 프리팹), Output Log(카테고리 · 수준 필터, 검색, 꼬리 추종), GPU Stats |
| 상태바 | 최근 로그 한 줄(수준별 색), 지금 키보드 입력이 가는 곳(Rename · Search · Value · Log Search) |
| 편집 | Undo/Redo 64단계: 트랜스폼 변경, 컴포넌트 속성 스냅샷, 액터 생성 · 삭제(2026-10-15. 서브트리를 글로 떠 두었다가 원래 ID · 부모로 되살리므로 아웃라이너 자리와 저장 결과가 같음). 기즈모를 끄는 동안에는 되돌리지 않습니다. Stop · New Scene · Load 는 Undo 기록을 지웁니다 |
| 단축키 | Ctrl+Z 되돌리기, Ctrl+Y · Ctrl+Shift+Z 다시 하기, Ctrl+S 저장, Ctrl+F 아웃라이너 검색, F2 리네임, Delete 액터 삭제, Esc 글자 편집 끝내기(숫자 칸은 값 확정), 글자 · 숫자 칸의 Ctrl+A · C · V |
| Play | Play · Pause · Stop. Play 를 누르면 씬을 글로 저장해 두고 Stop 하면 다시 읽어 복원합니다(2026-10-14 일괄 점검에서 세 번 왕복한 뒤 저장 결과가 글자까지 같음을 확인) |

### 4-12. 품질 관리

- **단위 테스트 15개, 검사 540개.** 외부 프레임워크 없이 자체 `check()` 로 만든 콘솔 실행 파일이며 그래픽 장치 없이 돕니다.

| 테스트 | 검사 | 대상 |
|---|---|---|
| `geometry_conventions` | 59 | 절차 도형 규약 |
| `actor_lifecycle` | 38 | 활성 · 컴포넌트 생명주기 · 카메라 자세 |
| `command_stack_purge` | 8 | 커맨드 스택의 액터 참조 제거 |
| `actor_undo` | 25 | 액터 생성 · 삭제 Undo(원래 ID · 부모 · 순서로 되살림) |
| `log_category` | 61 | 로그 카테고리 |
| `audio_system` | 56 | 오디오(헤드리스 출력 검산, XR 머리 자세 청취자) |
| `slate_input` | 97 | UI 입력 · 숫자 칸 |
| `physics_lifecycle` | 65 | 바디 생명주기 · 물리 API · 얇은 콜라이더 |
| `stereo_views` | 10 | 눈별 투영 · 두 눈 컬링 |
| `xr_input` | 9 | VR 입력 |
| `draw_batching` | 26 | 자동 인스턴싱 묶기 · 행 배정 |
| `render_extensions` | 17 | 패스 레지스트리 · 확장 지점 |
| `scene_components` | 39 | 컴포넌트 등록 · 씬 v4 값 · 회전 왕복 |
| `light_selection` | 16 | 라이트 선택 · 상한 설정 파일 |
| `asset_cache` | 14 | glTF 캐시 유효성, 에셋 논리 경로 해석 |

- 2026-10-14 이후 새로 만든 검사는 수정을 되돌리면 실패하는지 **변이로 확인**했습니다.
- 화면 회귀는 에디터를 띄워 `PrintWindow` 로 찍은 뒤 뷰포트를 픽셀 단위로 비교하고, Vulkan 검증 레이어 메시지 0 과 `Clean exit` 를
  확인합니다. 절차와 기록은 [Editor_QA_Checklist.md](Editor_QA_Checklist.md).
- 결함 정본은 [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md) 와 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 두
  문서입니다. DEF-01 ~ 48 은 두 문서에 함께 실리고(진단 맵과 요약 표는 2026-10-15 에 결함 절의 상태와 맞춤), DEF-49 이후는
  Core_Defects_Task_Spec.md 뒤쪽 절에만 있습니다. 상태는 각 결함 절과 Core_Defects_Task_Spec.md 끝의 결과 절을 기준으로 봅니다
  (6장의 표도 그 기준으로 셌습니다).

### 4-13. 수치 한도 요약

| 항목 | 값 | 바꾸는 곳 |
|---|---|---|
| 창 기본 크기 | 1600x900 | 코드 |
| 프레임 인 플라이트 | 2 | 코드(`MAX_FRAMES`) |
| 한 프레임에 그릴 수 있는 메시(transform 행) | 16384 | `Config/engine.ini` |
| GPU 로 올리는 라이트 | 256 | `Config/engine.ini` |
| 물리 바디 | 4096 | `Config/engine.ini` |
| 눈(뷰) | 2 | 코드(`kMaxViews`) |
| 그림자 캐스터 | Directional 1개, 2048², 40m 상자 | 코드 |
| 장치 메모리 힙 | 1GB(64 / 896 / 64MB) | 코드 |
| UI 정점 | 65,536 / 프레임 | 코드 |
| Undo 단계 | 64 | 코드 |

---

## 5. 미구현 기능

### 5-1. 플랫폼 · 런타임

| 항목 | 현재 상태 | 관련 |
|---|---|---|
| Quest 단독 실행(Android) | 없음. `Engine` 이 `GlfwWindow` 를 조건 없이 만들고 `AndroidWindow::createSurface` 는 예외를 던지는 스텁입니다. gradle 등 Android 빌드 파일도 없습니다 | DEF-08 · 103 |
| Vulkan 1.1 ~ 1.3 장치 | 시작하지 않음. 1.4 기능을 조건 없이 요구합니다(Quest 2 · Pro 는 1.1, Quest 3 은 1.3) | DEF-97 · 04 |
| 멀티뷰 | 없음. 눈마다 두 번 그립니다(셰이더 · UBO 는 두 눈 기준이라 이식 때 뷰 번호만 바꾸면 됨) | DEF-09 후속 |
| 런타임 · 에디터 분리 | 없음. 월드 tick 은 에디터 Play 중에만 돌고, 에디터를 빼는 빌드가 없습니다(`HARUKA_EDITOR` 는 정의만 되고 쓰이지 않음) | DEF-12 |
| Linux · macOS | 빌드된 적 없음 | |
| 입력 액션 정의 | `HInputSystem` 은 있으나 엔진이 등록한 액션이 없습니다(`Engine::setupInputBindings` 비어 있음) | |
| 비동기 에셋 로딩 | 없음. glTF 를 메인 스레드에서 동기로 읽습니다 | [Threading.md](Threading.md) |
| Zero-Copy 로딩 | 이름만 남음. 캐시를 호스트 메모리로 읽은 뒤 업로드합니다 | |

### 5-2. 렌더링

| 항목 | 현재 상태 |
|---|---|
| 클러스터드 라이팅 | 보류(DEF-34). 픽셀마다 올린 라이트를 모두 순회합니다. PC 에서 라이트 하나에 약 4µs(상한 256 에서 약 1ms), Quest 는 실측 필요 |
| 캐스케이드 섀도(CSM) · 여러 그림자 캐스터 | 없음. Directional 하나 · 40m 고정 상자 |
| 점광원 · 스팟 그림자 | 없음 |
| TAA · 모션 벡터 | 없음(가짜 TAA 를 지웠음). 안티에일리어싱은 MSAA 만 |
| 고정 포비티드 렌더링(FFR) | 없음(필요하면 Quest 에서) |
| 외부 HDRI 환경 맵 · 반사 프로브 · 안개 | 없음. IBL 은 절차 생성 하늘뿐 |
| Light Channels · 노출 설정 배선 | 없음(노출 1.0 하드코딩) |
| LOD | 거리로 고르는 코드는 있으나 LOD 메시를 채우는 경로가 없습니다 |
| 스키닝 · 애니메이션 · 모프 | 없음(glTF 정적 메시만) |
| glTF `alphaMode = MASK` | 없음 |
| GPU 컬링 · 간접 드로우 | 없음(순수 오버헤드여서 지웠음) |
| 3D UI 패널 | 없음. 방향은 UE 식 render-to-texture(`UIPanelComponent`)로 정했고, 상세 설계는 Spatial 6단계 때 합니다([Spatial_Representation_Design.md](Spatial_Representation_Design.md) 10절) |
| 깊이 정책(Reverse-Z) · 정점 패킹 · Floating Origin | 없음. 앞의 둘은 [Layer_Architecture.md](Layer_Architecture.md) §6 3단계, Floating Origin 은 Layer 1 옵션 후보입니다 |
| `FrameUBO.invViewProj` | 보류(DEF-13, 첫 사용처가 생길 때) |

### 5-3. 물리 · 오디오

| 항목 | 현재 상태 |
|---|---|
| 조인트 · 컨스트레인트 | 없음 |
| 메시 · 볼록 껍질 콜라이더 | 없음(Box · Sphere · Capsule 만) |
| 잡기 · 던지기 같은 VR 상호작용 | 없음(API 만 있음) |
| 긴 음악 스트리밍 · 보이스 예산 · HRTF · 버스와 마스터 볼륨 | 없음 |

### 5-4. 에디터

| 항목 | 현재 상태 |
|---|---|
| 씬 파일 선택 | 없음. 저장 · 불러오기 대상이 실행 폴더의 고정 파일 `scene.hscene` 하나입니다 |
| Undo 범위 | 트랜스폼 · 속성 · 액터 생성과 삭제(2026-10-15)까지입니다. 리네임 · 컴포넌트 추가/제거 · 부모 변경은 되돌릴 수 없습니다. 삭제를 되돌려도 그 액터의 삭제 전 편집 이력은 돌아오지 않습니다(커맨드가 포인터 기반, 사용자 결정) |
| 다중 선택 · 아웃라이너 끌어서 부모 바꾸기 · 부동 창 · 스크롤 막대 끌기 | 없음 |
| 뷰 모드(Lit · Unlit · Wireframe) · 선택 대상 프레이밍 | 없음 |
| 라이트 범위 · 카메라 절두체 · 오디오 반경 시각화 | 없음(Spatial 4단계) |
| 머티리얼 편집 | 없음(요구만 등재). 머티리얼은 텍스처 슬롯만 있고 팩터를 따로 보관하지 않습니다 |
| 한글 · 유니코드 표시 | 불가(폰트 아틀라스가 ASCII 전용). 에디터 문자열은 영어입니다 |
| 에디터 Phase 4 | VR 뒤로 미룸 |

### 5-5. Layer 2 (우주 · 무중력 도메인)

전부 미착수입니다. 무중력 비행 역학, 파편장(`DebrisFieldComponent`, 명시적 인스턴싱과 함께), 생존 규칙, 우주 비주얼이 계획에 있고
정본은 [Deep_Space_VR_Specification.md](Deep_Space_VR_Specification.md) 입니다. 정적 기지 건설은 2026-10-15 에 구현하기로 정했고(시점 미정),
지침과 선결 코어 작업은 [Static_Base_Building_Design.md](Static_Base_Building_Design.md) 에 있습니다. 코어 쪽 선결 조건 중 물리 API · VR 입력 · 6DOF 카메라 ·
인스턴싱 · 렌더 확장 지점 · 컴포넌트 등록은 갖췄습니다. [Layer_Architecture.md](Layer_Architecture.md) §6 기준으로 2단계의
`FrameUBO.invViewProj`(DEF-13 보류)와 3단계의 감쇠 운동 적분기 · 정점 패킹 · 보이스 예산 · 깊이 정책(Reverse-Z)이 남았고,
Floating Origin 은 Layer 1 옵션 후보입니다.

---

## 6. 열린 결함과 보류 항목

| ID | 심각도 | 내용 | 처리 시점 |
|---|---|---|---|
| DEF-04 | HIGH | `synchronization2` 를 조건 없이 요구합니다(1.1 장치에서는 확장으로 켜야 함) | Quest 이식 |
| DEF-06 | HIGH | MSAA 이미지에 `TRANSIENT` 용도는 있으나 `LAZILY_ALLOCATED` 메모리를 쓰지 않아, 타일 GPU 에서 실제 메모리를 차지합니다 | Quest 이식 |
| DEF-08 | HIGH | Android 진입점 없음(위 5-1) | Quest 이식 |
| DEF-97 | HIGH | Vulkan 1.4 장치 전제(위 5-1) | Quest 이식 |
| DEF-12 | MEDIUM | 월드 tick 이 에디터 Play 상태에 묶여 있음 | Quest 이식 |
| DEF-42 | MEDIUM | 힙이 1GB 보다 작게 잡히면 구역 오프셋이 할당 범위를 넘습니다 | Quest 이식 전 필수 |
| DEF-02 | MEDIUM · 잠재 | Zone C ring 할당에 펜스 추적이 없습니다. 지금은 시작할 때만 할당하므로 드러나지 않습니다 | 매 프레임 할당이 생길 때 |
| DEF-91 | LOW · 잠재 | 에디터 일부가 창 좌표, 일부가 프레임버퍼 좌표를 씁니다. Windows 에서는 둘이 같아 드러나지 않습니다 | 다른 플랫폼 |
| DEF-103 | LOW | `AndroidWindow` 가 클립보드 함수 둘을 구현하지 않아 추상 클래스입니다 | Quest 이식 |
| DEF-13 · 32 · 34 | 보류 | `invViewProj` · 재투영 여유분 · 클러스터드 라이팅 | 첫 사용처 · 늦은 자세 갱신 · Quest 측정 |

---

## 7. 우려되는 점

1. **VR 경로가 헤드셋에서 검증되지 않았습니다.** 엔진의 정체성이 VR 인데, OpenXR 세션 · 두 눈 렌더 · 컨트롤러 입력 · 헤드셋 주사율
   물리 · 최소화 중 XR 그리기는 모두 코드와 단위 테스트, 스테레오 흉내 모드로만 확인했습니다. 개발 PC 에 OpenXR 런타임이 없습니다.
   첫 헤드셋 실행에서 여러 문제가 한꺼번에 드러날 수 있습니다.
2. **Quest 이식 장벽이 한 덩어리로 남아 있습니다.** Android 진입점 · 빌드 파일(DEF-08 · 103), Vulkan 1.4 전제(DEF-97 · 04), 1GB 미만
   힙(DEF-42), 타일 메모리(DEF-06), 멀티뷰, 런타임 분리(DEF-12)가 서로 얽혀 있어 하나씩 끝내기 어렵고, 이식 착수가 곧 큰 작업입니다.
3. **성능 근거가 PC 측정 몇 건뿐입니다.** Quest 실측이 한 번도 없습니다. 라이트 비용(DEF-34)은 PC 측정에서 어림한 값입니다.
   문서에 남은 측정은 2026-09 의 데스크톱(RTX 3080) 측정(GPU 1.52ms 중 AO + Bloom 0.84ms, 55%)과 2026-10-04 의 기본 씬 GPU 합계
   0.32 ~ 0.37ms(다른 프로그램이 GPU 를 쓰는 중이라 참고값)뿐이며, 화면 크기에 비례하는 후처리가 큰 몫이었습니다.
   Quest 의 90Hz 예산(11.1ms)에 들어가는지는 아직 모릅니다.
4. **에디터와 런타임이 분리되어 있지 않습니다.** 게임만 실행하는 빌드가 없어, 배포 가능한 형태가 아직 없습니다(DEF-12).
5. **자동 검증의 공백이 있습니다.** 렌더러의 GPU 경로 · 메모리 할당자 · 에디터 UI 조작에는 자동 테스트가 없고, 화면 회귀는 사람이
   에디터를 띄워 픽셀을 비교해야 확인됩니다. CI 가 없어 빌드 · 테스트가 개발 PC 한 대에서만 돌았습니다.
6. **문서가 코드보다 쉽게 낡습니다.** 이번 작성 중에도 루트 README 의 "알려진 한계"(드로우 상한 1024, 스크롤 · 클리핑 없음 등)와 문서
   색인의 "VR 진행하지 않음"이 낡아 있었습니다. 결함 정본 두 문서의 동기화와 줄 번호 갱신도 손으로 해야 합니다. 그래서 이 저장소에서는
   문서의 체크박스보다 코드 확인이 우선입니다.
7. **원시 포인터를 붙잡는 구조가 남아 있습니다.** 디테일 패널의 값 접근자와 리플렉션 속성(`Reflect.hpp`)이 컴포넌트 주소를 캡처합니다.
   지금은 액터 파괴 이벤트와, 컴포넌트를 제거하기 직전에 부르는 `purgeComponent`(DEF-55)로 막고 있지만, 컴포넌트를 해제하거나 옮기는 새 경로가 생기면 DEF-55 같은 use-after-free 가 다시 나올 수 있습니다.
8. **메인 스레드 하나에 모든 일이 몰려 있습니다.** 에셋 로딩이 동기라 큰 모델을 읽는 동안 프레임이 멈추고, VR 에서는 곧 멀미로
   이어집니다. 비동기 로딩이 첫 멀티스레딩 도입 지점으로 예정되어 있습니다.
9. **직접 만든 부분이 많아 유지 비용이 큽니다.** 할당자 · UI 시스템 · 에디터를 외부 라이브러리 없이 1인이 유지합니다. 종합 오류 검출과
   일괄 점검에서 매번 새 결함이 나온 것(DEF-76 ~ 94, 105 ~ 109)은 아직 덜 다져진 영역이 있다는 뜻입니다.
10. **저장소 관리.** 라이선스 파일이 없고(2026-10-15 사용자 결정으로 당분간 둠), `stb` 는 버전이 고정되지 않아 구성
    시점에 따라 다른 소스를 받을 수 있습니다(영향 범위는 stb_truetype). 초기 커밋의 날짜는 문서에 적힌 작업 날짜와 맞지 않습니다
    (예: 2026-07-03 커밋의 도킹 Phase 0 은 문서상 2026-08-27 작업, 2-1).
11. **사용성 제약.** UI 가 ASCII 만 표시하고, 씬 저장이 고정 파일 하나를 덮어쓰며, 되돌릴 수 없는 편집(리네임 · 컴포넌트 추가/제거 ·
    부모 변경)이 남아 있습니다.

---

## 8. 앞으로의 순서

정본은 [Roadmap.md](Roadmap.md) 입니다. 2026-10-15 기준으로 합의한 순서는 다음과 같습니다.

1. **헤드셋 확인**: 사용자가 Quest Link 를 준비하면 `run.bat Release --xr` 로 확인 목록([Editor_QA_Checklist.md](Editor_QA_Checklist.md)
   의 헤드셋 항목)을 봅니다.
2. **Quest 이식**: DEF-08 · 12 · 42 · 04 · 97 · 06 · 103, 멀티뷰, Android 빌드, 그리고 Quest 에서 DEF-34(라이트 비용) 실측.
3. **에디터 Phase 4 · 렌더링 기능**: Light Channels, CSM, 노출 배선, 반사 프로브, 에디터 시각화 등.
4. **Layer 2**: 우주 · 무중력 도메인과 파편장, 정적 기지 건설([Static_Base_Building_Design.md](Static_Base_Building_Design.md)).

남은 비VR 결함은 잠재 항목 DEF-02 · DEF-91 뿐이며, 둘 다 지금 드러나는 경로가 없습니다.
