#include "EditorDock.hpp"
#include "../platform/IWindow.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

void EditorDock::init(Deps deps) {
    deps_ = std::move(deps);
}

std::shared_ptr<SBorder> EditorDock::buildHost() {
    main_.host = HNew(SBorder)->background(EditorTheme::transparent);
    return main_.host;
}

int EditorDock::floatOfPanel(const std::string& id) const {
    for (const auto& f : floats_)
        if (dockContains(f.root.get(), id)) return f.id;
    return 0;
}

bool EditorDock::panelActiveIn(int winId, const std::string& id) const {
    if (winId == 0) return panelActiveInMain(id);
    const int i = floatIndexOf(winId);
    return i >= 0 && floats_[(size_t)i].root && dockPanelIsActive(floats_[(size_t)i].root.get(), id);
}

std::unique_ptr<DockNode>* EditorDock::treeOf(int winId) {
    if (winId == 0) return &root_;
    const int i = floatIndexOf(winId);
    return i >= 0 ? &floats_[(size_t)i].root : nullptr;
}

bool EditorDock::isTabDragging() const {
    if (main_.tabPressPending && main_.tabDragActive) return true;
    for (const auto& w : floatWins_)
        if (w->tabPressPending && w->tabDragActive) return true;
    return false;
}

bool EditorDock::isSplitterDragging() const {
    if (main_.dragNode) return true;
    for (const auto& w : floatWins_)
        if (w->dragNode) return true;
    return false;
}

int EditorDock::floatIndexOf(int floatId) const {
    for (size_t i = 0; i < floats_.size(); ++i)
        if (floats_[i].id == floatId) return (int)i;
    return -1;
}

EditorDock::WindowState* EditorDock::floatState(int floatId) {
    for (auto& w : floatWins_)
        if (w && w->floatId == floatId) return w.get();
    return nullptr;
}

const EditorDock::WindowState* EditorDock::floatState(int floatId) const {
    for (const auto& w : floatWins_)
        if (w && w->floatId == floatId) return w.get();
    return nullptr;
}

std::shared_ptr<SBorder> EditorDock::floatHost(int floatId) const {
    const WindowState* w = floatState(floatId);
    return w ? w->host : nullptr;
}

std::shared_ptr<HWidget> EditorDock::floatRoot(int floatId) const {
    const WindowState* w = floatState(floatId);
    return w ? w->root : nullptr;
}

std::shared_ptr<SDockOverlay> EditorDock::floatDropOverlay(int floatId) const {
    const WindowState* w = floatState(floatId);
    return w ? w->dropOverlay : nullptr;
}

std::string EditorDock::floatTitle(int floatId) const {
    const int i = floatIndexOf(floatId);
    if (i < 0) return {};
    const DockNode* n = floats_[(size_t)i].root.get();
    while (n && !n->isLeaf()) n = n->children.empty() ? nullptr : n->children.front().get();
    if (!n || n->panelIds.empty()) return {};
    const int t = (n->activeTab >= 0 && n->activeTab < (int)n->panelIds.size()) ? n->activeTab : 0;
    const PanelDef* def = deps_.panelReg ? deps_.panelReg->find(n->panelIds[(size_t)t]) : nullptr;
    return def ? def->title : n->panelIds[(size_t)t];
}

void EditorDock::syncFloatStates() {
    std::vector<std::unique_ptr<WindowState>> next;
    next.reserve(floats_.size());
    for (const auto& f : floats_) {
        auto it = std::find_if(floatWins_.begin(), floatWins_.end(),
                               [&](const std::unique_ptr<WindowState>& w) { return w && w->floatId == f.id; });
        if (it != floatWins_.end()) {
            next.push_back(std::move(*it));   // 옮겨 감(아래 정리에서 건너뜀, 객체 주소 그대로)
        } else {
            auto w = std::make_unique<WindowState>();
            w->floatId = f.id;
            w->host    = HNew(SBorder)->background(EditorUiKit::kBgPanel);
            // 문맥 루트: 호스트 위에 미리보기 둘(M3b). 미리보기는 꺼져 있으면 아무것도 그리지 않고 히트 테스트에서도 빠집니다.
            w->overlay     = HNew(SDockOverlay);
            w->dropOverlay = HNew(SDockOverlay);
            w->root = HNew(SOverlay)->addSlot(w->host)->addSlot(w->overlay)->addSlot(w->dropOverlay);
            next.push_back(std::move(w));
        }
    }
    // 목록에서 빠진 창: OS 창은 프레임 끝까지 이 호스트를 루트로 들고 있으므로, 내용을 지금 비워 그 사이 패널 위젯이 옛 창 트리에
    // 남지 않게 합니다(그 패널은 이번 rebuildArea 에서 다른 트리에 들어갈 수 있음).
    for (auto& w : floatWins_) {
        if (!w) continue;
        if (w->host) w->host->content(nullptr);
        if (w->dropOverlay) w->dropOverlay->active = false;
        w->clearHits();
        w->clearDrag();
    }
    floatWins_ = std::move(next);
}

