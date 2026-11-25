# 엔진 설치 · 프로젝트 생성 (2026-11-23 ~)

> 상태: **설계 초안**. 다른 엔진 조사(9절)를 넣었고, 조사로 바꾼 점은 9절 끝에 있습니다(5.4 엔진 식별 · 찾기는 9절이 우선). 결정은 8절의 사용자 답을 받은 뒤 확정합니다.
> 앞선 결정: [Engine_Structure_Design.md](Engine_Structure_Design.md) 14절(엔진 · 프로젝트 분리 P1 ~ P4, D3 = 프로젝트별 정적 링크 실행 파일).

## 1. 목표

사용자 요청(2026-11-23): UE 5 소스 빌드 엔진처럼, 엔진 폴더 안의 배치 파일이나 실행 파일로 사용자가 지정한 경로에 프로젝트를 구성합니다.

- 엔진은 **엔진 폴더에서 한 번 빌드**합니다. 프로젝트를 만들 때마다 엔진을 다시 컴파일하지 않습니다.
- 프로젝트는 엔진 폴더 밖 어디에나 둘 수 있고, 프로젝트는 엔진을 **참조**만 합니다(복사하지 않음).
- 프로젝트 빌드는 자기 게임 코드만 컴파일하고 엔진 라이브러리에 정적 링크해 에디터 · 게임 실행 파일을 만듭니다.

## 2. 지금 상태 (2026-11-23 조사)

| 항목 | 지금 |
|---|---|
| 엔진 라이브러리 | `HarukaRuntime`(정적) · `HarukaVfs`(정적)만 라이브러리. 에디터 코드(`HARUKA_EDITOR_SOURCES`) · 게임 앞단(`HARUKA_GAME_SOURCES`) · `src/main.cpp`는 프로젝트마다 실행 파일에 직접 컴파일 |
| 프로젝트 대상 | `cmake/HarukaProject.cmake`의 `haruka_add_project`가 에디터 · 게임 실행 파일, 셰이더 · 콘텐츠 복사, 묶음(.hpak) 대상을 만듦 |
| 바깥 프로젝트 | `src/tools/new_project.py <폴더> <이름>`이 폴더 · `.hproject` · `CMakeLists.txt`를 만들고, 그 CMake가 `-DHARUKA_ENGINE_DIR=<경로>`로 받은 엔진을 `add_subdirectory`로 넣음 → **프로젝트마다 엔진 전체를 컴파일** |
| 엔진 위치 전달 | 빌드 때 실행 파일에 절대 경로를 넣음(`HARUKA_PROJECT_DIR` · `HARUKA_ENGINE_CONTENT_DIR`). 실행 때 그 폴더가 없으면 실행 폴더 옆 복사본으로 **경고 없이** 넘어감(`Engine.cpp` 경로 해석) |
| 외부 라이브러리 | FetchContent 약 10개(glm · tinygltf · stb · miniaudio · lz4 · xxhash · Jolt · meshoptimizer · nanosvg · recast · glfw · openxr, Android는 astcenc · bc7enc). `HarukaRuntime`이 Vulkan · glfw · glm · tinygltf · Jolt · openxr_loader · HarukaVfs · meshoptimizer를 PUBLIC으로 링크 |
| 폴더 범위 설정 | `add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)`, `/wd4100` 등이 엔진 폴더 범위라 바깥 대상에 전달되지 않음(GLM 정의가 어긋나면 DEF-95와 같은 깊이 규약 결함) |
| 도구 | 셰이더 = Vulkan SDK `glslc`(엔진 빌드가 컴파일), 묶음 도구 `HarukaPak`(호스트 실행 파일) |
| `.hproject` | `name` · `company` · `packageId` · `engineVersion` 네 키(`src/core/ProjectDescriptor.hpp` · CMake 읽기 함수가 같은 규칙) |

## 3. 결정

| # | 항목 | 결정 |
|---|---|---|
| I1 | 엔진 컴파일 위치 | **B안(2026-11-23 사용자 확정)**: 엔진 폴더에서 엔진을 정적 라이브러리로 한 번 빌드하고, 프로젝트는 CMake 패키지로 찾아 링크. 프로젝트별 정적 링크 실행 파일(앞선 D3) 유지. 버린 안: A(프로젝트마다 엔진 컴파일 = 지금), C(공유 에디터 + 게임 모듈 DLL, D3를 뒤집음) |
| I2 | Python | **엔진에 동봉(11-23 사용자 확정)**. AI 코딩 · MCP 같은 에디터 기반 검색 · 조정에 씁니다. 판은 5.8 조사 뒤 확정(권장 3.15.2) |
| I3 | 프로젝트 도구 형태 | **GUI 실행 파일(11-23 사용자 확정)**: 아이콘이 있는 `HarukaLauncher.exe`, UE 프로젝트 브라우저처럼 창에서 템플릿 · 경로 · 이름을 고릅니다. 배치 파일은 처음 빌드용 `Setup.bat` 하나만 남깁니다(5.5) |
| I4 | Vulkan 빌드 의존 | **엔진 안에 넣는 방향(11-23 사용자 희망)**. 헤더 · 함수 로더 · 셰이더 컴파일러를 엔진이 고정 판으로 가집니다. 세부안은 5.9(사용자 답 대기) |
| — | 8절 2 ~ 4 | 권장대로(출력 `<프로젝트>/Build`, `Engines.ini`, Blank 템플릿 하나) |

## 4. 목표 모습

