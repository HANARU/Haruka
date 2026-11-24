# 프로젝트 설정 창 (Project Settings) 설계

2026-11-24 사용자 결정: "Project Settings를 만든 김에, 여기에 들어갈 만한 구성을 전부 여기로 모으자. 에디터 스타일은 UE5를 참고."
V1(VR 자동 감지)에서 만든 체크박스 하나짜리 대화상자를 UE5식 설정 창으로 키웁니다.

## 1. 조사 요약

### 1-1. 지금 흩어져 있는 설정 (2026-11-24)

| 파일 | 키 | 성격 | 지금 고치는 방법 |
|---|---|---|---|
| `Config/project.ini` | `startScene` | 프로젝트 | 손으로 편집 |
| 〃 | `xr.height` · `xr.eyeHeight` · `xr.startInVr` | 프로젝트 | 손으로 편집, `xr.startInVr`만 V1 대화상자 |
| 〃 | `render.ao` · `render.bloom` · `render.csmCascades` · `render.shadowDistance` · `render.contactShadows` · `render.heightFog` · `render.reflectionCapture` · `render.lightLimit` (+ `.android` 접미사) | 프로젝트, 플랫폼별 덮어쓰기 | 손으로 편집 |
| 〃 | `sim.tickRate` (72 ~ 240) | 프로젝트 | 손으로 편집 |
| 〃 | `physics.layer.<1~15>` · `physics.ignore` | 프로젝트(충돌 레이어) | 손으로 편집 |
| 〃 | `input.action.*` · `input.axis.*` · `input.mod.*` · `input.context.*` | 프로젝트(입력 바인딩) | 손으로 편집 |
| `<이름>.hproject` | `name` · `company` · `packageId` · `engineVersion` · `engine` | 프로젝트 정보 | 손으로 편집. CMake(`cmake/HarukaProject.cmake` 147행)가 Android 패키지 이름 · 앱 이름을 여기서 가져옴 |
| `Config/game.pack.txt` | `map` · `exclude` · `stored` 명령 | 패키징 목록 | 손으로 편집 |
| `Packaging/android/AndroidManifest.xml.in` | `versionCode="1"` 고정, `minSdk 32` · `targetSdk 34`, `com.oculus.supportedDevices` = quest2\|questpro\|quest3\|quest3s | Android(Quest) 패키징 | 엔진 템플릿에 고정 |
| `Saved/Config/engine.ini` | `render.maxDrawRows` · `render.maxLights` · `physics.maxBodies` | 기계별 한도 | 손으로 편집 |
| `Saved/Config/editor_state.txt` | `lastScene` · `audioHeadsetInVr` · `audioHrtf` | 이 기계의 에디터 상태 | 에디터 메뉴(Audio Output 대화상자) |
| `Saved/Config/user_settings.ini` | `audio.*` · `render.*` · `input.*` · `comfort.*` | 플레이어 설정(게임 실행 중) | 게임 코드 · `setting` 콘솔 명령 |

발견한 공백 하나: Android `versionName`이 프로젝트 버전이 아니라 **엔진 버전**(`HP_DESC_engineVersion`)이고, `versionCode`는 1로 고정입니다. 스토어에 새 판을 올리려면 프로젝트가 이 두 값을 가져야 합니다(3절 PS2).

### 1-2. UE5의 Project Settings (5.x 기준)
- **창:** Edit > Project Settings가 따로 떠 있는 탭(창)으로 열립니다. 에디터 개인 설정(Edit > Editor Preferences)은 같은 모양의 다른 창입니다.
- **왼쪽:** 분류 목록입니다. 큰 묶음(Project · Game · Engine · Editor · Platforms · Plugins) 아래에 항목(Description · Maps & Modes · Packaging · Physics · Rendering · Input · Android · OpenXR …)이 있습니다.
- **위:** 검색 칸 하나가 **모든 분류**를 거릅니다. 검색어가 있으면 맞는 행이 있는 분류만 보입니다.
- **오른쪽:** 고른 분류의 제목과 설명이 있고, 그 아래에 "이 설정은 `DefaultEngine.ini`에 저장됩니다"라는 저장 위치 줄과 Export · Import · Reset to Defaults 단추가 있습니다. 아래 본문은 디테일 패널과 같은 접히는 절 + 속성 행이고, 행마다 기본값 되돌리기 화살표가 있습니다.
- **다시 시작:** 다시 시작해야 적용되는 값을 바꾸면 "Restart required" 알림과 "Restart Now" 단추가 뜹니다.
- **플랫폼별 값:** 일부 값은 행 옆 단추로 플랫폼별 덮어쓰기를 더합니다(Per-Platform 값). 플랫폼 전용 설정은 Platforms > Android 같은 분류에 둡니다.
- **저장:** 바꾸면 바로 ini에 씁니다(따로 저장 단추가 없음).

