# 에디터 여러 OS 창 · HSlate 개선 설계 (2026-11-21)

에디터 탭을 떼어 **별도 OS 창**(UE · Unity · Godot 4 방식)으로 띄우고, 그 바탕이 되는 HSlate 입력 · 포커스 · 팝업 구조를 고칩니다.
이어서 메뉴 · 명령 체계(H2)와 디테일 패널 보강(H3)을 같은 기반 위에 올립니다.

## 1. 조사 요약

- 어려운 곳은 렌더러가 아니라 **입력과 에디터**입니다.
  - `GlfwWindow` 객체가 `glfwInit` · `glfwTerminate`를 직접 부르고, `pollEvents`가 자기 창의 걸쇠만 비운 뒤 모든 창의 이벤트를 받습니다(`src/platform/glfw/GlfwWindow.cpp`). 지금 클래스로는 창을 하나 더 만들 수 없습니다.
  - 엔진 루프 · 단축키 · 글 입력 · 휠 · 끌어 놓기가 모두 창 하나(`EditorContext::window`)를 폴링합니다.
- **HSlate의 약점**(위젯 트리 밖에서 처리하는 것)
  - 키보드 이벤트 · 포커스가 위젯에 없습니다. 포커스는 `EFocusTarget` 열거 9개를 손으로 잇습니다(`src/editor/TextInputController.hpp`).
  - 마우스 판정을 위젯 밖에서 합니다. 에디터 `.cpp`에서 `cachedGeometry()`를 직접 부르는 줄이 56개입니다(도크 탭 · 스플리터 · 팝업 바깥 누름 · 끌어 놓기).
  - 팝업 · 메뉴 앵커가 없어 팝업 네 종류가 각자 호스트 · 바깥 누름 · 위치를 구현합니다.
  - arrange를 프레임당 최대 3회 하고 `computeDesiredSize`에 캐시가 없습니다. 스플리터를 끄는 동안 도크 위젯 트리를 통째로 다시 만듭니다.
  - 콤보 박스 · 벡터 칸 · 색 선택기 · 트리 뷰 · 가상화 목록 · 분할기 위젯이 없습니다. 반사 속성 종류는 Float · Toggle · Enum · ActorRef 넷입니다.
- **렌더러**는 프레임 펜스와 제출을 하나로 두고 창마다 표면 · 스왑체인 · 세마포어 · `imagesInFlight`만 더하면 됩니다. `HSlateRenderer`의 패널 경로(`buildPanel(HSlateContext&)` · `recordPanel`)가 이미 외부 문맥을 그립니다.
- **도크 배치 파일**은 파서가 트리 하나를 읽은 뒤 남은 글을 보지 않으므로, 뒤에 떠 있는 창 블록을 덧붙이면 옛 빌드와 호환됩니다.

## 2. 결정 (2026-11-21)

| 번호 | 항목 | 결정 |
|---|---|---|
| D1 | 창별 DPI 배율 | 처음에는 모든 창이 메인 배율 하나를 씁니다. 문맥 단위 렌더 변환(창별 배율)은 나중 단계입니다. |
| D2 | present | 창마다 따로 합니다. 메인을 먼저, 보조를 나중에 하고, 보조 실패는 그 창만 다시 만듭니다. |
| D3 | 보조 창 표시 모드 | MAILBOX → IMMEDIATE → FIFO, acquire 타임아웃 0(준비가 안 되면 그 프레임은 건너뜀), 그리기 목록 해시가 같으면 그리지 않습니다. |
| D4 | 떠 있는 창의 OS 닫기 | 안의 탭을 모두 닫습니다(UE). Window 메뉴로 원래 자리에 다시 엽니다. |
| D5 | 떠 있는 창의 팝업 | 그 창 문맥 안에 그립니다(H2와 함께). 팝업을 OS 창으로 띄우는 일은 나중입니다. 모달은 메인 창에 띄우고 모든 창의 입력을 막습니다. |
| D6 | 순서 | 3절 표의 순서입니다. |
| D7 | Play 중 보조 창 포커스 | 게임 입력을 막습니다(UE PIE와 같음). |
| D8 | 떼기 금지 패널 | Viewport만 막습니다(`PanelDef::floatable`). |
| D9 | 배치 파일 머리말 | `# Haruka Dock Layout v1`을 그대로 두고 메인 트리 뒤에 `F` 블록을 덧붙입니다. |

## 3. 단계

단계마다 빌드 2 ~ 3회 크기입니다. 회귀 기준은 ctest 전체 통과, UI 덤프 `build/chk/uidump.sh`가 기준과 같음(바뀐 장면은 이유를 적음), 검증 오류 0입니다.

| 단계 | 내용 | 헤드리스 확인 | 모니터 확인 |
|---|---|---|---|
| **H0** 플랫폼 펌프 | GLFW 수명 · 이벤트 펌프를 창에서 떼어 냄(`GlfwPlatform`), `IWindow`에 창 위치 · 크기 · 내용 배율 · 포커스 · 보이기(기본 구현), 보조 창 팩토리 | ctest, UI 덤프 동일 | 짧은 클릭(DEF-82) · 백스페이스 반복(DEF-58) · IME |
| **H1a** 경로 · 포커스 | 컨테이너 `forEachChild`, 히트 경로, 키보드 이벤트 · 포커스 위젯, 글 칸 · 숫자 칸을 새 포커스로 | `slate_input`에 경로 · 포커스 · 키 전달 시험, UI 덤프 동일 | 글 칸 · 숫자 칸 · 검색 칸 · 콘솔, 한글 IME |
| **M1** 도크 다중 트리 | `DockFloat`, 트리 사이 순수 연산, `F` 블록 직렬화, Viewport 거부 | `dock_tree`: 떼기 · 합치기 · 왕복 · 옛 파서 호환 · 중복 제거 | 없음 |
| **M2** 렌더러 창 출력 | 창별 스왑체인 · 세마포어, 보조 창 UI 지우기 패스 · 창별 프레임버퍼, 글꼴 아틀라스 업로드 1회, 정점 예산 | 헤드리스 렌더 · 합성 눈 PNG 그대로, 숨은 보조 창으로 검증 오류 0 | 보조 창 표시 · 최소화 · 크기 변경 · 닫기, 메인 FPS 유지 |
| **H1b** 배치 캐시 | 원하는 크기 캐시, 창마다 arrange 1회, 지연 변경 큐, 스플리터는 비율만 고침 | 시험 · UI 덤프 동일 | 스플리터 끌기 |
| **M3** 에디터 창 단위 | `EditorWindow{창, 문맥, 루트, 팝업 층, 도크 트리}`, 창별 입력 분배, 키보드는 포커스 창, 배치 저장 · 복원 | `F` 블록 배치로 창별 덤프, 메인 덤프 동일 | 떠 있는 창에서 버튼 · 스크롤 · 글 입력 · 단축키 · 재시작 복원 |
| **M4** 탭 떼기 · 붙이기 | 화면 좌표 끌기 상태, 창 밖 놓기 → 새 창, 다른 창 리프 → 재도킹, 마지막 탭이 빠진 창 닫기 | 끌기 상태 기계 시험 | 4절 1 ~ 3, 7, 9 |
| **H2** 팝업 층 · 메뉴(1단계) | 문맥별 팝업 스택, `SMenuAnchor`, 메뉴 모델(소제목 · 구분선 · 하위 메뉴 · 비활성 · 아이콘), `EditorCommandRegistry`, 메뉴 키보드 조작, 메뉴 재배치 | 명령 레지스트리 시험, 메뉴 덤프 새 기준 | 메뉴 · 하위 메뉴 · 키보드 조작 |
| **H3** 디테일 보강(5단계) | `PropDesc` 종류 확장 · 기본값, `SPropertyRow`, `SComboBox`, `SVectorField`, 에셋 칸, 색, 속성 검색, 기본값 되돌리기 | `slate_input` · `editor_commands`, 덤프 새 기준 | 칸 편집 감각, 색 선택기 |

