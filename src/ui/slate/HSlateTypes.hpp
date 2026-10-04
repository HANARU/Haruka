#pragma once
#include <cstdint>
#include <functional>

#define GLM_FORCE_RADIANS
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

    // 이동 이벤트를 여러 자식에게 뿌린 컨테이너가 응답을 합칩니다. 캡처는 처음 요청한 자식의
    // 것을 쓰고, 처리·해제는 하나라도 요청했으면 남깁니다. 예전에는 컨테이너가 응답을 버리거나
    // 마지막 Handled 만 남겨 캡처·해제 요청이 사라졌습니다(DEF-65).
    HReply& merge(const HReply& o) {
        handled      = handled || o.handled;
        if (!mouseCapture) mouseCapture = o.mouseCapture;
        mouseRelease = mouseRelease || o.mouseRelease;
        return *this;
    }
};

// ── 포인터(마우스) 이벤트 ─────────────────────────────────────────────────────
struct HPointerEvent {
    glm::vec2 position   = {};  // 렌더 타겟 공간 픽셀 좌표
    glm::vec2 delta      = {};
    int       button     = -1;  // -1=none, 0=left, 1=right, 2=middle
    bool      down       = false;
    float     wheelDelta = 0.f;
    // 이벤트를 만들 때의 수정 키 상태(좌우 구분 없음). 숫자 칸 끌기 배율에 씁니다(DEF-46).
    bool      shift      = false;
    bool      ctrl       = false;
};

// 잘라 낸 영역 밖의 이동을 자식에게 알릴 때 쓰는 이벤트입니다. 좌표가 어떤 위젯에도 들지
// 않아, 자식들은 호버를 풀되 잘려 보이지 않는 자리에서 호버를 켜지 않습니다(DEF-56).
// 버튼 상태(down)는 그대로 실어, 눌림 정리 규칙은 같게 동작합니다.
inline HPointerEvent HOutsideEvent(HPointerEvent e) {
    e.position = { -1.0e9f, -1.0e9f };
    return e;
}

// ── 글리프 정보 (폰트 아틀라스) ──────────────────────────────────────────────
struct HGlyphInfo {
    glm::vec2 uvMin, uvMax;   // 아틀라스 UV
    glm::vec2 bearing;        // xBearing, yBearing (px)
    glm::vec2 pixelSize;      // 비트맵 픽셀 크기 (gw, gh)
    float     advance;        // x 이동량 (px)
    float     size;           // 요청 폰트 크기
};
