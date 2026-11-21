# 저장 게임 · 사용자 설정 (2026-11-20)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 3위 후보입니다. 지금은 쓰기 루트(`happ::writeRoot`)와 기술 한도 파일
(`Config/engine.ini`), 에디터 상태 파일만 있고, 게임이 진행 상황을 남길 저장 슬롯과 플레이어가 고른 값(소리 크기 · 그래픽 · 키 · 편안함)을
보존하는 곳이 없습니다. 이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고, 항목을 모두 만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-20)

| 곳 | 지금 | 문제 |
|---|---|---|
| `hfile::writeAtomic` · `writeAtomicBinary` (core/FileUtil.hpp) | 같은 폴더의 `.tmp` 에 쓴 뒤 rename | 디스크로 내보내기(fsync)가 없어 전원이 끊기면 바꾼 파일이 비어 있을 수 있고, 손상을 알아챌 방법이 없음 |
| `AudioSystem::setMasterVolume` · `setBusVolume` (버스 5개: sfx · music · voice · ambient · ui) | API 만 있음, 0 ~ 2 | 부르는 곳도 저장하는 곳도 없음 |
| `HInputSystem::applyBinding` · `project.ini` 의 `input.action.*` · `input.axis.*` | 시작할 때 프로젝트 줄(없으면 엔진 기본값)을 적용 | 플레이어가 바꾼 키를 남길 곳이 없음 |
| `project.ini` 의 `render.*` 품질 키 → `Renderer::setQuality` · `setPostFeatures` | 시작할 때 한 번. AO · 블룸은 프레임마다 읽음(언제든 바꿀 수 있음), 그림자 단 수 · 거리는 패스를 만들 때 정함 | 플레이어 쪽 품질 값이 없음 |
| `RigLocomotionComponent`(스냅 회전 각 · 텔레포트) · `XrRigComponent`(높이 모드 · 눈높이) | 씬 컴포넌트 필드 | 앉아서 하는 플레이어가 눈높이를 바꾸거나 회전 각을 고를 수 없음 |
| `EngineContext` | world · assets · window · modules · render · components · requestScene | 저장 · 설정 통로가 없음 |
| 씬 GUID(`HGuid`, 16자리 hex) · `HActorRef`(`actor:<hex>`) | 씬 파일을 `GuidPolicy::Keep` 으로 읽으면 GUID 가 그대로 | 저장 게임이 액터를 가리킬 때 이것을 그대로 쓸 수 있음 |

### 1-2. 다른 엔진

- **Unreal:** `USaveGame` 하위 클래스(게임이 필드를 정함)를 `UGameplayStatics::SaveGameToSlot(slot, userIndex)` 로 직렬화합니다. 플랫폼
  저장소(`ISaveGameSystem`)가 슬롯 파일을 맡고, 버전은 파일 머리의 엔진 · 패키지 버전과 게임이 정한 커스텀 버전입니다. 사용자 설정은
  `UGameUserSettings`(해상도 · 품질 단계 · vsync)를 `GameUserSettings.ini` 에, 키 재지정은 Enhanced Input 의 사용자 설정에 둡니다.
  **바뀐 값만** ini 에 쓰고 나머지는 기본 ini 를 따릅니다.
- **Unity:** `PlayerPrefs`(키 · 값, 플랫폼 레지스트리 · plist)와 게임이 직접 쓰는 JSON 파일(`Application.persistentDataPath`). 원자적 쓰기 ·
  백업은 게임 몫입니다. 키 재지정은 Input System 의 `SaveBindingOverridesAsJson`(**덮어쓴 것만** 저장).
- **Godot:** `ConfigFile`(섹션 · 키 · 값 텍스트)과 `FileAccess`(`user://`), 프로젝트 설정 위의 `override.cfg`. 저장 파일 쓰기는
  임시 파일 뒤 이름 바꾸기입니다.
- **콘솔 · Quest 지침:** 저장 도중 전원이 꺼져도 이전 저장본이 남아야 하고(Xbox · PlayStation 인증 요구), 손상된 저장 파일을 읽어도
  멈추지 않고 알려야 합니다. Quest 는 앱 전용 폴더에 쓰고, 클라우드 백업은 그 폴더를 그대로 올립니다.

