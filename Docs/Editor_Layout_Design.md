# 에디터 레이아웃 설계 (UE5 기반, 2026-08-27)

사용자가 UE5 에디터 레이아웃을 참고해 Haruka 에디터를 다듬기로 했습니다. **전부 차용하지는 않으며**,
구조 골격은 이미 갖춰져 있으므로 부족한 디테일과 상·하단 크롬을 채우는 것이 목표입니다. 이 문서는
레이아웃 분석과 **구조 설계**이며 구현이 아닙니다. 근거는 UE5 스크린샷(Urban360)과 현재
`src/editor/EditorApp.cpp` 실측입니다.

---

## 1. 진단 요약

Haruka 에디터의 **구조 골격은 이미 UE5식 5분할**입니다(툴바 / 아웃라이너 / 뷰포트 / 디테일 / 하단 탭).
스플리터 리사이즈·기즈모 모드·하단 탭·아웃라이너 검색·디테일 프로퍼티 편집이 이미 동작합니다.
따라서 부족한 것은 "레이아웃 구조"가 아니라 **각 영역의 완성도**와, UE5에는 있으나 Haruka에는 없는
**상단 메뉴바·하단 상태바** 두 줄, 그리고 **비어 있는 뷰포트 툴바**입니다. 가장 큰 단일 격차는
아웃라이너가 **평면 리스트**라 이미 존재하는 Actor 부모/자식 계층이 화면에 드러나지 않는 점입니다.

---

## 2. UE5 레이아웃 분해 (위 → 아래)

| # | 영역 | UE5 구성 |
|---|---|---|
| 1 | 메뉴바 | File · Edit · Window · Tools · Build · Platforms · Select · Actor · Help (+ 우측 프로젝트명·창 컨트롤) |
| 2 | 레벨 탭 바 | 열린 레벨 탭("Showcase") + 홈/네비 아이콘 |
| 3 | 메인 툴바 | (좌) Selection Mode 드롭다운·Add(+)·툴 드롭다운 / (중) Play·Stop·Simulate / (우) 설정·상태 칩 |
| 4 | 도킹 영역 | **아웃라이너**(Label·Type 컬럼, 검색, 폴더 트리, 가시성 눈, 개수) \| **뷰포트**(+뷰포트 툴바) \| **디테일**(컴포넌트 트리·카테고리·검색·Add Component) |
| 5 | 하단 도크 | Content Browser(폴더 트리+에셋 그리드+브레드크럼+Add/Import/Save/Fab+검색/필터) · Output Log 탭 |
| 6 | 상태바 | Content Drawer · Cmd 콘솔 · Trace · Diagnostics · Zen Server · Revision Control · 진행률 · 줌% |

**뷰포트 툴바**(뷰포트 위에 떠 있는 오버레이): (좌) 변환 도구(Select/Move/Rotate/Scale)·좌표계(World/Local)·
스냅 토글+값(위치/회전/스케일)·카메라 속도 / (우) 뷰 타입(Perspective/Top/…)·뷰 모드(Lit/Unlit/Wireframe)·
Show 플래그·뷰포트 설정. 좌하단에 좌표축 기즈모.

---

## 3. Haruka 현재 구조 (`buildWidgetTree`, EditorApp.cpp:288)

```
root (SVerticalBox)
├── toolbar  (SBorder, 고정 kToolbarH=50)  → toolbarBox_ [rebuildToolbar]
│     Q/W/E/R 기즈모 · World/Local · Play/Pause/Stop · Save/Load/Refresh · (fill) · VRAM
├── mainArea (SHorizontalBox, Fill)
│   ├── outliner (고정 outlinerW_)   header "WORLD OUTLINER" + 검색 + outlinerList_
│   │     └ 평면 리스트: "+ Add Empty" + [행: 이름(선택/인라인 리네임) · x 삭제]
│   ├── viewportOverlay (SOverlay, Fill)
│   │   ├── viewportWidget_ (투명 — 씬은 뒤에 렌더)
│   │   └── stats 오버레이 (좌상단: FPS/GPU/CPU/LOOP)
│   └── details (고정 detailsW_)      header "DETAILS" + detailsList_
│         └ Transform(접이식) + 컴포넌트 프로퍼티 행 + Add Component
└── bottom (고정 bottomH_)            탭[Content Browser | Output Log | GPU Stats] + bottomContent_
```

