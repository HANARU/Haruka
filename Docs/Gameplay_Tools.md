# 게임플레이 기본 도구 · CVar · 콘솔 (2026-11-20)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 4위 후보입니다. 지금은 컴포넌트 수명 함수 · `HEvent` · 월드 서비스 · `HActorRef` 는
있지만, 게임 코드가 매번 직접 만들어야 하는 기본 도구(시간 배율 · 타이머 · 태그 · 이벤트 버스 · 물리 뒤 틱)와, 실행 중에 이름으로 값을
바꾸는 변수(CVar) · 콘솔이 없습니다. 이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고, 항목을 모두 만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-20)

| 곳 | 지금 | 문제 |
|---|---|---|
| `World::tick(dt)` | 액터를 스폰 순서로, 컴포넌트를 붙은 순서로 틱 → 트윈 → 지연 삭제 | 시간 배율 · 게임 시간이 없음. 모든 컴포넌트가 물리 앞에서만 틱 |
| `Engine::tickFrame` | 월드 틱 → 게임 `onTick` → 모듈(물리 · 소리) | 물리 결과를 따라가는 컴포넌트(카메라 등)가 한 프레임 늦음 |
| `Engine::readDesktopInput` | `dt = min(실제 경과, 0.05)`, `--fixed-dt` | 상한이 상수 |
| 일시정지 | 에디터 Pause · 헤드셋 초점 잃음 → 월드 · 모듈 틱을 건너뜀 | 그대로 둠(타이머도 이것을 따르면 됨) |
| 타이머 · 지연 호출 | 없음(트윈만 `World::tweens()`) | 게임마다 경과 시간을 직접 셈 |
| 태그 | 없음(액터 필드는 이름 · id · guid · 부모 · 활성) | 종류로 찾으려면 이름 비교나 컴포넌트 형 |
| 이벤트 | `HEvent`(리스너 직접 add · remove), `World::onActorSpawned` · `onActorDestroyed` | 보내는 쪽 · 받는 쪽이 서로 알아야 함 |
| CVar · 콘솔 | 없음. 명령줄 옵션 약 40개 · ini 는 시작할 때만 | 실행 중에 디버그 값을 바꿀 길이 없음 |
| 전역 접근 규칙(S3) | 정적 시스템 포인터 0, 전역 싱글턴은 `HInputSystem` · `ComponentRegistry` 만 | CVar 목록을 새 싱글턴으로 두면 규칙 위반 |
| Output Log 탭(에디터) | 검색 칸 하나, 입력 칸 포커스는 `TextInputController` 가 중재 | 명령 입력 줄이 없음 |

### 1-2. 다른 엔진

- **Unreal:** `FTimerManager`(월드 단위, 게임 시간, 일시정지 · 시간 배율을 따름, 객체가 사라지면 그 타이머 무효), `UWorld::GetTimeSeconds`,
  `AWorldSettings::TimeDilation`, 액터 · 컴포넌트 `Tags`(FName 배열), 틱 그룹(`TG_PrePhysics` · `TG_PostPhysics` 등), 콘솔 변수
  `IConsoleManager`(이름 · 도움말 · 형 · 설정한 곳의 우선순위 · 변경 콜백, ini `[SystemSettings]` · 명령줄 `-ExecCmds` · `~` 콘솔).
- **Unity:** `Time.timeScale` · `Time.time`, 코루틴 · `Invoke`(일시정지 = timeScale 0 을 따름), `GameObject.tag` · `FindGameObjectsWithTag`,
  스크립트 실행 순서 · `LateUpdate`(모든 Update 뒤), 콘솔은 없음(개발 콘솔은 에셋).
- **Godot:** `Engine.time_scale`, `SceneTree.create_timer` · `Timer` 노드(일시정지 모드), 그룹(`add_to_group` · `get_nodes_in_group`),
  `process_priority` · `_physics_process`, 시그널(이벤트), 프로젝트 설정을 실행 중에 바꾸는 디버거.

### 1-3. 공통 구조

"월드가 게임 시간(배율 · 일시정지 적용)을 소유 → 타이머 · 트윈 · 컴포넌트가 그 시간을 씀 → 객체가 사라지면 그 객체의 예약도 사라짐",
"태그 · 그룹 질의는 색인으로", "물리 뒤에 한 번 더 도는 틱 단계", "이름 · 형 · 도움말이 있는 변수 표를 ini · 명령줄 · 콘솔이 같은 이름으로 바꿈".

## 2. 결정

- **D1 게임 시간:** `World` 가 시간 배율(`setTimeScale`, 0 ~ 100, 기본 1)과 게임 시간(`gameTime()`, 배율을 적용한 누적 초)을 듭니다.
  `World::tick(dt)` 은 받은 실제 dt 에 배율을 곱해 컴포넌트 · 트윈 · 타이머에 주고, 엔진은 그 값(`lastDt()`)을 게임 `onTick` 과 모듈(물리 ·
  소리)에도 줍니다. 배율 1 이면 예전과 같은 값입니다(기준 PNG 그대로). 일시정지는 예전처럼 틱을 건너뜁니다.
