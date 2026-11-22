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

    void clearSlots() { slots_.clear(); hslate::noteLayoutChange(); }

    // 슬롯 수와 슬롯의 크기 값(Fill 가중치 · Fixed 픽셀)을 바꿉니다(H1b). 도크 스플리터가 끄는 동안 위젯 트리를
    // 다시 만들지 않고 비율만 고칠 때 씁니다. 범위 밖이면 false 이고 아무것도 바꾸지 않습니다. 값이 바뀌면 배치
    // 변경으로 알려, 다음 arrange 가 건너뛰지 않게 합니다.
    size_t slotCount() const { return slots_.size(); }
    bool setSlotValue(size_t i, float value) {
        if (i >= slots_.size()) return false;
        if (slots_[i].value != value) { slots_[i].value = value; hslate::noteLayoutChange(); }
        return true;
    }
    float slotValue(size_t i) const { return i < slots_.size() ? slots_[i].value : 0.f; }

    std::shared_ptr<SHorizontalBox> addSlot(std::shared_ptr<HWidget> w,
                                             ESizeRule rule  = ESizeRule::Auto,
                                             float     value = 0.f,
                                             HPadding  pad   = {},
                                             EVAlign   va    = EVAlign::Fill) {
        hslate::noteLayoutChange();
        slots_.push_back({ std::move(w), rule, value, pad, va, EHAlign::Fill });
        return std::static_pointer_cast<SHorizontalBox>(shared_from_this());
    }

    glm::vec2 computeDesiredSize() override {
        float totalW = 0.f, maxH = 0.f;
        for (auto& s : slots_) {
            if (s.sizeRule == ESizeRule::Fixed) {
                totalW += s.value + s.padding.totalH();
            } else {
                glm::vec2 d = s.widget->desiredSize();
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
                float dw = s.widget->desiredSize().x;
                desiredWidths[i] = dw;
                fixedWidth += dw + s.padding.totalH();
            } else {
                // Fill — 가중치 미지정(0 이하)은 1로 간주.
                // 0이면 슬롯 폭이 0이 되어 그리기는 넘쳐 보이지만 히트테스트가 죽는다.
                fillTotal += (s.value > 0.f) ? s.value : 1.f;
                // 패딩은 나눌 몫이 아니라 고정 소비입니다(SVerticalBox 와 같은 이유, DEF-62).
                fixedWidth += s.padding.totalH();
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

            glm::vec2 childDesired = s.widget->desiredSize();
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
        for (auto& s : slots_) {
            if (!s.widget->isVisible()) continue;   // paint 와 같은 기준
            if (s.widget->cachedGeometry().contains(e.position))
                if (auto r = s.widget->onMouseButton(e); r.handled) return r;
        }
        return HReply::Unhandled();
    }
    // 휠도 누름과 같은 규칙입니다(DEF-57).
    HReply onScroll(const HPointerEvent& e) override {
        for (auto& s : slots_) {
            if (!s.widget->isVisible()) continue;
            if (s.widget->cachedGeometry().contains(e.position))
                if (auto r = s.widget->onScroll(e); r.handled) return r;
        }
        return HReply::Unhandled();
    }
    std::shared_ptr<HWidget> findTooltipWidget(glm::vec2 pos) override {
        if (!isVisible()) return nullptr;
        for (auto& s : slots_)
            if (auto w = s.widget->findTooltipWidget(pos)) return w;
        if (!cachedGeo_.contains(pos)) return nullptr;
        return tooltip.get().empty() ? nullptr : shared_from_this();
    }

    HReply onMouseMove(const HPointerEvent& e) override {
        HReply ret = HReply::Unhandled();
        for (auto& s : slots_) ret.merge(s.widget->onMouseMove(e));
        return ret;
    }

    // 슬롯을 그리는 순서(앞 슬롯 먼저)대로 넘깁니다(H1a-1).
    void forEachChild(const std::function<void(const std::shared_ptr<HWidget>&)>& fn) override {
        for (auto& s : slots_)
            if (s.widget) fn(s.widget);
    }

private:
    std::vector<FSlot> slots_;
};