이미 있는 것: 5분할, **스플리터 리사이즈**(`ESplitter`, outlinerW_/detailsW_/bottomH_ 드래그 조절),
기즈모 모드 0~3 + World/Local 상태, 하단 3탭, 아웃라이너 검색, 인라인 리네임, 디테일 프로퍼티 편집
(enum 드롭다운/토글/숫자필드 + 조건부 노출), 로그 캡처(cout/cerr), Content 목록(모델/프리팹/오디오),
Play 상태(Editor/Playing/Paused), VRAM 계측.

HSlate 위젯: SVerticalBox·SHorizontalBox·SBorder·STextBlock·SButton·SToggleButton·SNumberField·
SCollapseSection·STabBar·SSearchBox·SImage·SBox·SOverlay.

---

## 4. 갭 분석 (영역별 · 채택/보류)

| 영역 | UE5 디테일 | Haruka 현재 | 판단 |
|---|---|---|---|
| **메뉴바** | File/Edit/Window/… 드롭다운 | **없음** | **채택** — File(New/Save/Load), Edit(Undo/Redo), Actor(Add…), View, Help. 흩어진 액션을 모음 |
| 레벨 탭 바 | 열린 레벨 탭 | 없음(단일 World) | **보류** — 단일 월드 에디터라 가치 낮음 |
| **메인 툴바** | 모드/추가/재생/설정 분리 | 변환+재생+씬+VRAM 혼재 | **재편** — 변환 도구는 뷰포트 툴바로 이관, 메인엔 Play·씬·설정 |
| **뷰포트 툴바** | 변환·좌표계·스냅·카메라·뷰모드 | **없음**(뷰포트 비어 있음) | **채택(고효용)** — 뷰포트 위 오버레이. 현재 키보드 전용인 기즈모/스냅을 UI로 노출 |
| **아웃라이너 계층** | 폴더/부모-자식 트리(들여쓰기·펼침) | **트리(들여쓰기+펼침)** | **완료(Phase 3)** — Actor 부모/자식을 들여쓰기 + 펼침 글리프로 렌더 |
| 아웃라이너 컬럼/개수 | Label \| Type 컬럼, 가시성 눈, "N actors" | Label \| Type + 눈 + 개수 | **완료(Phase 3)** — Type 텍스트 컬럼 + 가시성 토글 + 하단 개수. 타입 아이콘은 계속 보류(에셋 필요) |
| **디테일 완성도** | 프로퍼티 검색·카테고리·기본값 리셋 화살표 | 컴포넌트 섹션 + Transform 접이식 | **부분 채택** — 프로퍼티 검색 + 리셋 화살표. 카테고리는 이미 근접 |
| **상태바** | Cmd 콘솔·진행률·소스컨트롤 | **없음** | **채택** — Cmd 콘솔(콘솔 명령)·상태 텍스트·진행률 |
| Content Browser | 폴더 트리+그리드+브레드크럼 | 목록 | **점진 채택** — 폴더 트리 + 썸네일 그리드 |
| **도킹/탭 드래그** | 패널 도킹·탭 이동·스택·레이아웃 저장 | 스플리터 리사이즈 + 단순 탭 전환만 | **채택(Phase 0 · 기반)** — 도크 트리 + PanelRegistry. **부동(OS) 창은 보류**(별도 스왑체인 필요) |
| 타입 아이콘 | 타입별 아이콘 | 단색 사각형 | **보류** — 아이콘 아틀라스/폰트 선행 필요 |

---

## 5. 목표 구조

