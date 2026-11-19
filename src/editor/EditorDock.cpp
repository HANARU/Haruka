#include "EditorDock.hpp"
#include "../platform/IWindow.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

void EditorDock::init(Deps deps) {
    deps_ = std::move(deps);
}

std::shared_ptr<SBorder> EditorDock::buildHost() {
    host_ = HNew(SBorder)->background(EditorTheme::transparent);
    return host_;
}

std::shared_ptr<SDockOverlay> EditorDock::resetOverlay() {
    overlay_ = HNew(SDockOverlay);
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
}

void EditorDock::rebuildArea() {
    splitters_.clear();
    tabHits_.clear();
    leafHits_.clear();

    viewportVisible_       = root_ && dockPanelIsActive(root_.get(), "Viewport");
    const bool outlinerVis = root_ && dockPanelIsActive(root_.get(), "Outliner");
    const bool bottomVis   = root_ && dockPanelIsActive(root_.get(), "Bottom");
    const bool detailsVis  = root_ && dockPanelIsActive(root_.get(), "Details");

    if (host_ && root_)
        host_->content(buildNodeWidget(root_.get()));

    if (deps_.onVisibilityChanged)
        deps_.onVisibilityChanged(outlinerVis, bottomVis, detailsVis);
}

std::shared_ptr<HWidget> EditorDock::buildNodeWidget(DockNode* node) {
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
            tabHits_.push_back({ tab, node, i, node->panelIds[i], closeBtn, tabRowArea });
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
        leafHits_.push_back({ leaf, node });
        return leaf;
    }

    const bool vertical = (node->split == DockNode::Split::Vertical);
    if (vertical) {
        auto box = HNew(VR);
        for (size_t i = 0; i < node->children.size(); ++i) {
            const float r = (i < node->ratios.size()) ? node->ratios[i] : 1.f;
            box->addSlot(buildNodeWidget(node->children[i].get()), VR::ESizeRule::Fill, r);
            if (i + 1 < node->children.size()) {
                auto handle = HNew(BR)->background(EditorUiKit::kBorder);
                box->addSlot(handle, VR::ESizeRule::Fixed, s(5.f));
                splitters_.push_back({ handle, box, node, (int)i, true });
            }
        }
        return box;
    }
    auto box = HNew(HR);
    for (size_t i = 0; i < node->children.size(); ++i) {
        const float r = (i < node->ratios.size()) ? node->ratios[i] : 1.f;
        box->addSlot(buildNodeWidget(node->children[i].get()), HR::ESizeRule::Fill, r);
        if (i + 1 < node->children.size()) {
            auto handle = HNew(BR)->background(EditorUiKit::kBorder);
            box->addSlot(handle, HR::ESizeRule::Fixed, s(5.f));
            splitters_.push_back({ handle, box, node, (int)i, false });
        }
    }
    return box;
}

void EditorDock::closePanel(const std::string& id) {
    const PanelDef* def = deps_.panelReg->find(id);
    if (!def) return;
    if (!dockClosePanel(root_, panelHome_, id, def->closable)) return;
    rebuildArea();
    saveLayout();
}

void EditorDock::showPanel(const std::string& id) {
    if (!deps_.panelReg->find(id)) return;
    if (!dockShowPanel(root_, panelHome_, id)) return;
    rebuildArea();
    saveLayout();
}

void EditorDock::saveLayout() const {
    if (!root_ || !deps_.layoutWritePath) return;
    if (deps_.ensureWriteDir) deps_.ensureWriteDir();
    std::ofstream f(deps_.layoutWritePath());
    if (!f) {
        if (deps_.logCat) HLOG(*deps_.logCat, Error) << "Layout save failed";
        return;
    }
    f << dockSerializeLayout(root_.get());
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
    auto pruned = dockParseLayout(ss.str(),
        [this](const std::string& id) { return deps_.panelReg->find(id) != nullptr; });
    if (!pruned) return false;
    root_ = std::move(pruned);
    return true;
}

bool EditorDock::handleSplitterInput(float mx, float my, bool leftDown, bool prevLeftDown,
                                     bool overPopup, IWindow* window) {
    if (!window || window->isCameraModeActive()) return false;

    int cursorShape = 0;
    if (!dragNode_) {
        const float grab = s(4.f);
        for (auto& h : splitters_) {
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
                dragNode_        = h.node;
                dragIndex_       = h.index;
                dragVertical_    = h.vertical;
                dragStartMouse_  = h.vertical ? my : mx;
                dragStartRatioL_ = (h.index < (int)h.node->ratios.size())
                                 ? h.node->ratios[h.index] : 0.5f;
                dragAxisPx_ = 1.f;
                if (h.container) {
                    const HGeometry& cg = h.container->cachedGeometry();
                    dragAxisPx_ = std::max(1.f, h.vertical ? cg.size.y : cg.size.x);
                }
            }
            break;
        }
    } else {
        cursorShape = dragVertical_ ? 2 : 1;
    }
    window->setCursorShape(cursorShape);

    if (dragNode_ && leftDown) {
        const float cur    = dragVertical_ ? my : mx;
        const float dRatio = (cur - dragStartMouse_) / dragAxisPx_;
        auto&     R = dragNode_->ratios;
        const int i = dragIndex_;
        if (i >= 0 && i + 1 < (int)R.size()) {
            const float sum  = R[i] + R[i + 1];
            const float minR = 0.06f;
            if (sum > 2.f * minR) {
                const float newL = std::clamp(dragStartRatioL_ + dRatio, minR, sum - minR);
                if (newL != R[i]) { R[i] = newL; R[i + 1] = sum - newL; rebuildArea(); }
            }
        }
        return true;
    }
    if (!leftDown && dragNode_) {
        dragNode_ = nullptr;
        saveLayout();
        return true;
    }
    return false;
}