bool EditorDock::closeFloat(int floatId) {
    const int i = floatIndexOf(floatId);
    if (i < 0) return false;
    DockFloat& fl = floats_[(size_t)i];
    // 그 창의 노드를 가리키는 끌기 상태를 먼저 비웁니다. 창을 넘는 끌기(M4)는 다른 창 상태도 이 창 노드(놓을 자리)를 가리킬 수 있어
    // 모든 창 상태를 봅니다.
    forgetFloatInDrags(floatId);
    if (!dockCloseFloat(fl, panelHome_, [this](const std::string& id) {
            const PanelDef* d = deps_.panelReg->find(id);
            return !d || d->closable;
        }))
        return false;
    floats_.erase(floats_.begin() + i);
    rebuildArea();
    saveLayout();
    return true;
}

void EditorDock::forgetFloatInDrags(int floatId) {
    const int i = floatIndexOf(floatId);
    const DockNode* fr = i >= 0 ? floats_[(size_t)i].root.get() : nullptr;
    auto visit = [&](WindowState& s) {
        if (s.floatId == floatId) {
            s.clearDrag();
            return;
        }
        // 원래 자리 · 스플리터가 그 창 노드면(트리가 바뀐 경우를 막음) 모두 놓고, 놓을 자리만 그 창이면 놓을 자리만 지웁니다.
        if ((s.dragNode && dockOwns(fr, s.dragNode)) || (s.tabSrcStack && dockOwns(fr, s.tabSrcStack))) {
            s.clearDrag();
            return;
        }
        if (s.tabTargetWin == floatId || (s.tabTargetLeaf && dockOwns(fr, s.tabTargetLeaf))) {
            // 놓을 창이 사라짐: 다음 움직임이 다시 고를 때까지 "자기 창의 리프 아닌 곳"(놓으면 아무 일 없음)으로 둡니다. -1(창 밖)로 두면
            // 움직이지 않고 놓았을 때 새 창이 생깁니다.
            s.tabTargetLeaf = nullptr;
            s.tabTargetZone = EDockZone::None;
            s.tabTargetWin  = s.floatId;
        }
    };
    visit(main_);
    for (auto& w : floatWins_)
        if (w) visit(*w);
    if (WindowState* w = floatState(floatId); w && w->overlay) w->overlay->active = false;
}

void EditorDock::removeEmptyFloats() {
    for (int id : hdockdrag::emptyFloats(floats_)) {
        forgetFloatInDrags(id);
        const int i = floatIndexOf(id);
        if (i >= 0) floats_.erase(floats_.begin() + i);
        if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Floating window " << id << ": last tab moved out, closing";
    }
}

void EditorDock::setFloatRect(int floatId, int x, int y, int w, int h, bool maximized) {
    const int i = floatIndexOf(floatId);
    if (i < 0) return;
    DockFloat& f = floats_[(size_t)i];
    f.x = x;
    f.y = y;
    f.w = std::clamp(w, kDockFloatMinW, kDockFloatMaxDim);
    f.h = std::clamp(h, kDockFloatMinH, kDockFloatMaxDim);
    f.maximized = maximized;
}

std::shared_ptr<SDockOverlay> EditorDock::resetOverlay() {
    overlay_ = HNew(SDockOverlay);
    main_.overlay = overlay_;
    return overlay_;
}

void EditorDock::buildDefaultLayout() {
    using S = DockNode::Split;
    std::vector<std::unique_ptr<DockNode>> topKids;
    topKids.push_back(DockNode::makeLeaf({"Outliner"}));
    topKids.push_back(DockNode::makeLeaf({"Viewport"}));
    topKids.push_back(DockNode::makeLeaf({"Details"}));
    auto topRow = DockNode::makeSplit(S::Horizontal, std::move(topKids), {0.20f, 0.60f, 0.20f});

    std::vector<std::unique_ptr<DockNode>> rootKids;
    rootKids.push_back(std::move(topRow));
    rootKids.push_back(DockNode::makeLeaf({"Bottom"}));
    root_ = DockNode::makeSplit(S::Vertical, std::move(rootKids), {0.72f, 0.28f});
    // 기본 배치(Reset Layout 포함)는 떠 있는 창 없이 모든 패널을 메인에 둡니다.
    floats_.clear();
    main_.clearDrag();   // 옛 트리 노드를 가리키지 않게(떠 있는 창 상태는 다음 rebuildArea 가 버림)
}

