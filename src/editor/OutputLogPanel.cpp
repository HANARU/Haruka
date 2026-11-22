#include "OutputLogPanel.hpp"
#include "../core/Utf8.hpp"
#include "../core/AppPaths.hpp"
#include "../ui/slate/widgets/SBox.hpp"
#include "../ui/slate/widgets/SHorizontalBox.hpp"
#include "../ui/slate/widgets/SOverlay.hpp"
#include "../ui/slate/widgets/STextBlock.hpp"
#include "../ui/slate/widgets/SToggleButton.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace {
constexpr const char* kLogFilterPath = "Config/editor_log.txt";
constexpr ELogVerbosity kLogSeverities[] = {
    ELogVerbosity::Error, ELogVerbosity::Warning, ELogVerbosity::Log, ELogVerbosity::Verbose
};
} // namespace

void OutputLogPanel::init(const EditorUiKit* ui, HLogCategory* logCat, float menuBarH, bool dumpMode,
                          std::function<void()> onSearchClicked) {
    ui_              = ui;
    logCat_          = logCat;
    menuBarH_        = menuBarH;
    dumpMode_        = dumpMode;
    onSearchClicked_ = std::move(onSearchClicked);
    catAnchor_ = HNew(SMenuAnchor)
        ->menuContent([this] { return buildPopupPanel(); })
        ->popupContext([this]() -> HSlateContext* { return popupContextFn_ ? popupContextFn_() : nullptr; })
        ->configure([](HPopupLayer::Spec& sp) { sp.closeOnLeftOutside = true; })   // 단추 위는 바깥이 아님(앵커)
        ->onOpenChanged([this](bool open) { if (!open) popupOpen_ = false; });   // 층이 닫음(바깥 누름 · 다른 창)
}

bool OutputLogPanel::tick(bool tabActive) {
    bool logChanged = false;
    if (const uint64_t seq = HLog::latestSeq(); seq != seenSeq_) {
        seenSeq_   = seq;
        viewDirty_ = true;
        logChanged = true;
    }
    if (tabActive && rowsBox_) {
        const HGeometry& lg = rowsBox_->cachedGeometry();
        const glm::ivec2 sig{ (int)(lg.size.x / 4.f), (int)(lg.size.y / 4.f) };
        if (sig.y != rowsSig_.y)      rebuildRows();
        else if (sig != rowsSig_)   { rowsSig_ = sig; viewDirty_ = true; }
    }
    if (popupOpen_) syncCategories();
    return logChanged;
}

void OutputLogPanel::flushRebuilds(bool tabActive) {
    if (popupDirty_) { popupDirty_ = false; rebuildPopup(); }
    if (viewDirty_ && tabActive && rowsBox_) { viewDirty_ = false; refreshView(); }
}

void OutputLogPanel::resetPopup() {
    popupOpen_  = false;
    popupDirty_ = false;
    if (catAnchor_) catAnchor_->close();
}

void OutputLogPanel::openPopup() {
    popupOpen_  = true;
    popupDirty_ = true;
}

void OutputLogPanel::closePopup() {
    if (!popupOpen_) return;
    popupOpen_  = false;
    popupDirty_ = true;
    if (catAnchor_) catAnchor_->close();   // 판정에서 곧바로 빠짐(예전 popupContains 는 popupOpen_ 을 봄)
}

bool OutputLogPanel::searchContains(glm::vec2 p) const {
    return searchBox_ && searchBox_->cachedGeometry().contains(p);
}

OutputLogPanel::SearchField OutputLogPanel::searchField() {
    return {
        searchEdit_,
        searchFocused_,
        [this](glm::vec2 p) { return searchBox_ && searchBox_->cachedGeometry().contains(p); },
        [this] { viewDirty_ = true; }
    };
}

void OutputLogPanel::setConsole(std::function<std::string(const std::string&)> run, std::function<void()> clicked) {
    consoleRun_     = std::move(run);
    consoleClicked_ = std::move(clicked);
}