```
<엔진 폴더>/                     저장소 그대로(src · shaders · Engine/Content · cmake · tests · Docs)
  Setup.bat                      처음 한 번: 필요 도구 확인 → Python 받기 → 엔진 구성 · 빌드(Debug · Release) → 엔진 등록
                                 → 엔진 폴더에 HarukaLauncher 바로 가기 생성(유일한 배치 파일)
  Build/                         엔진 빌드 트리(git 제외): 정적 라이브러리, 셰이더 SPIR-V, 도구(HarukaPak · HarukaLauncher),
                                 CMake 패키지(HarukaConfig.cmake · 대상 파일)
  ThirdParty/Python/             동봉 Python(git 제외, Setup.bat이 판 · 해시를 고정해 받음)
<사용자가 고른 경로>/MyGame/
  MyGame.hproject                이름 · 회사 · 패키지 id · 엔진 연결(engineAssociation) · 엔진 최소 버전
  Content/  Config/  Source/     지금과 같음
  CMakeLists.txt                 find_package(Haruka) + haruka_add_project(...)
  Build/                         프로젝트 빌드 트리 · VS 솔루션(git 제외)
  Saved/                         에디터 상태 · 로그 · 캐시(git 제외)
```

사용자 흐름

1. 엔진 저장소를 받고 `Setup.bat`을 한 번 실행합니다(필요 도구 확인 → 엔진 빌드 → 엔진 등록).
2. `HarukaLauncher`를 실행해 템플릿 · 경로 · 이름을 고릅니다. 프로젝트 폴더와 VS 솔루션이 생깁니다.
3. 솔루션을 열고 빌드 · 실행합니다. 엔진 코드는 다시 컴파일하지 않고, 게임 코드와 실행 파일 링크만 합니다.
4. 엔진을 고치면 엔진 폴더에서 다시 빌드하고, 프로젝트는 다음 빌드 때 새 라이브러리를 링크합니다.

## 5. 설계 항목 (초안, 권장안 표시)

### 5.1 엔진 라이브러리 나누기
- 지금 실행 파일에 직접 들어가는 코드를 라이브러리로 뗍니다: `HarukaEditor`(에디터 코드, 정적) · `HarukaGameFrontend`(게임 앞단, 정적). `main.cpp`는 짧으므로 프로젝트 쪽에서 컴파일하거나 작은 정적 라이브러리로 둡니다.
- 에디터 쪽이 실행 파일마다 받던 정의(`HARUKA_EDITOR`, 프로젝트 이름 · 회사 · 패키지 id)는 컴파일 정의 대신 실행 때 값(프로젝트 설명 파일을 읽은 값)으로 바꿉니다. 라이브러리는 프로젝트를 몰라야 하기 때문입니다.
- 정적 라이브러리 안의 등록 코드(정적 초기화로 자기 등록하는 것)가 링커에 버려지지 않는지 확인합니다. 있으면 명시 등록 함수로 바꿉니다.
- **E1a 결과 (2026-11-23)**: `HarukaEditor`(에디터 32개 소스) · `HarukaGameFrontend`(`src/game/GameFrontend.cpp`) 정적 라이브러리를 분리하고, `haruka_add_project`가 `main.cpp`/`AndroidMain.cpp` · 프로젝트 소스 · 리소스만 컴파일해 링크하도록 바꾸었습니다(Android `libHarukaGame.so` 포함). `HARUKA_PROJECT_DIR` · `HARUKA_ENGINE_CONTENT_DIR`은 실행 파일(`main.cpp`)에만 남기고 `EngineFrontendConfig` 구조체로 `createEngineFrontend()`에 전달합니다. `ComponentRegistry` · `EditorCommandRegistry` 모두 명시 등록만 쓰며 정적 초기화 자기 등록은 없음을 확인했습니다.
- **E1b 결과 (2026-11-23)**: `HarukaRuntime`에서 시뮬레이션 모듈(`core` · `scene` · `geometry` · `vfx` · `physics` · `audio` · `nav`, `math` 헤더)을 `HarukaSim` 정적 라이브러리로 분리했습니다(데스크톱 · Android 공통). `HarukaSim`은 `haruka_vulkan` · `glfw` · `openxr_loader`를 링크하지 않고 `HarukaRuntime`이 `HarukaSim`을 `PUBLIC`으로 링크합니다. 패키지 내보내기(E2) 대상 목록에 `HarukaSim`이 추가됩니다.

### 5.2 사용 요구 사항(usage requirements) 정리
- 폴더 범위 설정을 대상 속성으로 옮깁니다: `GLM_FORCE_DEPTH_ZERO_TO_ONE`은 `HarukaRuntime`의 `INTERFACE`(PUBLIC) 정의로, 경고 설정은 `haruka_target_defaults`로. 그래야 프로젝트 코드도 같은 GLM 규약으로 컴파일됩니다.
- MSVC 런타임(/MD · /MDd)과 디버그 반복자 수준은 엔진과 프로젝트가 같아야 합니다. 엔진 패키지가 구성별 라이브러리를 둘 다 내보내고, 프로젝트는 같은 구성 이름으로 고르게 합니다.
- 컴파일러가 달라도 되는지: 정적 C++ 라이브러리라 **같은 MSVC 도구 집합**을 요구합니다. 패키지에 엔진을 빌드한 컴파일러 버전을 적고, 다르면 CMake 구성 단계에서 오류로 알립니다.
- **E1a 결과 (2026-11-23)**: 폴더 범위 `add_compile_options` · `add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)`를 제거하고 `GLM_FORCE_DEPTH_ZERO_TO_ONE`을 `HarukaRuntime`의 `PUBLIC` 정의로 옮겼습니다(독립 시험과 `HarukaPak`에는 개별 `PRIVATE` 부여). MSVC 런타임(/MD · /MDd)과 `_ITERATOR_DEBUG_LEVEL`은 엔진·프로젝트 모두 CMake 기본값을 공유함을 확인했습니다.

