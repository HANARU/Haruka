# 콘텐츠 브라우저 · Add Component 팝업 (2026-11-21)

에디터 편의성 작업입니다. 하단 Content Browser 탭을 UE 의 Content Browser 방식으로 다시 짜고, 디테일 패널의 컴포넌트
추가를 Unity 의 Add Component 창처럼 글자 입력으로 찾게 바꿨습니다. 같은 날 창 점검에서 찾은 결함 DEF-176 ~ 178 과
함께 했습니다([Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) "야간 일괄 작업 창 확인").

## 1. 예전 모습과 문제

| 곳 | 예전 | 문제 |
|---|---|---|
| 콘텐츠 브라우저 | 고정 폴더 넷(scenes · models · audio · prefabs)을 SCENES / MODELS / AUDIO / PREFABS 제목 아래 한 목록으로 늘어놓음. 한 번 누르면 바로 실행(모델 · 프리팹 꺼내기, 소리는 고른 액터에 붙임, 씬 열기) | 네 폴더 밖 · 다른 종류(.hcue · .hnav · 텍스처)는 보이지 않음. 하위 폴더 구분 없음. 누르기만 해도 액터가 생겨 헷갈림(창 점검 19번) |
| 컴포넌트 추가 | 등록된 타입마다 `+ 이름` 단추를 패널 폭에 맞춰 여러 줄로(DEF-22 · DEF-109) | 24종이 넘어 찾기 어려움. 분류 · 검색 없음 |

## 2. 다른 엔진의 방식과 적용

| 엔진 | 방식 | Haruka 적용 |
|---|---|---|
| UE Content Browser | 왼쪽 폴더 트리(Sources), 위 경로 · 뒤로/앞으로 · 검색 · 필터, 가운데 타일(썸네일 + 종류 색 띠) 또는 목록, 아래 항목 수. 한 번 누름 = 고르기, 두 번 = 열기, 끌어 뷰포트에 놓기, 오른쪽 누름 메뉴 | 같은 배치 · 같은 누름 규칙. 썸네일 대신 종류 아이콘 · 색 띠. 두 번 누름은 종류별 실행(씬 열기 · 모델/프리팹 놓기 · 소리 미리 듣기). 끌어 놓기는 같은 날 2차로 넣음(9절) |
| Unity Add Component | 인스펙터 맨 아래 단추 하나 → 검색 칸에 바로 입력되는 팝업. 빈 검색은 분류 계층, 글을 치면 모든 분류에서 한 목록. ↑ · ↓ · Enter · Backspace(위로) | 그대로. 분류는 레지스트리 항목의 `category`(Unity 의 AddComponentMenu 경로와 같은 몫). 이미 붙은 타입은 흐리게 "added"(같은 타입 하나 규칙) |

## 3. 결정

- 한 번 누름 = 고르기, 두 번 누름 = 실행(사용자 결정 2026-11-21, UE 와 같음).
- 뷰포트로 끌어 놓기는 1차 창 확인 뒤 2차로 넣음(사용자 지시, 9절).
- 놓는 자리: 에디터 카메라 정면 광선이 처음 닿는 액터 경계 상자 면(20 m 안), 없으면 카메라 앞 3 m
  (`EditorViewportController::placementPoint`).
- 검색 · 종류 거르기가 있으면 지금 폴더 아래를 하위 폴더까지 찾고 폴더는 보이지 않습니다(UE 와 같음).
- 소리 미리 듣기는 공간화 없이 sfx 버스로 한 번(`EditorApp::previewSound`).

## 4. 구조

