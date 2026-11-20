# 접근성: 자막 · 글자 크기 · 비네트 · 앉은 자세 · 한 손 조작 (2026-11-21)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 14위 후보입니다. 지금은 편안함 설정(`comfort.*`: 스냅 회전 각 · 텔레포트 · 높이 모드 ·
눈높이)만 있고, 소리를 듣지 못하는 플레이어를 위한 자막, 글자를 크게 보는 배율, 부드러운 이동의 멀미를 줄이는 비네트, 앉아서 하는 플레이어의
높이 보정, 한 손으로 하는 플레이어를 위한 손 바꾸기가 없습니다. 이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고, 항목을 모두
만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-21)

| 곳 | 지금 | 문제 |
|---|---|---|
| `ComfortSettings`(core/UserSettings, 월드 서비스) | 스냅 회전 각 · 텔레포트 · 높이 모드(tracked · fixed) · 눈높이 | 앉은 자세는 "눈높이 고정" 뿐이라, 바닥 기준(실제 키) 공간에서 앉은 키와 선 키의 차이를 보정하는 길이 없음 |
| `RigLocomotionComponent` | 왼쪽 스틱 부드러운 이동 · 오른쪽 스틱 스냅 회전 · 오른손 텔레포트, 데스크톱 마우스 둘러보기 | 이동 속도를 바깥에 알리지 않음(비네트가 쓸 값이 없음), 텔레포트 손이 오른손으로 고정 |
| `HInputSystem` | 바인딩 · 수정자 · 컨텍스트 · 재지정(Docs/Input_Extensions.md) | 주 손을 바꾸려면 플레이어가 축 바인딩 네 줄을 직접 다시 써야 함 |
| `XrSystem::setRigSpace` · `rigLift` | 바닥 기준이면 원점에 0, 머리 기준이면 눈높이를 더함 | 바닥 기준에서 더할 높이(앉은 자세 보정)가 없음 |
| `UIPanelComponent` | 내용 팩토리가 배치 배율 `s` 를 받아 크기 · 여백 · 글자에 곱함 | 플레이어가 고르는 글자 배율이 없음 |
| `post.frag`(톤매핑) | AO · 블룸 · ACES · 패널 가중치 · sRGB | 화면 가장자리 어둡게(비네트) · 색 보정 단계가 없음 |
| `AudioComponent` · `HText`(HTEXT · .po) | 소리 재생, 번역 글 | 소리를 글로 보여 줄 통로(자막)가 없음 |

### 1-2. 다른 엔진 · 지침

- **Unreal:** 자막은 `USoundWave` 의 자막 목록(글 · 시각)과 `FSubtitleManager`(우선순위 · 표시 시간 · 겹침 처리), 켜기는
  `UGameplayStatics::SetSubtitlesEnabled` 입니다. 글자 크기는 Slate 의 응용 배율(`SetApplicationScale`), 색약은
  `SetColorVisionDeficiencyType(종류, 세기, 보정 여부)` 로 후처리 단계에서 Machado 시뮬레이션 · 보정을 합니다. 앉은 자세는
  `SetTrackingOrigin(Eye | Floor)` 와 `ResetOrientationAndPosition` 입니다.
- **Unity:** XR Interaction Toolkit 의 `TunnelingVignetteController`(이동 공급자가 움직이는 동안 구멍 크기 · 가장자리 부드러움 · 들어가기/나오기
  시간으로 시야를 좁힘), XR Origin 의 추적 원점 모드(Floor · Device)와 카메라 Y 오프셋, UI 는 `CanvasScaler` 배율입니다. 자막은 엔진에 없어
  게임이 만듭니다.
- **Godot:** `XROrigin3D` 와 `XRServer.center_on_hmd`(다시 맞추기). 자막 · 비네트는 없습니다.
- **Meta Quest 지침(VRC · 접근성):** 부드러운 이동에는 시야 좁히기(비네트) 선택지, 앉아서 하기 · 높이 다시 맞추기, 한 손 조작(주 손 바꾸기)을
  권합니다. 머리에 붙은 글은 시야 아래쪽에 두어 장면을 가리지 않게 합니다.