### 1-3. 지금 에디터에 있는 재료
- `PropDesc`(`src/scene/Reflect.hpp` 49행): 종류(Float · Toggle · Enum · String · Vec3 · Color · AssetPath …), getter/setter, 단위, 기본값(`withDefault`)을 가진 범용 속성 설명자입니다. 컴포넌트가 아닌 값도 그대로 표현할 수 있습니다.
- 디테일 패널(`src/editor/DetailsPanel.cpp`): 종류별 값 위젯(`vectorField` · `colorField` · `comboField` · `assetPathValue` 등), 행 오른쪽 메뉴(기본값으로 되돌리기 · 복사 · 붙여넣기, H3a), 검색(`src/editor/DetailsSearch.hpp`, 영어 원문 · 번역 이름 둘 다로 찾음, H3b)이 있습니다. 다만 행을 만드는 코드가 `Actor` · `Component` · 실행 취소 스택에 묶여 있습니다(`buildComponentProp`, 761행).
- 패널 등록(`panelReg_`) · 도킹(`dock_.showPanel`) · 떠 있는 OS 창(Editor_MultiWindow_Design.md)이 있어서, 새 패널을 등록하면 Window 메뉴와 떠 있는 창을 그대로 쓸 수 있습니다.
- `project_settings::setIniBool`(V1): 한 줄만 바꾸고 주석 · 순서 · 줄 끝을 보존합니다.

## 2. 결정

- **PS-D1 창:** Project Settings는 등록 패널 하나(`ProjectSettings`)로 만들고, Edit > Configuration > Project Settings...로 엽니다. 처음 열 때는 가운데에 떠 있는 OS 창(약 960 × 640)으로 열고, 닫았다가 다시 열면 그 창을 앞으로 가져옵니다. V1의 모달 대화상자는 이 창으로 바꿉니다. 도킹도 다른 패널처럼 됩니다.
- **PS-D2 배치(UE5 모양):**
  - 위: 검색 칸이 모든 분류를 거릅니다(`hdsearch` 규칙).
  - 왼쪽: 분류 목록입니다. 묶음 제목은 Project · Engine · Platforms이고, 검색 중에는 맞는 행이 있는 분류만 보입니다.
  - 오른쪽 머리: 분류 제목, 한 줄 설명, 저장 위치(`Config/project.ini` 또는 `<이름>.hproject`)를 둡니다.
  - 오른쪽 본문: 접히는 절과 속성 행입니다. 디테일 패널과 같은 모양 · 같은 행 메뉴(기본값으로 되돌리기 · 복사 · 붙여넣기)를 씁니다.
  - 다시 시작 띠: 다시 시작 항목을 바꾸면 본문 위에 노란 띠 "Restart the editor to apply some changes"를 띄웁니다. "Restart Now" 단추는 PS1c에서 붙입니다(PS-D10).
- **PS-D3 설정 표 하나가 정본:** 모든 키를 표 `ProjectSettingsSchema`(core, 순수 데이터)에 한 번씩 적습니다. 표의 각 행은 다음 필드를 가집니다.
  - 파일과 키
  - 분류 · 절
  - 화면 이름(HTEXT)
  - 종류 · 범위
  - 기본값
  - 적용 시점(바로 / 다시 시작)
  - 플랫폼 덮어쓰기 허용 여부
  - 도움말 한 줄

  `ProjectSettings::parse`의 범위 · 기본값과 이 표가 어긋나지 않음을 단위 시험이 지킵니다. 창은 이 표에서 `PropDesc`를 만들어 그립니다. 그래서 키를 하나 더하면 표 한 줄만 고치면 됩니다.