| 파일 | 몫 |
|---|---|
| `src/editor/ContentBrowserModel.hpp` | 색인(`hcb::ContentIndex`): Content 폴더 트리 · 확장자 → 종류 · 지금 폴더 보기 · 검색 · 거르기. 화면 없이 시험 |
| `src/editor/ContentBrowserPanel.*` | 탭 위젯: 도구 줄(뒤로 · 앞으로 · 경로 · 검색 · 보기 바꾸기) · 거르기 줄 · 트리 · 타일/목록 · 상태 줄, 누름 규칙, 메뉴 항목 |
| `src/editor/EditorContextMenu.*` | 공용 오른쪽 누름 메뉴(루트 오버레이 호스트, 바깥 누름 · Esc 로 닫음) |
| `src/editor/ComponentPickerModel.hpp` | Add Component 목록 계산: 분류 순서 · 검색 점수(이름 앞 · 머리글자/단어 앞 · 가운데 · 타입 이름 · 분류 이름) · 고르기 이동. 화면 없이 시험 |
| `src/editor/ComponentPicker.*` | 팝업 위젯 · 키(↑ ↓ Enter Esc, 빈 칸 Backspace) · 마우스 |
| `src/scene/ComponentRegistry.hpp` | `Entry::category`, `setCategory`. 엔진 분류는 `CoreComponents.cpp` · `XrComponents.cpp`, 게임 모듈은 자기 타입에(`SamplesGameModule.cpp` 의 Look At = Gameplay) |
| `src/editor/TextInputController.*` | 글 칸 대상별 분기를 `fieldFn` 한 곳으로 모음. 새 대상 `Picker` · `ContentSearch`, 칸 콜백 `onCancel`(Esc) · `onBackspaceEmpty` |
| `Engine/Content/Editor/Icons/` | Lucide 아이콘 9개 더함(folder · chevron-left · music · film · layout-grid · list · package · map · image) |

메뉴 항목(종류별):

| 종류 | 항목 |
|---|---|
| 씬 | Open Scene |
| 모델 · 프리팹 | Place in Scene |
| 소리 · 큐 | Play, Assign to Selected Actor(고른 액터가 없으면 꺼짐) |
| 모든 파일 | References, Rename, Move...(참조 체계 대화상자) · Show in Explorer · Copy Path |
| 폴더 | Open · Show in Explorer · Copy Path |
| 빈 자리 | Show in Explorer(지금 폴더) · Refresh |

## 5. 덤프 옵션(창 없이 확인)

`--ui-add-component` · `--ui-add-component-search=` · `--ui-add-component-category=` · `--ui-add-component-down=N` ·
`--ui-add-component-commit`, `--ui-content-folder=` · `--ui-content-search=` · `--ui-content-filter=Sound,Prefab` ·
`--ui-content-view=list` · `--ui-content-select=` · `--ui-content-menu=<경로, 빈 값 = 빈 자리>` · `--ui-content-activate=`.
덤프 실행이 씬을 바꾸면 닫을 때 저장 확인을 묻지 않고 저장 없이 닫습니다(예전에는 대화상자가 떠 시간 제한까지 기다림).

## 6. 확인

- 시험: `scene_components` 1b(Add Component 목록) · 1c(콘텐츠 색인), `text_system`(새 화면 글 한국어 표).
- UI 덤프: 새 장면 11개(addcomp_* · content_*). 바뀐 기존 장면은 고른 액터가 있는 장면(디테일 아래 Add Component 단추)과
  콘텐츠 탭 장면뿐.
- 창 확인(사용자, 남음): 아래 체크리스트.

## 7. 창 확인 체크리스트(남음)

1. 디테일 맨 아래 Add Component → 팝업이 단추 아래(자리가 없으면 위)에 열리고 바로 글을 칠 수 있음.
2. `fog` 입력 → Exponential Height Fog 하나, Enter → 붙고 팝업 닫힘, Ctrl+Z 로 떼어짐.
3. 빈 칸에서 Rendering 에 Enter → 분류 안, Backspace → 분류 목록(방금 분류에 놓임), Esc · 바깥 누름 → 닫힘.
4. 한글 IME 로 검색 칸 입력(조합 중 Enter · Esc 는 IME 몫).
5. 콘텐츠 브라우저: 트리 누름 → 그 폴더, 경로 마디 누름 → 그 폴더, 뒤로 · 앞으로.
6. 타일 한 번 누름 → 고르기만(액터 생기지 않음), 두 번 → 모델은 화면 가운데 표면에 놓임 + 선택 + Undo 가능.
7. 소리 두 번 누름 → 들림. 오른쪽 누름 → 메뉴, Assign to Selected Actor(액터를 고른 뒤).
8. 검색 · 거르기(하위 폴더까지), 목록 보기, 폭을 바꾸면 타일 줄 나눔이 따라감.
9. Show in Explorer 가 그 파일을 고른 채 탐색기를 엶.
10. 바깥에서 Content 에 파일을 더하거나 지우면 목록이 따라옴(감시 알림 → 다시 훑기).

