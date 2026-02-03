#pragma once
#include <cstdint>
#include <functional>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// ── Alignment enums ───────────────────────────────────────────────────────────
enum class EHAlign : uint8_t { Fill, Left, Center, Right };
enum class EVAlign : uint8_t { Fill, Top, Center, Bottom };
enum class EVisibility : uint8_t { Visible, Hidden, Collapsed };

// ── Padding (Slate의 FMargin) ─────────────────────────────────────────────────
struct HPadding {
    float left = 0, top = 0, right = 0, bottom = 0;

    HPadding() = default;
    explicit HPadding(float all) : left(all), top(all), right(all), bottom(all) {}
    HPadding(float h, float v) : left(h), top(v), right(h), bottom(v) {}
    HPadding(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}

    float totalH() const { return left + right; }
    float totalV() const { return top + bottom; }
};

// ── 이벤트 응답 ──────────────────────────────────────────────────────────────
class HWidget;
struct HReply {
    bool     handled      = false;
    HWidget* mouseCapture = nullptr;  // non-null = request capture
    bool     mouseRelease = false;    // request release capture

    static HReply Handled()   { return {true};  }
    static HReply Unhandled() { return {false}; }

    HReply& captureMouse(HWidget* w) { mouseCapture = w; return *this; }
    HReply& releaseMouse()           { mouseRelease  = true; return *this; }
};

// ── 포인터(마우스) 이벤트 ─────────────────────────────────────────────────────
struct HPointerEvent {
    glm::vec2 position   = {};  // 렌더 타겟 공간 픽셀 좌표
    glm::vec2 delta      = {};
    int       button     = -1;  // -1=none, 0=left, 1=right, 2=middle
    bool      down       = false;
    float     wheelDelta = 0.f;
};

// ── 글리프 정보 (폰트 아틀라스) ──────────────────────────────────────────────
struct HGlyphInfo {
    glm::vec2 uvMin, uvMax;   // 아틀라스 UV
    glm::vec2 bearing;        // xBearing, yBearing (px)
    glm::vec2 pixelSize;      // 비트맵 픽셀 크기 (gw, gh)
    float     advance;        // x 이동량 (px)
    float     size;           // 요청 폰트 크기
};
