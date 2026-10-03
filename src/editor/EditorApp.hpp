#pragma once
#include "../ui/slate/HSlate.hpp"
#include <chrono>
#include "../core/Transform.hpp"
#include "EditorContext.hpp"
#include "../core/HLog.hpp"
#include "EditorGizmo.hpp"
#include "HCommandStack.hpp"
#include "HotReloader.hpp"
#include "dock/DockTree.hpp"
#include "dock/SDockOverlay.hpp"
#include "../core/HDelegate.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <algorithm>
#include <deque>
#include <memory>
#include <set>
#include <vector>
#include <string>
#include <unordered_set>

class Renderer;
class World;
class AssetManager;
class IWindow;
class Actor;
class CameraComponent;

class EditorApp {
public:
    // Base panel dimensions at 1080p reference (scaled at runtime by uiScale_)
    static constexpr float kOutlinerW = 290.f;
    static constexpr float kDetailsW  = 330.f;
    static constexpr float kToolbarH  =  50.f;
    static constexpr float kBottomH   = 260.f;

    enum class EPlayState { Editor, Playing, Paused };
    EPlayState getPlayState() const { return playState_; }
    bool isPlaying()  const { return playState_ == EPlayState::Playing; }
    bool isPaused()   const { return playState_ == EPlayState::Paused;  }
    // 텍스트 입력(리네임·검색·숫자 칸)이 키보드를 쥐고 있는지. 게임 입력계가 이 동안 키를
    // 무시하는 데 씁니다(DEF-67).
    bool isTextInputActive() const { return currentFocus() != EFocusTarget::None; }
    bool isInEditor() const { return playState_ == EPlayState::Editor;  }

    void beginPlay();
    void pausePlay();
    void stopPlay();

    void init(Renderer& renderer, World& world, AssetManager& assets, IWindow& window);
    void cleanup();
    void tick(float dt);
    void handleInput(float mx, float my, bool ldown, bool lup,
                     float scroll, float winW, float winH);

    // Returns true when (mx, my) falls inside the 3D viewport
    bool inViewport(float mx, float my, float winW, float winH) const;

private:
    // UI scale factor: 1.0 at 1080p, proportional to framebuffer height
    float uiScale_ = 1.f;
    float s(float x) const { return x * uiScale_; }

    EditorContext  ctx_;
    HSlateRenderer slateRenderer_;
    HCommandStack  cmdStack_;
    HotReloader    hotReloader_;

    // onActorDestroyed 구독 핸들. init 에서 붙이고 cleanup 에서 뗍니다. 이 구독이
    // 파괴되는 액터를 참조하는 커맨드 제거와 선택 포인터 무효화를 한곳에서 담당합니다.
    HEvent<Actor*>::Handle actorDestroyedSub_ = 0;

    // ── Editor camera ──────────────────────────────────────────────────────────
    Actor*           editorCamActor_ = nullptr;
    CameraComponent* editorCam_      = nullptr;
    bool             lmbOrbit_       = false;

    // 휠은 tick 이 한 번만 읽어(getScrollDelta 는 consume-on-read) 아웃라이너와 카메라
    // 중 한쪽으로 라우팅합니다. 여기서는 인자로 받은 값만 씁니다.
    void tickEditorCamera(float dt, float scroll);

    // 로그: 코어 링 버퍼에서 마지막으로 본 seq. 달라진 프레임에만 Output Log·상태바를 갱신합니다.
    uint64_t        logSeenSeq_ = 0;

