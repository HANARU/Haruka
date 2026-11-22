#include "AssetPicker.hpp"
#include "ContentBrowserPanel.hpp"
#include "EditorTheme.hpp"
#include "../core/HText.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPanelW  = 300.f;
constexpr float kRowH    = 22.f;
constexpr float kSearchH = 30.f;
constexpr float kFootH   = 20.f;
constexpr int   kMaxRows = 10;
} // namespace

void AssetPicker::resetPopup() {
    dismissPopup();
    open_ = false;
    panelDirty_ = listDirty_ = false;
    if (deps_.releaseFocusQuiet) deps_.releaseFocusQuiet();
    else                         focused_ = false;
    search_.reset();
    scroll_.reset();
    list_.reset();
    footer_.reset();
}

HSlateContext* AssetPicker::popupContext() const {
    return (ctx_ && handle_ && ctx_->popups().isOpen(handle_)) ? ctx_ : nullptr;
}

void AssetPicker::dismissPopup() {
    ++serial_;
    HSlateContext* c = popupContext();
    const HPopupLayer::Handle h = handle_;
    ctx_ = nullptr;
    handle_ = 0;
    if (c) c->dismissPopup(h);
}

void AssetPicker::toggle(const std::shared_ptr<HWidget>& anchor, const std::string& assetType, const std::string& current,
                         std::function<void(const std::string&)> onPick) {
    if (isOpenFor(anchor.get())) { close(); return; }
    open(anchor, assetType, current, std::move(onPick));
}

void AssetPicker::open(const std::shared_ptr<HWidget>& anchor, const std::string& assetType, const std::string& current,
                       std::function<void(const std::string&)> onPick) {
    if (!anchor) return;
    anchor_    = anchor;
    assetType_ = assetType;
    kinds_     = hasset::kindsOf(assetType);
    current_   = current;
    onPick_    = std::move(onPick);
    edit_.setText("");
    open_ = true;
    panelDirty_ = true;
    refresh(true);
    // 지금 값을 처음 강조합니다(UE 와 같음). 목록에 없으면 첫 행.
    if (const int i = hasset::indexOf(items_, current_); i >= 0) hi_ = i;
    if (deps_.focus) deps_.focus(searchKeep_);
}

void AssetPicker::close() {
    if (!open_) return;
    if (focused_ && deps_.clearFocus) deps_.clearFocus();
    focused_ = false;
    open_ = false;
    panelDirty_ = true;
    onPick_ = {};
    dismissPopup();
}

TextInputField AssetPicker::field() {
    TextInputField f{
        edit_,
        focused_,
        [this](glm::vec2 p) { return open_ && search_ && search_->cachedGeometry().contains(p); },
        [this] { if (edit_.text != lastQuery_) refresh(true); },
    };
    f.onCommit          = [this] { activateHighlighted(); };
    f.keepFocusOnCommit = true;   // 넣으면 close 가 포커스를 뺌
    f.onHistory         = [this](int dir) { moveHighlight(dir); };
    f.onCancel          = [this] { close(); };
    return f;
}

void AssetPicker::refresh(bool resetHighlight) {
    if (!open_) return;
    lastQuery_ = edit_.text;
    const hcb::ContentIndex* idx = deps_.index ? deps_.index() : nullptr;
    items_ = idx ? hasset::buildList(*idx, kinds_, lastQuery_) : std::vector<hcb::Item>{};
    if (resetHighlight || hi_ >= (int)items_.size()) {
        hi_ = items_.empty() ? -1 : 0;
        scrollY_ = 0.f;
        scrollReset_ = true;
    }
    listDirty_ = true;
}

void AssetPicker::moveHighlight(int dir) {
    hi_ = hasset::step((int)items_.size(), hi_, dir);
    listDirty_ = true;
}

void AssetPicker::activate(int idx) {
    if (idx < 0 || idx >= (int)items_.size()) return;
    const std::string path = items_[(size_t)idx].path;
    auto pick = onPick_;
    close();
    if (pick) pick(path);
}

