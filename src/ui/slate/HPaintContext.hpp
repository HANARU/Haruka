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

    // 이 요소를 자를 영역(x, y, w, h)입니다. clipped 가 거짓이면 자르지 않습니다. 그릴 때
    // 클립 스택의 맨 위(조상 클립의 교집합)를 받아 둡니다(DEF-56).
    glm::vec4   clip      = {0,0,0,0};
    bool        clipped   = false;
};

// ── ClipRect 스택 항목 ────────────────────────────────────────────────────────
struct HClipRect {
    glm::vec4 rect; // x, y, w, h
};

// 축 정렬 사각형을 clip 영역으로 자릅니다. UV 도 같은 비율로 줄여, 잘린 글리프·텍스처가
// 찌그러지지 않고 잘린 채로 그려지게 합니다. 남는 부분이 없으면 거짓입니다.
inline bool HClipQuad(glm::vec4& rect, glm::vec4& uv, const glm::vec4& clip) {
    const float x0 = rect.x, y0 = rect.y, x1 = rect.x + rect.z, y1 = rect.y + rect.w;
    const float nx0 = x0 > clip.x ? x0 : clip.x;
    const float ny0 = y0 > clip.y ? y0 : clip.y;
    const float nx1 = x1 < clip.x + clip.z ? x1 : clip.x + clip.z;
    const float ny1 = y1 < clip.y + clip.w ? y1 : clip.y + clip.w;
    if (nx1 <= nx0 || ny1 <= ny0) return false;
    if (nx0 == x0 && ny0 == y0 && nx1 == x1 && ny1 == y1) return true;   // 통째로 안쪽
    const float tx0 = rect.z > 0.f ? (nx0 - x0) / rect.z : 0.f;
    const float tx1 = rect.z > 0.f ? (nx1 - x0) / rect.z : 1.f;
    const float ty0 = rect.w > 0.f ? (ny0 - y0) / rect.w : 0.f;
    const float ty1 = rect.w > 0.f ? (ny1 - y0) / rect.w : 1.f;
    const float du = uv.z - uv.x, dv = uv.w - uv.y;
    uv   = { uv.x + du * tx0, uv.y + dv * ty0, uv.x + du * tx1, uv.y + dv * ty1 };
    rect = { nx0, ny0, nx1 - nx0, ny1 - ny0 };
    return true;
}

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
    // 이후 그리는 요소를 geo 안으로 자릅니다. 안쪽 클립은 바깥 클립과의 교집합이라, 스크롤
    // 상자 안의 스크롤 상자도 바깥 영역을 넘지 않습니다. 짝을 맞춰 popClip 해야 합니다.
    void pushClip(const HGeometry& geo);
    void popClip();
    bool      hasClip() const { return !clipStack.empty(); }
    glm::vec4 currentClip() const;

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

    void applyClip(HDrawElement& e) const;
};
