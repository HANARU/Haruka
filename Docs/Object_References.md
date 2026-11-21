# 참조 체계 — 액터 고유 ID · 액터 참조 · 에셋 참조 관리

2026-11-19 시작. 글자 체계([Text_System.md](Text_System.md))처럼 "여러 시스템이 기대는데 없던 기반"을 채웁니다.
이 문서는 **완료 조건 체크리스트**입니다. 각 줄은 확인 방법(시험 이름 · 덤프 장면 · 명령)을 함께 적고, 만족하면 `[x]` 로 바꿉니다.
창을 띄워야만 확인할 수 있는 줄은 `(창)` 으로 표시하고, 사용자가 정한 때 확인합니다.

## 0. 조사: 다른 엔진은 어떻게 하는가

| 엔진 | 액터(객체) 정체성 | 같은 씬 안의 참조 | 에셋 참조 | 에셋 이름 바꾸기 |
|---|---|---|---|---|
| UE 5 | `AActor::ActorGuid`(에디터에서 저장, 128비트) + 객체 경로 | 객체 포인터를 경로로 직렬화, 다른 레벨은 `TSoftObjectPtr` | 패키지 경로(`/Game/...`) | 에디터에서 옮기면 옛 자리에 Redirector 를 남기고 "Fix Up" 으로 참조 파일을 고쳐 씀. Asset Registry 가 의존 관계를 앎 |
| Unity | 파일 안의 `fileID`(64비트) | `{fileID}` | `.meta` 의 GUID + fileID | `.meta` 를 따라 옮기면 참조가 그대로. 탐색기에서 `.meta` 없이 옮기면 끊김 |
| Godot 4 | 노드 이름 경로 | `NodePath`(이름 경로, 이름을 바꾸면 끊김) | `uid://` + 경로 | 에디터에서 옮기면 참조 파일을 고쳐 씀. 4.4 부터 `.uid` 파일로 바깥 이동도 견딤 |

Haruka 의 지금 상태(조사 2026-11-19):
- `Actor::id` 는 `World::spawnActor` 가 매기는 **실행 중 번호**이고 파일에 쓰지 않습니다. 씬을 열 때마다 바뀝니다.
- 씬 · 프리팹은 부모를 **파일 안 블록 순서**(`PARENT:`)로 가리킵니다. 그 밖의 액터끼리의 참조는 저장할 방법이 없습니다.
- 에셋은 **논리 경로 문자열**(`model=/Game/models/x.glb`, `file=/Game/audio/y.wav`, `project.ini` 의 `startScene`)로 가리킵니다.
  누가 무엇을 쓰는지 아는 곳이 없고, 콘텐츠 브라우저에는 이름 바꾸기 · 옮기기가 없습니다. 없는 메시는 `defaultCube` + 오류 로그입니다.

## 1. 결정

- **D1 액터 GUID:** 액터마다 64비트 무작위 값(`HGuid`, 0 아님, 16자리 소문자 16진수)을 둡니다. Unity `fileID` 와 같은 크기입니다.
  액터 10만 개에서 겹칠 확률은 약 3 × 10⁻¹⁰ 이고, 겹치면 읽을 때 찾아 새 값을 줍니다. `id`(실행 중 번호, Undo 커맨드가 씀)는 그대로 둡니다.
  UE 의 객체 번호와 ActorGuid 를 나눈 것과 같습니다.
- **D2 파일:** 액터 블록에 `GUID:<16진수>` 줄. 씬 머리 줄 v6, 프리팹 v5. GUID 없는 옛 파일은 읽을 때 새 값을 받고, 다음 저장부터 고정됩니다.
- **D3 액터 참조 값:** `HActorRef`(GUID + 찾은 포인터 캐시). 파일에는 `이름=actor:<16진수>`(비었으면 빈 값). 대상이 지워지면 `nullptr`,
  Undo 로 되살리면 같은 GUID 라 다시 이어집니다(UE soft pointer 와 같은 성질).
- **D4 GUID 정책 두 가지:** `Keep`(씬 열기 · Play 되돌리기 · Undo 되살리기 · 게임 시작 씬) 은 파일 값을 지키고, `Fresh`(프리팹 꺼내기 · 붙여넣기 · 복제)
  는 세트 안의 GUID 를 모두 새로 주고 **세트 안을 가리키던 참조를 새 값으로 바꿉니다**. 세트 밖을 가리키는 참조는 그대로 둡니다(Unity · UE 복제와 같음).
  바꾸기는 글 단계에서 합니다(`actor:` 토큰). 그래서 등록되지 않은 컴포넌트(`UnknownComponent`)의 값도 함께 바뀝니다.
