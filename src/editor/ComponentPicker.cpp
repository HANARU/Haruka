#include "ComponentPicker.hpp"
#include "EditorTheme.hpp"
#include "../core/HText.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPanelW  = 280.f;
constexpr float kRowH    = 22.f;
constexpr float kSearchH = 30.f;
constexpr float kTitleH  = 24.f;
constexpr int   kMaxRows = 12;
} // namespace

void ComponentPicker::resetPopup() {
    dismissPopup();
    open_ = false;
    panelDirty_ = false;
    listDirty_  = false;
    if (deps_.releaseFocusQuiet) deps_.releaseFocusQuiet();
    else                         focused_ = false;
    panel_.reset();
    search_.reset();
    scroll_.reset();
    list_.reset();
    title_.reset();
}

HSlateContext* ComponentPicker::popupContext() const {
    return (ctx_ && handle_ && ctx_->popups().isOpen(handle_)) ? ctx_ : nullptr;
}

void ComponentPicker::dismissPopup() {
    ++serial_;
    HSlateContext* c = popupContext();
    const HPopupLayer::Handle h = handle_;
    ctx_    = nullptr;
    handle_ = 0;
    if (c) c->dismissPopup(h);
}

Actor* ComponentPicker::target() const {
    if (!targetId_ || !deps_.ctx || !deps_.ctx->world) return nullptr;
    return deps_.ctx->world->findById(targetId_);
}

void ComponentPicker::toggle(Actor* target, const std::shared_ptr<HWidget>& anchor) {
    if (open_ && target && target->id == targetId_) { close(); return; }
    open(target, anchor);
}

void ComponentPicker::open(Actor* target, const std::shared_ptr<HWidget>& anchor) {
    if (!target) return;
    targetId_ = target->id;
    anchor_   = anchor;
    category_.clear();
    edit_.setText("");
    scrollY_  = 0.f;
    scrollReset_ = true;
    open_     = true;
    panelDirty_ = true;
    refresh(true);
    if (deps_.setFocus) deps_.setFocus(EFocusTarget::Picker);
}

void ComponentPicker::close() {
    if (!open_) return;
    if (focused_ && deps_.setFocus) deps_.setFocus(EFocusTarget::None);
    focused_ = false;
    open_ = false;
    panelDirty_ = true;
    dismissPopup();
}

void ComponentPicker::tick() {
    if (!open_) return;
    Actor* t = target();
    if (!t || !deps_.ctx || deps_.ctx->selected != t) close();
}

TextInputField ComponentPicker::field() {
    TextInputField f{
        edit_,
        focused_,
        [this](glm::vec2 p) { return open_ && search_ && search_->cachedGeometry().contains(p); },
        // 글이 바뀔 때만 다시 계산합니다. 포커스가 바뀔 때도 불리는데, 그때 목록을 다시 만들면 방금 누른 행 단추가
        // 사라져 그 누름이 클릭이 되지 않습니다(DEF-83 과 같은 꼴).
        [this] { if (edit_.text != lastQuery_) refresh(true); },
    };
    f.onCommit          = [this] { activateHighlighted(); };
    f.keepFocusOnCommit = true;   // 분류에 들어갈 때는 계속 칩니다. 붙이면 close 가 포커스를 뺍니다
    f.onHistory         = [this](int dir) { moveHighlight(dir); };
    f.onCancel          = [this] { close(); };
    f.onBackspaceEmpty  = [this] { back(); };
    return f;
}

void ComponentPicker::refresh(bool resetHighlight) {
    if (!open_) return;
    const std::string query = edit_.text;
    lastQuery_ = query;
    items_ = hcpick::buildItems(ComponentRegistry::get(), target(), query, category_);
    if (resetHighlight || hi_ >= (int)items_.size() || (hi_ >= 0 && !items_[(size_t)hi_].selectable())) {
        hi_ = hcpick::firstSelectable(items_);
        scrollY_ = 0.f;
        scrollReset_ = true;
    }
    listDirty_ = true;
}

void ComponentPicker::moveHighlight(int dir) {
    hi_ = hcpick::stepSelection(items_, hi_, dir);
    listDirty_ = true;
}

void ComponentPicker::activateHighlighted() { activate(hi_); }

void ComponentPicker::activate(int idx) {
    if (idx < 0 || idx >= (int)items_.size()) return;
    const hcpick::Item it = items_[(size_t)idx];
    if (!it.selectable()) return;
    if (it.isCategory) { enterCategory(it.label); return; }
    Actor* t = target();
    close();
    if (t && deps_.onAdd) deps_.onAdd(t, it.entry);
}