### 1-3. 공통 구조

"게임이 정한 값 묶음 → 슬롯 이름으로 원자적 쓰기(이전 저장본 보존) → 읽을 때 손상 검사 · 버전 확인, 없는 필드는 기본값" 과
"설정은 기본값(프로젝트) 위에 사용자가 **바꾼 것만** 덮는 층" 입니다. 엔진의 기존 텍스트 형식(`이름=값` 줄, 씬 · engine.ini)과 맞습니다.

## 2. 결정

- **D1 저장 파일 형식:** UTF-8 텍스트 `이름=값` 줄입니다. 첫 줄 `# Haruka Save v1`, 메타 줄(`@dataVersion` · `@title` · `@savedAt`),
  값 줄, 마지막 줄 `@crc=<8자리 hex>`(그 앞 모든 바이트의 CRC-32)입니다. 씬 파일처럼 사람이 읽고 비교할 수 있고, CRC 가 잘림 · 손상을
  잡습니다. 값의 줄바꿈 · 역슬래시는 `\n` · `\r` · `\\` 로 씁니다. 이름은 `[A-Za-z0-9_.:/-]` 만 됩니다.
- **D2 원자적 쓰기와 이전 저장본:** `<슬롯>.hsave.tmp` 에 다 쓰고 디스크로 내보낸 뒤(Windows `_commit`, POSIX `fsync`), 원래 파일이
  온전하면 `<슬롯>.hsave.bak` 으로 옮기고 임시 파일을 `<슬롯>.hsave` 로 옮깁니다. 읽을 때는 본 파일이 없거나 CRC 가 틀리면 `.bak` 을
  읽고 그렇다고 알립니다. 두 이름 바꾸기 사이에서 끊겨도 `.bak` 이 남습니다.
- **D3 값 묶음 `SaveData`:** 형이 있는 접근자(int · float · bool · string · vec3 · `HGuid`)입니다. 없는 이름이나 형이 맞지 않는 값은 호출자가
  준 기본값을 돌려줍니다. float 는 `floatToText`(다시 읽으면 같은 값)로 씁니다. 액터 참조는 `HActorRef::toText` 와 같은 `actor:<hex>` 입니다.
- **D4 버전:** 형식 버전(파일 머리 `v1`)은 엔진이, 데이터 버전(`@dataVersion`)은 게임이 정합니다. 엔진은 더 새 형식 버전 파일을 거절하고
  이유를 돌려줍니다. 데이터 버전이 낮은 파일은 그대로 읽고(없는 필드는 기본값), 게임은 `dataVersion()` 을 보고 옮깁니다.
- **D5 슬롯:** `SaveStore`(core) 가 `<쓰기 루트>/Saves/` 의 `<슬롯>.hsave` 를 맡습니다. 슬롯 이름은 `[a-z0-9_-]` 1 ~ 64 자입니다(Windows 는
  대소문자를 가리지 않으므로 소문자만). `save` · `load` · `exists` · `remove` · `list`(이름 · 제목 · 시각 · 데이터 버전) 입니다.
- **D6 사용자 설정 층 `UserSettings`:** `<쓰기 루트>/Config/user_settings.ini`, engine.ini 와 같은 `이름=값` 줄입니다. **바꾼 값만** 씁니다
  (프로젝트 기본값이 바뀌면 따라감). 알려진 이름은 형 · 범위 · 적용 시점(바로 · 다시 시작)이 정해져 있고, 범위 밖은 잘라 알립니다.
  `game.*` 는 게임이 자유롭게 씁니다. 모르는 이름은 알리고 버리지 않습니다(새 버전의 키를 옛 실행 파일이 지우지 않게). 파일은 값을
  처음 바꿀 때 만듭니다(기준 PNG · UI 덤프 실행에 영향 없음).
