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

    // 키보드 포커스 요청(H1a-1). 둘 중 하나만 켜집니다. HSlateContext 가 이벤트를 처리한 뒤
    // 적용합니다(마우스 누름 · 키 이벤트). 위젯은 raw 포인터로 넘기고, 문맥이 적용할 때
    // weak_from_this 로 약한 참조로 바꿔 붙잡습니다(캡처와 같은 방식, DEF-76).
    HWidget* focusRequest = nullptr;  // non-null = 이 위젯에 포커스를 줌
    bool     focusClear   = false;    // 포커스를 풂

    static HReply Handled()   { return {true};  }
    static HReply Unhandled() { return {false}; }

    HReply& captureMouse(HWidget* w) { mouseCapture = w; return *this; }
    HReply& releaseMouse()           { mouseRelease  = true; return *this; }
    // 나중에 부른 쪽이 앞의 요청을 덮습니다(한 위젯 안에서는 마지막 뜻을 씀).
    HReply& setFocus(HWidget* w)     { focusRequest = w; focusClear = false; return *this; }
    HReply& clearFocus()             { focusRequest = nullptr; focusClear = true; return *this; }
    bool    hasFocusRequest() const  { return focusRequest != nullptr || focusClear; }

    // 이동 이벤트를 여러 자식에게 뿌린 컨테이너가 응답을 합칩니다. 캡처는 처음 요청한 자식의
    // 것을 쓰고, 처리·해제는 하나라도 요청했으면 남깁니다. 예전에는 컨테이너가 응답을 버리거나
    // 마지막 Handled 만 남겨 캡처·해제 요청이 사라졌습니다(DEF-65).
    // 포커스 요청도 캡처와 같이 처음 요청한 쪽을 씁니다. 설정과 해제는 한 묶음으로 보아, 먼저
    // 해제를 요청한 자식이 있으면 뒤 자식의 설정 요청은 버립니다(두 뜻을 섞지 않음).
    HReply& merge(const HReply& o) {
        handled      = handled || o.handled;
        if (!mouseCapture) mouseCapture = o.mouseCapture;
        mouseRelease = mouseRelease || o.mouseRelease;
        if (!hasFocusRequest()) { focusRequest = o.focusRequest; focusClear = o.focusClear; }
        return *this;
    }
};

// ── 키보드 이벤트 (H1a-1) ─────────────────────────────────────────────────────
// key 는 GLFW 키 코드 값입니다. src/platform/IWindow.hpp 의 HInput 상수(KEY_ENTER = 257 등)와
// 같은 값이지만, ui 모듈은 platform 을 include 할 수 없어(tests/ArchitectureRules.txt) 정수로만
// 둡니다. 수정 키 상태는 좌우를 구분하지 않습니다(HPointerEvent 와 같음).
struct HKeyEvent {
    int  key    = 0;
    bool down   = true;   // false = 뗌
    bool repeat = false;  // 누른 채 반복
    bool ctrl   = false;
    bool shift  = false;
    bool alt    = false;
};

// 글자 입력(유니코드 코드 포인트). IME 조합이 끝난 글자도 이 이벤트로 옵니다.
struct HCharEvent {
    uint32_t codepoint = 0;
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
    int       page = 0;       // 아틀라스 페이지(글자 체계 T4-3). 그릴 때 HFontAtlas::pageView(page) 로 텍스처를 고름
};