### 5.3 엔진 패키지 내보내기
- 후보 A(권장): **빌드 트리 내보내기**(`export(TARGETS …)` · `export(EXPORT …)`) + `HarukaConfig.cmake`. 소스 빌드 엔진이므로 설치 단계 없이 `<엔진>/Build`를 바로 씁니다. UE 소스 빌드가 엔진 폴더의 바이너리를 바로 쓰는 것과 같은 모양입니다.
- 후보 B: `install(EXPORT)`로 별도 설치 트리를 만듭니다. 엔진 바이너리 배포(설치판)를 할 때 필요하지만 지금 목표에는 과합니다. 나중 단계로 둡니다.
- 외부 라이브러리: PUBLIC으로 링크하는 것(glm · Vulkan · glfw · Jolt · openxr · tinygltf · meshoptimizer)은 대상이 함께 내보내져야 합니다. 프로젝트 코드가 직접 쓰지 않는 것은 PRIVATE으로 내려 내보낼 목록을 줄입니다. 헤더에 노출되는 것(glm · Vulkan · Jolt 일부)은 엔진 공개 헤더 점검이 필요합니다. 구체 방법은 조사(9절) 뒤 정합니다.
- 셰이더 SPIR-V · `Engine/Content`는 엔진 폴더에 그대로 두고, 패키지가 그 경로를 변수로 알려 줍니다. `haruka_add_project`는 그 경로에서 실행 폴더로 복사합니다(지금과 같은 동작).
- **E2 결과 (2026-11-23)**:
  - **방식**: 빌드 트리 내보내기(`export(TARGETS … NAMESPACE Haruka:: FILE HarukaTargets.cmake)`) + `HarukaConfig.cmake` · `HarukaConfigVersion.cmake`를 엔진 빌드 폴더에 생성합니다. FetchContent 의존(`glm` · `meshoptimizer` · `Jolt` · `glfw` · `tinygltf` · `openxr_loader` 등)이 `$<BUILD_INTERFACE:...>`를 쓰므로 별도 설치 트리 없이 헤더 · `.lib` 경로가 그대로 전달됩니다.
  - **내보낸 대상**: 엔진 대상 `Haruka::HarukaVfs` · `Haruka::HarukaSim` · `Haruka::HarukaRuntime` · `Haruka::HarukaGameFrontend` · `Haruka::HarukaEditor` · `Haruka::HarukaPak`(실행 파일) 및 내부/FetchContent 의존(`haruka_vulkan` · `haruka_lz4` · `haruka_recast` · `haruka_nanosvg` · `glm` · `glm-header-only` · `meshoptimizer` · `Jolt` · `glfw` · `tinygltf` · `openxr_loader`, `OpenXR::headers` · `Threads::Threads`). `haruka_add_project`는 패키지 모드에서 `Haruka::HarukaEditor` · `Haruka::HarukaGameFrontend` · `Haruka::HarukaPak`과 엔진이 이미 컴파일한 `.spv`를 사용합니다.
  - **어긋남 검사**: `HarukaConfig.cmake`에 컴파일러 ID · `MSVC_TOOLSET_VERSION` · 지원 구성(`Debug;Release`) · `MSVC_RUNTIME_LIBRARY`(`/MD` · `/MDd`) · `_ITERATOR_DEBUG_LEVEL`(Debug 2 · Release 0)을 기록하고, 프로젝트 구성 단계에서 다르면 `FATAL_ERROR`로 중단합니다. `RelWithDebInfo` · `MinSizeRel`은 `MAP_IMPORTED_CONFIG_*`로 `RELEASE`에 대응시킵니다.

### 5.4 엔진 등록과 프로젝트의 엔진 연결
- 엔진 목록 파일: `%LOCALAPPDATA%\Haruka\Engines.ini`(키 = 엔진 id, 값 = 엔진 폴더 경로). `Setup.bat`이 등록하고, 엔진 폴더를 옮기면 `Setup.bat`을 다시 실행해 경로를 갱신합니다. 레지스트리 대신 파일을 쓰는 이유는 손으로 보고 고치기 쉽고 OS 의존이 적기 때문입니다(조사 뒤 다시 봄).
- 엔진 id: 처음 등록할 때 만든 GUID를 엔진 폴더의 `Build/EngineId.txt`(git 제외)에 남깁니다. 같은 PC에 엔진을 여러 벌 둘 수 있습니다.
- `.hproject`에 `engineAssociation = <id>`를 더합니다. 비어 있으면 프로젝트 폴더의 상위 폴더를 거슬러 올라가 엔진을 찾습니다(저장소 안 `Projects/Samples`가 이 경우).
- CMake 쪽: 프로젝트 `CMakeLists.txt`는 엔진 경로를 직접 적지 않습니다. 작은 찾기 스크립트(`.hproject` → 목록 파일 → `Haruka_DIR`)를 거칩니다. 솔루션을 만드는 도구가 찾은 경로를 `-DHaruka_DIR`로 넘기므로 사용자는 경로를 손으로 넣지 않습니다.
- **E3 결과 (2026-11-23)**:
  - **엔진 식별 · 목록 파일**: 9절 결정대로 이름(`haruka`) + 버전(`project(HarukaEngine VERSION 0.1.0)`)을 사용하며, `Tools/Setup/engine_registry.py`가 `%LOCALAPPDATA%\Haruka\Engines.ini` (`HARUKA_ENGINES_INI` · `--engines-ini` 덮어쓰기 지원)에 `[engine.<n>]` 섹션(`name`, `version`, `engineDir`, `buildDir`)으로 멱등 등록 및 이동/삭제된 옛 항목 정리를 수행합니다.
  - **프로젝트 연결 · 기계별 덮어쓰기**: `ProjectDescriptor.hpp`와 `cmake/HarukaProject.cmake`가 `.hproject`의 `engine = haruka>=0.1` 키를 지원하며(`Samples.hproject` · `ExternalTest.hproject` 반영), 프로젝트 루트의 `Haruka.user.ini`(git 제외)로 기계별 경로 덮어쓰기를 지원합니다.
  - **찾기 스크립트**: `cmake/HarukaEngineFinder.cmake`가 ① 명시 `-DHaruka_DIR` → ② `Haruka.user.ini` → ③ 상위 폴더 엔진 소스 트리 → ④ `Engines.ini`에서 조건을 만족하는 최고 판 순서로 `Haruka_DIR`을 결정합니다. `tests/ExternalProject/CMakeLists.txt`가 이를 사용하도록 전환했습니다.

