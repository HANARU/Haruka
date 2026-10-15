# Haruka

Vulkan 1.4 기반 C++20 게임 엔진. 렌더러, GPU 메모리 할당자, UI 위젯 시스템, 에디터를 외부 프레임워크 없이 직접 구현하는 1인 개발 프로젝트입니다. 현재 빌드·실행되는 형태는 에디터가 함께 도는 **Windows 데스크톱 애플리케이션 하나**입니다.

장기 목표는 VR입니다. PC OpenXR 연결(`--xr`, Quest Link 등 PC VR 런타임)이 들어 있지만 아직 헤드셋에서 확인하지 않았습니다. 자세한 내용은 [알려진 한계](#알려진-한계)를 보십시오.

소스는 `src/` 기준 `.cpp`/`.hpp` 152 파일 24,920 줄, 셰이더는 GLSL 19 파일 705 줄입니다(2026-10-15). 릴리스·태그 없음, CI 없음, 자동 테스트 15건.

## 지금 동작하는 것

GLFW 창(1600x900) 하나를 띄우고 그 안에서 엔진과 에디터가 함께 돕니다. 에디터를 빼는 빌드 옵션은 없으며, 월드 틱과 게임·물리·오디오 모듈 틱은 에디터가 Play 상태일 때만 돕니다.

**렌더링** — 포워드 렌더링(G-buffer 없음). 메인 패스는 단일 `VkRenderPass` 안의 3개 서브패스입니다: (0) 깊이 프리패스 → (1) 불투명 PBR → (2) 투명 PBR + 라인·기즈모 오버레이. 서브패스 2에서 MSAA 컬러를 HDR(`R16G16B16A16_SFLOAT`) 타깃으로 resolve 합니다. MSAA는 디바이스 limits를 조회해 4x → 2x → 1x로 낮춥니다. 요구하는 디바이스 확장은 `VK_KHR_swapchain`, `VK_KHR_push_descriptor` 둘뿐입니다. 컬링은 CPU에서 바운딩 스피어 대 6평면 판정으로 합니다.

**라이팅 · 포스트** — Directional / Point / Spot 3종(SSBO 전달, 감쇠 반경·스팟 내외각·색온도(Kelvin) 틴트·per-light Cast Shadows). 실제로 그림자를 만드는 것은 `castShadows`가 켜진 첫 Directional 라이트 하나이며, 2048² 섀도맵 1장 + 3x3 PCF입니다. SSAO(half-res 선형 깊이 → 헤미스피어 커널), 블룸(threshold + 5단계 다운/업샘플), ACES 톤매핑 + sRGB 인코딩이 동작합니다. IBL 큐브맵은 HDRI 파일이 아니라 하늘 그라디언트로 CPU에서 절차 생성하고, BRDF LUT만 GPU 오프스크린 렌더로 굽습니다.

**에디터** — 도킹(분할 노드 + 탭 스택, 스플리터 리사이즈, 탭 드래그 재도킹, 레이아웃 파일 저장·복원), 아웃라이너 계층 트리 + 검색 + F2 리네임, 디테일 패널(Transform · 컴포넌트 프로퍼티 · Add Component · Save as Prefab), 뷰포트 툴바, 이동/회전/스케일 기즈모와 스냅, Play/Pause/Stop, Undo/Redo, Output Log · Content Browser · GPU Stats 탭, `.spv` 핫 리로드.

**그 외** — Jolt Physics v5.1.0(기본 1/60 고정 스텝, XR 중에는 헤드셋 주사율, Box/Sphere/Capsule, Static/Dynamic/Kinematic), miniaudio 기반 3D 공간화 오디오, tinygltf glTF 로더 + 바이너리 메시 캐시(`.gasset`) + 텍스트 씬·프리팹 직렬화(`.hscene`/`.hprefab`), GPU 타임스탬프 계측(Shadow/Main/AO/Bloom/Tonemap/UI 구간).

## 빌드와 실행

### 요구 사항

| 항목 | 내용 |
|---|---|
| OS | Windows x64. 빌드·실행 스크립트가 배치 파일이며 다른 OS에서 검증된 적 없음 |
| 컴파일러 | MSVC. `build.bat`은 `Visual Studio 18 2026`을 기본 생성기로 쓰고, 해당 설치 경로가 없고 2022가 있으면 `Visual Studio 17 2022`로 내려갑니다(`-A x64` 고정) |
| CMake | 3.21 이상(셰이더 include 의존성 추적에 depfile 을 씀) |
| Vulkan SDK | **1.4.x** — 코드가 `VK_API_VERSION_1_4`와 `VkPhysicalDeviceVulkan14Features.pushDescriptor`를 사용 |
| GPU 드라이버 | Vulkan 1.4. 그 밖에 요구하는 기능: `synchronization2`, `samplerAnisotropy` |
| VR(선택) | `--xr` 로 띄울 때만. PC VR OpenXR 런타임(Meta Quest Link 등)과 헤드셋. 없으면 경고를 남기고 데스크톱으로 계속합니다 |
| 네트워크 | 첫 CMake 구성 시 필요(의존성을 FetchContent로 내려받음) |

미리 설치할 도구는 Visual Studio(MSVC), CMake, Git, Vulkan SDK 입니다. glm 1.0.1, GLFW 3.4, tinygltf v2.9.3, stb, miniaudio 0.11.21, JoltPhysics v5.1.0, OpenXR-SDK release-1.1.63(로더 정적 링크)은 CMake FetchContent가 구성 시점에 내려받으며 저장소에는 포함되어 있지 않습니다. 셰이더는 런타임이 아니라 빌드 시점에 `glslc`로 SPIR-V(그래픽스 셰이더 18개)로 컴파일되므로, `glslc`를 찾지 못하면 구성 단계에서 실패합니다.

### 명령

```bat
build.bat                 :: Release (기본)
build.bat Debug           :: Debug
build.bat Release clean   :: CMake 캐시를 지우고 재구성

run.bat                   :: build\Release\HarukaEngine.exe 실행
run.bat Debug
run.bat Release --xr              :: OpenXR 런타임으로 헤드셋에 그림(에디터 뷰포트에 왼쪽 눈)
run.bat Release --emulate-stereo  :: 헤드셋 없이 두 눈 경로 확인(뷰포트를 좌우로 나눔)
```

`build.bat`은 `build\CMakeCache.txt`가 없을 때만 CMake를 구성합니다. 생성기나 옵션을 바꾸려면 `clean` 인자가 필요합니다. 전체 빌드 로그는 `Logs\build_log.txt`, error/fatal만 걸러낸 요약은 `Logs\build_error.txt`에 남습니다.

산출물은 `build\<Config>\HarukaEngine.exe`이고, SPIR-V와 `assets/`는 POST_BUILD로 실행 파일 옆에 복사됩니다.

그리기 행 · 라이트 · 물리 바디 상한은 실행 폴더의 `Config/engine.ini` 에서 바꿉니다(처음 실행할 때 기본값과 설명으로 만들어지고, 바꾼 뒤에는 다시 실행).

### 실행 시 주의

- 엔진이 셰이더와 에셋을 상대 경로(`shaders/pbr.vert.spv`, `assets/models/model.glb`)로 열기 때문에 **작업 디렉터리가 실행 파일이 있는 디렉터리여야 합니다.** `run.bat`이 `pushd`로 이를 처리하고, 출력을 콘솔과 `Logs\latest.txt`에 함께 기록하며 종료 코드를 그대로 전달합니다.
- 실행 파일은 `/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup`으로 링크되어 콘솔 창이 뜨지 않습니다. 엔진 로그는 실행 방법과 상관없이 실행 폴더의 `Logs/HarukaEngine.log`(UTF-8)에 남고, exe를 직접 실행하면 로그 체계를 거치지 않는 C stdio 출력이 `Logs/stdout.txt`로 갑니다. 콘솔이 필요하면 구성 시 `-DHARUKA_CONSOLE=ON`.
- Debug 빌드는 Vulkan 검증 레이어를 켜며, 레이어가 없으면 시작 시 예외를 던집니다.
- UI 폰트는 저장소에 없습니다. `assets/fonts/default.ttf`를 먼저 찾고 없으면 시스템 폰트(`C:/Windows/Fonts/segoeui.ttf` 등)로 대체합니다.

### 테스트

```bat
cd build
ctest -C Release
```

15건입니다: `geometry_conventions`(절차 도형 규약), `actor_lifecycle`(Actor 활성 · 컴포넌트 생명주기 · 카메라 자세), `command_stack_purge`, `actor_undo`(액터 생성 · 삭제 Undo), `log_category`, `audio_system`, `slate_input`(UI 입력 · 숫자 칸), `physics_lifecycle`(물리 바디 생명주기 · 물리 API), `stereo_views`(눈별 투영 · 두 눈 컬링), `xr_input`(VR 입력), `draw_batching`(자동 인스턴싱 묶기 · 행 배정), `render_extensions`(패스 레지스트리 · 렌더 확장 지점 순서), `scene_components`(컴포넌트 타입 등록 · 씬 v4 값 · 회전 왕복), `light_selection`(라이트 선택 · 상한 설정 파일), `asset_cache`(GLTF 캐시 유효성). 외부 테스트 프레임워크 없이 자체 `check()`로 만든 콘솔 실행 파일이고, 창을 띄우지 않아 그래픽 디바이스 없이 돕니다.

## 저장소 구조

```
CMakeLists.txt   build.bat   run.bat
src/
  core/        Engine · World · Actor · Component
  renderer/    Vulkan 렌더러, passes/(Shadow · Geometry · AO · Bloom · Post)
  memory/      HarukaAllocator (자체 3존 할당자)
  ui/slate/    HSlate — 자체 리테인드 모드 위젯 시스템 (14종)
  editor/      EditorApp · 도킹 · 기즈모 · 커맨드 스택 · 셰이더 핫 리로드
  components/  Light · Camera · Mesh · Audio · RigidBody
  physics/  audio/  input/  assets/  geometry/  platform/  game/  main.cpp
shaders/       GLSL → 빌드 시 glslc 로 SPIR-V 컴파일
assets/        모델 1 · 시험 음원 3 · 시작 씬 · 프리팹
tests/         단위 테스트 15개 (ctest)
Docs/          설계 · 결함 · 결정 기록 (색인: Docs/README.md)
```

## 설계 메모

**자체 메모리 할당자** (`src/memory/HarukaAllocator.hpp`) — VMA를 쓰지 않습니다. `vkAllocateMemory`를 한 번 호출해 1GB를 선점하고 전 범위를 지속 매핑한 뒤, Zone A 64MB(bump) / Zone B 896MB(free-list) / Zone C 64MB(ring, 프레임별 동적 데이터)로 나눠 씁니다. 합이 정확히 1GB임을 `static_assert`로 강제합니다. 메모리 타입은 `DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT`(UMA)를 1순위로 고르고, 힙의 75%까지만 선점하며 실패하면 절반씩 줄여 256MB까지 재시도합니다. Zone B는 `bufferImageGranularity`를 조회해 이미지의 시작 주소를 그 단위에 맞춰 정렬합니다. UI(HSlate) 버퍼 · 폰트 아틀라스와 라인 · 기즈모 정점 버퍼는 이 힙 밖에서 따로 할당합니다.

**좌표의 단일 진리원** (`src/editor/EditorGizmo.hpp`) — 마우스↔월드 변환을 `WorldViewport` 하나가 `project()` / `ray()`로 전담하고, 뷰포트 rect를 고정 치수가 아니라 뷰포트 위젯의 실측 geometry에서 유도합니다. 렌더 해상도(`renderExtent`)와 스왑체인 서브렉트도 분리되어 있어, 패널을 어디에 도킹하거나 리사이즈해도 픽·기즈모·렌더가 같은 좌표계를 씁니다.

**HSlate — 자체 UI 시스템** (`src/ui/slate/`) — Dear ImGui를 쓰지 않고 UE Slate를 본뜬 리테인드 모드 위젯 시스템을 직접 만들었습니다. 위젯 14종, 2패스 레이아웃(`computeDesiredSize` → `onArrangeChildren`), `Attr<T>` 지연 평가 바인딩, 마우스 캡처, 자체 폰트 아틀라스와 전용 `VkRenderPass`. 외부 의존은 폰트 래스터라이즈용 stb_truetype 하나뿐입니다.

**Actor + Component 컴포지션** (`src/core/`) — ECS가 아니며, 이는 측정에 근거한 선택입니다([Docs/ECS_Decision.md](Docs/ECS_Decision.md)). `World`가 `unique_ptr<Actor>`를, `Actor`가 `unique_ptr<Component>`를 소유하고 컴포넌트 조회는 `std::type_index` 캐시로 O(1)입니다. Unity식 `activeSelf` / `activeInHierarchy`, 활성 전이 시 서브트리 `onEnable`/`onDisable` 발화, 틱 중 파괴 요청을 지연 큐로 미루는 파괴 경로를 갖췄습니다. Engine은 고정 순서 코어 서비스(창 → 렌더러 → 에셋) 위에 동적 모듈(Audio, Physics)과 에디터·게임 모듈을 얹는 2계층 부트스트랩이고, 소멸은 역순입니다.

## 알려진 한계

아래는 전부 코드로 확인한 현재 상태입니다.

**플랫폼 · VR**
- Windows 전용입니다. CI가 없어 Linux/macOS에서 빌드된 적이 없습니다. macOS는 MoltenVK에 필요한 `VK_KHR_portability_enumeration` 처리가 없고 요구 API가 Vulkan 1.4라 실행 자체가 불가능합니다.
- Vulkan 1.4 드라이버가 필요합니다. 1.0~1.3 환경에서는 시작하지 않습니다.
- **VR은 첫걸음 단계입니다.** PC OpenXR 연결(`--xr`)이 있지만 헤드셋에서 확인하지 않았습니다(개발 PC 에 런타임이 없음). 원점은 활성 카메라의 위치와 요이고(LOCAL 공간), 두 눈은 눈마다 따로 그립니다(멀티뷰는 Quest 이식 때). 입력은 기본 액션 세트 하나입니다.
- Quest 단독 실행(Android)은 아직 없습니다. 아래 Android 항목을 보십시오.
- Android/Quest 경로는 스텁입니다. `AndroidWindow::createSurface()`가 예외를 던지고, `Engine` 생성자가 `GlfwWindow`를 조건 없이 만들기 때문에 CMake의 ANDROID 분기는 컴파일되지 않습니다. gradle 등 안드로이드 빌드 파일도 없습니다.

**렌더러**
- TAA는 없습니다. 지터·모션벡터 없는 구현이 고스팅만 내서 껐고, 남아 있던 코드도 2026-10-04 에 지웠습니다. 안티에일리어싱은 MSAA가 전담합니다.
- 컬링은 CPU에서 합니다. GPU 컴퓨트 컬링은 순수 오버헤드여서 dispatch를 멈췄고, 코드도 2026-10-04 에 지웠습니다.
- 섀도맵은 2048² 한 장이고 캐스케이드(CSM)가 없으며, 캐스터는 Directional 라이트 하나뿐입니다.
- IBL이 절차 생성 큐브맵이라 외부 HDRI 환경 맵을 불러올 수 없습니다.
- 프레임 인 플라이트 2. 한 프레임에 그릴 수 있는 메시 수(transform 행)는 기본 16384 이고 `Config/engine.ini` 로 바꿉니다.
- 저장소에 커밋된 벤치마크나 프레임 타임 수치가 없습니다. GPU 타임스탬프 계측 코드만 있습니다.

**엔진 · 런타임**
- 런타임이 에디터에서 분리되어 있지 않습니다. `World::tick`과 게임·물리·오디오 모듈 틱은 에디터가 Play 상태일 때만 돕니다(결함 대장 DEF-12, 미해결). `HARUKA_EDITOR` 매크로는 정의만 되고 코드에서 참조되지 않습니다.
- `HInputSystem`(액션 바인딩 + Pressed/Released/Held 구독)은 있으나 엔진이 등록한 액션 바인딩이 하나도 없습니다. `Engine::setupInputBindings()`는 본문이 주석뿐이고, 에디터 카메라는 창의 raw input을 직접 씁니다.
- 물리에 컨스트레인트·조인트 API가 없습니다.
- 문서에서 말하는 "Zero-Copy 로딩"은 이름만 남은 상태입니다. `GassetCache::load`는 `ifstream`으로 호스트 vector에 읽은 뒤 업로드합니다.

**HSlate · 에디터**
- HSlate 의 스크롤 막대는 위치만 보여 주고 끌어서 스크롤하지는 못합니다(휠로 스크롤). 클리핑과 스크롤 상자는 2026-10-03 에 들어갔습니다.
- 폰트 아틀라스가 ASCII 32~127만 래스터라이즈합니다. **UI에 한글·유니코드를 표시할 수 없어** 에디터 문자열은 전부 영문이고 아이콘 대신 ASCII 글리프를 씁니다(문서와 코드 주석은 한국어).
- UI는 한 프레임에 정점 65,536개(쿼드 16,384개)까지만 그리고 초과분은 조용히 버립니다.
- Undo/Redo 스택에 쌓이는 것은 Transform 변경, 컴포넌트 프로퍼티 스냅샷, 액터 생성·삭제입니다(최대 64단계). 리네임·컴포넌트 추가/제거·부모 변경은 되돌릴 수 없고, 삭제를 되돌려도 그 액터의 삭제 전 편집 이력은 돌아오지 않습니다.
- 액터 다중 선택, 아웃라이너 드래그&드롭 재부모화, 패널의 부동 창 분리, 뷰 모드(Lit/Unlit/Wireframe) 전환이 없습니다.
- 씬 저장/불러오기 대상이 고정 파일명 `scene.hscene` 하나이고 파일 선택 대화상자가 없습니다.
- 액터 픽은 삼각형 단위가 아니라 컴포넌트 로컬 바운드 합집합에 대한 광선-OBB 판정입니다.
- 셰이더 핫 리로드는 GLSL이 아니라 실행 파일 옆 `shaders` 디렉터리의 `.spv` 변경을 감시합니다(재빌드로 `.spv`가 갱신될 때 동작). 감시 구현은 `ReadDirectoryChangesW` 기반 Windows 전용입니다.

**품질 관리**
- 자동 테스트는 위 15건이고 렌더러의 GPU 경로 · 메모리 할당자에는 없습니다(렌더러는 픽셀 비교 캡처로 확인). CI 설정도 없습니다.
- 결함 대장은 109건 중 94건 해결, 9건 열림(HIGH 4건 모두 Quest 이식 묶음), 3건 보류, 3건은 자동 조작 환경의 착시로 닫았습니다(2026-10-15). 열린 항목은 [Docs/Engine_Specification.md](Docs/Engine_Specification.md) 6장에 있습니다. 문서가 코드보다 뒤처진 사례가 실재했으므로, 이 저장소에서는 체크박스보다 코드 확인이 우선입니다.
- 의존성 중 stb(stb_truetype 용도, 이미지 디코드는 tinygltf에 들어 있는 stb_image)만 `GIT_TAG master`로 고정되지 않아 구성 시점에 따라 내려받는 소스가 달라질 수 있습니다.

## 문서

설계 문서·결함 대장·결정 기록은 모두 `Docs/`에 있고 전부 한국어입니다. 범주별 지도와 정본 우선순위는 문서 색인이 정의합니다.

**→ [Docs/README.md](Docs/README.md)**

| 자주 찾는 문서 | 내용 |
|---|---|
| [Engine_Specification.md](Docs/Engine_Specification.md) | 엔진 사양 명세서: 이력 · 구현 기능 · 미구현 · 열린 결함 · 우려 |
| [Roadmap.md](Docs/Roadmap.md) | 진행 예정 작업의 우선순위 정본 |
| [Architecture_Overview.md](Docs/Architecture_Overview.md) | 엔진 아키텍처 개요 |
| [Core_Stabilization_Spec.md](Docs/Core_Stabilization_Spec.md) | 결함 심층 분석 + 마스터 체크리스트 (정본) |
| [Core_Defects_Task_Spec.md](Docs/Core_Defects_Task_Spec.md) | 결함 작업 지시서 (정본) |
| [RendererRefactor.md](Docs/RendererRefactor.md) | 렌더러 구조: RenderContext · GpuScene · passes |
| [Memory_Allocator_Decision.md](Docs/Memory_Allocator_Decision.md) | 존 기반 메모리 할당자 결정 근거 |
| [Editor_Layout_Design.md](Docs/Editor_Layout_Design.md) | 에디터 레이아웃 설계와 진행 단계 |

문서에는 과거 서술을 뒤집는 `[정정]` 블록이 섞여 있습니다. 문서와 코드가 어긋나면 코드가 기준입니다. `Docs/archive/`의 외부 엔진 분석 문서는 구체 수치에 오류가 많아 근거로 인용하지 않습니다.

## 라이선스

아직 라이선스 파일이 없습니다. 즉 기본적으로 저작권자가 모든 권리를 보유합니다.