TextInputField OutputLogPanel::consoleField() {
    TextInputField f{
        consoleEdit_,
        consoleFocused_,
        [this](glm::vec2 p) { return consoleBox_ && consoleBox_->cachedGeometry().contains(p); },
        [] {},
    };
    f.onCommit          = [this] { submitConsole(); };
    f.keepFocusOnCommit = true;
    f.selectAllOnFocus  = false;   // 이어 쓰기(커서를 끝에)
    f.onHistory         = [this](int dir) { stepHistory(dir); };
    return f;
}

void OutputLogPanel::submitConsole() {
    const std::string line = consoleEdit_.text;
    consoleEdit_.setText("");
    historyPos_ = -1;
    if (line.find_first_not_of(" \t") == std::string::npos) return;
    if (history_.empty() || history_.back() != line) history_.push_back(line);
    if (history_.size() > 64) history_.erase(history_.begin());
    if (consoleRun_) consoleRun_(line);
    // 결과를 보도록 맨 아래로 갑니다.
    anchorSeq_ = 0;
    scrollReq_ = 0;
    viewDirty_ = true;
}

void OutputLogPanel::stepHistory(int dir) {
    if (history_.empty()) return;
    if (historyPos_ < 0) historyPos_ = dir < 0 ? (int)history_.size() - 1 : -1;
    else historyPos_ += dir;
    if (historyPos_ >= (int)history_.size()) historyPos_ = -1;
    if (historyPos_ < -1) historyPos_ = 0;
    consoleEdit_.setText(historyPos_ < 0 ? std::string() : history_[(size_t)historyPos_]);
    consoleEdit_.end(false);
}

void OutputLogPanel::releaseTab(bool tabActive) {
    rowsBox_.reset();
    searchBox_.reset();
    consoleBox_.reset();
    catButton_.reset();
    if (!tabActive) closePopup();
}

