#pragma once
#include "../HWidget.hpp"

// ── SBox ──────────────────────────────────────────────────────────────────────
// 단일 자식을 감싸는 컨테이너. 최소 크기와 정렬을 지정할 수 있다.
class SBox : public HWidget {
public:
    std::shared_ptr<SBox> content(std::shared_ptr<HWidget> w) {
        child_ = std::move(w); return std::static_pointer_cast<SBox>(shared_from_this());
    }
    std::shared_ptr<SBox> widthOverride(float w)  {
        widthOverride_  = w; return std::static_pointer_cast<SBox>(shared_from_this());
    }
    std::shared_ptr<SBox> heightOverride(float h) {
        heightOverride_ = h; return std::static_pointer_cast<SBox>(shared_from_this());
    }
    std::shared_ptr<SBox> hAlign(EHAlign a) {
        hAlign_ = a; return std::static_pointer_cast<SBox>(shared_from_this());
    }
    std::shared_ptr<SBox> vAlign(EVAlign a) {
        vAlign_ = a; return std::static_pointer_cast<SBox>(shared_from_this());
    }
    std::shared_ptr<SBox> padding(HPadding p) {
        padding_ = p; return std::static_pointer_cast<SBox>(shared_from_this());
    }

    glm::vec2 computeDesiredSize() override {
        glm::vec2 childDesired = child_ ? child_->computeDesiredSize() : glm::vec2(0.f);
        glm::vec2 inner{ childDesired.x + padding_.totalH(),
                         childDesired.y + padding_.totalV() };
        return {
            widthOverride_  >= 0.f ? widthOverride_  : inner.x,
            heightOverride_ >= 0.f ? heightOverride_ : inner.y
        };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
        if (!child_) return;
        HGeometry content = allotted.deflate(padding_);
        glm::vec2 childSize = child_->computeDesiredSize();
        float cx = content.alignH(childSize.x, hAlign_);
        float cy = content.alignV(childSize.y, vAlign_);
        glm::vec2 finalSize = (hAlign_ == EHAlign::Fill && vAlign_ == EVAlign::Fill)
            ? content.size
            : glm::vec2{ hAlign_ == EHAlign::Fill ? content.size.x : childSize.x,
                         vAlign_ == EVAlign::Fill  ? content.size.y : childSize.y };
        child_->onArrangeChildren(content.makeChild({ cx - content.position.x,
                                                      cy - content.position.y }, finalSize));
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible() || !child_ || !child_->isVisible()) return;
        child_->paint(ctx, child_->cachedGeometry());
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
        return child_ ? child_->onMouseButton(e) : HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        return child_ ? child_->onMouseMove(e) : HReply::Unhandled();
    }
    HReply onScroll(const HPointerEvent& e) override {
        if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
        return child_ ? child_->onScroll(e) : HReply::Unhandled();
    }

private:
    std::shared_ptr<HWidget> child_;
    float    widthOverride_  = -1.f;
    float    heightOverride_ = -1.f;
    EHAlign  hAlign_         = EHAlign::Fill;
    EVAlign  vAlign_         = EVAlign::Fill;
    HPadding padding_        {};
};