void ComponentPicker::enterCategory(const std::string& c) {
    category_ = c;
    edit_.setText("");
    refresh(true);
    if (!focused_ && deps_.setFocus) deps_.setFocus(EFocusTarget::Picker);
}

void ComponentPicker::back() {
    if (category_.empty()) return;
    const std::string was = category_;
    category_.clear();
    refresh(true);
    // 돌아오면 방금 있던 분류에 놓습니다(Unity 와 같음).
    for (int i = 0; i < (int)items_.size(); ++i)
        if (items_[(size_t)i].isCategory && items_[(size_t)i].label == was) hi_ = i;
    if (!focused_ && deps_.setFocus) deps_.setFocus(EFocusTarget::Picker);
}

void ComponentPicker::flushRebuild() {
    // 단추가 아직 배치되지 않았으면(디테일을 이번 프레임에 다시 만듦) 자리를 정할 수 없으니 다음 프레임에 그립니다.
    if (panelDirty_ && open_) {
        auto a = anchor_.lock();
        if ((!a || a->cachedGeometry().size.x <= 0.f) && deps_.anchorNow) {
            if (auto now = deps_.anchorNow()) { a = now; anchor_ = now; }
        }
        if (a && a->cachedGeometry().size.x <= 0.f) return;
    }
    if (panelDirty_) { panelDirty_ = false; listDirty_ = false; rebuildPanel(); return; }
    if (listDirty_)  { listDirty_ = false; rebuildList(); }
}

void ComponentPicker::rebuildPanel() {
    panel_.reset();
    auto anchor = anchor_.lock();
    HSlateContext* c = (open_ && anchor && deps_.ui && deps_.popupContext) ? deps_.popupContext() : nullptr;
    if (!c) {
        open_ = false;
        dismissPopup();
        search_.reset();
        scroll_.reset();
        list_.reset();
        title_.reset();
        return;
    }
    const EditorUiKit& u = *deps_.ui;
    const float panelW = s(kPanelW);
    const float listH  = s(kRowH) * (float)kMaxRows;
    const float panelH = s(kSearchH) + s(kTitleH) + listH + s(6.f);

    search_ = HReuse(searchKeep_);
    search_->focusable(true)
        ->style(EditorUiKit::searchBoxStyle(s(1.f)))
        ->placeholder(HTEXT("Editor.Details", "ComponentSearchHint", "Search components...").toString())
        ->edit(&edit_)
        ->focused([this] { return focused_; })
        ->onClick([this] { if (deps_.setFocus) deps_.setFocus(EFocusTarget::Picker); })
        ->onEdited([this] { if (edit_.text != lastQuery_) refresh(true); })
        ->fontSize(s(12.f));

    // 제목 줄: 맨 위는 "Component", 분류 안은 "<  분류"(누르면 위로), 검색 중은 "Search". 글은 그릴 때마다 지금 상태에서 읽어,
    // 검색 칸을 다시 만들지 않습니다(다시 만들면 한 프레임 동안 칸 자리가 비어 바깥 누름 판정이 틀림).
    title_ = HNew(SBorder)->background(EditorTheme::transparent);
    title_->content(HNew(SButton)
        ->style(EditorUiKit::buttonStyle())->cornerRadius(0.f)
        ->normalColor(EditorTheme::ctlNormal)->hoveredColor(EditorTheme::ctlHover)
        ->pressedColor(EditorTheme::accentPressed)->hAlign(EHAlign::Fill)
        ->onClick([this] { back(); })
        ->content(HNew(STextBlock)
            ->text(AttrStr(std::function<std::string()>([this] {
                if (!hcpick::foldKey(edit_.text).empty())
                    return HTEXT("Editor.Details", "ComponentSearchTitle", "Search").toString();
                if (category_.empty()) return HTEXT("Editor.Details", "ComponentTitle", "Component").toString();
                return "<  " + category_;
            })))
            ->fontSize(s(12.f))->color(EditorUiKit::kTextHdr)->hAlign(EHAlign::Center)->vAlign(EVAlign::Center)));

    list_   = HNew(SVerticalBox);
    scroll_ = HNew(SScrollBox);
    scroll_->style(EditorUiKit::scrollBoxStyle(s(1.f)))->content(list_);

    auto body = HNew(SVerticalBox)
        ->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(4.f), s(3.f) })->content(search_),
                  SVerticalBox::ESizeRule::Fixed, s(kSearchH))
        ->addSlot(title_, SVerticalBox::ESizeRule::Fixed, s(kTitleH))
        ->addSlot(scroll_, SVerticalBox::ESizeRule::Fixed, listH);

    auto panel = HNew(SBox)->widthOverride(panelW)->heightOverride(panelH)->content(
        HNew(SBorder)
            ->background(EditorTheme::bgPopup)
            ->borderColor(EditorTheme::borderPopup)->borderThickness(EditorTheme::borderThin)
            ->cornerRadius(u.s(EditorTheme::radiusPopup))
            ->shadow({ 0.f, u.s(EditorTheme::shadowOffset.y) }, u.s(EditorTheme::shadowBlur), EditorTheme::shadowColor)
            ->padding(HPadding{ 0.f, s(2.f) })
            ->content(body));
    // 자리: 단추 아래, 창 아래에 자리가 없으면 단추 위. 좌우는 단추 가운데에 맞추되 창 안으로(팝업 층 Below · Center · Overflow =
    // 예전 식 그대로). 팝업 아래로 누름 · 휠이 새지 않는 것과 바깥 누름 닫기(단추 위는 바깥이 아님 = 단추가 토글)는 팝업 층이 맡습니다.
    HPopupLayer::Spec spec;
    spec.content = panel;
    spec.anchor  = HPopupRect::of(anchor->cachedGeometry());
    spec.placement.side  = HPopupPlacement::Side::Below;
    spec.placement.align = HPopupPlacement::Align::Center;
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
        close();   // 바깥 누름 · 다른 창에서 열림 · 창 닫힘: 포커스를 놓고 다음 소진 때 위젯 참조를 정리
    };
    handle_ = c->pushPopup(std::move(spec));
    ctx_    = handle_ ? c : nullptr;
    if (!handle_) { close(); return; }
    panel_ = panel;
    rebuildList();
}