- **D7 설정 이름(1차):**
  - 소리(바로): `audio.master` · `audio.sfx` · `audio.music` · `audio.voice` · `audio.ambient` · `audio.ui`, 0 ~ 2(API 범위).
  - 그래픽: `render.ao` · `render.bloom`(바로), `render.csmCascades`(1 | 2) · `render.shadowDistance`(8 ~ 200) · `render.contactShadows` ·
    `render.heightFog`(다시 시작). 프로젝트 값 위에 덮습니다.
  - 입력(바로): `input.action.<이름>` · `input.axis.<이름>`, project.ini 와 같은 글. 같은 이름의 프로젝트 바인딩을 대신하고, 틀린 글은
    알리고 프로젝트 바인딩을 씁니다. 2026-11-21 부터 `input.mod.<축>`(수정자) · `input.context.<컨텍스트>.*` 도 받고, 이 규칙은
    `HInputSystem::applyUserOverrides` 에 있습니다. 동작 하나의 키만 바꾸는 `rebind` 는 [Input_Extensions.md](Input_Extensions.md) D6.
  - 편안함(바로): `comfort.snapTurn`(0 = 씬 값, 5 ~ 180 도), `comfort.teleport`(scene | on | off), `comfort.height`(scene | tracked | fixed),
    `comfort.eyeHeight`(0 = 씬 값, 0.5 ~ 2.5 m). 씬 · 프로젝트 값보다 앞섭니다.
- **D8 적용:** 엔진이 시작할 때 다시 시작 항목을 포함해 전부 적용하고, 그 뒤에는 설정의 변경 번호가 바뀐 프레임에 바로 적용 항목만
  다시 적용합니다. 다시 시작 항목이 바뀌면 한 번 알립니다. 바뀐 설정은 마지막 변경 1 초 뒤와 종료할 때 저장합니다(슬라이더를 끄는 동안
  매 프레임 쓰지 않음). 편안함 값은 월드 서비스 `ComfortSettings` 로 컴포넌트에 전합니다(정적 포인터 없음, S3 규칙).
- **D9 게임 통로:** `EngineContext::settings`(UserSettings&) · `EngineContext::saves`(SaveStore&).
- **D10 개발 스위치:** `--user-setting=<이름>=<값>`(여러 번) 은 시작할 때 값을 바꾸고 저장합니다. 다시 시작 확인에 씁니다.
  `--user-setting-at=<프레임>:<이름>=<값>`(여러 번) 은 그 프레임을 그린 뒤 실행 중에 바꿉니다(게임 코드가 `ctx.settings.set` 을 부르는 것과
  같은 길, S3-6 확인용). 두 스위치 모두 쓰기 루트의 파일을 바꾸므로 확인할 때는 `--write-root=<임시 폴더>` 와 함께 씁니다.
- **D11 범위 밖:** 게임 설정 화면(UI), 해상도 배율 · vsync · FPS 제한(엔진에 값 자체가 없음), 부드러운 회전 · 비녜트(이동 컴포넌트에
  기능이 없음), 액터 상태 자동 스냅숏(어느 값을 남길지는 게임이 정함), 클라우드 저장, 저장 파일 암호화.

## 3. 체크리스트

### S1 저장 파일 (core)

- [x] S1-1 `SaveData` 쓰기 → 읽기 왕복: int · float(가장 짧은 십진수, 비트 같음) · bool · 줄바꿈 · 역슬래시 · `=` 가 든 문자열 · vec3 · GUID.
- [x] S1-2 CRC: 바이트 하나를 바꾸거나 끝을 자른 파일은 거절하고 이유를 돌려줌.
- [x] S1-3 더 새 형식 버전은 거절, 낮은 데이터 버전은 읽고 `dataVersion()` 이 그 값, 없는 필드는 기본값.
- [x] S1-4 형이 틀린 값(`"abc"` 를 int 로)은 기본값.

### S2 슬롯 (core)

- [x] S2-1 슬롯 이름 규칙(대문자 · 점 · 경로 · 빈 이름 · 65자 거절).
- [x] S2-2 저장 → 새 `SaveStore`(다시 시작과 같음) → 불러오기 왕복.
- [x] S2-3 두 번째 저장 뒤 `.bak` 이 첫 저장 내용. 본 파일을 자르면 `.bak` 을 읽고 "백업에서 읽음" 을 알림. 본 파일이 없고 `.bak` 만 있어도 읽음.
- [x] S2-4 남은 `.tmp` 는 읽지 않고 다음 저장이 덮음. `list` 는 `.hsave` 만, 제목 · 데이터 버전 · 시각을 돌려줌. `remove` 는 셋 다 지움.
- [x] S2-5 저장된 `actor:` 참조를, GUID 를 지키며 다시 읽은 씬(`GuidPolicy::Keep`)에서 찾음(값 · GUID 참조 유지).