### 5.5 프로젝트 도구 (`HarukaLauncher.exe`, I3)
- 왜 에디터 안이 아니라 따로인가: UE는 프로젝트 브라우저가 에디터 실행 파일 안에 있지만, B안의 에디터는 프로젝트마다 빌드되므로 프로젝트가 없으면 에디터도 없습니다. 그래서 O3DE Project Manager · Unity Hub처럼 엔진 폴더에 별도 실행 파일을 둡니다.
- 화면(HSlate, 에디터와 같은 모양 · 글꼴 · 한국어)
  - 최근 프로젝트: 목록(이름 · 경로 · 엔진 판 · 마지막으로 연 시각), 열기 · 폴더 열기 · 목록에서 빼기.
  - 새 프로젝트: 템플릿 카드(Blank부터, 미리보기 그림 · 설명) → 위치(폴더 고르기 창) · 이름 · 회사 · 패키지 id(이름에서 자동 채움) → 만들기.
  - 진행: 단계 표시(복사 → 솔루션 생성 → 선택 시 빌드)와 CMake 출력 로그. 끝나면 "VS에서 열기" · "빌드 후 에디터 열기".
- 열기 동작: 프로젝트의 에디터 실행 파일이 이미 있으면 바로 실행하고, 없거나 낡았으면 빌드부터 제안합니다(`cmake --build`, VS 위치는 vswhere로 찾음).
- 같은 실행 파일의 명령줄 모드(창 없음): `new` · `generate` · `register` · `open`. 시험 · 자동화 · AI(MCP)가 이 길을 씁니다. 로직은 C++ 라이브러리 `HarukaProjectOps` 하나에 두고 창과 명령줄이 함께 씁니다. 지금 `new_project.py`는 이 라이브러리로 옮긴 뒤 지웁니다.
  - 다른 안: O3DE처럼 로직을 Python 스크립트(`o3de.py`)로 두고 창이 그것을 부르는 방식. Python을 동봉하므로 가능하지만, 핵심 경로가 두 언어로 갈라지고 ctest로 지키기 어려워 C++을 권합니다. Python은 명령줄 모드를 감싸 씁니다.
- 아이콘: 엔진 아이콘 `.ico`(SVG 원본 → 16 · 24 · 32 · 48 · 256 크기)를 `.rc` 리소스로 넣습니다. 같은 아이콘을 에디터 실행 파일에도 씁니다. 프로젝트 템플릿 아이콘은 따로 둡니다.
- 파일 연결(Setup.bat 선택 사항, 사용자 범위 HKCU): `.hproject` 더블 클릭 = 런처가 엔진을 찾아 열기, 오른쪽 메뉴 "솔루션 다시 만들기"(UE의 Generate Visual Studio project files와 같은 역할).
- 템플릿: 엔진 폴더의 `Templates/Blank`(빈 게임 모듈 · 빈 씬 하나)부터 시작합니다. 각 템플릿 폴더에 `template.ini`(이름 · 설명 · 미리보기 · 치환 규칙)를 둡니다.
- **E4 결과 (2026-11-23)**:
  - `src/projectops/`(`HarukaProjectOps` 정적 라이브러리, `core` · `vfs`만 의존)와 `src/launcher/`(`HarukaLauncher.exe` 명령줄 모드, `res/Haruka.rc` · `Haruka.manifest` 재사용)를 추가했습니다.
  - `Templates/Blank` 템플릿(`template.ini`, `{{NAME}}.hproject`, `CMakeLists.txt`, `.gitignore`, `Config/project.ini`, `Content/scenes/Start.hscene`, `Source/{{NAME}}GameModule.cpp`)을 만들고 `new` · `generate` · `register` · `open` · `list` 명령줄을 구현했습니다(`--user-dir=` · `HARUKA_LAUNCHER_USER_DIR` · `--dry-run` 지원, UTF-8 ↔ Windows 넓은 문자 경로).
  - 기존 `src/tools/new_project.py`를 제거하고 `HarukaProjectOpsTest`(`project_ops`) 단위 시험을 추가했습니다.
- **E6 결과 (2026-11-23)**:
  - `HarukaLauncher.exe`에 경량 HSlate + Vulkan 호스트(`LauncherHost`)와 `LauncherApp`(최근 프로젝트 · 새 프로젝트 · 진행 3화면, 비동기 CMake/빌드 로그 스트리밍, 한국어 `HTEXT` 31키)을 구현했습니다. 명령줄 모드(`new` · `generate` · `register` · `open` · `list` · `associate` · `unassociate` · `<file.hproject>`)는 GLFW · Vulkan을 초기화하지 않고(`/SUBSYSTEM:WINDOWS` + 부모 콘솔 연결) 동작합니다.
  - `HarukaProjectOps`에 `.hproject` 사용자 범위(`HKCU\Software\Classes`) 파일 연결(`associate` · `unassociate`, `--dry-run` 지원), 최근 목록 제거(`removeRecentProject`), `buildProjectFiles`, `findProjectSolution`, `Setup.bat --associate` 연동을 추가했습니다.
  - 실행 파일 아이콘(`res/Haruka.rc` → `Haruka.ico`)이 `HarukaLauncher` · `HarukaEngine` · `HarukaGame` 및 외부 프로젝트 실행 파일에 모두 포함됨을 확인했습니다. (템플릿 별도 아이콘 및 시작 메뉴 바로 가기는 E6 판단대로 이번 단계에서는 추가하지 않음.)

