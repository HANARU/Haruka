#include "ViewportToolbar.hpp"
#include "../platform/IWindow.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr float kCamSpeeds[]  = { 1.f, 2.f, 5.f, 10.f, 20.f, 40.f };
constexpr float kSnapMove[]   = { 0.05f, 0.1f, 0.25f, 0.5f, 1.f, 5.f };
constexpr float kSnapRotate[] = { 5.f, 10.f, 15.f, 30.f, 45.f, 90.f };
constexpr float kSnapScale[]  = { 0.05f, 0.1f, 0.25f, 0.5f, 1.f };

template <size_t N>
float cycleFrom(const float (&arr)[N], float cur, int dir) {
    int   best  = 0;
    float bestD = std::abs(arr[0] - cur);
    for (int i = 1; i < (int)N; ++i) {
        const float d = std::abs(arr[i] - cur);
        if (d < bestD) { bestD = d; best = i; }
    }
    return arr[((best + dir) % (int)N + (int)N) % (int)N];
}

std::string snapValueLabel(int gizmoMode, const HGizmoSnap& sn) {
    char v[24];
    switch (gizmoMode) {
        case 1:  std::snprintf(v, sizeof v, "%.3f m", sn.move);      break;
        case 2:  std::snprintf(v, sizeof v, "%g deg", sn.rotateDeg); break;
        case 3:  std::snprintf(v, sizeof v, "x%.3f", sn.scale);      break;
        default: std::snprintf(v, sizeof v, "--");                   break;
    }
    char out[32];
    std::snprintf(out, sizeof out, sn.enabled ? "%s" : "(%s)", v);
    return out;
}

std::string camSpeedLabel(float speed, bool withPrefix) {
    char buf[32];
    std::snprintf(buf, sizeof buf, withPrefix ? "Cam %g m/s" : "%g m/s", speed);
    return buf;
}

} // namespace

void ViewportToolbar::init(Deps deps) {
    deps_ = std::move(deps);
}

std::shared_ptr<SBorder> ViewportToolbar::build() {
    box_ = HNew(SHorizontalBox);
    bar_ = HNew(SBorder)
        ->background({0.08f, 0.09f, 0.11f, 0.82f})
        ->borderColor(EditorUiKit::kBorder)->borderThickness(1.f)
        ->padding(HPadding{ s(6.f), s(4.f) })
        ->content(box_);
    rebuild();
    return bar_;
}

void ViewportToolbar::cycleCamSpeed(int dir) const {
    if (deps_.camSpeed)
        *deps_.camSpeed = cycleFrom(kCamSpeeds, *deps_.camSpeed, dir);
}

int ViewportToolbar::computeLevel(float margin) const {
    const float w = deps_.viewportWidth ? deps_.viewportWidth() : 0.f;
    for (int lv = 0; lv < 5; ++lv)
        if (w >= levelNeed_[lv] + margin) return lv;
    return 5;   // 숨김
}

void ViewportToolbar::measureLevels() {
    for (int lv = 0; lv < 5; ++lv) {
        auto box = HNew(SHorizontalBox);
        fill(*box, lv, /*worstCase=*/true);
        auto bar = HNew(SBorder)->padding(HPadding{ s(6.f), s(4.f) })->content(box);
        levelNeed_[lv] = bar->computeDesiredSize().x + 2.f * s(kInset);
    }
    levelNeedScale_ = deps_.ui->uiScale;
    levelNeedFont_  = HFontAtlas::metrics();
}

void ViewportToolbar::tick() {
    if (levelNeedScale_ != deps_.ui->uiScale || levelNeedFont_ != HFontAtlas::metrics())
        measureLevels();
    int lv = computeLevel(s(20.f));
    if (level_ >= 0 && lv < level_) {
        // 더 넓은 구성으로 되돌아갈 때만 여유 폭을 더 요구해, 스플리터를 움직일 때 떨림을 막습니다.
        if (computeLevel(s(40.f)) > lv) lv = std::min(level_, lv + 1);
    }
    if (lv != level_) { level_ = lv; dirty_ = true; }
    flushRebuild();
}

void ViewportToolbar::flushRebuild() {
    if (!dirty_) return;
    dirty_ = false;
    rebuild();
}

void ViewportToolbar::rebuild() {
    if (!box_ || !bar_) return;
    box_->clearSlots();

    const int lv = std::max(0, level_);
    if (lv >= 5) {
        bar_->padding(HPadding{0.f, 0.f})->borderThickness(0.f);
        return;
    }
    bar_->padding(HPadding{s(6.f), s(4.f)})->borderThickness(1.f);
    fill(*box_, lv, /*worstCase=*/false);
}

HViewportRect ViewportToolbar::fbRect() const {
    if (!bar_) return {};
    const HGeometry& g = bar_->cachedGeometry();
    if (g.size.x < 1.f || g.size.y < 1.f) return {};
    return { g.position.x, g.position.y, g.size.x, g.size.y };
}

std::string ViewportToolbar::readoutText() const {
    int   axis = -1;
    float v    = 0.f;
    if (!deps_.dragReadout || !deps_.dragReadout(axis, v)) return {};
    const char* an = (axis >= 0 && axis <= 2) ? (axis == 0 ? "X" : axis == 1 ? "Y" : "Z") : "";
    char buf[64];
    switch (deps_.gizmoMode ? *deps_.gizmoMode : 0) {
        case 1:  std::snprintf(buf, sizeof buf, "Move %s %+.3f m",     an, v); break;
        case 2:  std::snprintf(buf, sizeof buf, "Rotate %s %+.1f deg", an, v); break;
        case 3:  std::snprintf(buf, sizeof buf, "Scale %s x%.3f",      an, v); break;
        default: return {};
    }
    return buf;
}

