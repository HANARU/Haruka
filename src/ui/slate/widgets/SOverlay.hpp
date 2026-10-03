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
        for (int i = (int)slots_.size() - 1; i >= 0; --i) {
            if (!slots_[i].widget->isVisible()) continue;   // paint 와 같은 기준
            if (slots_[i].widget->cachedGeometry().contains(e.position))
                if (auto r = slots_[i].widget->onMouseButton(e); r.handled) return r;
        }
        return HReply::Unhandled();
    }
    // 이동은 포함 여부와 무관하게 모든 슬롯에 내려보냅니다(세로·가로 상자와 같은 규칙).
    // 포함하는 슬롯에만 주면 커서가 이 오버레이 밖으로 나갈 때, 마지막으로 hover 하던
    // 위젯이 해제 이벤트를 받지 못해 하이라이트가 켜진 채로 굳습니다. 각 위젯은 자기
    // geometry 로 포함 여부를 다시 판정하므로 넓게 뿌려도 결과는 정확합니다.
    HReply onMouseMove(const HPointerEvent& e) override {
        HReply ret = HReply::Unhandled();
        for (int i = (int)slots_.size() - 1; i >= 0; --i)
            ret.merge(slots_[i].widget->onMouseMove(e));
        return ret;
    }

private:
    std::vector<FSlot> slots_;
};
