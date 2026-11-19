#include "EditorChromePanels.hpp"
#include "../renderer/Renderer.hpp"
#include <algorithm>
#include <cstdio>

// ── EditorToolbar ──────────────────────────────────────────────────────────────
void EditorToolbar::init(Deps deps) {
    deps_ = std::move(deps);
}

std::shared_ptr<SBorder> EditorToolbar::build() {
    box_ = HNew(SHorizontalBox);
    auto bar = HNew(SBorder)
        ->background(EditorUiKit::kBgToolbar)
        ->borderColor(EditorUiKit::kBorder)->borderThickness(EditorTheme::borderThin)
        ->padding(HPadding{ EditorTheme::space4, 0.f })
        ->content(box_);
    rebuild();
    return bar;
}

void EditorToolbar::flushRebuild() {
    if (!dirty_) return;
    dirty_ = false;
    rebuild();
}

void EditorToolbar::rebuild() {
    if (!box_) return;
    box_->clearSlots();

    using HR = SHorizontalBox;

    // ── Play / Pause / Stop ────────────────────────────────────────────────────
    const bool playing = deps_.isPlaying && deps_.isPlaying();
    const bool paused  = deps_.isPaused  && deps_.isPaused();
    const bool inGame  = playing || paused;

    // Play · Pause · Stop 은 아이콘만(20px, D3). 아이콘 파일이 없으면 예전 글자를 그립니다.
    auto playBtn = [&](const char* icon, glm::vec4 iconColor, const char* label, glm::vec4 norm, glm::vec4 hov,
                       bool en, const char* tip, std::function<void()> fn) {
        auto b = deps_.ui->iconButton(icon, 20.f, iconColor, "", 13.f, EditorUiKit::kTextBody, {EditorTheme::space12, 6.f},
                                      norm, hov, std::move(fn), label);
        b->enabled.set(en);
        b->tooltip.set(tip);
        box_->addSlot(b, HR::ESizeRule::Auto);
        box_->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(EditorTheme::space2));
    };

    glm::vec4 playNorm  = playing ? EditorTheme::okBg   : EditorTheme::ctlNormal;
    glm::vec4 pauseNorm = paused  ? EditorTheme::warnBg : EditorTheme::ctlNormal;
    glm::vec4 stopNorm  = inGame  ? EditorTheme::errorBg : EditorTheme::ctlNormal;

    playBtn("play",   EditorTheme::ok,    paused ? "Resume" : "Play", playNorm, EditorTheme::okHover,
            true, paused ? "Resume Play" : "Play", deps_.onPlay);
    playBtn("pause",  EditorTheme::warn,  "Pause", pauseNorm, EditorTheme::warnHover,
            inGame, "Pause", deps_.onPause);
    playBtn("square", EditorTheme::error, "Stop",  stopNorm,  EditorTheme::errorHover,
            inGame, "Stop", deps_.onStop);

    box_->addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), HR::ESizeRule::Fixed, 1.f);
    box_->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(4.f));

    // ── Scene buttons ──────────────────────────────────────────────────────────
    auto sceneBtn = [&](const char* icon, const char* label, bool en, const char* tip, std::function<void()> cb) {
        auto b = deps_.ui->iconButton(icon, 16.f, EditorUiKit::kTextDim, label, 13.f, EditorUiKit::kTextBody, {10.f, 6.f},
                                      EditorTheme::ctlNormal, EditorTheme::ctlHover, std::move(cb));
        b->enabled.set(en);
        b->tooltip.set(tip);
        box_->addSlot(b, HR::ESizeRule::Auto);
        box_->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(2.f));
    };
    sceneBtn("save",        "Save",    !inGame, "Save Scene (Ctrl+S)", deps_.onSave);
    sceneBtn("folder-open", "Open",    !inGame, "Open Scene (Ctrl+O)", deps_.onOpen);
    sceneBtn("refresh-cw",  "Refresh", true,    "Refresh Content",     deps_.onRefresh);

    box_->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fill);

    // ── VRAM stats (right-aligned) ────────────────────────────────────────────
    box_->addSlot(
        HNew(STextBlock)
            ->text(AttrStr([this]() -> std::string {
                if (!deps_.ctx || !deps_.ctx->renderer) return "";
                auto st = deps_.ctx->renderer->getAllocStats();
                char buf[128];
                snprintf(buf, sizeof(buf), "VRAM  A: %.1f/%.0fMB  C: %.1f/%.0fMB",
                    st.zoneAUsed / (1024.f * 1024.f),
                    st.zoneATotal / (1024.f * 1024.f),
                    st.zoneCUsed / (1024.f * 1024.f),
                    st.zoneCTotal / (1024.f * 1024.f));
                return buf;
            }))
            ->fontSize(s(12.f))->color(EditorUiKit::kTextDim)
            ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
        HR::ESizeRule::Fixed, s(260.f));

    box_->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(6.f));
}

