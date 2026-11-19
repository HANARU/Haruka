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
        ->borderColor(EditorUiKit::kBorder)->borderThickness(1.f)
        ->padding(HPadding{ 4.f, 0.f })
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

    auto playBtn = [&](const char* label, glm::vec4 norm, glm::vec4 hov, std::function<void()> fn) {
        box_->addSlot(deps_.ui->textButton(label, 13.f, EditorUiKit::kTextBody, {12.f, 6.f}, norm, hov, std::move(fn)),
                      HR::ESizeRule::Auto);
        box_->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(2.f));
    };

    glm::vec4 playNorm  = playing ? glm::vec4{0.10f, 0.50f, 0.20f, 1.f} : glm::vec4{0.20f, 0.20f, 0.22f, 1.f};
    glm::vec4 pauseNorm = paused  ? glm::vec4{0.68f, 0.50f, 0.08f, 1.f} : glm::vec4{0.20f, 0.20f, 0.22f, 1.f};
    glm::vec4 stopNorm  = inGame  ? glm::vec4{0.50f, 0.10f, 0.10f, 1.f} : glm::vec4{0.14f, 0.14f, 0.16f, 1.f};

    playBtn(paused ? "Resume" : "Play", playNorm, {0.18f, 0.62f, 0.30f, 1.f}, deps_.onPlay);
    playBtn("Pause", pauseNorm, {0.78f, 0.60f, 0.10f, 1.f}, deps_.onPause);
    playBtn("Stop",  stopNorm,  {0.65f, 0.15f, 0.15f, 1.f}, deps_.onStop);

    box_->addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), HR::ESizeRule::Fixed, 1.f);
    box_->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(4.f));

    // ── Scene buttons ──────────────────────────────────────────────────────────
    auto sceneBtn = [&](const char* label, std::function<void()> cb) {
        box_->addSlot(
            deps_.ui->textButton(label, 13.f, EditorUiKit::kTextBody, {10.f, 6.f},
                                 {0.20f, 0.20f, 0.22f, 1.f}, {0.30f, 0.30f, 0.35f, 1.f}, std::move(cb)),
            HR::ESizeRule::Auto);
        box_->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(2.f));
    };
    sceneBtn("Save",    deps_.onSave);
    sceneBtn("Open",    deps_.onOpen);
    sceneBtn("Refresh", deps_.onRefresh);

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

    return HNew(SBorder)->background(EditorUiKit::kBgHeader)
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