void OutputLogPanel::buildTab(SVerticalBox& bottomContent) {
    using HR = SHorizontalBox;
    using VR = SVerticalBox;
    const EditorUiKit& u = *ui_;
    const glm::vec4 btnN = EditorTheme::ctlNormal;
    const glm::vec4 btnH = EditorTheme::ctlHover;

    searchBox_ = HReuse(searchBoxKeep_);
    searchBox_
        ->focusable(true)
        ->style(EditorUiKit::searchBoxStyle(u.s(1.f)))
        ->placeholder(HTEXT("Editor.OutputLog", "SearchHint", "Search log...").toString())
        ->edit(&searchEdit_)
        ->focused([this]() -> bool { return searchFocused_; })
        ->onClick([this] { if (onSearchClicked_) onSearchClicked_(); })
        ->fontSize(u.s(12.f));

    catButton_ = u.textButton("Categories", 11.f, EditorUiKit::kTextBody, HPadding{ 8.f, 3.f }, btnN, btnH,
                              [this] {
                                  popupOpen_ = !popupOpen_; popupDirty_ = true;
                                  if (!popupOpen_ && catAnchor_) catAnchor_->close();
                              });
    catAnchor_->content(catButton_);

    auto verbToggle = [&](const char* label, ELogVerbosity v) {
        const int vi = (int)v;
        glm::vec4 textCol = (v == ELogVerbosity::Log) ? EditorUiKit::kTextBody : EditorUiKit::logColor(v);
        if (v == ELogVerbosity::Verbose) textCol = EditorTheme::textDisabled;
        return HNew(SToggleButton)
            ->style(EditorUiKit::toggleButtonStyle(u.s(1.f)))
            ->isActive([this, vi] { return showVerb_[vi]; })
            ->onClick([this, vi] {
                showVerb_[vi] = !showVerb_[vi];
                viewDirty_    = true;
                saveFilter();
            })
            ->normalColor(btnN)->hoveredColor(btnH)->activeColor(EditorTheme::bgHeader)
            ->padding(HPadding{ u.s(7.f), u.s(2.f) })
            ->content(HNew(STextBlock)->text(label)->fontSize(u.s(11.f))->color(textCol)
                ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center));
    };
    auto counter = [&](bool errors) {
        const ELogVerbosity v = errors ? ELogVerbosity::Error : ELogVerbosity::Warning;
        return HNew(SButton)
            ->style(EditorUiKit::buttonStyle(u.s(1.f)))
            ->normalColor(EditorTheme::transparent)->hoveredColor(btnH)->pressedColor(btnN)
            ->padding(HPadding{ u.s(5.f), u.s(2.f) })
            ->onClick([this, v] {
                for (bool& on : showVerb_) on = false;
                showVerb_[(int)v] = true;
                viewDirty_ = true;
                saveFilter();
            })
            ->content(HNew(STextBlock)
                ->text(AttrStr([this, errors]() -> std::string { return errors ? errText_ : warnText_; }))
                ->fontSize(u.s(11.f))
                ->color(Attr<glm::vec4>([this, errors, v] {
                    return (errors ? hasErr_ : hasWarn_) ? EditorUiKit::logColor(v) : EditorTheme::textDisabled;
                })));
    };
    const HPadding vpad{ 0.f, u.s(3.f) };
    auto filterRow = HNew(HR)
        ->addSlot(u.spacer(),   HR::ESizeRule::Fixed, u.s(4.f))
        ->addSlot(searchBox_,   HR::ESizeRule::Fixed, u.s(200.f), vpad)
        ->addSlot(u.spacer(),   HR::ESizeRule::Fixed, u.s(6.f))
        ->addSlot(catAnchor_,   HR::ESizeRule::Auto, 0.f, vpad)
        ->addSlot(u.spacer(),   HR::ESizeRule::Fixed, u.s(10.f))
        ->addSlot(verbToggle("Err",  ELogVerbosity::Error),   HR::ESizeRule::Auto, 0.f, vpad)
        ->addSlot(verbToggle("Warn", ELogVerbosity::Warning), HR::ESizeRule::Auto, 0.f, vpad)
        ->addSlot(verbToggle("Log",  ELogVerbosity::Log),     HR::ESizeRule::Auto, 0.f, vpad)
        ->addSlot(verbToggle("Verb", ELogVerbosity::Verbose), HR::ESizeRule::Auto, 0.f, vpad)
        ->addSlot(u.spacer(),   HR::ESizeRule::Fill)
        ->addSlot(counter(true),  HR::ESizeRule::Auto, 0.f, vpad)
        ->addSlot(counter(false), HR::ESizeRule::Auto, 0.f, vpad)
        ->addSlot(u.spacer(),   HR::ESizeRule::Fixed, u.s(6.f))
        ->addSlot(u.textButton("Clear", 11.f, EditorUiKit::kTextBody, HPadding{ 8.f, 3.f }, btnN, btnH,
                               [this] {
                                   pullRecords();
                                   clearedSeq_ = mirrorSeq_;
                                   anchorSeq_  = 0;
                                   scrollReq_  = 0;
                                   viewDirty_  = true;
                               }),
                  HR::ESizeRule::Auto, 0.f, vpad)
        ->addSlot(u.spacer(),   HR::ESizeRule::Fixed, u.s(4.f));

    rowsBox_ = HNew(SVerticalBox);
    auto jumpButton = HNew(SButton)
        ->style(EditorUiKit::buttonStyle(u.s(1.f)))
        ->normalColor(EditorTheme::selectRow)->hoveredColor(EditorUiKit::kAccent)
        ->pressedColor(EditorTheme::accentPressed)
        ->padding(HPadding{ u.s(8.f), u.s(2.f) })
        ->onClick([this] {
            anchorSeq_ = 0;
            scrollReq_ = 0;
            viewDirty_ = true;
        })
        ->content(HNew(STextBlock)
            ->text(AttrStr([this]() -> std::string { return jumpText_; }))
            ->fontSize(u.s(11.f))->color(EditorUiKit::kTextHdr)
            ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center));
    jumpButton->visibility.bind([this] {
        return anchorSeq_ != 0 ? EVisibility::Visible : EVisibility::Collapsed;
    });
    auto rowsArea = HNew(SOverlay);
    rowsArea->addSlot(rowsBox_);
    rowsArea->addSlot(jumpButton, EHAlign::Right, EVAlign::Bottom, HPadding{ 0.f, 0.f, u.s(10.f), u.s(4.f) });

    bottomContent.addSlot(HNew(SBorder)->background(EditorUiKit::kBgHeader)->content(filterRow),
                          VR::ESizeRule::Fixed, u.s(28.f));
    bottomContent.addSlot(HNew(SBorder)->background(EditorTheme::transparent)
                              ->padding(HPadding{ u.s(4.f), u.s(2.f) })->content(rowsArea)
                              ->onWheel([this](float d) {
                                  scrollReq_ += (d > 0.f) ? 3 : -3;
                                  viewDirty_  = true;
                                  return true;
                              }),
                          VR::ESizeRule::Fill);
    // 콘솔 줄(D9): 로그 아래에 명령 입력 칸(UE Output Log 의 Cmd 줄과 같은 자리).
    consoleBox_ = HReuse(consoleBoxKeep_);
    consoleBox_
        ->focusable(true)
        ->style(EditorUiKit::searchBoxStyle(u.s(1.f)))
        ->icon(false)
        ->placeholder(HTEXT("Editor.OutputLog", "ConsoleHint", "Enter console command (help)").toString())
        ->edit(&consoleEdit_)
        ->focused([this]() -> bool { return consoleFocused_; })
        ->onClick([this] { if (consoleClicked_) consoleClicked_(); })
        ->fontSize(u.s(12.f));
    auto consoleRow = HNew(HR)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(6.f))
        ->addSlot(HNew(STextBlock)->text(">")->fontSize(u.s(12.f))->color(EditorUiKit::kTextBody)
                      ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center),
                  HR::ESizeRule::Fixed, u.s(12.f))
        ->addSlot(consoleBox_, HR::ESizeRule::Fill, 0.f, vpad)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(4.f));
    bottomContent.addSlot(HNew(SBorder)->background(EditorUiKit::kBgHeader)->content(consoleRow),
                          VR::ESizeRule::Fixed, u.s(26.f));
    rebuildRows();
}