- **D2 타이머 `World::timers()`:** `after(초, fn, 주인 액터)` · `every(간격, fn, 주인, 첫 지연)` · `cancel` · `active` · `remaining`. 게임 시간으로
  셉니다. 주인 액터가 지워지면 함께 취소합니다(트윈과 같은 자리). 한 틱에 간격이 여러 번 지나면 지난 만큼 부르되 틱당 16 번까지이고,
  예정 시각을 기준으로 다음 시각을 정해 밀리지 않습니다. 콜백 안의 추가 · 취소는 안전합니다. 에디터 Play 가 끝나면 모두 지웁니다.
- **D3 태그:** 액터에 태그 목록(`addTag` · `removeTag` · `hasTag` · `tags`), `World::actorsWithTag(tag)` 는 구조 버전 · 태그 버전이 같으면
  지난 결과를 돌려줍니다(프레임마다 훑지 않음). 태그 이름은 `[A-Za-z0-9_.-]`, 대소문자를 가립니다. 씬 파일에는 태그가 있을 때만 `TAGS:a,b`
  줄을 씁니다(태그 없는 씬은 예전과 같은 글). 옛 실행 파일은 모르는 줄을 건너뜁니다.
- **D4 이벤트 버스 `World::events()`:** 형으로 구독 · 발행합니다(`subscribe<T>(fn)` → 이동만 되는 구독 객체, 지우면 해제, `publish<T>(e)` 는
  바로 부름). 발행 중 구독 · 해제는 안전합니다(해제는 표시만 하고 끝나면 정리). 구독 순서대로 부릅니다.
- **D5 물리 뒤 틱:** `Component::tickGroup()`(기본 `PrePhysics`)이 `PostPhysics` 이면 모듈 틱 뒤의 `World::tickPostPhysics()` 에서 틱합니다.
  지금 컴포넌트는 모두 `PrePhysics` 라 순서가 바뀌지 않습니다. 물리 뒤 컴포넌트가 하나도 없으면 두 번째 훑기를 하지 않습니다.
- **D6 CVar `CVarRegistry`(core):** 이름 · 형(bool · int · float · string) · 범위 · 도움말 · 기본값 · 마지막으로 바꾼 곳(기본 · ini · 명령줄 ·
  콘솔 · 코드). 이름은 `[A-Za-z0-9_.]`, 찾을 때 대소문자를 가리지 않습니다. **엔진 단위**입니다(Engine 이 소유, `EngineContext::cvars`, 월드
  서비스 `world.service<CVarRegistry>()`). S3 규칙대로 새 전역 싱글턴을 만들지 않습니다. 등록 전에 들어온 값(명령줄 · ini)은 보관했다가
  등록할 때 적용하고, 시작이 끝났는데도 등록되지 않은 이름은 알립니다. 값은 바로 바뀌고, 변경 콜백은 다음 프레임 처음에 부릅니다.
- **D7 값을 바꾸는 길:** ini `<쓰기 루트>/Config/cvars.ini`(`이름=값`), 명령줄 `--cvar=<이름>=<값>`(ini 다음, 여러 번), 콘솔 명령 `<이름> <값>`
  (게임 코드는 `ctx.cvars.set`). 콘솔 명령: `<이름>`(값 · 도움말), `<이름> <값>`, `help [이름]`, `list [앞부분]`, `reset <이름>`, 게임 · 엔진이
  등록한 명령(`addCommand`). 사용자 설정(`UserSettings`, 플레이어 값 · 저장됨)과는 다른 것입니다: CVar 는 개발 · 디버그 값이고 저장하지 않습니다.
- **D8 엔진 CVar(1차):** `time.scale`(0 ~ 100, 월드 시간 배율), `time.maxDt`(0.005 ~ 0.25, 기본 0.05, 프레임 dt 상한), `debug.lines`(디버그 선을
  그림). 엔진 명령: `scene <경로>`(게임 실행 파일의 씬 전환).
- **D9 콘솔 화면:** 에디터 Output Log 탭 아래에 명령 입력 줄(`>`)을 둡니다(UE 와 같은 자리). Enter 로 실행하고 결과는 로그(`Console` 분류)에
  남깁니다. 위 · 아래 화살표로 지난 명령을 고릅니다. 개발 스위치 `--console-at=<프레임>:<명령>`(여러 번)으로 창 없이 같은 길을 씁니다.