### 5.6 실행 때 경로 찾기
- 에디터 실행 파일은 프로젝트 폴더 경로만 압니다(빌드 때 넣거나 `--project`). 엔진 콘텐츠(`/Engine/`) · 셰이더는 프로젝트의 엔진 연결(5.4)로 엔진 폴더를 찾아 읽습니다. 빌드 때 엔진 절대 경로를 넣지 않습니다.
- 에디터에서는 "경로가 없으면 실행 폴더 복사본으로 경고 없이 넘어감"을 없애고, 못 찾으면 무엇을 못 찾았는지 알리고 멈춥니다. 게임(패키징 결과)은 지금처럼 실행 폴더 기준입니다.
- **E5 결과 (2026-11-23)**:
  - `cmake/HarukaProject.cmake`에서 `HARUKA_PROJECT_DIR` · `HARUKA_ENGINE_CONTENT_DIR` 컴파일 정의를 제거해 에디터 실행 파일에 빌드 머신 절대 경로를 넣지 않습니다.
  - `HarukaProjectOps`에 `resolveEditorProjectDir`(① `--project=<폴더 또는 .hproject>` → ② 실행 파일 위치에서 위로 `.hproject` 또는 상위 엔진 트리의 `Projects/Samples` → ③ 있으면 빌드 시 정의)와 `resolveEngineForProject`(`Haruka.user.ini` → 상위 엔진 트리 → `Engines.ini` 판 조건 최고 판, `HarukaEngineFinder.cmake`와 동일 규칙 및 공유 시험 표 패리티 검증)를 추가하고, `EditorApp::resolveRuntimePaths`가 이를 호출해 선택 근거 1줄을 로그로 남깁니다.
  - `Engine.cpp`는 `Policy::requireProject` · `Policy::requireEngineContent`가 참일 때(에디터) 못 찾은 대상 · 검색 위치 · 해결 방법을 로그에 남기고, 창 있는 실행이면 `hplat::showFatalError`로 알린 뒤 0이 아닌 종료 코드로 끝납니다(`--hidden-window`에서는 알림 상자 없음). `GameFrontend`는 기존 실행 폴더 기준을 유지합니다.

### 5.7 저장소 안 Samples
- 엔진 개발 편의를 위해 저장소 안 `Projects/Samples`는 지금처럼 엔진 빌드 안에서(같은 CMake 트리) 빌드합니다. 단 바깥 프로젝트와 **같은 `haruka_add_project`와 같은 라이브러리 대상**을 쓰게 해서 두 길이 갈라지지 않게 합니다.
- 바깥 프로젝트 길은 CI 대신 시험 하나로 지킵니다: 임시 폴더에 Blank 프로젝트를 만들고 엔진 패키지로 구성 · 빌드 · 숨은 실행까지(엔진 다시 컴파일 0개 확인).
- **E5 결과 (2026-11-23)**:
  - `build/chk/ext_project.sh`를 `HarukaLauncher new` + `Templates/Blank` 생성 → `generate` → Release/Debug 빌드 → `.obj` 목록(엔진 소스 컴파일 0개) → 게임 `--synthetic-eyes` · 에디터 `--hidden-window` 실행으로 전환하고, 중복된 `tests/ExternalProject/`를 제거했습니다.

### 5.8 동봉 Python (I2)
- 받는 것: python.org 공식 Windows embeddable 패키지(`python-3.15.2-embed-amd64.zip`, 약 13 MB). `Setup.bat`이 판 · SHA-256을 고정해 받아 `ThirdParty/Python/`에 풉니다. 저장소에는 넣지 않습니다.
- 확인한 함정(11-23 시험): embeddable은 `._pth` 파일이 경로를 고정하고 `site`를 끕니다. pywin32(MCP SDK의 Windows 의존)는 `.pth` 처리가 필요해서, 패키지를 `Lib/site-packages`에 설치하고 `._pth`에 `import site`를 켜야 가져오기가 됩니다.
- 패키지 설치: 엔진이 `requirements.txt`(판 고정)를 두고, Setup이 `--only-binary :all:`로 설치합니다(소스 빌드 금지 = C 컴파일러 없이도 실패하지 않음).
- 쓰는 곳: 엔진 도구 스크립트(지금 시스템 `python -I`), MCP 서버, 나중에 에디터 안 Python(별도 설계).
- 빌드 종류: 일반(GIL) 빌드. free-threaded(`3.15.2t`)도 공식 embeddable이 있지만 C 확장 호환 때문에 보류합니다.
- **E3 결과 (2026-11-23)**:
  - 저장소 루트에 얇은 진입점 `Setup.bat`을 두고, `Tools/Setup/ensure_python.ps1`이 `python-3.15.2-embed-amd64.zip`을 내려받아 SHA-256 검증(불일치 시 삭제 후 중단) · `ThirdParty/Python/`(git 제외) 해제 · `._pth`의 `import site` 활성화를 수행한 뒤 `Tools/Setup/setup.py`를 실행합니다. `Tools/Setup/requirements.txt`는 빈 틀만 두어 패키지 설치 단계를 건너뜁니다.
  - `setup.py`는 `vswhere`(정식 + `-prerelease`) · CMake 최소 판 · Git(필수) 및 Vulkan SDK(선택)를 확인한 뒤 Debug/Release 구성·빌드 및 `Engines.ini` 등록을 수행합니다(`--unattended`, `--build-dir`, `--engines-ini`, `--skip-build` 지원).
  - `build.bat` · `run.bat`의 불필요한 `VULKAN_SDK` 탐색 및 경고를 제거하고 `README.md`를 `Setup.bat` 기준으로 갱신했습니다.
- **E4 결과 (2026-11-23)**:
  - 저장소 도구 스크립트(`Packaging/android/package_apk.py`, `src/tools/make_test_*.py`, `Docs/Logo/build_icon.py`) 호출 안내를 `Tools/run_python.bat`(`ThirdParty/Python/python.exe -I` 강제, 미설치 시 `Setup.bat` 안내 후 중단) 기준으로 통일했습니다.
  - 표준 라이브러리 외 패키지가 필요한 오프라인 제작 스크립트 목록(`Docs/Logo/build_icon.py`: Pillow, `src/tools/make_test_gloves.py` · `src/tools/make_test_texture_model.py`: numpy)을 정리하고, 일반 빌드/실행에 불필요하므로 `Tools/Setup/requirements.txt`에는 추가하지 않기로 판단했습니다.

