#pragma once
#include "../HWidget.hpp"
#include "../HPaintContext.hpp"
#include "../HTextEdit.hpp"
#include <algorithm>
#include <functional>

// ── SEditableText (2026-11-19, 글자 체계 T1 · T2, Docs/Text_System.md) ─────────────────────────────
// HTextEditState 하나를 그리는 한 줄 편집 칸의 글 부분입니다. 상태는 밖(패널)이 가지고 이 위젯은 포인터로 봅니다(행을 다시
// 만들어도 편집이 이어짐). 그리기: 선택 범위 사각형 → 글 → IME 조합 글 밑줄 → 커서 막대. 커서가 칸 밖으로 나가면 글을 왼쪽으로
// 밀어 보이게 합니다(상태 없이 커서 위치로 계산하므로 덤프가 결정적). 커서는 깜박이지 않습니다(덤프 · 화면 비교를 위해).
// 마우스: 누르면 커서를 놓고(Shift 면 선택을 늘림), 누른 채 끌면 선택합니다. 포커스는 onFocus 가 밖에 알립니다.
class SEditableText : public HWidget {
public:
    struct Style {
        glm::vec4 text       { 0.88f, 0.88f, 0.90f, 1.f };
        glm::vec4 placeholder{ 0.32f, 0.32f, 0.37f, 1.f };
        glm::vec4 selection  { 0.20f, 0.45f, 0.85f, 1.f };
        glm::vec4 caret      { 0.70f, 0.70f, 0.75f, 1.f };
        glm::vec4 composition{ 0.88f, 0.88f, 0.90f, 1.f };   // 조합 글 밑줄
        float     caretWidth = 1.f;
    };

    std::shared_ptr<SEditableText> state(HTextEditState* s)               { state_ = s; return self(); }
    std::shared_ptr<SEditableText> focused(std::function<bool()> fn)      { isFocused_ = std::move(fn); return self(); }
    std::shared_ptr<SEditableText> onFocus(std::function<void()> fn)      { onFocus_ = std::move(fn); return self(); }
    std::shared_ptr<SEditableText> onEdited(std::function<void()> fn)     { onEdited_ = std::move(fn); return self(); }
    std::shared_ptr<SEditableText> placeholder(std::string p)             { placeholder_ = std::move(p); return self(); }
    std::shared_ptr<SEditableText> fontSize(float f)                      { fontSize_ = f; return self(); }
    std::shared_ptr<SEditableText> textHeight(float h)                    { textH_ = h; return self(); }
    std::shared_ptr<SEditableText> style(const Style& s)                  { style_ = s; return self(); }

