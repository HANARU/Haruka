#include "ColorPickerPopup.hpp"
#include "EditorTheme.hpp"
#include "../core/HText.hpp"

#include <algorithm>
#include <cmath>

namespace {
// 1x 크기
constexpr float kPad     = 8.f;
constexpr float kTitleH  = 20.f;
constexpr float kPlaneW  = 200.f;
constexpr float kBarW    = 18.f;
constexpr float kGap     = 8.f;
constexpr float kColW    = 158.f;
constexpr float kRowH    = 20.f;
constexpr float kRowGap  = 2.f;
constexpr float kSwatchH = 26.f;
constexpr float kLabelH  = 14.f;
constexpr float kBlockGap = 8.f;
constexpr float kButtonsH = 24.f;
constexpr float kButtonW  = 72.f;

// 값이 이만큼 넘게 다르면 바깥(세터의 자르기 · 8비트 저장 · Undo)에서 바뀐 것으로 봅니다(8비트 반 칸보다 큼).
constexpr float kSyncEps = 0.003f;

// 누르면 먼저 onPress(포커스 주기)를 부르고 자식(글 칸)에게 그대로 넘기는 껍데기. 에디터 문맥은 누름으로 포커스를 주지 않으므로
// (setClickToFocus(false)) 검색 칸의 onClick 과 같은 일을 합니다. 자식이 그 자리에 커서를 놓습니다.
class SPressFocus : public HWidget {
public:
    std::shared_ptr<SPressFocus> content(std::shared_ptr<HWidget> w) { child_ = std::move(w); hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPressFocus> onPress(std::function<void()> fn)   { press_ = std::move(fn); return self(); }
    glm::vec2 computeDesiredSize() override { return child_ ? child_->desiredSize() : glm::vec2(0.f); }
    void onArrangeChildren(const HGeometry& a) override { cachedGeo_ = a; if (child_) child_->onArrangeChildren(a); }
    void paint(HPaintContext& ctx, const HGeometry& g) override { if (isVisible() && child_) child_->paint(ctx, child_->cachedGeometry()); (void)g; }
    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button == 0 && e.down && cachedGeo_.contains(e.position) && press_) press_();
        if (child_) if (auto r = child_->onMouseButton(e); r.handled) return r;
        return (e.button == 0 && e.down && cachedGeo_.contains(e.position)) ? HReply::Handled() : HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override { return child_ ? child_->onMouseMove(e) : HReply::Unhandled(); }
    void forEachChild(const std::function<void(const std::shared_ptr<HWidget>&)>& fn) override { if (child_) fn(child_); }
private:
    std::shared_ptr<SPressFocus> self() { return std::static_pointer_cast<SPressFocus>(shared_from_this()); }
    std::shared_ptr<HWidget> child_;
    std::function<void()>    press_;
};

bool differs(const glm::vec4& a, const glm::vec4& b, int n) {
    for (int i = 0; i < n; ++i) if (std::abs(a[i] - b[i]) > kSyncEps) return true;
    return false;
}
} // namespace

HSlateContext* ColorPickerPopup::popupContext() const {
    return (ctx_ && handle_ && ctx_->popups().isOpen(handle_)) ? ctx_ : nullptr;
}

void ColorPickerPopup::dismissPopup() {
    ++serial_;
    HSlateContext* c = popupContext();
    const HPopupLayer::Handle h = handle_;
    ctx_ = nullptr;
    handle_ = 0;
    if (c) c->dismissPopup(h);
}

void ColorPickerPopup::resetPopup() {
    if (open_) finish(true);
    dismissPopup();
    if (deps_.releaseFocusQuiet) deps_.releaseFocusQuiet();
    else                         hexFocused_ = false;
    hexHit_.reset();
}

void ColorPickerPopup::toggle(const std::shared_ptr<HWidget>& anchor, Target target) {
    if (isOpenFor(anchor.get())) { confirm(); return; }
    open(anchor, std::move(target));
}