- **D5 에셋 참조는 경로 그대로:** `.meta` · UID 파일을 새로 만들지 않습니다(사람이 읽는 글 형식 · pak 형식을 지킴). 대신 UE 의 Asset Registry ·
  Fix Up 처럼 **참조 색인 + 에디터에서 옮길 때 참조 파일을 고쳐 씀**을 둡니다. 탐색기에서 옮겨 끊긴 참조는 **검사 명령**이 찾습니다(Godot 4.3 이전과 같은 한계).
- **D6 참조 인식 규칙:** 씬 · 프리팹 · `/Config/*.ini` 의 값(`키=값`, `키:값`, `키 = 값`) 중 정규화한 결과가 `/Game/` · `/Engine/` 로 시작하는 것.
  glTF 안의 상대 `uri` 는 읽기 전용 참조로 색인합니다(그 파일을 따로 옮기면 깨지므로 거절하고, 폴더째 옮기기를 권함).
  C++ 소스 안의 경로 문자열은 고칠 수 없으므로 **보고만** 합니다(UE 도 같은 한계).
- **D7 열린 씬:** 디스크의 파일을 고친 뒤, 열린 씬(메모리)도 씬 글로 떠서 같은 규칙으로 값을 바꾸고 다시 읽습니다. 그래서 저장하지 않은 편집도
  남고 저장 여부 표시도 그대로입니다(처음 계획한 "저장을 묻고 디스크에서 다시 열기"보다 낫다고 판단해 바꿈). Undo 기록은 비웁니다(Undo 스냅샷
  안의 옛 경로가 되살아나지 않게). 열린 씬 파일 자신을 옮기면 문서 경로 · 마지막 씬 기록도 바꿉니다. Play 중에는 막습니다.

## R — 액터 고유 ID 와 액터 참조

- [x] R-1 `src/core/Guid.hpp`: `HGuid`(64비트, 0 = 없음) · 새 값 만들기(OS 난수로 씨앗을 준 64비트 생성기) · 16진수 쓰기 · 읽기(대소문자 무시, 길이 · 글자 검사).
      확인: `HarukaRefTest` guid 묶음(왕복, 잘못된 글 거절, 1만 개 겹침 없음).
- [x] R-2 `Actor::guid()`, `World::spawnActor` 가 새 GUID 를 줌, `World::findByGuid`, `World::assignGuid`(이미 쓰는 값이면 거절), 지우면 색인에서 빠짐.
      확인: `HarukaRefTest` world 묶음.
- [x] R-3 씬 · 프리팹 쓰기가 `GUID:` 줄을 씀(씬 v6, 프리팹 v5), 읽기가 그 값을 지킴(Keep). 옛 v5 씬 · v4 프리팹도 읽음(새 GUID).
      같은 GUID 가 둘이면 뒤의 것에 새 값 + 경고. 확인: `HarukaRefTest` file 묶음, 기존 `scene_components` · 프리팹 시험 통과(머리 줄 검사는 v6 으로 갱신).
- [x] R-4 `HActorRef`(`src/scene/ActorRef.hpp`): `resolve(World&)` 는 구조 버전이 같으면 캐시, 다르면 GUID 색인. 대상을 지우면 `nullptr`,
      같은 GUID 로 되살리면 다시 찾음. 값 쓰기 `actor:<16진수>` · 읽기(빈 값 = 없음, 깨진 값 = 경고 + 없음).
      확인: `HarukaRefTest` ref 묶음.
- [x] R-5 Fresh 정책(`sceneio/GuidRemap.hpp`): 세트 안 GUID 를 새로 주고 세트 안을 가리키는 `actor:` 값을 바꿈, 세트 밖 참조는 그대로,
      `UnknownComponent` 값도 바뀜. 프리팹 꺼내기 · 붙여넣기 · 복제가 Fresh, 씬 열기 · Play 되돌리기 · Undo 되살리기 · 게임 시작 씬이 Keep.
      확인: `HarukaRefTest` remap 묶음(프리팹을 두 번 꺼내면 각자 자기 안을 가리킴, 복제한 쌍이 서로를 가리킴, 밖을 가리키던 값은 원래 대상).
