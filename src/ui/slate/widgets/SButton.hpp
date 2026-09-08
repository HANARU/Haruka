#pragma once
#include "../HWidget.hpp"
#include <functional>

// ── SButton ───────────────────────────────────────────────────────────────────
// 클릭 가능한 버튼. SBorder처럼 배경을 그리고 자식 위젯을 포함.
class SButton : public HWidget {
public:
    using ClickFn = std::function<void()>;

    std::shared_ptr<SButton> content(std::shared_ptr<HWidget> w) {
        child_ = std::move(w); return self();
    }
    std::shared_ptr<SButton> onClick(ClickFn fn) {
        onClick_ = std::move(fn); return self();
    }
    std::shared_ptr<SButton> normalColor  (glm::vec4 c) { normal_   = c; return self(); }
    std::shared_ptr<SButton> hoveredColor (glm::vec4 c) { hovered_  = c; return self(); }
    std::shared_ptr<SButton> pressedColor (glm::vec4 c) { pressed_  = c; return self(); }
    std::shared_ptr<SButton> padding      (HPadding  p) { padding_  = p; return self(); }
    std::shared_ptr<SButton> hAlign       (EHAlign   a) { hAlign_   = a; return self(); }
    std::shared_ptr<SButton> vAlign       (EVAlign   a) { vAlign_   = a; return self(); }

    glm::vec2 computeDesiredSize() override {
        glm::vec2 cd = child_ ? child_->computeDesiredSize() : glm::vec2(0.f);
        return { cd.x + padding_.totalH(), cd.y + padding_.totalV() };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
        if (!child_) return;
        HGeometry inner = allotted.deflate(padding_);
        glm::vec2 cs = child_->computeDesiredSize();
        float cx = inner.alignH(cs.x, hAlign_);
        float cy = inner.alignV(cs.y, vAlign_);
        float cw = (hAlign_ == EHAlign::Fill) ? inner.size.x : cs.x;
        float ch = (vAlign_ == EVAlign::Fill)  ? inner.size.y : cs.y;
        child_->onArrangeChildren(
            inner.makeChild({ cx - inner.position.x, cy - inner.position.y }, { cw, ch }));
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        glm::vec4 bg = normal_;
        if (isPressed_ && pressed_.a > 0.f)      bg = pressed_;
        else if (isHovered_ && hovered_.a > 0.f) bg = hovered_;
        if (bg.a > 0.f) ctx.drawRect(geo, bg);
        if (child_ && child_->isVisible())
            child_->paint(ctx, child_->cachedGeometry());
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (!enabled.get()) return HReply::Unhandled();
        if (e.button == 0) {
            // 프레스와 릴리즈가 모두 내 위에서 일어났을 때만 발화합니다. 이 위젯은 포인터
            // 캡처를 요청하지 않으므로(캡처는 SNumberField 만 씁니다) 눌린 곳을 스스로
            // 기억해야, 다른 데서 시작한 드래그를 내 위에서 놓았을 때 눌린 것으로 오인하지
            // 않습니다. 뷰포트 오빗을 아웃라이너 x 위에서 놓아 액터가 지워지던 경로입니다.
            if (e.down) {
                armed_     = cachedGeo_.contains(e.position);
                isPressed_ = armed_;
            } else {
                if (armed_ && cachedGeo_.contains(e.position) && onClick_) onClick_();
                armed_     = false;
                isPressed_ = false;
            }
            return HReply::Handled();
        }
        return HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        isHovered_ = cachedGeo_.contains(e.position);
        // 좌버튼이 이미 떨어진 뒤라면 눌림 상태를 정리합니다. 버튼 위에서 눌러 밖에서
        // 떼면 이 위젯은 마우스-업을 영영 받지 못해, 없으면 눌린 색으로 굳습니다.
        if (!e.down) { armed_ = false; isPressed_ = false; }
        return HReply::Unhandled();
    }

private:
    std::shared_ptr<HWidget> child_;
    ClickFn  onClick_;
    glm::vec4 normal_  { 0.25f, 0.25f, 0.28f, 1.f };
    glm::vec4 hovered_ { 0.35f, 0.35f, 0.40f, 1.f };
    glm::vec4 pressed_ { 0.15f, 0.15f, 0.18f, 1.f };
    HPadding  padding_ { 8.f, 4.f };
    EHAlign   hAlign_  = EHAlign::Center;
    EVAlign   vAlign_  = EVAlign::Center;
    bool      isHovered_ = false;
    bool      isPressed_ = false;
    bool      armed_     = false;   // 이 위젯 위에서 눌렸는가(프레스-릴리즈 짝 판정)

    std::shared_ptr<SButton> self() {
        return std::static_pointer_cast<SButton>(shared_from_this());
    }
};
