#pragma once
#include "EditorUiKit.hpp"
#include "dock/DockDragRules.hpp"
#include "dock/DockTree.hpp"
#include "dock/SDockOverlay.hpp"
#include "../core/HLog.hpp"
#include "../ui/slate/HSlate.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class IWindow;

// 에디터 도킹 시스템(S6f).
// 도크 트리 소유, 위젯 트리 조합, 스플리터/탭/닫기 x 입력 처리, 배치 저장/복원을 담당합니다.
//
// 창 단위(다중 OS 창 M3a, 2026-11-22): 메인 트리와 떠 있는 창(floats_)마다 "창 상태"(WindowState: 도크 영역 호스트 위젯 · 히트
// 목록 · 끌기 상태)를 하나씩 둡니다. 위젯 트리는 rebuildArea 가 모든 창을 한 번에 다시 만들고, 떠 있는 창의 호스트 위젯은 그 창이
// 살아 있는 동안 같은 객체라 OS 창 쪽(EditorFloatWindows)은 문맥의 루트를 다시 넣지 않아도 됩니다. 입력은 창마다 그 창 상태로
// 받습니다(M3b). 탭 끌기는 누른 창의 상태가 끝까지 들고, 놓을 자리는 화면 좌표로 다른 창의 히트 목록에서도 고릅니다(M4: 미리보기는
// 놓을 창의 overlay, 규칙은 dock/DockDragRules.hpp).
class EditorDock {
public:
    struct Deps {
        const EditorUiKit*   ui               = nullptr;
        const PanelRegistry* panelReg         = nullptr;
        HLogCategory*        logCat           = nullptr;
        const std::string*   uiLayoutOverride = nullptr;
        std::function<std::string()> layoutWritePath;
        std::function<void()>        ensureWriteDir;
        // 보임 여부는 메인 트리와 떠 있는 창 모두의 활성 탭을 셉니다(M3a). 메인 창 입력에서 좌표로 패널을 판정하는 곳은
        // panelActiveInMain 을 따로 봅니다(떠 있는 창의 위젯 좌표는 그 창 기준이라 메인 마우스 좌표와 비교하면 안 됨).
        std::function<void(bool outlinerVis, bool bottomVis, bool detailsVis)> onVisibilityChanged;
        std::function<void()>        onCancelOutlinerDrag;
        // Window 메뉴 · Ctrl+F 가 떠 있는 창에 있는 패널을 열려고 하면 그 창을 앞으로 가져옵니다(M3a). 인자는 떠 있는 창 번호입니다.
        // 탭을 다른 창에 놓은 뒤에도 그 창을 앞으로 가져옵니다(M4, UE 와 같음). 이때는 0 = 메인일 수 있습니다.
        std::function<void(int winId)> onRaiseFloat;
        // 저장 직전에 떠 있는 OS 창의 지금 자리 · 크기(내용 영역, 화면 좌표)를 읽습니다. 거짓이면(창 없음 · 최소화) 배치 값을 그대로 씁니다.
        std::function<bool(int floatId, int& x, int& y, int& w, int& h)> readFloatRect;
        // ── 창을 넘는 탭 끌기(M4) ──
        // 놓을 후보 창(메인 + 떠 있는 창)의 화면 자리 · 앞뒤 순서. 없거나 원래 창이 목록에 없으면 예전처럼 원래 창 안에서만 고릅니다
        // (창 밖에 놓으면 아무 일 없음).
        std::function<std::vector<hdockdrag::DropWindow>()> dropWindows;
        // 새 떠 있는 창 자리를 모니터 작업 영역 안으로 끼웁니다(hwinplace::clampToWorkAreas). 없으면 그대로.
        std::function<void(hdockdrag::ScreenRect&)> clampFloatRect;
    };

    void init(Deps deps);
    std::shared_ptr<SBorder>      buildHost();
    std::shared_ptr<SDockOverlay> resetOverlay();

    bool hasRoot() const { return (bool)root_; }
    bool isViewportVisible() const { return viewportVisible_; }
    // 메인 트리와 떠 있는 창 모두를 봅니다(Window 메뉴 체크 표시가 떠 있는 창의 패널도 "열림"으로 보이게).
    bool contains(const std::string& id) const { return dockContainsAll(root_.get(), floats_, id); }
    // 메인 트리에서 활성 탭인지(메인 창 좌표로 패널 위젯을 판정해도 되는지).
    bool panelActiveInMain(const std::string& id) const { return root_ && dockPanelIsActive(root_.get(), id); }
    // 패널이 들어 있는 떠 있는 창 번호(없거나 메인이면 0). 떠 있는 창 번호는 1 부터입니다.
    int  floatOfPanel(const std::string& id) const;
    // 창 winId(0 = 메인, 1.. = 떠 있는 창)의 트리에서 활성 탭인지(M3b: 그 창 좌표로 패널 위젯을 판정해도 되는지).
    bool panelActiveIn(int winId, const std::string& id) const;
    const std::vector<DockFloat>& floats() const { return floats_; }

