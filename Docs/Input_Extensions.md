# 입력 확장: 게임패드 · 수정자 · 입력 컨텍스트 · 사용자 재지정 (2026-11-21)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 10위 후보입니다. 지금 입력 시스템은 키 · 마우스 · XR 컨트롤러를 동작 · 축 이름에
묶을 수 있지만, 데스크톱 게임패드를 읽지 못하고, 데드존 · 감도 같은 수정자가 없으며, 메뉴처럼 입력을 잠시 가로채는 층(컨텍스트)이 없고,
플레이어가 키를 바꾸는 API 가 없습니다. 이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고, 항목을 모두 만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-21)

| 곳 | 지금 | 문제 |
|---|---|---|
| `input/HInputSystem` | 동작(키 · XR 버튼 11개) · 축(XR 축 8개 · 키 쌍 · 마우스) 매핑, 이름으로 구독 · `held` · `axis` | 바인딩 표가 하나뿐(컨텍스트 없음), 수정자 없음 |
| `applyBinding` | `project.ini` 의 `input.action.*` · `input.axis.*` 한 줄을 바인딩으로 | 게임패드 소스(`pad:`)가 없음, 지금 바인딩을 글로 되돌리는 길이 없음 |
| `platform/IWindow` · `GlfwWindow` | 키 · 마우스만 | GLFW 3.4 의 게임패드(표준 매핑) API 를 쓰지 않음 |
| `Engine::applyUserInput` | 사용자 설정의 `input.action.*` · `input.axis.*` 를 프로젝트 줄 위에 덮음(바뀐 것만 저장, 지우면 프로젝트 값) | 덮어쓰기 규칙이 Engine 안에 있어 창 없이 시험할 수 없음, 동작 하나의 키 하나만 바꾸는 API · 충돌 검사가 없음 |
| XR | `XrSessionController` 가 `setXrInput`, 햅틱은 `setHapticSink` | 그대로 둠 |

### 1-2. 다른 엔진

- **Unreal Enhanced Input:** 입력 동작(`UInputAction`, 값 형식 bool · 1D · 2D) + 매핑 컨텍스트(`UInputMappingContext`, 키 → 동작, 키마다
  수정자 · 트리거). 컨텍스트는 우선순위와 함께 `AddMappingContext` · `RemoveMappingContext`. 높은 우선순위 컨텍스트에 매핑된 **키는**
  (`bConsumeInput`) 낮은 컨텍스트의 같은 키 매핑을 막습니다. 수정자는 데드존(`Axial` · `Radial`, 하한 · 상한 재조정) · 배율(감도) ·
  반전(`Negate`) · 응답 곡선(지수 · 사용자 곡선). 플레이어 재지정은 `UEnhancedInputUserSettings`(바뀐 키만 저장, 슬롯별, 기본값 되돌리기).
  게임패드는 XInput · SDL 이 아닌 자체 플랫폼 층(`FGenericApplicationMessageHandler`)에서 읽고 `EKeys::Gamepad_*` 키로 들어옵니다.
- **Unity Input System:** `InputActionAsset` 안에 액션 맵(= 컨텍스트) 여러 개, 맵을 켜고 끔(우선순위 없음, 같은 컨트롤 공유 가능).
  바인딩마다 처리기(`StickDeadzone` 원형 · `AxisDeadzone` · `Scale` · `Invert` · `Clamp`), 재지정은 `PerformInteractiveRebinding` +
  `SaveBindingOverridesAsJson`(덮어쓴 경로만), 기본값은 `RemoveBindingOverride`. 장치는 플랫폼 백엔드가 읽고 `Gamepad.current` 는
  마지막으로 쓴 패드입니다.
- **Godot:** `InputMap` 동작 + 동작별 데드존(축별 하나), `Input.get_vector` 가 원형 데드존. 컨텍스트 개념은 없고 `_unhandled_input` 에서
  `set_input_as_handled()` 로 이벤트를 소비합니다. 게임패드는 SDL 게임 컨트롤러 DB(표준 매핑), 연결 · 해제는 `joy_connection_changed` 신호.
  재지정은 `InputMap.action_erase_events` · `action_add_event` 를 게임이 직접 저장.
- **GLFW 3.4:** `glfwJoystickIsGamepad` · `glfwGetGamepadState` 가 SDL 게임 컨트롤러 DB 로 버튼 15개 · 축 6개(스틱 Y 는 아래가 +, 트리거는
  -1 ~ 1)를 표준 배치로 줍니다. 연결 · 해제는 `glfwSetJoystickCallback` 또는 폴링으로 압니다. **진동 API 는 없습니다.**