void AssetPicker::flushRebuild() {
    if (panelDirty_ && open_) {
        auto a = anchor_.lock();
        if (!a) { close(); return; }
        if (a->cachedGeometry().size.x <= 0.f) return;   // 칸이 아직 배치 전(디테일을 이번 프레임에 다시 만듦)
    }
    if (panelDirty_) { panelDirty_ = false; listDirty_ = false; rebuildPanel(); return; }
    if (listDirty_)  { listDirty_ = false; rebuildList(); }
}

void AssetPicker::rebuildPanel() {
    auto anchor = anchor_.lock();
    HSlateContext* c = (open_ && anchor && deps_.ui && deps_.popupContext) ? deps_.popupContext() : nullptr;
    if (!c) {
        open_ = false;
        dismissPopup();
        search_.reset();
        scroll_.reset();
        list_.reset();
        footer_.reset();
        return;
    }
    const EditorUiKit& u = *deps_.ui;
    // 폭: 칸 폭과 300 중 큰 쪽(좁은 패널에서도 이름이 보이게).
    const float panelW = std::max(s(kPanelW), std::floor(anchor->cachedGeometry().size.x));
    const float listH  = s(kRowH) * (float)kMaxRows;
    const float panelH = s(kSearchH) + listH + s(kFootH) + s(6.f);

    search_ = HReuse(searchKeep_);
    search_->focusable(true)
        ->style(EditorUiKit::searchBoxStyle(s(1.f)))
        ->placeholder(HTEXT("Editor.Details", "AssetSearchHint", "Search assets...").toString())
        ->edit(&edit_)
        ->focused([this] { return focused_; })
        ->onClick([this] { if (deps_.focus) deps_.focus(searchKeep_); })
        ->onEdited([this] { if (edit_.text != lastQuery_) refresh(true); })
        ->fontSize(s(12.f));

    list_   = HNew(SVerticalBox);
    scroll_ = HNew(SScrollBox);
    scroll_->style(EditorUiKit::scrollBoxStyle(s(1.f)))->content(list_);
    footer_ = HNew(STextBlock);
    footer_->fontSize(s(11.f))->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);

    auto body = HNew(SVerticalBox)
        ->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(4.f), s(3.f) })->content(search_),
                  SVerticalBox::ESizeRule::Fixed, s(kSearchH))
        ->addSlot(scroll_, SVerticalBox::ESizeRule::Fixed, listH)
        ->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(10.f), 0.f })->content(footer_),
                  SVerticalBox::ESizeRule::Fixed, s(kFootH));

    auto panel = HNew(SBox)->widthOverride(panelW)->heightOverride(panelH)->content(
        HNew(SBorder)
            ->background(EditorTheme::bgPopup)
            ->borderColor(EditorTheme::borderPopup)->borderThickness(EditorTheme::borderThin)
            ->cornerRadius(u.s(EditorTheme::radiusPopup))
            ->shadow({ 0.f, u.s(EditorTheme::shadowOffset.y) }, u.s(EditorTheme::shadowBlur), EditorTheme::shadowColor)
            ->padding(HPadding{ 0.f, s(2.f) })
            ->content(body));
    // 자리: 칸 아래 왼쪽 맞춤, 아래에 자리가 없으면 위(콤보와 같은 규칙). 바깥 누름 닫기 · 칸 위 누름 = 토글은 팝업 층이 맡습니다.
    HPopupLayer::Spec spec;
    spec.content = panel;
    spec.anchor  = HPopupRect::of(anchor->cachedGeometry());
    spec.placement.side  = HPopupPlacement::Side::Below;
    spec.placement.align = HPopupPlacement::Align::Start;
    spec.placement.flip  = HPopupPlacement::Flip::Overflow;
    spec.placement.clamp = true;
    spec.placement.gap   = s(2.f);
    spec.size            = { panelW, panelH };
    spec.closeOnLeftOutside = true;
    spec.anchorWidget       = anchor_;
    dismissPopup();
    const uint64_t serial = serial_;
    spec.onDismissed = [this, serial] {
        if (serial != serial_) return;
        ctx_ = nullptr; handle_ = 0;
        close();
    };
    handle_ = c->pushPopup(std::move(spec));
    ctx_    = handle_ ? c : nullptr;
    if (!handle_) { close(); return; }
    rebuildList();
}