void OutputLogPanel::pullRecords() {
    std::vector<HLogRecord> fresh;
    HLog::copySince(mirrorSeq_, fresh);
    if (fresh.empty()) return;

    for (HLogRecord& r : fresh) {
        if (r.category >= catNames_.size()) syncCategories();
        if (r.category < catCounts_.size()) ++catCounts_[r.category];
        mirror_.push_back(std::move(r));
    }
    while (mirror_.size() > HLog::kCapacity) {
        const uint16_t c = mirror_.front().category;
        if (c < catCounts_.size()) --catCounts_[c];
        mirror_.pop_front();
    }
    mirrorSeq_ = mirror_.back().seq;

    uint32_t errs = 0, warns = 0;
    for (const HLogCategoryState* st : HLog::categories()) {
        errs  += st->errors.load(std::memory_order_relaxed);
        warns += st->warnings.load(std::memory_order_relaxed);
    }
    errText_  = "E " + std::to_string(errs);
    warnText_ = "W " + std::to_string(warns);
    hasErr_   = errs  > 0;
    hasWarn_  = warns > 0;
}

void OutputLogPanel::syncCategories() {
    const std::vector<HLogCategoryState*> cats = HLog::categories();
    if (cats.size() == catNames_.size()) return;
    for (size_t i = catNames_.size(); i < cats.size(); ++i) {
        catNames_.push_back(EditorUiKit::toDisplayAscii(cats[i]->name));
        catHidden_.push_back(hiddenNames_.count(cats[i]->name) ? 1 : 0);
    }
    catCounts_.resize(cats.size(), 0);
    if (popupOpen_) popupDirty_ = true;
}

void OutputLogPanel::setCategoryHidden(uint16_t index, bool hidden) {
    if (index >= catHidden_.size()) return;
    catHidden_[index] = hidden ? 1 : 0;
    if (const HLogCategoryState* st = HLog::categoryAt(index)) {
        if (hidden) hiddenNames_.insert(st->name);
        else        hiddenNames_.erase(st->name);
    }
    viewDirty_ = true;
}