void ComponentPicker::rebuildList() {
    if (!list_ || !scroll_) return;
    // 휠로 옮긴 자리를 이어 갑니다(목록을 새로 계산했으면 맨 위에서).
    if (scrollReset_) scrollReset_ = false;
    else scrollY_ = scroll_->scrollOffset();
    list_->clearSlots();
    const float rowH = s(kRowH);
    const bool searching = !hcpick::foldKey(edit_.text).empty();
    if (items_.empty()) {
        list_->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(10.f), 0.f })
                           ->content(HNew(STextBlock)
                               ->text(HTEXT("Editor.Details", "ComponentNoMatch", "No matching component").toString())
                               ->fontSize(s(12.f))->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                       SVerticalBox::ESizeRule::Fixed, rowH);
        return;
    }
    for (int i = 0; i < (int)items_.size(); ++i) {
        const hcpick::Item& it = items_[(size_t)i];
        const bool hi = (i == hi_);
        std::string right;
        if (it.isCategory) right = std::to_string(it.count) + "  >";
        else if (it.present) right = HTEXT("Editor.Details", "ComponentAdded", "added").toString();
        else if (searching) right = it.category;
        const glm::vec4 labelCol = it.selectable() ? EditorUiKit::kTextBody : EditorUiKit::kTextDim;
        auto row = HNew(SHorizontalBox)
            ->addSlot(HNew(STextBlock)->text(it.label)->fontSize(s(13.f))->color(labelCol)
                          ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill)
            ->addSlot(HNew(STextBlock)->text(right)->fontSize(s(11.f))->color(EditorUiKit::kTextDim)
                          ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Auto);
        auto btn = HNew(SButton)
            ->style(EditorUiKit::buttonStyle())->cornerRadius(0.f)
            ->normalColor (hi ? EditorTheme::selectRow : EditorTheme::transparent)
            ->hoveredColor(hi ? EditorTheme::selectRowHover : EditorTheme::ctlHover)
            ->pressedColor(EditorTheme::accentPressed)
            ->padding(HPadding{ s(10.f), 0.f })
            ->hAlign(EHAlign::Fill)
            ->onClick([this, i] { hi_ = i; activate(i); })
            ->content(row);
        btn->enabled.set(it.selectable());
        list_->addSlot(btn, SVerticalBox::ESizeRule::Fixed, rowH);
    }
    // 고른 행이 보이게 스크롤합니다.
    const float viewH = rowH * (float)kMaxRows;
    if (hi_ >= 0) {
        const float y0 = rowH * (float)hi_, y1 = y0 + rowH;
        if (y0 < scrollY_) scrollY_ = y0;
        if (y1 > scrollY_ + viewH) scrollY_ = y1 - viewH;
    }
    scrollY_ = std::max(0.f, std::min(scrollY_, rowH * (float)items_.size() - viewH));
    scroll_->setScrollOffset(scrollY_);
}