void AssetPicker::rebuildList() {
    if (!list_ || !scroll_) return;
    if (scrollReset_) scrollReset_ = false;
    else scrollY_ = scroll_->scrollOffset();
    list_->clearSlots();
    const EditorUiKit& u = *deps_.ui;
    const float rowH = s(kRowH);
    if (footer_)
        footer_->text(HText::format(HTEXT("Editor.Details", "AssetCount", "{0} items"), { std::to_string(items_.size()) }).toString());
    if (items_.empty()) {
        list_->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(10.f), 0.f })
                           ->content(HNew(STextBlock)
                               ->text(HTEXT("Editor.Details", "AssetNoMatch", "No matching asset").toString())
                               ->fontSize(s(12.f))->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                       SVerticalBox::ESizeRule::Fixed, rowH);
        return;
    }
    using HR = SHorizontalBox;
    for (int i = 0; i < (int)items_.size(); ++i) {
        const hcb::Item& it = items_[(size_t)i];
        const bool hi  = (i == hi_);
        const bool cur = (it.path == current_);
        std::string folder = hcb::parentOf(it.path);
        if (folder.compare(0, std::char_traits<char>::length(hcb::kRoot), hcb::kRoot) == 0)
            folder = folder.substr(std::char_traits<char>::length(hcb::kRoot));
        if (folder.empty()) folder = "/";
        auto row = HNew(SHorizontalBox)
            ->addSlot(cur ? u.icon("check", 12.f, EditorTheme::accent, "*", 11.f) : u.spacer(), HR::ESizeRule::Fixed, s(16.f),
                      {}, EVAlign::Center)
            ->addSlot(u.icon(hcbui::kindIcon(it.kind), 13.f, hcbui::kindColor(it.kind), hcbui::kindFallback(it.kind), 10.f),
                      HR::ESizeRule::Fixed, s(18.f), {}, EVAlign::Center)
            ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(4.f))
            ->addSlot(HNew(STextBlock)->text(it.name)->fontSize(s(12.f))->color(EditorUiKit::kTextBody)
                          ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), HR::ESizeRule::Fill)
            ->addSlot(HNew(STextBlock)->text(folder)->fontSize(s(11.f))->color(EditorUiKit::kTextDim)
                          ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center), HR::ESizeRule::Auto);
        auto btn = HNew(SButton)
            ->style(EditorUiKit::buttonStyle())->cornerRadius(0.f)
            ->normalColor (hi ? EditorTheme::selectRow : EditorTheme::transparent)
            ->hoveredColor(hi ? EditorTheme::selectRowHover : EditorTheme::ctlHover)
            ->pressedColor(EditorTheme::accentPressed)
            ->padding(HPadding{ s(6.f), 0.f, s(10.f), 0.f })
            ->hAlign(EHAlign::Fill)
            ->onClick([this, i] { hi_ = i; activate(i); })
            ->content(row);
        btn->tooltip.set(it.path);
        list_->addSlot(btn, SVerticalBox::ESizeRule::Fixed, rowH);
    }
    const float viewH = rowH * (float)kMaxRows;
    if (hi_ >= 0) {
        const float y0 = rowH * (float)hi_, y1 = y0 + rowH;
        if (y0 < scrollY_) scrollY_ = y0;
        if (y1 > scrollY_ + viewH) scrollY_ = y1 - viewH;
    }
    scrollY_ = std::max(0.f, std::min(scrollY_, rowH * (float)items_.size() - viewH));
    scroll_->setScrollOffset(scrollY_);
}