void OutputLogPanel::loadFilter() {
    std::ifstream f(happ::inWriteRoot(kLogFilterPath));
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key, a, b;
        ls >> key >> a >> b;
        ELogVerbosity v = ELogVerbosity::Off;
        if (key == "severity" && parseLogVerbosity(a, v) && v != ELogVerbosity::Off &&
            (b == "on" || b == "off"))
            showVerb_[(int)v] = (b == "on");
        else if (key == "hide" && !a.empty())
            hiddenNames_.insert(a);
    }
}

void OutputLogPanel::saveFilter() const {
    std::error_code ec;
    fs::create_directories(happ::inWriteRoot("Config"), ec);
    std::ofstream f(happ::inWriteRoot(kLogFilterPath), std::ios::trunc);
    if (!f) { if (logCat_) HLOG(*logCat_, Warning) << "Output Log filter save failed (" << kLogFilterPath << ")"; return; }
    f << "# Haruka Output Log display filter v1. Search text is not saved.\n";
    for (ELogVerbosity v : kLogSeverities)
        f << "severity " << logVerbosityName(v) << (showVerb_[(int)v] ? " on\n" : " off\n");
    for (const std::string& name : hiddenNames_) f << "hide " << name << '\n';
}

void OutputLogPanel::saveLevels() const {
    if (!HLog::saveLevelOverrides(happ::inWriteRoot(HLog::kLevelOverridesPath).string()))
        if (logCat_) HLOG(*logCat_, Warning) << "log level save failed (" << HLog::kLevelOverridesPath << ")";
}

bool OutputLogPanel::recordVisible(const HLogRecord& r, const std::string& needleLower) const {
    const int v = (int)r.verbosity;
    if (v <= 0 || v >= 5 || !showVerb_[v]) return false;
    if (r.category < catHidden_.size() && catHidden_[r.category]) return false;
    if (needleLower.empty()) return true;

    // ASCII 만 접는 대소문자 무시(글자 체계 T1-4). 한글 등은 그대로 비교합니다.
    auto icontains = [&](const std::string& hay) { return utf8::containsIgnoreCase(hay, needleLower); };
    if (icontains(r.text)) return true;
    return r.category < catNames_.size() && icontains(catNames_[r.category]);
}

void OutputLogPanel::rebuildRows() {
    if (!rowsBox_) return;
    const EditorUiKit& u = *ui_;
    const HGeometry& g = rowsBox_->cachedGeometry();
    rowsSig_ = { (int)(g.size.x / 4.f), (int)(g.size.y / 4.f) };

    const float rowH = u.s(kRowH);
    const int   cap  = std::max(4, g.size.y > rowH ? (int)(g.size.y / rowH) : 10);
    rowCap_ = cap;

    rowsBox_->clearSlots();
    viewRows_.assign((size_t)cap, ViewRow{});

    using HR = SHorizontalBox;
    using VR = SVerticalBox;
    for (int i = 0; i < cap; ++i) {
        rowsBox_->addSlot(
            HNew(HR)
                ->addSlot(HNew(STextBlock)
                              ->text(AttrStr([this, i]() -> std::string {
                                  return i < (int)viewRows_.size() ? viewRows_[i].time : std::string();
                              }))
                              ->fontSize(u.s(11.f))->color(EditorTheme::textDisabled)
                              ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
                          HR::ESizeRule::Fixed, u.s(kTimeW))
                ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(8.f))
                ->addSlot(HNew(STextBlock)
                              ->text(AttrStr([this, i]() -> std::string {
                                  return i < (int)viewRows_.size() ? viewRows_[i].category : std::string();
                              }))
                              ->fontSize(u.s(11.f))->color(EditorTheme::logCategory)
                              ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                          HR::ESizeRule::Fixed, u.s(kCatW))
                ->addSlot(HNew(STextBlock)
                              ->text(AttrStr([this, i]() -> std::string {
                                  return i < (int)viewRows_.size() ? viewRows_[i].text : std::string();
                              }))
                              ->fontSize(u.s(12.f))
                              ->color(Attr<glm::vec4>([this, i] {
                                  return i < (int)viewRows_.size() ? viewRows_[i].color : EditorTheme::textPrimary;
                              }))
                              ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                          HR::ESizeRule::Fill),
            VR::ESizeRule::Fixed, rowH);
    }
    viewDirty_ = false;
    refreshView();
}