    glm::vec2 computeDesiredSize() override {
        const HFontAtlas* m = HFontAtlas::metrics();
        const float w = (m && state_) ? m->measureText(state_->displayText(), fontSize_) : 0.f;
        return { w + style_.caretWidth, textH_ > 0.f ? textH_ : fontSize_ * 1.4f };
    }
    void onArrangeChildren(const HGeometry& allotted) override { cachedGeo_ = allotted; }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        paintState(ctx, geo, state_, isFocused_ && isFocused_(), placeholder_, fontSize_, textH_, style_);
    }

    // 다른 위젯(SSearchBox)이 같은 그림을 그릴 때 씁니다.
    static void paintState(HPaintContext& ctx, const HGeometry& geo, const HTextEditState* st, bool focused,
                           const std::string& placeholder, float fontSize, float textH, const Style& style) {
        const float h = textH > 0.f ? std::min(textH, geo.size.y) : geo.size.y;
        const float y = (geo.size.y - h) * 0.5f;
        const std::string shown = st ? st->displayText() : std::string();
        if (shown.empty()) {
            if (!placeholder.empty() && !(focused && st && st->isComposing()))
                ctx.drawText(geo.makeChild({ 0.f, y }, { geo.size.x, h }), placeholder, fontSize, style.placeholder);
            if (focused) {
                const HGeometry cg = geo.makeChild({ 0.f, y }, { style.caretWidth, h });
                ctx.drawRect(cg, style.caret);
                if (st) st->caretRect = { cg.position.x, cg.position.y, cg.size.x, cg.size.y };
            }
            return;
        }
        const HFontAtlas* f = ctx.fonts();
        auto xAt = [&](size_t pos) { return HTextEditState::xOf(f, shown, fontSize, 0, pos); };
        const float caretX = focused ? xAt(st->displayCaret()) : 0.f;
        const float room   = std::max(0.f, geo.size.x - style.caretWidth);
        const float scroll = focused ? std::max(0.f, caretX - room) : 0.f;

        ctx.pushClip(geo);
        if (focused && st->hasSelection() && !st->isComposing()) {
            const float x0 = xAt(st->selBegin()) - scroll, x1 = xAt(st->selEnd()) - scroll;
            ctx.drawRect(geo.makeChild({ x0, y }, { std::max(1.f, x1 - x0), h }), style.selection);
        }
        ctx.drawText(geo.makeChild({ -scroll, y }, { geo.size.x + scroll, h }), shown, fontSize, style.text);
        if (focused && st->isComposing()) {
            const float x0 = xAt(st->compositionBegin()) - scroll, x1 = xAt(st->compositionEnd()) - scroll;
            const float lw = std::max(1.f, style.caretWidth);
            ctx.drawRect(geo.makeChild({ x0, y + h - lw }, { std::max(1.f, x1 - x0), lw }), style.composition);
        }
        if (focused) {
            const HGeometry cg = geo.makeChild({ caretX - scroll, y }, { style.caretWidth, h });
            ctx.drawRect(cg, style.caret);
            st->caretRect = { cg.position.x, cg.position.y, cg.size.x, cg.size.y };
        }
        ctx.popClip();
    }

    // 칸 안 x(위젯 왼쪽 기준 px)를 글자 경계로(그림과 같은 밀림을 뺌).
    static size_t caretAt(const HTextEditState& st, const HFontAtlas* f, float fontSize, float localX, float width,
                          bool focused, float caretWidth) {
        float scroll = 0.f;
        if (focused && f) {
            const float caretX = HTextEditState::xOf(f, st.displayText(), fontSize, 0, st.displayCaret());
            scroll = std::max(0.f, caretX - std::max(0.f, width - caretWidth));
        }
        return st.caretFromX(f, fontSize, 0, localX + scroll);
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button != 0 || !state_) return HReply::Unhandled();
        if (e.down) {
            if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
            pressAt(e.position, e.shift);
            dragging_ = true;
            return HReply::Handled();
        }
        const bool was = dragging_;
        dragging_ = false;
        return was ? HReply::Handled() : HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        if (!dragging_ || !state_) return HReply::Unhandled();
        if (!e.down) { dragging_ = false; return HReply::Unhandled(); }
        dragTo(e.position);
        return HReply::Handled();
    }

    // 누름 · 끌기(SSearchBox 도 씀). 포커스가 없으면 먼저 포커스를 얻은 뒤 커서를 놓습니다.
    void pressAt(glm::vec2 p, bool shift) {
        const bool wasFocused = isFocused_ && isFocused_();
        if (!wasFocused && onFocus_) onFocus_();
        if (state_->isComposing()) return;   // 조합 중에는 커서를 옮기지 않음(IME 가 확정할 때까지)
        const size_t pos = caretAt(*state_, HFontAtlas::metrics(), fontSize_, p.x - cachedGeo_.position.x,
                                   cachedGeo_.size.x, wasFocused, style_.caretWidth);
        state_->setCaret(pos, shift && wasFocused);
        if (onEdited_) onEdited_();
    }
    void dragTo(glm::vec2 p) {
        if (state_->isComposing()) return;
        const size_t pos = caretAt(*state_, HFontAtlas::metrics(), fontSize_, p.x - cachedGeo_.position.x,
                                   cachedGeo_.size.x, true, style_.caretWidth);
        if (pos != state_->caret) { state_->setCaret(pos, true); if (onEdited_) onEdited_(); }
    }
    bool isDragging() const { return dragging_; }
    void setGeometryForInput(const HGeometry& g) { cachedGeo_ = g; }

private:
    HTextEditState*       state_ = nullptr;
    std::function<bool()> isFocused_;
    std::function<void()> onFocus_;
    std::function<void()> onEdited_;
    std::string           placeholder_;
    float                 fontSize_ = 10.f;
    float                 textH_    = 0.f;
    Style                 style_;
    bool                  dragging_ = false;

    std::shared_ptr<SEditableText> self() { return std::static_pointer_cast<SEditableText>(shared_from_this()); }
};