    // ── Output Log (로그 카테고리 L3) ─────────────────────────────────────────
    // 설계: Docs/Log_Category_Design.md §7. 여기 있는 것은 **표시 필터**뿐입니다. 기록 수준은
    // 코어(HLogCategoryState::level)에 있고, Categories 팝업의 Capture 열이 그것을 직접 바꿉니다.
    //
    // 로그 행 위젯은 패널 실측 높이에 맞춘 개수만 만들고, 텍스트와 색을 Attr 로 logViewRows_
    // 에서 읽습니다. 새 로그가 와도 위젯을 다시 만들지 않습니다(DEF-70·DEF-61). 리빌드는
    // 행 수가 바뀔 때(패널 높이 변화)와 탭을 다시 열 때만 일어납니다.
    static constexpr float kLogRowH  = 16.f;
    static constexpr float kLogTimeW = 52.f;
    static constexpr float kLogCatW  = 84.f;
    struct LogViewRow { std::string time, category, text; glm::vec4 color{ 1.f }; };
    std::deque<HLogRecord>        logMirror_;           // 코어 링의 사본(최대 HLog::kCapacity)
    uint64_t                      logMirrorSeq_  = 0;   // 사본에 들어온 마지막 seq
    std::vector<std::string>      logCatNames_;         // 카테고리 인덱스 → 이름
    std::vector<int>              logCatCounts_;        // 사본 안의 카테고리별 레코드 수
    std::vector<char>             logCatHidden_;        // 표시 필터: 1 = 숨김(인덱스 기준)
    bool                          logShowVerb_[5] = { false, true, true, true, true };  // ELogVerbosity 순
    std::string                   logSearch_;
    bool                          logSearchFocused_ = false;
    bool                          logSearchSelAll_  = false;
    uint64_t                      logClearedSeq_    = 0;   // Clear 는 코어 링을 비우지 않고 이 값만 올립니다
    std::string                   logErrText_ = "E 0", logWarnText_ = "W 0";   // 누계 표시 캐시
    bool                          logHasErr_  = false, logHasWarn_ = false;
    std::vector<LogViewRow>       logViewRows_;
    bool                          logViewDirty_ = true;
    int                           logRowCap_    = 0;
    glm::ivec2                    logRowsSig_ {0, 0};   // 행 영역 실측 크기(4px 양자화)
    std::shared_ptr<SVerticalBox> logRowsBox_;
    std::shared_ptr<SSearchBox>   logSearchBox_;
    std::shared_ptr<SButton>      logCatButton_;
    // Categories 팝업. 메뉴 팝업과 달리 전면 캐처를 쓰지 않습니다. 바깥 클릭은 handleInput 이
    // 팝업을 닫기만 하고 삼키지 않으므로, 팝업이 열린 채로 다른 곳을 한 번에 누를 수 있습니다.
    std::shared_ptr<SBorder>      logPopupHost_;        // 최상위 오버레이 슬롯
    std::shared_ptr<HWidget>      logPopupPanel_;       // 바깥 클릭 판정용
    bool                          logPopupOpen_  = false;
    bool                          logPopupDirty_ = false;
    void pullLogRecords();          // 코어 링의 새 레코드를 사본으로 옮김
    void syncLogCategories();       // 새로 등록된 카테고리를 이름·개수·필터 배열에 반영
    void rebuildLogRows();          // 행 위젯 N개를 다시 만듦(높이 변화·탭 진입 시에만)
    void refreshLogView();          // 필터를 적용해 logViewRows_ 문자열을 갱신(위젯 생성 없음)
    void rebuildLogPopup();
    bool logRecordVisible(const HLogRecord& r, const std::string& needleLower) const;

    // ── Output Log 설정 보존·스크롤 (로그 카테고리 L4) ────────────────────────
    // 설정은 바뀌는 순간 저장합니다. 표시 필터는 Config/editor_log.txt, 기록 수준은
    // HLog::kLevelOverridesPath 입니다. 숨긴 카테고리는 인덱스가 아니라 이름으로 기억합니다.
    // 인덱스는 등록 순서라 실행마다 달라지고, 이번 실행에서 아직 등록되지 않은 카테고리의
    // 설정도 파일에서 지우면 안 되기 때문입니다(설계 문서 §9).
    std::set<std::string> logHiddenNames_;
    void loadLogFilter();
    void saveLogFilter() const;
    void saveLogLevels() const;
    void setLogCategoryHidden(uint16_t index, bool hidden);   // 인덱스 배열과 이름 집합을 함께 갱신
    // 스크롤 위치는 "화면 맨 아래 행의 seq" 로 기억합니다(0 = 꼬리 추종). 줄 수로 기억하면
    // 고정 중에 새 로그가 들어오거나 필터가 바뀔 때마다 보던 자리가 밀립니다.
    uint64_t    logAnchorSeq_ = 0;
    uint64_t    logPinSeq_    = 0;   // 고정을 시작한 순간의 최신 seq. 이후 들어온 줄이 "new lines"
    int         logScrollReq_ = 0;   // 다음 갱신에 적용할 휠 이동(보이는 행 수, 양수 = 위)
    std::string logJumpText_;        // 꼬리로 돌아가는 버튼의 라벨 캐시(고정 중에만 표시)
    // 하단 패널이 자기 리프에서 활성 탭인지. outlinerVisible_ 과 같은 이유로 휠 라우팅에 필요합니다.
    bool        bottomVisible_ = true;

