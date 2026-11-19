# 글자 체계 — 유니코드 입력 · 표시 글(HText) · 글꼴 사슬

2026-11-19 시작. 배경 조사는 `Handoff/Plan_Text_System.md`(언리얼 FText · Slate, Godot TextServer 와 견줌).
이 문서는 **완료 조건 체크리스트**입니다. 각 줄은 확인 방법(시험 이름 · 덤프 장면 · 명령)을 함께 적고, 만족하면 `[x]` 로 바꿉니다.
창을 띄워야만 확인할 수 있는 줄은 `(창)` 으로 표시하고, 사용자가 정한 때 확인합니다(그때까지 `[ ]` 로 남김).

## 0. 원칙

- 문자열은 계속 `std::string` UTF-8 하나입니다(UE 의 UTF-16 `FString` 을 따르지 않음). 경로 규약(`PathUtf8.hpp`, 매니페스트)은 그대로입니다.
- **사용자 데이터**(액터 이름 · 경로 · 로그 · 검색어)는 `std::string`, **화면에 보이는 엔진 · 에디터 · 게임 글**은 `HText` 입니다(UE 와 같은 나눔).
- 편집 단위는 **글자 단위(grapheme)** 입니다. 커서는 언제나 글자 경계에 있고, 지우기 · 이동은 한 글자씩 합니다.
- 범위 밖(나중): 조합 엔진(HarfBuzz), 양방향 글(아랍어 · 히브리어), 커닝, 컬러 이모지, Android 소프트 키보드 · VR 가상 키보드, 여러 줄 편집 칸.

## T1 — UTF-8 기초와 편집 상태

- [x] T1-1 `src/core/Utf8.hpp`: 코드 포인트 읽기(잘못된 바이트는 U+FFFD 대신 지금처럼 '?' 하나) · 쓰기 · 올바른지 검사 · 고치기(잘못된 바이트를 '?' 로).
      확인: `HarukaTextTest` 의 utf8 묶음.
- [x] T1-2 글자 경계: 다음 · 이전 글자 경계가 한글 자모 묶음(L · V · T), 결합 문자(Mn · Me 주요 구간), 변형 선택자, ZWJ 이음,
      국기(지역 표시 두 개), CR LF 를 한 글자로 봅니다. 확인: `HarukaTextTest` 의 grapheme 묶음(예: "é", "👍🏽", "🇰🇷", 자모 "한").
- [x] T1-3 한글 NFC: 첫가끝 자모 · 호환 자모가 아닌 조합형 자모(U+1100 ~ ) 순서를 완성형으로 합칩니다(macOS 파일 이름).
      확인: `HarukaTextTest` "한" → "한".
- [x] T1-4 대소문자 무시 비교: ASCII 만 접고 그 밖은 그대로 비교(미정의 동작 없는 `unsigned char` 처리). 아웃라이너 · 로그 검색이 이것을 씀.
      확인: `HarukaTextTest` + `OutlinerPanel.cpp` · `OutputLogPanel.cpp` 에 `::tolower(char)` 없음(grep).
- [x] T1-5 `HFontAtlas::nextCodepoint` · `utf8Floor` 가 `Utf8.hpp` 를 부름(같은 결과, 기존 `testUtf8AndBoldFont` 통과).
- [x] T1-6 `HTextEditState`(`src/ui/slate/HTextEdit.hpp`): 글 · 커서 · 기준점(선택). 넣기(선택을 바꿈) · Backspace · Delete(글자 단위,
      Ctrl 이면 단어 단위) · 좌우 이동(글자, Ctrl 이면 단어) · Home · End · Shift 로 선택 늘리기 · 전체 선택 · 선택 글 · 잘라내기.
      커서는 늘 글자 경계, 한 줄 칸은 줄바꿈 · 제어 문자를 받지 않음, 최대 길이(바이트) 넘으면 글자 경계에서 자름.
      확인: `HarukaTextTest` 의 edit 묶음(한글 · 이모지 · 결합 문자 섞인 글).
- [x] T1-7 x 좌표 ↔ 커서: 글자 폭으로 클릭한 x 에 가장 가까운 글자 경계를 찾고, 커서 위치의 x 를 잼(그리기와 같은 폭 함수).
      확인: `HarukaSlateTest`(Pretendard 로 잼).
- [x] T1-8 `TextInputController` 가 버퍼 대신 `HTextEditState` 를 다룸: 인쇄 가능한 모든 코드 포인트를 받음(ASCII 필터 없음), Ctrl+C 는 선택(없으면
      아무것도 안 함), Ctrl+X 잘라내기, Ctrl+V UTF-8 붙여넣기(한 줄로 고침), 화살표 · Home · End · Delete · Backspace 가 OS 자동 반복을 따름,
      Shift 선택, Ctrl+A. 편집 중 Delete 가 액터를 지우지 않음(기존 규칙 유지).
      확인: `HarukaTextTest` 의 가짜 창 묶음(키 · 글자 · 클립보드를 넣어 결과 검사).
