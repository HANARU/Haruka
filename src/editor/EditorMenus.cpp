#include "EditorMenus.hpp"
#include <algorithm>

void EditorMenus::init(const EditorUiKit* ui, float menuBarH) {
    ui_       = ui;
    menuBarH_ = menuBarH;
}

std::shared_ptr<SHorizontalBox> EditorMenus::buildBar() {
    barBox_ = HNew(SHorizontalBox);
    return barBox_;
}

std::shared_ptr<SBorder> EditorMenus::resetPopupHost() {
    popupHost_ = HNew(SBorder)->background({0.f, 0.f, 0.f, 0.f});
    return popupHost_;
}

void EditorMenus::setDefs(std::vector<Def> defs) {
    defs_ = std::move(defs);
}

void EditorMenus::close() {
    if (openMenu_ < 0) return;
    openMenu_   = -1;
    barDirty_   = true;
    popupDirty_ = true;
}

bool EditorMenus::openByName(const std::string& name) {
    for (int mi = 0; mi < (int)defs_.size(); ++mi) {
        if (defs_[mi].name == name) {
            openMenu_   = mi;
            barDirty_   = true;
            popupDirty_ = true;
            return true;
        }
    }
    return false;
}

void EditorMenus::handleHoverSwitch(glm::vec2 mousePos) {
    if (openMenu_ < 0) return;
    for (int mi = 0; mi < (int)buttons_.size(); ++mi) {
        if (mi == openMenu_ || !buttons_[mi]) continue;
        if (buttons_[mi]->cachedGeometry().contains(mousePos)) {
            openMenu_   = mi;
            barDirty_   = true;
            popupDirty_ = true;
            break;
        }
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
                ->normalColor (open ? glm::vec4{0.20f, 0.20f, 0.26f, 1.f}
                                    : glm::vec4{0.f,   0.f,   0.f,   0.f})
                ->hoveredColor({0.18f, 0.18f, 0.22f, 1.f})
                ->pressedColor({0.24f, 0.24f, 0.30f, 1.f})
                ->padding(HPadding{s(10.f), s(4.f)})
                ->onClick([this, mi]{
                    openMenu_   = (openMenu_ == mi) ? -1 : mi;
                    barDirty_   = true;
                    popupDirty_ = true;
                })
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
    if (!popupHost_) return;
    if (openMenu_ < 0 || openMenu_ >= (int)defs_.size()) {
        popupHost_->content(nullptr);
        return;
    }
    const Def& md = defs_[openMenu_];

    const float rowH  = s(30.f);
    const float dropW = s(200.f);            // 최소 너비 고정 → Undo/Redo 같은 짧은 항목도 반듯한 박스

    // 체크 상태를 가진 항목이 하나라도 있으면 모든 행 앞에 체크 열을 둡니다(줄 맞춤).
    bool hasChecks = false;
    for (const auto& it : md.items) hasChecks = hasChecks || (bool)it.checked;
    auto rowContent = [&](const Item& it) -> std::shared_ptr<HWidget> {
        auto text = HNew(STextBlock)->text(it.label)->fontSize(s(13.f))
            ->color(EditorUiKit::kTextBody)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);
        if (!hasChecks) return text;
        std::shared_ptr<HWidget> mark = ui_->spacer();
        if (it.checked) {
            const bool on = it.checked();
            mark = HNew(SBox)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)->content(
                HNew(SBox)->widthOverride(s(10.f))->heightOverride(s(10.f))->content(
                    HNew(SBorder)->background(on ? EditorUiKit::kAccent : glm::vec4{ 0.f, 0.f, 0.f, 0.f })
                        ->borderColor({ 0.55f, 0.55f, 0.62f, 1.f })->borderThickness(1.f)));
        }
        return HNew(SHorizontalBox)
            ->addSlot(mark, SHorizontalBox::ESizeRule::Fixed, s(20.f))
            ->addSlot(text, SHorizontalBox::ESizeRule::Fill);
    };

    auto items = HNew(SVerticalBox);
    for (int k = 0; k < (int)md.items.size(); ++k) {
        items->addSlot(
            HNew(SButton)
                ->normalColor ({0.11f, 0.11f, 0.14f, 1.f})
                ->hoveredColor({0.20f, 0.42f, 0.68f, 1.f})
                ->pressedColor({0.14f, 0.34f, 0.56f, 1.f})
                ->padding(HPadding{s(18.f), s(7.f)})
                ->hAlign(EHAlign::Fill)
                ->onClick([this, k]{
                    if (openMenu_ < 0 || openMenu_ >= (int)defs_.size()) return;
                    if (k < 0 || k >= (int)defs_[openMenu_].items.size()) return;
                    auto act = defs_[openMenu_].items[k].action;
                    openMenu_   = -1;
                    barDirty_   = true;
                    popupDirty_ = true;
                    if (act) act();
                })
                ->content(rowContent(md.items[k])),
            SVerticalBox::ESizeRule::Fixed, rowH);
    }

    auto dropdown = HNew(SBox)->widthOverride(dropW)->content(
        HNew(SBorder)
            ->background({0.055f, 0.055f, 0.07f, 1.f})
            ->borderColor({0.34f, 0.34f, 0.40f, 1.f})->borderThickness(1.f)
            ->content(items));
    auto shadow = HNew(SBox)->widthOverride(dropW)
        ->heightOverride((float)md.items.size() * rowH)
        ->content(HNew(SBorder)->background({0.f, 0.f, 0.f, 0.35f}));

    // 캐처(전면 투명 버튼)로 바깥 클릭을 받아 메뉴를 닫습니다. 메뉴 바 행(y < menuBarH_)은 덮지 않습니다.
    auto catcher = HNew(SVerticalBox)
        ->addSlot(ui_->spacer(), SVerticalBox::ESizeRule::Fixed, s(menuBarH_))
        ->addSlot(
            HNew(SBorder)->onWheel([](float) { return true; })->content(
                HNew(SButton)->normalColor({0,0,0,0})->hoveredColor({0,0,0,0})->pressedColor({0,0,0,0})
                    ->onClick([this]{ openMenu_ = -1; barDirty_ = popupDirty_ = true; })
                    ->content(ui_->spacer())),
            SVerticalBox::ESizeRule::Fill);

    auto inner = HNew(SOverlay);
    inner->addSlot(catcher);
    inner->addSlot(shadow,   EHAlign::Left, EVAlign::Top, HPadding{md.x + s(3.f), s(menuBarH_) + s(3.f)});
    inner->addSlot(dropdown, EHAlign::Left, EVAlign::Top, HPadding{md.x,          s(menuBarH_)});
    popupHost_->content(inner);
}