    // Dynamic panel refs
    std::shared_ptr<SHorizontalBox> toolbarBox_;
    std::shared_ptr<SVerticalBox>   outlinerList_;
    std::shared_ptr<SVerticalBox>   detailsList_;
    std::shared_ptr<SVerticalBox>   logList_;
    std::shared_ptr<SVerticalBox>   contentList_;
    std::shared_ptr<SBorder>        viewportWidget_;
    std::shared_ptr<SVerticalBox>   bottomContent_;

    // ── Panel sizes (viewportFbRect 프레임0 fallback + 기본 비율 시드로만 사용) ──
    // 실제 레이아웃은 도크 트리 비율(ratios)이 구동합니다. 이 값들은 첫 배치 전 프레임의
    // 뷰포트 rect 추정과 기본 도크 비율의 시드로만 쓰입니다.
    float outlinerW_ = kOutlinerW;
    float detailsW_  = kDetailsW;
    float bottomH_   = kBottomH;

    // ── 도킹 (Phase 0) ───────────────────────────────────────────────────────
    PanelRegistry             panelReg_;              // id → {제목, 위젯}
    std::unique_ptr<DockNode> dockRoot_;              // 도크 트리(위젯과 독립, 안정적)
    std::shared_ptr<SBorder>  dockHost_;              // 도크 영역 컨테이너(content 교체)
    // 뷰포트 패널이 현재 자신의 리프에서 활성 탭(=화면에 배치)인지. rebuildDockArea 에서 갱신.
    // 비활성(다른 패널과 탭 스택으로 묶여 가려짐)이면 viewportWidget_ 이 arrange 되지 않아
    // cachedGeometry 가 stale 해지므로, inViewport 게이팅을 꺼서 픽·오빗·기즈모 오라우팅을 막습니다.
    bool                      viewportVisible_ = true;

    // 스플리터 히트존: rebuildDockArea 마다 재생성. 드래그 자체는 노드 기준이라 안정적.
    struct DockSplitterHit {
        std::shared_ptr<HWidget> handle;     // 얇은 핸들(지오메트리 히트테스트)
        std::shared_ptr<HWidget> container;  // 분할 박스(축 픽셀 크기)
        DockNode* node     = nullptr;        // 이 스플리터가 조정하는 분할 노드
        int       index    = 0;              // node->children[index] 와 [index+1] 사이
        bool      vertical = false;          // true = 상하 분할(핸들 수평, Y 드래그)
    };
    std::vector<DockSplitterHit> dockSplitters_;
    // 드래그 상태(위젯이 아니라 노드 기준 — 리빌드해도 안정)
    DockNode* dockDragNode_        = nullptr;
    int       dockDragIndex_       = 0;
    bool      dockDragVertical_    = false;
    float     dockDragStartMouse_  = 0.f;
    float     dockDragStartRatioL_ = 0.f;
    float     dockDragAxisPx_      = 1.f;

    // ── 탭 드래그 재도킹 (Phase 0 · 2b) ──────────────────────────────────────
    enum class EDockZone { None, Center, Left, Right, Top, Bottom };
    // 탭 히트: 리프의 각 탭. 클릭=활성 전환, 드래그=재도킹.
    struct DockTabHit {
        std::shared_ptr<HWidget> widget;
        DockNode*   stack   = nullptr;
        int         tabIndex = 0;
        std::string panelId;
    };
    // 리프 히트: 드롭 타깃 판정용(리프 전체 rect).
    struct DockLeafHit {
        std::shared_ptr<HWidget> widget;
        DockNode* leaf = nullptr;
    };
    std::vector<DockTabHit>  dockTabHits_;
    std::vector<DockLeafHit> dockLeafHits_;

    bool        tabPressPending_ = false;   // 탭 프레스했으나 아직 드래그 아님
    bool        tabDragActive_   = false;   // threshold 넘어 실제 드래그
    DockNode*   tabSrcStack_     = nullptr;
    int         tabSrcIndex_     = 0;
    std::string tabSrcPanel_;
    glm::vec2   tabPressPos_ {};
    DockNode*   tabTargetLeaf_   = nullptr;
    EDockZone   tabTargetZone_   = EDockZone::None;