H2를 마친 뒤 결과 화면을 보고 설정 표 · 설정 창 · 특수 페이지(2 ~ 4단계)를 정합니다.

## 4. 모니터 확인 목록 (M2 ~ M4 뒤)

1. 탭을 창 밖에 놓으면 새 창이 커서 자리에 생기고, 끄는 동안 미리보기가 커서를 따라옵니다.
2. 떠 있는 창을 다른 모니터로 옮겨도 글자와 마우스 판정이 맞습니다.
3. 떠 있는 창에 탭을 더 붙이거나 메인으로 되돌립니다. 마지막 탭을 빼면 창이 닫힙니다.
4. 떠 있는 창을 최소화 · 최대화 · 복원하고 크기를 바꿉니다. 메인 창 최소화와 섞어도 됩니다(DEF-106).
5. 창이 둘일 때 메인 FPS가 떨어지지 않습니다. 주사율이 다른 모니터에 나눠 둬도 같습니다.
6. 보조 창에 포커스가 있을 때 Ctrl+S · Ctrl+Z · F 단축키와 한글 IME 조합 · 후보 창 위치가 맞습니다.
7. 떠 있는 콘텐츠 브라우저에서 메인 뷰포트로 끌어 놓고, 떠 있는 아웃라이너에서 행을 끌어 놓습니다.
8. 재시작 뒤 배치가 복원되고, 모니터 하나를 뺀 뒤 실행하면 창이 보이는 자리로 옵니다.
9. 떠 있는 창의 OS 닫기 단추가 D4대로 동작합니다.
10. XR Play 중 보조 창이 있어도 헤드셋 프레임이 떨어지지 않습니다(Quest Link).

## 5. 위험

- 걸쇠 비우기 순서가 틀리면 DEF-82(한 프레임 안의 누름 · 뗌을 놓침)가 다시 생깁니다. 펌프 직전에 모든 창에 대해 한 번만 비웁니다.
- 에디터 입력 순서 규칙(포커스 해제 → 팝업 바깥 → 끌기 → 메뉴 hover → overPopup → 스플리터 · 탭 → 휠 → 기즈모 → UI)은 DEF-56 · 57 · 64 · 65 · 76 · 77 · 82 · 83 · 84 · 92를 고치며 생겼습니다. H1a · M3에서 이 순서를 깨지 않습니다.
- 보조 창 UI 패스는 앞에 3D 패스가 없으므로 지우기 패스(`UNDEFINED → PRESENT_SRC`)를 따로 씁니다. 메인 패스를 지우기로 바꾸면 3D 화면이 지워집니다.
- 글꼴 아틀라스 업로드가 앞 프레임의 읽기와 겹칠 수 있다는 의심이 있습니다(추정). M2에서 확인합니다.
- 창 생성 · 파괴는 이벤트 펌프 밖(프레임 끝)에서 합니다.
- HarukaGame · Android는 보조 창 목록이 비어 있어 지금 경로와 같아야 합니다(`headless_render` · `vulkan_imports`로 확인).

## 6. 진행 기록