void EditorDock::rebuildArea() {
    // 1) 모든 창의 히트 목록 · 호스트 내용을 먼저 비웁니다. 패널이 메인에서 떠 있는 창으로(또는 반대로) 옮겨 갔을 때 새 트리를
    //    만드는 동안 옛 트리에도 남아 있으면 한 패널이 두 루트에 동시에 들어갑니다(cachedGeo_ 가 엉킴, 조사 A5).
    main_.clearHits();
    for (auto& w : floatWins_) {
        w->clearHits();
        if (w->host) w->host->content(nullptr);
    }
    if (main_.host && root_) main_.host->content(nullptr);
    for (const DockFloat& f : floats_) floatIdFloor_ = std::max(floatIdFloor_, f.id);
    syncFloatStates();

    // 보임: 메인 트리 또는 떠 있는 창의 활성 탭(M3a). Viewport 는 떠 있는 창에 들어가지 못하므로 메인만 봅니다(D8).
    auto activeAnywhere = [this](const char* id) {
        if (root_ && dockPanelIsActive(root_.get(), id)) return true;
        for (const auto& f : floats_)
            if (dockPanelIsActive(f.root.get(), id)) return true;
        return false;
    };
    viewportVisible_       = root_ && dockPanelIsActive(root_.get(), "Viewport");
    const bool outlinerVis = activeAnywhere("Outliner");
    const bool bottomVis   = activeAnywhere("Bottom");
    const bool detailsVis  = activeAnywhere("Details");

    // 2) 메인 → 떠 있는 창 순으로 만듭니다.
    if (main_.host && root_)
        main_.host->content(buildNodeWidget(root_.get(), main_));
    for (auto& w : floatWins_) {
        const int i = floatIndexOf(w->floatId);
        if (i >= 0 && w->host && floats_[(size_t)i].root)
            w->host->content(buildNodeWidget(floats_[(size_t)i].root.get(), *w));
    }

    if (deps_.onVisibilityChanged)
        deps_.onVisibilityChanged(outlinerVis, bottomVis, detailsVis);
}

std::shared_ptr<HWidget> EditorDock::buildNodeWidget(DockNode* node, WindowState& win) {
    using VR = SVerticalBox;
    using HR = SHorizontalBox;
    using BR = SBorder;
    if (!node) return deps_.ui->spacer();

    if (node->isLeaf()) {
        auto tabRow = HNew(HR);
        auto tabRowArea = HNew(BR)->background(EditorTheme::bgWindow)
            ->borderColor(EditorUiKit::kBorder)->borderThickness(1.f)
            ->clipChildren(true)->content(tabRow);
        for (int i = 0; i < (int)node->panelIds.size(); ++i) {
            const PanelDef* def = deps_.panelReg->find(node->panelIds[i]);
            std::string title = def ? def->title : node->panelIds[i];
            const bool active = (i == node->activeTab);
            std::shared_ptr<HWidget> tabContent = HNew(STextBlock)->text(title)->fontSize(s(12.f))
                ->color(active ? EditorUiKit::kTextBody : EditorUiKit::kTextDim)->vAlign(EVAlign::Center);
            std::shared_ptr<HWidget> closeBtn;
            const bool closable = def && def->closable;
            if (closable) {
                closeBtn = HNew(SButton)
                    ->style(EditorUiKit::buttonStyle(s(1.f)))
                    ->normalColor(EditorTheme::transparent)->hoveredColor(EditorTheme::ctlHover)
                    ->pressedColor(EditorTheme::ctlPressed)
                    ->padding(HPadding{s(4.f), 0.f})
                    ->content(deps_.ui->icon("x", 10.f, EditorUiKit::kTextDim, "x", 11.f));
                closeBtn->setTooltip(HTEXT("Editor.Dock", "Tip.CloseTab", "Close Tab"));
                tabContent = HNew(HR)
                    ->addSlot(tabContent, HR::ESizeRule::Auto)
                    ->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(6.f))
                    ->addSlot(closeBtn,   HR::ESizeRule::Auto);
            }
            auto tab = HNew(BR)
                ->background(active ? EditorUiKit::kBgPanel : EditorTheme::bgWindow)
                ->borderColor(EditorUiKit::kBorder)->borderThickness(1.f)
                ->cornerRadius(s(EditorTheme::radiusControl))
                ->padding(closable ? HPadding{s(12.f), s(6.f), s(4.f), s(6.f)} : HPadding{s(12.f), s(6.f)})
                ->content(tabContent);
            tabRow->addSlot(tab, HR::ESizeRule::Auto);
            win.tabHits.push_back({ tab, node, i, node->panelIds[i], closeBtn, tabRowArea });
        }
        tabRow->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fill);

        std::shared_ptr<HWidget> content;
        if (node->activeTab >= 0 && node->activeTab < (int)node->panelIds.size()) {
            const PanelDef* def = deps_.panelReg->find(node->panelIds[node->activeTab]);
            if (def) content = def->widget;
        }

        auto leaf = HNew(VR)
            ->addSlot(tabRowArea, VR::ESizeRule::Fixed, s(26.f))
            ->addSlot(HNew(BR)->clipChildren(true)->content(content ? content : deps_.ui->spacer()),
                      VR::ESizeRule::Fill);
        win.leafHits.push_back({ leaf, node, tabRowArea });
        return leaf;
    }

    const bool vertical = (node->split == DockNode::Split::Vertical);
    if (vertical) {
        auto box = HNew(VR);
        for (size_t i = 0; i < node->children.size(); ++i) {
            const float r = (i < node->ratios.size()) ? node->ratios[i] : 1.f;
            box->addSlot(buildNodeWidget(node->children[i].get(), win), VR::ESizeRule::Fill, r);
            if (i + 1 < node->children.size()) {
                auto handle = HNew(BR)->background(EditorUiKit::kBorder);
                box->addSlot(handle, VR::ESizeRule::Fixed, s(5.f));
                win.splitters.push_back({ handle, box, node, (int)i, true, box, nullptr });
            }
        }
        return box;
    }
    auto box = HNew(HR);
    for (size_t i = 0; i < node->children.size(); ++i) {
        const float r = (i < node->ratios.size()) ? node->ratios[i] : 1.f;
        box->addSlot(buildNodeWidget(node->children[i].get(), win), HR::ESizeRule::Fill, r);
        if (i + 1 < node->children.size()) {
            auto handle = HNew(BR)->background(EditorUiKit::kBorder);
            box->addSlot(handle, HR::ESizeRule::Fixed, s(5.f));
            win.splitters.push_back({ handle, box, node, (int)i, false, nullptr, box });
        }
    }
    return box;
}