    std::shared_ptr<SDockOverlay> dockOverlay_;   // 드롭 존 하이라이트(최상위)

    // ── 상단 메뉴 바 (Phase 1) ────────────────────────────────────────────────
    static constexpr float kMenuBarH   = 26.f;
    static constexpr float kStatusBarH = 24.f;
    struct MenuItem { std::string label; std::function<void()> action; };
    struct MenuDef  { std::string name;  std::vector<MenuItem> items; float x = 0.f; };
    std::vector<MenuDef>            menus_;
    int                             openMenu_ = -1;      // 열린 메뉴 인덱스(-1=없음)
    std::shared_ptr<SHorizontalBox> menuBarBox_;
    std::shared_ptr<SBorder>        menuPopupHost_;      // 드롭다운 컨테이너(최상위 오버레이)
    // 메뉴 바 버튼 위젯(menus_ 와 1:1). 메뉴가 열린 상태에서 다른 버튼 위로 hover 하면 그 메뉴로
    // 전환하기 위해(표준 메뉴 바 동작) handleInput 이 이 위젯들의 geometry 를 히트테스트합니다.
    std::vector<std::shared_ptr<HWidget>> menuButtons_;
    void buildMenus();
    void rebuildMenuBar();
    void rebuildMenuPopup();
    // 크롬(메뉴 바·팝업·툴바·디테일)의 지연 리빌드 요청입니다. 위젯 콜백은 자기를 담은
    // 컨테이너를 직접 비울 수 없습니다 — 슬롯을 순회하는 도중 clearSlots 가 돌면 지금
    // 실행 중인 버튼과 그 std::function 자체가 파괴됩니다. 아웃라이너의 outlinerDirty_
    // 와 같은 방식으로, 콜백은 플래그만 세우고 프레임 끝에서 한 번 리빌드합니다.
    bool menuBarDirty_   = false;
    bool menuPopupDirty_ = false;
    bool toolbarDirty_   = false;
    bool detailsDirty_   = false;
    void flushChromeRebuilds();
    // 디테일 패널이 지금 보여 주는 액터입니다. 패널의 값 접근자는 이 액터와 그 컴포넌트를
    // raw 포인터로 붙잡으므로, 이 액터가 파괴되면 onActorDestroyed 구독이 리빌드를 요청하고
    // handleInput 이 이벤트를 보내기 전에 소진합니다(DEF-92). 선택 포인터와 따로 두는
    // 이유는, 여러 삭제 경로가 파괴 전에 ctx_.selected 부터 비우기 때문입니다.
    Actor* detailsActor_ = nullptr;
    // 디테일 TRANSFORM 섹션의 펼침 상태. 선택이 바뀌어도 유지합니다(DEF-90).
    bool   detailsTxExpanded_ = true;

    // ── 하단 상태바 (Phase 1) ────────────────────────────────────────────────
    // 슬롯은 buildWidgetTree 에서 **한 번만** 채우고 이후 절대 다시 만들지 않습니다.
    // 표시는 Attr 로 읽고, 폭에 따른 축약도 슬롯 재구성이 아니라 문자열 길이로만
    // 처리합니다(빈 문자열이면 Auto 슬롯이 스스로 폭 0 이 됩니다). 그래서 이 패널에는
    // "콜백 중 자기 컨테이너 파괴" 가 구조적으로 불가능합니다.
    std::shared_ptr<SHorizontalBox> statusBarBox_;
    std::string                     statusMsg_ = "Ready";
    glm::vec4                       statusMsgColor_ { 0.42f, 0.42f, 0.45f, 1.f };
    int                             statusNarrow_  = 0;   // 0=전체, 1=축약, 2=숨김
    glm::ivec2                      statusSizeSig_ {0, 0};
    void        updateStatusMessage();      // 로그 한 줄을 잘라 캐시(프레임마다 호출 금지)
    static glm::vec4   logColor(ELogVerbosity v);
    static std::string formatLogLine(const HLogRecord& rec);   // 접두어 + ASCII 치환
    static std::string toDisplayAscii(std::string s);          // 폰트가 그릴 수 없는 바이트를 '?' 로
    std::string statusFocusText() const;    // 짧은 리터럴만 반환