void OutputLogPanel::refreshView() {
    pullRecords();

    const std::string needle = utf8::asciiLower(searchEdit_.text);
    auto shown      = [&](size_t i) { return recordVisible(mirror_[i], needle); };
    auto firstAfter = [&](uint64_t seq) {
        return (size_t)(std::upper_bound(mirror_.begin(), mirror_.end(), seq,
                                         [](uint64_t v, const HLogRecord& r) { return v < r.seq; })
                        - mirror_.begin());
    };

    const size_t n   = mirror_.size();
    const size_t lo  = firstAfter(clearedSeq_);
    const int    cap = std::max(1, rowCap_);
    size_t end = anchorSeq_ ? std::max(lo, firstAfter(anchorSeq_)) : n;

    for (int k = scrollReq_; k > 0 && end > lo; ) { --end; if (shown(end)) --k; }
    for (int k = -scrollReq_; k > 0 && end < n; ++end) if (shown(end)) --k;
    scrollReq_ = 0;

    std::vector<size_t> picked;
    picked.reserve((size_t)cap);
    for (size_t i = end; i > lo && (int)picked.size() < cap; ) { --i; if (shown(i)) picked.push_back(i); }
    std::reverse(picked.begin(), picked.end());
    for (; end < n && (int)picked.size() < cap; ++end) if (shown(end)) picked.push_back(end);

    if (!anchorSeq_) pinSeq_ = mirrorSeq_;
    size_t below = 0, fresh = 0;
    for (size_t i = n; i > end; ) {
        --i;
        if (!shown(i)) continue;
        ++below;
        if (mirror_[i].seq > pinSeq_) ++fresh;
    }
    if (below == 0 || picked.empty()) {
        anchorSeq_ = 0;
        pinSeq_    = 0;
        jumpText_.clear();
    } else {
        anchorSeq_ = mirror_[picked.back()].seq;
        jumpText_  = fresh == 0 ? std::string("v Latest")
                   : fresh == 1 ? std::string("v 1 new line")
                                : "v " + std::to_string(fresh) + " new lines";
    }

    const EditorUiKit& u = *ui_;
    const HGeometry& g   = rowsBox_ ? rowsBox_->cachedGeometry() : HGeometry{};
    const float      msgW = g.size.x - u.s(kTimeW) - u.s(8.f) - u.s(kCatW) - u.s(4.f);

    viewRows_.resize(picked.size());
    for (size_t i = 0; i < picked.size(); ++i) {
        const HLogRecord& r   = mirror_[picked[i]];
        ViewRow&          row = viewRows_[i];

        if (dumpMode_) {
            row.time = "#.##";
        } else {
            char tb[32];
            std::snprintf(tb, sizeof tb, "%.2f", (double)r.time);
            row.time = tb;
        }

        const std::string& cat = r.category < catNames_.size() ? catNames_[r.category]
                                                               : std::string("?");
        row.category = u.ellipsizeText(cat, u.s(kCatW) - u.s(4.f), u.s(11.f));

        std::string text;
        text.reserve(r.text.size() + 10);
        if (r.verbosity == ELogVerbosity::Error)   text += "Error: ";
        if (r.verbosity == ELogVerbosity::Warning) text += "Warning: ";
        if (dumpMode_) {
            text += "#";
        } else {
            text += r.text;
            text = EditorUiKit::toDisplayAscii(std::move(text));
        }
        row.text  = (msgW > u.s(24.f)) ? u.ellipsizeText(text, msgW, u.s(12.f)) : std::string();
        row.color = EditorUiKit::logColor(r.verbosity);
    }
}

