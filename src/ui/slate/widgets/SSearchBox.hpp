#pragma once
#include "../HWidget.hpp"
#include "../HAttr.hpp"
#include "../HFontAtlas.hpp"
#include "SEditableText.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

// ── SSearchBox ────────────────────────────────────────────────────────────────
// 검색 입력 박스 위젯. 텍스트 상태는 EditorApp에서 관리하며 AttrStr로 주입.
// focused() 람다가 true를 반환하면 커서(|)를 표시하고 강조 테두리를 그린다.
// 실제 키 입력 라우팅은 EditorApp::tickRenameInput()에서 처리.
// edit(상태) 를 붙이면(글자 체계 T1, 2026-11-19) 글 · 커서 · 선택 · IME 조합 글을 SEditableText 와 같게 그리고, 누르면 그 자리에
// 커서를 놓고 끌면 선택합니다. 붙이지 않으면 예전 그림(글 뒤 '|')입니다.
class SSearchBox : public HWidget {
public:
    struct Style {
        glm::vec4 background    { 0.11f, 0.11f, 0.12f, 1.f };
        glm::vec4 borderFocused { 0.07f, 0.47f, 0.79f, 1.f };
        glm::vec4 borderHovered { 0.32f, 0.32f, 0.37f, 1.f };
        glm::vec4 borderNormal  { 0.18f, 0.18f, 0.20f, 1.f };
        glm::vec4 icon          { 0.38f, 0.38f, 0.42f, 1.f };
        glm::vec4 placeholder   { 0.32f, 0.32f, 0.37f, 1.f };
        glm::vec4 selection     { 0.20f, 0.45f, 0.85f, 1.f };
        glm::vec4 text          { 0.88f, 0.88f, 0.90f, 1.f };
        glm::vec4 cursor        { 0.70f, 0.70f, 0.75f, 1.f };
        float     cornerRadius  = 0.f;
        float     borderWidth   = 1.f;
        // 돋보기 아이콘 이름(D3). 비어 있거나 아이콘 공급자가 없으면 글자 "o" 를 그립니다(기본, 게임 쪽 그대로).
        const char* iconName    = nullptr;
    };

    std::shared_ptr<SSearchBox> text(AttrStr t)
        { text_ = std::move(t); return self(); }
    std::shared_ptr<SSearchBox> placeholder(std::string p)
        { placeholder_ = std::move(p); return self(); }
    std::shared_ptr<SSearchBox> focused(std::function<bool()> fn)
        { isFocused_ = std::move(fn); return self(); }
    std::shared_ptr<SSearchBox> selectedAll(std::function<bool()> fn)
        { isSelectedAll_ = std::move(fn); return self(); }
    std::shared_ptr<SSearchBox> onClick(std::function<void()> fn)
        { onClick_ = std::move(fn); return self(); }
    std::shared_ptr<SSearchBox> edit(HTextEditState* st)
        { edit_ = st; return self(); }
    std::shared_ptr<SSearchBox> onEdited(std::function<void()> fn)
        { onEdited_ = std::move(fn); return self(); }
    std::shared_ptr<SSearchBox> fontSize(float f)
        { fontSize_ = f; return self(); }
    // icon(false): magnifier glyph off (name fields in dialogs).
    std::shared_ptr<SSearchBox> icon(bool on)
        { icon_ = on; return self(); }
    std::shared_ptr<SSearchBox> style(const Style& s)
        { style_ = s; return self(); }

    glm::vec2 computeDesiredSize() override { return { 0.f, 24.f }; }
    void onArrangeChildren(const HGeometry& allotted) override { cachedGeo_ = allotted; }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;

        bool foc = isFocused_ ? isFocused_() : false;
        bool hov = cachedGeo_.contains(hoverPos_);

        glm::vec4 borderCol = foc ? style_.borderFocused
                            : hov ? style_.borderHovered
                                  : style_.borderNormal;
        if (style_.cornerRadius > 0.f) {
            ctx.drawRoundedRect(geo, style_.background, style_.cornerRadius, borderCol, style_.borderWidth);
        } else {
            ctx.drawRect(geo, style_.background);
            ctx.drawBorder(geo, borderCol, style_.borderWidth);
        }

        // Magnifier icon placeholder
        float fs      = fontSize_ / 10.f;
        float iconSz  = 12.f * fs;
        float iconOff = (geo.size.y - iconSz) * 0.5f;
        HGeometry iconGeo = geo.makeChild({iconOff, iconOff}, {iconSz, iconSz});
        if (icon_ && (!style_.iconName || !ctx.drawIcon(iconGeo, style_.iconName, style_.icon)))
            ctx.drawText(iconGeo, "o", 9.f * fs, style_.icon);

