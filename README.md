# Haruka

Vulkan 1.1 기반 C++20 게임 엔진. 렌더러, GPU 메모리 할당자, UI 위젯 시스템, 에디터를 외부 프레임워크 없이 직접 구현하는 1인 개발 프로젝트입니다. 현재 빌드·실행되는 형태는 **Windows 데스크톱 실행 파일 둘**입니다: 에디터가 함께 도는 `HarukaEngine`, 게임만 실행하는 `HarukaGame`(배포 ZIP 으로 내보냄, 2026-10-19 ~ 24). Quest 단독 실행용 Android APK 는 빌드까지 되고 헤드셋 첫 실행이 남았습니다(2026-10-31).

장기 목표는 VR입니다. PC OpenXR 연결(`--xr`, Quest Link 등 PC VR 런타임)이 들어 있고, 2026-10-15 Meta Quest Pro(Quest Link)에서 확인했습니다. 자세한 내용은 [알려진 한계](#알려진-한계)를 보십시오.

소스는 `src/` 기준 `.cpp`/`.hpp` 179 파일 30,275 줄, 셰이더는 GLSL 19 파일 705 줄입니다(2026-10-31). 릴리스·태그 없음, CI 없음, 자동 테스트 24건.

## 지금 동작하는 것

GLFW 창(1600x900) 하나를 띄웁니다. 에디터 실행 파일(`HarukaEngine`)은 그 안에서 엔진과 에디터가 함께 돌고, 월드 틱과 게임·물리·오디오 모듈 틱은 에디터가 Play 상태일 때 돕니다. 게임 실행 파일(`HarukaGame`)은 에디터 코드 없이 프로젝트 설정의 시작 씬을 열고 처음부터 Play 로 돕니다(엔진은 두 실행 형태를 앞단 인터페이스로만 구별합니다).

**패키징** — 에디터를 뺀 엔진을 `HarukaRuntime` 라이브러리로 묶어 두 실행 파일이 링크합니다. 게임 배포 ZIP 에는 `HarukaGame.exe`, 셰이더와 에셋을 묶은 `game.hpak`(자체 묶음 형식 v1: LZ4 64KiB 블록 · XXH3-64 · 대소문자 무시 찾기), VC++ 런타임 DLL, 서드파티 라이선스 고지가 들어갑니다. 엔진은 셰이더 · 에셋을 가상 파일 시스템(낱개 폴더와 묶음을 우선순위로 겹침)으로 읽고, 게임의 설정 · 로그 · 캐시는 `%LOCALAPPDATA%\Haruka\HarukaGame` 에 씁니다. 정본은 [Docs/Packaging_Design.md](Docs/Packaging_Design.md).

**렌더링** — 포워드 렌더링(G-buffer 없음). 메인 패스는 단일 `VkRenderPass` 안의 3개 서브패스입니다: (0) 깊이 프리패스 → (1) 불투명 PBR → (2) 투명 PBR + 라인·기즈모 오버레이. 서브패스 2에서 MSAA 컬러를 HDR(`R16G16B16A16_SFLOAT`) 타깃으로 resolve 합니다. MSAA는 디바이스 limits를 조회해 4x → 2x → 1x로 낮춥니다. 요구하는 디바이스 확장은 `VK_KHR_push_descriptor` 와 창이 있을 때의 `VK_KHR_swapchain` 이고, `VK_KHR_synchronization2` 는 있으면 씁니다(없으면 Vulkan 1.0 배리어). 컬링은 CPU에서 바운딩 스피어 대 6평면 판정으로 합니다.

**라이팅 · 포스트** — Directional / Point / Spot 3종(SSBO 전달, 감쇠 반경·스팟 내외각·색온도(Kelvin) 틴트·per-light Cast Shadows). 실제로 그림자를 만드는 것은 `castShadows`가 켜진 첫 Directional 라이트 하나이며, 2048² 섀도맵 1장 + 3x3 PCF입니다. SSAO(half-res 선형 깊이 → 헤미스피어 커널), 블룸(threshold + 5단계 다운/업샘플), ACES 톤매핑 + sRGB 인코딩이 동작합니다. IBL 큐브맵은 HDRI 파일이 아니라 하늘 그라디언트로 CPU에서 절차 생성하고, BRDF LUT만 GPU 오프스크린 렌더로 굽습니다.

**에디터** — 도킹(분할 노드 + 탭 스택, 스플리터 리사이즈, 탭 드래그 재도킹, 레이아웃 파일 저장·복원), 씬 파일 열기 · 저장 · 다른 이름으로 저장(에디터 안의 대화상자, 저장하지 않은 변경 확인), 아웃라이너 계층 트리 + 검색 + F2 리네임 + 다중 선택 + 끌어 놓아 부모 바꾸기, 디테일 패널(Transform · 컴포넌트 프로퍼티 · Add Component · Save as Prefab), 뷰포트 툴바, 이동/회전/스케일 기즈모와 스냅, 복사 · 붙여넣기 · 복제, F 화면 맞추기, Play/Pause/Stop, 모든 편집의 Undo/Redo, Output Log · Content Browser · GPU Stats 탭, `.spv` 핫 리로드.

**플레이어와 입력** — 플레이어 리그 · 추적 자세 · 기본 이동(데스크톱 WASD + 오른쪽 끌기 둘러보기, VR 스틱 이동 + 45° 스냅 회전) · 그립 잡기와 던지기 컴포넌트, 표준 구성 `XrPlayer` 프리팹과 시연 씬 `XrDemo`. 입력 바인딩(키 · 키 쌍 축 · 마우스 축 · XR 버튼과 축)과 시작 씬은 `assets/project.ini` 에서 정합니다.

**그 외** — Jolt Physics v5.1.0(기본 1/60 고정 스텝, XR 중에는 헤드셋 주사율, Box/Sphere/Capsule, Static/Dynamic/Kinematic), miniaudio 기반 3D 공간화 오디오, tinygltf glTF 로더 + 바이너리 메시 캐시(`.gasset`) + 텍스트 씬·프리팹 직렬화(`.hscene`/`.hprefab`), GPU 타임스탬프 계측(Shadow/Main/AO/Bloom/Tonemap/UI 구간).

## 빌드와 실행

### 요구 사항

| 항목 | 내용 |
|---|---|
| OS | Windows x64. 빌드·실행 스크립트가 배치 파일이며 다른 OS에서 검증된 적 없음 |
| 컴파일러 | MSVC. `build.bat`은 `Visual Studio 18 2026`을 기본 생성기로 쓰고, 해당 설치 경로가 없고 2022가 있으면 `Visual Studio 17 2022`로 내려갑니다(`-A x64` 고정) |
| CMake | 3.21 이상(셰이더 include 의존성 추적에 depfile 을 씀) |
| Vulkan SDK | 1.4.x(헤더 · `glslc`). 엔진 자체는 Vulkan 1.1 만 씁니다 |
| GPU 드라이버 | Vulkan 1.1 이상 + `VK_KHR_push_descriptor`. `synchronization2` · `samplerAnisotropy` 는 있으면 씁니다 |
| VR(선택) | `--xr` 로 띄울 때만. PC VR OpenXR 런타임(Meta Quest Link 등)과 헤드셋. 없으면 경고를 남기고 데스크톱으로 계속합니다 |
| 네트워크 | 첫 CMake 구성 시 필요(의존성을 FetchContent로 내려받음) |

미리 설치할 도구는 Visual Studio(MSVC), CMake, Git, Vulkan SDK 입니다. glm 1.0.1, GLFW 3.4, tinygltf v2.9.3, stb, miniaudio 0.11.21, JoltPhysics v5.1.0, OpenXR-SDK release-1.1.63(로더 정적 링크), LZ4 v1.10.0 · xxHash v0.8.3(묶음 파일)은 CMake FetchContent가 구성 시점에 내려받으며 저장소에는 포함되어 있지 않습니다. 셰이더는 런타임이 아니라 빌드 시점에 `glslc`로 SPIR-V(그래픽스 셰이더 18개)로 컴파일되므로, `glslc`를 찾지 못하면 구성 단계에서 실패합니다.

### 명령

```bat
build.bat                 :: Release (기본)
build.bat Debug           :: Debug
build.bat Release clean   :: CMake 캐시를 지우고 재구성

run.bat                   :: build\Release\HarukaEngine.exe 실행
run.bat Debug
run.bat Release --xr              :: Play 동안 헤드셋에 그림(에디터 뷰포트에 왼쪽 눈)
run.bat Release --emulate-stereo  :: 헤드셋 없이 두 눈 경로 확인(뷰포트를 좌우로 나눔)

cmake --build build --config Release --target PACKAGE   :: 게임 배포 ZIP(build\HarukaGame-<버전>-win64.zip)
```

`build.bat`은 `build\CMakeCache.txt`가 없을 때만 CMake를 구성합니다. 생성기나 옵션을 바꾸려면 `clean` 인자가 필요합니다. 전체 빌드 로그는 `Logs\build_log.txt`, error/fatal만 걸러낸 요약은 `Logs\build_error.txt`에 남습니다.

산출물은 `build\<Config>\HarukaEngine.exe`(에디터) · `HarukaGame.exe`(게임) · `HarukaPak.exe`(묶음 도구)와 `build\pack\game.hpak`(목록 `Packaging/game.pack.txt`)이고, SPIR-V와 `assets/`는 POST_BUILD로 실행 파일 옆에 복사됩니다. 게임 실행 파일은 VR 시작을 `assets/project.ini` 의 `xr.startInVr` 로 정하고 `--xr` · `--no-xr` 이 덮어씁니다. 실행 폴더에서 바로 띄우면 묶음 없이 낱개 파일을 읽고, `--loose` 를 주면 묶음이 있어도 낱개 파일을 먼저 찾습니다.

그리기 행 · 라이트 · 물리 바디 상한은 실행 폴더의 `Config/engine.ini` 에서 바꿉니다(처음 실행할 때 기본값과 설명으로 만들어지고, 바꾼 뒤에는 다시 실행). 시작 씬과 입력 바인딩은 프로젝트 설정 `assets/project.ini` 에 있고(버전 관리함), 에디터는 마지막에 연 씬을 먼저 엽니다(실행 폴더의 `Config/editor_state.txt`).

### 실행 시 주의

- 셰이더 · 에셋은 작업 디렉터리가 아니라 **실행 파일이 있는 디렉터리 기준**으로 찾습니다(2026-10-19, 예전에는 작업 디렉터리가 실행 폴더여야 했음). 에셋(`assets/...`)은 에디터 빌드가 프로젝트 폴더의 `assets/` 를 직접 읽고 씁니다(에디터에서 저장한 프리팹이 소스 트리에 남음). 그 폴더가 없으면 빌드가 실행 파일 옆에 복사한 `assets/` 를 씁니다. `run.bat`은 출력을 콘솔과 `Logs\latest.txt`에 함께 기록하며 종료 코드를 그대로 전달합니다.
- 실행 파일은 `/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup`으로 링크되어 콘솔 창이 뜨지 않습니다. 엔진 로그는 실행 방법과 상관없이 쓰기 폴더의 `Logs/<실행 파일 이름>.log`(UTF-8)에 남습니다. 쓰기 폴더는 에디터가 실행 폴더, 게임이 `%LOCALAPPDATA%\Haruka\HarukaGame` 입니다. exe를 직접 실행하면 로그 체계를 거치지 않는 C stdio 출력이 `Logs/stdout.txt`로 갑니다. 콘솔이 필요하면 구성 시 `-DHARUKA_CONSOLE=ON`.
- Debug 빌드는 Vulkan 검증 레이어를 켜며, 레이어가 없으면 시작 시 예외를 던집니다.
- UI 폰트는 저장소에 없습니다. `assets/fonts/default.ttf`를 먼저 찾고 없으면 시스템 폰트(`C:/Windows/Fonts/segoeui.ttf` 등)로 대체합니다.

### 테스트

```bat
cd build
ctest -C Release
```

18건입니다: `pak_format`(묶음 파일 왕복 · 손상 감지 · 결정적 출력 · 우선순위 · 동시 읽기), `geometry_conventions`(절차 도형 규약), `actor_lifecycle`(Actor 활성 · 컴포넌트 생명주기 · 카메라 자세), `command_stack_purge`, `actor_undo`(액터 생성 · 삭제 Undo), `log_category`, `audio_system`, `slate_input`(UI 입력 · 숫자 칸), `physics_lifecycle`(물리 바디 생명주기 · 물리 API · 잡기와 던지기 · 플레이어 리그), `stereo_views`(눈별 투영 · 두 눈 컬링), `xr_input`(VR 입력 · 키와 마우스 축 · 설정 줄 바인딩 · 햅틱), `draw_batching`(자동 인스턴싱 묶기 · 행 배정), `render_extensions`(패스 레지스트리 · 렌더 확장 지점 순서), `scene_components`(컴포넌트 타입 등록 · 씬 v4 값 · 회전 왕복), `light_selection`(라이트 선택 · 상한 설정 파일), `asset_cache`(GLTF 캐시 유효성 · 논리 경로 키 · 묶음 안 원본), `scene_document`(씬 변경 여부 · 원자적 저장 · 프로젝트 설정), `editor_commands`(ID 기반 Undo 커맨드 · 부모 바꾸기 · 클립보드 · 화면 맞추기). 외부 테스트 프레임워크 없이 자체 `check()`로 만든 콘솔 실행 파일이고, 창을 띄우지 않아 그래픽 디바이스 없이 돕니다.

## 저장소 구조

```
CMakeLists.txt   build.bat   run.bat
src/
  core/        Engine · 엔진 앞단 인터페이스 · World · Actor · Component · 실행 폴더와 쓰기 루트
  renderer/    Vulkan 렌더러, passes/(Shadow · Geometry · AO · Bloom · Post)
  memory/      HarukaAllocator (자체 3존 할당자)
  ui/slate/    HSlate — 자체 리테인드 모드 위젯 시스템 (14종)
  editor/      EditorApp(에디터 앞단) · 도킹 · 기즈모 · 커맨드 스택 · 셰이더 핫 리로드
  game/        기본 게임 모듈 · 게임 실행 파일의 앞단(GameFrontend)
  vfs/         묶음 파일 .hpak 형식 · 쓰기 · 읽기, 가상 파일 시스템
  tools/       묶음 도구 HarukaPak
  components/  Light · Camera · Mesh · Audio · RigidBody · XR 리그 · 추적 자세 · 이동 · 잡기
  physics/  audio/  input/  assets/  geometry/  platform/  xr/  main.cpp
shaders/       GLSL → 빌드 시 glslc 로 SPIR-V 컴파일
assets/        모델 1 · 시험 음원 3 · 씬 2 · 프리팹 2 · 프로젝트 설정(project.ini)
Packaging/     게임 묶음 목록(game.pack.txt), Android 매니페스트 · APK 포장 스크립트(android/)
res/           실행 파일 아이콘 · 매니페스트(UTF-8 코드 페이지)
tests/         단위 테스트 24개 (ctest)
Docs/          설계 · 결함 · 결정 기록 (색인: Docs/README.md)
```

## 설계 메모

**자체 메모리 할당자** (`src/memory/HarukaAllocator.hpp`) — VMA를 쓰지 않습니다. `vkAllocateMemory`를 한 번 호출해 1GB를 선점하고 전 범위를 지속 매핑한 뒤, Zone A 64MB(bump) / Zone B 896MB(free-list) / Zone C 64MB(ring, 프레임별 동적 데이터)로 나눠 씁니다. 합이 정확히 1GB임을 `static_assert`로 강제합니다. 메모리 타입은 `DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT`(UMA)를 1순위로 고르고, 1GB 를 한 번에 잡지 못하면 이유를 남기고 멈춥니다(축소 경로 없음). 타일 GPU 에서는 MSAA 컬러를 지연 할당 메모리에 따로 둡니다. Zone B는 `bufferImageGranularity`를 조회해 이미지의 시작 주소를 그 단위에 맞춰 정렬합니다. UI(HSlate) 버퍼 · 폰트 아틀라스와 라인 · 기즈모 정점 버퍼는 이 힙 밖에서 따로 할당합니다.

**좌표의 단일 진리원** (`src/editor/EditorGizmo.hpp`) — 마우스↔월드 변환을 `WorldViewport` 하나가 `project()` / `ray()`로 전담하고, 뷰포트 rect를 고정 치수가 아니라 뷰포트 위젯의 실측 geometry에서 유도합니다. 렌더 해상도(`renderExtent`)와 스왑체인 서브렉트도 분리되어 있어, 패널을 어디에 도킹하거나 리사이즈해도 픽·기즈모·렌더가 같은 좌표계를 씁니다.

**HSlate — 자체 UI 시스템** (`src/ui/slate/`) — Dear ImGui를 쓰지 않고 UE Slate를 본뜬 리테인드 모드 위젯 시스템을 직접 만들었습니다. 위젯 14종, 2패스 레이아웃(`computeDesiredSize` → `onArrangeChildren`), `Attr<T>` 지연 평가 바인딩, 마우스 캡처, 자체 폰트 아틀라스와 전용 `VkRenderPass`. 외부 의존은 폰트 래스터라이즈용 stb_truetype 하나뿐입니다.

**Actor + Component 컴포지션** (`src/core/`) — ECS가 아니며, 이는 측정에 근거한 선택입니다([Docs/ECS_Decision.md](Docs/ECS_Decision.md)). `World`가 `unique_ptr<Actor>`를, `Actor`가 `unique_ptr<Component>`를 소유하고 컴포넌트 조회는 `std::type_index` 캐시로 O(1)입니다. Unity식 `activeSelf` / `activeInHierarchy`, 활성 전이 시 서브트리 `onEnable`/`onDisable` 발화, 틱 중 파괴 요청을 지연 큐로 미루는 파괴 경로를 갖췄습니다. Engine은 고정 순서 코어 서비스(창 → 렌더러 → 에셋) 위에 동적 모듈(Audio, Physics)과 에디터·게임 모듈을 얹는 2계층 부트스트랩이고, 소멸은 역순입니다.

## 알려진 한계

아래는 전부 코드로 확인한 현재 상태입니다.

**플랫폼 · VR**
- 데스크톱은 Windows 전용입니다. CI가 없어 Linux/macOS에서 빌드된 적이 없습니다. macOS는 MoltenVK에 필요한 `VK_KHR_portability_enumeration` 처리가 없습니다.
- Vulkan 1.1 이상과 `VK_KHR_push_descriptor` 가 필요합니다. 1.0 장치나 push descriptor 가 없는 장치에서는 이유를 남기고 시작하지 않습니다.
- **VR은 첫걸음 단계입니다.** PC OpenXR 연결(`--xr`)을 Quest Link(Meta Quest Pro)에서 확인했습니다(2026-10-15). 헤드셋에는 Play 동안만 그립니다. 소리는 에디터에서 고른 출력 장치(Edit > Audio Output, 기본은 시스템 기본 장치)로 나가고, XR Play 동안에는 헤드셋 장치로 바뀝니다(2026-10-16, 이 자동 전환은 헤드셋 확인이 남음). XR 에는 언제나 리그가 있습니다. 씬에 플레이어 리그가 없으면 카메라 아래 바닥에 보이지 않는 리그를 붙이고, 리그 높이는 실제 키(LOCAL_FLOOR) 또는 고정 눈높이입니다(2026-10-16). 두 눈은 눈마다 따로 그립니다(멀티뷰는 Quest 이식 때). 입력은 기본 액션 세트 하나입니다. 플레이어 리그 · 잡기와 던지기는 2026-10-16 헤드셋에서 확인했습니다.
- Quest 단독 실행(Android)은 APK 까지 만들었고(2026-10-31, NDK 빌드 · Gradle 없음) 헤드셋 첫 실행이 남았습니다. 첫 실행은 Android 빌드가 만든 묶음 파일을 `adb push` 로 넣고 소리를 끕니다. 엔진은 묶음의 셰이더가 실행 파일과 같은 빌드의 것인지 확인합니다(2026-11-01). 방법은 [Docs/Packaging_Design.md](Docs/Packaging_Design.md) 7절.

**렌더러**
- TAA는 없습니다. 지터·모션벡터 없는 구현이 고스팅만 내서 껐고, 남아 있던 코드도 2026-10-04 에 지웠습니다. 안티에일리어싱은 MSAA가 전담합니다.
- 컬링은 CPU에서 합니다. GPU 컴퓨트 컬링은 순수 오버헤드여서 dispatch를 멈췄고, 코드도 2026-10-04 에 지웠습니다.
- 섀도맵은 2048² 한 장이고 캐스케이드(CSM)가 없으며, 캐스터는 Directional 라이트 하나뿐입니다.
- IBL이 절차 생성 큐브맵이라 외부 HDRI 환경 맵을 불러올 수 없습니다.
- 프레임 인 플라이트 2. 한 프레임에 그릴 수 있는 메시 수(transform 행)는 기본 16384 이고 `Config/engine.ini` 로 바꿉니다.
- 저장소에 커밋된 벤치마크나 프레임 타임 수치가 없습니다. GPU 타임스탬프 계측 코드만 있습니다.

**엔진 · 런타임**
- 게임 배포 형태는 Windows ZIP 하나입니다(2026-10-24). 다른 PC(ReBAR 가 없는 GPU 등)에서 실행해 본 적이 없고, 게임 설정 화면(창 크기 · 전체 화면 · 소리 장치)이 없습니다. Quest(Android) APK 는 빌드까지 되고, 헤드셋에서는 아직 실행해 보지 않았습니다.
- 게임 입력 바인딩은 `assets/project.ini` 에서 정합니다(2026-10-15). 에디터 카메라는 바인딩을 거치지 않고 창 입력을 직접 씁니다.
- 물리에 컨스트레인트·조인트 API가 없습니다.
- 잡은 물체는 Kinematic 으로 손을 따라가므로 벽을 뚫을 수 있고(속도 추적 잡기 없음), 플레이어 리그 이동은 벽에 막히지 않습니다(캐릭터 컨트롤러 없음). 텔레포트도 없습니다.
- 문서에서 말하는 "Zero-Copy 로딩"은 이름만 남은 상태입니다. `GassetCache::load`는 `ifstream`으로 호스트 vector에 읽은 뒤 업로드합니다.

**HSlate · 에디터**
- HSlate 의 스크롤 막대는 위치만 보여 주고 끌어서 스크롤하지는 못합니다(휠로 스크롤). 클리핑과 스크롤 상자는 2026-10-03 에 들어갔습니다.
- 폰트 아틀라스가 ASCII 32~127만 래스터라이즈합니다. **UI에 한글·유니코드를 표시할 수 없어** 에디터 문자열은 전부 영문이고 아이콘 대신 ASCII 글리프를 씁니다(문서와 코드 주석은 한국어).
- UI는 한 프레임에 정점 65,536개(쿼드 16,384개)까지만 그리고 초과분은 조용히 버립니다.
- Undo/Redo 는 최대 64단계입니다. 2026-10-15 부터 에디터의 모든 편집이 Undo 되고, 커맨드가 액터를 ID 로 찾으므로 삭제를 되돌리면 그 전 편집 이력도 이어집니다.
- 여러 액터를 고르면 디테일 패널은 주 선택 하나만 보여 줍니다(여러 액터의 속성을 한 번에 바꾸지 못함). 패널의 부동 창 분리, 뷰 모드(Lit/Unlit/Wireframe) 전환이 없습니다.
- 씬은 프로젝트 폴더의 `assets/scenes/` 아래에만 저장합니다(에디터 안의 대화상자, OS 파일 대화상자는 쓰지 않음).
- 액터 픽은 삼각형 단위가 아니라 컴포넌트 로컬 바운드 합집합에 대한 광선-OBB 판정입니다.
- 셰이더 핫 리로드는 GLSL이 아니라 실행 파일 옆 `shaders` 디렉터리의 `.spv` 변경을 감시합니다(재빌드로 `.spv`가 갱신될 때 동작). 감시 구현은 `ReadDirectoryChangesW` 기반 Windows 전용입니다.

**품질 관리**
- 자동 테스트는 위 17건이고 렌더러의 GPU 경로 · 메모리 할당자에는 없습니다(렌더러는 픽셀 비교 캡처로 확인). CI 설정도 없습니다.
- 결함 대장은 147건 중 111건 해결, 30건 열림(HIGH 4건 모두 Quest 이식 묶음), 3건 보류, 3건은 자동 조작 환경의 착시로 닫았습니다(2026-10-31). 열린 항목은 [Docs/Engine_Specification.md](Docs/Engine_Specification.md) 6장에 있습니다. 문서가 코드보다 뒤처진 사례가 실재했으므로, 이 저장소에서는 체크박스보다 코드 확인이 우선입니다.
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
