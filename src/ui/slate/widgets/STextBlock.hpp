#pragma once
#include "../HWidget.hpp"
#include "../HFontAtlas.hpp"

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
        // 폰트 아틀라스 없이도 크기 추정: 문자당 0.6 × fontSize
        const std::string& t = textAttr_.get().empty() ? text_ : textAttr_.get();
        float approxW = t.size() * fontSize_ * 0.6f;
        return { approxW, fontSize_ * 1.2f };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        const std::string& t = textAttr_.get().empty() ? text_ : textAttr_.get();
        if (t.empty()) return;

        // 정렬: drawText는 geo.position을 텍스트 좌상단으로 사용
        float tw = t.size() * fontSize_ * 0.6f;
        float th = fontSize_ * 1.2f;
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