void EditorDock::closePanel(const std::string& id) {
    const PanelDef* def = deps_.panelReg->find(id);
    if (!def) return;
    if (DockLocation loc = dockFindLeafAll(root_.get(), floats_, id); loc && loc.floatIndex >= 0) {
        // 떠 있는 창의 탭 닫기. 그 창 트리가 바뀌므로 다시 만듭니다(M3a: 떠 있는 창도 화면에 있음). 마지막 탭이면 창이 목록에서
        // 빠지고, OS 창은 프레임 끝에 없어집니다.
        bool emptied = false;
        DockFloat& fl = floats_[loc.floatIndex];
        if (!dockClosePanelInFloat(fl, panelHome_, id, def->closable, emptied)) return;
        if (emptied) {
            forgetFloatInDrags(fl.id);
            floats_.erase(floats_.begin() + loc.floatIndex);
        }
        rebuildArea();
        saveLayout();
        return;
    }
    if (!dockClosePanel(root_, panelHome_, id, def->closable)) return;
    rebuildArea();
    saveLayout();
}

void EditorDock::showPanel(const std::string& id) {
    if (!deps_.panelReg->find(id)) return;
    // 떠 있는 창에 있는 패널: 그 창 안에서 그 탭을 활성으로 하고 창을 앞으로 가져옵니다(M3a). 여기서 dockShowPanel 을
    // 부르면 메인에 같은 패널이 하나 더 생깁니다.
    if (DockLocation loc = dockFindLeafAll(root_.get(), floats_, id); loc && loc.floatIndex >= 0) {
        const int floatId = floats_[(size_t)loc.floatIndex].id;
        for (int i = 0; i < (int)loc.leaf->panelIds.size(); ++i) {
            if (loc.leaf->panelIds[(size_t)i] != id || loc.leaf->activeTab == i) continue;
            loc.leaf->activeTab = i;
            rebuildArea();
            break;
        }
        if (deps_.onRaiseFloat) deps_.onRaiseFloat(floatId);
        return;
    }
    if (!dockShowPanel(root_, panelHome_, id)) return;
    rebuildArea();
    saveLayout();
}

void EditorDock::showPanelFloating(const std::string& id, int w, int h, int x, int y) {
    if (!deps_.panelReg->find(id)) return;
    if (contains(id)) {
        showPanel(id);
        if (DockLocation loc = dockFindLeafAll(root_.get(), floats_, id); loc && loc.floatIndex < 0 && deps_.onRaiseFloat)
            deps_.onRaiseFloat(0);
        return;
    }
    DockFloat fl;
    fl.id = ++floatIdFloor_;
    hdockdrag::ScreenRect r{ x, y, w, h };
    if (deps_.clampFloatRect) deps_.clampFloatRect(r);
    fl.x = r.x; fl.y = r.y;
    fl.w = std::clamp(r.w, kDockFloatMinW, kDockFloatMaxDim);
    fl.h = std::clamp(r.h, kDockFloatMinH, kDockFloatMaxDim);
    fl.root = DockNode::makeLeaf({ id });
    floats_.push_back(std::move(fl));
    const int floatId = floats_.back().id;
    rebuildArea();
    saveLayout();
    if (deps_.onRaiseFloat) deps_.onRaiseFloat(floatId);
}

void EditorDock::saveLayout() {
    if (!root_ || !deps_.layoutWritePath) return;
    if (deps_.readFloatRect)
        for (DockFloat& f : floats_) {
            int x = 0, y = 0, w = 0, h = 0;
            if (deps_.readFloatRect(f.id, x, y, w, h)) setFloatRect(f.id, x, y, w, h, f.maximized);
        }
    if (deps_.ensureWriteDir) deps_.ensureWriteDir();
    std::ofstream f(deps_.layoutWritePath());
    if (!f) {
        if (deps_.logCat) HLOG(*deps_.logCat, Error) << "Layout save failed";
        return;
    }
    f << dockSerializeLayout(root_.get(), floats_);
}

bool EditorDock::loadLayout() {
    const std::string path = (deps_.uiLayoutOverride && !deps_.uiLayoutOverride->empty())
        ? *deps_.uiLayoutOverride
        : (deps_.layoutWritePath ? deps_.layoutWritePath() : std::string());
    if (path.empty()) return false;
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    DockLayoutData data = dockParseLayoutAll(ss.str(),
        [this](const std::string& id) { return deps_.panelReg->find(id) != nullptr; },
        [this](const std::string& id) {
            const PanelDef* d = deps_.panelReg->find(id);
            return d && d->floatable;
        });
    if (!data.main) return false;
    main_.clearDrag();
    for (auto& w : floatWins_) w->clearDrag();
    root_   = std::move(data.main);
    floats_ = std::move(data.floats);
    return true;
}