        // Text / placeholder
        float textX       = icon_ ? iconOff + iconSz + iconOff : 6.f * fs;
        float textH       = 14.f * fs;
        float textY       = (geo.size.y - textH) * 0.5f;
        HGeometry textGeo = geo.makeChild({textX, textY}, {geo.size.x - textX - 4.f, textH});
        textGeo_ = textGeo;
        if (edit_) {
            SEditableText::paintState(ctx, textGeo, edit_, foc, placeholder_, 10.f * fs, textH, editStyle(fs));
            return;
        }
        std::string disp  = text_.get();
        if (disp.empty()) {
            ctx.drawText(textGeo, placeholder_, 10.f * fs, style_.placeholder);
        } else {
            bool selAll = foc && isSelectedAll_ && isSelectedAll_();
            if (selAll) {
                // 전체 선택 하이라이트 (커서 없음)
                float tw = ctx.fonts() ? ctx.fonts()->measureText(disp, 10.f * fs) : 0.f;
                HGeometry sg = textGeo;
                sg.size.x = std::min(tw + 2.f, textGeo.size.x);
                ctx.drawRect(sg, style_.selection);
            } else if (foc) {
                disp += '|';
            }
            ctx.drawText(textGeo, disp, 10.f * fs, style_.text);
        }
        // Cursor when empty and focused
        if (disp.empty() && foc) {
            ctx.drawText(textGeo, "|", 10.f * fs, style_.cursor);
        }
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button != 0) return HReply::Unhandled();
        if (edit_) {
            // 누르는 순간 포커스를 얻고 그 자리에 커서를 놓습니다. 끌기 · 떼기는 이 위젯 위에서 누른 경우만 받습니다.
            if (e.down) {
                armed_ = cachedGeo_.contains(e.position);
                if (!armed_) return HReply::Unhandled();
                const bool wasFocused = isFocused_ && isFocused_();
                if (!wasFocused && onClick_) onClick_();
                if (!edit_->isComposing()) {
                    edit_->setCaret(caretAt(e.position, wasFocused), e.shift && wasFocused);
                    if (onEdited_) onEdited_();
                }
                return HReply::Handled();
            }
            const bool was = armed_;
            armed_ = false;
            return was ? HReply::Handled() : HReply::Unhandled();
        }
        // 버튼류와 같은 규약. 뷰포트에서 시작한 드래그를 검색창 위에서 놓으면 포커스를
        // 빼앗겨, 이후 타이핑이 전부 필터로 들어가고 사용자에게는 키가 죽은 것처럼 보입니다.
        if (e.down) {
            armed_ = cachedGeo_.contains(e.position);
            return armed_ ? HReply::Handled() : HReply::Unhandled();
        }
        const bool hit = armed_ && cachedGeo_.contains(e.position);
        armed_ = false;
        if (hit) {
            if (onClick_) onClick_();
            return HReply::Handled();
        }
        return HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        hoverPos_ = e.position;
        if (edit_ && armed_ && e.down && !edit_->isComposing()) {
            const size_t pos = caretAt(e.position, true);
            if (pos != edit_->caret) { edit_->setCaret(pos, true); if (onEdited_) onEdited_(); }
            return HReply::Handled();
        }
        if (!e.down) armed_ = false;
        return HReply::Unhandled();
    }

private:
    bool                      armed_ = false;   // 이 위젯 위에서 눌렸는가
    AttrStr                   text_;
    std::string               placeholder_ = "Search...";
    float                     fontSize_    = 10.f;
    bool                      icon_        = true;
    std::function<bool()>     isFocused_;
    std::function<bool()>     isSelectedAll_;
    std::function<void()>     onClick_;
    Style                     style_;
    HTextEditState*           edit_ = nullptr;
    std::function<void()>     onEdited_;
    HGeometry                 textGeo_;

    SEditableText::Style editStyle(float fs) const {
        SEditableText::Style es;
        es.text        = style_.text;
        es.placeholder = style_.placeholder;
        es.selection   = style_.selection;
        es.caret       = style_.cursor;
        es.composition = style_.text;
        es.caretWidth  = std::max(1.f, std::floor(fs));   // 1x 에서 1px, 배율 2 면 2px
        return es;
    }
    size_t caretAt(glm::vec2 p, bool focused) const {
        // 글 칸 위치는 그린 뒤에 알 수 있으므로, 아직 그리지 않았으면 위젯 기준으로 셈합니다.
        const HGeometry& g = textGeo_.size.x > 0.f ? textGeo_ : cachedGeo_;
        const float fs = fontSize_ / 10.f;
        return SEditableText::caretAt(*edit_, HFontAtlas::metrics(), 10.f * fs, p.x - g.position.x, g.size.x,
                                      focused, editStyle(fs).caretWidth);
    }
    glm::vec2                 hoverPos_{ -1.f, -1.f };

    std::shared_ptr<SSearchBox> self() {
        return std::static_pointer_cast<SSearchBox>(shared_from_this());
    }
};