    // 트리 변형: 드롭 시 소스에서 빼고 타깃에 붙임(탭화 또는 분할). 빈 리프 정리.
    void performRedock(DockNode* src, int srcIdx, const std::string& panelId,
                       DockNode* target, EDockZone zone);
    void collapseEmptyLeaf(DockNode* leaf);

    // Gizmo mode & space
    int  gizmoMode_  = 1;   // 0=select, 1=move, 2=rotate, 3=scale
    bool worldSpace_ = true;

    // ── 뷰포트 툴바 (Phase 2) ────────────────────────────────────────────────
    // 뷰포트 오버레이 상단에 떠 있는 툴바입니다. 변환 도구·좌표계는 메인 툴바에서
    // 여기로 이관했고, 카메라 속도가 새로 붙습니다. 툴바가 차지한 rect 는 inViewport
    // 에서 배제해, 그 위 클릭이 픽·오빗·기즈모·RMB 카메라 모드로 새지 않게 합니다.
    static constexpr float kVpToolbarInset = 6.f;   // 뷰포트 가장자리 인셋(미스케일)
    float camSpeed_          = 5.f;    // 현행 하드코딩과 같은 기본값(m/s)
    int   vpToolbarLevel_    = -1;     // 폭 사다리 레벨(-1 = 미초기화)
    bool  vpToolbarDirty_    = true;   // 지연 리빌드 요청
    bool  prevGizmoDragging_ = false;  // 드래그 종료(하강 에지) 검출
    std::shared_ptr<SBorder>        viewportToolbarBar_;   // 배제 rect 의 진리원
    std::shared_ptr<SHorizontalBox> viewportToolbarBox_;
    // 기즈모 스냅. 소유자는 여기이고, 기즈모에는 매 프레임 사본을 밀어 넣습니다.
    HGizmoSnap snap_{};

    // Bottom panel tabs
    int bottomTab_ = 1;     // 0=content, 1=log, 2=gpu stats

    // Outliner search
    std::string outlinerSearch_;
    bool        outlinerSearchFocused_ = false;
    // 검색 상자 위젯. 포커스 밖 클릭 판정에 geometry 가 필요해 참조를 남깁니다.
    std::shared_ptr<SSearchBox> outlinerSearchBox_;

    // ── 텍스트 입력 포커스 중재 ───────────────────────────────────────────────
    // 텍스트를 받는 곳이 셋(인라인 리네임·아웃라이너 검색·숫자 필드)인데 각자 플래그로
    // 관리되어 서로를 몰랐습니다. 그래서 둘이 동시에 활성이 되거나, 다른 곳을 클릭해도
    // 편집 모드가 풀리지 않아 나중의 Enter 가 엉뚱한 값을 확정하는 일이 있었습니다.
    // 진입·이탈을 setTextFocus 한 곳으로 모으고, 실제 상태는 currentFocus 가 유도합니다.
    // 나중에 추가한 Output Log 검색(LogSearch)도 같은 중재를 따릅니다.
    enum class EFocusTarget { None, Rename, Search, NumberField, LogSearch };
    EFocusTarget currentFocus() const;
    void         setTextFocus(EFocusTarget t);
    // 지금 포커스를 가진 위젯이 이 좌표를 품는가(포커스 밖 클릭 판정).
    bool         focusWidgetContains(float x, float y) const;

    // ── 아웃라이너 계층 트리 (Phase 3) ────────────────────────────────────────
    // 행 치수(미스케일 기준값). s() 로 스케일해 씁니다.
    static constexpr float kOutRowH        = 30.f;
    static constexpr float kOutIndent      = 12.f;
    static constexpr float kOutArrowW      = 16.f;
    static constexpr float kOutTypeW       = 74.f;
    static constexpr float kOutEyeW        = 20.f;
    static constexpr float kOutDelW        = 26.f;
    static constexpr float kOutMinLabelW   = 56.f;
    static constexpr float kOutHeaderH     = 34.f;   // "+ Add Empty" 줄
    static constexpr float kOutFooterH     = 20.f;   // 액터 개수 줄