    // ── 떠 있는 창(M3a) ───────────────────────────────────────────────────────────
    // 그 창 문맥의 루트로 쓸 도크 영역 위젯(탭 줄 + 패널, 메뉴바 · 툴바 · 상태바 없음). rebuildArea 뒤부터 있고, 창이 목록에서 빠지면
    // nullptr 입니다. 같은 번호의 창이 살아 있는 동안 같은 객체입니다.
    std::shared_ptr<SBorder> floatHost(int floatId) const;
    // 그 창 문맥의 루트(M3b): SOverlay{호스트, 탭 끌기 미리보기, 아웃라이너 끌어 놓기 미리보기}. 미리보기가 꺼져 있으면 그리는 것이
    // 호스트와 같습니다(M3a 덤프 그대로). 같은 번호의 창이 살아 있는 동안 같은 객체입니다.
    std::shared_ptr<HWidget> floatRoot(int floatId) const;
    // 그 창의 아웃라이너 끌어 놓기 미리보기(아웃라이너가 그 창에 있을 때 OutlinerPanel::useDropOverlay 로 넘김).
    std::shared_ptr<SDockOverlay> floatDropOverlay(int floatId) const;
    // 창 제목: 창 트리 첫 리프의 활성 탭 제목(UE 와 같음). 창이 없으면 빈 글.
    std::string floatTitle(int floatId) const;
    // OS 닫기 단추(결정 D4): 안의 탭을 모두 닫고 창을 목록에서 뺍니다. 그 창의 노드를 가리키는 끌기 상태를 먼저 비우고, 위젯 트리를
    // 다시 만든 뒤 저장합니다. 닫을 수 없는 패널이 있으면 아무것도 하지 않고 false 입니다.
    bool closeFloat(int floatId);
    // OS 창에서 읽은 자리 · 크기를 배치 데이터에 적습니다(저장 전에 부름). 트리는 다시 만들지 않습니다.
    void setFloatRect(int floatId, int x, int y, int w, int h, bool maximized);

    void buildDefaultLayout();
    bool loadLayout();
    // 저장할 때 떠 있는 창의 자리 · 크기를 OS 창에서 읽어 배치 데이터에 먼저 적습니다(readFloatRect).
    void saveLayout();
    // 메인과 모든 떠 있는 창의 도크 위젯 트리를 다시 만듭니다. 먼저 모든 창의 히트 목록과 호스트 내용을 비운 뒤 만들므로, 패널
    // 위젯 하나가 두 루트에 동시에 있는 순간이 없습니다(패널은 트리 하나에만 있음 — dockParseLayoutAll 의 seen 공유와 이동 연산).
    void rebuildArea();

    void closePanel(const std::string& id);
    void showPanel(const std::string& id);

    // 스플리터 · 탭 입력 처리. winId 는 0 = 메인, 1.. = 떠 있는 창이고 좌표 · 버튼은 그 창 것입니다(M3b). 그 창의 창 상태(히트 목록 ·
    // 끌기 상태 · 미리보기)만 씁니다. window 는 그 창(커서 모양 · 카메라 모드). 이벤트를 소비했으면 true(handleInput 조기 return).
    // 탭 끌기는 놓을 자리를 화면 좌표로 고릅니다(M4): 커서 아래 창들 중 가장 앞의 창(원래 창 포함)의 리프 → 그 창으로 재도킹,
    // 어느 창 위도 아니면 새 떠 있는 창. 떠 있는 창의 탭 줄 빈 곳을 끌면 그 창 통째로 다른 창의 리프에 합칩니다.
    bool handleSplitterInput(int winId, float mx, float my, bool leftDown, bool prevLeftDown,
                             bool overPopup, IWindow* window);
    bool handleTabInput(int winId, float mx, float my, bool leftDown, bool prevLeftDown,
                        bool overPopup, IWindow* window);
    bool isSplitterDragging() const;
    // 어느 창에서든 탭(또는 떠 있는 창 통째) 끌기가 6px 넘게 움직인 중인지. 그동안 다른 창은 커서 모양을 바꾸지 않습니다.
    bool isTabDragging() const;