- **PS-D4 행 위젯 공유:** 디테일 패널의 값 위젯 · 행 메뉴 · 기본값 화살표 코드를 `Actor`/`Component`에 묶이지 않은 행 도구(예: `PropertyRowKit`)로 떼어 두 패널이 함께 씁니다. 복사해서 두 벌로 만들지 않습니다. 디테일 패널의 UI 덤프는 **바뀌지 않아야** 합니다(이것이 떼어 낸 작업의 회귀 시험).
- **PS-D5 저장:**
  - 바꾸면 바로 그 파일의 그 줄만 고쳐 씁니다. `setIniBool`을 일반화한 `setIniValue` · `removeIniKey`를 쓰고, 주석 · 순서 · 줄 끝을 보존합니다.
  - 파일이 있는데 읽지 못하면 쓰지 않고 경고합니다.
  - 숫자 칸은 끌기 · 입력이 끝났을 때 한 번 씁니다. 끄는 동안 프레임마다 쓰지 않습니다.
  - 기본값으로 되돌리기는 기본값을 적습니다(줄을 지우지 않음, 지금 파일들이 모든 키를 적어 두는 관례를 따름).
  - 설정 변경은 씬의 실행 취소 스택에 넣지 않습니다(UE와 같음).
- **PS-D6 플랫폼 덮어쓰기:** 덮어쓰기를 허용하는 행(지금은 `render.*`)은 값 옆에 작은 "+ Android" 단추를 둡니다. 누르면 그 아래에 들여 쓴 "Android" 행이 생기고 `<키>.android = 값` 줄을 씁니다. 그 행의 x를 누르면 줄을 지웁니다. 이 경우만 줄을 지웁니다.
- **PS-D7 적용:**
  - "바로" 항목은 에디터 실행 중에 엔진에 바로 반영합니다(예: `render.ao` · `render.bloom`은 렌더러가 프레임마다 읽음).
  - "다시 시작" 항목은 파일만 고치고 띠를 띄웁니다.
  - 어느 키가 어느 쪽인지는 1단계에서 코드를 보고 표에 확정합니다. 확실하지 않으면 "다시 시작"으로 둡니다.
- **PS-D8 넣지 않는 것:**
  - 기계별 한도(`engine.ini`): 프로젝트와 함께 버전 관리하지 않는 값입니다(haruka 관례: 기계별 값은 `Saved/Config/`).
  - 에디터 개인 설정(언어 · 출력 장치 · 헤드셋 소리 · HRTF · 기즈모 크기): 사람마다 다른 값이라 UE처럼 **Editor Preferences** 창(같은 틀, PS-D12)에 둡니다. 마지막 씬은 화면에 두지 않습니다.
  - 플레이어 설정(`user_settings.ini`): 게임 설정 화면의 몫입니다.
  - `game.pack.txt`: 명령 목록이라 칸으로 나타내기 어렵습니다. Packaging 분류에 파일 위치와 "Open in Explorer"만 둡니다.
