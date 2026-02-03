#pragma once
#include "../HWidget.hpp"
#include <vector>

// ── SHorizontalBox ────────────────────────────────────────────────────────────
// 자식들을 가로로 배치. Slot마다 Auto / Fill / Fixed 크기 규칙 지원.
class SHorizontalBox : public HWidget {
public:
    enum class ESizeRule : uint8_t { Auto, Fill, Fixed };

    struct FSlot {
        std::shared_ptr<HWidget> widget;
        ESizeRule sizeRule = ESizeRule::Auto;
        float     value    = 1.f;   // Fill: 비율 / Fixed: 픽셀 너비
        HPadding  padding  {};
        EVAlign   vAlign   = EVAlign::Fill;
        EHAlign   hAlign   = EHAlign::Fill;
    };

    void clearSlots() { slots_.clear(); }

    std::shared_ptr<SHorizontalBox> addSlot(std::shared_ptr<HWidget> w,
                                             ESizeRule rule  = ESizeRule::Auto,
                                             float     value = 0.f,
                                             HPadding  pad   = {},
                                             EVAlign   va    = EVAlign::Fill) {
        slots_.push_back({ std::move(w), rule, value, pad, va, EHAlign::Fill });
        return std::static_pointer_cast<SHorizontalBox>(shared_from_this());
    }

    glm::vec2 computeDesiredSize() override {
        float totalW = 0.f, maxH = 0.f;
        for (auto& s : slots_) {
            if (s.sizeRule == ESizeRule::Fixed) {
                totalW += s.value + s.padding.totalH();
            } else {
                glm::vec2 d = s.widget->computeDesiredSize();
                totalW += d.x + s.padding.totalH();
                maxH = std::max(maxH, d.y + s.padding.totalV());
            }
        }
        return { totalW, maxH };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;

        // Pass 1: measure fixed + auto, accumulate fill ratios
        float fixedWidth = 0.f, fillTotal = 0.f;
        std::vector<float> desiredWidths(slots_.size(), 0.f);
        for (size_t i = 0; i < slots_.size(); ++i) {
            auto& s = slots_[i];
            if (s.sizeRule == ESizeRule::Fixed) {
                desiredWidths[i] = s.value;
                fixedWidth += s.value + s.padding.totalH();
            } else if (s.sizeRule == ESizeRule::Auto) {
                float dw = s.widget->computeDesiredSize().x;
                desiredWidths[i] = dw;
                fixedWidth += dw + s.padding.totalH();
            } else {
                // Fill — 가중치 미지정(0 이하)은 1로 간주.
                // 0이면 슬롯 폭이 0이 되어 그리기는 넘쳐 보이지만 히트테스트가 죽는다.
                fillTotal += (s.value > 0.f) ? s.value : 1.f;
            }
        }

        // Pass 2: distribute remaining space to Fill slots
        float remaining = std::max(allotted.size.x - fixedWidth, 0.f);
        float penX = allotted.position.x;
        for (size_t i = 0; i < slots_.size(); ++i) {
            auto& s = slots_[i];
            float fillW = (s.value > 0.f) ? s.value : 1.f;
            float slotW = (s.sizeRule == ESizeRule::Fill)
                ? (fillTotal > 0.f ? (fillW / fillTotal) * remaining : 0.f)
                : desiredWidths[i];
            slotW += s.padding.totalH();

            HGeometry slotGeo;
            slotGeo.position = { penX + s.padding.left,
                                 allotted.position.y + s.padding.top };
            slotGeo.size     = { slotW - s.padding.totalH(),
                                 allotted.size.y - s.padding.totalV() };
            slotGeo.scale    = allotted.scale;

            glm::vec2 childDesired = s.widget->computeDesiredSize();
            float cy = slotGeo.alignV(childDesired.y, s.vAlign);
            float childH = (s.vAlign == EVAlign::Fill) ? slotGeo.size.y : childDesired.y;
            s.widget->onArrangeChildren(
                slotGeo.makeChild({ 0.f, cy - slotGeo.position.y }, { slotGeo.size.x, childH }));

            penX += slotW;
        }
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        for (auto& s : slots_)
            if (s.widget->isVisible())
                s.widget->paint(ctx, s.widget->cachedGeometry());
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        for (auto& s : slots_)
            if (s.widget->cachedGeometry().contains(e.position))
                if (auto r = s.widget->onMouseButton(e); r.handled) return r;
        return HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        HReply ret = HReply::Unhandled();
        for (auto& s : slots_) {
            auto r = s.widget->onMouseMove(e);
            if (r.handled) ret = r;
        }
        return ret;
    }

private:
    std::vector<FSlot> slots_;
};