- **자막 지침(Xbox XAG 104 · BBC):** 화면 아래, 한 번에 두 줄 이하, 화자 이름 표시, 크기 · 배경 불투명도 조절, 읽는 속도 초당 약 15 자
  (분당 160 ~ 180 단어)에 맞는 표시 시간.

### 1-3. 공통 구조

"설정은 사용자 설정 층의 값 → 엔진이 월드 서비스로 컴포넌트 · 렌더 입력에 전함" 과 "자막은 큐(우선순위 · 최대 줄 수 · 표시 시간) + 머리 · 화면에
붙은 표시" 입니다. 비네트 · 색 보정은 톤매핑 뒤 화면 공간 단계이고, 앉은 자세는 추적 원점에 더하는 높이, 손 바꾸기는 입력 매핑 층의 소스 교환입니다.

## 2. 결정

- **D1 설정 이름:** 이동 · 높이에 관한 것은 `comfort.*`, 나머지는 `access.*` 입니다. 모두 바로 적용(Live)이고, 기본값이면 지금과 같은 화면 · 동작입니다.
  - `comfort.vignette`(0 ~ 1, 기본 0 = 끔), `comfort.seated`(bool, 기본 false), `comfort.standingHeight`(0 = 씬 · 프로젝트 눈높이, 그 밖에는 1.0 ~ 2.5 m).
  - `access.subtitles`(bool, 기본 false), `access.subtitleSize`(0.5 ~ 3, 기본 1), `access.subtitleBackground`(0 ~ 1, 기본 0.6),
    `access.textScale`(0.5 ~ 3, 기본 1), `access.hand`(right | left, 기본 right), `access.colorFilter`(none | protanopia | deuteranopia | tritanopia).
- **D2 순수 계산은 core:** `core/Accessibility.hpp` 에 설정 묶음 `AccessSettings`, 월드 서비스 `AccessibilityState`(비네트 상태 · 앉은 자세 보정
  · 다시 맞추기 요청), 계산 함수 `haccess::*`(비네트 목표 · 필터 · 모양, 앉은 높이, 색 행렬, 자막 표시 시간 · 줄 나누기)를 둡니다.
  `core/Subtitles.hpp` 에 자막 큐 `SubtitleQueue`(월드 서비스)를 둡니다. 다른 엔진 모듈을 보지 않아 시험이 쉽고, 소리 · 게임 코드 어디서나 씁니다.
- **D3 자막 큐:** 줄은 `HText` 글(HTEXT 키로 번역) · 화자(`HText`) · 길이(0 = 글자 수로 자동: 1.5 + 글자 수 / 15 초, 1.5 ~ 8 초) · 우선순위 · id
  입니다. 같은 id 의 줄이 오면 글을 바꾸고 시간을 다시 셉니다(같은 소리가 다시 남). 보이는 줄이 최대(기본 2)면, 새 줄의 우선순위가 보이는 줄 중
  가장 낮은 것보다 높을 때 그 줄(같으면 오래된 것)을 밀어내고, 아니면 기다립니다. 기다리는 줄은 자리가 나면 우선순위 · 도착 순서로 나오고,
  자기 길이보다 오래 기다리면 버립니다. 표시가 꺼져 있어도 큐는 돕니다(켜는 순간 지금 말하는 줄이 보임).
- **D4 자막 표시:** `components/SubtitlePanel` 이 `IRenderPanelSource` 를 구현해 3D UI 패널 경로(UIPanelPass)로 그립니다. 액터가 아니라 엔진이 가진
  월드 서비스라 씬 파일 · 아웃라이너에 나오지 않고, 추출(`RenderExtract`)이 자막이 있을 때만 패널 목록 끝에 더합니다. 자리는 엔진이 프레임마다
  정합니다: XR 세션이 머리를 주거나 합성 눈(`--synthetic-eyes`)이면 머리 앞 1.2 m, 시선 아래 15° 에 머리를 향해(머리 고정, 폭 0.9 m =
  배치 1,200 px 이라 글자 40 px 가 3 cm · 약 1.4°, 한 행 약 60 자), 아니면 화면 아래(카메라 앞 0.5 m, 화면 폭의 80 %, 아래 여백 6 %, 배율 1 의
  글자 높이 = 화면 높이의 4.5 %)입니다. 판은 배경이 투명하고 줄마다 반투명 배경(`access.subtitleBackground`)을 둡니다. 판 높이는 최대 줄 수 × 두 줄(긴 글 줄바꿈) 기준이라
  줄 수가 바뀌어도 텍스처를 다시 만들지 않습니다(크기 · 배율 설정이 바뀔 때만).