    // 평탄화된 한 행. 위젯 조립 직전에만 쓰는 스냅샷이라, actor 포인터는 콜백이
    // 캡처하지 않습니다(캡처 키는 언제나 id).
    struct OutlinerRow {
        uint64_t id       = 0;
        Actor*   actor    = nullptr;
        int      depth    = 0;
        bool     hasKids  = false;   // 필터 적용 후 실제로 표시될 자식이 있는가
        bool     expanded = false;   // 필터 중에는 항상 참
        bool     dimPath  = false;   // 매치가 아니라 '경로'로만 남은 조상
        bool     dimOff   = false;   // activeInHierarchy() 가 거짓
    };

    // 접힌 액터의 id 집합입니다. "펼침"이 아니라 "접힘"을 저장하므로, 상태가 비어도
    // 아무것도 숨지 않습니다(신규 스폰·재부모화가 즉시 보임). Actor::id 는 단조 증가라
    // 재사용되지 않아 해제된 포인터를 키로 쓰는 위험이 없습니다.
    std::unordered_set<uint64_t> outlinerCollapsed_;
    std::vector<OutlinerRow>     outlinerRows_;
    // 위젯 콜백 안에서 rebuildOutliner() 를 직접 부르면 슬롯 순회 중 자기 파괴가 되므로,
    // 콜백은 이 플래그만 세우고 tick/handleInput 종료 시점에 한 번 리빌드합니다.
    bool        outlinerDirty_     = false;
    bool        outlinerRevealSel_ = false;   // 이번 리빌드에서 선택 행을 창 안으로
    int         outlinerScrollTop_ = 0;       // 창 시작 행 인덱스
    int         outlinerRowCap_    = 0;       // 마지막 리빌드에서 표시 가능했던 행 수
    glm::ivec2  outlinerLayoutSig_ {0, 0};    // 리스트 실측 크기(4px 양자화) — 리사이즈 감지
    std::string outlinerFooter_;              // 개수 문자열 캐시(Attr 은 매 프레임 평가됨)
    std::string outlinerFilterPrev_;          // 직전 리빌드의 필터(바뀌면 스크롤을 맨 위로)
    // 아웃라이너가 자신의 리프에서 활성 탭인지. 뷰포트의 viewportVisible_ 과 같은 이유로
    // 필요합니다. 비활성이면 위젯이 배치되지 않아 cachedGeometry 가 stale 하고, 그 낡은
    // rect 로 휠을 가로채면 그 자리를 차지한 다른 패널 위에서 카메라 돌리가 죽습니다.
    bool        outlinerVisible_ = true;
    // 좌버튼을 아웃라이너 목록 위에서 눌러 아직 떼지 않았는지. 그동안은 목록 리빌드를 미뤄
    // 눌린 행 버튼이 뗄 때까지 살아 있게 합니다(DEF-83).
    bool        outlinerPressHeld_ = false;
    // 직전 선택(= 지금 선택의 하나 앞). Actor 메뉴의 부착 액션이 부모로 씁니다.
    Actor*      prevSelected2_ = nullptr;

    // Viewport overlay stats
    struct VpTimings {
        int fps=0, drawCalls=0, culled=0, transparentDraws=0;
        float shadowMs=0.f, mainMs=0.f, aoMs=0.f, bloomMs=0.f,
              tonemapMs=0.f, taaBlitMs=0.f, uiMs=0.f, totalMs=0.f;
        // 초당 스왑체인 재생성 횟수입니다. 0 이 아니면 그 자체가 결함입니다.
        float recreatePerSec = 0.f;
    };
    VpTimings vpTimings_;
    // FPS 는 실시간으로 셉니다. tick(dt) 의 dt 는 main.cpp 에서 0.05 로 클램프된
    // 값이라, 그것으로 세면 제목 표시줄(실시간 기준)과 다른 숫자가 나옵니다.
    // 실제로 83 과 91 이 동시에 표시되던 원인의 일부입니다.
    std::chrono::steady_clock::time_point fpsMark_  = std::chrono::steady_clock::now();
    int       fpsCount_ = 0;
    uint64_t  prevRecreate_ = 0;

    // Gizmo hotkey edge tracking
    bool prevGizQDown_ = false;
    bool prevGizWDown_ = false;
    bool prevGizEDown_ = false;
    bool prevGizRDown_ = false;