- [x] T1-9 그리기: 검색 칸 · 이름 바꾸기 행 · 대화상자 이름 칸이 커서를 제자리에, 선택 범위를 정확한 폭으로 그림(글자 '|' 를 덧붙이지 않음).
      마우스 누름이 커서를 놓고, Shift+누름이 선택을 늘리고, 끌면 선택함(검색 칸 · 대화상자 칸).
      확인: UI 덤프 `--ui-outliner-search` · `--ui-rename` 장면에 커서 사각형 · 선택 사각형, 새 장면 `edit_search_sel`(선택) · `edit_rename_ko`(한글 이름 가운데 커서).
- [x] T1-10 숫자 칸(`SNumberField`)은 그대로 동작(기존 시험 통과).
- [x] T1-11 씬 저장 대화상자의 이름이 모든 언어의 글자를 받음(ASCII 는 예전 규칙, Windows 금지 문자 · 빈 이름 · 깨진 바이트는 거절, 조합형 한글은
      완성형으로). 확인: `HarukaTextTest` 의 scene names 묶음(`project_settings::normalizeSceneName`).
- [x] T1-12 오디오 장치 이름 등 표시용 이름이 한글을 지우지 않음(`hutf::displayLabel`, 예전 `asciiLabel` 은 ASCII 만 남김).

## T2 — Windows IME

- [x] T2-1 `IWindow` 에 IME 사용 켜기 · 끄기, 조합 상태 읽기(조합 글 · 조합 안 커서), 후보 창 자리 지정을 더함. 헤드리스 · Android 창은 빈 구현.
- [x] T2-2 GLFW 창(Win32)이 창 프로시저를 감싸 `WM_IME_STARTCOMPOSITION` · `WM_IME_COMPOSITION`(GCS_COMPSTR · GCS_CURSORPOS) ·
      `WM_IME_ENDCOMPOSITION` 을 받고, 기본 조합 창을 숨김(`WM_IME_SETCONTEXT`). 확정 글은 지금처럼 WM_CHAR 로 들어옴.
- [x] T2-3 입력 칸에 포커스가 없으면 IME 를 끔(`ImmAssociateContextEx`). 한글 모드에서도 W · E · R · F · Delete 같은 에디터 단축키가 먹힘.
      포커스를 얻으면 켬. 확인: 코드 · 시험(가짜 창이 켜고 끈 기록), (창) 한글 모드에서 W 키로 이동 기즈모.
- [x] T2-4 편집 칸이 조합 글을 커서 자리에 밑줄로 그림, 조합 중에는 선택 범위를 지우지 않다가 확정 때 바꿈.
      확인: `HarukaTextTest`(가짜 조합), 덤프 옵션 `--ui-ime-compose=<글>` 장면 `edit_rename_ime` 에 밑줄 요소.
- [x] T2-5 후보 창(한자 변환) · 조합 창 자리를 커서 아래로 알림(`ImmSetCompositionWindow` · `ImmSetCandidateWindow`), 화면 배율 반영.
- [ ] T2-6 (창) 한국어 IME 로 이름 바꾸기 · 검색에 "한글 입력" 을 치고 Backspace 로 자모가 지워지고, 한자 변환 후보 창이 칸 아래에 뜸.
- [ ] T2-7 (창) 일본어 · 중국어 IME 조합 · 변환(설치되어 있다면).

## T3 — 표시 글 HText (FText 대응)

- [x] T3-1 `src/core/HText.hpp/.cpp`: 공유 불변 값. 종류 = 원문 그대로(번역 안 함) · 번역(namespace · key · 원문) · 서식(틀 + 인자) · 숫자.
      `toString()` 은 지금 문화권으로 풀고 캐시(문화권 판 번호가 바뀌면 다시 풂). 복사가 가벼움(shared_ptr). 빈 값 · 같음 비교.
      확인: `HarukaTextTest` 의 htext 묶음.
- [x] T3-2 `HTEXT("ns", "key", "원문")` 매크로, `HText::fromString`(원문 그대로), `HText::invariant`.
- [x] T3-3 `HTextTable`(문화권 표): `.po` 읽기(msgctxt = "ns,key", msgid = 원문, msgstr = 번역, 여러 줄 · 이스케이프), 엔진
      `/Engine/Localization/<문화권>/Engine.po` 와 프로젝트 `Content/Localization/<문화권>/Game.po` 를 겹쳐 읽음(프로젝트가 이김).
      원문이 표와 다르면(코드 원문이 바뀜) 번역을 쓰지 않고 원문을 보임(UE 와 같음). 확인: `HarukaTextTest`.