void ColorPickerPopup::open(const std::shared_ptr<HWidget>& anchor, Target target) {
    if (!anchor || !target.get || !target.set) return;
    if (open_) finish(true);   // 다른 색 칸: 앞의 것은 확정
    target_ = std::move(target);
    anchor_ = anchor;
    state_.open(target_.get(), target_.components, target_.srgb);
    hexEdit_.setText(state_.hex());
    hexEscaped_ = false;
    dragging_ = false;
    open_ = true;
    panelDirty_ = true;
}

void ColorPickerPopup::confirm() { if (open_) finish(true); }
void ColorPickerPopup::cancel()  { if (open_) finish(false); }

void ColorPickerPopup::finish(bool commit) {
    if (!open_) return;
    // 16진수 칸을 치던 중이면 포커스를 먼저 놓습니다(확정이면 친 값이 들어감 · 취소면 버림).
    if (hexFocused_) {
        if (!commit) hexEscaped_ = true;
        if (deps_.clearFocus) deps_.clearFocus();
    }
    open_ = false;
    dragging_ = false;
    Target t = std::move(target_);
    target_ = {};
    dismissPopup();
    if (t.alive && !t.alive()) return;
    if (commit) { if (t.commit) t.commit(); }
    else        { if (t.cancel) t.cancel(); }
}

void ColorPickerPopup::closeQuiet() {
    if (hexFocused_ && deps_.releaseFocusQuiet) deps_.releaseFocusQuiet();
    hexFocused_ = false;
    open_ = false;
    dragging_ = false;
    target_ = {};
    dismissPopup();
}

void ColorPickerPopup::apply() {
    if (!open_ || !target_.set) return;
    target_.set(state_.value);
    // 세터가 잘랐으면(라이트 색 0 ~ 1 · 8비트 저장) 그 값을 따라갑니다. 작은 차이는 무시해 색상 · 채도가 흔들리지 않게 합니다.
    if (target_.get) {
        const glm::vec4 now = target_.get();
        if (differs(now, state_.value, state_.components)) state_.setValue(now);
    }
    if (!hexFocused_) hexEdit_.setText(state_.hex());
}

void ColorPickerPopup::tick(bool escEdge) {
    if (!open_) return;
    if (target_.alive && !target_.alive()) { closeQuiet(); return; }
    // 바깥에서 값이 바뀌면(디테일 성분 칸 · Undo) 따라갑니다. 끄는 중에는 보지 않습니다.
    if (!dragging_ && target_.get) {
        const glm::vec4 now = target_.get();
        if (differs(now, state_.value, state_.components)) {
            state_.setValue(now);
            if (!hexFocused_) hexEdit_.setText(state_.hex());
        }
    }
    if (escEdge) cancel();
}

TextInputField ColorPickerPopup::hexField() {
    TextInputField f{
        hexEdit_,
        hexFocused_,
        [this](glm::vec2 p) { return open_ && hexHit_ && hexHit_->cachedGeometry().contains(p); },
        // 포커스를 잃음: 친 값을 넣습니다(Esc 면 버림). 틀린 모양이면 지금 값으로 되돌림.
        [this] {
            if (hexFocused_) return;
            if (open_ && !hexEscaped_ && hexEdit_.text != state_.hex()) {
                if (state_.setHex(hexEdit_.text)) apply();
            }
            hexEscaped_ = false;
            hexEdit_.setText(state_.hex());
        },
        [this]() -> bool {
            if (!open_) return false;
            hexEscaped_ = false;
            hexEdit_.setText(state_.hex(), true);
            return true;
        },
        // Enter: 넣고 칸을 나감
        [this] {
            if (state_.setHex(hexEdit_.text)) apply();
            hexEdit_.setText(state_.hex());
        }
    };
    f.onEscape = [this] { hexEscaped_ = true; };
    return f;
}

std::shared_ptr<HWidget> ColorPickerPopup::numberRow(const char* label, glm::vec4 bar, std::function<float()> get,
                                                     std::function<void(float)> set, float step, ENumberUnit unit) {
    auto f = HNew(SNumberField)
        ->style(EditorUiKit::numberFieldStyle(s(1.f)))->fontSize(s(12.f))
        ->label(label)->labelColor(EditorUiKit::kTextDim)
        ->get(std::move(get))->set(std::move(set))->step(step)->unit(unit)
        ->onDragBegin([this] { dragging_ = true; })->onDragEnd([this] { dragging_ = false; })
        ->clipText(true);
    if (bar.a > 0.f) f->axisBar(bar, s(3.f));
    return f;
}

