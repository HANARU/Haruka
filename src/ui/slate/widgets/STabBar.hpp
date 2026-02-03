#pragma once
#include "../HWidget.hpp"
#include <vector>
#include <string>
#include <functional>

// ── STabBar ───────────────────────────────────────────────────────────────────
// 수평 탭 바. activeTab 람다가 현재 활성 탭 인덱스를 반환하고,
// onTabChanged가 선택 변경 시 호출된다.
class STabBar : public HWidget {
public:
    std::shared_ptr<STabBar> addTab(std::string label)
        { tabs_.push_back(std::move(label)); return self(); }
    std::shared_ptr<STabBar> activeTab(std::function<int()> fn)
        { activeFn_ = std::move(fn); return self(); }
    std::shared_ptr<STabBar> onTabChanged(std::function<void(int)> fn)
        { onChange_ = std::move(fn); return self(); }
    std::shared_ptr<STabBar> tabWidth(float w)
        { tabW_ = w; return self(); }
    std::shared_ptr<STabBar> height(float h)
        { tabH_ = h; return self(); }
    std::shared_ptr<STabBar> fontSize(float f)
        { fontSize_ = f; return self(); }

    glm::vec2 computeDesiredSize() override { return { 0.f, tabH_ }; }
    void onArrangeChildren(const HGeometry& allotted) override { cachedGeo_ = allotted; }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;

        ctx.drawRect(geo, glm::vec4{0.07f, 0.07f, 0.075f, 1.f});
        // Bottom border
        ctx.drawRect(geo.makeChild({0.f, tabH_ - 1.f}, {geo.size.x, 1.f}),
                     glm::vec4{0.055f, 0.055f, 0.060f, 1.f});

        int active = activeFn_ ? activeFn_() : 0;
        float x = 0.f;
        for (int i = 0; i < (int)tabs_.size(); i++) {
            HGeometry tg = geo.makeChild({x, 0.f}, {tabW_, tabH_});
            bool isAct  = (i == active);
            bool isHov  = tg.contains(hoverPos_);

            glm::vec4 bg = isAct  ? glm::vec4{0.13f, 0.13f, 0.15f, 1.f}
                         : isHov  ? glm::vec4{0.10f, 0.10f, 0.12f, 1.f}
                                  : glm::vec4{0.f, 0.f, 0.f, 0.f};
            if (bg.a > 0.f) ctx.drawRect(tg, bg);

            if (isAct) {
                // UE5-style accent underline
                ctx.drawRect(geo.makeChild({x, tabH_ - 2.f}, {tabW_, 2.f}),
                             glm::vec4{0.07f, 0.47f, 0.79f, 1.f});
            }

            // Right separator
            ctx.drawRect(geo.makeChild({x + tabW_ - 1.f, 2.f}, {1.f, tabH_ - 4.f}),
                         glm::vec4{0.055f, 0.055f, 0.060f, 1.f});

            float     textH = (fontSize_ + 1.f);
            HGeometry textGeo = tg.makeChild({8.f, (tabH_ - textH) * 0.5f},
                                             {tabW_ - 10.f, textH});
            glm::vec4 tc = isAct ? glm::vec4{0.88f, 0.88f, 0.90f, 1.f}
                                 : glm::vec4{0.48f, 0.48f, 0.53f, 1.f};
            ctx.drawText(textGeo, tabs_[i], fontSize_, tc);

            x += tabW_;
        }
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button == 0 && !e.down) {
            float x = 0.f;
            for (int i = 0; i < (int)tabs_.size(); i++) {
                HGeometry tg = cachedGeo_.makeChild({x, 0.f}, {tabW_, tabH_});
                if (tg.contains(e.position)) {
                    if (onChange_) onChange_(i);
                    return HReply::Handled();
                }
                x += tabW_;
            }
        }
        return HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        hoverPos_ = e.position;
        return HReply::Unhandled();
    }

private:
    std::vector<std::string>      tabs_;
    std::function<int()>          activeFn_;
    std::function<void(int)>      onChange_;
    float     tabH_     = 28.f;
    float     tabW_     = 145.f;
    float     fontSize_ = 10.f;
    glm::vec2 hoverPos_{ -1.f, -1.f };

    std::shared_ptr<STabBar> self() {
        return std::static_pointer_cast<STabBar>(shared_from_this());
    }
};