bool EditorDock::handleSplitterInput(int winId, float mx, float my, bool leftDown, bool prevLeftDown,
                                     bool overPopup, IWindow* window) {
    WindowState* w = stateOf(winId);
    return w && handleSplitterInput(*w, mx, my, leftDown, prevLeftDown, overPopup, window);
}

bool EditorDock::handleTabInput(int winId, float mx, float my, bool leftDown, bool prevLeftDown,
                                bool overPopup, IWindow* window) {
    WindowState* w = stateOf(winId);
    return w && handleTabInput(*w, mx, my, leftDown, prevLeftDown, overPopup, window);
}

bool EditorDock::handleSplitterInput(WindowState& w, float mx, float my, bool leftDown, bool prevLeftDown,
                                     bool overPopup, IWindow* window) {
    if (!window || window->isCameraModeActive()) return false;
    // 어느 창에서 탭을 끄는 중이면(M4) 커서 모양은 그 끌기가 정합니다. 다른 창이 여기서 화살표로 되돌리면 끄는 창의 모양과 싸웁니다.
    if (!w.dragNode && isTabDragging()) return false;

    int cursorShape = 0;
    if (!w.dragNode) {
        const float grab = s(4.f);
        for (auto& h : w.splitters) {
            if (overPopup) break;
            if (!h.handle) continue;
            const HGeometry& hg = h.handle->cachedGeometry();
            if (hg.size.x < 1.f && hg.size.y < 1.f) continue;
            const bool hit = mx >= hg.position.x - grab && mx < hg.position.x + hg.size.x + grab &&
                             my >= hg.position.y - grab && my < hg.position.y + hg.size.y + grab;
            if (!hit) continue;
            cursorShape = h.vertical ? 2 : 1;
            if (leftDown && !prevLeftDown && h.node) {
                if (deps_.onCancelOutlinerDrag) deps_.onCancelOutlinerDrag();
                w.dragNode        = h.node;
                w.dragIndex       = h.index;
                w.dragVertical    = h.vertical;
                w.dragStartMouse  = h.vertical ? my : mx;
                w.dragStartRatioL = (h.index < (int)h.node->ratios.size())
                                  ? h.node->ratios[h.index] : 0.5f;
                w.dragAxisPx = 1.f;
                if (h.container) {
                    const HGeometry& cg = h.container->cachedGeometry();
                    w.dragAxisPx = std::max(1.f, h.vertical ? cg.size.y : cg.size.x);
                }
            }
            break;
        }
    } else {
        cursorShape = w.dragVertical ? 2 : 1;
    }
    window->setCursorShape(cursorShape);

    if (w.dragNode && leftDown) {
        const float cur    = w.dragVertical ? my : mx;
        const float dRatio = (cur - w.dragStartMouse) / w.dragAxisPx;
        auto&     R = w.dragNode->ratios;
        const int i = w.dragIndex;
        if (i >= 0 && i + 1 < (int)R.size()) {
            const float sum  = R[i] + R[i + 1];
            const float minR = 0.06f;
            if (sum > 2.f * minR) {
                const float newL = std::clamp(w.dragStartRatioL + dRatio, minR, sum - minR);
                if (newL != R[i]) {
                    R[i] = newL; R[i + 1] = sum - newL;
                    // 예전에는 여기서 rebuildArea() 로 도크 위젯 트리를 통째로 다시 만들었습니다(마우스가 움직일
                    // 때마다). 가중치만 바뀌므로 상자의 두 슬롯 값을 고칩니다. 배치 결과는 다시 만든 트리와 같습니다
                    // (만들 때 넣는 값도 ratios 그대로). 상자를 못 찾으면 예전처럼 다시 만듭니다.
                    if (!applySplitterWeights(w, R[i], R[i + 1])) rebuildArea();
                }
            }
        }
        return true;
    }
    if (!leftDown && w.dragNode) {
        w.dragNode = nullptr;
        saveLayout();
        return true;
    }
    return false;
}

bool EditorDock::applySplitterWeights(const WindowState& win, float left, float right) {
    for (const SplitterHit& h : win.splitters) {
        if (h.node != win.dragNode || h.index != win.dragIndex) continue;
        const size_t a = (size_t)h.index * 2, b = a + 2;
        if (h.vbox && b < h.vbox->slotCount())
            return h.vbox->setSlotValue(a, left) && h.vbox->setSlotValue(b, right);
        if (h.hbox && b < h.hbox->slotCount())
            return h.hbox->setSlotValue(a, left) && h.hbox->setSlotValue(b, right);
        return false;
    }
    return false;
}