void ViewportToolbar::fill(SHorizontalBox& box, int lv, bool worstCase) const {
    const float fontPx = s(12.f);
    auto label = [fontPx](const std::string& t) {
        return HNew(STextBlock)->text(t)->fontSize(fontPx)->color(EditorUiKit::kTextBody)
                 ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center);
    };
    auto dynLabel = [&](std::function<std::string()> fn,
                        const std::vector<std::string>& candidates) -> std::shared_ptr<HWidget> {
        if (!worstCase)
            return HNew(STextBlock)->text(AttrStr(std::move(fn)))->fontSize(fontPx)
                     ->color(EditorUiKit::kTextBody)->hAlign(EHAlign::Center)->vAlign(EVAlign::Center);
        std::string widest;
        float       widestW = -1.f;
        for (const auto& c : candidates) {
            const float w = EditorUiKit::textWidth(c, fontPx);
            if (w > widestW) { widestW = w; widest = c; }
        }
        return label(widest);
    };
    auto chip = [&](std::shared_ptr<HWidget> content,
                    std::function<bool()> active, std::function<void()> click) {
        box.addSlot(
            HNew(SToggleButton)->padding(HPadding{s(10.f), s(5.f)})
                ->isActive(std::move(active))->onClick(std::move(click))
                ->content(std::move(content)),
            SHorizontalBox::ESizeRule::Auto);
    };
    auto divider = [&] {
        box.addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(4.f));
        box.addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), SHorizontalBox::ESizeRule::Fixed, 1.f);
        box.addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(4.f));
    };

    // ── 변환 도구 ─────────────────────────────────────────────────────────────
    static const char* kToolLong [4] = { "Select", "Move", "Rotate", "Scale" };
    static const char* kToolShort[4] = { "Sel",    "Mov",  "Rot",    "Scl"   };
    for (int m = 0; m < 4; ++m)
        chip(label(lv <= 0 ? kToolLong[m] : kToolShort[m]),
             [this, m] { return *deps_.gizmoMode == m; },
             [this, m] { *deps_.gizmoMode = m; });

    divider();

    // ── 좌표계 ────────────────────────────────────────────────────────────────
    if (lv <= 1) {
        chip(label("World"), [this] { return *deps_.worldSpace;  }, [this] { *deps_.worldSpace = true;  });
        chip(label("Local"), [this] { return !*deps_.worldSpace; }, [this] { *deps_.worldSpace = false; });
    } else {
        chip(dynLabel([this] { return *deps_.worldSpace ? std::string("World") : std::string("Local"); },
                      { "World", "Local" }),
             [] { return false; }, [this] { *deps_.worldSpace = !*deps_.worldSpace; });
    }

    // ── 스냅 ──────────────────────────────────────────────────────────────────
    if (lv <= 3) {
        divider();
        chip(label("Snap"), [this] { return deps_.snap->enabled; },
             [this] { deps_.snap->enabled = !deps_.snap->enabled; });
        if (lv <= 2) {
            std::vector<std::string> candidates;
            if (worstCase) {
                for (bool on : { true, false }) {
                    HGizmoSnap sn = *deps_.snap;
                    sn.enabled = on;
                    for (int mode = 0; mode <= 3; ++mode) candidates.push_back(snapValueLabel(mode, sn));
                    for (float v : kSnapMove)   { sn.move      = v; candidates.push_back(snapValueLabel(1, sn)); }
                    for (float v : kSnapRotate) { sn.rotateDeg = v; candidates.push_back(snapValueLabel(2, sn)); }
                    for (float v : kSnapScale)  { sn.scale     = v; candidates.push_back(snapValueLabel(3, sn)); }
                }
            }
            chip(dynLabel([this] { return snapValueLabel(*deps_.gizmoMode, *deps_.snap); }, candidates),
                 [this] { return deps_.snap->enabled; },
                 [this] {
                     const int dir = (deps_.ctx->window &&
                                      deps_.ctx->window->isShiftDown()) ? -1 : 1;
                     switch (*deps_.gizmoMode) {
                         case 1: deps_.snap->move      = cycleFrom(kSnapMove,   deps_.snap->move,      dir); break;
                         case 2: deps_.snap->rotateDeg = cycleFrom(kSnapRotate, deps_.snap->rotateDeg, dir); break;
                         case 3: deps_.snap->scale     = cycleFrom(kSnapScale,  deps_.snap->scale,     dir); break;
                         default: break;
                     }
                 });
        }
    }

    divider();

    // ── 카메라 속도 ───────────────────────────────────────────────────────────
    const bool camPrefix = (lv <= 3);
    std::vector<std::string> camCandidates;
    if (worstCase) {
        camCandidates.push_back(camSpeedLabel(*deps_.camSpeed, camPrefix));
        for (float v : kCamSpeeds) camCandidates.push_back(camSpeedLabel(v, camPrefix));
    }
    chip(dynLabel([this, camPrefix] { return camSpeedLabel(*deps_.camSpeed, camPrefix); }, camCandidates),
         [this] { return *deps_.camSpeed != 5.f; },
         [this] {
             const bool back = deps_.ctx->window &&
                               deps_.ctx->window->isShiftDown();
             cycleCamSpeed(back ? -1 : 1);
         });
}
