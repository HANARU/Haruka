#pragma once
#include "EditorUiKit.hpp"
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
class EditorDock {
public:
    struct Deps {
        const EditorUiKit*   ui               = nullptr;
        const PanelRegistry* panelReg         = nullptr;
        HLogCategory*        logCat           = nullptr;
        const std::string*   uiLayoutOverride = nullptr;
        std::function<std::string()> layoutWritePath;
        std::function<void()>        ensureWriteDir;
        std::function<void(bool outlinerVis, bool bottomVis, bool detailsVis)> onVisibilityChanged;
        std::function<void()>        onCancelOutlinerDrag;
    };

    void init(Deps deps);
    std::shared_ptr<SBorder>      buildHost();
    std::shared_ptr<SDockOverlay> resetOverlay();

    bool hasRoot() const { return (bool)root_; }
    bool isViewportVisible() const { return viewportVisible_; }
    bool contains(const std::string& id) const { return root_ && dockContains(root_.get(), id); }

    void buildDefaultLayout();
    bool loadLayout();
    void saveLayout() const;
    void rebuildArea();

    void closePanel(const std::string& id);
    void showPanel(const std::string& id);

    // 스플리터 · 탭 입력 처리. 이벤트를 소비했으면 true 를 반환합니다(handleInput 조기 return).
    bool handleSplitterInput(float mx, float my, bool leftDown, bool prevLeftDown,
                             bool overPopup, IWindow* window);
    bool handleTabInput(float mx, float my, bool leftDown, bool prevLeftDown,
                        bool overPopup, IWindow* window);
    bool isSplitterDragging() const { return dragNode_ != nullptr; }

private:
    struct SplitterHit {
        std::shared_ptr<HWidget> handle;
        std::shared_ptr<HWidget> container;
        DockNode* node     = nullptr;
        int       index    = 0;
        bool      vertical = false;
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
    };

    float s(float x) const { return deps_.ui->s(x); }
    std::shared_ptr<HWidget> buildNodeWidget(DockNode* node);

    Deps deps_;
    std::unique_ptr<DockNode>     root_;
    std::shared_ptr<SBorder>      host_;
    std::shared_ptr<SDockOverlay> overlay_;
    DockPanelHome                 panelHome_;
    bool                          viewportVisible_ = true;

    std::vector<SplitterHit> splitters_;
    std::vector<TabHit>      tabHits_;
    std::vector<LeafHit>     leafHits_;

    DockNode* dragNode_        = nullptr;
    int       dragIndex_       = 0;
    bool      dragVertical_    = false;
    float     dragStartMouse_  = 0.f;
    float     dragStartRatioL_ = 0.f;
    float     dragAxisPx_      = 1.f;

    bool        tabPressPending_ = false;
    bool        tabDragActive_   = false;
    DockNode*   tabSrcStack_     = nullptr;
    int         tabSrcIndex_     = 0;
    std::string tabSrcPanel_;
    glm::vec2   tabPressPos_     {};
    DockNode*   tabTargetLeaf_   = nullptr;
    EDockZone   tabTargetZone_   = EDockZone::None;

    std::shared_ptr<HWidget> tabCloseWidget_;
    std::shared_ptr<HWidget> tabCloseRow_;
    std::string              tabClosePanel_;
};
