#pragma once
#include "../HWidget.hpp"
#include "../HFontAtlas.hpp"
#include <algorithm>
#include <string>

// ── SIcon ─────────────────────────────────────────────────────────────────────
// 이름으로 찾는 단색 아이콘(D3, 2026-11-19). 크기는 size × size(배율을 곱한 픽셀)이고, 아이콘 공급자가 없거나 모르는
// 이름이면 대신할 글자(fallback)를 그 글자 크기로 그립니다(아이콘 파일이 없어도 버튼 뜻이 남음).
class SIcon : public HWidget {
public:
    std::shared_ptr<SIcon> icon(std::string name)     { name_ = std::move(name); return self(); }
    std::shared_ptr<SIcon> icon(Attr<std::string> n)  { nameAttr_ = std::move(n); return self(); }
    std::shared_ptr<SIcon> size(float px)             { size_ = px; return self(); }
    std::shared_ptr<SIcon> color(glm::vec4 c)         { color_.set(c); return self(); }
    std::shared_ptr<SIcon> color(Attr<glm::vec4> c)   { color_ = std::move(c); return self(); }
    // 아이콘을 그릴 수 없을 때 쓸 글자와 크기(예: 예전의 "x" · "O")입니다.
    std::shared_ptr<SIcon> fallback(std::string t, float fontSize) {
        fallback_ = std::move(t); fallbackSize_ = fontSize; return self();
    }

    glm::vec2 computeDesiredSize() override {
        if (iconAvailable() || fallback_.empty()) return { size_, size_ };
        const glm::vec2 t = fallbackSize();
        return { std::max(t.x, size_), std::max(t.y, size_) };
    }

    void onArrangeChildren(const HGeometry& allotted) override { cachedGeo_ = allotted; }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        std::string        a    = nameAttr_.get();
        const std::string& name = a.empty() ? name_ : a;
        const glm::vec4    c    = color_.get();
        HGeometry box = geo;
        box.position  = { geo.alignH(size_, EHAlign::Center), geo.alignV(size_, EVAlign::Center) };
        box.size      = { size_, size_ };
        if (!name.empty() && ctx.drawIcon(box, name, c)) return;
        if (fallback_.empty()) return;
        const glm::vec2 ts = fallbackSize();
        HGeometry t = geo;
        t.position  = { geo.alignH(ts.x, EHAlign::Center), geo.alignV(ts.y, EVAlign::Center) };
        t.size      = ts;
        ctx.drawText(t, fallback_, fallbackSize_, c);
    }

private:
    // 레이아웃은 그리기 컨텍스트 없이 재므로, 글자 폭 기준 아틀라스(에디터는 UI 아틀라스 하나)로 아이콘이 있는지 봅니다.
    bool iconAvailable() const {
        const HFontAtlas* font = HFontAtlas::metrics();
        if (!font) return false;
        std::string a = nameAttr_.get();
        return font->hasIcon(a.empty() ? name_ : a);
    }
    glm::vec2 fallbackSize() const {
        const HFontAtlas* font = HFontAtlas::metrics();
        const float tw = font ? font->measureText(fallback_, fallbackSize_) : fallback_.size() * fallbackSize_ * 0.6f;
        return { tw, fallbackSize_ * 1.2f };
    }

    std::shared_ptr<SIcon> self() { return std::static_pointer_cast<SIcon>(shared_from_this()); }

    std::string       name_;
    Attr<std::string> nameAttr_;
    float             size_ = 16.f;
    Attr<glm::vec4>   color_{ glm::vec4(1.f) };
    std::string       fallback_;
    float             fallbackSize_ = 12.f;
};
