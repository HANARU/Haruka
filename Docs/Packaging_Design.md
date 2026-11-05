# 패키징 설계 (Windows · Android XR)

> **문서 지위**: 에디터 없이 게임만 실행하는 배포 형태(패키징)의 정본입니다. 2026-10-17 사용자가 목표로 정했고
> ("standalone VR 이 사실상 Quest 단독 실행을 위한 패키징이다. 방향은 같다"), 2026-10-19 에 다른 엔진 조사 5건을 거쳐
> 1단계 결정을 받고 구현했습니다. 엔진 기본 기능 다섯 가지 중 5번(독립 실행 빌드)이 이 문서입니다.
>
> **기준 시점**: 커밋 `4cc931a` 위의 미커밋 작업(2026-10-19, 묶음 파일 · 실행 확인은 2026-10-24). 엔진 전체의 현재 상태는
> [Engine_Specification.md](Engine_Specification.md), 결함은 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 를 보십시오.

---

## 1. 목표와 단계

패키징 대상은 Windows 와 Android XR 계열(Quest Pro 우선, Quest 3 · 다른 Android XR 기기 고려)입니다. 두 대상 모두
"에디터 없이 게임만 실행"이 공통이라, Windows 에서 확인할 수 있는 일을 먼저 끝내고 Android 를 마지막에 붙입니다.

| 단계 | 내용 | 확인 장소 | 상태 |
|---|---|---|---|
| 1. Windows 첫 패키징 | 런타임 분리(DEF-12), 실행 폴더 기준 경로와 쓰기 루트, 배포 폴더(ZIP), 자체 묶음 파일 | PC | 끝남(2026-10-19 ~ 24, 실행 확인 포함). `--xr` 로 Quest Link 실행 확인만 남음 |
| 2. Android 준비 | DEF-97 · 04(Vulkan 1.1), DEF-42(1GB 미만 힙 차단), DEF-06(MSAA 지연 할당), 창 없는 렌더 경로 | PC + Quest Link | 코드 끝남(2026-10-31, 6절). 창 · 헤드셋 확인만 남음 |
| 3. Android | NativeActivity 진입(DEF-08 · 103), Khronos OpenXR 로더, Manifest · APK(Gradle 없이), 앱 수명주기, APK 안 파일 읽기 | Quest Pro 실기 | 진행 중(2026-10-31, 7절). APK 까지 만듦, 헤드셋 첫 실행 남음 |
| 나중 | Zone 별 할당, ReBAR 없는 PC 의 스테이징, DEF-02, 깊이 resolve, 멀티뷰, Quest 에서 DEF-34 측정 | | |

## 2. 조사 요약 (2026-10-19)

조사는 다섯 갈래(런타임 분리 · Android OpenXR 진입 · Vulkan 1.1 대체 경로 · 에셋 배포와 쓰기 경로 · GPU 힙)였고, 결정 6 뒤에
묶음 형식 조사를 한 번 더 했습니다. 기록과 달랐거나 새로 알게 된 것만 적습니다.

- **런타임 분리**: UE(Game · Editor 타깃, `WITH_EDITOR`)와 O3DE(Editor · GameLauncher)는 빌드 타깃을 나눕니다. Godot 은 export
  template(에디터 없이 빌드한 엔진)이고, 에디터의 Play 를 별도 프로세스로 실행합니다. Unity 는 미리 만든 플레이어 + 데이터입니다.
  Haruka 는 게임 코드가 C++ 로 실행 파일에 들어가므로 UE · O3DE 방식이 맞습니다.