| 단계 | 상태 | 비고 |
|---|---|---|
| 조사 | 끝 (2026-11-21) | |
| H0 플랫폼 펌프 | 구현 · 검증 (2026-11-21, 미커밋) | `GlfwPlatform`(참조 계수 수명 · 모든 창 걸쇠 비운 뒤 펌프 1회) · `IWindow` 창 관리 9개(기본 구현) · `createAuxWindow`. 시험 45(새 `window_platform`) · 검사 3,641(+38), 경고 R0 · D0, UI 덤프 72장면 `c_04`와 같음 · 검증 오류 0. 모니터 확인(짧은 클릭 · 백스페이스 반복 · IME) 남음 |
| H1a-1 경로 · 키 · 포커스 기반 | 구현 · 검증 (2026-11-21, 미커밋) | `forEachChild`(그리는 순서) · `hitTestVisible` · `clipsChildren`, `HWidgetPath` · `HSlateContext::hitTest`, `HKeyEvent` · `HCharEvent`, 포커스 위젯(약한 참조 · 키 버블 · 글자는 포커스만 · `HReply` 포커스 요청은 처음 요청이 이김 · 누르면 포커스 기본 켜짐). 에디터 동작은 그대로. `slate_input` +61 → 검사 3,702, 경고 R0 · D0, UI 덤프 72장면 `c_04`와 같음 · 검증 오류 0. 변이 확인 4개 모두 실패로 잡힘 |
| H1a-2 글 칸 포커스 이전 | 구현 · 검증 (2026-11-21, 미커밋) | 글 칸 6종(Rename · Search · LogSearch · Console · Picker · ContentSearch)의 "어느 칸인가"를 에디터 문맥의 HSlate 포커스 위젯으로 옮김. `TextInputController::Deps` 의 칸별 함수 6개 → `TextFieldBinding` 목록(이름표 · 위젯 · 칸), `currentFocus()` 는 포커스 위젯에서 이름표를 계산(패널 거울 값이 꺼지면 None, 예전과 같음), 새 칸은 이름표 `TextField` + `setFocusWidget`. `HSlateContext::setFocusHook`(바깥 변경 → 같은 부수 효과), 루트 교체는 `preserveFocusAcross`(확정 · 취소 없음), 글 칸 위젯은 `HReuse` 로 같은 객체, 에디터 문맥은 누르면 포커스 끔. NumberField · Dialog 그대로. 검사 3,739(+37: `text_system` +27 · `slate_input` +10), 경고 R0 · D0, UI 덤프 72장면 `c_04`와 같음 · 검증 오류 0, 변이 5개 모두 잡힘. 모니터 확인 남음 |
| M1 도크 다중 트리 | 구현 · 검증 (2026-11-21, 미커밋) | `DockFloat{번호, 트리, x · y · w · h, 최대화}` · `EditorDock::floats_` · `PanelDef::floatable`(Viewport 만 false), 순수 연산(떼기 `dockMoveToFloat` · 트리 사이 `dockRedockAcross` · 통째 합치기 `dockMergeFloatInto` · 창 닫기 `dockCloseFloat`(D4, home 은 예전 기록 → 같은 창 순) · `dockClosePanelInFloat` · 모든 트리 찾기), `F` 블록 직렬화(떠 있는 창이 없으면 v1 과 같은 글) · 새 파서 `dockParseLayoutAll`(seen 공유 · 메인 우선 · 창 안 떼기 불가 패널 거름 · 깨진 블록만 버림 · 크기 고침, 화면 밖 끼우기는 M3). 떠 있는 창은 아직 화면에 안 나오고 왕복 저장만 함, 그 패널의 Window 메뉴 열기는 M3 전까지 아무것도 안 함. `dock_tree` +90 → 검사 3,829, 경고 R0 · D0, UI 덤프 72장면 `c_04`와 같음 · 검증 오류 0, F 블록 배치 숨은 창 실행 · 저장 왕복 확인, 변이 4개 모두 잡힘 |
| M2 렌더러 창 출력 | 구현 · 검증 (2026-11-21, 미커밋) | 보조 창 목록 `WindowTarget`(`Renderer_window.cpp`, 메인 경로 그대로 · 목록이 비면 새 코드 건너뜀): `addWindowTarget` · `removeWindowTarget` · `setWindowUi` · `requestWindowRedraw` · `windowTargetStats`. 메인 acquire 뒤 · 펜스 리셋 전에 창마다 목록 → 해시 → acquire(타임아웃 0, `GlfwSwapchain::setAcquireTimeout`), 제출 1회에 받은 창 세마포어만, 표시는 메인 뒤 창마다 따로, 실패 · 크기 변경은 그 창 스왑체인 · 프레임버퍼만. 표시 방식 MAILBOX → IMMEDIATE → FIFO, 보조 스왑체인은 프레임 계측 값을 덮어쓰지 않음. `HSlateRenderer` 창 패스(UNDEFINED · CLEAR → PRESENT_SRC, 외부 의존을 메인 패스와 같게 해 `pipeline_` 공유 — Debug 검증이 의존 차이를 비호환으로 잡아 맞춤), `buildWindow`(업로드 없음) · `recordWindow`, 형식이 다르면 그 창을 만들지 않음. 아틀라스 업로드는 따로 제출 + 큐 대기라 앞 프레임과 겹치지 않음(확인), 보조 목록을 메인 record 전에 만들어 업로드 1회. 정점: 72장면 최대 5,628(목록 하나 · 슬롯 모두) → 에디터 슬롯 버퍼 131,072(목록 상한 65,536 의 2배), 게임 패널 UI 는 그대로. 임시 `--ui-aux-probe[=cycle]`(`EditorAuxProbe.cpp`, 덤프 `<덤프>.aux.txt`, M3a 에서 지우고 `--ui-float-cycle` 로 대신). `slate_input` +21 → 검사 3,850(grep 3,851, 1 은 M1 출력의 줄 섞임), 경고 R0 · D0 · Android 새 경고 0, base7 7장 같음, UI 덤프 72장면 `c_04`와 같음 · 검증 오류 0, 숨은 보조 창 R · D: 120 프레임 중 표시 13 · 해시 같음 107 · 검증 오류 0, cycle(지우기 · 다시 만들기) 검증 오류 0. 모니터 확인 남음 |
| H1b 배치 캐시 | 구현 · 검증 (2026-11-22, 미커밋) | 원하는 크기 캐시: `HWidget::desiredSize()`(측정 패스마다 `computeDesiredSize` 1회, 패스 밖은 늘 새로 잼), 컨테이너 9종이 자식에게 `desiredSize()` 를 부름(Auto 슬롯 이중 측정 없어짐). 패스는 `HSlateContext::arrange` 가 시작할 때마다 새로 받음(위젯 단위 무효화 없음). 배치 건너뛰기: 크기 · 루트 · 배치 변경 알림(`hslate::noteLayoutChange`: 슬롯 추가 · 비우기 · 값, 내용 · 여백 · 정렬, 스크롤 위치, 펼침, `HReuse`) · 누름 · 뗌 · 휠 · 키 · 글자 이벤트가 없고 트리의 모든 원하는 크기가 직전 패스와 같을 때만 `onArrangeChildren` 을 건너뜀(`layoutGeneration` 그대로), 이동 이벤트는 무효화 안 함. 지연 변경 큐 `deferred(fn)` · `flushDeferred()`(전달 중에는 실행 안 함, 시작 때 쌓인 것만, 전달 중 맡긴 일은 다음 arrange 앞에서). 하단 패널 리빌드(`bottomDirty_`)만 옮김(같은 소진 자리), 나머지 패널 플래그는 소진 순서 · 조건 때문에 그대로. `DeferredRebuildGuard` 는 `handleInput` → `handleInputEvents` + 소진으로 바꿔 없앰. 스플리터: 끄는 동안 `rebuildArea()` 대신 상자 `setSlotValue`(못 찾으면 예전처럼 리빌드), 놓을 때 저장 그대로. 계측 `--ui-layout-stats[=nocache]`. 기본 장면 덤프 실행 프레임당: 실제 배치 2.00 → 0.01, 측정 3,374 → 1,032, arrange CPU 0.168 → 0.066 ms(arrange 호출은 2 그대로). `slate_input` +38 → 검사 3,889, 경고 R0 · D0 · Android 새 경고 0, base7 7장 같음, UI 덤프 72장면 `c_04`와 같음 · 검증 오류 0, 숨은 보조 창 R · D 표시 13 · 해시 같음 107 · 검증 오류 0, 변이 4개 모두 잡힘. 모니터 확인(스플리터 끌기) 남음 |
| M3a 창 단위 · 떠 있는 창 표시 | 구현 · 검증 (2026-11-22, 미커밋) | 떠 있는 창(F 블록)마다 숨김 규칙을 따르는 OS 창 + 문맥 + 렌더러 대상(`EditorFloatWindows` · `EditorWindow`, 입력 없음, 메인은 이 단위로 옮기지 않음). `EditorDock` 은 창마다 `WindowState`(호스트 · 히트 목록 · 끌기 상태), `rebuildArea` 가 모든 창 호스트를 먼저 비운 뒤 메인 → 떠 있는 창 순으로 만들어 패널이 두 루트에 동시에 있지 않음, 떠 있는 창 루트는 탭 줄 + 패널, 제목 = 첫 리프 활성 탭. 보임 여부는 떠 있는 창 활성 탭도 셈, 메인 좌표 판정(아웃라이너 끌기 · 콘텐츠 누름 · 오른쪽 메뉴)은 `panelActiveInMain` 만. 생성 · 파괴 · OS 닫기(D4 `closeFloat`)는 `syncFloatWindows`(handleInput 끝 · 최소화 중 `tickWhileMinimized` · init), 정리 순서 렌더러 대상 → OS 창. 화면 밖 자리는 `hwinplace::clampToWorkAreas`(`GlfwPlatform::monitorWorkAreas`), 저장 때 OS 창 자리 · 크기를 읽음(내용 영역 기준), Window 메뉴 → 그 창 앞으로. 메인 최소화 비 XR 프레임은 보조 창만 그림(`hwin::chooseDesktopFrame` · `drawAuxOnlyFrame`). 창별 덤프 `<덤프>.float<번호>.txt`. `--ui-aux-probe` 지우고 `--ui-float-cycle`(60 프레임 OS 닫기 → 90 프레임 배치 다시 읽기). 거울 값 직접 끄기 4곳 → `TextInputController::releaseQuiet`(부수 효과 없음). 종료 때 UI 정리 전 장치 대기(떠 있는 창 없는 Debug 자동 실행의 종료 검증 오류 6 · exit 3, 기존 결함). 검사 3,946(+57: `slate_input` +38 · `window_platform` +13 · `text_system` +6), 경고 R0 · D0, base7 7장 같음, UI 덤프 72장면 `c_04` 와 같음 · 검증 오류 0, F 블록 배치 3종 R · D 메인 덤프 = F 없는 배치 · 검증 오류 0, cycle R · D 검증 오류 0, 변이 5개 모두 잡힘. 모니터 확인 남음 |
| M3b 창별 입력 분배 | 구현 · 검증 (2026-11-22, 미커밋) | 떠 있는 창마다 자기 OS 창의 커서 · 버튼 · 휠을 읽어 그 창 문맥에 보냄(`readFloatPointers` → 메인 `handleInputEvents` 예전 순서 그대로 → `handleFloatInputs`: 아웃라이너 끌기 → 포커스 해제 → 팝업 바깥(메인 팝업 · 메뉴 닫기) → 콘텐츠 누름 → 스플리터 · 탭 → 휠 → UI, 누름 엣지는 창마다 `prevLeftDown` · `prevRightDown`). 순수 규칙 `editor/EditorWindowInput.hpp`(`hwininput`: 엣지 · `keyboardWindow` · 글 칸 · 패널 포인터 · D7 · D5 · 창 사이 좌표 · 시험 대본). `EditorDock::handleSplitterInput` · `handleTabInput` 은 창 번호 → 그 창 `WindowState&`(같은 창 재도킹은 그 창 트리, 창 밖에 놓기는 메인과 같이 무동작 — M4), 떠 있는 창 루트 = `SOverlay{호스트, 탭 끌기 미리보기, 아웃라이너 미리보기}`, `floatWins_` 는 주소가 안 바뀌게 `unique_ptr`. 패널 판정은 그 패널이 있는 창 좌표로(`panelActiveInMain` 가드 4곳 → 창별, 아웃라이너 수정자 키 · 휠 중 좌버튼도 그 창), 떠 있는 콘텐츠 브라우저의 끌기는 화면 좌표로 메인 뷰포트에 놓음(커서가 그 창 위면 놓지 않음). 키보드 창 = OS 포커스 떠 있는 창, 없으면 메인: `TextInputController` 가 그 창에서 키 · 글자를 읽고 칸은 그 칸이 있는 창에 키보드가 있을 때만 받음, 창이 바뀌면 엣지를 다시 심음, IME 는 칸이 있는 창만 켜고 후보 창 자리도 그 창(IME 훅은 이미 창마다), 단축키 · Ctrl+F · F2 는 키보드 창 + 패널이 떠 있으면 그 창을 앞으로. D7 `blocksGameInput`(키 · 마우스 시선 · 월드 패널 마우스, 패드 · XR 제외), D5 모달 중 떠 있는 창 입력 없음 · 모달을 열 때 메인을 앞으로. 팝업은 떠 있는 창에서 열지 않음(Add Component · Categories · 오른쪽 메뉴, 창별 팝업 층은 H2). 메인은 `EditorWindow` 로 옮기지 않음. 시험 대본 `--ui-float-input`. 검사 4,026(+80: `slate_input` +64 · `text_system` +16), 경고 R0 · D0 · Android 새 경고 0, base7 7장 같음, UI 덤프 72장면 `c_04` 와 같음 · 검증 오류 0, F 블록 3종 R · D 메인 덤프 = F 없는 배치 · 떠 있는 창 덤프 = M3a 기준, 입력 흉내 10종 R · D 검증 오류 0(탭 · 스플리터 · 휠 · 선택 · 재도킹 · 탭 닫기 · 부모 바꾸기 · 팝업 막힘 · 모달 · D7), cycle R · D 검증 오류 0, 변이 7개 모두 잡힘. 모니터 확인 남음 |
| M4 탭 떼기 · 붙이기 | 구현 · 검증 (2026-11-22, 미커밋) | 탭 끌기 놓을 자리를 화면 좌표로: 누른 창 좌표 + 그 창 내용 영역 화면 자리(D1 이라 배율 없음), 놓을 창 = 커서 아래 창들 중 가장 앞(원래 창 포함). Z 순서 대안 = 포커스를 받은 순서(`hdockdrag::FrontOrder`: 처음 보는 창 · 포커스가 새로 온 창 · 에디터가 앞으로 가져온 창이 맨 앞, 최소화 창은 후보 아님). 순수 규칙 `editor/dock/DockDragRules.hpp`(`pickDropWindow` · `zoneInLeaf` · `decideDrop` · `dragCursor` · `newFloatRect` · `emptyFloats` · `issueFloatId`), 도크는 `Deps::dropWindows` · `clampFloatRect` 로 창 자리를 받음(없으면 예전처럼 같은 창만). 놓기: 같은 창 리프 = `dockRedock`, 다른 창 리프 = `dockRedockAcross`(메인 ↔ 떠 있는 창 · 떠 있는 창 ↔ 떠 있는 창), 어느 창 위도 아님 = `dockMoveToFloat` 로 새 창(번호 = 이번 실행 최댓값 + 1, 잡은 자리가 커서 아래, 크기 = 원래 리프 320 ~ 1280 × 240 ~ 900, `clampToWorkAreas`), 다른 창의 리프 아닌 곳 = 무동작, Viewport 는 창 밖 · 떠 있는 창 모두 거부(D8). 떠 있는 창 탭 줄 빈 곳 끌기 = 창 통째 `dockMergeFloatInto`(창 밖 · 자기 창은 무동작, 창 옮기기는 OS 제목 줄). 놓기 전에 끌기 상태를 비우고 이동 뒤 빈 떠 있는 창을 빼며(끌기 상태 먼저, closeFloat 와 같은 순서) OS 창 · 렌더러 대상은 프레임 끝, 놓은 창을 앞으로. 끄는 중 놓을 창이 닫히면 그 자리는 무동작(다시 움직이면 새로 고름). 미리보기: 리프 위 = 놓을 창의 `overlay`, 창 밖 = 커서 모양(3 이동 = 새 창, 4 금지 = 거부, 누른 창이 커서를 잡은 동안 Win32 `SetCursor` 로 창 밖에서도 보임), 끄는 동안 다른 창은 커서를 안 바꿈. 끄는 중 다른 창을 앞으로 가져오지 않음(GLFW 는 포커스를 잃은 창의 버튼을 뗌으로 처리해 끌기가 끊김). 떠 있는 콘텐츠 → 메인 뷰포트 놓기도 같은 앞뒤 규칙. 시험 대본: 창 0 = 메인 포인터, `place@x,y`. 검사 4,116(+90: `dock_tree` +51 · `slate_input` +39), 경고 R0 · D0 · Android 새 경고 0, base7 7장 같음, UI 덤프 72장면 `c_04` 와 같음 · 검증 오류 0, M3b F 블록 · cycle R · D 같음, M3b 입력 흉내는 `in_outside` 만 바뀜(놓은 자리가 숨은 메인 창 위라 메인으로 재도킹), 새 흉내 8종 R · D(메인 떼기 · 재시작 복원 · Window 메뉴 체크 · 떠 있는 창 → 메인 · 메인 → 떠 있는 창 · 마지막 탭 → 창 닫힘 · 창 밖 새 창 · 통째 합치기 · Viewport 거부 둘) rc 0 · 검증 오류 0 · 렌더러 대상 추가 · 제거 짝 맞음, 변이 7개 모두 잡힘. 모니터 확인 남음 |
| H2a 팝업 층 · SMenuAnchor | 구현 · 검증 (2026-11-22, 미커밋) | 문맥마다 팝업 스택 `HPopupLayer`(`src/ui/slate/HPopupLayer.hpp`, `HSlateContext::popups` · `pushPopup` · `dismissPopup` · `dismissAllPopups`): 루트 다음 · 툴팁 앞에 그리고 누름 · 휠 · 이동 · 툴팁 · 히트 경로 · 포커스 경로는 루트보다 먼저, 자리 `hslate::placePopup`(Below · Right, 뒤집기 None · Overflow · Half, 끼우기 · 메뉴 줄 비움, 넣을 때 한 번 정함 = 예전 식 그대로), 바깥 누름 PassThrough(`dismissOutside`, 누름은 흘려보냄 · 앵커 위는 바깥 아님 · 왼쪽 · 오른쪽 따로) · Consume(메뉴 드롭다운: 메뉴 줄 아래 배경의 누름 · 휠을 먹고 누르고 떼면 닫힘 = 예전 캐처), Esc(`closeOnEscape` 가장 위 하나), 하위 팝업 연쇄(같은 부모의 다른 하위 · 부모 닫기), `overPopup`(영역 안 또는 Consume), 닫힌 위젯은 전달이 끝난 뒤 놓음, `onDismissed`, `pushHook`. 새 위젯 `SMenuAnchor`(자식 그대로 배치 · 자기 자리를 앵커로 그 문맥 층에 띄움). 팝업 4종을 층으로: 메뉴 드롭다운(Consume) · 오른쪽 메뉴(Right · 왼쪽 · 오른쪽 바깥 · Esc) · Add Component(Below · Center) · 로그 Categories(`SMenuAnchor`, Below · Half), 루트 오버레이 팝업 호스트 4개와 `resetPopupHost` · `popupContains` · `handleOutsideClick` · 같은 크기 빈 단추 · 전면 캐처를 없앰. 입력 순서 그대로(팝업 바깥 단계 = 층의 `dismissOutside`, overPopup = `menus_.isOpen()` · 층 `overPopup`). 떠 있는 창: M3b 의 막기 3곳(Add Component · Categories · 콘텐츠 오른쪽 메뉴)을 풀어 그 패널이 있는 창 층에 띄움, 그 창 입력 순서에 팝업 바깥 · 오른쪽 메뉴 · Esc · overPopup(스플리터 · 탭 막음), 한 창을 누르거나 한 창에서 팝업이 열리면 다른 창 팝업을 닫음, 패널이 다른 창으로 가면 옛 창 팝업 닫기, 떠 있는 창이 없어질 때 그 창 팝업 닫기, Picker 글 칸은 디테일이 있는 창 키보드 규칙. 창별 덤프에 그 창 팝업이 나옴. 검사 4,188(+72: `slate_input`), 경고 R0 · D0 · Android 새 경고 0, base7 7장 같음, UI 덤프 72장면 `c_04` 와 같음 · 검증 오류 0, M3b · M4 확인 R · D 같음(`in_popup` 은 떠 있는 창에 팝업이 열려 바뀜, Release `in_outside` 는 숨은 메인 창의 OS 자리가 달라 새 창이 됨 — 메인을 M4 자리로 옮기면 같음), 새 팝업 흉내 14종 R · D rc 0 · 검증 오류 0, cycle R · D 검증 오류 0, 변이 8개 모두 잡힘. 모니터 확인 남음 |
| H2b 메뉴 모델 · 명령 레지스트리 · 메뉴 키보드 · 재배치 | 구현 · 검증 (2026-11-22, 미커밋) | 명령 레지스트리 `EditorCommandRegistry`(`src/editor/EditorCommandRegistry.hpp`, EditorApp 소유 · 싱글턴 아님): `EditorCommand{id, label(HText), icon, chords, needsNoTextFocus, dragNotice, execute, canExecute, isChecked}`, 정의 표 `heditorcmd::defaultSpecs()`(등록 순서 = 단축키 우선순위) + `registerCommands()` 가 동작을 붙임, `execute` 는 canExecute 가 거짓이면 무동작(canExecute = 예전 동작이 아무 일도 안 하던 조건과 같게). `tickShortcuts` 는 `resolveShortcut`(키 엣지 · Ctrl · Shift · 대화상자 · 카메라 비행 · 글 칸 · 끄는 중)으로 명령을 고르고 레지스트리로 실행, 예전 if 사다리와 8,192 조합이 같음(시험). Delete · Ctrl+C · Ctrl+V 는 처리 자리 그대로(TextInputController) 같은 명령 id, 툴바 단추도 명령 id, 메뉴 단축키 글은 chord 에서. 키 상수를 `platform/InputKeys.hpp` 로 뗌(IWindow.hpp 가 include). 메뉴 모델 `EditorMenuEntry = Command(id · 그 자리 동작) | Heading | Separator | SubMenu`(`EditorMenuModel.hpp`, `hmenu::resolve` · `step` · `decide`), 위젯 `EditorMenuView`(상단 드롭다운 · 오른쪽 메뉴 공용: 체크 · 아이콘 칸, 단축키 열, 하위 메뉴 화살표, 꺼진 항목 흐림 · 누름 무반응, 하위 메뉴 = 층 push(parent) + Right · Overflow · clamp, hover 로 열고 다른 행 hover 로 닫음, 포인터가 움직일 때만 강조 이동), `EditorMenus::Item` · `EditorContextMenu::Item` 은 이 모델로 합침. 키보드: ↑ ↓(소제목 · 구분선 · 꺼진 항목 건너뜀, 돎) · Enter · →(하위 · 다음 메뉴) · ←(하위 닫기 · 이전 메뉴) · Esc(한 단계), 메뉴의 창이 키보드 창이면 그 프레임 키는 메뉴 몫(단축키 · 글 칸 · QWER 로 안 샘, `tickMenuKeys`), Alt · F10 · 가속 글자는 뺌. 재배치(메뉴 줄 이름 그대로): File(Scene | Save) · Edit(History | Edit | Configuration: Language ▸ · Audio Output) · Actor(Create | Hierarchy) · View(Viewport: Frame Selected · Gizmo Size) · Window(Panels | Layout: Reset Layout), 지운 항목 없음, 대응표 `Handoff/mw/H2b/menu_map.txt`. H2a 판단 반영: 에디터 문맥 팝업 층 `setSingleRoot(true)`(창 안 루트 팝업 하나), 팝업이 덮은 자리 툴팁 막기, 드롭다운 Overflow · clamp. 덤프 `--ui-open-menu=Edit/Language` · `--ui-menu-keys=`, 대본 `<창>:key@이름`. 검사 4,252(+64: `editor_commands` +53 · `slate_input` +11), 경고 R0 · D0 · Android 새 경고 0, base7 7장 같음, UI 덤프 72장면 중 64 `c_04` 와 같음 · 8 메뉴 장면 다름(드롭다운 · 오른쪽 메뉴 내용만, 팝업 앞 줄은 모두 같음) · vp 72 같음 · 검증 오류 0, 새 장면 8 → 새 기준 `h2b`, H2a 흉내 R · D 같음(메뉴 글 장면 · 고정한 in_outside 만 다름), 새 메뉴 흉내 16종 R · D rc 0 · 검증 오류 0, 변이 9개 모두 잡힘. 모니터 확인 남음 |
| H3a 디테일 패널 기반(속성 종류 · 기본값 · 속성 행 · 콤보 · 성분 칸 · 되돌리기) | 구현 · 검증 (2026-11-22, 미커밋) | `PropDesc` 종류 String · Vec3 · Color(RGB/RGBA, `srgb` 표시 규칙) · AssetPath(데이터만) 추가(값 번호는 뒤에 붙여 예전 그대로), 기본값 `hasDefault` · `def*` · `withDefault(...)`(종류별 해석 · 열거값 템플릿), 공용 도우미 `hprop::isDefault · canReset · resetToDefault · valueText · setFromText`(1e-6 상대 오차, UE 복사 글 모양), 팩토리 `makePropString · makePropVec3 · makePropColor · makePropAssetPath`, 매크로 `XPROP_String · XPROP_Vec3Min`. 컴포넌트 21개 파일 기본값 채움(생성자 기본값이 분명한 칸만, 데이터 · 에셋이 정하는 Clip · Content · Transparent 제외, 액터 참조는 늘 없음), 손으로 둔 X · Y · Z / R · G · B 세 칸을 성분 칸 하나로(라이트 · 안개 · 데칼 · 기본 도형 재질 색, 데칼 크기, 파티클 중력, 잔향 · 강체 반 크기, 강체 중심, 조인트 Anchor · Axis · Connected Anchor, 내비 Extent, 단위 · step 은 예전 그대로), AI Brain 태그 글 칸 둘. 씬 직렬화(writeFields · readField)는 그대로. Undo: `PropValue` 에 글 · 성분, `hcmd::commitPropChange`(클릭 편집 한 단계) · `resetPropToDefault`(함께 고른 액터 포함 한 단계) · `setPropComponent`(성분 하나만 다른 액터에). 위젯(ui/slate): `SPropertyRow`(이름 열 \| 값 열 \| 되돌리기 자리, 열 나눔 비율 `HPropertyColumns` 패널 공유 · 끌기 ±4px, hover, 이름 ".." 줄임 + 툴팁) · `SVectorField`(SNumberField 셋/넷 + 색 띠) · `SColorSwatch`, `SNumberField` 에 색 띠 · 칸 자르기 · 세로 가운데 · 끌기 힌트 끄기(선택 사항). 콤보는 editor 쪽: 오른쪽 메뉴와 같은 `EditorContextMenu::openBelow`(→ `EditorMenuView`, 체크 · 지금 값 강조 · ↑↓ Enter Esc · 바깥 · 단추 토글 · 배치 전 단추는 다음 프레임), 메뉴는 연 패널을 열 때 받음(예전 Bottom 고정), 그래서 `menuWindowId` · 메뉴 키 · 떠 있는 창 문맥은 그대로 적용. 행 오른쪽 메뉴 Reset to Default · Copy · Paste = 레지스트리 명령 `Details.*`(canExecute 로 흐림), 되돌리기 화살표 = `rotate-ccw` 아이콘(lucide). 글 칸 = `PropertyTextEdit`(TextField 포커스, Enter 확정 · Esc 취소 · 바깥 취소 = 이름 바꾸기 규칙), `windowOfFocusTarget(TextField)` = 디테일 창. 디테일 다시 만들 때 디테일이 연 메뉴 · 콤보를 닫음, 여러 줄 읽기 정보 칸 높이를 줄 수만큼(예전 겹침). 덤프 `--ui-details-set · -combo · -menu · -edit · -split`, `--ui-save-scene-copy`. 검사 4,352(+100: `editor_commands` +47 · `slate_input` +41 · `text_system` +12, `physics_lifecycle` 는 옛 칸 이름 HalfX · CenterY 를 성분 칸으로 고침), 경고 R0 · D0 · Android 새 경고 0, base7 7장 같음, UI 덤프 기존 80장면 중 46 같음 · 34 디테일 안만 다름 · vp 80 같음 · 검증 오류 0, 새 장면 10 → 새 기준 `h3a`, 흉내 5종 R · D rc 0 · 검증 오류 0(디테일 든 덤프만 다름), LightingProbe 열기 → 저장 바이트 같음, 변이 8개 모두 잡힘. 모니터 확인 남음 |
| H3b 디테일 보강(에셋 경로 칸 · 색 선택기 · 속성 검색) | 구현 · 검증 (2026-11-22, 미커밋) | 에셋 경로 칸 `[종류 아이콘 · 이름 · ▾][찾기][비우기]` + 고르기 팝업 `AssetPicker`(검색 칸 · 그 종류 목록 = 콘텐츠 색인 · 지금 값 체크 · ↑↓ Enter Esc · 바깥 닫기 · 칸 다시 누름 닫기, 디테일이 있는 창 팝업 층), 목록 계산 `AssetPickerModel.hpp`(`assetType` = 종류 이름 쉼표 목록, 이름 낱말 검색), 콘텐츠 브라우저 끌어 놓기(메인 창 디테일만, 종류가 다르면 거부 · 테두리 빨강), 찾기 = 콘텐츠 브라우저 그 폴더 · 고름. 옮긴 칸: StaticMesh `Model`(파일 메시만, `setModelPath` 가 다시 읽음), Audio `File`(`setFile`), NavMeshVolume `Data`(`loadData`), 남긴 칸: SkinnedMesh 모델 · 애니메이션 Clip · UI 패널 Content. 에셋 세터가 쓸 AssetManager 는 `Component::bindLoadContext`(디테일이 보일 때). 색 선택기 `ColorPickerPopup`(판 8×8 · 색상 막대 · 알파 막대 · 이전/새 견본 · RGB(A) · HSV · 16진수 sRGB · 확인/취소), 값 `ColorPickerModel.hpp`(저장 값 그대로 HSV, 16진수만 sRGB, 회색 · 검정에서 색상 기억, HDR 은 V > 1 · 판 범위 max(1, V)), Undo = 세션 `hcmd::PropSession`(확정 한 단계 · 취소 원래 값), 색 변환 `math/Color.hpp`, 그리기 `HPaintContext::drawGradientRect`(정점 색, 잘린 자리 쌍선형). 속성 검색: 디테일 위 검색 칸(TextField), 낱말 모두 · 문화권 이름 또는 영어 원문, TRANSFORM 접기 · 컴포넌트 머리 줄만 · 없으면 알림, 선택이 바뀌어도 유지, Esc 지움, `DetailsSearch.hpp`. H3a 판단 1: 글 칸 바깥 누름 = 확정(`TextInputField::onEscape` 로 Esc 취소와 구별). 검사 4,437(+85: `editor_commands` +54 · `text_system` +17 · `slate_input` +14), 경고 R0 · D0 · Android 새 경고 0, base7 7장 같음, UI 덤프 기존 90장면 모두 다름(디테일 위 검색 칸 33px) — 85 디테일 안만 · 5 디테일 행에 붙은 팝업(Add Component · 행 메뉴)이 33px 아래로, vp 90 같음, 107장면 rc 0 · 검증 오류 0, 새 장면 17 → 새 기준 `h3b`, 흉내 6종 R · D rc 0 · 검증 오류 0(H3a 와 글 같음, 새 `h3b` 흉내 5: 떠 있는 창 색 선택기 · 에셋 고르기 · 검색 · 끌어 놓기), LightingProbe 열기 → 저장 바이트 같음, 변이 11개 모두 잡힘. 모니터 확인 남음 |
| B1 콘솔 명령 | 구현 · 검증 (2026-11-23, 미커밋) | `EditorApp::registerCommands()` 가 월드 서비스 `CVarRegistry` 에 `cmd <id>` · `cmd.list [filter]` 를 등록(에디터 전용, 게임 · Android 미등록). `heditorcmd::runConsoleCmd` · `suggestIds` · `formatCommandList` 순수 함수(`EditorCommandRegistry.hpp`): `cmd` 는 메뉴와 같은 `execute` 호출(`needsNoTextFocus` 등 단축키 조건 미적용), 없는 id 는 `LogConsole` 경고 + 앞부분/포함 최대 5개 추천, `canExecute` 거짓이면 실행 안 함, `cmd.list` 는 id 순(id · 라벨 · 단축키 · 실행 가능 여부) + id/라벨 거르기. |
| B2 시험 · 품질 | 구현 · 검증 (2026-11-23, 미커밋) | `HarukaSceneQualityTest`(`scene_quality` ctest 추가): (1) `ComponentRegistry::get().entries()` 23종 전수 생성 · `buildProps()` 161개 `withDefault` 속성 `hprop::isDefault` 자동 대조 → 불일치 0건(GPU/에셋 제외 0종). (2) `AssetManager` null 렌더러 가드(`src/assets/AssetManager.cpp`) 추가 후 `Projects/Samples/Content/scenes/*.hscene` 14종 전체 2차 왕복(`SceneSerializer::readScene` → `writeScene` ×2) 바이트 일치 확인(14/14 일치, DEF-183 없음). (3) `build/chk/uidump.sh` `details_asset_picker_mesh` 에 `--ui-details-asset-search=test` 추가 → git 추적 모델만 표시(`b2` 대 `h3b`: 106 `[같음] [vp:같음]`, `details_asset_picker_mesh` 1만 `[다름] [vp:같음]`). ctest 46/46 · 검사 4,663(+214), 경고 R0 · D0, base7 7장 같음, 변이 3/3 잡힘. |
| C1 메뉴 아이콘 · Alt | 구현 · 검증 (2026-11-23, 미커밋) | Lucide 1.75.0 아이콘 17종 추가(총 55종)로 상단 메뉴 · 오른쪽 메뉴 · 툴바 전체 적용(언어 · 열거 콤보는 체크 전용, `Window > Panels`는 열림=체크 · 닫힘=패널 아이콘). `STextBlock::underline` · `hmenu::assignBarAccelerators` · `findMnemonicRow` · `stepBar` 순수 모델 분리. 메인 창 키보드일 때 Alt 탭 · Alt+글자(F10 미지원, GLFW `GLFW_WIN32_KEYBOARD_MENU` 기본 차단 확인) 및 영어 밑줄 · 한국어 `(F)` 표시, 드롭다운 글자 키 지원. ctest 46/46 · 검사 4,687(+24), 경고 R0 · D0, base7 7장 같음. |
| C2 Output Log 표시 옵션 | 구현 · 검증 (2026-11-23, 미커밋) | 툴바 오른쪽 끝 Display Options 단추(`EditorContextMenu::openBelow`, 떠 있는 창 팝업 층 지원) · `OutputLog.*` 명령 6종 · `HLog::startWallMs()` · 순수 모델 `OutputLogModel.hpp`(시각 없음/초/지역시각, 증분 Collapse `×N` 배지, Word Wrap 가변 행 높이, `Config/editor_log.txt` v2 왕복 + v1 하위 호환, Clear on Play). 2,048행 일괄 줄 바꿈 6.57 ms(> 1 ms) 확인 후 보이는 범위만 계산. ctest 46/46 · 검사 4,707(+18), 경고 R0 · D0, `menu_icons` 누락 0, base7 7장 같음. |
| C3 콘솔 입력 편의 | 구현 · 검증 (2026-11-23, 미커밋) | `CVarRegistry::complete`(첫 낱말 앞부분/포함 · 인자 단계 한 줄 도움말) · `OutputLogPanel` 후보 목록 팝업(H2a `Side::Above`, 최대 10행, ↑↓/Tab/Enter/Esc/클릭, IME 조합 중 유지) · `ConsoleHistoryModel.hpp`(`Config/console_history.txt` 64줄 유지) · `` ` `` 키(`Console.Focus`, D7/모달/다른 글 칸 예외, `terminal` 아이콘). ctest 46/46 · 검사 4,725(+17), 경고 R0 · D0, `menu_icons` 누락 0, base7 7장 같음. |

## 7. 단계별 판단 확정 (2026-11-22 사용자 승인: 제안대로)

각 단계의 설계 판단은 아래와 같이 정리합니다. 출처는 인계 자료의 단계 보고와 후속 작업 계획을 따릅니다.

| 출처(단계 · 번호) | 내용 | 결정 | 근거 한 줄 |
|---|---|---|---|
| M3b 1 | 떠 있는 창 팝업 막기(당시 임시 제한, H2a에서 창별 팝업 층으로 해제) | 확정 | H2 전까지 안전한 우회로였고 H2a에서 계획대로 해제됨 |
| M3b 2 | D7 Play 중 보조 창 포커스 시 패드 · XR 컨트롤러 입력은 유지 | 확정 | XR은 헤드셋 쪽 입력이므로 보조 창 포커스로 막으면 안 됨 |
| M3b 3 | 글 칸 포커스 중 다른 창으로 OS 포커스 이동 시 칸 포커스 유지 · 입력만 중단 | 확정 | Windows 앱의 창별 포커스 기억과 같아 돌아오면 이어서 입력 가능 |
| M3b 4 | QWER 기즈모 단축키는 메인 창이 키보드 창일 때만 동작 | 확정 | 뷰포트 조작은 메인 창 문맥에 속함 |
| M4 1 | 탭 끄는 중 다른 창을 앞으로 가져오지 않고 놓은 뒤에만 앞으로 가져옴 | 확정 | GLFW가 포커스를 잃은 창의 버튼 뗌을 보내 끌기가 끊기는 것을 방지 |
| M4 3 | 창 밖 탭 끌기 미리보기로 반투명 탭 창 대신 커서 모양 사용 | 확정 | 이동 · 금지 커서로 의사 전달이 충분하며 필요 시 나중에 보강 |
| M4 4 | 떠 있는 창의 유일한 탭을 창 밖으로 끌면 새 창 생성 후 옛 창 닫기 | 확정 | 창 옮기기는 OS 제목 줄, 탭 떼기는 새 창 규칙으로 일관됨 |
| M4 5 | 탭을 다른 창의 메뉴 줄 · 툴바 등 리프 아닌 곳에 놓으면 무동작 | 확정 | 잘못된 영역 놓기로 인한 의도치 않은 도킹 변경 방지 |
| M4 6 | 콘텐츠 브라우저 → 메인 뷰포트 끌어 놓기 대상 창 판정을 가장 앞의 창으로 통일 | 확정 | 탭 끌기와 동일한 Z 순서(앞뒤) 규칙으로 일관성 유지 |
| H2a 2 | 메인 창 누름 시 떠 있는 창 팝업 닫기 | 확정 | 활성 창 전환 시 열린 팝업을 정리하는 자연스러운 동작 |
| H2a 3 | 팝업이 끌기 이름표 · 도크 표시 · 모달보다 위에 그려짐 | 확정 | 실제로 동시에 뜨는 경우가 없고 팝업 계층이 단순함 |
| H2a 5 | `--ui-add-component` · `--ui-content-menu`가 떠 있는 창에도 열림 | 확정 | 창별 팝업 층 검증에 그대로 활용됨 |
| H2a 6 | 아웃라이너 오른쪽 메뉴 없음(원래 없음) | 확정 | 기존 동작을 유지함 |
| H2b 1 | 메뉴가 열린 동안 단축키 막기 | 확정 | UE와 동일하게 메뉴 키보드 조작 중 단축키 오작동 방지 |
| H2b 2 | 메뉴 재배치(Frame Selected · Gizmo Size → View, Reset Layout → Window, Language → Edit > Configuration) | 확정 | 기능 성격에 맞는 메뉴 분류로 정리됨 |
| H2b 3 | Tools · Help 메뉴 없음(넣을 항목 없음) | 확정 | 빈 메뉴를 만들지 않음 |
| H2b 5 | 메뉴 모델의 그 자리 동작(`act`) 허용 | 확정 | 레지스트리 등록 없이 문맥 동작을 간결하게 연결함 |
| H2b 6 | 소제목 · 구분선 · 비활성 항목 누름 시 메뉴 유지 | 확정 | UE 방식과 같으며 오조작으로 메뉴가 닫히지 않음 |
| H3a 2 | 성분 칸에 X/Y/Z 글자 없이 색 띠만 표시 | 확정 | UE5 방식과 같아 좁은 칸에서도 가독성이 좋음 |
| H3a 4 | 회전 기본값 0(회전한 액터는 되돌리기 화살표 표시) | 확정 | UE와 동일한 기본값 기준 |
| H3a 5 | 합친 칸 이름 변경(HalfX → Half Extent 등, 씬 파일 무관) | 확정 | 직렬화 필드명이 아닌 UI 표시 이름만 변경됨 |
| H3a 6 | 디테일 패널을 다시 만들 때 열린 콤보 · 행 메뉴 닫기 | 확정 | 위젯 재생성 시 낡은 팝업 앵커 참조 방지 |
| H3b 2 | HDR 색(안개 등)은 별도 세기 칸 없이 명도 V > 1로 표현 | 확정 | 기존 저장 값 구조를 바꾸지 않고 HDR 색을 편집 가능 |
| H3b 3 | 4성분 색 속성은 실제 컴포넌트에 없음(시험 값으로만 확인) | 확정 | 알파 막대 경로는 단위 시험으로 검증됨 |
| H3b 4 | 에셋 검색은 이름만 검색 | 확정 | UE와 동일하며 빠른 필터링에 충분함 |
| H3b 5 | 에셋 끌어 놓기 수신은 메인 창 디테일만 지원 | 확정 | 필요 시 이후 확장 가능하며 현재 구조로 충분함 |
| H3b 6 | 같은 액터를 다시 만들 때 에셋 고르기 · 색 선택기는 열린 채 유지 | 확정 | 연속 편집 중 팝업이 닫히지 않도록 함 |
| H3b 7 | StaticMesh `Model` 교체 시 `Transparent`를 새 메시 값으로 갱신 | 확정 | UE 방식과 같이 새 에셋 속성을 따름 |
| H2a 1 | 같은 창 안에서도 루트 팝업은 한 번에 하나(UE 방식) | 반영(H2b) | `HPopupLayer::setSingleRoot`, 에디터 문맥에서 켬 |
| H2a 4 | 팝업 밑 위젯의 툴팁이 팝업 위에 뜨지 않게 막기 | 반영(H2b) | 팝업 영역 위에서는 아래 위젯 툴팁을 띄우지 않음 |
| H3a 1 | 글 칸 바깥 누름 = 확정(이름 바꾸기 칸은 그대로) | 반영(H3b) | UE · 숫자 칸과 같은 규칙 |
| H2b 7 | 콘솔 `cmd <id>` · `cmd.list [filter]` 명령 연결 | 반영(B1) | 월드 서비스 `CVarRegistry`를 통해 에디터 명령 실행 · 목록 조회 구현 |
| H3a 확신 낮음 | 손으로 채운 `withDefault` 기본값을 생성자 값과 자동 대조하는 시험 추가 | 반영(B2) | `HarukaSceneQualityTest`로 23종 · 161개 속성 자동 대조(불일치 0) |
| H3b 참고 | 에셋 고르기 덤프 장면을 git 추적 콘텐츠만 보이도록 고정 | 반영(B2) | `details_asset_picker_mesh`에 검색어 `test` 추가해 로컬 폴더 의존 제거 |
| H3a 확신 낮음 | 샘플 씬 14종 저장 2차 왕복(`readScene` → `writeScene` ×2) 확인 | 반영(B2) | 14종 모두 1차 · 2차 바이트 일치 확인(DEF-183 없음) |
| M3a 2 | 메인 창 최소화 중 떠 있는 창 내용 갱신 여부 | 보류(화면 확인 뒤) | 현재는 DEF-106 규칙으로 tick을 건너뜀 |
| M4 2 | 떠 있는 창을 메인 위에 둘지(Win32 소유 창 지정 여부) | 보류(화면 확인 뒤) | 현재는 독립 창이라 메인을 누르면 메인이 위로 올라옴 |
| H2b 아이콘 | 메뉴 아이콘 일관성(Edit 메뉴 Delete에만 아이콘 있음 등) | 확정(2026-11-23 사용자): 전체 적용 | 상단 · 오른쪽 메뉴 · 툴바 전체에 Lucide 아이콘 적용(언어 · 열거 콤보 제외) |
| H2b 4 | Alt · F10 · 밑줄 가속 글자 지원 여부 | 확정(2026-11-23 사용자): Alt 지원 · F10 미지원 | Alt 탭 · Alt+가속 글자 · 드롭다운 글자 키 지원, F10 미지원 |
| H3a 3 | 디테일 이름 열 비율(기본 0.4) 저장 여부 | 보류(화면 확인 뒤) | `editor_state` 저장 필요성을 화면에서 확인 |
| H3b 1 | 기본 도형 재질 색을 끄는 동안 뷰포트에 실시간 반영할지 여부 | 보류(화면 확인 뒤) | 현재는 확인 때만 반영(라이트 · 안개는 실시간) |

### 사용자 화면 확인 뒤 결정할 것

1. **M3a 2 (최소화 중 떠 있는 창 내용 갱신)**: 메인 창을 최소화한 상태에서 떠 있는 창을 볼 때 내용 갱신이 멈춘 것이 불편한지 확인해 결정합니다.
2. **M4 2 (떠 있는 창을 메인 위에 둘지)**: 메인 창을 클릭했을 때 떠 있는 창이 뒤로 가는 동작이 불편해 Win32 소유 창으로 늘 메인 위에 둘지 확인해 결정합니다.
3. **H2b (메뉴 아이콘 일관성)**: Edit 메뉴(Delete에만 아이콘 있음) 등 드롭다운 메뉴의 아이콘 배치를 보고 통일하거나 뺄지 결정합니다.
4. **H2b 4 (Alt · F10 · 밑줄 가속 글자)**: 상단 메뉴 바를 마우스 없이 `Alt`/`F10`과 밑줄 글자로 여는 기능이 필요한지 화면에서 확인해 결정합니다.
5. **H3a 3 (이름 열 비율 저장)**: 디테일 패널의 이름 열 너비(기본 0.4)를 조절한 뒤 재시작할 때 비율을 유지할 필요가 있는지 확인해 결정합니다.
6. **H3b 1 (기본 도형 색 실시간 반영)**: 기본 도형 재질 색을 색 선택기로 끄는 동안 뷰포트에 즉시 반영할지, 지금처럼 확인 시 반영할지 확인해 결정합니다.