```
root (SVerticalBox)
├── menuBar     (SMenuBar, ~28)              ★신규   File · Edit · Actor · View · Help
├── mainToolbar (SBorder, ~44)               재편    Play/Pause/Stop · Save/Load/Refresh · (fill) · VRAM/설정
├── mainArea (SHorizontalBox, Fill)
│   ├── outliner (고정 outlinerW_)           강화    트리(들여쓰기+펼침) + Type 컬럼 + 가시성 눈 + 개수
│   ├── viewportOverlay (SOverlay, Fill)
│   │   ├── viewportWidget_ (씬은 뒤에)
│   │   ├── viewportToolbar (오버레이 상단)  ★신규   변환도구·World/Local·스냅·카메라속도 · (우)뷰모드
│   │   ├── stats 오버레이 (좌상단)          유지
│   │   └── 좌표축 기즈모 (좌하단)           선택
│   └── details (고정 detailsW_)             강화    프로퍼티 검색 + 리셋 화살표
├── bottom (고정 bottomH_)                   유지/강화  탭 + (Content Browser 폴더 트리+그리드)
└── statusBar   (SStatusBar, ~24)            ★신규   Cmd 콘솔 · 상태 텍스트 · 진행률
```

### 신규 HSlate 위젯 (기존 조합 스타일 유지)

- **SMenuBar + SPopupMenu** — 메뉴바 + 드롭다운. 팝업은 최상위 z-order 오버레이 + 외부 클릭 닫힘이
  필요합니다(현 SOverlay 위에 팝업 레이어 추가). *중간 난이도.*
- **SComboBox** — 실제 드롭다운 선택(뷰 모드 Lit/Unlit/Wireframe, Perspective/Ortho, 카메라 속도).
  현재 enum 은 순환 버튼(addEnumRow)뿐이라 신규. *중간.*
- **SViewportToolbar** — 조합 위젯(SHorizontalBox 토글/콤보). viewportOverlay 상단 슬롯에 배치. *낮음.*
- **SStatusBar** — 조합 위젯. Cmd 콘솔은 텍스트 입력(SSearchBox/SNumberField 편집 로직 재사용). *낮음.*
- ~~**아웃라이너 트리**~~ — **Phase 3 에서 완료**. 전용 STreeView 를 만들지 않고 `rebuildOutliner` 확장으로
  해결했습니다(신규 위젯 0개). 상세 규약은 7.9 절에 있습니다.
- 디테일 **리셋 화살표**(프로퍼티별 소형 버튼) · **프로퍼티 검색**(SSearchBox 재사용). *낮음.*

### 통합 지점 — 좌표 단일 진리원 (중요)

`viewportFbRect()` 가 마우스↔월드·기즈모·렌더 뷰포트의 **유일한 좌표 생산자**입니다(known-bugs 규약).
지금 수직 예산은 `winH − kToolbarH − bottomH_`, 수평은 `winW − outlinerW_ − detailsW_` 입니다.
**메뉴바·상태바를 추가하면 수직 크롬이 늘어나므로** `viewportRect`/`viewportFbRect` 의 수직 계산을
`winH − menuBarH − toolbarH − bottomH_ − statusBarH` 로 바꿔야 합니다. 이 한 곳만 고치면 기즈모·픽·
렌더가 함께 따라옵니다. **뷰포트 툴바는 뷰포트 내부 오버레이**(stats 처럼)라 fb rect 를 바꾸지 않습니다
(UE5 도 뷰포트 툴바가 3D 뷰 위에 떠 있음) — 좌표 수학이 단순하게 유지됩니다.

### 툴바 재편

현재 한 줄에 섞인 것을 분리합니다: **변환 도구(Q/W/E/R→Select/Move/Rotate/Scale 라벨·아이콘)+World/Local
+스냅+카메라속도 → 뷰포트 툴바**, **Play/Pause/Stop + Save/Load/Refresh + VRAM/설정 → 메인 툴바**.
Q/W/E/R 라벨은 단축키라 의미가 불투명하므로 뷰포트 툴바에선 명칭(또는 아이콘)으로 노출합니다.

---

## 6. 단계별 채택 (Phase)

- **Phase 0 — 도킹 프레임워크(기반)**: 하드코딩 5분할을 **도크 트리 + PanelRegistry** 로 승격. 상세는 §7.
  이걸 먼저 하는 이유는, 나머지 크롬·패널이 전부 그 위에 얹히고, **좌표 단일 진리원(`viewportFbRect`)을
  고정 치수에서 뷰포트 위젯 실측 geometry 로 승격**시키기 때문입니다. "지금은 도킹만" 범위(부동 창 보류).