bool EditorDock::handleTabInput(WindowState& w, float mx, float my, bool leftDown, bool prevLeftDown,
                                bool overPopup, IWindow* window) {
    if (!window || window->isCameraModeActive() || w.dragNode) return false;
    const glm::vec2 m{ mx, my };

    if (leftDown && !prevLeftDown && !w.tabPressPending && !w.tabCloseWidget && !overPopup) {
        for (auto& t : w.tabHits) {
            if (!t.row || !t.row->cachedGeometry().contains(m)) continue;
            if (t.close && t.close->cachedGeometry().contains(m)) {
                if (deps_.onCancelOutlinerDrag) deps_.onCancelOutlinerDrag();
                w.tabCloseWidget = t.close;
                w.tabCloseRow    = t.row;
                w.tabClosePanel  = t.panelId;
                break;
            }
            if (t.widget && t.widget->cachedGeometry().contains(m)) {
                if (deps_.onCancelOutlinerDrag) deps_.onCancelOutlinerDrag();
                w.tabPressPending = true; w.tabDragActive = false;
                w.tabSrcStack = t.stack; w.tabSrcIndex = t.tabIndex;
                w.tabSrcPanel = t.panelId; w.tabPressPos = m;
                w.tabMergeDrag = false;
                w.tabGrab = m - t.widget->cachedGeometry().position;
                w.tabSrcLeafSize = {};
                for (const LeafHit& lf : w.leafHits)
                    if (lf.leaf == t.stack && lf.widget) w.tabSrcLeafSize = lf.widget->cachedGeometry().size;
                break;
            }
        }
        // 떠 있는 창의 탭 줄 빈 곳(탭이 아닌 곳): 창 통째 끌기(M4, dockMergeFloatInto). 메인은 예전처럼 아래 UI 로 흘려보냅니다.
        if (!w.tabPressPending && !w.tabCloseWidget && w.floatId != 0) {
            for (const LeafHit& lf : w.leafHits) {
                if (!lf.tabRow || !lf.tabRow->cachedGeometry().contains(m)) continue;
                bool onTab = false;
                for (const TabHit& t : w.tabHits)
                    if (t.stack == lf.leaf && t.widget && t.widget->cachedGeometry().contains(m)) onTab = true;
                if (onTab) break;
                if (deps_.onCancelOutlinerDrag) deps_.onCancelOutlinerDrag();
                w.tabPressPending = true; w.tabDragActive = false;
                w.tabMergeDrag = true;
                w.tabSrcStack = nullptr; w.tabSrcIndex = 0;
                w.tabSrcPanel.clear(); w.tabPressPos = m;
                w.tabGrab = {}; w.tabSrcLeafSize = {};
                break;
            }
        }
    }

    if (w.tabCloseWidget) {
        if (!leftDown) {
            const bool        fire = w.tabCloseWidget->cachedGeometry().contains(m) &&
                                     w.tabCloseRow && w.tabCloseRow->cachedGeometry().contains(m);
            const std::string id   = w.tabClosePanel;
            w.tabCloseWidget.reset();
            w.tabCloseRow.reset();
            w.tabClosePanel.clear();
            if (fire) closePanel(id);   // 마지막 탭이면 이 창 상태가 사라질 수 있음 — 이 뒤로 w 를 건드리지 않음
        }
        return true;
    }

    if (w.tabPressPending) {
        if (leftDown) {
            if (!w.tabDragActive && glm::length(m - w.tabPressPos) > 6.f)
                w.tabDragActive = true;
            if (w.tabDragActive) {
                updateDropTarget(w, m);
                window->setCursorShape(hdockdrag::dragCursor(w.tabShownAction));
            }
            return true;
        }
        // 놓음. 할 일에 필요한 값을 먼저 옮겨 두고 창 상태를 비운 뒤 합니다. 이동이 이 창(떠 있는 창)을 비우면 다음 rebuildArea 가
        // 이 창 상태를 지우므로, 이 뒤로 w 를 건드리지 않습니다(closeFloat 와 같은 순서: 끌기 상태 → 트리 → 위젯).
        DropRequest r;
        r.srcWin     = w.floatId;
        r.srcStack   = w.tabSrcStack;
        r.srcIndex   = w.tabSrcIndex;
        r.panel      = w.tabSrcPanel;
        r.targetWin  = w.tabTargetWin;
        r.targetLeaf = w.tabTargetLeaf;
        r.zone       = w.tabTargetZone;
        r.dragActive = w.tabDragActive;
        r.merge      = w.tabMergeDrag;
        r.haveScreen = w.tabHaveScreen;
        r.screen     = w.tabScreenPos;
        r.grab       = w.tabGrab;
        r.leafSize   = w.tabSrcLeafSize;
        const bool wasDragging = w.tabDragActive;
        hideDropOverlays();
        w.tabPressPending = w.tabDragActive = w.tabMergeDrag = w.tabHaveScreen = false;
        w.tabSrcStack = w.tabTargetLeaf = nullptr;
        w.tabTargetZone = EDockZone::None;
        w.tabTargetWin = -1;
        w.tabShownAction = hdockdrag::DropAction::None;
        w.tabShownOver = -2;
        w.tabSrcPanel.clear();
        if (wasDragging) window->setCursorShape(0);
        performDrop(r);
        return true;
    }
    return false;
}

void EditorDock::hideDropOverlays() {
    if (main_.overlay) main_.overlay->active = false;
    for (auto& f : floatWins_)
        if (f && f->overlay) f->overlay->active = false;
}

