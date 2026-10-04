#pragma once
#include "../HWidget.hpp"
#include <functional>

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
    // 자식이 이 테두리 밖으로 그려지지 않게 자릅니다(DEF-56). 도크 리프가 패널 내용을 자르는
    // 데 씁니다. 자르는 테두리는 영역 밖의 이동을 자식에게 "바깥" 으로 알려, 잘려 보이지 않는
    // 자리에서 호버가 켜지지 않게 합니다(누름과 휠은 원래 영역 안에서만 받습니다).
    std::shared_ptr<SBorder> clipChildren(bool c) {
        clip_ = c; return self();
    }
    // 휠 처리기. 자식이 받지 않은 휠이 이 테두리 위에서 굴려지면 부르고, 참을 돌려주면 받은
    // 것으로 칩니다(DEF-57). 보이는 행만 위젯으로 만드는 목록(아웃라이너·Output Log)처럼
    // 스크롤 상자 대신 스스로 스크롤하는 곳이 씁니다.
    std::shared_ptr<SBorder> onWheel(std::function<bool(float)> fn) {
        onWheel_ = std::move(fn); return self();
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
        if (child_ && child_->isVisible()) {
            if (clip_) ctx.pushClip(geo);
            child_->paint(ctx, child_->cachedGeometry());
            if (clip_) ctx.popClip();
        }
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        // 자기 rect 밖이면 자식에게 넘기지 않습니다. 넘기면 테두리·패딩 띠를 누른 것이
        // 안쪽 버튼을 누른 것으로 처리됩니다(SBorder 로 감싼 버튼에서 실제로 발생).
        if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
        return child_ ? child_->onMouseButton(e) : HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        if (!child_) return HReply::Unhandled();
        if (clip_ && !cachedGeo_.contains(e.position)) return child_->onMouseMove(HOutsideEvent(e));
        return child_->onMouseMove(e);
    }
    HReply onScroll(const HPointerEvent& e) override {
        if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
        if (child_)
            if (auto r = child_->onScroll(e); r.handled) return r;
        if (onWheel_ && onWheel_(e.wheelDelta)) return HReply::Handled();
        return HReply::Unhandled();
    }

private:
    std::shared_ptr<HWidget> child_;
    glm::vec4 bgColor_     { 0.f, 0.f, 0.f, 0.f };
    glm::vec4 borderColor_ { 0.f, 0.f, 0.f, 0.f };
    float     borderThick_ = 0.f;
    HPadding  padding_     {};
    EHAlign   hAlign_      = EHAlign::Fill;
    EVAlign   vAlign_      = EVAlign::Fill;
    bool      clip_        = false;
    std::function<bool(float)> onWheel_;

    std::shared_ptr<SBorder> self() {
        return std::static_pointer_cast<SBorder>(shared_from_this());
    }
};
