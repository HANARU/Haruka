#include "EditorMenus.hpp"
#include <algorithm>

void EditorMenus::init(const EditorUiKit* ui, float menuBarH, HSlateContext* ctx, EditorCommandRegistry* reg) {
    ui_       = ui;
    menuBarH_ = menuBarH;
    ctx_      = ctx;
    reg_      = reg;
    // 행을 고르면 메뉴를 닫은 뒤 실행합니다. 층이 드롭다운을 닫으면(배경 누름 · 다른 팝업이 열림) 열림 상태를 맞춥니다.
    view_.init(ui, reg, [this] { setOpen(-1); },
               [this] {
                   if (openMenu_ >= 0) { openMenu_ = -1; barDirty_ = true; popupDirty_ = true; }
               });
}

std::shared_ptr<SHorizontalBox> EditorMenus::buildBar() {
    barBox_ = HNew(SHorizontalBox);
    return barBox_;
}

void EditorMenus::setOpen(int mi) {
    openMenu_   = mi;
    barDirty_   = true;
    popupDirty_ = true;
    highlightFirst_ = false;
    pendingSub_.clear();
    if (mi < 0) view_.close();   // 판정(overPopup)에서 곧바로 빠짐. 예전에도 isOpen() 이 곧바로 거짓이었음
}

void EditorMenus::switchTo(int mi, bool highlightFirst) {
    openMenu_       = mi;
    barDirty_       = true;
    popupDirty_     = true;
    highlightFirst_ = highlightFirst;
    pendingSub_.clear();
}

void EditorMenus::setDefs(std::vector<Def> defs) {
    defs_ = std::move(defs);
}

void EditorMenus::close() {
    if (openMenu_ < 0) return;
    setOpen(-1);
}

bool EditorMenus::openByName(const std::string& path) {
    const size_t slash = path.find('/');
    const std::string name = path.substr(0, slash);
    for (int mi = 0; mi < (int)defs_.size(); ++mi) {
        if (defs_[mi].name == name) {
            switchTo(mi, false);
            if (slash != std::string::npos) pendingSub_ = path.substr(slash + 1);
            return true;
        }
    }
    return false;
}

void EditorMenus::handleHoverSwitch(glm::vec2 mousePos) {
    // 포인터가 움직였을 때만 바꿉니다(H2b). 가만히 있는 포인터가 키보드(← →)로 옮긴 메뉴를 그 단추 메뉴로 되돌리지 않게.
    const bool moved = mousePos != lastBarHover_;
    lastBarHover_ = mousePos;
    if (openMenu_ < 0 || !moved) return;
    for (int mi = 0; mi < (int)buttons_.size(); ++mi) {
        if (mi == openMenu_ || !buttons_[mi]) continue;
        if (buttons_[mi]->cachedGeometry().contains(mousePos)) {
            switchTo(mi, false);
            break;
        }
    }
}

void EditorMenus::handleKey(hmenu::Key k) {
    if (openMenu_ < 0 || defs_.empty()) return;
    const int n = (int)defs_.size();
    const hmenu::Act a = view_.key(k, true);
    switch (a) {
        case hmenu::Act::NextMenu: switchTo((openMenu_ + 1) % n, true); break;
        case hmenu::Act::PrevMenu: switchTo((openMenu_ + n - 1) % n, true); break;
        case hmenu::Act::Close:    setOpen(-1); break;
        default: break;
    }
}

void EditorMenus::flushRebuilds() {
    if (barDirty_)   { barDirty_   = false; rebuildBar();   }
    if (popupDirty_) { popupDirty_ = false; rebuildPopup(); }
}

void EditorMenus::rebuildBar() {
    if (!barBox_) return;
    barBox_->clearSlots();
    buttons_.assign(defs_.size(), nullptr);   // hover 전환 히트테스트용(defs_ 와 1:1)
    barBox_->addSlot(ui_->spacer(), SHorizontalBox::ESizeRule::Fixed, s(6.f));
    float x = s(6.f);
    for (int mi = 0; mi < (int)defs_.size(); ++mi) {
        defs_[mi].x = x;                      // 팝업 위치용 x(스케일 픽셀)
        const bool open = (openMenu_ == mi);
        auto btn = HNew(SButton)
                ->style(EditorUiKit::buttonStyle(s(1.f)))
                ->normalColor (open ? EditorTheme::selectRow      : EditorTheme::transparent)
                ->hoveredColor(open ? EditorTheme::selectRowHover : EditorTheme::ctlNormal)
                ->pressedColor(EditorTheme::ctlHover)
                ->padding(HPadding{s(10.f), s(4.f)})
                ->onClick([this, mi]{ setOpen((openMenu_ == mi) ? -1 : mi); })
                ->content(HNew(STextBlock)->text(defs_[mi].name)->fontSize(s(13.f))
                    ->color(EditorUiKit::kTextBody)->hAlign(EHAlign::Center)->vAlign(EVAlign::Center));
        buttons_[mi] = btn;
        // 폭은 글자 실측에 맞추되 짧은 이름도 같은 최소 폭을 씁니다. 고정 폭이면 "Window" 처럼
        // 긴 이름이 버튼 밖으로 넘칩니다.
        const float w = std::max(s(58.f), btn->computeDesiredSize().x);
        barBox_->addSlot(btn, SHorizontalBox::ESizeRule::Fixed, w);
        x += w;
    }
    barBox_->addSlot(ui_->spacer(), SHorizontalBox::ESizeRule::Fill);
}

void EditorMenus::rebuildPopup() {
    if (!ctx_) return;
    if (openMenu_ < 0 || openMenu_ >= (int)defs_.size()) {
        view_.close();
        return;
    }
    const Def& md = defs_[openMenu_];
    // 자리: 메뉴 단추 아래(왼쪽 맞춤). 창 오른쪽 · 아래 끝에 닿으면 창 안으로 끼웁니다(H2b, 메뉴 줄 아래로). 바깥은 Consume: 메뉴 줄
    // 행(y < menuBarH_)을 뺀 배경이 누름 · 휠을 먹고, 배경에서 누르고 떼면 닫힙니다(예전 전면 투명 캐처 단추).
    EditorMenuView::RootSpec spec;
    spec.anchor = { { md.x, 0.f }, { 0.f, s(menuBarH_) } };
    spec.placement.side     = HPopupPlacement::Side::Below;
    spec.placement.align    = HPopupPlacement::Align::Start;
    spec.placement.flip     = HPopupPlacement::Flip::Overflow;
    spec.placement.clamp    = true;
    spec.placement.insetTop = s(menuBarH_);
    spec.outside            = HPopupLayer::EOutside::Consume;
    spec.closeOnLeft        = false;
    spec.backdropInsetTop   = s(menuBarH_);
    spec.subInsetTop        = s(menuBarH_);
    spec.minWidth           = 200.f;
    const bool first = highlightFirst_;
    highlightFirst_ = false;
    if (!view_.show(ctx_, md.entries, spec, first ? 0 : -1)) return;
    if (!pendingSub_.empty()) {
        const std::string sub = std::move(pendingSub_);
        pendingSub_.clear();
        view_.openSubmenuByLabel(sub);
    }
}