- **PS-D10 Restart Now (2026-11-24 사용자 결정: "넣자. VR 감지 후 재기동이 필요한 요소들이 초기화 면에서 편하다"):**
  - 에디터가 스스로 다시 시작하는 명령 `Editor.Restart`를 둡니다. 다시 시작 띠와 아래 VR 알림에 "Restart Now" 단추로 붙고, File 메뉴에도 둡니다.
  - 순서는 다음과 같습니다.
    1. 씬을 저장하지 않았으면 기존 저장 확인(Save · Don't Save · Cancel, `EditorApp_scene.cpp`의 닫기 확인과 같은 것)을 띄웁니다.
    2. 에디터 상태 · 레이아웃을 저장합니다.
    3. 같은 실행 파일을 같은 인자로 띄우되, 한 번만 쓰는 시험 인자(`--frames` · `--ui-*` · `--dump-*` 등)는 빼고 `--restart-wait-pid=<지금 PID>`를 붙입니다.
    4. 지금 프로세스는 정상 종료합니다. 새 프로세스는 앞 프로세스가 끝날 때까지 기다린 뒤 시작합니다(설정 파일 경합 방지, 제한 시간 10초).
  - 프로세스 띄우기는 런처의 기존 코드(`src/projectops/ProjectOps.cpp`, `CreateProcessW`)를 공유합니다.
  - 숨은 창 · 헤드리스 실행에서는 명령을 흐리게 둡니다. 시험은 인자 거르기 순수 함수 단위 시험과 `--restart-dry-run`(띄울 명령줄만 로그에 남기고 띄우지 않음)으로 합니다.
- **PS-D11 VR 늦은 연결 알림:**
  - VR 자동 감지가 켜져 있는데 시작 때 헤드셋이 없어 OpenXR 준비가 실패했다고 합시다(런타임은 있음, `xrGetSystem` 실패). 이때 OpenXR 인스턴스를 남겨 두고, 편집 중에 2초 간격으로 헤드셋을 확인합니다.
  - 헤드셋이 잡히면 알림 "VR headset connected. Restart the editor to use VR."과 [Restart Now]를 띄웁니다. 렌더러 장치는 OpenXR이 고른 GPU로 다시 만들어야 하므로 실행 중 전환은 하지 않고 다시 시작으로 해결합니다.
  - 런타임 자체가 없으면 확인하지 않습니다.
  - Play 툴팁 · XR 분류 상태 줄에도 같은 상태를 보입니다.
- **PS-D12 Editor Preferences (2026-11-24 사용자 결정: "넣자"):**
  - Edit > Configuration > Editor Preferences...로 여는 두 번째 창입니다. Project Settings와 같은 틀(분류 · 검색 · 행 도구 · 바로 저장)을 씁니다.
  - 저장 위치는 이 기계의 에디터 상태(`Saved/Config/editor_state.txt`)이고, 프로젝트 파일에는 쓰지 않습니다.
  - 1차 분류:
    - General > Region & Language: 언어
    - General > Appearance / Viewport: 기즈모 크기
    - Audio: 출력 장치 · VR Play 동안 헤드셋 소리 · HRTF 모드
  - Edit 메뉴의 Language 하위 메뉴는 빠른 길로 남깁니다. Audio Output 대화상자는 이 창의 Audio 분류로 바꿉니다(메뉴 항목은 그 분류를 엶).
- **PS-D9 시험 안전:** 시험(UI 덤프 · 숨은 실행)은 창을 열고 분류 · 검색 장면을 그리기만 하고, 값을 바꾸는 장면은 두지 않습니다. 쓰기 경로는 단위 시험(문자열 → 문자열)과 임시 폴더에 복사한 프로젝트로 확인합니다. `Projects/Samples`에는 쓰지 않습니다.

## 3. 분류와 단계

| 단계 | 분류 (UE5 대응) | 키 | 비고 |
|---|---|---|---|
| **PS1** | Project > Description | `.hproject`의 `name` · `company` · `packageId`, `engineVersion` · `engine`(읽기 전용) | `packageId`를 바꾸면 Android 다시 구성이 필요하다는 도움말 |
| PS1 | Project > Maps & Modes | `startScene`(씬 에셋 고르기) | |
| PS1 | Engine > General (Simulation) | `sim.tickRate` | 72 ~ 240 |
| PS1 | Engine > Rendering | `render.*` 8개 + Android 덮어쓰기 | PS-D6 |
| PS1 | Platforms > XR (UE의 Plugins > OpenXR) | `xr.startInVr`(VR auto-detect, 이번 실행 상태 줄 포함) · `xr.height` · `xr.eyeHeight` | V1 대화상자 대체 |
| PS1 | Project > Packaging | `game.pack.txt` 위치 · Open in Explorer | 읽기 전용 |
| **PS1c** | (명령 · 알림) | Restart Now(PS-D10), VR 늦은 연결 알림(PS-D11) | 다시 시작 띠 · XR 상태 줄에 단추 연결 |
| **PS1d** | Editor Preferences 창 | 언어 · 기즈모 크기 · 출력 장치 · 헤드셋 소리 · HRTF(PS-D12) | Audio Output 대화상자 대체 |
| PS2 | Engine > Physics (Collision) | `physics.layer.1 ~ 15` 이름, `physics.ignore` | 레이어 이름 목록 + 충돌 행렬(Unity의 Layer Collision Matrix 모양, 체크 = 부딪힘) |
| PS2 | Platforms > Android | 앱 버전 이름 · 버전 코드(새 `.hproject` 키 `version` · `versionCode`), 지원 기기(`quest2 \| questpro \| quest3 \| quest3s` 체크), `minSdk` · `targetSdk`(읽기 전용) | CMake와 매니페스트 템플릿이 새 키를 읽게 함. 버전 공백 해결 |
| PS3 | Engine > Input | `input.action.*` · `input.axis.*` · `input.mod.*` · `input.context.*` | 동작 · 축 목록 편집(더하기 · 지우기 · 키 고르기). 규칙은 `Docs/Input_Extensions.md` |
| 나중 | Engine > Network | 멀티플레이 설정(방 인원 · 틱 · 지연 보정) | `Docs/Multiplayer_Design.md` 구현 때 |

PS1이 틀(창 · 표 · 행 공유 · 저장 · 검색 · 다시 시작 띠 · 플랫폼 덮어쓰기)을 모두 만들고, PS2 · PS3는 그 틀에 분류를 더합니다.

## 4. 결정된 질문
- **Restart Now:** 넣습니다(2026-11-24, PS-D10 · D11). 사용자 이유: VR 감지 뒤 다시 시작해야 하는 요소들은 초기화 면에서 다시 시작이 편합니다.
- **Editor Preferences:** 넣습니다(2026-11-24, PS-D12). 사용자 이유: "괜히 이런 게 있는 게 아니다."

## 5. 결과

### PS1a 결과 (2026-11-24)
- **정본 설정 표:** `src/core/ProjectSettingsSchema.hpp` (`hps_schema::entries()`, PS1 범위 18개 키: `Config/project.ini` 13개 + `<이름>.hproject` 5개). `tests/ProjectSettingsTest.cpp`(`project_settings`)가 `ProjectSettings::parse` 와의 기본값 · 범위 · 키 누락을 검증합니다.
- **ini 한 줄 쓰기/지우기:** `src/core/ProjectSettings.hpp` (`project_settings::setIniValue`, `removeIniKey`, `setIniBool`, `decideIniWrite`). `.hproject` 도 같은 형식이라 공용으로 씁니다.
- **디테일 행 도구:** `src/editor/PropertyRowKit.hpp` · `PropertyRowKit.cpp` (`PropertyRowKit`). `DetailsPanel` 이 이 도구 위에서 동작하며, `uidump.sh ps1a v1c` 기준 디테일 · 메뉴 포함 전 장면 차이 0건입니다.
- **적용 시점 확정 표:**

| 키 | 파일 | 적용 시점 | 근거 (코드 확인) |
|---|---|---|---|
| `render.ao`, `render.bloom` | `project.ini` | 바로 (Live) | `Engine.cpp` 818~819행에서 매 프레임 `renderer_->setPostFeatures` 호출 |
| `xr.height`, `xr.eyeHeight` | `project.ini` | 바로 (Live) | `XrViewOrigin.cpp` 26~27행에서 매 프레임 `project` 참조(실제 `Engine::project_` 갱신은 PS1b) |
| `render.csmCascades`, `render.shadowDistance`, `render.contactShadows`, `render.heightFog`, `render.reflectionCapture`, `render.lightLimit` | `project.ini` | 다시 시작 (Restart) | `Engine.cpp` 473행에서 부팅 시 1회 `renderer_->setQuality` 호출 |
| `sim.tickRate`, `startScene`, `xr.startInVr` | `project.ini` | 다시 시작 (Restart) | `Engine.cpp` 부팅 시 1회 초기화 · 로그 |
| `name`, `company`, `packageId`, `engineVersion`, `engine` | `.hproject` | 다시 시작 (Restart) | 부팅 · 패키징 시 읽음 (`engineVersion`, `engine` 은 읽기 전용) |

### PS1b 결과 (2026-11-24)
- **창 틀 (`SettingsPanel`):** `src/editor/SettingsPanel.hpp` · `.cpp` (PS1d `Editor Preferences` 재사용 가능하도록 `SettingsSource` 매개변수화, 다시 시작 띠에 PS1c 단추 자리 포함).
- **Project Settings 창 (`ProjectSettingsPanel`):** `src/editor/ProjectSettingsPanel.hpp` · `.cpp`. 등록 패널 `ProjectSettings` 로 추가(`Edit.ProjectSettings` · `Window.Panel.ProjectSettings` 가 가운데 960×640 떠 있는 OS 창으로 엶). V1 모달 대화상자 및 `ModalSpec` 체크박스 제거.
- **분류 · 기능:** `Description` · `Maps & Modes` · `Packaging`(Explorer 열기) · `General` · `Rendering`(`+ Android` / `x` 덮어쓰기) · `XR`(이번 실행 상태 줄). `hdsearch` 검색 · 다시 시작 띠 · 저장 후 재파싱 경고 · 바로 적용(`render.ao`, `render.bloom`, `xr.height`, `xr.eyeHeight` → `Engine::project_` 갱신) 구현.
