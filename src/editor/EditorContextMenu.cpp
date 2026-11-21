#include "EditorContextMenu.hpp"
#include "EditorTheme.hpp"

#include <algorithm>

std::shared_ptr<SBorder> EditorContextMenu::resetPopupHost() {
    host_  = HNew(SBorder)->background(EditorTheme::transparent);
    panel_.reset();
    open_  = false;
    items_.clear();
    return host_;
}

void EditorContextMenu::open(glm::vec2 at, std::vector<Item> items) {
    items_ = std::move(items);
    at_    = at;
    open_  = !items_.empty();
    dirty_ = true;
}

void EditorContextMenu::close() {
    if (!open_) return;
    open_  = false;
    dirty_ = true;
}

bool EditorContextMenu::popupContains(glm::vec2 p) const {
    return open_ && panel_ && panel_->cachedGeometry().contains(p);
}

void EditorContextMenu::handleOutsideClick(glm::vec2 p) {
    if (open_ && !popupContains(p)) close();
}

void EditorContextMenu::flushRebuild() {
    if (!dirty_) return;
    dirty_ = false;
    rebuild();
}

bool EditorContextMenu::invoke(const std::string& label) {
    for (const auto& it : items_) {
        if (it.label != label || !it.enabled) continue;
        auto fn = it.action;
        close();
        if (fn) fn();
        return true;
    }
    return false;
}

void EditorContextMenu::rebuild() {
    if (!host_) return;
    panel_.reset();
    if (!open_ || !ui_) {
        host_->content(nullptr);
        return;
    }
    const EditorUiKit& u = *ui_;
    const float rowH = u.s(24.f);
    const float sepH = u.s(7.f);
    float maxText = 0.f;
    float panelH  = u.s(8.f);
    for (const auto& it : items_) {
        maxText = std::max(maxText, EditorUiKit::textWidth(it.label, u.s(13.f)));
        panelH += rowH + (it.separator ? sepH : 0.f);
    }
    const float panelW = std::max(u.s(180.f), maxText + u.s(40.f));

    auto list = HNew(SVerticalBox);
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        if (it.separator)
            list->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ u.s(8.f), u.s(3.f) })
                              ->content(HNew(SBorder)->background(EditorTheme::lineDivider)),
                          SVerticalBox::ESizeRule::Fixed, sepH);
        const std::string label = it.label;
        auto btn = HNew(SButton)
            ->style(EditorUiKit::buttonStyle())->cornerRadius(0.f)
            ->normalColor(EditorTheme::transparent)->hoveredColor(EditorTheme::selectRow)
            ->pressedColor(EditorTheme::accentPressed)
            ->padding(HPadding{ u.s(14.f), 0.f })->hAlign(EHAlign::Fill)
            ->onClick([this, label] { invoke(label); })
            ->content(HNew(STextBlock)->text(it.label)->fontSize(u.s(13.f))
                          ->color(it.enabled ? EditorUiKit::kTextBody : EditorUiKit::kTextDim)
                          ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center));
        btn->enabled.set(it.enabled);
        list->addSlot(btn, SVerticalBox::ESizeRule::Fixed, rowH);
    }

    auto panel = HNew(SBox)->widthOverride(panelW)->heightOverride(panelH)->content(
        HNew(SBorder)
            ->background(EditorTheme::bgPopup)
            ->borderColor(EditorTheme::borderPopup)->borderThickness(EditorTheme::borderThin)
            ->cornerRadius(u.s(EditorTheme::radiusPopup))
            ->shadow({ 0.f, u.s(EditorTheme::shadowOffset.y) }, u.s(EditorTheme::shadowBlur), EditorTheme::shadowColor)
            ->padding(HPadding{ 0.f, u.s(4.f) })
            ->content(list));
    auto absorber = HNew(SBox)->widthOverride(panelW)->heightOverride(panelH)->content(
        HNew(SBorder)->onWheel([](float) { return true; })->content(
            HNew(SButton)
                ->normalColor(EditorTheme::transparent)->hoveredColor(EditorTheme::transparent)
                ->pressedColor(EditorTheme::transparent)->content(u.spacer())));

    // 누른 자리의 오른쪽 아래. 창 밖으로 나가면 왼쪽 · 위로 엽니다.
    const HGeometry& host = host_->cachedGeometry();
    float left = at_.x, top = at_.y;
    if (left + panelW > host.position.x + host.size.x) left = at_.x - panelW;
    if (top + panelH > host.position.y + host.size.y) top = at_.y - panelH;
    left = std::clamp(left, host.position.x, std::max(host.position.x, host.position.x + host.size.x - panelW));
    top  = std::clamp(top,  host.position.y, std::max(host.position.y, host.position.y + host.size.y - panelH));
    const HPadding pos{ left - host.position.x, top - host.position.y, 0.f, 0.f };
    auto ov = HNew(SOverlay);
    ov->addSlot(absorber, EHAlign::Left, EVAlign::Top, pos);
    ov->addSlot(panel,    EHAlign::Left, EVAlign::Top, pos);
    panel_ = panel;
    host_->content(ov);
}
