#include "SettingsPanel.hpp"
#include <algorithm>

void SettingsPanel::init(Deps deps, SettingsSource source) {
    deps_   = std::move(deps);
    source_ = std::move(source);
    if (!source_.categories.empty() && selectedCategory_.empty())
        selectedCategory_ = source_.categories.front().id;

    PropertyRowKit::Deps kd;
    kd.ui              = deps_.ui;
    kd.columns         = cols_;
    kd.openCombo       = deps_.openCombo;
    kd.isComboOpen     = deps_.isComboOpen;
    kd.closeMenu       = deps_.closeMenu;
    kd.focusText       = deps_.focusText;
    kd.openAssetPicker = deps_.openAssetPicker;
    kd.browseToAsset   = deps_.browseToAsset;
    kd.onChanged       = [this] { dirty_ = true; };
    rowKit_.init(std::move(kd));
}

std::shared_ptr<HWidget> SettingsPanel::build() {
    auto search = HReuse(searchKeep_);
    search->focusable(true)
        ->style(EditorUiKit::searchBoxStyle(s(1.f)))
        ->placeholder(HTEXT("Editor.Settings", "SearchHint", "Search Settings").toString())
        ->edit(&searchEdit_)
        ->focused([this] { return searchFocused_; })
        ->onClick([this] { if (deps_.focusText) deps_.focusText(searchKeep_); })
        ->onEdited([this] { if (searchEdit_.text != shownSearch_) dirty_ = true; })
        ->fontSize(s(12.f));

    catList_  = HNew(SVerticalBox);
    bodyHost_ = HNew(SVerticalBox);
    scroll_   = HNew(SScrollBox)->style(EditorUiKit::scrollBoxStyle(s(1.f)))->wheelStep(s(48.f))->content(bodyHost_);

    using HR = SHorizontalBox;
    using VR = SVerticalBox;

    auto catScroll = HNew(SScrollBox)->style(EditorUiKit::scrollBoxStyle(s(1.f)))->wheelStep(s(36.f))->content(catList_);
    auto split = HNew(HR)
        ->addSlot(HNew(SBorder)->background(EditorTheme::bgWindow)->content(catScroll), HR::ESizeRule::Fixed, s(210.f))
        ->addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), HR::ESizeRule::Fixed, 1.f)
        ->addSlot(scroll_, HR::ESizeRule::Fill);

    rootBox_ = HNew(VR)
        ->addSlot(HNew(SBorder)->background(EditorTheme::bgHeader)->padding(HPadding{ s(8.f), s(6.f) })->content(search),
                  VR::ESizeRule::Fixed, s(36.f))
        ->addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), VR::ESizeRule::Fixed, 1.f)
        ->addSlot(split, VR::ESizeRule::Fill);

    reloadAndRebuild();
    return HNew(SBorder)->background(EditorUiKit::kBgPanel)->content(rootBox_);
}

TextInputField SettingsPanel::searchField() {
    TextInputField f{
        searchEdit_,
        searchFocused_,
        [this](glm::vec2 p) { return searchKeep_ && searchKeep_->cachedGeometry().contains(p) && searchKeep_->cachedGeometry().size.x > 0.f; },
        [this] { if (searchEdit_.text != shownSearch_) dirty_ = true; },
    };
    f.onCancel = [this] { searchEdit_.setText(""); dirty_ = true; };
    return f;
}

bool SettingsPanel::selectCategory(const std::string& id) {
    if (!findCategory(id)) return false;
    if (selectedCategory_ != id) {
        selectedCategory_ = id;
        if (scroll_) scroll_->scrollToTop();
    }
    if (source_.onReload) source_.onReload();
    dirty_ = true;
    return true;
}

void SettingsPanel::reloadAndRebuild() {
    if (source_.onReload) source_.onReload();
    rebuild();
}

void SettingsPanel::flushRebuild() {
    if (!dirty_) return;
    dirty_ = false;
    rebuild();
}

const SettingsCategoryDef* SettingsPanel::findCategory(const std::string& id) const {
    for (const auto& c : source_.categories)
        if (c.id == id) return &c;
    return nullptr;
}

bool SettingsPanel::scrollContains(const glm::vec2& p) const {
    return !scroll_ || scroll_->cachedGeometry().contains(p);
}

bool SettingsPanel::openRowMenuAt(glm::vec2 p) {
    if (!deps_.openMenu || !scrollContains(p)) return false;
    auto ops = rowKit_.hitTestRow(p);
    if (!ops) return false;
    menuRow_ = std::move(ops);
    deps_.openMenu(p, {
        EditorMenuEntry::cmd("Details.ResetToDefault"),
        EditorMenuEntry::separator(),
        EditorMenuEntry::cmd("Details.CopyValue"),
        EditorMenuEntry::cmd("Details.PasteValue"),
    });
    return true;
}