- [x] T3-4 문화권: 기본 `en`, 에디터 상태 `Config/editor_state.txt` 의 `culture=`, 명령줄 `--culture=ko`. 실행 중에 바꾸면 판 번호가 올라가고 화면이 다시 그려짐.
- [x] T3-5 서식: `HText::format(틀, {인자...})` — `{0}` · `{이름}` 자리, `{{` · `}}` 이스케이프, 인자는 HText · 정수 · 실수 · 문자열.
      숫자는 문화권별 묶음 기호(en · ko "1,234.5", de "1.234,5"). 확인: `HarukaTextTest`.
- [x] T3-6 위젯: `STextBlock::text(HText)`, 툴팁 `tooltip(HText)`, 메뉴 항목이 HText 를 받음(`std::string` 도 계속 받음). 문화권이 바뀌면 크기를 다시 잼.
      확인: `HarukaSlateTest`(문화권을 바꾸면 desired size 가 바뀜).
- [x] T3-7 에디터 글을 HText 로 옮김: 메인 메뉴 · 하위 메뉴 항목, 툴팁(D5b), 패널 · 탭 제목, 메인 · 뷰포트 툴바 글, 대화상자 단추 · 제목.
      (디테일 속성 이름 · 로그 · 액터 이름은 옮기지 않음.) 확인: grep 표 · 덤프.
- [x] T3-8 한국어 표 `Engine/Content/Localization/ko/Engine.po` 가 T3-7 의 모든 키를 덮음. 확인: 시험이 소스의 HTEXT 키와 표를 맞대어 빠진 키 0.
- [x] T3-9 모으기 도구: `HarukaPak gather-text <src 폴더...> -o <pot>` 가 `HTEXT(...)` 를 찾아 `.pot` 를 씀(같은 키 · 다른 원문이면 오류).
      확인: 도구 시험 · 실제 실행으로 `Engine/Content/Localization/Engine.pot` 생성.
- [x] T3-10 에디터 메뉴 Edit > Language(English · 한국어)로 바로 바뀌고 설정에 저장. 확인: 덤프 `--culture=ko` 장면 `menu_ko` · `default_ko` · `hover_play_ko`, 넘침 점검, 화면 `uishot` `default_ko`.

## T4 — 글꼴 사슬 · 아틀라스 · 게임 쪽

- [x] T4-1 대체 글꼴 사슬: 주 글꼴(면마다) 뒤에 대체 글꼴 목록. 글자마다 글리프를 가진 첫 글꼴을 골라 캐시하고, 폭 재기와 그리기가 같은 글꼴을 씀.
      TTC(글꼴 모음)는 첫 글꼴을 씀. 기본 목록: Windows `malgun.ttf`(한자 일부) · `YuGothR.ttc`/`meiryo.ttc`(가나) · `msyh.ttc`(중국어) ·
      `seguisym.ttf`(기호), Android `/system/fonts/NotoSansCJK-Regular.ttc` · `DroidSansFallback.ttf`. 없는 파일은 건너뜀.
      확인: `HarukaSlateTest`(이 PC: "漢字かな" 폭 > 0 이고 글리프가 그려짐, 대체 글꼴이 하나도 없을 때도 멈추지 않음).
- [x] T4-2 어디에도 없는 글자는 주 글꼴의 .notdef(네모)를 그리고, 코드 포인트를 한 번만 경고. 굵게 면에 없으면 보통 면 사슬을 씀.
- [x] T4-3 아틀라스가 가득 차면 같은 크기의 페이지를 더 엶(최대 8장). 비우고 다시 채우는 방식(UE FSlateFontCache)은 쓰지 않음: 에디터 UI 와
      3D 패널이 한 아틀라스를 다른 시점에 쓰므로 패널의 그리기 목록이 낡은 자리를 가리킬 수 있음. 글리프는 자기 페이지 텍스처로 그림.
      확인: `HarukaSlateTest`(2048 x 64 아틀라스에 한글 300자 → 페이지가 늘고 모두 그림, 8장을 넘을 때만 넘침).
- [x] T4-4 게임 쪽: 빌드 출력(`build/<구성>/Engine/Content`)과 패키지(Windows · APK)에 엔진 콘텐츠(글꼴 · 아이콘 · 번역)를 담고, 패널 전용
      UI 렌더러의 기본 글꼴을 `/Engine/Fonts/Pretendard-Regular.ttf` 로(없으면 지금의 시스템 글꼴). 확인: 패키지 목록 · base7 의 panel 로그 · 그림
      (기준 PNG 2장 · 오른눈 2장이 바뀌는 것은 의도, 새 해시를 적음).