    // 시험 · 진단: 창 상태 수(메인 포함).
    size_t windowStateCount() const { return 1 + floatWins_.size(); }

private:
    // 스플리터 손잡이 하나. 끄는 동안 위젯 트리를 다시 만들지 않고 vbox · hbox(둘 중 하나)의 슬롯 가중치만
    // 고칩니다(H1b). 도크 노드의 자식 i 는 상자의 슬롯 2i 입니다(자식 · 손잡이 · 자식 … 순서).
    struct SplitterHit {
        std::shared_ptr<HWidget> handle;
        std::shared_ptr<HWidget> container;
        DockNode* node     = nullptr;
        int       index    = 0;
        bool      vertical = false;
        std::shared_ptr<SVerticalBox>   vbox;
        std::shared_ptr<SHorizontalBox> hbox;
    };
    struct TabHit {
        std::shared_ptr<HWidget> widget;
        DockNode*   stack    = nullptr;
        int         tabIndex = 0;
        std::string panelId;
        std::shared_ptr<HWidget> close;
        std::shared_ptr<HWidget> row;
    };
    struct LeafHit {
        std::shared_ptr<HWidget> widget;
        DockNode* leaf = nullptr;
        std::shared_ptr<HWidget> tabRow;   // 그 리프의 탭 줄(떠 있는 창의 빈 곳 누르기 = 창 통째 끌기, M4)
    };

    // 창 하나의 도크 위젯 상태(M3a). 히트 목록은 위젯 좌표가 그 창 기준이라 창마다 따로 둡니다. 끌기 상태도 창마다 두어, 떠 있는
    // 창이 닫힐 때 그 창의 노드를 가리키는 상태가 함께 사라지게 합니다(M3b 의 창별 입력이 이 구조체를 그대로 씀).
    struct WindowState {
        int                      floatId = 0;   // 0 = 메인, 1.. = DockFloat::id
        std::shared_ptr<SBorder> host;
        // 떠 있는 창만: 문맥 루트(SOverlay{host, overlay, dropOverlay}). 메인은 EditorApp 루트가 이 자리입니다.
        std::shared_ptr<HWidget>      root;
        // 탭 끌기 미리보기(메인은 overlay_ — EditorApp 루트의 맨 위 근처). 아웃라이너 끌어 놓기 미리보기(떠 있는 창만).
        std::shared_ptr<SDockOverlay> overlay;
        std::shared_ptr<SDockOverlay> dropOverlay;

        std::vector<SplitterHit> splitters;
        std::vector<TabHit>      tabHits;
        std::vector<LeafHit>     leafHits;

        DockNode* dragNode        = nullptr;
        int       dragIndex       = 0;
        bool      dragVertical    = false;
        float     dragStartMouse  = 0.f;
        float     dragStartRatioL = 0.f;
        float     dragAxisPx      = 1.f;

        bool        tabPressPending = false;
        bool        tabDragActive   = false;
        DockNode*   tabSrcStack     = nullptr;
        int         tabSrcIndex     = 0;
        std::string tabSrcPanel;
        glm::vec2   tabPressPos     {};
        DockNode*   tabTargetLeaf   = nullptr;
        EDockZone   tabTargetZone   = EDockZone::None;
        // M4: 놓을 창(-1 = 어느 창 위도 아님) · 커서의 화면 좌표(화면 좌표를 모르면 haveScreen 거짓) · 누를 때 탭 안에서 잡은 자리 ·
        // 원래 리프 크기(새 창 크기의 기준) · 창 통째 끌기 · 끄는 동안 마지막으로 알린 할 일(로그 · 커서 모양은 바뀔 때만).
        int         tabTargetWin    = -1;
        bool        tabHaveScreen   = false;
        glm::vec2   tabScreenPos    {};
        glm::vec2   tabGrab         {};
        glm::vec2   tabSrcLeafSize  {};
        bool        tabMergeDrag    = false;
        hdockdrag::DropAction tabShownAction = hdockdrag::DropAction::None;
        int         tabShownOver    = -2;   // 마지막으로 알린 놓을 창(-2 = 아직 없음)

        std::shared_ptr<HWidget> tabCloseWidget;
        std::shared_ptr<HWidget> tabCloseRow;
        std::string              tabClosePanel;

        void clearHits() { splitters.clear(); tabHits.clear(); leafHits.clear(); }
        // 끌기 · 누름 상태를 모두 놓습니다(창이 닫히거나 트리가 통째로 바뀔 때).
        void clearDrag() {
            if (overlay) overlay->active = false;
            dragNode = nullptr;
            tabPressPending = tabDragActive = false;
            tabSrcStack = tabTargetLeaf = nullptr;
            tabTargetZone = EDockZone::None;
            tabTargetWin = -1;
            tabHaveScreen = tabMergeDrag = false;
            tabShownAction = hdockdrag::DropAction::None;
            tabShownOver = -2;
            tabSrcPanel.clear();
            tabCloseWidget.reset();
            tabCloseRow.reset();
            tabClosePanel.clear();
        }
    };

