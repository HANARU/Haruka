#pragma once
#include "../HWidget.hpp"
#include "../HFontAtlas.hpp"
#include <algorithm>

// ── STextBlock ────────────────────────────────────────────────────────────────
// 단순 텍스트 레이블. HPaintContext::drawText로 렌더.
class STextBlock : public HWidget {
public:
    std::shared_ptr<STextBlock> text(std::string t) {
        text_ = std::move(t); return self();
    }
    std::shared_ptr<STextBlock> text(Attr<std::string> t) {
        textAttr_ = std::move(t); return self();
    }
    std::shared_ptr<STextBlock> fontSize(float s) {
        fontSize_ = s; return self();
    }
    std::shared_ptr<STextBlock> color(glm::vec4 c) {
        color_ = c; return self();
    }
    std::shared_ptr<STextBlock> hAlign(EHAlign a) {
        hAlign_ = a; return self();
    }
    std::shared_ptr<STextBlock> vAlign(EVAlign a) {
        vAlign_ = a; return self();
    }

    glm::vec2 computeDesiredSize() override {
        // 폰트 아틀라스 없이도 크기 추정: 문자당 0.6 × fontSize.
        // Attr 은 매 프레임 여러 번 평가되므로 게터는 한 번만 부릅니다.
        std::string        a = textAttr_.get();
        const std::string& t = a.empty() ? text_ : a;
        size_t lines = 1, longest = 0, cur = 0;
        for (char c : t) {
            if (c == '\n') { longest = std::max(longest, cur); cur = 0; ++lines; }
            else            { ++cur; }
        }
        longest = std::max(longest, cur);
        return { longest * fontSize_ * 0.6f, lines * fontSize_ * 1.2f };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        std::string        a = textAttr_.get();
        const std::string& t = a.empty() ? text_ : a;
        if (t.empty()) return;

        // 정렬: drawText는 geo.position을 텍스트 좌상단으로 사용.
        // 여러 줄이면 최장 줄과 줄 수로 잡아야 정렬이 어긋나지 않습니다.
        size_t lines = 1, longest = 0, cur = 0;
        for (char c : t) {
            if (c == '\n') { longest = std::max(longest, cur); cur = 0; ++lines; }
            else            { ++cur; }
        }
        longest = std::max(longest, cur);
        float tw = longest * fontSize_ * 0.6f;
        float th = lines * fontSize_ * 1.2f;
        float tx = geo.alignH(tw, hAlign_);
        float ty = geo.alignV(th, vAlign_);

        HGeometry textGeo;
        textGeo.position = { tx, ty };
        textGeo.size     = { tw, th };
        textGeo.scale    = geo.scale;
        ctx.drawText(textGeo, t, fontSize_, color_);
    }

private:
    std::string        text_;
    Attr<std::string>  textAttr_ { std::string{} };
    float              fontSize_ = 14.f;
    glm::vec4          color_    { 1.f, 1.f, 1.f, 1.f };
    EHAlign            hAlign_   = EHAlign::Left;
    EVAlign            vAlign_   = EVAlign::Center;

    std::shared_ptr<STextBlock> self() {
        return std::static_pointer_cast<STextBlock>(shared_from_this());
    }
};
