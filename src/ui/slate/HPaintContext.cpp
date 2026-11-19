#include "HPaintContext.hpp"
#include "HFontAtlas.hpp"
#include <algorithm>
#include <cmath>

void HPaintContext::applyClip(HDrawElement& e) const {
    e.clipped = !clipStack.empty();
    if (e.clipped) e.clip = clipStack.back().rect;
}

void HPaintContext::drawRect(const HGeometry& geo, glm::vec4 color) {
    HDrawElement e;
    e.type    = HDrawElement::Type::Rect;
    e.rect    = {geo.position.x, geo.position.y, geo.size.x, geo.size.y};
    e.color   = color;
    e.layer   = layerBase_;
    applyClip(e);
    elements.push_back(e);
}

void HPaintContext::drawBorder(const HGeometry& geo, glm::vec4 color, float t) {
    // top
    drawRect(geo.makeChild({0,0},    {geo.size.x, t}),              color);
    // bottom
    drawRect(geo.makeChild({0, geo.size.y-t}, {geo.size.x, t}),     color);
    // left
    drawRect(geo.makeChild({0, t},   {t, geo.size.y - 2*t}),        color);
    // right
    drawRect(geo.makeChild({geo.size.x-t, t},{t, geo.size.y - 2*t}),color);
}

void HPaintContext::drawRoundedRect(const HGeometry& geo, glm::vec4 color, float radius,
                                    glm::vec4 borderColor, float borderWidth) {
    HDrawElement e;
    e.type         = HDrawElement::Type::Rect;
    e.rect         = {geo.position.x, geo.position.y, geo.size.x, geo.size.y};
    e.color        = color;
    e.cornerRadius = radius;
    e.borderWidth  = borderWidth;
    e.borderColor  = borderColor;
    e.layer        = layerBase_;
    applyClip(e);
    elements.push_back(e);
}

void HPaintContext::drawShadow(const HGeometry& geo, float radius, glm::vec2 offset,
                               float blur, glm::vec4 color) {
    HDrawElement e;
    e.type         = HDrawElement::Type::Shadow;
    e.rect         = {geo.position.x + offset.x, geo.position.y + offset.y, geo.size.x, geo.size.y};
    e.color        = color;
    e.cornerRadius = radius;
    e.shadowBlur   = blur;
    e.layer        = layerBase_;
    applyClip(e);
    elements.push_back(e);
}

void HPaintContext::drawTexture(const HGeometry& geo, VkImageView tex,
                                glm::vec4 tint, glm::vec4 uvRect) {
    HDrawElement e;
    e.type    = HDrawElement::Type::Texture;
    e.rect    = {geo.position.x, geo.position.y, geo.size.x, geo.size.y};
    e.color   = tint;
    e.texture = tex;
    e.uvRect  = uvRect;
    e.layer   = layerBase_;
    applyClip(e);
    elements.push_back(e);
}

bool HPaintContext::drawIcon(const HGeometry& geo, const std::string& name, glm::vec4 color) {
    if (!fonts_ || !fonts_->hasIcon(name)) return false;
    const float px = std::floor(std::min(geo.size.x, geo.size.y));
    if (px < 1.f) return true;   // 자리가 없으면 그리지 않지만 아이콘은 있음
    if (!disabledColorStack_.empty()) color = disabledColorStack_.back();
    HDrawElement e;
    e.type     = HDrawElement::Type::Icon;
    // 아이콘 칸을 정수 픽셀 위치에 둡니다(반 픽셀에 걸치면 선이 흐려짐).
    e.rect     = { std::round(geo.position.x + (geo.size.x - px) * 0.5f),
                   std::round(geo.position.y + (geo.size.y - px) * 0.5f), px, px };
    e.color    = color;
    e.text     = name;
    e.fontSize = px;
    e.layer    = layerBase_;
    applyClip(e);
    elements.push_back(e);
    return true;
}

void HPaintContext::drawText(const HGeometry& geo, const std::string& text,
                             float fontSize, glm::vec4 color, int face) {
    if (!disabledColorStack_.empty()) color = disabledColorStack_.back();
    HDrawElement e;
    e.type     = HDrawElement::Type::Text;
    e.fontFace = (uint8_t)(face == 1 ? 1 : 0);
    e.rect     = {geo.position.x, geo.position.y, geo.size.x, geo.size.y};
    e.color    = color;
    e.text     = text;
    e.fontSize = fontSize;
    e.layer    = layerBase_;
    applyClip(e);
    elements.push_back(e);
}

void HPaintContext::pushClip(const HGeometry& geo) {
    glm::vec4 r{ geo.position.x, geo.position.y, geo.size.x, geo.size.y };
    if (!clipStack.empty()) {
        const glm::vec4& o = clipStack.back().rect;
        const float x0 = std::max(r.x, o.x), y0 = std::max(r.y, o.y);
        const float x1 = std::min(r.x + r.z, o.x + o.z), y1 = std::min(r.y + r.w, o.y + o.w);
        r = { x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0) };
    }
    clipStack.push_back({ r });
}

void HPaintContext::popClip() {
    if (!clipStack.empty()) clipStack.pop_back();
}

glm::vec4 HPaintContext::currentClip() const {
    if (clipStack.empty()) return {0, 0, 1e9f, 1e9f};
    return clipStack.back().rect;
}