- **Phase 1 — 크롬 프레임**: `SMenuBar`+`SPopupMenu`, `SStatusBar` 신설 + root 에 두 줄 추가(도크 트리 바깥의
  고정 크롬). 메뉴에 흩어진 액션(New/Save/Load/Undo/Redo/Add) 집약, 상태바에 Cmd 콘솔.
- **Phase 2 — 뷰포트 툴바**: `SComboBox` 신설 + `SViewportToolbar` 조합. 변환 도구·World/Local·스냅·
  카메라 속도를 메인 툴바에서 이관해 오버레이 상단에 배치. 뷰 모드(Lit/Unlit/Wireframe)는 렌더러 디버그 뷰 후.
- **Phase 3 — 아웃라이너 깊이(최대 격차) · 완료**: Actor 부모/자식 계층을 트리로(들여쓰기+펼침) + Type 컬럼 +
  가시성 눈(`activeSelf` 토글과 연동) + 하단 액터 개수. 확정 규약은 §7.9.
- **Phase 4 — 디테일·콘텐츠 완성도**: 디테일 프로퍼티 검색 + 리셋 화살표. Content Browser 폴더 트리 + 썸네일 그리드.
- **보류(예약)**: 부동(OS) 창, 레벨 탭 바, 타입 아이콘(아이콘 에셋 선행), 소스 컨트롤 연동, 유틸리티 위젯 **저작**(데이터 기반).

**권고 착수 순서**: ~~Phase 0(도킹 기반)~~ → Phase 1(크롬 프레임, 메뉴 바까지 완료 · 상태바 잔여) →
~~Phase 3(아웃라이너 계층)~~ → **Phase 2(뷰포트 툴바)** → Phase 4. Phase 0 이 패널 호스팅과 좌표 진리원을
재정의하므로 반드시 먼저였습니다. 남은 착수 지점은 Phase 2 입니다.

---

## 7. 도킹 프레임워크 (Phase 0 · 기반)

### 7.1 범위와 개념

- **지금 범위 = 메인 창 내 도킹만**: 도크 트리(분할 + 탭 스택), 탭 드래그 재도킹, 탭화(stack), 레이아웃 저장/복원.
- **보류**: 부동(floating) OS 창(별도 창 + 스왑체인 + 입력 라우팅이 필요해 무겁습니다). 도크 트리 안의 탭 이동·
  재도킹까지만 합니다.
- **유틸리티 위젯 = 확장성 seam**: 지금 커스텀 툴을 만들지 않습니다. `PanelRegistry` 를 두어 **그 위에 위젯을
  설계할 수 있는 기반**만 놓습니다. 기존 패널도 신규 위젯도 "등록 → 스폰 → 도킹"이 동일합니다(UE5 의 Editor
  Utility Widget 이 네이티브 탭과 같은 탭 시스템에 얹히는 것과 같은 개념. Haruka 엔 UMG 저작이 없으니 위젯은
  C++ HSlate 를 이름으로 등록하는 형태이고, 데이터 기반 저작은 나중 단계).

### 7.2 도크 트리 모델

```
DockNode (variant)
├── SplitNode   { orientation: H|V, children: DockNode[], ratios: float[] }   # 내부
└── TabStackNode{ panelIds: string[], activeIndex: int }                       # 리프
Panel(레지스트리 등록 단위) = { id, title, factory() -> shared_ptr<HWidget>, closable }
```

- 루트는 하나의 `DockNode`. 현 `mainArea`(outliner|viewport|details) + `bottom` 전체가 이 트리로 표현됩니다.
- 분할 비율 드래그(현 `ESplitter` 로직)는 `SplitNode.ratios` 조정으로 **일반화**됩니다.
- 탭 스택은 탭 바(현 `STabBar` 를 발전) + 활성 패널 콘텐츠를 렌더합니다.

### 7.3 PanelRegistry (확장성 seam)

- `registerPanel(id, title, factory)`. 초기 등록: `Outliner`·`Details`·`Viewport`·`ContentBrowser`·`OutputLog`·`GPUStats`.
- **Window 메뉴**(Phase 1)가 등록된 패널을 나열해 스폰/토글합니다. 이미 열린 패널은 포커스, 닫힌 건 기본 위치에 스폰.
- 신규 위젯은 `registerPanel` 한 줄이면 도킹 대상이 됩니다 — 이것이 사용자가 말한 "위에 위젯을 디자인"하는 확장 지점.