- **D5 글자 배율:** `UIPanelComponent::textScale()` 이 월드의 `AccessibilityState` 에서 `access.textScale` 을 읽고, 값이 바뀌면 트리를 다시
  만듭니다(레이아웃 다시 계산). 내용 팩토리는 글자 크기에 `s * panel.textScale()` 을 곱합니다(배치 배율과 같은 방식, 샘플 Demo 를 고침).
  자막 글자는 `access.subtitleSize × access.textScale` 입니다. 1.0 이면 같은 float 곱이라 지금 화면과 바이트 단위로 같습니다.
  위젯 측정(`computeDesiredSize`)이 문맥을 받지 않아 Slate 전역 배율은 넣지 않았습니다(전역 싱글턴 금지 규칙과도 맞음).
- **D6 비네트:** 후처리(`post.frag`)에서 톤매핑 뒤 화면 가장자리를 어둡게 합니다. 머리 고정 메시보다 단순하고 두 눈 · 거울 화면에 같은
  식이 들어가며, 깊이에 가려지지 않습니다. 세기는 이동 컴포넌트가 알린 속도(`AccessibilityState::reportMotion`: 선속도 m/s · 요 회전 °/s)에서
  `목표 = 설정 × max(선 0.1 → 2 m/s, 회전 5 → 60 °/s 선형)` 이고, 들어갈 때 0.08 초 · 나올 때 0.25 초 시간 상수로 부드럽게 합니다. 스냅 회전 ·
  텔레포트는 순간 이동이라 넣지 않습니다. 모양은 눈 화면 가운데에서의 거리 r(가장자리 1)에 대해 `1 - min(2v, 1) × smoothstep(안, 안 + 0.45, r)`,
  `안 = mix(0.95, 0.3, v)` 입니다. 값이 0 이면 셰이더가 그 단계를 건너뛰어 지금과 같은 PNG 입니다. 모양 확인용 CVar
  `comfort.vignetteDebug`(0 ~ 1)는 세기를 그 값으로 고정합니다.
- **D7 앉은 자세:** 바닥 기준 공간(LOCAL_FLOOR)에서 `comfort.seated` 가 켜지면 원점에 `max(0, 선 눈높이 − 앉은 머리 높이)`(최대 1.2 m)를 더합니다.
  선 눈높이는 `comfort.standingHeight`, 0 이면 씬 리그 · 프로젝트 눈높이입니다. 앉은 머리 높이는 켠 뒤 처음 머리 자세를 받은 프레임과
  `AccessibilityState::requestSeatedRecalibrate()`(다시 맞추기 API, 게임 · 콘솔 `comfort.recenterSeated`)를 부른 다음 프레임에 잽니다.
  머리 기준 공간(눈높이 고정)은 이미 앉든 서든 같은 높이이므로 더하지 않습니다. `XrSystem::setRigSpace` 에 더할 높이 인자를 더하고
  `rigLift` · 입력의 `floorOffset` 이 그 값을 씁니다.
- **D8 한 손 조작:** `access.hand = left` 면 `HInputSystem::setHandSwap(true)` 로 축 평가에서 XR 스틱(왼 ↔ 오른)과 패드 스틱을 맞바꿉니다.
  바인딩 글은 그대로라(재지정 · 충돌 목록 · 컨텍스트 소비는 논리 소스 기준) 손 바꾸기와 재지정이 서로 덮지 않습니다. 잡기(그립)는 손 자체의
  동작이라 바꾸지 않고, 텔레포트 포물선은 왼손에서 나갑니다.
- **D9 색 보정([선택]):** `access.colorFilter` 는 Machado(2009) 세기 1 시뮬레이션 행렬 S 와 Fidaner 식 오차 옮기기 행렬 E 로 만든 보정 행렬
  `I + E(I − S)` 를 톤매핑 뒤 선형 색에 곱합니다(UE 의 색약 보정과 같은 종류). none 이면 단계를 건너뜁니다.