// ── StatusBarPanel ─────────────────────────────────────────────────────────────
void StatusBarPanel::init(Deps deps) {
    deps_ = std::move(deps);
}

std::shared_ptr<SBorder> StatusBarPanel::build() {
    using HR = SHorizontalBox;
    box_ = HNew(HR);
    box_->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(8.f))
        ->addSlot(HNew(STextBlock)
                      ->text(AttrStr([this]() -> std::string { return msg_; }))
                      ->fontSize(s(11.f))->color(Attr<glm::vec4>([this] { return msgColor_; }))
                      ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                  HR::ESizeRule::Fill, 1.f)
        ->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(10.f))
        ->addSlot(HNew(STextBlock)
                      ->text(AttrStr([this]() -> std::string {
                          return narrow_ >= 2 ? std::string() : "Scene: " + deps_.sceneTitle(); }))
                      ->fontSize(s(11.f))->color(EditorUiKit::kTextDim)
                      ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
                  HR::ESizeRule::Auto)
        ->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(14.f))
        ->addSlot(HNew(STextBlock)
                      ->text(AttrStr([this]() -> std::string { return focusText(); }))
                      ->fontSize(s(11.f))->color(EditorUiKit::kTextDim)
                      ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
                  HR::ESizeRule::Auto)
        ->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(8.f));

    return HNew(SBorder)->background(EditorTheme::bgWindow)
        ->borderColor(EditorUiKit::kBorder)->borderThickness(1.f)
        ->content(box_);
}

void StatusBarPanel::tick(bool logChanged) {
    bool statusDirty = logChanged;
    if (box_) {
        const HGeometry& g = box_->cachedGeometry();
        const glm::ivec2 sig{ (int)(g.size.x / 4.f), (int)(g.size.y / 4.f) };
        if (sig != sizeSig_) {
            sizeSig_ = sig;
            const float w  = g.size.x / std::max(0.01f, deps_.ui->uiScale);
            const int   lv = (w >= 260.f) ? 0 : (w >= 150.f) ? 1 : 2;
            if (lv != narrow_) narrow_ = lv;
            statusDirty = true;
        }
    }
    if (statusDirty) updateMessage();
}

std::string StatusBarPanel::formatLogLine(const HLogRecord& rec) {
    const HLogCategoryState* cat = HLog::categoryAt(rec.category);
    std::string line;
    line.reserve(rec.text.size() + 24);
    line += '[';
    line += cat ? cat->name : std::string("?");
    line += "] ";
    if (rec.verbosity == ELogVerbosity::Error)   line += "Error: ";
    if (rec.verbosity == ELogVerbosity::Warning) line += "Warning: ";
    line += rec.text;
    return EditorUiKit::toDisplayAscii(std::move(line));
}

void StatusBarPanel::updateMessage() {
    HLogRecord rec;
    if (!HLog::latestAtMost(ELogVerbosity::Log, rec)) {
        msg_      = "Ready";
        msgColor_ = EditorUiKit::kTextDim;
        return;
    }
    msgColor_ = EditorUiKit::logColor(rec.verbosity);
    const std::string line = deps_.dumpMode ? std::string("#") : formatLogLine(rec);

    float avail = 0.f;
    if (box_) {
        const HGeometry& g = box_->cachedGeometry();
        avail = g.size.x - s(26.f) - (narrow_ >= 2 ? 0.f : s(80.f));
    }
    msg_ = (avail > s(24.f)) ? deps_.ui->ellipsizeText(line, avail, s(11.f)) : std::string();
}

std::string StatusBarPanel::focusText() const {
    if (narrow_ >= 2 || !deps_.currentFocus) return {};
    switch (deps_.currentFocus()) {
        case EFocusTarget::Rename:      return narrow_ ? "RENAME" : "KBD: Rename";
        case EFocusTarget::Search:      return narrow_ ? "SEARCH" : "KBD: Search";
        case EFocusTarget::NumberField: return narrow_ ? "VALUE"  : "KBD: Value";
        case EFocusTarget::LogSearch:   return narrow_ ? "LOG"    : "KBD: Log Search";
        default:                        return {};
    }
}