bool EditorDock::handleTabInput(float mx, float my, bool leftDown, bool prevLeftDown,
                                bool overPopup, IWindow* window) {
    if (!window || window->isCameraModeActive() || dragNode_) return false;
    const glm::vec2 m{ mx, my };

    if (leftDown && !prevLeftDown && !tabPressPending_ && !tabCloseWidget_ && !overPopup) {
        for (auto& t : tabHits_) {
            if (!t.row || !t.row->cachedGeometry().contains(m)) continue;
            if (t.close && t.close->cachedGeometry().contains(m)) {
                if (deps_.onCancelOutlinerDrag) deps_.onCancelOutlinerDrag();
                tabCloseWidget_ = t.close;
                tabCloseRow_    = t.row;
                tabClosePanel_  = t.panelId;
                break;
            }
            if (t.widget && t.widget->cachedGeometry().contains(m)) {
                if (deps_.onCancelOutlinerDrag) deps_.onCancelOutlinerDrag();
                tabPressPending_ = true; tabDragActive_ = false;
                tabSrcStack_ = t.stack; tabSrcIndex_ = t.tabIndex;
                tabSrcPanel_ = t.panelId; tabPressPos_ = m;
                break;
            }
        }
    }

    if (tabCloseWidget_) {
        if (!leftDown) {
            const bool        fire = tabCloseWidget_->cachedGeometry().contains(m) &&
                                     tabCloseRow_ && tabCloseRow_->cachedGeometry().contains(m);
            const std::string id   = tabClosePanel_;
            tabCloseWidget_.reset();
            tabCloseRow_.reset();
            tabClosePanel_.clear();
            if (fire) closePanel(id);
        }
        return true;
    }

    if (tabPressPending_) {
        if (leftDown) {
            if (!tabDragActive_ && glm::length(m - tabPressPos_) > 6.f)
                tabDragActive_ = true;
            if (tabDragActive_) {
                tabTargetLeaf_ = nullptr; tabTargetZone_ = EDockZone::None;
                if (overlay_) overlay_->active = false;
                for (auto& lf : leafHits_) {
                    if (!lf.widget) continue;
                    const HGeometry& g = lf.widget->cachedGeometry();
                    if (!g.contains(m)) continue;
                    tabTargetLeaf_ = lf.leaf;
                    const float rx = (m.x - g.position.x) / std::max(1.f, g.size.x);
                    const float ry = (m.y - g.position.y) / std::max(1.f, g.size.y);
                    const float edge = 0.28f;
                    HGeometry hi = g;
                    if      (rx < edge)       { tabTargetZone_ = EDockZone::Left;   hi.size.x *= 0.5f; }
                    else if (rx > 1.f - edge) { tabTargetZone_ = EDockZone::Right;  hi.position.x += g.size.x * 0.5f; hi.size.x *= 0.5f; }
                    else if (ry < edge)       { tabTargetZone_ = EDockZone::Top;    hi.size.y *= 0.5f; }
                    else if (ry > 1.f - edge) { tabTargetZone_ = EDockZone::Bottom; hi.position.y += g.size.y * 0.5f; hi.size.y *= 0.5f; }
                    else                      { tabTargetZone_ = EDockZone::Center; }
                    if (overlay_) { overlay_->active = true; overlay_->rect = hi; }
                    break;
                }
                window->setCursorShape(0);
            }
            return true;
        }
        if (overlay_) overlay_->active = false;
        if (tabDragActive_ && tabTargetLeaf_ && tabTargetZone_ != EDockZone::None) {
            if (dockRedock(root_, tabSrcStack_, tabSrcIndex_, tabSrcPanel_, tabTargetLeaf_, tabTargetZone_)) {
                rebuildArea();
                saveLayout();
            }
        } else if (!tabDragActive_ && tabSrcStack_ &&
                   tabSrcIndex_ >= 0 && tabSrcIndex_ < (int)tabSrcStack_->panelIds.size()) {
            tabSrcStack_->activeTab = tabSrcIndex_;
            rebuildArea();
        }
        tabPressPending_ = tabDragActive_ = false;
        tabSrcStack_ = tabTargetLeaf_ = nullptr;
        tabTargetZone_ = EDockZone::None;
        return true;
    }
    return false;
}