void ColorPickerPopup::flushRebuild() {
    if (!panelDirty_) return;
    if (!open_) { panelDirty_ = false; return; }
    auto a = anchor_.lock();
    if (!a) { closeQuiet(); panelDirty_ = false; return; }
    if (a->cachedGeometry().size.x <= 0.f) return;   // 견본이 아직 배치 전
    panelDirty_ = false;
    rebuildPanel();
}

void ColorPickerPopup::rebuildPanel() {
    auto anchor = anchor_.lock();
    HSlateContext* c = (open_ && anchor && deps_.ui && deps_.popupContext) ? deps_.popupContext() : nullptr;
    if (!c) { closeQuiet(); return; }
    const EditorUiKit& u = *deps_.ui;
    using HR = SHorizontalBox;
    using VR = SVerticalBox;
    const bool alpha = state_.hasAlpha();
    const int  nComp = alpha ? 4 : 3;

    // 오른쪽 열 높이 = 견본 묶음 + RGB(A) + HSV + 16진수. 판 · 막대는 이 높이에 맞춥니다.
    auto block = [&](int rows) { return s(kRowH) * (float)rows + s(kRowGap) * (float)(rows - 1); };
    const float colH   = s(kLabelH) + s(kSwatchH) + s(kBlockGap) + block(nComp) + s(kBlockGap) + block(3) + s(kBlockGap) + s(kLabelH) + s(kRowH);
    const float planeW = s(kPlaneW);
    const float leftW  = planeW + s(kGap) + s(kBarW) + (alpha ? s(kGap) + s(kBarW) : 0.f);
    const float panelW = s(kPad) + leftW + s(kGap) + s(4.f) + s(kColW) + s(kPad);
    const float panelH = s(kPad) + s(kTitleH) + s(4.f) + colH + s(kBlockGap) + s(kButtonsH) + s(kPad);
    const glm::vec4 frame = EditorTheme::borderPopup;

    // ── 왼쪽: 판 · 막대 ──
    auto plane = HNew(SColorPlane)
        ->colorAt([this](float pu, float pv) { return state_.planeColor(pu, pv); })
        ->marker([this] { return state_.planeMarker(); })
        ->onBegin([this] { dragging_ = true; })
        ->onEnd([this] { dragging_ = false; })
        ->onChange([this](float pu, float pv) { state_.setPlane(pu, pv); apply(); })
        ->cells(8)->size({ planeW, colH })->border(frame)->markerRadius(s(5.f));
    auto hue = HNew(SColorBar)
        ->colorAt([this](float t) { return state_.hueColor(t); })
        ->marker([this] { return state_.hueMarker(); })
        ->onBegin([this] { dragging_ = true; })
        ->onEnd([this] { dragging_ = false; })
        ->onChange([this](float t) { state_.setHue(t * 360.f); apply(); })
        ->segments(12)->vertical(true)->size({ s(kBarW), colH })->border(frame);
    auto left = HNew(SHorizontalBox)
        ->addSlot(plane, HR::ESizeRule::Fixed, planeW)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(kGap))
        ->addSlot(hue, HR::ESizeRule::Fixed, s(kBarW));
    if (alpha) {
        auto al = HNew(SColorBar)
            ->colorAt([this](float t) { return state_.alphaColor(t); })
            ->marker([this] { return state_.alphaMarker(); })
            ->onBegin([this] { dragging_ = true; })
            ->onEnd([this] { dragging_ = false; })
            ->onChange([this](float t) { state_.setAlpha(1.f - t); apply(); })
            ->segments(1)->vertical(true)->size({ s(kBarW), colH })->border(frame)
            ->checker(std::floor(s(kBarW) * 0.5f), EditorTheme::srgb(0xCCCCCC), EditorTheme::srgb(0x808080));
        left->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(kGap));
        left->addSlot(al, HR::ESizeRule::Fixed, s(kBarW));
    }

    // ── 오른쪽 열 ──
    auto label = [&](const HText& t) {
        return HNew(STextBlock)->text(t.toString())->fontSize(s(11.f))->color(EditorUiKit::kTextDim)
            ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);
    };
    auto swatch = [&](bool old) {
        auto w = HNew(SColorSwatch)
            ->color([this, old] { return old ? state_.displayOriginal() : state_.displayValue(); })
            ->border(frame, EditorTheme::borderThin)->backdrop(EditorTheme::bgInput)
            ->cornerRadius(0.f)->size({ s(kColW) * 0.5f, s(kSwatchH) });
        if (old) {
            w->onClick([this] { state_.setValue(state_.original); apply(); });
            w->setTooltip(HTEXT("Editor.ColorPicker", "Tip.Old", "Original color (click to restore)"));
        } else {
            w->setTooltip(HTEXT("Editor.ColorPicker", "Tip.New", "New color"));
        }
        return w;
    };
    auto labels = HNew(SHorizontalBox)
        ->addSlot(label(HTEXT("Editor.ColorPicker", "Old", "Old")), HR::ESizeRule::Fill)
        ->addSlot(label(HTEXT("Editor.ColorPicker", "New", "New")), HR::ESizeRule::Fill);
    auto swatches = HNew(SHorizontalBox)
        ->addSlot(swatch(true), HR::ESizeRule::Fill)
        ->addSlot(swatch(false), HR::ESizeRule::Fill);

    const ENumberUnit unit = ENumberUnit::Ratio;
    auto col = HNew(SVerticalBox);
    col->addSlot(labels, VR::ESizeRule::Fixed, s(kLabelH));
    col->addSlot(swatches, VR::ESizeRule::Fixed, s(kSwatchH));
    col->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(kBlockGap));
    static const char* const kRgba[4] = { "R", "G", "B", "A" };
    const glm::vec4 bars[4] = { EditorTheme::axisX, EditorTheme::axisY, EditorTheme::axisZ, EditorTheme::textSecondary };
    for (int i = 0; i < nComp; ++i) {
        if (i) col->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(kRowGap));
        col->addSlot(numberRow(kRgba[i], bars[i], [this, i] { return state_.value[i]; },
                               [this, i](float v) { state_.setComponent(i, i == 3 ? std::clamp(v, 0.f, 1.f) : std::max(0.f, v)); apply(); },
                               0.005f, unit),
                     VR::ESizeRule::Fixed, s(kRowH));
    }
    col->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(kBlockGap));
    col->addSlot(numberRow("H", glm::vec4(0.f), [this] { return state_.hsv.x; },
                           [this](float v) { state_.setHue(std::clamp(v, 0.f, 360.f)); apply(); }, 1.f, ENumberUnit::Angle),
                 VR::ESizeRule::Fixed, s(kRowH));
    col->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(kRowGap));
    col->addSlot(numberRow("S", glm::vec4(0.f), [this] { return state_.hsv.y; },
                           [this](float v) { state_.setHsv({ state_.hsv.x, std::clamp(v, 0.f, 1.f), state_.hsv.z }); apply(); }, 0.005f, unit),
                 VR::ESizeRule::Fixed, s(kRowH));
    col->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(kRowGap));
    col->addSlot(numberRow("V", glm::vec4(0.f), [this] { return state_.hsv.z; },
                           [this](float v) { state_.setHsv({ state_.hsv.x, state_.hsv.y, std::max(0.f, v) }); apply(); }, 0.005f, unit),
                 VR::ESizeRule::Fixed, s(kRowH));
    col->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(kBlockGap));
    col->addSlot(label(HTEXT("Editor.ColorPicker", "HexSrgb", "Hex (sRGB)")), VR::ESizeRule::Fixed, s(kLabelH));
    {
        SEditableText::Style es;
        es.text        = EditorUiKit::kTextBody;
        es.composition = EditorUiKit::kTextBody;
        es.selection   = EditorTheme::accent;
        es.caret       = EditorUiKit::kTextBody;
        es.caretWidth  = std::max(1.f, std::floor(s(1.f)));
        auto edit = HReuse(hexKeep_)->focusable(true)->state(&hexEdit_)->focused([this] { return hexFocused_; })
                        ->fontSize(s(12.f))->textHeight(s(16.f))->style(es);
        auto box = HNew(SBorder)->background(EditorTheme::bgInput)->borderThickness(EditorTheme::borderThin)
            ->cornerRadius(s(EditorTheme::radiusInput))->padding(HPadding{ s(5.f), 0.f })->vAlign(EVAlign::Center)
            ->content(HNew(SPressFocus)
                ->onPress([this] { if (!hexFocused_ && deps_.focus) deps_.focus(hexKeep_); })
                ->content(edit));
        box->borderColor(EditorTheme::borderPopup);
        hexHit_ = box;
        col->addSlot(box, VR::ESizeRule::Fixed, s(kRowH));
    }

    // ── 위: 제목, 아래: 확인 · 취소 ──
    auto title = HNew(STextBlock)
        ->text(HText::format(HTEXT("Editor.ColorPicker", "Title", "Color Picker: {0}"), { target_.title }).toString())
        ->fontSize(s(12.f))->color(EditorUiKit::kTextHdr)->bold()->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);
    auto okBtn = u.textButton(HTEXT("Editor.ColorPicker", "OK", "OK").toString(), 12.f, EditorUiKit::kTextBody, { 8.f, 2.f },
                              EditorTheme::accent, EditorTheme::accentHover, [this] { confirm(); });
    okBtn->hAlign(EHAlign::Center);
    auto cancelBtn = u.textButton(HTEXT("Editor.ColorPicker", "Cancel", "Cancel").toString(), 12.f, EditorUiKit::kTextBody, { 8.f, 2.f },
                                  EditorTheme::ctlNormal, EditorTheme::ctlHover, [this] { cancel(); });
    cancelBtn->hAlign(EHAlign::Center);
    auto buttons = HNew(SHorizontalBox)
        ->addSlot(u.spacer(), HR::ESizeRule::Fill)
        ->addSlot(okBtn, HR::ESizeRule::Fixed, s(kButtonW))
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(6.f))
        ->addSlot(cancelBtn, HR::ESizeRule::Fixed, s(kButtonW));

    auto middle = HNew(SHorizontalBox)
        ->addSlot(left, HR::ESizeRule::Fixed, leftW)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(kGap) + s(4.f))
        ->addSlot(col, HR::ESizeRule::Fixed, s(kColW));
    auto body = HNew(SVerticalBox)
        ->addSlot(title, VR::ESizeRule::Fixed, s(kTitleH))
        ->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(4.f))
        ->addSlot(middle, VR::ESizeRule::Fixed, colH)
        ->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(kBlockGap))
        ->addSlot(buttons, VR::ESizeRule::Fixed, s(kButtonsH));

    auto panel = HNew(SBox)->widthOverride(panelW)->heightOverride(panelH)->content(
        HNew(SBorder)
            ->background(EditorTheme::bgPopup)
            ->borderColor(EditorTheme::borderPopup)->borderThickness(EditorTheme::borderThin)
            ->cornerRadius(u.s(EditorTheme::radiusPopup))
            ->shadow({ 0.f, u.s(EditorTheme::shadowOffset.y) }, u.s(EditorTheme::shadowBlur), EditorTheme::shadowColor)
            ->padding(HPadding{ s(kPad), s(kPad) })
            ->content(body));

    // 자리: 견본 아래 왼쪽 맞춤(아래에 자리가 없으면 위, 창 안으로). 바깥 누름 = 확정(UE 의 색 선택기 창을 닫는 것과 같음),
    // 견본 위 누름은 바깥이 아님(견본이 토글 = 확정).
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
        confirm();   // 바깥 누름 · 다른 창에서 팝업이 열림 · 창 닫힘
    };
    handle_ = c->pushPopup(std::move(spec));
    ctx_    = handle_ ? c : nullptr;
    if (!handle_) closeQuiet();
}