void EditorDock::updateDropTarget(WindowState& w, glm::vec2 m) {
    using namespace hdockdrag;
    // 1) 놓을 창: 원래 창 좌표 + 원래 창의 화면 자리 = 화면 좌표, 그 아래 창들 중 가장 앞. 화면 자리를 모르면(시험 · 창 없음) 원래 창
    //    안에서만 고릅니다(M3b 와 같음).
    int over = w.floatId;
    glm::vec2 local = m;
    w.tabHaveScreen = false;
    if (deps_.dropWindows) {
        const std::vector<DropWindow> wins = deps_.dropWindows();
        if (const DropWindow* src = findWindow(wins, w.floatId)) {
            const glm::vec2 sp{ (float)src->rect.x + m.x, (float)src->rect.y + m.y };
            w.tabHaveScreen = true;
            w.tabScreenPos  = sp;
            over = pickDropWindow(sp.x, sp.y, wins);
            if (const DropWindow* ow = over >= 0 ? findWindow(wins, over) : nullptr)
                local = { sp.x - (float)ow->rect.x, sp.y - (float)ow->rect.y };
        }
    }
    // 2) 그 창의 리프 · 영역. 창 통째 끌기는 자기 창의 리프를 고르지 않습니다. 미리보기는 놓을 창의 overlay 에만 켭니다.
    w.tabTargetWin  = over;
    w.tabTargetLeaf = nullptr;
    w.tabTargetZone = EDockZone::None;
    hideDropOverlays();
    WindowState* tw = over >= 0 ? stateOf(over) : nullptr;
    if (tw && !(w.tabMergeDrag && over == w.floatId)) {
        for (auto& lf : tw->leafHits) {
            if (!lf.widget) continue;
            const HGeometry& g = lf.widget->cachedGeometry();
            if (!g.contains(local)) continue;
            ZoneRect hiz;
            w.tabTargetLeaf = lf.leaf;
            w.tabTargetZone = zoneInLeaf(local.x, local.y, { g.position.x, g.position.y, g.size.x, g.size.y }, hiz);
            HGeometry hi = g;
            hi.position = { hiz.x, hiz.y };
            hi.size     = { hiz.w, hiz.h };
            if (tw->overlay) { tw->overlay->active = true; tw->overlay->rect = hi; }
            break;
        }
    }
    // 3) 지금 놓으면 할 일 → 커서 모양(창 밖 미리보기, 부르는 쪽)과 로그(바뀔 때만).
    DropQuery q;
    q.dragActive = true;
    q.mergeDrag  = w.tabMergeDrag;
    q.srcWin     = w.floatId;
    q.overWin    = over;
    q.overLeaf   = w.tabTargetLeaf && w.tabTargetZone != EDockZone::None;
    q.floatable  = true;
    if (w.tabMergeDrag) {
        const int fi = floatIndexOf(w.floatId);
        std::vector<const DockNode*> st{ fi >= 0 ? floats_[(size_t)fi].root.get() : nullptr };
        while (!st.empty()) {
            const DockNode* n = st.back();
            st.pop_back();
            if (!n) continue;
            for (const auto& c : n->children) st.push_back(c.get());
            for (const auto& id : n->panelIds) {
                const PanelDef* d = deps_.panelReg->find(id);
                if (d && !d->floatable) q.floatable = false;
            }
        }
    } else {
        const PanelDef* d = deps_.panelReg->find(w.tabSrcPanel);
        q.floatable = !d || d->floatable;
    }
    // 화면 자리를 모르면 창 밖을 알 수 없으므로 "새 창"이 되지 않게 원래 창으로 둡니다.
    if (!w.tabHaveScreen && q.overWin < 0) q.overWin = w.floatId;
    const DropAction a = decideDrop(q);
    if (a == DropAction::Rejected) hideDropOverlays();   // 놓아도 거부되는 자리에는 도킹 미리보기를 그리지 않음(커서 = 금지)
    if (a != w.tabShownAction || over != w.tabShownOver) {
        w.tabShownAction = a;
        w.tabShownOver   = over;
        if (deps_.logCat) {
            const char* what = a == DropAction::NewFloat     ? "release opens a new floating window" :
                               a == DropAction::Rejected     ? "release is refused (panel cannot float)" :
                               a == DropAction::RedockAcross ? "release docks into another window" :
                               a == DropAction::MergeFloat   ? "release merges the window into another window" :
                               a == DropAction::RedockSame   ? "release docks in the same window" : "nothing to dock into";
            HLOG(*deps_.logCat, Log) << "Tab drag (window " << w.floatId << ", "
                                     << (w.tabMergeDrag ? std::string("whole window") : w.tabSrcPanel) << "): over "
                                     << (over < 0 ? std::string("no window") : "window " + std::to_string(over)) << ", " << what;
        }
    }
}