- **D10 범위 밖:** 게임 설정 화면(UI), 음성 안내(TTS), 자막 패널의 깊이 무시(벽에 가까이 서면 가려질 수 있음, 오버레이 · 쿼드 레이어 필요),
  큰 대비 UI 테마([선택] 미완, 에디터 · 패널 색이 위젯마다 코드에 있어 테마 층부터 필요), 소리 큐(`.hcue`)의 자막 줄, 에디터 디테일 패널의
  자막 칸(문자열 속성 칸이 없어 씬 파일 · 코드로만 정함).

## 3. 체크리스트

### A1 자막 (core + components + runtime)

- [x] A1-1 `SubtitleQueue`: 자동 길이, 같은 id 바꾸기, 최대 줄 수 · 우선순위 밀어내기 · 기다리기 · 기다림 버리기, 시간 경과로 사라짐(시험).
- [x] A1-2 `AudioComponent` 의 자막 칸(`subtitle` · `subtitleKey` · `subtitleSpeaker`)이 재생할 때 줄을 냄, 게임 코드 API, 콘솔 `subtitle.say`.
- [x] A1-3 표시: 3D 패널 경로, XR 은 머리 고정 시선 아래, 데스크톱은 화면 아래. 켜기 · 크기 · 배경 불투명도 설정.
- [x] A1-4 창 없는 PNG: 자막을 켠 확인 장면 1장(새 기준).

### A2 글자 배율

- [x] A2-1 `access.textScale` 이 3D UI 패널(트리 다시 만듦)과 자막에 들어감, 배율 레이아웃 시험.
- [x] A2-2 기본 1.0 에서 base7 의 panel · panelR 이 같음.

### A3 비네트

- [x] A3-1 속도 → 세기, 필터, 모양 계산(시험). 이동 컴포넌트가 속도를 알림.
- [x] A3-2 후처리 적용, 세기 0 이면 같은 PNG(base7).

### A4 앉은 자세

- [x] A4-1 앉은 높이 계산 · 다시 맞추기(시험), `XrSystem` 원점 높이에 들어감.

### A5 한 손 조작

- [x] A5-1 손 바꾸기가 이동 · 회전 · 텔레포트 축을 맞바꿈(시험), 텔레포트 손.

### A6 설정

- [x] A6-1 모든 새 설정이 `comfort.*` · `access.*` 로 저장 · 다시 읽기 왕복(시험), 엔진 시작 · 실행 중 적용(로그).

### A7 [선택]

- [x] A7-1 색약 보정 필터(후처리 행렬).
- [ ] A7-2 큰 대비 UI 테마.

### G 공통

- [x] G-1 Release · Debug 경고 0, ctest 전부 통과(새 시험 포함), 아키텍처 PASS.
- [x] G-2 기준 PNG 7장 · UI 덤프 그대로.
- [x] G-3 Android 빌드 통과.
- [ ] G-4 (헤드셋) 자막 위치 · 비네트 느낌 · 앉은 자세 높이 · 왼손 이동.

## 4. 결과 (2026-11-21)

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0 |
| ctest | 두 구성 41/41 통과, 검사 3,253 → 3,364(새 `accessibility` 111, 나머지 같음) |
| 변이 확인 | 축 평가의 손 바꾸기를 빼면 2 실패, 패널의 글자 배율 다시 만들기 조건을 빼면 2 실패 |
| 아키텍처 | `architecture_deps` PASS. 순수 계산 · 큐는 core, 표시는 components, 엔진 연결은 runtime. 정적 시스템 포인터 · 새 전역 싱글턴 없음(모두 월드 서비스) |
| 기준 PNG 7장(`base7.sh b_07`, GPU 1) | 모두 그대로(ce6def24 · 73dd1f02 · c283f276 · b4bd1fee · 5d6e3e5f · 9cf28353 · 201fad0e), 검증 오류 0. 셰이더 · 패널이 바뀌어도 기본 설정이면 같은 화소 |
| UI 덤프(`uidump.sh b_07 b_04`) | 57장면 모두 UI · 뷰포트 같음 |
| 자막 확인 PNG(새 기준) | 아래 명령, 1024², 검증 오류 0, 해시 `3837602d`(두 번 그려 같음). 판이 시선 아래에 보이고 긴 줄은 두 행으로 나뉨 |
| 비네트 · 색 보정 확인 | `--cvar=comfort.vignetteDebug=0.6 --user-setting=access.colorFilter=deuteranopia` 로 가장자리가 어두워지고 빨강이 옮겨짐(눈 확인, 기준 아님) |
| Android | `build/android` 빌드 통과. 바꾼 줄에서 나온 경고 없음(남은 경고는 기존 헤더 · `XrSystem.cpp` 의 것) |

