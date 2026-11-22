#include "EditorContextMenu.hpp"
#include "EditorTheme.hpp"

#include <algorithm>

void EditorContextMenu::resetPopup() {
    view_.close();
    open_  = false;
    dirty_ = false;
    entries_.clear();
}

void EditorContextMenu::open(glm::vec2 at, std::vector<EditorMenuEntry> entries, const std::string& panel) {
    entries_   = std::move(entries);
    at_        = at;
    panel_     = panel;
    anchor_.reset();
    anchored_  = false;
    highlight_ = -1;
    open_      = !entries_.empty();
    dirty_     = true;
}

void EditorContextMenu::openBelow(const std::shared_ptr<HWidget>& anchor, std::vector<EditorMenuEntry> entries,
                                  const std::string& panel, int highlightRow) {
    entries_   = std::move(entries);
    at_        = anchor ? anchor->cachedGeometry().position : glm::vec2{ 0.f };
    panel_     = panel;
    anchor_    = anchor;
    anchored_  = true;
    highlight_ = highlightRow;
    open_      = !entries_.empty() && anchor;
    dirty_     = true;
}

void EditorContextMenu::close() {
    if (!open_) return;
    open_  = false;
    dirty_ = true;
    view_.close();   // 판정에서 곧바로 빠짐(예전에는 open_ 이 꺼지면 popupContains 가 거짓)
}

HSlateContext* EditorContextMenu::targetContext() const {
    if (!open_) return nullptr;
    if (HSlateContext* c = view_.context()) return c;
    return popupContext_ ? popupContext_(panel_) : nullptr;
}

void EditorContextMenu::flushRebuild() {
    if (!dirty_) return;
    // 콤보 단추가 아직 배치되지 않았으면(디테일을 이번 프레임에 다시 만듦) 자리를 정할 수 없으니 다음 프레임에 띄웁니다(Add Component 와 같음).
    if (open_ && anchored_)
        if (auto a = anchor_.lock(); a && a->cachedGeometry().size.x <= 0.f) return;
    dirty_ = false;
    rebuild();
}

bool EditorContextMenu::invoke(const std::string& label) {
    // 덤프 · 시험이 열자마자(소진 전) 부를 수 있으므로 항목 목록에서 직접 찾습니다(맨 위 단계, 꺼진 항목 · 소제목은 제외).
    const std::vector<EditorMenuRow> rows = hmenu::resolve(entries_, nullptr);
    for (const EditorMenuRow& r : rows) {
        if (r.label != label || !hmenu::selectable(r) || r.kind != hmenu::Kind::Command || r.entry < 0) continue;
        auto fn = entries_[(size_t)r.entry].action;
        close();
        if (fn) fn();
        return true;
    }
    return false;
}

void EditorContextMenu::handleKey(hmenu::Key k) {
    if (!open_) return;
    if (view_.key(k, false) == hmenu::Act::Close) close();
}

void EditorContextMenu::rebuild() {
    HSlateContext* c = (open_ && ui_ && popupContext_) ? popupContext_(panel_) : nullptr;
    if (!c) {
        open_ = false;
        view_.close();
        return;
    }
    if (anchored_) {
        // 콤보(H3a): 단추 아래, 넘치면 위. 왼쪽 끝을 단추에 맞추고 폭은 단추 폭 이상입니다. 바깥 누름(왼쪽 · 오른쪽)으로 닫고, 단추 위
        // 누름은 바깥이 아닙니다(단추의 onClick 이 닫음). 단추가 사라졌으면(디테일을 다시 만듦) 닫습니다.
        std::shared_ptr<HWidget> a = anchor_.lock();
        if (!a) { open_ = false; view_.close(); return; }
        const HGeometry& g = a->cachedGeometry();
        EditorMenuView::RootSpec spec;
        spec.anchor          = { g.position, g.size };
        spec.placement.side  = HPopupPlacement::Side::Below;
        spec.placement.align = HPopupPlacement::Align::Start;
        spec.placement.flip  = HPopupPlacement::Flip::Overflow;
        spec.placement.clamp = true;
        spec.placement.gap   = ui_->s(1.f);
        spec.closeOnLeft     = true;
        spec.closeOnRight    = true;
        spec.minWidth        = ui_->uiScale > 0.f ? g.size.x / ui_->uiScale : g.size.x;
        spec.anchorWidget    = a;
        if (!view_.show(c, entries_, spec, highlight_)) open_ = false;
        return;
    }
    // 누른 자리의 오른쪽 아래. 창 밖으로 나가면 왼쪽 · 위로 열고 창 안으로 끼웁니다(팝업 층의 Right 자리 = 예전 식 그대로).
    // 팝업 아래로 누름 · 휠이 새지 않는 것은 팝업 층이 맡습니다(예전 같은 크기의 빈 단추). Esc 는 메뉴 키 처리가 한 단계씩 닫습니다.
    EditorMenuView::RootSpec spec;
    spec.anchor          = { at_, { 0.f, 0.f } };
    spec.placement.side  = HPopupPlacement::Side::Right;
    spec.placement.flip  = HPopupPlacement::Flip::Overflow;
    spec.placement.clamp = true;
    spec.closeOnLeft     = true;
    spec.closeOnRight    = true;
    spec.minWidth        = 180.f;
    if (!view_.show(c, entries_, spec, -1)) open_ = false;
}
