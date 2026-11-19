#pragma once
#include "../HWidget.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

// ── SCheckBox ─────────────────────────────────────────────────────────────────
// 16px 정사각 체크박스 위젯(D5a).
// 참 = accent 바탕 + 흰 체크 아이콘(없으면 "v"), 거짓 = bgInput 바탕 + 1px 테두리,
// 호버 때 테두리를 hoverBorder 로 강조합니다.
class SCheckBox : public HWidget {
public:
    using IsCheckedFn = std::function<bool()>;
    using ClickFn     = std::function<void()>;
    using ToggleFn    = std::function<void(bool)>;

    struct Style {
        glm::vec4 uncheckedBg     { 0.08f, 0.08f, 0.10f, 1.f };
        glm::vec4 uncheckedBorder { 0.30f, 0.30f, 0.34f, 1.f };
        glm::vec4 checkedBg       { 0.07f, 0.42f, 0.84f, 1.f };
        glm::vec4 hoverBorder     { 0.30f, 0.52f, 0.91f, 1.f };
        glm::vec4 checkColor      { 1.f,   1.f,   1.f,   1.f };
        float       boxSize      = 16.f;
        float       cornerRadius = 3.f;
        float       borderWidth  = 1.f;
        const char* checkIcon    = "";
        glm::vec4 disabledColor   { 0.45f, 0.45f, 0.50f, 1.f };
    };

    std::shared_ptr<SCheckBox> style   (const Style& s) { style_ = s; return self(); }
    std::shared_ptr<SCheckBox> checked (IsCheckedFn fn) { isChecked_ = std::move(fn); return self(); }
    std::shared_ptr<SCheckBox> onClick (ClickFn fn)     { onClick_   = std::move(fn); return self(); }
    std::shared_ptr<SCheckBox> onToggle(ToggleFn fn)    { onToggle_  = std::move(fn); return self(); }
    std::shared_ptr<SCheckBox> boxSize (float px)       { style_.boxSize = px; return self(); }

    glm::vec2 computeDesiredSize() override {
        return { style_.boxSize, style_.boxSize };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        const float sz = std::min({ style_.boxSize, geo.size.x, geo.size.y });
        if (sz <= 0.f) return;
        const float ox = std::floor((geo.size.x - sz) * 0.5f);
        const float oy = std::floor((geo.size.y - sz) * 0.5f);
        HGeometry box  = geo.makeChild({ ox, oy }, { sz, sz });

        const bool en  = enabled.get();
        const bool chk = isChecked_ ? isChecked_() : false;
        if (chk) {
            const glm::vec4 bgCol = en ? style_.checkedBg : style_.uncheckedBg;
            const glm::vec4 bCol  = !en ? style_.uncheckedBorder : (isHovered_ ? style_.hoverBorder : glm::vec4{ 0.f });
            const float     bw    = (!en || isHovered_) ? style_.borderWidth : 0.f;
            ctx.drawRoundedRect(box, bgCol, style_.cornerRadius, bCol, bw);

            const glm::vec4 icCol  = en ? style_.checkColor : style_.disabledColor;
            const float     iconSz = std::max(1.f, std::floor(sz * 0.75f));
            const float     pad    = std::floor((sz - iconSz) * 0.5f);
            HGeometry       icGeo  = box.makeChild({ pad, pad }, { iconSz, iconSz });
            if (!(style_.checkIcon && style_.checkIcon[0] != '\0' &&
                  ctx.drawIcon(icGeo, style_.checkIcon, icCol))) {
                ctx.drawText(icGeo, "v", iconSz, icCol);
            }
        } else {
            const glm::vec4 bCol = (en && isHovered_) ? style_.hoverBorder : style_.uncheckedBorder;
            ctx.drawRoundedRect(box, style_.uncheckedBg, style_.cornerRadius, bCol, style_.borderWidth);
        }
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (!enabled.get()) return HReply::Unhandled();
        if (e.button == 0) {
            if (e.down) {
                armed_ = cachedGeo_.contains(e.position);
            } else {
                if (armed_ && cachedGeo_.contains(e.position)) {
                    const bool next = !(isChecked_ ? isChecked_() : false);
                    if (onToggle_) onToggle_(next);
                    if (onClick_)  onClick_();
                }
                armed_ = false;
            }
            return HReply::Handled();
        }
        return HReply::Unhandled();
    }

    HReply onMouseMove(const HPointerEvent& e) override {
        isHovered_ = enabled.get() && cachedGeo_.contains(e.position);
        if (!e.down || !enabled.get()) armed_ = false;
        return HReply::Unhandled();
    }

private:
    IsCheckedFn isChecked_;
    ClickFn     onClick_;
    ToggleFn    onToggle_;
    Style       style_;
    bool        isHovered_ = false;
    bool        armed_     = false;

    std::shared_ptr<SCheckBox> self() {
        return std::static_pointer_cast<SCheckBox>(shared_from_this());
    }
};
