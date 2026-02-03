#pragma once
#include "../HWidget.hpp"
#include <vector>

// ── SOverlay ──────────────────────────────────────────────────────────────────
// 자식들을 Z축으로 쌓는다. 모든 슬롯이 동일한 공간을 공유.
class SOverlay : public HWidget {
public:
    struct FSlot {
        std::shared_ptr<HWidget> widget;
        EHAlign  hAlign  = EHAlign::Fill;
        EVAlign  vAlign  = EVAlign::Fill;
        HPadding padding {};
    };

    std::shared_ptr<SOverlay> addSlot(std::shared_ptr<HWidget> w,
                                       EHAlign ha = EHAlign::Fill,
                                       EVAlign va = EVAlign::Fill,
                                       HPadding pad = {}) {
        slots_.push_back({ std::move(w), ha, va, pad });
        return std::static_pointer_cast<SOverlay>(shared_from_this());
    }

    glm::vec2 computeDesiredSize() override {
        glm::vec2 maxSize{ 0.f, 0.f };
        for (auto& s : slots_) {
            glm::vec2 d = s.widget->computeDesiredSize();
            maxSize.x = std::max(maxSize.x, d.x + s.padding.totalH());
            maxSize.y = std::max(maxSize.y, d.y + s.padding.totalV());
        }
        return maxSize;
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
        for (auto& s : slots_) {
            HGeometry inner = allotted.deflate(s.padding);
            glm::vec2 childSize = s.widget->computeDesiredSize();
            float cx = inner.alignH(childSize.x, s.hAlign);
            float cy = inner.alignV(childSize.y, s.vAlign);
            float cw = (s.hAlign == EHAlign::Fill) ? inner.size.x : childSize.x;
            float ch = (s.vAlign == EVAlign::Fill)  ? inner.size.y : childSize.y;
            s.widget->onArrangeChildren(
                inner.makeChild({ cx - inner.position.x, cy - inner.position.y }, { cw, ch }));
        }
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        for (auto& s : slots_)
            if (s.widget->isVisible())
                s.widget->paint(ctx, s.widget->cachedGeometry());
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        for (int i = (int)slots_.size() - 1; i >= 0; --i)
            if (slots_[i].widget->cachedGeometry().contains(e.position))
                if (auto r = slots_[i].widget->onMouseButton(e); r.handled) return r;
        return HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        for (int i = (int)slots_.size() - 1; i >= 0; --i)
            if (slots_[i].widget->cachedGeometry().contains(e.position))
                if (auto r = slots_[i].widget->onMouseMove(e); r.handled) return r;
        return HReply::Unhandled();
    }

private:
    std::vector<FSlot> slots_;
};