### 7.4 뷰포트 통합 (핵심 · 좌표 진리원 승격)

도킹으로 패널 크기가 동적이 되므로, `viewportFbRect()` 를 **고정 패널 폭(outlinerW_ 등)이 아니라 뷰포트
패널 위젯의 실측 arranged geometry** 에서 유도해야 합니다. 방법: 뷰포트 위젯(`viewportWidget_`)이 매 프레임
배치된 `HGeometry`(position/size)를 캐시하고(`SNumberField::cachedGeo_` 와 동일 패턴), `viewportFbRect` 가
그 캐시를 프레임버퍼 픽셀로 환산해 씁니다. 결과: **좌표 단일 진리원이 "고정 치수"에서 "실측 위젯 rect"로
승격**되어, 뷰포트를 좌/우/중앙 어디에 도킹하거나 리사이즈해도 기즈모·픽·렌더가 자동으로 정확합니다.
(known-bugs 의 "좌표는 `WorldViewport`/`viewportFbRect` 하나로만" 규약을 지키면서 소스만 실측으로 바꾸는 것.)

### 7.5 드래그 재도킹

- 탭 바에서 탭 드래그 감지 → 드래그 중 **도크 타깃 오버레이** 표시: 대상 탭 스택의 좌/우/상/하/중앙 5존
  (+ 도크 영역 전체 가장자리). 중앙=탭화(stack), 좌우상하=그 방향으로 `SplitNode` 삽입.
- 드롭 → 도크 트리 변형(스택에 추가 또는 분할 삽입) 후 **빈 탭 스택 정리**(자식 0 이면 부모 분할에서 제거,
  분할 자식 1 이면 접기).

### 7.6 레이아웃 직렬화

- 도크 트리(분할 방향·비율, 탭 스택의 패널 id 목록·활성 인덱스)를 텍스트로 저장/복원(예: `Config/editor_layout.txt`).
  파일 없거나 파싱 실패 시 **기본 레이아웃**으로 폴백. 알 수 없는 패널 id 는 건너뜁니다(전방 호환).

### 7.7 신규 HSlate 위젯

- **SDockArea** — 도크 트리를 재귀 렌더하는 루트 컨테이너.
- **SDockTabStack** — 탭 바(드래그 소스) + 활성 패널 콘텐츠. 현 `STabBar` 를 흡수·발전.
- **SDockTarget** — 드롭 존 5분할 오버레이(드래그 중에만).
- **SSplitter** — 비율 분할 + 핸들 드래그. 현 `ESplitter`(Outliner/Details/Bottom) 로직을 일반화해 흡수.

### 7.8 기존 코드 이관

- `buildWidgetTree` 의 하드코딩 `mainArea`/`bottom` → `SDockArea`(기본 레이아웃 = 현재 배치와 동일)로 교체.
- `EditorApp` 의 `splitterActive_`/`outlinerW_`/`detailsW_`/`bottomH_` → `SplitNode.ratios` 로 이관.
- `rebuildOutliner`/`rebuildDetails`/… 는 그대로. 단지 **패널 팩토리 안에서** 그 콘텐츠를 반환하도록 감쌉니다.
- 리스크: 좌표 진리원 승격(§7.4)이 가장 민감. 이관 후 픽/기즈모 정확도를 실기로 반드시 확인.

---

## 7.9 아웃라이너 계층 트리 (Phase 3 · 확정 규약)

전용 `STreeView` 를 만들지 않고 `rebuildOutliner` 확장으로 구현했습니다(신규 위젯 0개). 아래는
구현 과정에서 코드로 확인해 확정한 규약이며, 이후 이 영역을 고칠 때 그대로 지켜야 합니다.

**표시 순서와 범위**
- 루트는 `getParent() == nullptr` 인 액터이며, **모든 레벨에서 `Actor::id` 오름차순**으로 정렬합니다.
  `getActors()` 는 스폰 순서고 `getChildren()` 은 `attachTo` 호출 순서라, 씬을 다시 읽으면 순서가
  달라져 표시 순서의 진리원이 될 수 없습니다.