### S3 사용자 설정 (core + runtime)

- [x] S3-1 파싱: 알려진 이름의 형 · 범위(잘라서 알림), `game.*` 통과, 모르는 이름은 알리고 다시 쓸 때 남김, BOM · UTF-16 파일(`readConfigText`).
- [x] S3-2 바꾼 값만 씀. 기본값으로 되돌린 이름은 파일에서 빠짐(`reset`). 변경 번호.
- [x] S3-3 엔진 적용: 소리 6개가 `AudioSystem` 값, AO · 블룸, 품질 4개가 프로젝트 값 위에(로그), 입력 덮어쓰기(틀린 글은 경고 후 프로젝트 바인딩).
- [x] S3-4 편안함: `ComfortSettings` 서비스로 스냅 회전 각 · 텔레포트 · 높이 모드 · 눈높이가 컴포넌트 값보다 앞섬(시험).
- [x] S3-5 다시 시작 왕복: `--write-root=<임시>` 로 `--user-setting` 실행 → 그 옵션 없이 다시 실행하면 로그의 적용 값이 같음(소리 · 그래픽 · 입력).
- [x] S3-6 실행 중 바꾼 값이 다음 프레임에 적용되고 1 초 뒤 저장됨, 다시 시작 항목은 한 번 알림.

### G 공통

- [x] G-1 Release · Debug 경고 0, ctest 전부 통과(새 시험 포함), 아키텍처 PASS(정적 포인터 0).
- [x] G-2 기준 PNG 7장 · UI 덤프 54장면 그대로(설정 파일이 없으면 예전과 같음).
- [x] G-3 Android 빌드 통과(쓰기 루트 = 앱 외부 파일 폴더).
- [ ] G-4 (헤드셋) 편안함 설정(눈높이 고정 · 스냅 각)이 Quest 에서 그대로 보임.

## 4. 결과 (2026-11-20)

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0 |
| ctest | 두 구성 34/34 통과, 검사 2,708(새 `save_settings` 89, `headless_render` 에 명령줄 8) |
| 아키텍처 | `HarukaArchTest` PASS. `SaveGame` · `UserSettings` 는 core(다른 엔진 모듈을 보지 않음), 편안함 값은 월드 서비스(정적 포인터 0) |
| 기준 PNG 7장(`base7.sh sav`, GPU 1 이 빈 때) | 모두 그대로(ce6def24 · 73dd1f02 · c283f276 · b4bd1fee · 5d6e3e5f · 9cf28353 · 201fad0e), 검증 오류 0 |
| UI 덤프(`uidump.sh sav asy`) | 54장면 모두 UI · 뷰포트 같음. 에디터 실행 폴더에 `user_settings.ini` 가 생기지 않음(값을 바꾸지 않으면 파일을 만들지 않음) |
| 다시 시작 왕복(S3-5) | `--write-root=build/chk/save/wr` 로 소리 2 · 그래픽 3 · 입력 2 · 편안함 3 · game 1 개를 `--user-setting` 으로 바꾼 실행 → 옵션 없이 다시 실행: 두 로그의 적용 줄이 같음(`Quality: shadow cascades 1, shadow distance 40 m … (project.ini + user_settings.ini)`, `Post: Bloom off`, `Audio volumes: master 0.8 … music 0.25`, `Comfort: snap turn 30 deg … height fixed, eye height 1.2 m`, `Input bindings: 2 user override(s)`) |
| 실행 중 변경(S3-6) | `--user-setting-at` 으로 10 ~ 18 프레임에 바꿈: 소리 · AO 는 다음 프레임에 적용(로그), 그림자 단 수는 "takes effect after a restart" 한 번, 틀린 키(`key:NOPE`)는 경고 후 프로젝트 바인딩, 마지막 변경 약 1 초 뒤 `Saved Config/user_settings.ini (changed)`(30,000 프레임 실행 중간), 바뀐 것이 남았으면 종료할 때 저장 |
| Android | `build/android` 빌드 통과. 새 경고는 중괄호 없는 `if` 안의 `HLOG` 두 줄이었고 고침(남은 경고는 예전 것) |