- [x] R-6 Undo · Play: 대상 액터를 지우고 Undo 하면 참조가 다시 이어짐, Play → Stop 뒤에도 같은 GUID.
      확인: `HarukaRefTest`(스냅샷 되살리기 · 씬 글 왕복).
- [x] R-7 디테일 패널: 속성 종류 `PropDesc::ActorRef`(+ `XPROP_ActorRef`). 행은 대상 이름(없으면 "None", 못 찾으면 "(missing)")을 보이고,
      누르면 액터 목록 대화상자(None + 아웃라이너 순서, 같은 이름은 " #2" 처럼 구분)에서 고름. 실행 취소 · 여러 선택에 같은 값 넣기가 다른 칸과 같음.
      확인: `HarukaRefTest`(capture · apply · setProp 에 ActorRef), UI 덤프 새 장면 `ref_details`(행) · `ref_details_missing`("(missing)") ·
      `ref_picker`(목록), 화면 `build/chk/uishot/rf_ref_picker.png`.
- [x] R-8 쓰는 예: 샘플 프로젝트 `LookAtComponent`(Play 중 대상 액터를 바라봄, 게임 쪽 사용법을 보여 줌) + 샘플 씬 `ReferenceProbe.hscene`.
      확인: UI 덤프 `ref_details` 가 이 씬을 씀, `HarukaRefTest` 11 묶음(씬 왕복 · 틱 한 번에 대상을 바라봄 · 못 찾으면 돌지 않음).

## A — 에셋 참조 관리

- [x] A-1 참조 색인(`src/core/AssetRefs.hpp/.cpp`, 도구 · 에디터 공용): 루트 목록(`/Game` → 디스크 폴더, `/Config` → 디스크 폴더)을 받아
      `.hscene` · `.hprefab` · `.ini` 의 참조(파일 · 줄 번호 · 키 · 정규화한 경로)와 `.gltf` 의 상대 `uri` 를 모음. 옛 `assets/...` 값도 정규화해 인식.
      확인: `HarukaRefTest` index 묶음(임시 폴더에 만든 작은 프로젝트).
- [x] A-2 찾기: 어떤 에셋을 누가 쓰는지, 없는 파일을 가리키는 참조 목록(검사). 확인: `HarukaRefTest`, 샘플 프로젝트에 끊긴 참조 0(시험이 실제 `Projects/Samples` 를 검사).
- [x] A-3 옮기기(파일 · 폴더, 이름 바꾸기 포함): 대상이 이미 있으면 거절, glTF 가 따로 가리키는 파일이면 거절(폴더째 옮기기 권함), glTF 를 다른 폴더로
      옮길 때 바깥 `uri` 가 있으면 거절. 참조 파일은 그 값의 글자만 바꿔 원자적으로 다시 씀(줄 끝 · 나머지 바이트 그대로), 그다음 파일을 옮김.
      C++ 소스(`Source/`) 안의 같은 문자열은 경고로 보고. 확인: `HarukaRefTest` move 묶음(파일 · 폴더 · 거절 경우 · 줄 끝 보존 · 실패 시 원래대로).
- [x] A-4 `HarukaPak` 명령: `refs <프로젝트> [에셋]`(누가 쓰는지 · 없는 참조), `move-asset <프로젝트> <옛 경로> <새 경로>`. 끊긴 참조가 있으면 `refs --check` 가 0 아닌 값으로 끝남.
      확인: 샘플 프로젝트 사본에 실행(참조 29개 · 끊김 0, model.glb 옮기기 계획 5값, Test_BeepLoop.wav 를 옮기면 값 줄만 바뀜,
      탐색기처럼 직접 옮기면 `refs --check` 가 끊긴 참조 5개를 찾고 1 로 끝남).
- [x] A-5 에디터: 콘텐츠 브라우저의 각 항목 끝에 "…" 단추 → 에셋 대화상자(경로 · 참조하는 파일 목록, 씬을 고르면 그 씬 열기) + "Rename / Move…"
      (새 경로 입력, D7 규칙: 저장 확인 · 다시 열기 · Undo 비움 · Play 중 막음 · 열린 씬 자신을 옮기면 문서 경로 갱신 · 시작 씬이면 `project.ini` 도 고침).
      확인: UI 덤프 새 장면 `asset_actions` · `asset_actions_ko`(대화상자), 화면 `build/chk/uishot/rf_asset_ko.png`,
      `build/chk/rf_move_check.sh`(샘플 사본에 에디터를 띄워 `--ui-move-asset` 로 model.glb 를 옮김: 파일 이동 · 두 씬의 값 5개 고침 ·
      열린 씬이 새 경로로 메시를 다시 읽음 `Loaded: /Game/models/props/Ship.glb` · 저장 여부 그대로).