### 1-3. 공통 구조

1. 장치 읽기(플랫폼 백엔드)와 매핑(입력 층)이 나뉘고, 게임패드는 표준 배치(아래 · 오른쪽 · 왼쪽 · 위 면 버튼, 숄더, 스틱, 트리거)로 들어옵니다.
2. 데드존은 스틱 단위(원형)가 기본이고, 축별(사각형)도 고를 수 있습니다. 데드존 밖은 0 ~ 1 로 다시 펼칩니다.
3. 컨텍스트(매핑 묶음)를 켜고 끄며, 위 층이 가져간 입력은 아래 층에 가지 않습니다(UE 는 키 단위 소비, Godot 은 이벤트 소비).
4. 재지정은 기본 바인딩 위의 **덮어쓰기만** 저장하고, 덮어쓰기를 지우면 기본값입니다.

## 2. 결정

- **D1 장치는 플랫폼 층, 매핑은 입력 층.** `platform/Gamepad.hpp` 에 `HGamepadState`(연결 · 이름 · 버튼 15 · 축 6)와 `HPadButton` ·
  `HPadAxis`(GLFW 표준 배치 순서)를 두고, `IWindow::readGamepad` 가 채웁니다(기본 구현은 연결 없음, GLFW 창만 읽음). 스틱 Y 는 위가 +
  (XR 엄지 스틱과 같게), 트리거는 0 ~ 1 로 플랫폼 층에서 바꿉니다. 입력 층은 `HInputSystem::setGamepad` 로 받으므로, 시험은 가짜 상태를 직접
  넣습니다. `update(dt, IWindow&)` 가 창에서 읽어 넣습니다.
- **D2 여러 패드 중 하나만, "붙어 있는 첫 패드".** 지금 쓰는 패드가 연결되어 있는 동안은 그 패드를 계속 씁니다. 없으면 GLFW 조이스틱 번호가
  가장 작은 게임패드를 고릅니다(`hgamepad::pick`, 시험 가능). 그래서 두 번째 패드를 꽂아도 바뀌지 않고, 쓰던 패드를 빼면 남은 패드 중 첫
  번째로 넘어갑니다. 연결 · 해제는 `LogInput` 에 한 줄씩 남기고, 해제되면 상태가 모두 0 이라 눌려 있던 동작은 `Released` 를 한 번 받습니다.
  로컬 다인 플레이(패드별 플레이어)는 범위 밖입니다.
- **D3 소스 이름.** 동작: `pad:a · b · x · y · lb · rb · back · start · guide · ls · rs · dpad.up · dpad.down · dpad.left · dpad.right · lt · rt`(트리거는 XR 과 같이
  절반 넘게 당기면 눌림). 축: `pad:left.x · left.y · right.x · right.y · lt · rt`. 면 버튼 이름은 Xbox 배치(아래 = a)입니다(UE · SDL 과 같음).
- **D4 수정자는 축마다 한 줄, 바인딩과 따로.** `input.mod.<축> = deadzone:0.2, axial, curve:2, sens:1.5, invert`. 따로 둔 이유는
  플레이어가 감도 · 반전만 바꿀 때 소스 목록을 다시 쓰지 않게 하기 위해서입니다(UE 의 사용자 설정 · Unity 처리기 덮어쓰기와 같은 쓰임).
  - 데드존 · 곡선은 **아날로그 소스(패드 · XR)마다** 적용합니다. 원형(`radial`, 기본)은 스틱 벡터 길이로, 축별(`axial`)은 그 성분만으로
    자르고, 데드존 밖은 0 ~ 1 로 다시 펼칩니다. 트리거는 둘이 같습니다. 곡선은 `부호 × |x|^curve` 입니다.
  - 감도 · 반전은 **축 전체 값**(아날로그 + 키 + 마우스)에 곱합니다. 시선 감도 · 반전은 마우스 · 스틱이 함께 따라야 하기 때문입니다.
  - 데드존을 적지 않으면 **패드 소스만 0.15 원형**, XR 소스는 0 입니다. XR 런타임 값은 지금처럼 그대로 두어 기존 동작을 바꾸지 않습니다.
  - 여러 아날로그 소스는 지금처럼 절댓값이 가장 큰 값 하나를 쓰고(패드 · XR 이 함께 경쟁), 키 · 마우스는 더합니다.
