#pragma once
#include "../HWidget.hpp"
#include "../HAttr.hpp"
#include "../HFontAtlas.hpp"
#include <functional>
#include <string>

// ── SSearchBox ────────────────────────────────────────────────────────────────
// 검색 입력 박스 위젯. 텍스트 상태는 EditorApp에서 관리하며 AttrStr로 주입.
// focused() 람다가 true를 반환하면 커서(|)를 표시하고 강조 테두리를 그린다.
// 실제 키 입력 라우팅은 EditorApp::tickRenameInput()에서 처리.
class SSearchBox : public HWidget {
public:
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
    std::shared_ptr<SSearchBox> fontSize(float f)
        { fontSize_ = f; return self(); }

    glm::vec2 computeDesiredSize() override { return { 0.f, 24.f }; }
    void onArrangeChildren(const HGeometry& allotted) override { cachedGeo_ = allotted; }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;

        bool foc = isFocused_ ? isFocused_() : false;
        bool hov = cachedGeo_.contains(hoverPos_);

        ctx.drawRect(geo, glm::vec4{0.11f, 0.11f, 0.12f, 1.f});

        glm::vec4 borderCol = foc ? glm::vec4{0.07f, 0.47f, 0.79f, 1.f}
                            : hov ? glm::vec4{0.32f, 0.32f, 0.37f, 1.f}
                                  : glm::vec4{0.18f, 0.18f, 0.20f, 1.f};
        ctx.drawBorder(geo, borderCol, 1.f);

        // Magnifier icon placeholder
        float fs      = fontSize_ / 10.f;
        float iconSz  = 12.f * fs;
        float iconOff = (geo.size.y - iconSz) * 0.5f;
        HGeometry iconGeo = geo.makeChild({iconOff, iconOff}, {iconSz, iconSz});
        ctx.drawText(iconGeo, "o", 9.f * fs, {0.38f, 0.38f, 0.42f, 1.f});

        // Text / placeholder
        std::string disp  = text_.get();
        float textX       = iconOff + iconSz + iconOff;
        float textH       = 14.f * fs;
        float textY       = (geo.size.y - textH) * 0.5f;
        HGeometry textGeo = geo.makeChild({textX, textY}, {geo.size.x - textX - 4.f, textH});
        if (disp.empty()) {
            ctx.drawText(textGeo, placeholder_, 10.f * fs, {0.32f, 0.32f, 0.37f, 1.f});
        } else {
            bool selAll = foc && isSelectedAll_ && isSelectedAll_();
            if (selAll) {
                // 전체 선택 하이라이트 (커서 없음)
                float tw = ctx.fonts() ? ctx.fonts()->measureText(disp, 10.f * fs) : 0.f;
                HGeometry sg = textGeo;
                sg.size.x = std::min(tw + 2.f, textGeo.size.x);
                ctx.drawRect(sg, {0.20f, 0.45f, 0.85f, 1.f});
            } else if (foc) {
                disp += '|';
            }
            ctx.drawText(textGeo, disp, 10.f * fs, {0.88f, 0.88f, 0.90f, 1.f});
        }
        // Cursor when empty and focused
        if (disp.empty() && foc) {
            ctx.drawText(textGeo, "|", 10.f * fs, {0.70f, 0.70f, 0.75f, 1.f});
        }
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button == 0 && !e.down && cachedGeo_.contains(e.position)) {
            if (onClick_) onClick_();
            return HReply::Handled();
        }
        return HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        hoverPos_ = e.position;
        return HReply::Unhandled();
    }

private:
    AttrStr                   text_;
    std::string               placeholder_ = "Search...";
    float                     fontSize_    = 10.f;
    std::function<bool()>     isFocused_;
    std::function<bool()>     isSelectedAll_;
    std::function<void()>     onClick_;
    glm::vec2                 hoverPos_{ -1.f, -1.f };

    std::shared_ptr<SSearchBox> self() {
        return std::static_pointer_cast<SSearchBox>(shared_from_this());
    }
};