**새 파일:** `src/core/SaveGame.hpp/.cpp`(`SaveData` · `hsave::serialize` · `parse` · `crc32` · `SaveStore`), `src/core/UserSettings.hpp/.cpp`
(`UserSettings` · `ComfortSettings`), `tests/SaveTest.cpp`.

**바꾼 곳:** `Engine`(사용자 설정 읽기 · 품질 · 후처리 · 소리 · 입력 · 편안함 적용, 프레임 처음 변경 확인, 1 초 뒤 · 종료 때 저장, `--user-setting` ·
`--user-setting-at`), `EngineOptions`, `EngineContext`(`settings` · `saves`), `RigLocomotionComponent`(스냅 각 · 텔레포트를 `ComfortSettings` 에서),
`XrViewOrigin::applyRigSpace`(높이 모드 · 눈높이), CMake(`HarukaSaveTest`, `add_test save_settings`), `tests/HeadlessRenderTest.cpp`.

**쓰는 법(게임 모듈):**

```cpp
// 저장
SaveData d;
d.dataVersion = 2;                       // 게임이 정한 데이터 버전
d.title = "Chapter 2";
d.setVec3("player.pos", rig->worldPosition());
d.setGuid("quest.door", door->guid());   // 액터 참조(씬을 GuidPolicy::Keep 으로 다시 읽으면 같은 액터)
std::string why;
if (!ctx.saves.save("slot1", d, &why)) { /* 이유를 알림. 이전 저장본은 그대로 */ }

// 불러오기
SaveData r;
const auto res = ctx.saves.load("slot1", r);   // res.fromBackup: 본 파일이 손상돼 이전 저장본을 읽음
if (res.ok && r.dataVersion < 2) { /* 옛 형식 옮기기 */ }
const float hp = r.getFloat("player.health", 1.f);   // 없으면 기본값

// 사용자 설정: 바꾸면 다음 프레임에 적용되고 1 초 뒤 저장됩니다.
ctx.settings.setFloat("audio.music", 0.5f);
ctx.settings.set("input.action.Teleport", "key:T, xr:right.a");
ctx.settings.reset("comfort.snapTurn");          // 씬 값으로
```

**남긴 것(범위 밖 · 다음 후보):**
- 게임 설정 화면(UI): 설정 표(`UserSettings::keys()` 의 이름 · 형 · 범위 · 설명)를 그대로 화면으로 만들 수 있게 해 두었습니다. 글자는
  [Text_System.md](Text_System.md) 번역 표를 씁니다.
- 해상도 배율 · vsync · FPS 제한, 부드러운 회전 · 비녜트: 엔진에 기능 자체가 없어 설정 이름을 만들지 않았습니다.
- 액터 상태 자동 스냅숏(어느 컴포넌트 값을 남길지 표시하는 방식), 클라우드 저장, 암호화.
- POSIX 에서 이름 바꾸기 뒤 폴더 fsync(Android 전원 차단 대비)는 하지 않았습니다. `.bak` 이 남으므로 본 파일이 사라져도 이전 저장본을 읽습니다.
- (헤드셋) G-4.

## 5. 콘솔 명령 `setting` (2026-11-21)

헤드셋 점검에서 설정을 바꿔 가며 비교하려고 넣었습니다(시작 옵션 `--user-setting` 은 실행마다 다시 띄워야 함).
`setting` = 바꾼 값 목록, `setting <이름>` = 지금 값(없으면 project default), `setting <이름> <값>` = 바꾸기,
`setting <이름> reset` = 프로젝트 기본값으로. 게임 설정 화면과 같은 길(`UserSettings::set`)이라 형 확인 · 범위 자르기 · 모르는 이름 거부가
같고, Live 값은 다음 프레임에 적용되며 1 초 뒤 `Config/user_settings.ini` 에 저장됩니다. 확인: `--console-at` 으로 바꾸기 · 목록 · 되돌리기 ·
모르는 이름 거부와 Comfort · Accessibility 적용 로그.