- **에디터 카메라는 아웃라이너와 개수에서 완전히 제외**합니다(직렬화 집합과 같은 기준). 노출하면
  사용자가 눈으로 끌 수 있고, 그 순간 `getActiveCamera()` 가 비어 뷰포트·기즈모·픽이 한꺼번에 죽습니다.

**펼침 상태**
- **접힌 `Actor::id` 집합**으로 보관합니다(기본값 = 펼침). 상태가 비거나 유실돼도 아무것도 숨지 않는
  방향으로 실패하고, 새로 스폰하거나 부모를 배선한 액터가 즉시 보입니다.
- 키는 반드시 id 입니다. `Actor*` 는 해제 후 주소가 재사용되면 옛 포인터가 새 액터와 우연히 일치합니다.
- 세션 한정이며 디스크에 저장하지 않습니다. 씬 로드는 전량 재스폰이라 `deserializeWorld` 에서 비웁니다.
- 검색 중에는 접힘을 **무시**(강제 펼침)하고, 화살표 클릭은 펼침이 아니라 행 선택으로 동작합니다.
  그러지 않으면 매치된 자손이 접힌 조상 아래 숨어 검색이 사실상 회귀합니다.

**스크롤 · 클리핑**
- HSlate 에는 스크롤 컨테이너도 클리핑도 없습니다. 그래서 **보이는 창에 드는 행만 슬롯으로 만드는**
  가상 스크롤을 씁니다. 창 밖 행은 위젯으로 존재하지 않으므로 이웃 패널 위로 새지 않습니다.
- 행 숨김에 `EVisibility::Collapsed` 를 쓰면 안 됩니다. `SVerticalBox` 배치가 visibility 를 보지 않아
  빈 줄이 그대로 남습니다.
- 휠은 **`tick` 이 프로세스 전체에서 한 번만** 읽어(`getScrollDelta` 는 consume-on-read) 커서 위치에 따라
  아웃라이너와 카메라 돌리 중 한쪽으로 라우팅합니다. 두 곳에서 읽으면 한쪽이 항상 0 을 받습니다.
- **좌버튼을 누르고 있는 동안에는 스크롤하지 않습니다.** 포인터 캡처 메커니즘
  (`HReply::captureMouse` → `HSlateRenderer::capturedWidget_`)은 존재하지만 `SNumberField` 만 쓰고,
  버튼류는 캡처를 요청하지 않습니다. `SButton`·`SToggleButton` 모두 "내가 눌린 위젯인가"를 보지 않으므로,
  프레스와 릴리즈 사이에 행이 밀리면 마우스-업이 **그 자리에 새로 온 다른 액터의 버튼**으로
  배달됩니다. 누른 적 없는 액터가 삭제되고, 삭제에는 undo 가 없습니다.
- 휠 라우팅은 `outlinerVisible_`(= 아웃라이너가 자신의 리프에서 활성 탭인가)로 게이팅합니다. 비활성
  탭이면 위젯이 배치되지 않아 `cachedGeometry` 가 stale 하고, 그 낡은 rect 로 휠을 삼키면 그 자리를
  차지한 다른 패널 위에서 카메라 돌리가 죽습니다. 뷰포트의 `viewportVisible_` 과 같은 이유입니다.
- 실측 지오메트리가 **아예 없을 때만**(위젯 트리를 막 만든 프레임) 시드 높이로 폴백합니다. 실측이
  작다고 큰 시드를 쓰면 짧은 패널에 행을 과하게 만들어 그대로 패널 밖으로 넘칩니다.
- 검색어가 바뀌면 스크롤을 맨 위로 되돌립니다. 유지하면 매치된 앞부분이 창 밖에 남아 "검색했는데
  아무것도 안 나온" 것처럼 보입니다. 스크롤바가 없으므로 창이 넘칠 때 푸터의 `(a-b)` 범위 표기가
  현재 위치를 알려 주는 유일한 단서입니다.
- 진짜 `SScrollBox` 와 클리핑(`HDrawElement` 클립 필드 + `vkCmdSetScissor` 배칭)은 여전히 미구현입니다.

