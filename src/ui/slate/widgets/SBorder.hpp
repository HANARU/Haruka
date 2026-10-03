#pragma once
#include "../HWidget.hpp"

// ── SBorder ───────────────────────────────────────────────────────────────────
// 단색 배경 + 선택적 테두리를 가진 단일 자식 컨테이너.
class SBorder : public HWidget {
public:
    std::shared_ptr<SBorder> content(std::shared_ptr<HWidget> w) {
        child_ = std::move(w); return self();
    }
    std::shared_ptr<SBorder> background(glm::vec4 c) {
        bgColor_ = c; return self();
    }
    std::shared_ptr<SBorder> borderColor(glm::vec4 c) {
        borderColor_ = c; return self();
    }
    std::shared_ptr<SBorder> borderThickness(float t) {
        borderThick_ = t; return self();
    }
    std::shared_ptr<SBorder> padding(HPadding p) {
        padding_ = p; return self();
    }
    std::shared_ptr<SBorder> hAlign(EHAlign a) {
        hAlign_ = a; return self();
    }
    std::shared_ptr<SBorder> vAlign(EVAlign a) {
        vAlign_ = a; return self();
    }

    glm::vec2 computeDesiredSize() override {
        glm::vec2 childDesired = child_ ? child_->computeDesiredSize() : glm::vec2(0.f);
        return {
            childDesired.x + padding_.totalH(),
            childDesired.y + padding_.totalV()
        };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
        if (!child_) return;
        HGeometry inner = allotted.deflate(padding_);
        glm::vec2 childSize = child_->computeDesiredSize();
        float cx = inner.alignH(childSize.x, hAlign_);
        float cy = inner.alignV(childSize.y, vAlign_);
        float cw = (hAlign_ == EHAlign::Fill) ? inner.size.x : childSize.x;
        float ch = (vAlign_ == EVAlign::Fill)  ? inner.size.y : childSize.y;
        child_->onArrangeChildren(
            inner.makeChild({ cx - inner.position.x, cy - inner.position.y }, { cw, ch }));
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        if (bgColor_.a > 0.f)
            ctx.drawRect(geo, bgColor_);
        if (borderThick_ > 0.f && borderColor_.a > 0.f)
            ctx.drawBorder(geo, borderColor_, borderThick_);
        if (child_ && child_->isVisible())
            child_->paint(ctx, child_->cachedGeometry());
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        // 자기 rect 밖이면 자식에게 넘기지 않습니다. 넘기면 테두리·패딩 띠를 누른 것이
        // 안쪽 버튼을 누른 것으로 처리됩니다(SBorder 로 감싼 버튼에서 실제로 발생).
        if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
        return child_ ? child_->onMouseButton(e) : HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        return child_ ? child_->onMouseMove(e) : HReply::Unhandled();
    }

private:
    std::shared_ptr<HWidget> child_;
    glm::vec4 bgColor_     { 0.f, 0.f, 0.f, 0.f };
    glm::vec4 borderColor_ { 0.f, 0.f, 0.f, 0.f };
    float     borderThick_ = 0.f;
    HPadding  padding_     {};
    EHAlign   hAlign_      = EHAlign::Fill;
    EVAlign   vAlign_      = EVAlign::Fill;

    std::shared_ptr<SBorder> self() {
        return std::static_pointer_cast<SBorder>(shared_from_this());
    }
};