- [ ] A-6 (창) 에디터에서 모델 하나를 다른 폴더로 옮기고, 그 모델을 쓰는 씬을 다시 열어 메시가 그대로 보이는지.

## G — 공통

- [x] G-1 Release · Debug 경고 0, `ctest` 두 구성 모두 통과, `HarukaArchTest` PASS(새 파일이 계층 규칙을 지킴: `core/Guid` · `core/AssetRefs` 는 core, `ActorRef` 는 scene).
- [x] G-2 기준 PNG 7장(`base7.sh`) 그대로, UI 덤프 기존 장면은 콘텐츠 목록(새 샘플 씬) 말고 그대로.
- [x] G-3 문서: 이 문서의 결과 절, `Engine_Specification.md` 시험 표, `Editor_Layout_Design.md`(콘텐츠 브라우저 · 디테일 행).
- [x] G-4 보이는 창을 띄우지 않음(덤프는 숨은 창, `--gpu=1`).

## 결과 (2026-11-19 밤, 커밋 `fb452fd`)

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0 |
| ctest | 두 구성 30/30 통과, 검사 2,395(새 `object_refs` 98, `scene_components` 74) |
| 아키텍처 | `HarukaArchTest` PASS(검사 28) |
| 기준 PNG 7장(`base7.sh rf2`, Debug 다시 빌드 뒤) | 모두 그대로(ce6def24 · 73dd1f02 · c283f276 · b4bd1fee · 5d6e3e5f · 9cf28353 · 201fad0e), 검증 오류 0 |
| UI 덤프(`uidump.sh rf tx`) | 52장면(47 + 참조 5). 기존 47장면 `_vp` 모두 같음. UI 차이는 두 가지뿐: 액터를 고른 장면의 Add Component 에 샘플 모듈이 등록한 "+ Look At" 단추(20장면), 콘텐츠 목록의 "..." 단추(`bottom_content*`) |
| 넘침 | 가로 넘침 0. 세로는 기존과 같은 스크롤 영역 아래쪽이며, 줄이 한 줄 늘어난 만큼만 늘어남 |
| 에디터 옮기기(`rf_move_check.sh`) | 파일 이동 · 참조 5개 고침 · 열린 씬 메시를 새 경로로 다시 읽음 · 검증 오류 0 |

**새 파일:** `src/core/Guid.hpp` · `AssetRefs.hpp/.cpp`, `src/scene/ActorRef.hpp`, `src/sceneio/GuidRemap.hpp`, `src/editor/ActorPicker.hpp`,
`tests/ReferenceTest.cpp`, `Projects/Samples/Source/LookAtComponent.hpp`, `Projects/Samples/Content/scenes/ReferenceProbe.hscene`.

**함께 고친 것:** 콘텐츠 목록 · 프리팹 목록이 파일 이름을 시스템 코드 페이지로 바꾸던 것(`fs::path::string()`)을 UTF-8 로(한글 이름 에셋이 깨지거나
예외가 나던 문제), 소리 · 프리팹 목록이 하위 폴더를 보지 않던 것(옮긴 에셋이 목록에서 사라지지 않게).

**남긴 것(범위 밖 · 다음 후보):**
- `(창)` A-6: 에디터에서 모델을 옮긴 뒤 그 모델을 쓰는 씬을 다시 열어 메시가 그대로인지 눈으로 확인.
- 다른 씬의 액터를 가리키는 참조(UE 의 다른 레벨 soft pointer), 프리팹 인스턴스와 원본의 연결(UE · Unity 의 프리팹 덮어쓰기).
- 탐색기에서 옮긴 에셋을 따라가는 것(Godot 4.4 의 `.uid` 파일 같은 방식). 지금은 `HarukaPak refs --check` 와 시험이 끊긴 참조를 찾습니다.
- 콘텐츠 브라우저의 폴더 보기 · 끌어 놓기 · 지우기(참조가 있으면 경고), 디테일 패널에서 에셋 경로 칸을 고르는 UI.
- 기존 샘플 씬에는 아직 `GUID:` 줄이 없습니다(읽을 때 새 값을 받고, 에디터에서 저장하면 생김).