- **D10 범위 밖:** 헤드셋 안 콘솔 패널, 컴포넌트 단위 BeginPlay · EndPlay(지금 `onStart` = 첫 틱, `onDestroy` = 끝이 그 자리), 틱 그룹 더 잘게
  (DuringPhysics 등), 이벤트 지연 발행 큐, C++ 핫리로드 · 스크립트 계층, 에디터 디테일 패널의 태그 편집 칸(씬 파일과 코드로 씀),
  CVar 값 저장 · 치트 플래그, 콘솔 자동 완성.

## 3. 체크리스트

### T1 게임 시간 · 타이머 · 틱 (scene)

- [x] T1-1 배율 1 이면 컴포넌트 dt 가 받은 dt 와 같음, 0.5 면 반, 0 이면 0(onStart 는 불림). 게임 시간 누적.
- [x] T1-2 `after` 가 게임 시간으로 정확한 틱에 한 번, 배율 0.5 면 두 배 늦게, 틱하지 않으면(일시정지) 부르지 않음.
- [x] T1-3 `every` 가 큰 dt 에서 지난 횟수만큼(16 번 상한), 예정 시각 기준으로 밀리지 않음, 첫 지연.
- [x] T1-4 주인 액터를 지우면 취소, 콜백 안에서 자기 취소 · 새 타이머 · 액터 삭제가 안전, `cancel` · `active` · `remaining`.
- [x] T1-5 `PostPhysics` 컴포넌트는 `tickPostPhysics` 에서만 틱하고 `onStart` 를 한 번 받음, 없으면 두 번째 훑기 생략.
- [x] T1-6 엔진: 게임 `onTick` · 모듈이 배율 적용 dt 를 받음, 에디터 Play 끝에 타이머를 지움.

### T2 태그 · 이벤트 버스 (scene)

- [x] T2-1 `addTag` 중복 무시 · 틀린 이름 거절 · `removeTag` · `hasTag`.
- [x] T2-2 `actorsWithTag` 결과 · 캐시(같은 버전이면 같은 벡터, 태그 · 스폰 · 삭제로 다시 만듦).
- [x] T2-3 씬 파일 `TAGS:` 왕복, 태그 없는 씬의 글은 예전과 같음.
- [x] T2-4 이벤트 버스: 형별 전달, 구독 객체를 지우면 해제, 발행 중 해제 · 구독 · 재발행이 안전, 구독 순서.

### T3 CVar · 콘솔 (core + runtime + editor)

- [x] T3-1 등록 · 형 · 범위(잘라 알림) · 틀린 값 거절 · 대소문자 무시 찾기 · 중복 등록 거절.
- [x] T3-2 등록 전 값 보관 → 등록할 때 적용, 끝까지 모르는 이름 알림. 바꾼 곳 기록.
- [x] T3-3 변경 콜백은 `dispatchChanges` 에서 한 번(같은 값은 부르지 않음).
- [x] T3-4 콘솔 명령: 값 보기 · 바꾸기 · help · list · reset · 등록 명령 · 모르는 명령 · 따옴표 인자.
- [x] T3-5 엔진: ini · 명령줄 · 콘솔(`--console-at`)이 같은 이름을 바꾸고 다음 프레임 로그에 반영.
- [x] T3-6 `time.scale` 이 실제 움직임에 반영: `AnimProbe` 를 `--fixed-dt` 로 배율 0.5 · 60 프레임과 배율 1 · 30 프레임의 그림이 같은 자세.
- [x] T3-7 에디터 Output Log 의 콘솔 줄: `--ui-console=<명령>` 으로 입력 · 실행되고 결과가 로그에 보임(UI 덤프), 위 · 아래 화살표 기록.

### G 공통

- [x] G-1 Release · Debug 경고 0, ctest 전부 통과(새 시험 포함), 아키텍처 PASS(정적 포인터 0, 싱글턴 목록 그대로).
- [x] G-2 기준 PNG 7장 그대로. UI 덤프는 Output Log 탭이 보이는 장면만 콘솔 줄만큼 바뀌고 나머지는 같음.
- [x] G-3 Android 빌드 통과.