**행 구성과 히트테스트**
- 한 행은 겹치지 않는 형제 슬롯입니다: `[들여쓰기 spacer][화살표][이름(Fill)][Type][눈][삭제]`.
  `SButton` 은 좌클릭을 자식에게 넘기지 않고 항상 Handled 로 끊으므로, 작은 버튼은 이름 버튼의
  **자식이 아니라 형제**여야 합니다.
- 작은 토글(화살표·Type·눈)은 `SToggleButton` 을 씁니다. 마우스-업에서 위치를 재검사하므로 눌렀다가
  다른 칸에서 떼면 발화하지 않습니다(`SButton` 은 재검사가 없어 그대로 발화합니다).
- **폭 사다리 필수**: 고정폭 합이 패널 폭을 넘으면 이름 Fill 슬롯이 0 이 되어, 그리기는 넘쳐 보여도
  히트테스트가 죽어 행 선택 자체가 불가능해집니다. 들여쓰기 클램프 → Type 컬럼 강등 → 들여쓰기 제거
  순으로 폭을 반납합니다.
- **글리프는 ASCII 만** 씁니다(폰트 아틀라스가 32-127 전용). 유니코드 삼각형·눈은 컴파일도 실행도
  되지만 화면에만 .notdef 박스가 찍혀 리뷰에서 놓치기 쉽습니다. 화살표는 `v`/`>`, 눈은
  `O`(보임)/`-`(자신을 껐음)/`o`(조상이 꺼짐)입니다.

**갱신 경로**
- 위젯 콜백은 **절대 `rebuildOutliner()` 를 직접 부르지 않습니다.** 슬롯 순회 도중 `clearSlots` 가 돌면
  지금 실행 중인 버튼과 그 `std::function` 자체가 파괴됩니다. 콜백은 `outlinerDirty_` 만 세우고,
  `tick` 과 `handleInput` 종료 시점(RAII 가드)에 한 번 리빌드합니다.
- 행 콜백은 `Actor*` 가 아니라 **`uint64_t id` 를 캡처하고 `World::findById` 로 되찾습니다.** 부모 삭제가
  서브트리를 재귀 파괴하므로, 계층 도입으로 해제된 포인터를 만질 구간이 넓어졌습니다.
- 패널 크기가 바뀌면 표시 행 수와 말줄임 폭이 달라지므로, 리스트 실측 지오메트리를 4px 로 양자화한
  시그니처를 비교해 리빌드합니다(드래그 중 매 프레임 리빌드 방지).

**Type 컬럼**
- `Component::typeName()` 을 고정 우선순위표로 하나 고릅니다(추가 순서 비의존). 기여 컴포넌트가
  둘 이상이면 `Mesh+` 처럼 `+` 를 붙입니다. 색은 각 컴포넌트가 이미 선언한 `editorIcon` 색을
  재사용해, 아이콘 에셋 없이 타입을 시각 구분합니다. **타입 아이콘 자체는 계속 보류**입니다.

**가시성**
- 눈은 `Actor::setActive` 를 토글하고, 표시는 `activeInHierarchy()` 를 따릅니다. 눈이 거짓말하지 않도록
  `buildEditorIcons`·`pickActor`·**기즈모(표시와 드래그 모두)** 에 활성 게이트를 넣었습니다(렌더는 이미
  게이트되어 있었음). 규칙은 하나입니다: **뷰포트에 보이지 않는 것은 뷰포트에서 만질 수 없습니다.**
  아웃라이너 선택과 디테일 편집은 그대로 됩니다.
- 알려진 한계: `setActive(false)` 가 물리 바디 해제와 재생 중인 사운드 정지까지 하지는 않습니다
  (각 시스템이 주석으로 명시). Play 중 오디오 액터를 끄면 소리가 계속 날 수 있습니다.

**재부모화**
- 드래그&드롭 재부모화는 계속 보류입니다. 지금 계층을 만드는 수단은 씬 역직렬화와 Actor 메뉴의
  `Attach To Prev Selected` / `Detach Selected` 뿐이며, 후자는 **실기 검증 수단**입니다.
- `attachTo` 는 사이클을 `std::cerr` 로만 알리고 조용히 무시하며 반환값이 없습니다. 호출 전에 조상
  체인을 직접 확인해야 UI 가 성공한 줄 착각하지 않습니다.