void EditorDock::performDrop(const DropRequest& r) {
    using namespace hdockdrag;
    DropQuery q;
    q.dragActive = r.dragActive;
    q.mergeDrag  = r.merge;
    q.srcWin     = r.srcWin;
    q.overWin    = r.targetWin;
    q.overLeaf   = r.targetLeaf && r.zone != EDockZone::None;
    const PanelDef* def = r.merge ? nullptr : deps_.panelReg->find(r.panel);
    q.floatable  = r.merge || !def || def->floatable;   // 통째 끌기는 아래 dockMergeFloatInto 가 패널마다 다시 봄
    if (!r.haveScreen && q.overWin < 0) q.overWin = r.srcWin;
    const DropAction a = decideDrop(q);

    std::unique_ptr<DockNode>* src = treeOf(r.srcWin);
    auto srcValid = [&] {
        return src && *src && r.srcStack && dockOwns(src->get(), r.srcStack) && r.srcIndex >= 0 &&
               r.srcIndex < (int)r.srcStack->panelIds.size() && r.srcStack->panelIds[(size_t)r.srcIndex] == r.panel;
    };
    // 이동 뒤: 빈 떠 있는 창 빼기(끌기 상태 먼저) → 위젯 트리 → 저장 → 놓은 창을 앞으로.
    auto finish = [&](int raiseWin) {
        removeEmptyFloats();
        rebuildArea();
        saveLayout();
        if (raiseWin >= 0 && deps_.onRaiseFloat) deps_.onRaiseFloat(raiseWin);
    };
    auto floatableOf = [this](const std::string& id) {
        const PanelDef* d = deps_.panelReg->find(id);
        return !d || d->floatable;
    };

    switch (a) {
        case DropAction::None:
            return;
        case DropAction::Activate:
            if (srcValid()) {
                r.srcStack->activeTab = r.srcIndex;
                rebuildArea();
            }
            return;
        case DropAction::Rejected:
            if (deps_.logCat)
                HLOG(*deps_.logCat, Log) << "Tab drop refused: "
                                         << (r.merge ? "window " + std::to_string(r.srcWin) : r.panel)
                                         << " cannot be in a floating window";
            return;
        case DropAction::RedockSame:
            // 같은 창 안의 재도킹(M3b: 떠 있는 창은 그 창 트리).
            if (srcValid() && dockOwns(src->get(), r.targetLeaf) &&
                dockRedock(*src, r.srcStack, r.srcIndex, r.panel, r.targetLeaf, r.zone)) {
                rebuildArea();
                saveLayout();
            }
            return;
        case DropAction::RedockAcross: {
            std::unique_ptr<DockNode>* dst = treeOf(r.targetWin);
            bool emptied = false;
            if (!srcValid() || !dst || !*dst || !dockOwns(dst->get(), r.targetLeaf)) return;
            if (!dockRedockAcross(*src, r.srcWin == 0, r.srcStack, r.srcIndex, r.panel, *dst, r.targetWin == 0, r.targetLeaf,
                                  r.zone, q.floatable, emptied)) {
                if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Tab drop refused: cannot move " << r.panel << " to window " << r.targetWin;
                return;
            }
            if (deps_.logCat)
                HLOG(*deps_.logCat, Log) << "Tab " << r.panel << " moved from window " << r.srcWin << " to window " << r.targetWin;
            finish(r.targetWin);
            return;
        }
        case DropAction::NewFloat: {
            if (!srcValid() || !r.haveScreen) return;
            DockFloat out;
            bool emptied = false;
            if (!dockMoveToFloat(*src, r.srcWin == 0, r.srcStack, r.srcIndex, r.panel, q.floatable, panelHome_, out, emptied)) {
                if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Tab drop refused: cannot detach " << r.panel;
                return;
            }
            // 번호는 원래 창이 아직 목록에 있을 때 줍니다(빈 원래 창의 번호를 되쓰지 않게).
            out.id = issueFloatId(floats_, floatIdFloor_);
            ScreenRect rect = newFloatRect(r.screen.x, r.screen.y, r.grab.x, r.grab.y, r.leafSize.x, r.leafSize.y);
            if (deps_.clampFloatRect) deps_.clampFloatRect(rect);
            out.x = rect.x;
            out.y = rect.y;
            out.w = std::clamp(rect.w, kDockFloatMinW, kDockFloatMaxDim);
            out.h = std::clamp(rect.h, kDockFloatMinH, kDockFloatMaxDim);
            if (deps_.logCat)
                HLOG(*deps_.logCat, Log) << "Tab " << r.panel << " detached from window " << r.srcWin << " into new floating window "
                                         << out.id << ": " << out.w << "x" << out.h << " at " << out.x << "," << out.y;
            floats_.push_back(std::move(out));
            finish(-1);   // 새 창은 프레임 끝에 만들어지고 OS 가 앞에 띄웁니다(FrontOrder 도 처음 보는 창을 맨 앞으로)
            return;
        }
        case DropAction::MergeFloat: {
            const int fi = floatIndexOf(r.srcWin);
            std::unique_ptr<DockNode>* dst = treeOf(r.targetWin);
            if (fi < 0 || !dst || !*dst || !dockOwns(dst->get(), r.targetLeaf)) return;
            if (!dockMergeFloatInto(floats_[(size_t)fi], *dst, r.targetWin == 0, r.targetLeaf, r.zone, floatableOf)) {
                if (deps_.logCat)
                    HLOG(*deps_.logCat, Log) << "Tab drop refused: cannot merge window " << r.srcWin << " into window " << r.targetWin;
                return;
            }
            if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Floating window " << r.srcWin << " merged into window " << r.targetWin;
            finish(r.targetWin);   // 비워진 원래 창은 removeEmptyFloats 가 뺌
            return;
        }
    }
}
