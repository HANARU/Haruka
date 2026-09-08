#pragma once
#include "../HWidget.hpp"
#include <functional>

// ── SToggleButton ─────────────────────────────────────────────────────────────
// 외부 상태(isActive 람다)에 따라 활성/비활성 배경색을 표시하는 버튼.
// 기즈모 모드 버튼, World/Local 토글 등에 사용.
class SToggleButton : public HWidget {
public:
    using ClickFn    = std::function<void()>;
    using IsActiveFn = std::function<bool()>;

    std::shared_ptr<SToggleButton> content(std::shared_ptr<HWidget> w)
        { child_ = std::move(w); return self(); }
    std::shared_ptr<SToggleButton> onClick(ClickFn fn)
        { onClick_ = std::move(fn); return self(); }
    std::shared_ptr<SToggleButton> isActive(IsActiveFn fn)
        { isActiveFn_ = std::move(fn); return self(); }
    std::shared_ptr<SToggleButton> normalColor (glm::vec4 c) { normal_  = c; return self(); }
    std::shared_ptr<SToggleButton> hoveredColor(glm::vec4 c) { hovered_ = c; return self(); }
    std::shared_ptr<SToggleButton> activeColor (glm::vec4 c) { active_  = c; return self(); }
    std::shared_ptr<SToggleButton> padding     (HPadding  p) { pad_     = p; return self(); }

    glm::vec2 computeDesiredSize() override {
        glm::vec2 cd = child_ ? child_->computeDesiredSize() : glm::vec2(0.f);
        return { cd.x + pad_.totalH(), cd.y + pad_.totalV() };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
        if (!child_) return;
        HGeometry inner = allotted.deflate(pad_);
        child_->onArrangeChildren(inner.makeChild({0.f, 0.f}, inner.size));
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        bool act = isActiveFn_ ? isActiveFn_() : false;
        glm::vec4 bg = act ? active_ : (isHovered_ ? hovered_ : normal_);
        if (bg.a > 0.f) ctx.drawRect(geo, bg);
        if (act) {
            HGeometry bar = geo.makeChild({0.f, geo.size.y - 2.f}, {geo.size.x, 2.f});
            ctx.drawRect(bar, glm::vec4{0.25f, 0.65f, 1.00f, 1.f});
        }
        if (child_ && child_->isVisible())
            child_->paint(ctx, child_->cachedGeometry());
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button == 0) {
            // 프레스와 릴리즈가 모두 내 위에서 일어났을 때만 발화합니다. 이 위젯은 캡처를
            // 요청하지 않으므로(캡처는 SNumberField 만 씁니다) 눌린 곳을 스스로 기억해야,
            // 다른 곳에서 시작한 드래그를 내 위에서 놓았을 때 눌린 것으로 오인하지 않습니다.
            if (e.down) {
                armed_ = cachedGeo_.contains(e.position);
            } else {
                if (armed_ && cachedGeo_.contains(e.position) && onClick_) onClick_();
                armed_ = false;
            }
            return HReply::Handled();
        }
        return HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        isHovered_ = cachedGeo_.contains(e.position);
        if (!e.down) armed_ = false;   // 눌린 채 리빌드되어 남은 상태를 정리
        return HReply::Unhandled();
    }

private:
    std::shared_ptr<HWidget> child_;
    ClickFn    onClick_;
    IsActiveFn isActiveFn_;
    glm::vec4  normal_  { 0.18f, 0.18f, 0.21f, 1.f };
    glm::vec4  hovered_ { 0.28f, 0.28f, 0.33f, 1.f };
    glm::vec4  active_  { 0.07f, 0.33f, 0.58f, 1.f };
    HPadding   pad_     { 9.f, 4.f };
    bool       isHovered_ = false;
    bool       armed_     = false;   // 이 위젯 위에서 눌렸는가(프레스-릴리즈 짝 판정)

    std::shared_ptr<SToggleButton> self() {
        return std::static_pointer_cast<SToggleButton>(shared_from_this());
    }
};