    bool      prevLeftDown_   = false;
    Actor*    prevSelected_   = nullptr;
    int       prevActorCount_ = -1;
    glm::vec3 cachedEuler_    {0.f, 0.f, 0.f};
    Transform undoSnapshot_;

    EPlayState  playState_     = EPlayState::Editor;
    std::string worldSnapshot_;

    std::vector<std::string> modelFiles_;
    std::vector<std::string> prefabFiles_;
    std::vector<std::string> audioFiles_;

    // ── Gizmo ─────────────────────────────────────────────────────────────────
    EditorGizmo gizmo_;

    // 기즈모 좌표계 헬퍼. 정의는 EditorApp.cpp 에 있습니다 (IWindow 완전 정의 필요).
    // 씬은 뷰포트가 아니라 프레임버퍼 전체에 그려지므로(Renderer_render.cpp:164),
    // 기즈모의 투영과 화면 매핑도 프레임버퍼 전체를 기준으로 삼아야 GPU 가 그린
    // 화살표 위치와 CPU 판정이 일치합니다. 마우스도 프레임버퍼 픽셀로 변환합니다.
    HViewportRect fullFbRect() const;
    // 씬을 그릴 3D 뷰포트 rect(프레임버퍼 픽셀, 오프셋 포함). 렌더러 setViewportRect 와
    // worldVP 가 공유하는 단일 진리원입니다.
    HViewportRect viewportFbRect() const;
    // 뷰포트 위젯이 아직 배치되지 않은 프레임에 돌려줄 직전 유효 rect. 고정 치수 산술로
    // 되돌아가면 크롬 높이가 바뀔 때마다 어긋나고, 그 rect 로 렌더 타깃이 한 번 더
    // 재생성됩니다. viewportFbRect 가 const 라 mutable 입니다.
    mutable HViewportRect lastValidVpRect_ {};
    glm::vec2     mouseToFb(float mx, float my, float winW, float winH) const;

    // 마우스↔월드 변환의 **유일한** 좌표 생산자입니다. pickActor 와 기즈모는 오직 이
    // 결과(WorldViewport)로만 좌표를 받으므로, 뷰포트 rect 나 윈도 마우스를 실수로
    // 넘기는 것이 구조적으로 불가능합니다. (좌표 감사 2026-08-26 권고 A)
    //
    // 주의(가정): fullFbRect 는 GLFW getFramebufferSize 를, 렌더는 swapchain extent 를
    // 각각 씁니다. 통상 같지만 리사이즈 직후 한 프레임에서 갈릴 수 있습니다. 그 경계는
    // 아직 코드로 보장하지 않습니다.
    WorldViewport worldVP(float mx, float my, float winW, float winH) const;

    // ── Rename state ──────────────────────────────────────────────────────────
    bool        renaming_          = false;
    bool        prevRenaming_      = false;
    std::string renameBuffer_;

    // ── Key edge tracking ─────────────────────────────────────────────────────
    bool prevDeleteDown_    = false;
    bool prevF2Down_        = false;
    bool prevBackspaceDown_ = false;
    bool prevEnterDown_     = false;
    bool prevEscapeDown_    = false;
    bool prevZDown_         = false;
    bool prevYDown_         = false;
    bool prevSDown_         = false;
    bool prevVDown_         = false;
    bool prevCDown_         = false;
    bool prevADown_         = false;
    bool prevFDown_         = false;

    // ── 텍스트 입력 전체 선택 상태 (Ctrl+A) ───────────────────────────────────
    bool renameSelAll_ = false;
    bool searchSelAll_ = false;

    // ── Actor counter (for "Add Empty" names) ─────────────────────────────────
    int actorCounter_ = 0;

    // ── Widget helpers (theme-aware; apply uiScale_ internally) ───────────────
    std::shared_ptr<HWidget> spacer() const;
    std::shared_ptr<HWidget> panelHeader(const std::string& title) const;
    std::shared_ptr<HWidget> sectionHeader(const std::string& title,
                                           glm::vec4 color = kTextHdr,
                                           float fontSize = 12.f) const;
    std::shared_ptr<SButton> textButton(const std::string& label, float fontSize,
                                        glm::vec4 textColor, HPadding pad,
                                        glm::vec4 normal, glm::vec4 hovered,
                                        std::function<void()> onClick) const;