- **Quest 에는 화면 표면이 필요 없습니다.** Khronos hello_xr 과 Meta OpenXR SDK 예제는 Android 에서 VkSurfaceKHR · VkSwapchainKHR
  를 만들지 않고 XR 스왑체인에만 그립니다. 창(ANativeWindow)은 수명주기에만 쓰입니다. 그래서 DEF-08 에 적힌 해법("AndroidWindow 로
  화면 표면을 만든다")은 "창 없는 렌더 경로"로 바꿔야 합니다. 데스크톱 없이 두 눈만 그리는 프레임 경로는 DEF-106 때 이미 있습니다.
- **Quest 의 Vulkan**: 현재 OS 에서 Quest 2 · Pro 는 1.1.295, Quest 3 · 3S 는 1.3.295 입니다(gpuinfo.org). synchronization2 ·
  push descriptor · multiview · renderpass2 · LAZILY 메모리 · 4x MSAA 는 네 기기 모두 지원합니다.
- **DEF-97 은 반드시 고쳐야 합니다.** Android 의 libvulkan 은 `vkCmdPushDescriptorSet` 을 API 36 부터, `vkCmdPipelineBarrier2` 를
  API 33 부터 내보냅니다. Quest 는 API 34 라 지금 코드는 링크 · 로드 단계에서 막힙니다. Quest Link 런타임도 Vulkan 범위를
  "1.0 ~ 1.2" 로 알려 주는데 엔진은 1.4 를 요청하고 있었습니다.
- **Quest 메모리**: `maxMemoryAllocationSize` 가 정확히 1GiB 라 지금 1GB 힙이 한도에 딱 맞고, 축소 경로는 타지 않습니다. 앱 한도는
  PSS 기준 Quest Pro 4.4GiB(GPU 메모리 포함)입니다. Quest 해상도에서 Zone B 가 텍스처 전에 쓰는 양은 375 ~ 540MB 로 896MB 안입니다.
- **ReBAR 없는 PC** 에서는 메가 버퍼를 포함한 버퍼 전체가 시스템 메모리에 놓입니다(개발 PC 는 ReBAR 가 켜져 있어 탄 적 없음).
  지연 할당 메모리는 규격상 CPU 에서 접근할 수 없어서, "힙 하나 · 타입 하나" 전제는 UMA · ReBAR 기기에서만 성립합니다.
- **배포**: Quest APK 는 1GB 미만, OBB 는 파일 하나에 4GB 미만입니다. Android 는 APK 안의 파일을 표준 파일 입출력으로 읽을 수 없어
  AAssetManager 가 필요합니다. 쓰기 위치는 UE 가 `%LOCALAPPDATA%\<프로젝트>\Saved`, Unity 가 `LocalLow`, Godot 이 `%APPDATA%` 입니다.

## 3. 1단계 결정 (2026-10-19 사용자 결정)

사용자 지시: "결정 6을 제외하고 전부 권장대로 진행한다. 결정 6은 자체 묶음 파일을 쓰는 게 좋아보여. 가장 큰 이유는 … 실제 게임
개발에 들어가면 더 커질거야. 규약을 하나 만들어두는게 좋아."

| # | 결정 | 고른 안 | 이유 |
|---|---|---|---|
| 1 | 게임 실행 파일을 만드는 방식 | 빌드 타깃 분리(HarukaRuntime 라이브러리 + 에디터 `HarukaEngine.exe` + 게임 `HarukaGame.exe`), 엔진은 앞단 인터페이스로만 에디터 · 게임을 부름 | 게임 실행 파일에 에디터 코드가 들어가지 않고, 런타임이 에디터에 기대면 링크가 실패해 분리가 구조로 지켜짐. Android 는 같은 라이브러리에 진입점만 바꿔 얹음 |
| 2 | 게임 빌드에서 VR 을 켜는 방법 | `assets/project.ini` 의 `xr.startInVr` + 명령줄 `--xr` · `--no-xr` 덮어쓰기. Android 는 항상 XR | 더블클릭으로 VR 시작, 시험은 명령줄로. UE 의 Start in VR, Unity 의 Initialize XR on Startup 과 같은 자리 |
| 3 | 헤드셋이 없거나 런타임이 세션을 끝냈을 때(Windows) | 시작할 때 헤드셋이 없으면 데스크톱으로 계속(제목에 표시), 실행 중 EXITING 이면 종료, LOSS_PENDING 이면 데스크톱으로 계속하며 다시 연결 시도 | 헤드셋 없이 시험하기 쉽고, 대시보드에서 종료하면 사용자 기대(OpenXR 권고)대로 앱이 끝남 |
| 4 | 헤드셋을 벗었을 때 | 게임 실행 파일만 일시정지(초점을 잃으면 Pause 와 같은 경로, 돌아오면 이어서). 에디터 Play 는 그대로 | Meta 심사 요건 VRC.Quest.Functional.2. 개발 중에는 헤드셋을 벗고 화면을 보는 일이 많음 |
| 5 | 설정 · 로그 · 캐시를 쓰는 위치 | 게임: Windows `%LOCALAPPDATA%\Haruka\<실행 파일 이름>`, Android 외부 앱 폴더. 에디터: 실행 폴더(지금과 같음). 설정은 두 겹(패키지 기본값 + 사용자 덮어쓰기) | Program Files 에 설치해도 쓸 수 있고 사용자마다 따로. 점검 도구는 에디터의 실행 폴더를 그대로 씀 |
| 6 | 에셋 배포 방식 | **자체 묶음 파일**(사용자 결정, 권장안은 폴더 그대로였음) | 게임 개발에 들어가면 에셋이 커지므로 규약을 먼저 둠. 형식은 5절에서 결정 |
| 7 | VC++ 런타임 | DLL 을 실행 파일 옆에(CMake `InstallRequiredSystemLibraries`) | 압축만 풀면 실행되고 관리자 권한이 필요 없음. 정식 설치 프로그램을 만들면 vc_redist 로 바꿈 |

작은 결정(제안대로): 에디터 실행 파일 이름 유지 · 게임은 `HarukaGame.exe`, 게임의 시작 씬은 `project.ini` 의 `startScene`, 창 닫기는
즉시 종료, 데스크톱 마우스 시선은 오른쪽 버튼 끌기, 게임 창 1600x900, 게임 빌드의 디버그 선은 끔(그와 별개로 엔진이 매 프레임 비움),
게임의 소리 장치는 시스템 기본 + XR 중 헤드셋 자동 전환, 한글 경로는 UTF-8 코드 페이지 매니페스트, 배포 도구는 CMake install + CPack ZIP.

## 4. 1단계 구현 (2026-10-19)

### 4-1. 런타임 분리 (결정 1, DEF-12)

- `CMakeLists.txt`: 에디터를 뺀 엔진 전부를 `HarukaRuntime` 정적 라이브러리로 한 번만 빌드합니다. `HarukaEngine.exe` 는
  `src/editor/*` + `editor/EditorFrontend.cpp`, `HarukaGame.exe` 는 `game/GameFrontend.cpp` 를 더해 링크합니다. `HARUKA_EDITOR` 와
  소스 트리 경로 `HARUKA_PROJECT_DIR` 은 에디터 실행 파일에만 정의합니다. 게임 실행 파일에는 에디터 문자열이 들어가지 않는 것을
  실행 파일 검사로 확인했습니다.
- `core/IEngineFrontend.hpp`: 엔진이 에디터 대신 부르는 인터페이스입니다(부팅 · 첫 씬 · Play 상태 · 창 닫기 · tick · 입력 ·
  창 제목 · 헤드셋 소리). 실행 파일마다 링크한 `createEngineFrontend()` 가 앞단을 고르므로 엔진 코드에 `#ifdef` 가 없습니다.
  실행 형태마다 다른 규칙은 `Policy`(프로젝트 폴더 · VR 시작 · 초점 일시정지 · EXITING 종료 · 재연결 · 쓰기 루트)로 넘깁니다.
- `EditorApp` 이 이 인터페이스를 구현합니다(동작은 그대로). 앞단 멤버는 예전 에디터 자리에 두어 파괴 순서도 같습니다.
- `GameFrontend`: 시작 씬을 `SceneSerializer::readScene` 으로 열고 `World::setPlaying(true)` 로 처음부터 Play 입니다. 에디터의 Play
  와 같은 경로라 게임 코드는 두 실행 형태를 구별하지 않습니다. 씬에 켜진 primary 카메라가 없으면 에디터가 처음 여는 시점과 같은
  자리에 기본 카메라를 놓습니다(UE 의 기본 폰과 같은 자리). 오른쪽 버튼 끌기를 시선 축으로 넘깁니다.
- 디버그 선(`hdebug`)은 에디터만 비워, 에디터가 없거나 최소화로 에디터 tick 을 건너뛰면 끝없이 쌓였습니다. 이제 엔진이 앞단 tick
  뒤 · 월드 tick 전에 비웁니다.

### 4-2. XR 실행 규칙 (결정 2 · 3 · 4)

- `EngineOptions::xr` 가 셋 중 하나(지정 안 함 · `--xr` · `--no-xr`)입니다. 지정하지 않으면 게임 실행 파일은 `xr.startInVr`
  (기본 false), 에디터는 끔입니다. 게임은 시작하자마자 Play 라 세션도 바로 엽니다.
- `XrSystem::lossReason()`: 세션을 잃은 이유를 Exiting · Disconnected(LOSS_PENDING) · InstanceLost 로 구별합니다. 게임 실행 파일은
  Exiting 이면 창 닫기를 요청해 끝내고, Disconnected 면 데스크톱으로 계속하며 2초마다 `headsetAvailable()`(로그 없는 xrGetSystem)로
  확인해 돌아오면 세션을 다시 엽니다. 에디터는 예전처럼 데스크톱으로 계속하고 다음 Play 에서 엽니다.
- 창 제목: VR 을 원했는데 쓸 수 없으면 "VR unavailable", 연결이 끊겨 기다리는 중이면 "VR disconnected" 를 덧붙입니다.
- 초점 일시정지: 게임 실행 파일에서 세션이 있고 입력 초점(FOCUSED)이 없으면, Play 를 Pause 로 겹쳐 월드 · 모듈 tick 을 멈추고 소리
  그룹을 일시정지합니다. 세션을 시작해 FOCUSED 가 되기 전 몇 프레임도 멈춘 상태입니다.

### 4-3. 경로와 쓰기 루트 (결정 5)

- `core/AppPaths.hpp/.cpp`: 실행 폴더(`GetModuleFileNameW`)와 쓰기 루트를 한 곳에서 정합니다. `main()` 이 앞단을 먼저 만들고 정책에
  따라 쓰기 루트를 정한 뒤 로그를 엽니다. 게임 실행 파일은 `%LOCALAPPDATA%\Haruka\HarukaGame` 이고, 그 폴더를 얻지 못하면 실행 폴더로
  내려가며 경고합니다. 로그 파일 이름은 실행 파일 이름을 따릅니다(에디터는 예전과 같은 `Logs/HarukaEngine.log`).
- 실행 폴더 기준: 셰이더(`readFile`, `HSlateRenderer::loadSpv`, 핫 리로드 감시 폴더), 에셋 루트(프로젝트 폴더가 없으면 엔진이
  `hproj` 루트를 실행 폴더로 정함). 예전에는 모두 작업 폴더 기준이라 `run.bat` 의 `pushd` 덕분에만 맞았습니다.
- 쓰기 루트 기준: 로그 · `stdout.txt` · `Config/log_levels.txt` · `pipeline_cache.bin` · glTF 캐시(에디터는 실행 폴더의
  `assets/cache`, 게임은 `Cache/models`) · 에디터 설정 파일(`editor_state` · `editor_layout` · `editor_log`).
- 상한 설정 `Config/engine.ini` 는 두 겹입니다. 실행 폴더의 파일(패키지 기본값) 위에 쓰기 루트의 파일(사용자 값)을 덮습니다.
  에디터는 두 자리가 같아 한 파일이고, 없으면 예전처럼 기본값으로 만듭니다. 게임 실행 파일은 파일을 만들지 않습니다.
- 로그 파일을 열지 못하면 이제 경고를 남깁니다(예전에는 아무 표시 없이 로그 없이 돌았음).
- `res/Haruka.manifest`: 프로세스 코드 페이지를 UTF-8 로 둡니다(Windows 10 1903 이상). 엔진은 경로를 `std::string` 으로 다루므로,
  이것이 없으면 사용자 이름이 한글인 `%LOCALAPPDATA%` 경로가 시스템 코드 페이지로 해석됩니다.

### 4-4. 배포 폴더 (결정 7)

`cmake --build build --config Release --target PACKAGE` 가 `build/HarukaGame-<버전>-win64.zip` 을 만듭니다. 설치 규칙은 모두 Game
구성 요소로 둡니다. 외부 라이브러리(GLFW · Jolt · OpenXR)가 자기 헤더 · 라이브러리 설치 규칙을 갖고 있어, 구성 요소를 가리지 않은
첫 시험에서는 `include/` · `lib/` 300여 개가 함께 들어갔습니다.

| 들어가는 것 | 비고 |
|---|---|
| `HarukaGame.exe` | OpenXR 로더는 정적 링크라 DLL 이 없음 |
| `game.hpak` | 셰이더 18개와 에셋 9개의 묶음(2026-10-24, 5절). 2026-10-19 에는 묶음 대신 `shaders/` · `assets/` 폴더를 넣었음 |
| `THIRD_PARTY_NOTICES.txt` | 서드파티 라이선스 고지(2026-10-24). 구성할 때 받은 소스의 라이선스 파일에서 만듦 |
| VC++ 런타임 DLL 8개 | `InstallRequiredSystemLibraries`. UCRT 는 Windows 10 부터 운영체제에 있어 넣지 않음 |

2026-10-24 기준 ZIP 은 11개 파일입니다. 옛 셰이더 결과물(목록에서 빠진 `.spv`, 예: 2026-10-04 에 지운 `cull.comp.spv`)은 구성할 때
빌드 폴더에서 지워, 묶음에 따라 들어가지 않습니다.

### 4-5. 확인

- 빌드 경고 0(엔진 · 테스트), 테스트 17개 모두 통과. `scene_document` 에 `xr.startInVr` 읽기 3개를 더했습니다.
- 실행 확인(2026-10-24, 사용자 허락):
  - 배포 ZIP 을 저장소 밖에 풀고 다른 작업 폴더에서 `HarukaGame.exe` 를 실행했습니다. 시작 씬(LightingProbe, 13 액터)과 기본 카메라가
    보이고 창 제목은 "Haruka | LightingProbe" 입니다. 쓰기는 `%LOCALAPPDATA%\Haruka\HarukaGame` 에만 했고(로그 · 파이프라인 캐시 ·
    glTF 캐시), 배포 폴더와 작업 폴더에는 아무것도 쓰지 않았습니다. 정상 종료(코드 0).
  - 이 확인에서 결함 하나를 찾아 고쳤습니다. UI 패스의 렌더 패스(finalLayout = PRESENT_SRC)가 스왑체인 이미지의 표시 전 레이아웃
    전환을 맡고 있어, 에디터 없이 실행하면 프레임마다 검증 오류(VUID-VkPresentInfoKHR-pImageIndices-01430)가 났습니다. 이제 UI 가
    없으면 렌더러가 직접 전환합니다(`Renderer_render.cpp`). 고친 뒤 검증 메시지 0건입니다.
  - 패키지 생성이 Visual Studio 자동 업데이트(2026-10-20, 재배포 폴더 14.51 → 14.52) 뒤 캐시에 남은 옛 경로 때문에 실패했습니다.
    캐시된 경로가 사라졌으면 다시 찾게 했습니다(`CMakeLists.txt`).
  - 에디터 회귀: Play · Stop 정상, 설정 파일은 실행 폴더 `Config/` 그대로, 검증 메시지 0건, 정상 종료.
  - 두 실행 파일의 빌드 후 단계가 같은 폴더로 셰이더 · 에셋을 복사해 병렬 빌드에서 가끔 부딪혔습니다(MSB3073). 복사를 `RuntimeFiles`
    대상 하나로 모으고 두 실행 파일이 그 대상에 기대게 했습니다.
- 남은 실행 확인: `--xr` 로 Quest Link 실행(시작 때 세션 · 초점 일시정지 · 대시보드 종료 · 연결 끊김과 재연결). 헤드셋을 준비할 때 합니다.

## 5. 자체 묶음 파일 (결정 6) — 형식 v1

**2026-10-24 사용자 결정: 권장안 전부("진행해").** 압축은 LZ4, 손상 검사는 XXH3-64, 경로는 원래 표기를 저장하고 찾을 때 대소문자를
무시(대소문자만 다른 두 파일은 도구 오류), 정렬은 16B · 64KiB 이상 4KiB, 줄 끝은 도구가 텍스트 에셋(씬 · 프리팹 · ini)을 LF 로 바꿔
넣음(저장소는 그대로), glTF 캐시 키는 이번에 "논리 경로 + 내용 해시"로 바꿈. 작은 결정: 묶음 순서는 파일 이름 규칙, 낱개 파일 우선은
`--loose`, 넣을 파일 목록은 `Packaging/`, 메모리 매핑 · 암호화 · 서명은 v1 에 없음, OBB 기준은 Android 단계. 배포물에는 서드파티
라이선스 고지 파일을 함께 넣습니다(BSD-2 · MIT · Apache 2.0 은 실행 파일 배포에 문구를 요구).

아래는 결정 전에 보고한 형식안입니다(구현하면서 바뀐 점은 구현 절에 적습니다).

2026-10-19 조사(UE pak · IoStore, Godot pck, Valve VPK, pk3, BSA/BA2, O3DE Archive, Defold, PhysicsFS)를 바탕으로 한 형식안입니다.

- 단일 파일 `.hpak`. 앞에 128B 머리, 이어서 파일 데이터, 끝에 목차(머리에 목차 위치 · 크기 · 해시). Godot v3/v4 pck(2025-06)와
  O3DE Archive 가 같은 배치이고, 한 번에 순차로 쓸 수 있습니다.
- 경로는 정규화한 UTF-8 논리 경로 문자열(`assets/…`, `shaders/…`)을 그대로 저장하고, 항목은 경로 순으로 정렬합니다.
- 압축은 파일마다: 그대로 저장(ogg · png · jpg · ktx2 · wav 등 이미 압축되었거나 PCM 인 것)과 LZ4 64KiB 독립 블록(glb · spv · 씬 등).
  압축 결과가 원본의 90% 이상이면 저장. 블록이 독립이라 소리 스트리밍처럼 중간부터 읽을 때 블록 하나만 풉니다.
- 정렬은 항목 16B, 64KiB 이상 항목 4KiB. 무결성은 머리 해시 · 목차 해시 · 전체 크기 · 항목마다 압축 전 내용의 XXH3-64.
- 여러 묶음: 기본 `game.hpak` < DLC < 패치 `game_pN.hpak`(N 이 클수록 위). 개발용으로 `--loose` 를 주면 낱개 폴더가 맨 위.
- 암호화 · 서명 · 메모리 매핑은 v1 에 넣지 않고 비트 · 필드 자리만 남깁니다.
- 묶음 도구 `HarukaPak`(build · list · verify · extract, 결정적 출력), 가상 파일 시스템(`src/vfs`, Windows 는 위치 지정 읽기,
  Android 는 `AAsset_openFileDescriptor64` + `pread`, APK 에 `noCompress "hpak"`), 서드파티 연결(tinygltf `LoadBinaryFromMemory`,
  miniaudio `pResourceManagerVFS`, 셰이더 · 글꼴 · 씬은 `readAll`).
- 예상 작업량 6 ~ 8일. 새 의존성: LZ4(BSD-2), xxHash(BSD-2, 헤더 하나).

결정 전에 고른 선택지(2026-10-24 결정으로 닫힘): 압축 코덱(LZ4 · zstd · stb deflate · 압축 없음), 체크섬(XXH3 · CRC32), 경로
대소문자 규칙, 정렬 값, 묶음 순서 규칙(파일 이름 · `packs.ini`), 낱개 우선 방식, 목록 파일 위치, 암호화 · 서명, 메모리 매핑, glTF 캐시
키, 줄 끝(`.gitattributes` 가 없고 `core.autocrlf=true` 라 텍스트 에셋의 줄 끝이 체크아웃마다 달라질 수 있어 결정적 출력과 얽힘).

### 5-1. 구현 (2026-10-24)

형식안 그대로 구현했습니다. 바이트 배치의 정본은 `src/vfs/PakFormat.hpp` 입니다(머리 128B · 목차 머리 32B · 항목 48B 를 `static_assert` 로
고정).

| 부분 | 파일 | 내용 |
|---|---|---|
| 형식 | `src/vfs/PakFormat.hpp/.cpp` | 상수 · 레코드, 경로 정규화(`normalizePath`) · 찾기 키(`foldKey`, ASCII 소문자), XXH3-64(xxHash 를 이 번역 단위에만 넣음) |
| 쓰기 | `src/vfs/PakWriter.hpp/.cpp` | 정규화 · 대소문자 충돌 검사 · 찾기 키 순 정렬 · 블록마다 LZ4 HC(수준 9 고정, 블록이 줄지 않으면 원문 블록) · 90% 규칙 · 정렬 패딩 0 · 마지막에 머리를 다시 씀 |
| 읽기 | `src/vfs/PakReader.hpp/.cpp` | 마운트 때 머리 해시 · 목차 해시 · 전체 크기 · 모든 항목 범위 · 블록 합 · 경로 정규화 · 중복을 확인. `readAll` 은 내용 해시를 확인, `readRange` 는 필요한 블록만 풂. 모르는 코덱 · 항목 플래그면 그 파일만 실패. 읽는 바탕은 파일(Windows `ReadFile` + OVERLAPPED 위치 지정, POSIX `pread`) · 메모리 |
| 가상 파일 시스템 | `src/vfs/Vfs.hpp/.cpp` | 낱개 폴더(접두어 제한 가능) · 묶음 마운트, 우선순위 높은 순(같으면 나중 마운트), `mountGamePacks`(이름 규칙, 패치 번호는 숫자 크기), 읽기 · stat · 내용 해시 · 실제 경로. 읽기는 공유 잠금을 쥔 채 하므로 여러 스레드가 함께 읽음. 절대 경로는 그 파일을 직접 읽음 |
| 묶음 도구 | `src/tools/HarukaPak.cpp` | `build`(목록 파일 · `-D` 변수 · 임시 파일에 쓴 뒤 바꿈) · `list` · `verify` · `extract` |
| 목록 | `Packaging/game.pack.txt` | `map` 으로 `assets/` · 셰이더 결과 폴더를 넣고, 캐시 · 임시 · 숨김 파일 · 숨김 폴더 아래(5-2) · 탐색기 파일을 빼고, 이미 압축된 형식은 그대로 저장, 씬 · 프리팹 · ini · txt 는 줄 끝을 LF 로 |
| 빌드 | `CMakeLists.txt` | `HarukaVfs` 라이브러리(엔진 · 도구 · 시험이 함께 씀), `HarukaPak`, `GamePack` 대상(`build/pack/game.hpak`, 에셋 · 셰이더 · 목록이 바뀌면 다시 만듦), 배포 ZIP 에 묶음과 라이선스 고지 |

엔진 쪽 연결:
- 마운트(`Engine::Engine`): 실행 폴더의 낱개 파일(우선순위 0) 위에 에디터는 프로젝트 폴더의 `assets/`(10), 게임은 실행 폴더의 묶음 파일
  (20 부터), `--loose` 면 실행 폴더의 낱개 파일을 맨 위에 다시 올립니다. 마운트 목록은 시작 로그에 남습니다. 개발 중 실행 폴더
  (`build/Release`)의 게임 실행 파일은 묶음이 없어 낱개 파일을 읽고, 배포 ZIP 에는 묶음만 들어갑니다. 2026-10-31 부터 프로젝트 폴더를
  쓰는 에디터는 실행 폴더에서 `shaders/` 만 찾고, `--loose` 는 모든 묶음보다 위(`hvfs::kLoosePriority`)이며 게임 실행 파일에만 듣습니다(5-2).
- 읽는 곳을 가상 파일 시스템으로 옮겼습니다: 셰이더(`readFile`, `HSlateRenderer::loadSpv`), 글꼴(`HFontAtlas`), `project.ini`, 게임의 시작
  씬, 프리팹 읽기, glTF(본문은 `LoadBinaryFromMemory` · `LoadASCIIFromString`, 외부 파일은 tinygltf 파일 콜백), 소리(miniaudio
  `pResourceManagerVFS` 콜백, 열 때 파일 전체를 메모리로). 에디터가 쓰는 쪽(씬 · 프리팹 저장, 콘텐츠 브라우저 목록)은 프로젝트 폴더의
  낱개 파일을 그대로 씁니다.
- glTF 캐시 v3: 파일 이름은 정규화한 논리 경로(대소문자 무시)의 FNV-1a 64비트, 원본 해시는 XXH3-64 입니다. 원본이 묶음 안이면 목차의
  크기 · 내용 해시로 판정하므로 파일을 읽지 않습니다. 낱개 파일은 예전처럼 수정 시각이 같으면 그대로, 다르면 내용 해시를 비교합니다.
  버전 2 이하 캐시는 한 번 다시 만듭니다.

결과(2026-10-24): 묶음은 27개 파일(24개 LZ4), 데이터 571,364 → 364,707 바이트, 묶음 377,144 바이트. `model.glb` 는 235,784 → 67,930
바이트, 셰이더는 대략 절반입니다. 같은 목록으로 다시 만들면 같은 바이트입니다.

확인:
- 시험 `pak_format` 56개(왕복 · 블록 경계 범위 읽기 · 손상 · 정렬 · 결정적 출력 · 경로 · 버전 · 우선순위 · 이름 규칙 · 8 스레드 동시 읽기 ·
  절대 경로), `asset_cache` 23개(논리 경로 키 · 묶음 안 원본), `audio_system` 에 묶음 안 WAV · OGG 디코딩 3개. 변이 14종을 모두 잡았습니다.
- 배포 ZIP 을 저장소 밖에 풀어 다른 작업 폴더에서 실행: 마운트 로그에 `game.hpak (27 files)`, 시작 씬과 `model.glb` 를 묶음에서 읽음,
  검증 메시지 0, 정상 종료. 두 번째 실행은 glTF 를 다시 읽지 않고 캐시를 씀(묶음 원본 판정). 에디터 Play · Stop 회귀 통과.

### 5-2. 마무리 묶음 (2026-10-31)

2026-10-31 오류 검토(워크플로, 결함마다 검증 두 표)가 1단계 코드에서 결함 8건을 찾았습니다. 사용자 결정("전부 권장대로, 패키징
마무리 묶음부터")으로 2단계 전에 고쳤습니다. 결함 번호는 결함 문서 끝 "패키징 마무리 묶음과 오류 검토 결과" 절입니다.

| 결함 | 고친 것 |
|---|---|
| DEF-121 `HarukaPak` 에 UTF-8 매니페스트가 없음 | 도구와 모든 시험 실행 파일에도 매니페스트를 넣고, 목차의 경로는 코드 페이지와 상관없이 UTF-8 로 바꿈(`src/vfs/PathUtf8.hpp`). 예상하지 못한 예외는 이유와 종료 코드 1 |
| DEF-122 조작한 목차의 정수 넘침 | 개수 × 크기를 나눗셈으로 확인, 블록 수 계산이 넘치지 않게, 블록마다 원문 크기 · 압축 크기 0 · LZ4 최대 배율(256)을 마운트 때 확인 |
| DEF-123 VFS 를 거치는 C 콜백에 예외 경계가 없음 | VFS 의 읽기 함수(`exists` · `stat` · `readAll` · `readText` · `contentHash` · `diskPath`)가 예외를 던지지 않음(올바르지 않은 UTF-8 은 "없음", 메모리 부족 같은 실패는 false), 소리 열기 콜백이 모든 예외를 오류 코드로 바꿈 |
| DEF-124 에디터가 실행 폴더의 옛 에셋 복사본을 읽음 | 프로젝트 폴더를 쓰는 에디터는 실행 폴더에서 `shaders/` 만 마운트 |
| DEF-125 glTF uri 의 `../` · `./` 를 찾지 못함 | 로더의 경로 콜백이 `.` · `..` 를 접고, 결과는 논리 경로 규칙을 통과해야 함(맨 위보다 위로 가는 경로 · 드라이브 문자 uri 거부). glTF 자체를 절대 경로로 열 때만 운영체제 경로로 찾음. glTF 캐시 v4(v3 은 텍스처를 놓쳤을 수 있어 한 번 다시 만듦) |
| DEF-126 `--loose` 가 큰 번호 패치에 가려지고 에디터에도 들음 | 우선순위를 `int` 최댓값으로, 에디터에서는 무시하고 경고 |
| DEF-127 숨김 폴더 아래 파일이 묶음에 들어감 | 목록에 `**/.*/**` 와 `Thumbs.db` · `desktop.ini` 를 더함 |
| DEF-128 OpenXR 로더에 든 jsoncpp 의 고지가 없음 | 라이선스 고지에 JsonCpp 를 더함. 같은 고지 블록이 Android 구성에서 없는 OpenXR 폴더의 MIT 원문을 읽어 구성이 멈추던 것(1단계부터)도 MIT 원문을 CMake 에 두어 고침(고지 파일은 바이트까지 같음) |

확인: 빌드 경고 0, 시험 19개 · 검사 807개 통과(새 시험 `gltf_external_files` 11개, `pak_format` 에 조작한 목차 · UTF-8 경로 · 실제 목록
파일의 제외 패턴 · `--loose` 우선순위 · 에디터 배치 20개, `asset_cache` 에 v3 캐시 1개). 임시 폴더를 한글 이름으로 바꿔도 모든 시험이
통과합니다(매니페스트를 넣기 전에는 오디오 시험이 23개 실패). 변이 15종(예전 넘침 검사 · 블록 수 공식 · 블록 검사 없음 · 배율 없음 ·
원문 크기 검사 없음 · 던지는 경로 변환 · `--loose` 1000 · 숨김 폴더 패턴 없음 · `utf8Of` 의 예외 처리 없음 · glTF 경로를 접지 않음 · `..` 를
거부하지 않고 버림 · `.` 를 남김 · 접은 결과를 검사하지 않음 · 절대 경로 glTF 도 논리 경로로 봄 · 드라이브 문자 입력을 통과시킴)을 모두
잡았습니다. 고친 뒤 독립 검토(영역 셋, 지적마다 반박 시도)가 4건을 더 찾아 같은 날 고쳤습니다: 매니페스트가 없는 시험 실행 파일 · `readText`
의 복사 할당 · Android 구성의 고지 블록 · `./C:/...` 후보가 절대 경로로 접히는 것. 실행 확인: `HarukaPak` 이 한글 · 이모지 이름을 UTF-8 로
묶고 풂, 에디터는 `--loose` 를 무시하고 `shaders/` 만 마운트, 새 배포 ZIP 은 묶음에서 읽고 캐시 v4 로 모델을 한 번 다시 읽음(검증 0).

## 6. 2단계 결과: Vulkan 1.1 기준선과 창 없는 렌더 경로 (2026-10-31)

사용자 결정(2026-10-31, "전부 권장대로 진행해"): 결정 18개를 모두 권장안으로 정했습니다. 주요한 것은 다음과 같습니다.
- 창 없는 GPU 실행은 묻지 않고 수시로 합니다.
- 개발 옵션은 배포 실행 파일에도 남깁니다.
- 배리어: sync2 가 있으면 KHR 함수를 쓰고, 없을 때만 1.0 배리어로 바꿉니다.
- XR 런타임 범위: 최솟값 미만이면 XR 을 끄고, 최댓값을 넘으면 경고만 합니다.
- 하는 일이 없던 깊이 전환은 지웁니다.
- 이방성이 없으면 끄고 계속합니다. 1GB 가 힙의 75% 를 넘으면 경고만 합니다.
- 합성 눈의 기본 크기는 512x512 입니다.

같은 날 사용자가 "가장 큰 목표는 가능한 빨리 스탠드얼론에 넣어 보는 것"이라고 정해, 2단계 뒤 3단계로 바로 넘어갔습니다.

| 순서 | 내용 | 결함 |
|---|---|---|
| 0 | 순수 헤더와 시험. `renderer/VulkanBaseline.hpp`(장치 계획 · 기능 체인 · 큐 계열 · 장치 함수 로드 · XR 범위), `renderer/BarrierCompat.hpp`(sync2 → 1.0 변환과 기록 분기), `memory/MemoryTypeSelect.hpp`(핵심 힙 · 지연 할당 타입 · 실패 문구), `xr/XrVersion.hpp`, `xr/SyntheticEyes.hpp` | |
| 1 | 창 없는 뼈대. `IWindow::providesSurface`, `platform/headless/HeadlessWindow.hpp`, 빈 스왑체인(`NullSwapchain`, 0장 · 명목 R8G8B8A8_SRGB 64x64), 표면 없이 장치 고르기(표시 큐 · 스왑체인 확장 없음), 합성 눈(`attachSyntheticEyes` · `captureXrEye`), 자동 실행 옵션과 종료 코드. AndroidWindow 는 표면 없이 0x0 · 클립보드 구현 | DEF-118 · 103 해결, DEF-08 의 표면 부분 |
| 2 | 배리어는 `RenderContext` 의 `transitionImage` · `pipelineBarrier` · `memoryBarrier` 로만(`hsync::record`). 기능 체인에서 Vulkan13Features 를 빼고 sync2 확장 구조체 하나만. 깊이 전환 삭제. UI 가 그리지 않은 프레임은 렌더러가 표시 전환 | DEF-04 · 120 해결 |
| 3 | 인스턴스 1.1(XR 경로는 런타임 최댓값까지 올림, 2026-11-02 DEF-164), push descriptor 6곳을 `rc.vk.push` 로(HSlate 는 init 인자), XR 런타임 범위 판정, glslc `--target-env=vulkan1.0`, 가져오기 표 시험 | DEF-97 해결 |
| 4 | 핵심 힙은 1GB 를 한 번에 잡고 못 잡으면 이유와 함께 멈춤(축소 경로 삭제), MSAA 컬러는 지연 할당 타입이 있으면 핵심 힙 밖(`AllocZone::Dedicated`), 이미지 생성 실패 때 누수 정리 | DEF-42 · 06 해결 |

**개발 옵션**(게임 실행 파일, 배포본에도 있음):

| 옵션 | 뜻 |
|---|---|
| `--headless` | OS 창 없이 돎(에디터는 무시하고 경고) |
| `--synthetic-eyes[=WxH]` | 헤드셋 없이 XR 프레임 경로로 두 눈을 그림(기본 512x512) |
| `--frames=N` · `--timeout=S` | N 프레임을 그리면 끝냄. 시간 안에 못 그리면 종료 코드 2(frames 만 주면 60초) |
| `--capture=path` | 끝낼 때 합성 눈 왼쪽 층을 PNG 로 남김 |
| `--vk-no-sync2` | sync2 가 있어도 1.0 배리어로 기록 |
| `--vk-dedicated-msaa` | MSAA 컬러를 핵심 힙 밖 별도 할당으로(지연 할당 경로를 PC 에서 시험) |
| `--ssao` · `--no-ssao` | 화면 공간 AO 를 켜거나 끔(AO 를 뺀 비교 · 끔 경로 시험). 프로젝트 설정 `render.ao` 를 덮어씀 |

종료 코드는 다음과 같습니다.

| 코드 | 뜻 |
|---|---|
| 0 | 성공 |
| 1 | FATAL |
| 2 | 시간 초과, 또는 정한 프레임 수를 못 그림 |
| 3 | 검증 오류(Debug 빌드만) |
| 4 | 저장한 화면이 비어 있음 |

점검 명령: `HarukaGame --headless --no-xr --synthetic-eyes --frames=120 --capture=out.png` (Debug).

확인:
- **빌드와 시험**: 경고 0, 시험 24개 · 검사 1,030개 통과. 새 시험은 `vulkan_baseline` · `barrier_compat` · `memory_types` · `headless_render` · `vulkan_imports` 이고, `stereo_views` 에 합성 눈 검사를 더했습니다. 헤더 변이 39종을 모두 잡았습니다.
- **창 없는 실행**: 기본 · `--vk-no-sync2` · `--vk-dedicated-msaa` 모두 검증 0, PNG 가 1단계 기준과 바이트까지 같습니다. Debug 두 번과 Release 도 같은 바이트입니다.
- **프로필 레이어(Quest Pro 형 1.1.295, 창 없이)**: 10-31 수정 전에는 첫 배리어에서 접근 위반이 났는데, 지금은 120프레임 · 검증 0 입니다. 이방성 최대치를 맞추면 PNG 도 같습니다(기준 프로필의 최대치는 1).
- **프로필 변형**: sync2 가 없으면 스위치 없이 1.0 배리어로 내려갑니다. push descriptor 가 없거나 장치가 1.0 이면 이유를 적은 FATAL 로 끝나고, 접근 위반은 없습니다.
- **실행 파일 가져오기 표**: vulkan-1.dll 에서 1.0 · 1.1 코어와 표면 · 스왑체인 함수만 가져옵니다.

새로 찾은 문제: 동기화 검증(`VK_LAYER_VALIDATE_SYNC=1`)이 메인 렌더 패스의 서브패스 의존성에서 WRITE_AFTER_WRITE 위험을 보고합니다(DEF-148, 8절). 두 배리어 경로에서 같고 이번 변경 전부터 있던 것입니다.

남은 것: 5번(창 · 헤드셋 확인, 사용자가 정한 때). 에디터 회귀(창 크기 · 스플리터 · Play/Stop · `--emulate-stereo` · 최소화), 창 모드 합성 눈, Quest Link 에서 1.1 인스턴스와 새 기능 체인.

## 7. 3단계 진행: Android(Quest) 빌드 (2026-10-31 ~)

조사(워크플로: 도구 · Quest OpenXR · 빌드 이식 · 런타임 이식, 종합과 비평)를 거쳐 다음과 같이 정했습니다. 사용자 결정은 "전부 권장대로"입니다.

| 항목 | 고른 안 | 이유 |
|---|---|---|
| 빌드 | **Gradle 없이** NDK + 기존 CMake/Ninja 로 `.so`, aapt2 · zipalign(`-P 16`) · apksigner 로 APK | `hasCode="false"` 라 Java 가 없음. 빌드 체계가 하나이고, 내려받기는 약 1GB(Gradle 은 4 ~ 5GB). Godot 의 내보내기 템플릿 · rawdrawandroid 와 같은 방식 |
| 도구 | NDK r30, build-tools 36.0.0, platform android-34, JDK 21(apksigner · keytool). adb 는 Meta Quest Developer Hub 의 것 | 공식 저장소에서 받아 해시를 확인함 |
| OpenXR 로더 | 데스크톱과 같은 OpenXR-SDK 1.1.63 소스를 공유 라이브러리로(`libopenxr_loader.so`) | hello_xr · Meta 예제와 같은 형태. 추가 내려받기 없음 |
| SDK 수준 | minSdk 32 / targetSdk 34 | Meta 의 현재 규칙 |
| 첫 실행의 에셋 | Android 빌드가 만든 `build/android/pack/game.hpak` 을 `adb push` 로 앱의 외부 파일 폴더(안 되면 OBB 폴더)에 넣음 | 기존 POSIX 묶음 읽기를 그대로 씀. APK 안 묶음은 다음 단계(AAsset fd + 시작 위치, aapt2 `-0 hpak`). 엔진이 묶음의 셰이더가 이 빌드의 것인지 확인함(7-1, DEF-149) |
| 소리 | 첫 실행은 끔(`noAudio`) | 변수를 줄임. 다음 단계에서 켬 |
| 패키지 이름 | `com.haruka.game` | |
| XR | Android 는 언제나 XR, 못 쓰면 이유와 함께 멈춤(`xrRequired`) | Quest 에는 데스크톱으로 계속할 화면이 없음. 연결이 잠시 끊기는 것만 다시 엶 |

**구현**:
- `platform/android/AndroidMain.cpp` 가 `android_main` 입니다.
  - 스레드를 JavaVM 에 붙이고, 폴더와 로그를 엽니다.
  - RESUME 과 창을 받을 때까지 이벤트를 처리합니다. UI 스레드가 기다리므로 ANR 을 막으려는 것입니다.
  - 그다음 Engine 을 실행하고, 끝나면 액티비티를 끝내 달라고 합니다. 시스템이 받았다는 첫 표시(PAUSE · STOP · DESTROY)까지만, 최대
    1초 이벤트를 처리한 뒤 `_exit` 합니다(DEF-154, 예전에는 DESTROY 까지 기다림). 같은 프로세스에 두 번째로 들어오면 바로 끝냅니다.
  - 개발 스위치는 `<외부 파일 폴더>/Config/args.txt` 에서 읽습니다. PC 에서 만든 BOM 이 있는 UTF-8 · UTF-16 파일도 읽습니다(DEF-152).
- `AndroidWindow` 는 android_app 의 루퍼를 비우고, destroyRequested 면 닫힙니다.
- `PlatformContext`(android_app · JavaVM · 액티비티)를 `EngineOptions` 로 넘깁니다.
- `XrSystem::init` 은 Android 에서 `xrInitializeLoaderKHR` 를 먼저 부르고, `XR_KHR_android_create_instance` 를 켭니다.
- `AppPaths` 의 `setPlatformDirs` · `packDirs` 가 실행 파일 경로(`app_process64`) 대신 외부 파일 폴더를 씁니다.
- CMake 의 Android 갈래는 `cmake/HarukaAndroid.cmake` 로 옮겼습니다.
  - `android_native_app_glue.c` 를 오브젝트로 직접 넣어 `ANativeActivity_onCreate` 가 남게 합니다.
  - Jolt 의 LTO 를 끕니다.
  - Android 에서는 sysroot 를 `-I` 로 다시 넣지 않습니다.
- APK 는 `Packaging/android/AndroidManifest.xml` · `package_apk.py` 로 만듭니다. 디버그 키스토어는 처음 쓸 때 만듭니다.

**구성 · 빌드 명령**(NDK 경로는 자리표시). 묶음과 셰이더 목록은 데스크톱 빌드의 HarukaPak 으로 만들므로 그것을 먼저 빌드합니다(없으면
구성이 멈추고 방법을 알림, 다른 위치의 도구는 `-DHARUKA_HOST_PAK=<경로>`). `--target HarukaGame` 이 묶음(`build/android/pack/game.hpak`)도
새로 만들고, `package_apk.py` 가 끝에 그 위치와 `adb push` 명령을 알려 줍니다.
```
cmake --build build --config Release --target HarukaPak
cmake -S . -B build/android -G Ninja -DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-32 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/android --target HarukaGame
python -I Packaging/android/package_apk.py        (ANDROID_HOME · JAVA_HOME 필요)
```

**확인(PC)**:
- `libHarukaGame.so` 가 `--no-undefined` 로 링크됩니다(API 32 의 libvulkan).
  - NEEDED 는 libvulkan · libandroid · liblog · libopenxr_loader · libm · libdl · libc 입니다.
  - `ANativeActivity_onCreate` 를 내보냅니다.
  - 미해결 Vulkan 기호에 1.2 이상 함수가 없습니다. Features2 · Properties2 는 API 28 부터 있는 1.1 코어입니다.
  - LOAD 정렬은 16KB 입니다.
- APK 는 5.6MB 입니다(strip 한 엔진 5.0MB · 로더 0.6MB, 2026-11-01 고지 파일을 넣은 뒤 5.7MB). zipalign `-P 16` 검사와 apksigner 검증을 통과했고, `aapt2 dump badging` 이 manifest 대로입니다.
- 이 과정에서 처음 컴파일된 Android 갈래의 기존 오류 1건(`AppPaths.cpp` 의 변수 재정의)을 고쳤습니다.
- clang `-Wall -Wextra` 경고는 기존 코드의 같은 유형입니다(로그 매크로의 dangling-else, XR · Vulkan 구조체의 빈 칸 초기화, 쓰지 않는 인자). 첫 실행 뒤에 정리합니다(결정).

**남은 것(헤드셋)**:
1. 헤드셋 준비: 개발자 모드를 켜고(Meta Horizon 앱, 개발자 조직 · 계정 인증), USB 디버깅을 "항상" 허용합니다.
2. 첫 실행: 설치 → 한 번 실행해 앱 폴더를 만들고 → 묶음을 넣고 → 다시 실행합니다. 로그는 logcat(태그 `Haruka`)과 `Logs/HarukaGame.log` 입니다.
3. 수명주기: 헤드셋을 벗고 쓰기, Meta 버튼, 대시보드 종료, 다시 실행.
4. 그 뒤: 소리 켜기, APK 안 묶음 읽기, Android 경고 정리, 성능(멀티뷰 · MSAA · 렌더 배율). APK 의 라이선스 고지는 넣었습니다(7-1).

### 7-1. Android 오류 검출과 수정 (2026-11-01)

사용자 지시("안드로이드 계열 에러 검출, 특히 패키징 관점")로 워크플로(영역 4갈래 + 반박 검증)를 돌려 실제 결함 10건(겹치는 것을 합쳐 8가지)을
찾았고, 같은 날 모두 고쳤습니다. 확인 중에 Windows 쪽 기존 결함 1건을 더 찾아 고쳤습니다. 상세는
[Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 끝 "Android 오류 검출과 수정" 절입니다.

| 결함 | 고친 것 |
|---|---|
| DEF-149 묶음과 실행 파일의 짝을 확인하지 않음, Android 빌드가 묶음을 만들지 않음, 외부 파일 폴더의 옛 묶음이 OBB 의 새 묶음을 가림 | 빌드가 셰이더 목록(경로 · 내용 해시)을 엔진에 넣고, 게임 실행 파일이 마운트 뒤 비교해 다르면 FATAL. Android 구성이 자기 셰이더로 묶음을 만듦. 마운트 로그에 전체 경로, 쓰지 않는 묶음 폴더는 경고 |
| DEF-150 `configChanges` 에 없는 설정 변경이면 액티비티를 다시 만들어 게임이 꺼짐 | API 34 의 18개 항목 전부 |
| DEF-151 APK 에 고지가 없고 jnipp · android_native_app_glue 가 고지 목록에 없음 | 둘을 더하고 `assets/THIRD_PARTY_NOTICES.txt` 로 넣음 |
| DEF-152 PowerShell 이 만든 설정 파일(BOM · UTF-16)에서 옵션이 사라짐 | `hfile::readConfigText` |
| DEF-153 파이프라인 캐시를 원자적이지 않게 쓰고 검사 없이 드라이버에 넘김 | 자체 머리로 감싸 원자적으로 쓰고, 읽을 때 크기 · 해시 · 장치를 확인. 새 파이프라인 뒤 첫 프레임에도 저장 |
| DEF-154 종료 직후 다시 실행하면 새 실행이 함께 죽음 | DESTROY 를 기다리지 않음(첫 수명주기 표시까지, 최대 1초) |
| DEF-155 XR 을 되살릴 수 없으면 Quest 앱이 빈 화면인 채 끝나지 않음 | 잃은 세션 판정을 넓히고, XR 이 필수이면 끝냄(연결 끊김은 30초) |
| DEF-156 SSAO 가 선형 필터링이 선택인 깊이 포맷을 LINEAR 로 읽음 | AO 전용 NEAREST 샘플러. 창 없는 실행 PNG 기준이 `884daefd…` 로 바뀜 |
| DEF-157 (Windows) Jolt 의 `_HAS_EXCEPTIONS=0` 이 엔진으로 새어 문자열로 만든 FATAL 문구가 깨짐 | Jolt 의 `CPP_EXCEPTIONS_ENABLED` 를 켜고, 다시 새면 컴파일을 멈춤 |

확인: 데스크톱 경고 0, 시험 24개 · 검사 1,061개. 창 없는 실행은 DEF-156 전까지 옛 기준과 바이트까지 같았고, 뒤에는 모든 경로(기본 · 1.0
배리어 · 별도 할당 · Release · Quest Pro 형 프로필의 포맷 흉내)에서 새 기준이 같습니다. 짝이 다른 묶음은 셰이더 이름을 적은 FATAL 로 멈춥니다.
Android 빌드가 만든 셰이더 목록 · 묶음은 데스크톱 것과 바이트까지 같고, APK 는 aapt2 · zipalign · apksigner 를 통과합니다. 수명주기 ·
XR 손실 · 드라이버 캐시의 기기 동작은 헤드셋 첫 실행 때 확인합니다. 고친 뒤의 독립 검토가 6건(XR 다시 열기 상한, UTF-32 파일, 셰이더를
목록에서 뺐을 때의 목록 파일, 묶음이 없을 때의 안내, 낡은 호스트 HarukaPak, OpenXR 고지의 Collabora)을 더 찾아 같은 날 고쳤고, 그 수정의
재검토에서 나온 4건(복구 판정은 5초 이어서 돎, 마운트 실패의 안내, 도구 확인 안내의 구성, 확인 대상에서 PakReader 뺌)도 고쳤습니다.
Android 빌드는 묶음 형식 · 쓰기 규칙의 소스가 데스크톱 HarukaPak 보다 새로우면 멈추므로, 그 소스를 바꿨으면 데스크톱 HarukaPak 을 먼저
다시 빌드합니다.

## 8. 남은 일과 찾은 문제

결함 번호는 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 끝 "패키징 1단계 결과" 절에 등재했습니다.

| 문제 | 상태 |
|---|---|
| DEF-112 디버그 선 목록을 에디터만 비움(에디터가 없거나 최소화하면 끝없이 쌓임) | 해결(4-1) |
| DEF-113 UI 가 없으면 스왑체인 이미지를 표시 레이아웃으로 바꾸지 않음(검증 오류) | 해결(4-5) |
| DEF-114 셰이더 · 에셋을 작업 폴더 기준으로 찾음(바로가기 · 다른 폴더에서 실행하면 실패) | 해결(4-3) |
| DEF-115 로그 파일을 못 열어도 아무 표시 없음 | 해결(4-3) |
| DEF-116 한글 사용자 이름 경로가 시스템 코드 페이지로 해석됨 | 해결(4-3, UTF-8 매니페스트) |
| DEF-117 glTF 캐시 키가 실제 경로의 `std::hash`(실행 · 컴파일러마다 달라질 수 있어 미리 만들어 넣을 수 없음) | 해결(5-1, 캐시 v3) |
| DEF-118 데스크톱 스왑체인 이미지가 0장이면 `renderDoneSem(imgIdx)` 가 범위 밖(`Renderer_render.cpp`) | 해결(6절) |
| DEF-119 Android 에 쓸 글꼴이 없음(`assets/fonts/default.ttf` 가 없어 Windows 의 Segoe UI 를 씀) | 남음. 게임 UI 를 만들 때 라이선스를 확인한 글꼴을 넣음 |
| DEF-120 UI 렌더러가 그릴 것이 없어 일찍 돌아가면 스왑체인 이미지를 표시 레이아웃으로 바꾸지 않음(DEF-113 과 같은 계열) | 해결(6절) |
| DEF-121 ~ 128 마무리 묶음(UTF-8 도구 · 조작한 목차 · C 콜백 예외 · 에디터의 옛 복사본 · glTF `../` · `--loose` · 숨김 폴더 · jsoncpp 고지) | 해결(5-2) |
| DEF-129 헤드셋 재연결 때 새 시스템 ID 로 그래픽 요구사항을 다시 묻지 않음 | 남음. `--xr` 게임 실행 확인(헤드셋)과 함께 |
| 패키지 기본 `Config/engine.ini` 를 ZIP 에 넣지 않음(엔진 기본값을 씀) | 필요할 때 넣음 |
| 옛 캐시 파일(버전 2, 실제 경로 키)이 에디터 실행 폴더의 `assets/cache` 에 남음 | 쓰이지 않을 뿐 해는 없음. 빌드 폴더를 지우면 사라짐 |
| 게임 설정 화면(창 크기 · 전체 화면 · 소리 장치) | 없음. 게임 UI 를 만들 때 |
| Android 의 묶음 읽기(AAsset fd + 시작 위치) | 3단계 첫 실행 뒤. 첫 실행은 `adb push`(7절). 읽는 바탕(`ReadSource`)만 더하면 됨 |
| DEF-148 메인 렌더 패스의 서브패스 1 → 2 의존성에 깊이 단계 · 쓰기 접근이 없어, 동기화 검증이 깊이 저장(STORE)과 레이아웃 전환 사이의 WRITE_AFTER_WRITE 를 보고 | 남음. 2단계 확인 중 발견(기존 결함), Quest 첫 실행은 막지 않음 |
| DEF-149 ~ 157 Android 오류 검출(묶음 짝 확인 · configChanges · 고지 · 설정 파일 인코딩 · 파이프라인 캐시 · 종료 순서 · XR 손실 · SSAO 깊이 샘플러)과 Windows 의 `_HAS_EXCEPTIONS` | 해결(7-1). 기기 동작은 헤드셋 첫 실행 때 확인 |
