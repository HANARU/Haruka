#pragma once
#include "../ui/slate/HSlate.hpp"
#include <chrono>
#include "../core/Transform.hpp"
#include "EditorContext.hpp"
#include "EditorGizmo.hpp"
#include "HCommandStack.hpp"
#include "HotReloader.hpp"
#include "dock/DockTree.hpp"
#include "dock/SDockOverlay.hpp"
#include "../core/HDelegate.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <algorithm>
#include <memory>
#include <vector>
#include <string>

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

    void tickEditorCamera(float dt);

    // Log capture
    LogStreamBuf*   coutBuf_  = nullptr;
    LogStreamBuf*   cerrBuf_  = nullptr;
    std::streambuf* origCout_ = nullptr;
    std::streambuf* origCerr_ = nullptr;

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
    static constexpr float kMenuBarH = 26.f;
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

    // 트리 변형: 드롭 시 소스에서 빼고 타깃에 붙임(탭화 또는 분할). 빈 리프 정리.
    void performRedock(DockNode* src, int srcIdx, const std::string& panelId,
                       DockNode* target, EDockZone zone);
    void collapseEmptyLeaf(DockNode* leaf);

    // Gizmo mode & space
    int  gizmoMode_  = 1;   // 0=select, 1=move, 2=rotate, 3=scale
    bool worldSpace_ = true;

    // Bottom panel tabs
    int bottomTab_ = 1;     // 0=content, 1=log, 2=gpu stats

    // Outliner search
    std::string outlinerSearch_;
    bool        outlinerSearchFocused_ = false;

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
    void rebuildOutliner();
    void rebuildDetails();
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