    void buildWidgetTree();
    // ── 도킹 ─────────────────────────────────────────────────────────────────
    void buildDefaultDockLayout();                      // dockRoot_ 를 현재 배치로 초기화
    bool loadDockLayout();                              // Config/editor_layout.txt 복원(성공 시 dockRoot_ 설정)
    void saveDockLayout() const;                        // dockRoot_ → Config/editor_layout.txt
    void rebuildDockArea();                             // dockRoot_ → dockHost_ 로 조합
    std::shared_ptr<HWidget> buildDockWidget(DockNode* node);   // 노드 재귀 조합
    void rebuildToolbar();
    // ── 뷰포트 툴바 (Phase 2) ────────────────────────────────────────────────
    void          rebuildViewportToolbar();
    int           computeVpToolbarLevel() const;   // 뷰포트 폭 → 축약 단계(0=최대)
    HViewportRect viewportToolbarFbRect() const;   // 툴바가 가린 영역(프레임버퍼 픽셀)
    std::string   gizmoReadoutText() const;        // 드래그 중 값(뷰포트 하단 표시)
    void rebuildOutliner();
    // ── 아웃라이너 계층 헬퍼 (Phase 3) ───────────────────────────────────────
    // 루트부터 전위 순회하며 표시할 행을 평탄화합니다. 필터가 있으면 매치되지 않고
    // 매치 자손도 없는 서브트리를 통째로 버립니다.
    void        flattenOutliner(const std::string& filterLower,
                                std::vector<OutlinerRow>& out) const;
    // 컴포넌트에서 Type 라벨과 색을 유도합니다(고정 우선순위, 추가 순서 비의존).
    std::string outlinerTypeLabel(const Actor* a, glm::vec4& outColor) const;
    // 폭을 넘는 텍스트를 ".." 로 줄입니다. 폰트 실측을 쓰므로 const 가 아닙니다.
    std::string ellipsizeText(const std::string& text, float maxPx, float fontPx);
    void        addOutlinerRow(const OutlinerRow& r, float listW);
    void        revealActor(Actor* a);          // 조상 체인을 모두 펼침
    void        flushOutlinerRebuild();         // 지연 리빌드 요청 소진
    void rebuildDetails();
    // 쌓인 디테일 리빌드 요청을 소진합니다. tick·handleInput 첫머리에서 불러, 에디터 밖
    // (게임 tick·플레이 시작·끝 처리)에서 표시 대상이 파괴된 경우를 잡습니다(DEF-92).
    void flushDetailsRebuild();
    void rebuildLog();
    void rebuildContent();
    void rebuildBottomPanel();
    void scanModelFiles();

    void pickActor(float mx, float my, float winW, float winH);

    // 라이트·카메라·오디오처럼 솔리드 몸체가 없는 액터를 뷰포트에서 보이게 하는
    // 에디터 아이콘(빌보드)을 오버레이 리스트에 덧붙입니다. 기즈모와 합쳐 한 번에 설정합니다.
    void buildEditorIcons(World& world, const WorldViewport& wvp,
                          std::vector<LineVertex>& lines, std::vector<LineVertex>& tris);
    void deleteSelectedActor();
    void tickRenameInput();
    void tickShortcuts();
    void undo();
    void redo();
    void syncEulerCache();

    std::string serializeWorld();
    void        deserializeWorld(const std::string& data);
    void saveScene();
    void loadScene();

    // UE5-style dark theme
    static constexpr glm::vec4 kBgPanel   { 0.105f, 0.105f, 0.11f,  1.f };
    static constexpr glm::vec4 kBgHeader  { 0.078f, 0.078f, 0.082f, 1.f };
    static constexpr glm::vec4 kBgToolbar { 0.12f,  0.12f,  0.13f,  1.f };
    static constexpr glm::vec4 kBorder    { 0.055f, 0.055f, 0.060f, 1.f };
    static constexpr glm::vec4 kTextHdr   { 0.85f,  0.85f,  0.87f,  1.f };
    static constexpr glm::vec4 kTextBody  { 0.88f,  0.88f,  0.90f,  1.f };
    static constexpr glm::vec4 kTextDim   { 0.42f,  0.42f,  0.45f,  1.f };
    static constexpr glm::vec4 kSelected  { 0.071f, 0.467f, 0.792f, 0.50f };
    static constexpr glm::vec4 kAccent    { 0.071f, 0.467f, 0.792f, 1.f };
};