    float s(float x) const { return deps_.ui->s(x); }
    std::shared_ptr<HWidget> buildNodeWidget(DockNode* node, WindowState& win);
    // 끄는 스플리터(win.dragNode · dragIndex)의 상자 슬롯 가중치를 고칩니다(H1b). splitters 에서 지금 트리의 손잡이를
    // 찾으므로, 끄는 도중 다른 경로가 도크를 다시 만들어도 새 상자를 고칩니다. 못 찾으면 false 입니다.
    bool applySplitterWeights(const WindowState& win, float left, float right);
    // 창 상태 하나의 스플리터 · 탭 입력(M3b). 공개 함수는 번호로 창 상태를 찾아 부릅니다.
    bool handleSplitterInput(WindowState& w, float mx, float my, bool leftDown, bool prevLeftDown,
                             bool overPopup, IWindow* window);
    bool handleTabInput(WindowState& w, float mx, float my, bool leftDown, bool prevLeftDown,
                        bool overPopup, IWindow* window);
    WindowState* stateOf(int winId) { return winId == 0 ? &main_ : floatState(winId); }
    // 창 상태가 가리키는 트리의 루트(메인 root_ · 떠 있는 창 floats_[i].root). 없으면 null.
    std::unique_ptr<DockNode>* treeOf(int winId);
    // floatWins_ 를 floats_ 의 번호와 맞춥니다(같은 번호는 상태 · 호스트 유지, 빠진 창은 호스트 내용을 비우고 버림).
    void syncFloatStates();
    WindowState*       floatState(int floatId);
    const WindowState* floatState(int floatId) const;
    int                floatIndexOf(int floatId) const;
    // ── 창을 넘는 끌기(M4) ──
    // 끄는 커서(원래 창 좌표 m)로 놓을 창 · 리프 · 영역을 찾아 w 에 적고, 미리보기는 그 창의 overlay 에만 켭니다.
    void updateDropTarget(WindowState& w, glm::vec2 m);
    // 모든 창의 탭 끌기 미리보기를 끕니다.
    void hideDropOverlays();
    // 끌기를 놓았을 때 할 일을 합니다. w 는 부르기 전에 이미 비워져 있어야 합니다(이동이 그 창을 지울 수 있음 — closeFloat 와 같은 순서).
    struct DropRequest {
        int         srcWin = 0;
        DockNode*   srcStack = nullptr;
        int         srcIndex = 0;
        std::string panel;
        int         targetWin = -1;
        DockNode*   targetLeaf = nullptr;
        EDockZone   zone = EDockZone::None;
        bool        dragActive = false;
        bool        merge = false;
        bool        haveScreen = false;
        glm::vec2   screen{}, grab{}, leafSize{};
    };
    void performDrop(const DropRequest& r);
    // 떠 있는 창 floatId 가 곧 사라질 때: 그 창 상태의 끌기 · 누름을 비우고, 다른 창 상태가 그 창 노드를 가리키면(끄는 놓을 자리) 놓을
    // 자리만 지웁니다(closeFloat · 탭 닫기 · 이동으로 빈 창 모두).
    void forgetFloatInDrags(int floatId);
    // 마지막 탭이 빠진 떠 있는 창을 목록에서 뺍니다(끌기 상태를 먼저 비움). OS 창 · 렌더러 대상은 프레임 끝에 지워집니다.
    void removeEmptyFloats();

    Deps deps_;
    std::unique_ptr<DockNode>     root_;
    std::shared_ptr<SDockOverlay> overlay_;
    DockPanelHome                 panelHome_;
    // 떠 있는 창(M1 데이터 · M3a 화면). OS 창은 EditorFloatWindows 가 이 목록의 번호를 보고 프레임 끝에 만들고 없앱니다.
    std::vector<DockFloat>        floats_;
    bool                          viewportVisible_ = true;

    WindowState              main_;        // 메인 창(host = buildHost 의 도크 영역)
    // floats_ 와 같은 번호 집합(rebuildArea 가 맞춤). 창 상태의 주소가 rebuildArea 를 건너 바뀌지 않게 unique_ptr 로 둡니다(M3b:
    // 떠 있는 창의 탭 누름 처리가 rebuildArea 를 부른 뒤에도 같은 창 상태를 고침).
    std::vector<std::unique_ptr<WindowState>> floatWins_;
    // 이번 실행에서 쓴 가장 큰 떠 있는 창 번호(같은 프레임에 지운 창 번호를 새 창에 되쓰지 않게, hdockdrag::issueFloatId).
    int                           floatIdFloor_ = 0;
};