## 8. 다음 단계(하지 않은 것)

- 끌어 놓는 동안 실제 액터 미리 보기(UE 의 임시 액터), 아웃라이너 · 디테일 · 콘텐츠 폴더(옮기기)로 끌어 놓기.
- 썸네일(모델 · 텍스처 미리 그리기), 여러 개 고르기(Ctrl · Shift), 새 폴더 · 이름 바꾸기 칸(지금은 참조 체계 대화상자),
  보기 설정 저장(마지막 폴더 · 타일/목록).

## 9. 끌어 놓기 (2026-11-21 2차)

1차 창 확인 통과 뒤 넣었습니다. HSlate 에 끌기 기능을 따로 만들지 않고, 에디터 입력(`EditorApp::handleInput`)이 마우스로 직접 판정합니다
(아웃라이너 행 끌기와 같은 방식).

| 단계 | 동작 |
|---|---|
| 시작 | 에셋 보기의 항목(폴더 제외)을 누른 채 6 px 넘게 움직이면 끌기. 그 항목을 고릅니다 |
| 끄는 동안 | 커서 옆 이름표(놓을 수 없으면 흐리게 "cannot drop here"). 뷰포트 위면 놓일 자리에 원 + 세운 선(카메라 거리에 비례해 화면에서 같은 크기). 소리를 소리 액터 위에 끌면 그 액터에 초록 구 |
| 놓기(뷰포트) | 모델 · 프리팹 = 커서 아래 자리에 놓고 고름. 소리 · 큐 = 커서 아래가 소리 액터(오디오 컴포넌트가 있음)면 그 소리를 바꿈, 그 밖이면 그 자리에 소리 액터를 만듦(UE 의 Ambient Sound 와 같은 몫). 모두 Undo 가능 |
| 취소 | Esc, 또는 뷰포트 밖에서 놓기 |

놓을 자리(`EditorViewportController::dropPoint`): 아이콘이 있는 액터(소리 · 라이트, 경계 상자가 없음)는 고르기와 같이 아이콘 18 px 안이면 그
액터 → 커서 광선이 처음 닿는 액터 경계 상자 면 → 바닥 평면 y = 0(200 m 안) → 광선 5 m.

규칙 조정: 처음에는 "소리를 액터 위에 놓으면 그 액터에 지정"으로 했으나, 바닥에 놓으면 Ground 에 소리가 붙어 "소리 액터 위면 바꾸기, 그 밖은
새 소리 액터"로 바꿨습니다. 다른 액터에 소리를 붙이는 길은 오른쪽 누름 메뉴의 Assign to Selected Actor 입니다.

덤프 옵션: `--ui-content-drag=<경로>@x,y`(그 자리까지 끌고 있는 화면), `--ui-content-drop=<경로>@x,y[;...]`(차례로 놓기).
UI 덤프 장면 content_drag · content_drop_model · content_drop_sound.

창 확인(남음): 11. 모델 타일을 뷰포트로 끌면 이름표 · 자리 표시가 따라오고 놓으면 그 자리, Ctrl+Z 로 사라짐. 12. 소리를 바닥에 놓으면 소리
액터, 그 아이콘 위에 다른 소리를 놓으면 소리만 바뀜. 13. 끄는 중 Esc → 취소, 콘텐츠 브라우저 안에서 놓으면 아무 일 없음.