- **D5 입력 컨텍스트 = 이름 있는 바인딩 묶음 + 우선순위 스택.** `input.context.<이름>.action.<동작>` · `.axis.<축>` · `.mod.<축>` 줄로
  정의하고(접두어 없는 줄은 `default` 컨텍스트), 게임 코드가 `pushContext(이름, 우선순위, blockLower)` · `popContext(이름)` 로 켜고 끕니다.
  `default` 는 언제나 맨 아래에 켜져 있습니다. 같은 우선순위는 나중에 민 것이 위입니다.
  - **소비 규칙은 소스 단위(UE 방식)입니다.** 켜진 컨텍스트가 매핑한 키 · 버튼 · 축은 그보다 아래 컨텍스트에서 보이지 않습니다. 그래서
    메뉴 컨텍스트가 `key:W` 를 쓰면 게임의 `MoveY` 는 W 를 잃지만 S · 스틱은 그대로입니다. `blockLower` 로 민 컨텍스트는 그 아래를 모두
    막습니다(모달 메뉴). XR 트리거 버튼과 트리거 축처럼 같은 물리 입력은 같은 소스로 봅니다.
  - 동작 · 축 이름이 여러 컨텍스트에 있으면 동작은 OR, 축은 합입니다(소비 규칙 때문에 같은 소스가 두 번 세지지 않음). 이벤트 · `held` 는
    이름 단위라, 컨텍스트를 밀어 동작이 막히면 `Released` 를 한 번 받습니다.
  - 소비 결과는 입력 상태와 상관없이 스택 · 바인딩으로만 정해지므로, 바뀔 때만 다시 계산합니다.
- **D6 재지정은 소스 글 단위, 덮어쓰기만 저장.** `HInputSystem::rebind(settings, 바인딩 키, from, to)` 는 지금 바인딩 글에서 소스 `from` 을
  `to` 로 바꾸고(`from` 이 비면 더하고, `to` 가 비면 뺌), 바로 적용하며, 사용자 설정에 씁니다. 결과가 프로젝트 줄과 같으면 덮어쓰기를
  지웁니다(바뀐 것만 저장). 소스를 모두 빼면 `none` 입니다. `resetBinding` · `resetAllBindings` 는 덮어쓰기를 지워 프로젝트 줄로 돌립니다.
  결과(`RebindResult`)에는 같은 컨텍스트에서 같은 소스를 쓰는 다른 동작 · 축 목록(충돌)이 들어 있고, 충돌이 있어도 바꾸기는 합니다(UE ·
  Unity 처럼 처리 방법은 게임 UI 가 정함). 소스 글은 비교할 수 있게 정규 글(`key:W` · `key:space` · `pad:a` · `xr:left.grip`)로 바꿉니다.
- **D7 덮어쓰기 적용 규칙을 입력 층으로 옮김.** 예전 `Engine::applyUserInput` 의 규칙(프로젝트 줄 위에 덮음, 지우면 되돌림, 틀린 글은 알리고
  프로젝트 줄 유지, `none` = 비움)을 `HInputSystem::applyUserOverrides` 로 옮겨, 다시 시작한 뒤 적용을 창 없이 시험합니다. 엔진은 시작할 때와
  사용자 설정이 바뀐 프레임에 이것을 부르고 결과만 로그로 남깁니다. 사용자 설정에는 `input.mod.` · `input.context.` 접두어를 더합니다.
- **D8 진동은 이번에 넣지 않습니다.** GLFW 에 진동 API 가 없어 XInput(Windows 전용) · SDL 을 따로 붙여야 합니다. 새 외부 의존 · 플랫폼 코드가
  커지므로 "남음"에 둡니다. XR 햅틱은 그대로입니다. 재지정 UI 도 게임 설정 화면 범위라 API 만 둡니다.
- **D9 전역 싱글턴은 늘리지 않습니다.** 컨텍스트 · 수정자 · 게임패드 상태는 모두 `HInputSystem`(이미 허용된 싱글턴) 안에 둡니다.

## 3. 체크리스트

### I1 게임패드 (platform + input)
- [x] `HGamepadState` · `HPadButton` · `HPadAxis` · `hgamepad::pick`(D1 · D2), `IWindow::readGamepad` 기본 구현, GLFW 구현(연결 · 해제 로그)
- [x] `pad:` 동작 · 축 소스(D3), `setGamepad` 로 가짜 장치 주입, 해제 시 `Released`. 엔진 기본 바인딩 · 샘플 `project.ini` 의 `MoveX` · `MoveY` 에 `pad:left.x` · `pad:left.y` 를 더함