## 4. 결과 (2026-11-20)

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0. Android 빌드 통과(바꾼 파일의 새 경고 0, 남은 경고는 예전 것) |
| ctest | 두 구성 35/35 통과, 검사 2,802(새 `gameplay_tools` 84, `headless_render` 에 명령줄 10) |
| 아키텍처 | `HarukaArchTest` PASS. 정적 포인터 0, 전역 싱글턴은 예전 둘 그대로(`CVarRegistry` 는 Engine 이 소유하는 엔진 단위 객체). `TimerManager` · `EventBus` 는 scene, `CVar` 는 core |
| 기준 PNG 7장(`base7.sh gpt`) | 모두 그대로(배율 1 의 dt 는 예전과 비트가 같음), 검증 오류 0 |
| 시간 배율의 실제 효과(T3-6) | `AnimProbe` 를 `--fixed-dt=0.016667` 로 그림: 배율 1 · 30 프레임 = 예전 기준(17ac81a4). 배율 0.5 · 60 프레임은 그것과 2 픽셀 · 최대 2/255 차이(실수 누적의 반올림), 대조로 배율 1 · 60 프레임은 35,706 픽셀이 다름 |
| ini · 명령줄 · 콘솔(T3-5) | `Config/cvars.ini` 의 `time.scale=0.75` → `--cvar=time.scale=1` 이 이김, 등록되지 않은 `game.unknown` 은 시작 끝에 경고 한 번, `--console-at` 으로 `time.scale 2` → 다음 프레임 `Time scale: 2 (cvar time.scale)`, `list time.` · `scene <경로>`(게임 실행 파일에서 전환 시작) · 모르는 명령 |
| UI 덤프(`uidump.sh gpt sav`) | 뷰포트는 54장면 모두 같음. UI 는 콘텐츠 · GPU 탭 4장면이 같고, Output Log 탭이 보이는 50장면은 맨 아래 콘솔 줄(`>` · 안내 글)이 생기고 로그 칸이 한 줄 줄어 보이는 줄이 하나씩 밀림(그 밖의 차이 없음). 새 기준 tag `gpt` |
| 콘솔 줄(T3-7) | 새 덤프 장면 3개: `console`(두 명령의 결과가 Console 분류 줄로 보임, 다음 프레임 Engine 줄), `console_recall`(위 화살표 두 번 → 칸에 `time.scale 0.5`), `console_ko`(안내 글 "콘솔 명령 입력(help)"). 상태 표시줄 `KBD: Console` |
| 글자 표 | 새 키 `Editor.OutputLog,ConsoleHint` 를 `gather-text` 로 `Engine.pot` 에, `ko/Engine.po` 에 번역(글자 시험의 "ko 표가 소스 키를 모두 덮음") |

**새 파일:** `src/scene/TimerManager.hpp/.cpp`, `src/scene/EventBus.hpp`, `src/core/CVar.hpp/.cpp`, `tests/GameplayTest.cpp`.

**바꾼 곳:** `World`(시간 배율 · 게임 시간 · `lastDt` · `tickPostPhysics` · 타이머 · 이벤트 버스 · 태그 색인), `Actor`(태그, 틱 단계별 `tick`),
`Component`(`tickGroup`), `SceneSerializer`(`TAGS:`), `Engine`(배율 적용 dt 를 게임 · 모듈에, 물리 뒤 틱, Play 끝 타이머 정리, CVar 3개 · `scene` 명령,
`--cvar` · `--console-at`, `time.maxDt`), `EngineOptions`, `EngineContext::cvars`, 에디터(`OutputLogPanel` 콘솔 줄 · 기록, `TextInputController` 의
`Console` 포커스 · 위아래 화살표, 상태 표시줄, `--ui-console` · `--ui-console-recall`), `IWindow`(`KEY_UP` · `KEY_DOWN`), 글자 표, `uidump.sh`.

**쓰는 법(게임 모듈 · 컴포넌트):**

```cpp
// 타이머: 2 초 뒤 문 열기(주인 액터가 지워지면 취소), 0.5 초마다 생성
world.timers().after(2.f, [this] { openDoor(); }, owner);
TimerHandle wave = world.timers().every(0.5f, [this] { spawn(); }, owner);
world.timers().cancel(wave);

// 시간 배율(슬로 모션), 게임 시간
world.setTimeScale(0.3f);
const double t = world.gameTime();

// 태그
door->addTag("door");
for (Actor* e : world.actorsWithTag("enemy")) { ... }   // 색인(프레임마다 훑지 않음)

// 이벤트 버스
struct ScoreChanged { int score; };
EventSubscription sub = world.events().subscribe<ScoreChanged>([](const ScoreChanged& e) { ... });   // 멤버로 두면 함께 해제
world.events().publish(ScoreChanged{ 10 });

// 물리 뒤 틱(카메라 따라가기 등): 생성자에서
tickGroup = ETickGroup::PostPhysics;

// CVar · 콘솔 명령(onStart)
auto* god = ctx.cvars.addBool("game.god", false, "Player takes no damage");
ctx.cvars.addCommand("give", "give <item>", [](const std::vector<std::string>& a) { return std::string("ok"); });
```

**남긴 것(범위 밖 · 다음 후보):** 헤드셋 안 콘솔 패널, 컴포넌트 단위 BeginPlay · EndPlay, 틱 그룹 더 잘게, 이벤트 지연 발행 큐, 스크립트 계층,
디테일 패널의 태그 편집 칸, CVar 저장 · 치트 플래그 · 자동 완성. 콜백 안에서 더한 타이머는 0 초여도 다음 틱에 부릅니다(문서화한 동작).