void OutputLogPanel::addPopupCategoryRow(SVerticalBox& list, HLogCategoryState* st,
                                         float rowH, const glm::vec4& btnN, const glm::vec4& btnH) {
    using HR = SHorizontalBox;
    using VR = SVerticalBox;
    const EditorUiKit& u = *ui_;
    const HPadding vpad{ 0.f, u.s(2.f) };
    const uint16_t idx = st->index;
    list.addSlot(HNew(HR)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(8.f))
        ->addSlot(HNew(SToggleButton)
                      ->style(EditorUiKit::toggleButtonStyle(u.s(1.f)))
                      ->isActive([this, idx] { return !(idx < catHidden_.size() && catHidden_[idx]); })
                      ->onClick([this, idx] {
                          if (idx >= catHidden_.size()) return;
                          setCategoryHidden(idx, !catHidden_[idx]);
                          saveFilter();
                      })
                      ->normalColor(btnN)->hoveredColor(btnH)->activeColor(EditorTheme::selectRow)
                      ->padding(HPadding{ u.s(6.f), u.s(1.f) })
                      ->content(HNew(STextBlock)
                          ->text(AttrStr([this, idx]() -> std::string {
                              return (idx < catHidden_.size() && catHidden_[idx]) ? "off" : "on";
                          }))
                          ->fontSize(u.s(10.f))->color(EditorUiKit::kTextBody)
                          ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center)),
                  HR::ESizeRule::Fixed, u.s(36.f), vpad)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(8.f))
        ->addSlot(HNew(STextBlock)->text(EditorUiKit::toDisplayAscii(st->name))->fontSize(u.s(12.f))->color(EditorUiKit::kTextBody)
                      ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                  HR::ESizeRule::Fill)
        ->addSlot(HNew(STextBlock)
                      ->text(AttrStr([this, idx]() -> std::string {
                          return idx < catCounts_.size() ? std::to_string(catCounts_[idx]) : "0";
                      }))
                      ->fontSize(u.s(11.f))->color(EditorUiKit::kTextDim)
                      ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
                  HR::ESizeRule::Fixed, u.s(50.f))
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(12.f))
        ->addSlot(HNew(SButton)
                      ->style(EditorUiKit::buttonStyle(u.s(1.f)))
                      ->normalColor(btnN)->hoveredColor(btnH)->pressedColor(EditorTheme::ctlPressed)
                      ->padding(HPadding{ u.s(6.f), u.s(1.f) })->hAlign(EHAlign::Left)
                      ->onClick([this, st] {
                          const uint8_t next = (uint8_t)((st->level.load(std::memory_order_relaxed) + 1) % 5);
                          st->level.store(next, std::memory_order_relaxed);
                          saveLevels();
                      })
                      ->content(HNew(STextBlock)
                          ->text(AttrStr([st]() -> std::string { return logVerbosityName(st->currentLevel()); }))
                          ->fontSize(u.s(11.f))
                          ->color(Attr<glm::vec4>([st] {
                              return st->currentLevel() != st->defaultLevel
                                  ? EditorTheme::accentHover : EditorUiKit::kTextBody;
                          }))
                          ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                  HR::ESizeRule::Fixed, u.s(80.f), vpad)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(8.f)),
        VR::ESizeRule::Fixed, rowH);
}