### 5.9 Vulkan을 엔진 안에 (I4, 세부 답 대기)
지금 빌드는 Vulkan SDK에 세 가지를 기댑니다: ① 헤더(공개 헤더 30개가 `vulkan.h`를 포함, 대부분 renderer) ② `vulkan-1.lib` 링크(`Vulkan::Vulkan`) ③ `glslc`(셰이더 컴파일, 엔진 빌드 때만). 실행 중 셰이더 컴파일은 없습니다. 검증 레이어는 Debug 실행 때 SDK 설치본을 씁니다.

| 의존 | 권장 | 근거 |
|---|---|---|
| ① 헤더 | Khronos `Vulkan-Headers`를 판 고정(SDK 태그와 같은 판)으로 FetchContent, 엔진 패키지가 `Haruka::VulkanHeaders`로 함께 내보냄 | Godot `thirdparty/vulkan` · Filament `bluevk` · UE `ThirdParty/Vulkan`이 모두 헤더를 엔진 안에 둠. 프로젝트 쪽은 엔진이 준 헤더만 보게 되어 SDK 판 차이 문제(UE 포럼 사례)가 없어짐 |
| ② 로더 링크 | `volk`(메타 로더)로 바꿔 `vulkan-1.lib` 링크를 없앰. 실제 로더(`vulkan-1.dll`)는 GPU 드라이버가 설치함 | Godot · ANGLE이 volk, UE · Filament도 `vulkan-1.dll`을 실행 중에 불러옴. OpenXR에는 `vkGetInstanceProcAddr`를 넘기면 됨 |
| ③ 셰이더 컴파일러 | Khronos `glslang`을 엔진 빌드 때 호스트 도구로 함께 빌드(FetchContent), `glslc` 대신 `glslangValidator`(`--target-env vulkan1.0` · `--depfile`)를 씀 | UE도 셰이더 컴파일러(DXC 등)를 ThirdParty 바이너리로 가짐. 바꾼 뒤 SPIR-V가 달라질 수 있어 기준 PNG 7장으로 확인 |
| 검증 레이어 | 필수에서 뺌. `Setup.bat`이 SDK 설치 여부를 보고 없으면 "검증 레이어가 필요하면 SDK 설치"를 알리고, 선택 시 LunarG 설치 파일을 무인 실행 | 레이어는 개발 중 진단 도구라 게임 실행에 필요 없음. 레이어 소스 빌드는 무겁고, Godot도 레이어는 SDK에 맡김 |

- 라이선스: Vulkan-Headers · volk · glslang은 Apache 2.0 / MIT / BSD 계열이라 함께 배포할 수 있습니다. 고지문은 `ThirdParty/Licenses`에 모읍니다.
- Android: NDK sysroot 헤더 대신 같은 Vulkan-Headers를 쓸지 E1에서 확인합니다. sysroot를 `-I`로 다시 넣었을 때 libc++의 `#include_next` 순서가 깨진 이력이 있습니다.
- 헤더 노출 정리: 헤더를 엔진이 가지면 노출 자체는 문제가 아닙니다. UE(RHI) · Godot(RenderingDevice) · O3DE(Atom RHI)는 게임 코드가 Vulkan 형을 직접 보지 않게 추상층을 두지만, 그것은 여러 그래픽 API를 위한 설계입니다. 우리는 Vulkan 하나이므로 ui/slate 헤더 5개 정도의 정리만 나중 과제로 남깁니다.
- **E0a 결과 (2026-11-23)**: `Vulkan-Headers` · `volk`(`vulkan-sdk-1.4.357.0`)를 FetchContent로 받아 `haruka_vulkan` 대상으로 묶고, `find_package(Vulkan REQUIRED)`를 제거했습니다(남은 SDK 의존은 `glslc` 하나). `volkInitialize` → `glfwInitVulkanLoader` → `volkLoadInstanceOnly` → `volkLoadDevice` 및 OpenXR `pfnGetInstanceProcAddr` 값 전달로 전환해 기준 PNG 7장 해시 일치 · 구동 시간(에디터 1,154 ms, 게임 969 ms) · ctest 46/46을 확인했습니다. Android(`build/android`)에서도 `Vulkan-Headers` `-I` 경로로 libc++ `#include_next` 충돌 없이 `haruka_vulkan` · `volk`가 정상 빌드되었습니다. `vulkan_imports` 시험은 PE 가져오기 표 비어 있음(가) + `HarukaRuntime.lib` COFF 미해결 기호 검사(나)로 대체했습니다.
- **E0b 결과 (2026-11-23)**: Khronos `glslang`(`vulkan-sdk-1.4.357.0`, `ENABLE_OPT OFF` · `ENABLE_HLSL OFF`)을 FetchContent로 받아 `glslang-standalone`으로 셰이더를 컴파일하고, `find_program(GLSLC glslc)`와 `VULKAN_SDK` 의존을 완전히 제거했습니다. Android는 `HARUKA_HOST_GLSLANG` + `CheckHostGlslang.cmake`로 데스크톱 도구를 씁니다. 검증 레이어가 시스템에 없는 Debug 실행은 1회 경고 후 레이어 없이 계속하며 `Run summary:`에 `(not checked)`로 구분합니다. Android 기존 경고 208건도 0건으로 정리했습니다. (`glslang` 라이선스 고지: 다중 라이선스이므로 `LICENSE.txt` 원문을 `haruka_notice_file`로 포함)

## 6. 단계 (초안)

