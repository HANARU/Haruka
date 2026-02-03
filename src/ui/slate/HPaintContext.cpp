#include "HPaintContext.hpp"

void HPaintContext::drawRect(const HGeometry& geo, glm::vec4 color) {
    HDrawElement e;
    e.type    = HDrawElement::Type::Rect;
    e.rect    = {geo.position.x, geo.position.y, geo.size.x, geo.size.y};
    e.color   = color;
    e.layer   = layerBase_;
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

void HPaintContext::drawTexture(const HGeometry& geo, VkImageView tex,
                                glm::vec4 tint, glm::vec4 uvRect) {
    HDrawElement e;
    e.type    = HDrawElement::Type::Texture;
    e.rect    = {geo.position.x, geo.position.y, geo.size.x, geo.size.y};
    e.color   = tint;
    e.texture = tex;
    e.uvRect  = uvRect;
    e.layer   = layerBase_;
    elements.push_back(e);
}

void HPaintContext::drawText(const HGeometry& geo, const std::string& text,
                             float fontSize, glm::vec4 color) {
    HDrawElement e;
    e.type     = HDrawElement::Type::Text;
    e.rect     = {geo.position.x, geo.position.y, geo.size.x, geo.size.y};
    e.color    = color;
    e.text     = text;
    e.fontSize = fontSize;
    e.layer    = layerBase_;
    elements.push_back(e);
}

void HPaintContext::pushClip(const HGeometry& geo) {
    clipStack.push_back({{geo.position.x, geo.position.y, geo.size.x, geo.size.y}});
}

void HPaintContext::popClip() {
    if (!clipStack.empty()) clipStack.pop_back();
}

glm::vec4 HPaintContext::currentClip() const {
    if (clipStack.empty()) return {0, 0, 1e9f, 1e9f};
    return clipStack.back().rect;
}
