#pragma once
#include "HGeometry.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

// ── DrawElement (GPU로 가는 렌더 커맨드 단위) ─────────────────────────────────
struct HDrawElement {
    enum class Type : uint8_t { Rect, Texture, Text } type;

    glm::vec4   rect;               // x, y, w, h (렌더 타겟 픽셀)
    glm::vec4   color;              // RGBA linear
    VkImageView texture   = VK_NULL_HANDLE;  // Texture / Text 타입에서 사용
    glm::vec4   uvRect    = {0,0,1,1};       // u0,v0,u1,v1
    int         layer     = 0;      // z-order (낮을수록 먼저)

    // Text 타입 전용 (HSlateRenderer가 개별 글리프로 분해)
    std::string text;
    float       fontSize  = 14.f;
};

// ── ClipRect 스택 항목 ────────────────────────────────────────────────────────
struct HClipRect {
    glm::vec4 rect; // x, y, w, h
};

// ── HPaintContext ─────────────────────────────────────────────────────────────
// 위젯들이 paint()에서 호출 → DrawElement 목록을 수집
class HFontAtlas;

class HPaintContext {
public:
    explicit HPaintContext(HFontAtlas* fonts) : fonts_(fonts) {}

    // ── 기본 드로우 ──────────────────────────────────────────────────────────
    void drawRect   (const HGeometry& geo, glm::vec4 color);
    void drawBorder (const HGeometry& geo, glm::vec4 color, float thickness);
    void drawTexture(const HGeometry& geo, VkImageView tex,
                     glm::vec4 tint = {1,1,1,1},
                     glm::vec4 uvRect = {0,0,1,1});
    void drawText   (const HGeometry& geo, const std::string& text,
                     float fontSize, glm::vec4 color);

    // ── 클립 스택 ────────────────────────────────────────────────────────────
    void pushClip(const HGeometry& geo);
    void popClip();

    // ── 레이어 제어 ──────────────────────────────────────────────────────────
    void pushLayer(int delta = 1) { layerBase_ += delta; }
    void popLayer (int delta = 1) { layerBase_ -= delta; }

    // ── HSlateRenderer가 소비 ────────────────────────────────────────────────
    std::vector<HDrawElement> elements;
    std::vector<HClipRect>    clipStack;

    HFontAtlas* fonts() const { return fonts_; }

private:
    HFontAtlas* fonts_     = nullptr;
    int         layerBase_ = 0;

    glm::vec4 currentClip() const;
};
