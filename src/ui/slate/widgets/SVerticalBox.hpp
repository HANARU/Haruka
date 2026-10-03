#pragma once
#include "../HWidget.hpp"
#include <vector>

// ── SVerticalBox ──────────────────────────────────────────────────────────────
// 자식들을 세로로 배치. SHorizontalBox의 축 전환 버전.
class SVerticalBox : public HWidget {
public:
    enum class ESizeRule : uint8_t { Auto, Fill, Fixed };

    struct FSlot {
        std::shared_ptr<HWidget> widget;
        ESizeRule sizeRule = ESizeRule::Auto;
        float     value    = 1.f;
        HPadding  padding  {};
        EHAlign   hAlign   = EHAlign::Fill;
        EVAlign   vAlign   = EVAlign::Fill;
    };

    void clearSlots() { slots_.clear(); }

    std::shared_ptr<SVerticalBox> addSlot(std::shared_ptr<HWidget> w,
                                           ESizeRule rule  = ESizeRule::Auto,
                                           float     value = 0.f,
                                           HPadding  pad   = {},
                                           EHAlign   ha    = EHAlign::Fill) {
        slots_.push_back({ std::move(w), rule, value, pad, ha, EVAlign::Fill });
        return std::static_pointer_cast<SVerticalBox>(shared_from_this());
    }

    glm::vec2 computeDesiredSize() override {
        float maxW = 0.f, totalH = 0.f;
        for (auto& s : slots_) {
            if (s.sizeRule == ESizeRule::Fixed) {
                totalH += s.value + s.padding.totalV();
            } else {
                glm::vec2 d = s.widget->computeDesiredSize();
                maxW   = std::max(maxW, d.x + s.padding.totalH());
                totalH += d.y + s.padding.totalV();
            }
        }
        return { maxW, totalH };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;

        float fixedHeight = 0.f, fillTotal = 0.f;
        std::vector<float> desiredHeights(slots_.size(), 0.f);
        for (size_t i = 0; i < slots_.size(); ++i) {
            auto& s = slots_[i];
            if (s.sizeRule == ESizeRule::Fixed) {
                desiredHeights[i] = s.value;
                fixedHeight += s.value + s.padding.totalV();
            } else if (s.sizeRule == ESizeRule::Auto) {
                float dh = s.widget->computeDesiredSize().y;
                desiredHeights[i] = dh;
                fixedHeight += dh + s.padding.totalV();
            } else {
                // Fill — 가중치 미지정(0 이하)은 1로 간주.
                // 0이면 슬롯 높이가 0이 되어 그리기는 넘쳐 보이지만 히트테스트가 죽는다.
                fillTotal += (s.value > 0.f) ? s.value : 1.f;
                // 패딩은 나눌 몫이 아니라 고정 소비입니다. 2패스가 몫에 패딩을 더하므로 여기서도
                // 빼 두어야, 패딩 준 Fill 슬롯 뒤의 슬롯이 컨테이너 밖으로 밀리지 않습니다(DEF-62).
                fixedHeight += s.padding.totalV();
            }
        }

        float remaining = std::max(allotted.size.y - fixedHeight, 0.f);
        float penY = allotted.position.y;
        for (size_t i = 0; i < slots_.size(); ++i) {
            auto& s = slots_[i];
            float fillW = (s.value > 0.f) ? s.value : 1.f;
            float slotH = (s.sizeRule == ESizeRule::Fill)
                ? (fillTotal > 0.f ? (fillW / fillTotal) * remaining : 0.f)
                : desiredHeights[i];
            slotH += s.padding.totalV();

            HGeometry slotGeo;
            slotGeo.position = { allotted.position.x + s.padding.left,
                                 penY + s.padding.top };
            slotGeo.size     = { allotted.size.x - s.padding.totalH(),
                                 slotH - s.padding.totalV() };
            slotGeo.scale    = allotted.scale;

            glm::vec2 childDesired = s.widget->computeDesiredSize();
            float cx = slotGeo.alignH(childDesired.x, s.hAlign);
            float childW = (s.hAlign == EHAlign::Fill) ? slotGeo.size.x : childDesired.x;
            s.widget->onArrangeChildren(
                slotGeo.makeChild({ cx - slotGeo.position.x, 0.f }, { childW, slotGeo.size.y }));

            penY += slotH;
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
    HReply onMouseMove(const HPointerEvent& e) override {
        // Dispatch to ALL children so each can clear its own hover state
        HReply ret = HReply::Unhandled();
        for (auto& s : slots_) ret.merge(s.widget->onMouseMove(e));
        return ret;
    }

private:
    std::vector<FSlot> slots_;
};