std::shared_ptr<SVerticalBox> OutputLogPanel::buildPopupList(const std::vector<HLogCategoryState*>& cats) {
    using HR = SHorizontalBox;
    using VR = SVerticalBox;
    const EditorUiKit& u = *ui_;
    const glm::vec4 btnN = EditorTheme::ctlNormal;
    const glm::vec4 btnH = EditorTheme::ctlHover;
    const float rowH = u.s(22.f);

    auto colText = [&](const char* label, EHAlign ha) {
        return HNew(STextBlock)->text(label)->fontSize(u.s(10.f))->color(EditorUiKit::kTextDim)
            ->hAlign(ha)->vAlign(EVAlign::Center);
    };

    auto list = HNew(VR);
    list->addSlot(HNew(HR)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(6.f))
        ->addSlot(u.textButton("All", 11.f, EditorUiKit::kTextBody, HPadding{ 8.f, 2.f }, btnN, btnH, [this] {
                      std::fill(catHidden_.begin(), catHidden_.end(), (char)0);
                      hiddenNames_.clear();
                      viewDirty_ = true;
                      saveFilter();
                  }), HR::ESizeRule::Auto, 0.f, HPadding{ 0.f, u.s(4.f) })
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(4.f))
        ->addSlot(u.textButton("None", 11.f, EditorUiKit::kTextBody, HPadding{ 8.f, 2.f }, btnN, btnH, [this] {
                      for (uint16_t i = 0; i < (uint16_t)catHidden_.size(); ++i) setCategoryHidden(i, true);
                      saveFilter();
                  }), HR::ESizeRule::Auto, 0.f, HPadding{ 0.f, u.s(4.f) })
        ->addSlot(u.spacer(), HR::ESizeRule::Fill)
        ->addSlot(u.textButton("Reset levels", 11.f, EditorUiKit::kTextBody, HPadding{ 8.f, 2.f }, btnN, btnH, [this] {
                      HLog::resetLevels();
                      saveLevels();
                  }), HR::ESizeRule::Auto, 0.f, HPadding{ 0.f, u.s(4.f) })
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(6.f)),
        VR::ESizeRule::Fixed, u.s(28.f));
    list->addSlot(HNew(HR)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(8.f))
        ->addSlot(colText("Show", EHAlign::Left),     HR::ESizeRule::Fixed, u.s(44.f))
        ->addSlot(colText("Category", EHAlign::Left), HR::ESizeRule::Fill)
        ->addSlot(colText("Count", EHAlign::Right),   HR::ESizeRule::Fixed, u.s(50.f))
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(12.f))
        ->addSlot(colText("Capture", EHAlign::Left),  HR::ESizeRule::Fixed, u.s(80.f))
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, u.s(8.f)),
        VR::ESizeRule::Fixed, u.s(20.f));

    for (HLogCategoryState* st : cats)
        addPopupCategoryRow(*list, st, rowH, btnN, btnH);
    return list;
}

void OutputLogPanel::rebuildPopup() {
    if (!popupOpen_ || !catButton_ || !catAnchor_) {
        popupOpen_ = false;
        if (catAnchor_) catAnchor_->close();
        return;
    }
    syncCategories();
    // 자리: 단추가 창 아래 절반이면 위로, 아니면 아래로 열고 메뉴 줄 아래 · 창 안으로 끼웁니다(예전 positionPopupPanel 식 그대로).
    const EditorUiKit& u = *ui_;
    HPopupPlacement pl;
    pl.side     = HPopupPlacement::Side::Below;
    pl.align    = HPopupPlacement::Align::Start;
    pl.flip     = HPopupPlacement::Flip::Half;
    pl.clamp    = true;
    pl.gap      = u.s(2.f);
    pl.insetTop = u.s(menuBarH_);
    catAnchor_->placement(pl);
    if (!catAnchor_->open()) popupOpen_ = false;   // 이미 열려 있으면 내용 · 자리를 다시 만듦
}

std::shared_ptr<HWidget> OutputLogPanel::buildPopupPanel() {
    std::vector<HLogCategoryState*> cats = HLog::categories();
    std::sort(cats.begin(), cats.end(), [](const HLogCategoryState* a, const HLogCategoryState* b) {
        return a->name < b->name;
    });

    const EditorUiKit& u = *ui_;
    const float rowH     = u.s(22.f);
    const float panelW   = u.s(330.f);
    const float headerH  = u.s(28.f) + u.s(20.f);
    const float panelH   = headerH + rowH * (float)cats.size() + u.s(8.f);

    // 팝업 아래로 누름 · 휠이 새지 않는 것은 팝업 층이 맡습니다(예전 같은 크기의 빈 단추).
    return HNew(SBox)->widthOverride(panelW)->heightOverride(panelH)->content(
        HNew(SBorder)
            ->background(EditorTheme::bgPopup)
            ->borderColor(EditorTheme::borderPopup)->borderThickness(EditorTheme::borderThin)
            ->cornerRadius(u.s(EditorTheme::radiusPopup))
            ->shadow({ 0.f, u.s(EditorTheme::shadowOffset.y) }, u.s(EditorTheme::shadowBlur), EditorTheme::shadowColor)
            ->padding(HPadding{ 0.f, u.s(4.f) })
            ->content(buildPopupList(cats)));
}