| 단계 | 내용 | 확인 |
|---|---|---|
| E0 | Vulkan을 엔진 안에(5.9): Vulkan-Headers · volk · glslang. SDK 없이 빌드 | `VULKAN_SDK` 없이 구성 · 빌드, 기준 PNG 7장 같음, Android 빌드 |
| E1 | 라이브러리 나누기(5.1) · 사용 요구 사항 정리(5.2). 저장소 안 빌드만 바뀜 | 빌드 경고 0, ctest, 기준 PNG 7장 · UI 덤프 그대로 |
| E2 | 빌드 트리 패키지(5.3). 바깥 프로젝트가 `find_package(Haruka)`로 빌드 | 바깥 시험 프로젝트 빌드 · 실행, **엔진 소스 컴파일 0개**, 빌드 시간 비교(지금 add_subdirectory 대비) |
| E3 | 엔진 등록 · 엔진 연결(5.4), `Setup.bat` | 엔진 두 벌 등록 · 프로젝트가 맞는 엔진을 찾음, 엔진 폴더 이동 뒤 다시 등록 |
| E4 | `HarukaProjectOps` + 런처 명령줄 모드(5.5) · Blank 템플릿 · 동봉 Python(5.8) | 명령줄로 새 프로젝트 → 솔루션 → 빌드 → 실행을 한 번에, 한글 · 공백 경로, 동봉 Python으로 도구 스크립트 실행 |
| E5 | 실행 때 경로 찾기(5.6), 조용한 복사본 전환 제거 | 엔진 폴더를 옮긴 뒤 다시 등록하면 동작, 못 찾으면 알림 |
| E6 | 런처 창(5.5) · 아이콘 · `.hproject` 파일 연결 | 숨은 실행 PNG(최근 목록 · 새 프로젝트 · 진행), 사용자 화면 확인 |
| E7 | 구동 시간 측정 · 최적화 (`Renderer::init` 세분화, 1 GB 핵심 힙 128 MB 청크 온디맨드 커밋) | 에디터 786 ms(-37.0%) · 게임 497 ms(-52.9%) (E6 대비, 목표 15% 달성), 기준 PNG 7장 · ctest 49/49 · 경고 0; Quest Pro 최저 프레임(1초 표본)은 QP3 soak로 측정 |
| 나중 | 설치판(install 트리) · Android 바깥 프로젝트 · 에디터 자동화 통로 · MCP · 에디터 안 Python(별도 설계) | — |

## 7. 위험

- 외부 라이브러리 내보내기(5.3)가 가장 큰 일입니다. 공개 헤더가 외부 헤더를 얼마나 드러내는지에 따라 범위가 바뀝니다.
- 구성 · 컴파일러가 어긋난 링크는 링크 오류가 아니라 실행 중 이상 동작으로 나타날 수 있습니다(반복자 디버그 수준 · GLM 정의). 구성 단계에서 막아야 합니다.
- 정적 라이브러리로 옮기면 링커가 쓰지 않는 객체 파일을 버립니다. 자기 등록 코드가 있으면 조용히 빠집니다(E1에서 확인).
- Vulkan을 엔진 안에 넣으면(5.9) 프로젝트 사용자는 SDK가 필요 없습니다. 대신 volk 전환은 인스턴스 · 장치 생성 순서와 OpenXR 연결을 건드리고, glslang 전환은 SPIR-V를 바꿀 수 있습니다(E0에서 기준 PNG로 확인).
- 동봉 Python의 바이너리 패키지는 새 판(cp315) 휠이 나와야 설치됩니다. 패키지를 올릴 때 `--only-binary`로 실패를 먼저 드러냅니다.

## 8. 사용자 결정이 필요한 것

끝난 것: 도구 형태(I3), 출력 위치 · 엔진 목록 · 첫 템플릿(권장대로), Python 동봉(I2).

남은 것
1. Python 판: 3.15.2(권장) / 3.14.10.
2. Vulkan 세부(5.9): 헤더 · volk · glslang을 엔진 안에(권장) / 헤더만 엔진 안에, 나머지는 SDK 유지. 검증 레이어는 선택 사항으로 둠(권장) / Setup이 SDK를 늘 함께 설치.
3. 런처 화면 기술: HSlate(권장, 에디터와 같은 모양) / Win32 기본 대화 상자(작고 빠르지만 모양이 다름).
4. 에디터 자동화 통로 · MCP · 에디터 안 Python은 설치 작업 뒤 별도 설계(권장).

## 9. 다른 엔진 조사 (2026-11-23)

출처는 Epic · O3DE · CMake · Microsoft 공식 문서와 GitHub 소스(o3de/o3de, godotengine/godot)입니다. UE 엔진 소스는 계정 연결이 필요해 직접 읽지 못했고, UE 일부 항목은 Epic 직원의 포럼 답변에 기댑니다.

| | UE 5 소스 빌드 | O3DE |
|---|---|---|
| 엔진 준비 | `Setup.bat`(바이너리 의존물 받기 · 필수 구성 요소 · 파일 연결) → `GenerateProjectFiles.bat`(`Engine/Intermediate/ProjectFiles`) | 소스 엔진: 프로젝트 빌드 트리 안에서 엔진도 함께 빌드. SDK 엔진: `INSTALL` 대상으로 미리 빌드한 배포형 |
| 엔진 등록 | 레지스트리 `HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds`(값 = 경로), 소스 빌드는 **기계마다 만든 GUID**. 값 이름을 별칭으로 바꾸면 그 별칭을 쓸 수 있음 | `scripts\o3de.bat register --this-engine` → `%USERPROFILE%\.o3de\o3de_manifest.json`의 `engines`[], 엔진 이름 = `engine.json`의 `engine_name` |
| 프로젝트의 엔진 연결 | `.uproject`의 `EngineAssociation`: 설치판 `"5.1"` · 소스 빌드 `{GUID}` · **빈 값 = 상위 폴더를 거슬러 찾음**(엔진과 게임을 함께 브랜치할 때 권장) | `project.json`의 `engine` = 이름 + 버전 지정자(`o3de>=2.0`), 기계별 경로 덮어쓰기는 `user/project.json`의 `engine_path`. 엔진 트리 안이면 `engine` 없이도 호환 |
| 프로젝트 생성 | 에디터 프로젝트 브라우저, 템플릿 `Templates/TP_*` + `TemplateDefs.ini`(폴더 · 파일 이름 · 파일 안 글 치환) | `o3de create-project --project-path …` 또는 Project Manager GUI, `template.json`의 `copyFiles` + `${Name}` 치환 |
| 엔진 찾기(빌드) | UBT · UnrealVersionSelector(`/projectfiles` · `/switchversion`, .uproject 셸 메뉴) | 프로젝트 `cmake/EngineFinder.cmake`(템플릿이 복사) → `find_package(o3de)`. 순서: ① `CMAKE_MODULE_PATH` ② `user/project.json` ③ 매니페스트 엔진마다 `o3deConfigVersion.cmake`로 호환 확인, 가장 높은 버전 |
| 엔진 다시 컴파일? | Shared 빌드 환경(기본)은 엔진 폴더 빌드를 여러 프로젝트가 공유. Unique는 프로젝트마다 다시 빌드(포럼 근거) | 소스 엔진: `add_subdirectory`로 다시 컴파일. SDK 엔진: `IMPORTED` 대상(구성별 파일), 다시 컴파일 안 함, 구성은 설치된 것만 |

