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
        color_.set(c); return self();
    }
    // 색을 매 프레임 평가합니다. 값으로 넘기면 생성 시점의 색에 고정되어, 상태에 따라
    // 색이 바뀌어야 하는 곳(상태바의 로그 심각도, 로그 행)이 갱신되지 않습니다.
    std::shared_ptr<STextBlock> color(Attr<glm::vec4> c) {
        color_ = std::move(c); return self();
    }
    std::shared_ptr<STextBlock> hAlign(EHAlign a) {
        hAlign_ = a; return self();
    }
    std::shared_ptr<STextBlock> vAlign(EVAlign a) {
        vAlign_ = a; return self();
    }

    glm::vec2 computeDesiredSize() override {
        // Attr 은 매 프레임 여러 번 평가되므로 게터는 한 번만 부릅니다.
        std::string        a = textAttr_.get();
        const std::string& t = a.empty() ? text_ : a;
        return measure(t);
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
        const glm::vec2 sz = measure(t);
        float tw = sz.x;
        float th = sz.y;
        float tx = geo.alignH(tw, hAlign_);
        float ty = geo.alignV(th, vAlign_);

        HGeometry textGeo;
        textGeo.position = { tx, ty };
        textGeo.size     = { tw, th };
        textGeo.scale    = geo.scale;
        ctx.drawText(textGeo, t, fontSize_, color_.get());
    }

private:
    // 글자 폭은 폰트 실측입니다. 예전에는 "글자 수 x 크기 x 0.6" 이라, 가운데·오른쪽 정렬이
    // 어긋나고 Auto 슬롯이 실제 글자보다 좁거나 넓었습니다(DEF-60). 폰트가 없으면(폰트 없이
    // 도는 테스트) 예전 근사를 씁니다. 같은 문자열이면 다시 재지 않습니다.
    glm::vec2 measure(const std::string& t) {
        const HFontAtlas* font = HFontAtlas::metrics();
        if (font == measuredFont_ && fontSize_ == measuredSize_ && t == measuredText_)
            return measured_;
        size_t lines = 1, longest = 0, cur = 0;
        for (char c : t) {
            if (c == '\n') { longest = std::max(longest, cur); cur = 0; ++lines; }
            else            { ++cur; }
        }
        longest = std::max(longest, cur);
        const float w = font ? font->measureText(t, fontSize_) : longest * fontSize_ * 0.6f;
        measured_     = { w, lines * fontSize_ * 1.2f };
        measuredText_ = t;
        measuredSize_ = fontSize_;
        measuredFont_ = font;
        return measured_;
    }

    std::string        text_;
    Attr<std::string>  textAttr_ { std::string{} };
    float              fontSize_ = 14.f;
    Attr<glm::vec4>    color_    { glm::vec4{ 1.f, 1.f, 1.f, 1.f } };
    EHAlign            hAlign_   = EHAlign::Left;
    EVAlign            vAlign_   = EVAlign::Center;
    std::string        measuredText_;
    float              measuredSize_ = -1.f;
    const HFontAtlas*  measuredFont_ = nullptr;
    glm::vec2          measured_ { 0.f, 0.f };

    std::shared_ptr<STextBlock> self() {
        return std::static_pointer_cast<STextBlock>(shared_from_this());
    }
};