### I2 수정자 (input)
- [x] `input.mod.<축>` 해석(데드존 원형 · 축별, 감도, 반전, 곡선), 기본값(D4), project.ini · 사용자 설정 둘 다

### I3 입력 컨텍스트 (input)
- [x] `input.context.<이름>.*` 줄, `pushContext` · `popContext` · `activeContexts`, 소스 단위 소비 · `blockLower`(D5)

### I4 사용자 재지정 (input + core + runtime)
- [x] `rebind` · `resetBinding` · `resetAllBindings` · `bindingLine` · 충돌 목록(D6), 정규 소스 글
- [x] `applyUserOverrides` 로 덮어쓰기 규칙 이동, 엔진은 그것을 부름(D7), 사용자 설정 접두어 더함

### I5 시험 · 회귀
- [x] `tests/InputTest.cpp`: 가짜 패드 축 · 버튼 → 동작, 데드존 · 곡선 값, 재지정 저장 → 다시 읽기 왕복, 위 컨텍스트가 아래 동작을 막음, 패드 선택 규칙
- [x] 기존 입력 시험 그대로 통과, 기준 PNG · UI 덤프 같음, Android 빌드 통과

## 4. 결과 (2026-11-21)

| 확인 | 결과 |
|---|---|
| 빌드 경고 | Release 0 · Debug 0 |
| ctest | 40/40(Release · Debug) |
| 검사 수 | 3,175 → 3,253(`xr_input` 31 → 109) |
| 변이 확인 | 컨텍스트 소비 검사를 빼면 "Menu 를 밀면 … Released 한 번" 실패, 패드 기본 데드존을 0 으로 바꾸면 데드존 검사 3개 실패 |
| 아키텍처 | `architecture_deps` PASS, 허용 목록 0, 전역 싱글턴 그대로(`HInputSystem` · `ComponentRegistry`) |
| 기준 PNG 7장 · UI 덤프 57장면 | 모두 같음(입력 경로만 바뀌고 그리는 것은 같음) |
| Android | 통과, 바꾼 파일에서 새 경고 없음(`readGamepad` 기본 구현 = 연결 없음) |

바뀐 곳: `platform/Gamepad.hpp`(새 파일) · `IWindow::readGamepad` · `GlfwWindow::readGamepad`, `input/HInputSystem`(컨텍스트 · 패드 · 수정자 ·
재지정 · `applyUserOverrides`), `runtime/Engine`(덮어쓰기 규칙을 입력 층으로 옮기고 `setProjectLines`), `core/UserSettings`(접두어
`input.mod.` · `input.context.`), `tests/InputTest.cpp`(런타임 라이브러리에 링크), 샘플 `project.ini`.

게임 코드 사용 예:

```cpp
auto& in = HInputSystem::get();
in.pushContext("Menu", 10);                 // 메뉴를 열 때. project.ini 의 input.context.Menu.* 줄이 위에 옴
in.popContext("Menu");                      // 닫을 때
HRebindResult r = in.rebind(ctx.settings, "input.action.Jump", "key:space", "key:J");
if (!r.conflicts.empty()) { /* 게임 UI 가 처리 방법을 고름 */ }
in.resetBinding(ctx.settings, "input.action.Jump");   // 기본값으로
ctx.settings.set("input.mod.LookY", "invert");        // 시선 반전(플레이어 설정)
```

### 남음

- **실제 패드 연결 확인(창):** Xbox · DualSense 패드를 꽂고 빼며 로그 · 이동을 봅니다(window-checks). 스틱 Y 부호 · 트리거 0 ~ 1 변환은
  GLFW 경로에서만 일어나므로 창에서 확인해야 합니다.
- **게임패드 진동(D8):** GLFW 에 API 가 없음. XInput(Windows) 또는 SDL 을 붙일 때 `HInputSystem::haptic` 의 받는 곳을 넓힙니다.
- **패드 시선:** 데스크톱 시선 축(`LookX` · `LookY`)은 프레임당 마우스 픽셀을 쓰므로, 패드 스틱(-1 ~ 1)을 같은 축에 묶으려면 소스별 배율과
  시간 기준 회전(`XrComponents`)이 필요합니다. 이번에는 기본 바인딩에 넣지 않았습니다.
- **재지정 UI:** 게임 설정 화면 범위. "다음에 누른 키 받기"(UE `InputKeySelector` · Unity `PerformInteractiveRebinding`)도 그때 넣습니다.
- **로컬 다인 플레이:** 패드별 플레이어 · 컨텍스트는 범위 밖(D2).