void SettingsPanel::rebuild() {
    if (!catList_ || !bodyHost_ || !deps_.ui) return;
    dirty_ = false;
    if (deps_.closeMenu) deps_.closeMenu();
    rowKit_.beginRebuild(bodyHost_);
    menuRow_.reset();
    shownSearch_ = searchEdit_.text;
    const bool searching = hdsearch::active(shownSearch_);

    catList_->clearSlots();
    bodyHost_->clearSlots();

    const std::vector<SettingsRowItem> items = source_.buildItems ? source_.buildItems() : std::vector<SettingsRowItem>{};
    const std::vector<std::string> visibleCats = hsettings_ui::filterCategories(source_.categories, items, shownSearch_);
    auto isVisibleCat = [&](const std::string& id) {
        return std::find(visibleCats.begin(), visibleCats.end(), id) != visibleCats.end();
    };

    if (!visibleCats.empty() && !isVisibleCat(selectedCategory_)) {
        selectedCategory_ = visibleCats.front();
        if (scroll_) scroll_->scrollToTop();
    }

    // ── 왼쪽 분류 목록 ──
    using VR = SVerticalBox;
    using HR = SHorizontalBox;
    for (const auto& [grpId, grpLabel] : source_.groups) {
        bool anyInGroup = false;
        for (const auto& c : source_.categories) {
            if (c.group == grpId && isVisibleCat(c.id)) { anyInGroup = true; break; }
        }
        if (!anyInGroup) continue;

        catList_->addSlot(
            HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(10.f), s(6.f), s(8.f), s(2.f) })
                ->content(HNew(STextBlock)->text(grpLabel.toString())->fontSize(s(11.f))
                              ->color(EditorUiKit::kTextDim)->bold()->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
            VR::ESizeRule::Fixed, s(24.f));

        for (const auto& c : source_.categories) {
            if (c.group != grpId || !isVisibleCat(c.id)) continue;
            const bool active = (c.id == selectedCategory_);
            const std::string catId = c.id;
            auto btn = HNew(SButton)
                ->style(EditorUiKit::buttonStyle(s(1.f)))
                ->normalColor(active ? EditorTheme::selectRow : EditorTheme::transparent)
                ->hoveredColor(active ? EditorTheme::selectRow : EditorTheme::ctlNormal)
                ->pressedColor(EditorTheme::accentPressed)
                ->padding(HPadding{ s(18.f), s(4.f), s(8.f), s(4.f) })
                ->hAlign(EHAlign::Fill)
                ->onClick([this, catId] { selectCategory(catId); })
                ->content(HNew(STextBlock)->text(c.title.toString())->fontSize(s(12.f))
                              ->color(EditorUiKit::kTextBody)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center));
            catList_->addSlot(btn, VR::ESizeRule::Fixed, s(26.f));
        }
    }

    // ── 검색 결과 없음 ──
    if (searching && visibleCats.empty()) {
        bodyHost_->addSlot(
            HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(16.f), s(16.f) })
                ->content(HNew(STextBlock)
                              ->text(HText::format(HTEXT("Editor.Settings", "SearchNoMatch", "No settings match '{0}'"),
                                                   { shownSearch_ }).toString())
                              ->fontSize(s(13.f))->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)),
            VR::ESizeRule::Fixed, s(44.f));
        return;
    }

    // ── 다시 시작 띠 (PS-D2, PS1c 단추 자리 포함) ──
    if (source_.hasRestartChanges && source_.hasRestartChanges()) {
        auto banner = HNew(HR);
        banner->addSlot(deps_.ui->icon("alert-triangle", 14.f, EditorTheme::warn, "!", 12.f),
                        HR::ESizeRule::Fixed, s(20.f), {}, EVAlign::Center);
        banner->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(6.f));
        banner->addSlot(HNew(STextBlock)
                            ->text(HTEXT("Editor.Settings", "RestartBanner",
                                         "Restart the editor to apply some changes").toString())
                            ->fontSize(s(12.f))->color(EditorUiKit::kTextBody)
                            ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                        HR::ESizeRule::Fill);
        if (source_.restartBannerAction) {
            if (auto act = source_.restartBannerAction())
                banner->addSlot(std::move(act), HR::ESizeRule::Auto);
        }
        bodyHost_->addSlot(
            HNew(SBorder)->background(EditorTheme::warnBg)->borderColor(EditorTheme::warn)
                ->borderThickness(EditorTheme::borderThin)->padding(HPadding{ s(12.f), s(6.f) })
                ->content(banner),
            VR::ESizeRule::Fixed, s(32.f));
    }

    const SettingsCategoryDef* cat = findCategory(selectedCategory_);
    if (!cat) return;

    // ── 오른쪽 머리 ──
    auto hdr = HNew(VR);
    hdr->addSlot(HNew(STextBlock)->text(cat->title.toString())->fontSize(s(16.f))
                     ->color(EditorUiKit::kTextBody)->bold()->hAlign(EHAlign::Left),
                 VR::ESizeRule::Fixed, s(24.f));
    hdr->addSlot(HNew(STextBlock)->text(cat->description.toString())->fontSize(s(12.f))
                     ->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left),
                 VR::ESizeRule::Fixed, s(18.f));
    if (cat->storagePath) {
        const std::string rel = cat->storagePath();
        if (!rel.empty()) {
            hdr->addSlot(HNew(STextBlock)
                             ->text(HText::format(HTEXT("Editor.Settings", "SavedIn", "Saved in: {0}"), { rel }).toString())
                             ->fontSize(s(11.f))->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left),
                         VR::ESizeRule::Fixed, s(16.f));
        }
    }
    if (cat->statusLine) {
        const std::string st = cat->statusLine();
        if (!st.empty()) {
            hdr->addSlot(HNew(STextBlock)->text(st)->fontSize(s(12.f))
                             ->color(EditorUiKit::kTextBody)->hAlign(EHAlign::Left),
                         VR::ESizeRule::Fixed, s(18.f));
        }
    }
    const float hdrH = s(52.f) + (cat->storagePath && !cat->storagePath().empty() ? s(16.f) : 0.f)
                               + (cat->statusLine  && !cat->statusLine().empty()  ? s(18.f) : 0.f);
    bodyHost_->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(14.f), s(10.f) })->content(hdr),
                       VR::ESizeRule::Fixed, hdrH);
    bodyHost_->addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), VR::ESizeRule::Fixed, 1.f);

    if (cat->customBody) {
        cat->customBody(*bodyHost_, s(1.f));
        return;
    }

    // ── 절(Section)별 행 묶기 ──
    std::vector<std::string> sections;
    for (const auto& it : items) {
        if (it.categoryId != cat->id) continue;
        const std::string shown = it.prop.label ? it.prop.label : "";
        if (!hdsearch::matches(shownSearch_, shown, it.englishName)) continue;
        if (std::find(sections.begin(), sections.end(), it.section) == sections.end())
            sections.push_back(it.section);
    }

    for (const std::string& secName : sections) {
        auto secBody = HNew(VR);
        for (const auto& it : items) {
            if (it.categoryId != cat->id || it.section != secName) continue;
            const std::string shown = it.prop.label ? it.prop.label : "";
            if (!hdsearch::matches(shownSearch_, shown, it.englishName)) continue;

            PropertyRowKit::RowParams rp;
            rp.rowKey      = cat->id + "|" + (it.prop.label ? it.prop.label : "");
            rp.englishName = it.englishName;
            rp.readOnly    = it.readOnly;
            rp.tooltip     = it.tooltip;
            rp.onDragEnd   = it.onDragEnd;
            rp.nameColor   = it.readOnly ? EditorUiKit::kTextDim : EditorUiKit::kTextBody;
            if (it.allowPlatformOverride && !it.platformOverrideActive && it.onAddPlatformOverride) {
                auto addFn = it.onAddPlatformOverride;
                rp.trailing = deps_.ui->textButton(
                    HTEXT("Editor.Settings", "AddAndroid", "+ Android").toString(), 11.f,
                    EditorUiKit::kTextBody, { 6.f, 0.f }, EditorTheme::ctlNormal, EditorTheme::ctlHover,
                    [this, addFn] { addFn(); dirty_ = true; });
            }
            rowKit_.buildRow(it.prop, rp, secBody.get());

            if (it.allowPlatformOverride && it.platformOverrideActive) {
                PropertyRowKit::RowParams ap;
                ap.rowKey      = rp.rowKey + "|android";
                ap.englishName = it.englishName + " Android";
                ap.extraIndent = 18.f;
                ap.onDragEnd   = it.onPlatformDragEnd;
                ap.nameColor   = EditorUiKit::kTextDim;
                if (it.onRemovePlatformOverride) {
                    auto rmFn = it.onRemovePlatformOverride;
                    auto xb = deps_.ui->iconButton("x", 11.f, EditorUiKit::kTextBody, "", 11.f, EditorUiKit::kTextBody, { 4.f, 0.f },
                                                   EditorTheme::ctlNormal, EditorTheme::ctlHover,
                                                   [this, rmFn] { rmFn(); dirty_ = true; }, "x");
                    xb->setTooltip(HTEXT("Editor.Settings", "RemoveAndroid", "Remove Android override"));
                    ap.trailing = xb;
                }
                rowKit_.buildRow(it.platformOverrideProp, ap, secBody.get());
            }
        }

        const bool remembered = sectionExpanded_.count(secName) ? sectionExpanded_[secName] : true;
        auto sec = HNew(SCollapseSection)
            ->title(secName)->style(EditorUiKit::collapseSectionStyle())
            ->expanded(searching ? true : remembered)
            ->onToggle([this, secName, searching](bool ex) {
                if (!searching) sectionExpanded_[secName] = ex;
                dirty_ = true;
            })
            ->body(secBody);
        bodyHost_->addSlot(sec, VR::ESizeRule::Auto);
    }
}