- [x] T4-5 3D 패널에 한글 글이 보임. 확인: 시험 또는 UIPanelProbe 덤프에서 한글 글리프 요소.

## G — 공통 검증

- [x] G-1 Release · Debug 경고 0, ctest 두 구성 통과(검사 수 기록), `HarukaArchTest` PASS · 허용 0(새 파일의 층 규칙 지킴: core 는 ui 를 모름).
- [x] G-2 `base7.sh`: T4-4 전까지 7장 그대로, T4-4 뒤 바뀐 장만 이유와 함께 새 기준.
- [x] G-3 `uidump.sh`: 기존 장면 `_vp` 같음, UI 차이는 커서 · 선택 그리기(T1-9) · 문화권(T3) 때문인 줄만. `uidump_overflow.py` 늘어난 곳 없음(ko 장면은 따로 보고).
- [x] G-4 `uishot.sh` 두 번 해시 같음 · 검증 0.
- [x] G-5 문서: 이 파일 결과 절, `Docs/Engine_Specification.md` 검사 수, `Docs/Editor_Layout_Design.md` 에 편집 칸 규칙.
- [x] G-6 보이는 창을 띄우지 않음. `(창)` 줄은 모아서 사용자에게 알림.

## 결과 (2026-11-19 밤, 미커밋)

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0 |
| ctest | 두 구성 29/29 통과, 검사 2,296(새 `text_system` 121, `slate_input` +35) |
| 아키텍처 | `HarukaArchTest` PASS(검사 28), 허용 0 |
| 기준 PNG 7장(`base7.sh tx`) | 5장 그대로(ce6def24 · c283f276 · 5d6e3e5f · 9cf28353 · 201fad0e), `panel` 7788eb5f → **73dd1f02**, `panelR` 559660c0 → **b4bd1fee**(T4-4 의도: 패널 글꼴 Segoe UI → Pretendard, 로그 `Font atlas ready: 2048x512 (/Engine/Fonts/Pretendard-Regular.ttf)`), 검증 오류 0 |
| UI 덤프(`uidump.sh tx d5bv`) | 47장면(41 + 편집 3 + 한국어 3). 기존 41장면 `_vp` 모두 같음, UI 차이 5장면: `outliner_search` · `outliner_rename`(편집 칸 그림), `menu_File` · `menu_Edit` · `menu_View`(단축키 열 · 언어 항목) |
| 넘침(`uidump_overflow.py`) | 늘어난 넘침은 새 `edit_rename_*` 두 장면의 디테일 스크롤 아래쪽(기존 `outliner_rename` 과 같은 23줄)뿐, 한국어 장면 넘침 0 |
| 화면 점검(`uishot.sh`) | 11장면(9 + `default_ko` · `edit_ko`), Release 두 번 · Debug 한 번 해시 모두 같음, 검증 오류 0. 그림 `build/chk/uishot/txa_default_ko.png` · `txa_edit_ko.png` |

**새 파일:** `src/core/Utf8.hpp` · `HText.hpp/.cpp` · `HTextGather.hpp`, `src/ui/slate/HTextEdit.hpp` · `widgets/SEditableText.hpp`,
`tests/TextTest.cpp`, `Engine/Content/Localization/Engine.pot` · `ko/Engine.po`.

**남긴 것(범위 밖 · 다음 후보):**
- `(창)` 줄 T2-6 · T2-7 · T2-3 의 창 확인: 한국어 IME 로 이름 바꾸기 · 검색 입력, 자모 Backspace, 한자 후보 창 자리, 한글 모드에서 W · E · R.
- 번역하지 않은 에디터 글: 디테일 패널(섹션 · 속성 이름 · "(No Selection)" · Add Component), 출력 로그 단추(Categories · Err · Warn · Clear),
  콘텐츠 브라우저, 오디오 출력 대화상자의 목록 항목, 상태 바. 옮길 때 `HTEXT` 로 쓰고 `gather-text` → `ko/Engine.po`.
- 커닝 · HarfBuzz 조합 · 양방향 글 · 컬러 이모지 · 여러 줄 편집 칸 · 칸 안 실행 취소 · 커서 깜박임 · Android 소프트 키보드 · VR 가상 키보드.
- 같은 프레임에 이동 키와 지우기 키가 함께 들어오면 지우기가 먼저 적용됩니다(키 사건 순서를 모르므로). 보통 프레임 속도에서는 나타나지 않음.