자막 확인 PNG 명령(`build/Release` 에서, `MSYS2_ARG_CONV_EXCL="--write-root=;--console-at=;--capture="`):

```
HarukaGame.exe --headless --no-xr --gpu=1 --synthetic-eyes=1024x1024 --frames=60 --write-root=<임시> \
  --user-setting=access.subtitles=true '--console-at=1:subtitle.say "Welcome aboard. The station is waiting for you." Captain 30' \
  --capture=<경로>/sub.png
```

**새 파일:** `src/core/Accessibility.hpp/.cpp`(`AccessSettings` · `AccessibilityState` · `haccess::*`), `src/core/Subtitles.hpp/.cpp`(`SubtitleQueue`),
`src/components/SubtitlePanel.hpp/.cpp`, `tests/AccessibilityTest.cpp`.

**바꾼 곳:** `UserSettings`(새 이름 9 개, `ComfortSettings` 에 비네트 · 앉은 자세 · 선 눈높이), `HInputSystem`(`setHandSwap` · `swapHands`),
`RigLocomotionComponent`(속도 알림 · 텔레포트 손), `UIPanelComponent`(`textScale` · 다시 만들기), `AudioComponent` · `AudioSystem`(자막 칸 · 시작할 때 줄),
`RenderSceneInput` · `RenderExtract`(비네트 · 색 보정 · 자막 판), `Renderer` · `PostPass` · `post.frag`(푸시 상수 40 → 96 바이트),
`XrSystem::setRigSpace`(바닥 기준 더할 높이) · `XrViewOrigin`, `Engine`(월드 서비스 3 개 · 적용 로그 · 콘솔 `subtitle.say` · `subtitle.clear` ·
`comfort.recenterSeated` · CVar `comfort.vignetteDebug`), 샘플 Demo 패널(글자에 `textScale`), CMake(`HarukaAccessTest`, `add_test accessibility`).

**쓰는 법(게임 모듈):**

```cpp
// 자막: 번역 키로 줄을 넣습니다. 길이 0 = 글자 수로 자동, 같은 id 는 줄을 바꿈.
if (auto* subs = ctx.world.service<SubtitleQueue>()) {
    SubtitleLine l;
    l.text    = HTEXT("Game", "Intro_01", "Welcome aboard.");
    l.speaker = HTEXT("Game", "Captain", "Captain");
    l.id      = "intro";
    subs->push(std::move(l));
}
// 소리에 붙이기(씬 파일): subtitle=The door opens. · subtitleKey=Game:DoorOpens · subtitleSpeaker=Door

// 3D UI 패널 내용: 글자 크기에 플레이어 배율을 곱합니다.
UIPanelComponent::registerContent("Hud", [](UIPanelComponent& panel, float s) {
    auto t = std::make_shared<STextBlock>();
    t->text("Oxygen")->fontSize(32.f * s * panel.textScale());
    return t;
});

// 앉은 자세 다시 맞추기(설정 화면의 버튼 등)
if (auto* acc = ctx.world.service<AccessibilityState>()) acc->requestSeatedRecalibrate();
```

**남음:**
- (헤드셋, G-4) 자막 판의 위치 · 크기 · 읽기 편함, 비네트 세기 느낌(Quest 화면 비율에서 원 모양), 앉은 자세 높이 · 다시 맞추기, 왼손 이동 · 텔레포트.
- 자막 판이 장면 깊이에 가려지는 문제(벽에 가까이 서면): 깊이 무시 패널 파이프라인 또는 OpenXR 쿼드 레이어(3D UI P4).
- 큰 대비 UI 테마(A7-2), 에디터 디테일 패널의 자막 칸, `.hcue` 큐의 자막 줄, 게임 설정 화면 · 음성 안내(범위 밖).
