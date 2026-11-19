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
    struct Style {
        glm::vec4 background      { 0.07f,  0.07f,  0.075f, 1.f };
        glm::vec4 border          { 0.055f, 0.055f, 0.060f, 1.f };
        glm::vec4 tabActiveBg     { 0.13f,  0.13f,  0.15f,  1.f };
        glm::vec4 tabHoveredBg    { 0.10f,  0.10f,  0.12f,  1.f };
        glm::vec4 tabNormalBg     { 0.f,    0.f,    0.f,    0.f };
        glm::vec4 activeUnderline { 0.07f,  0.47f,  0.79f,  1.f };
        glm::vec4 textActive      { 0.88f,  0.88f,  0.90f,  1.f };
        glm::vec4 textInactive    { 0.48f,  0.48f,  0.53f,  1.f };
        float     tabCornerRadius = 0.f;
    };

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
    std::shared_ptr<STabBar> style(const Style& s)
        { style_ = s; return self(); }
    std::shared_ptr<STabBar> tabCornerRadius(float r)
        { style_.tabCornerRadius = r; return self(); }

    glm::vec2 computeDesiredSize() override { return { 0.f, tabH_ }; }
    void onArrangeChildren(const HGeometry& allotted) override { cachedGeo_ = allotted; }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;

        ctx.drawRect(geo, style_.background);
        // Bottom border
        ctx.drawRect(geo.makeChild({0.f, tabH_ - 1.f}, {geo.size.x, 1.f}),
                     style_.border);

        int active = activeFn_ ? activeFn_() : 0;
        float x = 0.f;
        for (int i = 0; i < (int)tabs_.size(); i++) {
            HGeometry tg = geo.makeChild({x, 0.f}, {tabW_, tabH_});
            bool isAct  = (i == active);
            bool isHov  = tg.contains(hoverPos_);

            glm::vec4 bg = isAct  ? style_.tabActiveBg
                         : isHov  ? style_.tabHoveredBg
                                  : style_.tabNormalBg;
            if (bg.a > 0.f) {
                if (style_.tabCornerRadius > 0.f) ctx.drawRoundedRect(tg, bg, style_.tabCornerRadius);
                else                              ctx.drawRect(tg, bg);
            }

            if (isAct) {
                // UE5-style accent underline
                ctx.drawRect(geo.makeChild({x, tabH_ - 2.f}, {tabW_, 2.f}),
                             style_.activeUnderline);
            }

            // Right separator
            ctx.drawRect(geo.makeChild({x + tabW_ - 1.f, 2.f}, {1.f, tabH_ - 4.f}),
                         style_.border);

            float     textH = (fontSize_ + 1.f);
            HGeometry textGeo = tg.makeChild({8.f, (tabH_ - textH) * 0.5f},
                                             {tabW_ - 10.f, textH});
            glm::vec4 tc = isAct ? style_.textActive : style_.textInactive;
            ctx.drawText(textGeo, tabs_[i], fontSize_, tc);

            x += tabW_;
        }
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button != 0) return HReply::Unhandled();
        // 누른 탭과 뗀 탭이 같을 때만 바꿉니다(버튼류와 같은 프레스 추적). 예전에는 다른
        // 곳에서 누르고 탭 위에서 떼도 그 탭으로 바뀌었습니다(DEF-63).
        const int hit = tabAt(e.position);
        if (e.down) {
            pressedTab_ = hit;
            return hit >= 0 ? HReply::Handled() : HReply::Unhandled();
        }
        const bool fire = (hit >= 0 && hit == pressedTab_);
        pressedTab_ = -1;
        if (fire && onChange_) onChange_(hit);
        return hit >= 0 ? HReply::Handled() : HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        hoverPos_ = e.position;
        if (!e.down) pressedTab_ = -1;   // 탭에서 눌러 밖에서 뗀 경우
        return HReply::Unhandled();
    }

private:
    std::vector<std::string>      tabs_;
    std::function<int()>          activeFn_;
    std::function<void(int)>      onChange_;
    float     tabH_     = 28.f;
    float     tabW_     = 145.f;
    float     fontSize_ = 10.f;
    Style     style_;
    glm::vec2 hoverPos_{ -1.f, -1.f };
    int       pressedTab_ = -1;   // 누른 탭(없으면 -1)

    int tabAt(glm::vec2 p) const {
        float x = 0.f;
        for (int i = 0; i < (int)tabs_.size(); i++) {
            HGeometry tg = cachedGeo_.makeChild({x, 0.f}, {tabW_, tabH_});
            if (tg.contains(p)) return i;
            x += tabW_;
        }
        return -1;
    }

    std::shared_ptr<STabBar> self() {
        return std::static_pointer_cast<STabBar>(shared_from_this());
    }
};