그 밖: Godot는 엔진 실행 파일 + `project.godot`(새 판 프로젝트는 열기 거부), C++ 모듈은 엔진을 다시 빌드하고 다시 빌드하지 않는 길은 GDExtension입니다. Unity Hub는 에디터 설치 목록을 관리하고 프로젝트는 `ProjectSettings/ProjectVersion.txt`에 판을 적습니다. Fyrox는 프로젝트 관리자가 game · editor · executor 워크스페이스를 만듭니다(게임 = 엔진 · 에디터의 플러그인).

CMake 관례
- `install(EXPORT)` + Config 패키지가 옮길 수 있는 정석입니다. Config 안에서 `find_dependency`로 의존을 찾습니다.
- `export(TARGETS/EXPORT)`는 빌드 트리 전용이고 "설치하면 안 됨"이 문서 규칙입니다. `export(PACKAGE)`(사용자 레지스트리)는 3.15부터 기본 꺼짐입니다.
- FetchContent 의존이 어떤 export set에도 없으면 "requires target … not in any export set" 오류가 납니다. 정적 라이브러리는 PRIVATE 의존도 최종 링크까지 `$<LINK_ONLY:>`로 전달되므로 **의존 .lib 자체는 소비자에게 넘어가야** 합니다. 흔한 해법: 의존의 install 규칙을 켜 함께 내보내기, 자체 규칙, `$<BUILD_LOCAL_INTERFACE:>`(3.26+), `FIND_PACKAGE_ARGS`(3.24+).
- MSVC: `MSVC_RUNTIME_LIBRARY` 기본 /MD · /MDd(CMP0091), `_ITERATOR_DEBUG_LEVEL` Debug 2 · Release 0. 어긋나면 LNK2038. 소비자에게 없는 구성(RelWithDebInfo 등)은 `MAP_IMPORTED_CONFIG_<CONFIG>`로 대응시켜야 합니다.

조사로 바꾼 것
- **엔진 식별(5.4)**: GUID 대신 **엔진 이름 + 버전**으로 바꿉니다. UE 소스 빌드의 GUID는 기계마다 달라 팀끼리 맞지 않습니다. `.hproject`에는 `engine = haruka>=0.1` 형식(O3DE)을 쓰고, 기계별 경로 덮어쓰기는 프로젝트의 git 제외 파일에 둡니다.
- **엔진 찾기(5.4)**: 템플릿이 프로젝트에 `cmake/HarukaEngineFinder.cmake`를 복사하고, 찾는 순서(명시 `-DHaruka_DIR` → 프로젝트의 기계별 덮어쓰기 → 엔진 목록에서 호환되는 가장 높은 판)를 고정합니다. 실패하면 "엔진 폴더에서 `Setup.bat` 실행" 같은 고칠 방법을 오류로 알립니다.
- **엔진 트리 안 프로젝트**: 엔진 연결이 비어 있으면 상위 폴더를 거슬러 엔진을 찾는 규칙(UE 빈 값 · O3DE 트리 안 호환)을 그대로 씁니다. 저장소 안 `Projects/Samples`가 등록 없이 동작합니다.
- **등록은 무인 실행 가능하게**: `Setup.bat`에 대화 없는 경로(`--unattended`)를 둡니다. 파일 연결 · 셸 메뉴는 선택 사항입니다.
- **패키지(5.3)**: 빌드 트리 export냐 엔진 폴더 안 install 트리냐는 E2 시제품에서 정합니다. 어느 쪽이든 FetchContent 의존 .lib까지 함께 내보내야 하고, Config 파일에 엔진을 빌드한 구성 · MSVC 런타임 · `_ITERATOR_DEBUG_LEVEL` · 도구 집합을 적어 프로젝트 쪽에서 같은 값을 강제합니다. RelWithDebInfo 같은 구성은 `MAP_IMPORTED_CONFIG`로 Release에 대응시킵니다.

### 9.1 Python 3.15 시험 (2026-11-23)
- 판: 3.15.0 = 2026-10-09, 지금 최신 3.15.2(10-25 빌드). 직전 3.14는 3.14.10(10-25 보안 수정). python.org에 3.15.2 embeddable(amd64 · arm64 · win32, free-threaded `t` 판 포함)이 있습니다.
- PyPI의 Windows cp315 휠(11-23 확인): pydantic-core 2.50.5 · rpds-py · jiter · pyyaml · pywin32 312 · cryptography(abi3) 모두 있음. mcp 2.3.0 · nanobind 3.1.0 · pybind11 3.1.0 · uvicorn은 순수 Python 휠.
- 실제 시험: 임시 폴더에 3.15.2 embeddable을 풀고 `uv pip install --target … --only-binary :all: mcp nanobind`(30개) → `MCPServer` 생성까지 성공, `sys.flags.utf8_mode = 1`. 크기는 Python 26 MB + 패키지 52 MB.
- 참고: mcp 2.x에서 `FastMCP`가 `MCPServer`(`mcp.server.mcpserver`)로 이름이 바뀌었습니다. 예전 예제 코드는 그대로 돌지 않습니다.
- 참고: 같은 조사 중 시스템 3.11에서 UTF-8 JSON을 `open()` 기본값으로 읽다가 cp949 오류가 났습니다. 3.15의 UTF-8 기본(PEP 686)이 바로 이 문제를 없앱니다.
