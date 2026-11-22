#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cstdlib>
#include <string>
#include <vector>

// 창별 입력 분배의 순수 규칙(다중 OS 창 M3b, 2026-11-22, Docs/Editor_MultiWindow_Design.md 3절 M3).
//
// 에디터는 창마다(메인 + 떠 있는 창) 자기 OS 창의 커서 · 버튼 · 휠을 읽어 그 창 문맥에 보냅니다. GLFW 는 이미 창마다 입력을
// 나눠 주므로(커서 좌표는 그 창 내용 영역 기준, 누름은 커서 아래 창, 키는 OS 포커스 창), 여기 규칙은 "어느 창의 입력을 누가
// 쓰는가"만 정합니다. 위젯 · 플랫폼을 모르므로 단위 시험이 그대로 부릅니다(slate_input).
namespace hwininput {

// 창 하나의 이번 프레임 포인터 값(OS 창에서 읽거나 시험 대본이 만든 값).
struct PointerSample {
    glm::vec2 pos   { -10000.f, -10000.f };   // 그 창 내용 좌표(숨은 창은 -10000, GlfwWindow 와 같음)
    bool      left  = false;
    bool      right = false;
    float     scroll = 0.f;                    // 이번 프레임 휠(그 창에서 읽어 비움)
};

// 한 프레임 묶음. 누름 · 뗌 엣지는 그 창의 직전 프레임 값으로 판정합니다(창마다 따로 — 메인의 prevLeftDown_ 과 섞지 않음).
struct PointerFrame {
    glm::vec2 pos{ 0.f };
    bool  left = false, right = false;
    bool  leftPressed = false, leftReleased = false, rightPressed = false;
    float scroll = 0.f;
};

inline PointerFrame makeFrame(const PointerSample& s, bool prevLeft, bool prevRight) {
    PointerFrame f;
    f.pos          = s.pos;
    f.left         = s.left;
    f.right        = s.right;
    f.leftPressed  = s.left && !prevLeft;
    f.leftReleased = !s.left && prevLeft;
    f.rightPressed = s.right && !prevRight;
    f.scroll       = s.scroll;
    return f;
}

// 키보드 창 고르기: OS 포커스를 가진 떠 있는 창(목록 순서로 처음 것), 없으면 0 = 메인. 숨은 GLFW 창의 isFocused() 는 보통
// 거짓이라 숨은 실행 · 덤프는 언제나 메인입니다(예전 동작). 최소화된 창은 키보드를 받지 않습니다.
struct WindowFocus {
    int  id        = 0;
    bool focused   = false;
    bool iconified = false;
};
inline int keyboardWindow(const std::vector<WindowFocus>& floats) {
    for (const WindowFocus& f : floats)
        if (f.id > 0 && f.focused && !f.iconified) return f.id;
    return 0;
}

// 글 칸 · 숫자 칸은 그 칸이 있는 창에 키보드가 있을 때만 키 · 글자를 받습니다.
inline bool fieldTakesKeys(int fieldWindow, int keyboardWin) { return fieldWindow == keyboardWin; }

// 패널의 포인터 판정(끌기 · 누름 · 오른쪽 메뉴)은 그 패널이 있는 창의 포인터로만 합니다. 다른 창 좌표와 비교하면 위젯 좌표계가 달라
// 엉뚱한 자리가 맞습니다(M3a 의 panelActiveInMain 가드를 대신함).
inline bool panelGetsPointer(int panelWindow, int window) { return panelWindow == window; }

// 결정 D7: Play 중 떠 있는 창에 키보드(OS 포커스)가 있으면 게임 입력(키 · 마우스 시선 · 월드 패널 마우스)을 막습니다(UE PIE).
inline bool blocksGameInput(bool inPlay, int keyboardWin) { return inPlay && keyboardWin != 0; }

// 결정 D5: 모달 대화상자는 메인에 뜨고, 그동안 떠 있는 창은 입력을 받지 않습니다. 최소화된 창도 받지 않습니다.
inline bool floatTakesInput(bool modalOpen, bool iconified) { return !modalOpen && !iconified; }

// 창 사이 좌표: from 창 내용 좌표 p 를 to 창 내용 좌표로(두 창의 내용 영역 왼쪽 위 화면 좌표 차이). 결정 D1(모든 창이 메인 배율
// 하나)이라 배율은 곱하지 않습니다. 떠 있는 콘텐츠 브라우저에서 끈 에셋을 메인 뷰포트에 놓을 때 씁니다(OS 가 누른 창에 커서를
// 잡아 두므로 창 밖 좌표도 옵니다).
inline glm::vec2 toWindow(glm::vec2 p, glm::ivec2 fromOrigin, glm::ivec2 toOrigin) {
    return p + glm::vec2(fromOrigin - toOrigin);
}

// ── 시험 입력 대본(--ui-float-input) ─────────────────────────────────────────────────────────────
// 숨은 실행에서 떠 있는 창의 입력을 흉내 냅니다. 단계는 ';' 로 나누고, 한 단계가 한 프레임입니다:
//   <창>:move@x,y   <창>:down@x,y   <창>:up@x,y   <창>:rdown@x,y   <창>:rup@x,y   <창>:wheel@x,y=d
//   <창>:click@x,y          → down · up 두 프레임
//   <창>:drag@x,y>x2,y2     → down@시작 · move@가운데 · move@끝 · up@끝 네 프레임
//   <창>:focus · <창>:unfocus → 그 창에 OS 포커스가 있는 것처럼(키보드 창 고르기 · 앞뒤 순서 확인)
//   <창>:place@x,y          → 그 창 내용 영역을 화면 좌표 x,y 로 옮김(M4: 창 사이 끌기를 정해진 자리에서 흉내 냄)
//   <창>:key@이름           → 그 창에서 메뉴 키 한 번(H2b: up · down · left · right · enter · esc, 열린 메뉴가 그 창에 있을 때만)
//   wait                    → 한 프레임 쉼
// <창>은 0 = 메인(M4), 1.. = F 블록 번호입니다. 좌표는 그 창 내용 좌표입니다(누른 창이 커서를 잡아 두므로 창 밖 좌표도 됨).
// 버튼 · 자리는 다음 단계까지 그대로 남고 휠은 그 프레임만입니다. 메인 단계가 하나라도 있으면 그 실행의 메인 포인터는 대본 값입니다.
struct ScriptStep {
    enum class Kind { Move, Down, Up, RDown, RUp, Wheel, Focus, Unfocus, Place, Wait, Key };
    int         window = 0;
    Kind        kind   = Kind::Wait;
    glm::vec2   pos    { 0.f };
    bool        hasPos = false;
    float       value  = 0.f;
    std::string key;            // Key: 키 이름(H2b)
};

namespace detail {
inline bool parseXY(const std::string& s, glm::vec2& out) {
    const size_t c = s.find(',');
    if (c == std::string::npos || c == 0 || c + 1 >= s.size()) return false;
    char* e1 = nullptr;
    char* e2 = nullptr;
    const std::string a = s.substr(0, c), b = s.substr(c + 1);
    out.x = std::strtof(a.c_str(), &e1);
    out.y = std::strtof(b.c_str(), &e2);
    return e1 && *e1 == '\0' && e2 && *e2 == '\0';
}
}  // namespace detail

// 대본을 단계 목록으로 폅니다(click · drag 는 여러 단계로). 틀린 단계가 있으면 거짓과 그 단계 글을 err 에 남기고, 앞의 단계는 남깁니다.
inline bool parseScript(const std::string& text, std::vector<ScriptStep>& out, std::string& err) {
    using K = ScriptStep::Kind;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t semi = text.find(';', pos);
        const std::string one = text.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        pos = semi == std::string::npos ? text.size() + 1 : semi + 1;
        if (one.empty()) continue;
        if (one == "wait") { out.push_back({}); continue; }
        const size_t colon = one.find(':');
        if (colon == std::string::npos || colon == 0) { err = one; return false; }
        char* end = nullptr;
        const std::string idText = one.substr(0, colon);
        const long id = std::strtol(idText.c_str(), &end, 10);
        if (!end || *end != '\0' || id < 0) { err = one; return false; }
        std::string act = one.substr(colon + 1), arg;
        const size_t at = act.find('@');
        if (at != std::string::npos) { arg = act.substr(at + 1); act = act.substr(0, at); }
        ScriptStep st;
        st.window = (int)id;
        if (act == "focus" || act == "unfocus") {
            if (!arg.empty()) { err = one; return false; }
            st.kind = act == "focus" ? K::Focus : K::Unfocus;
            out.push_back(st);
            continue;
        }
        if (act == "key") {
            static const char* const kNames[] = { "up", "down", "left", "right", "enter", "esc" };
            bool known = false;
            for (const char* n : kNames) known = known || arg == n;
            if (!known) { err = one; return false; }
            st.kind = K::Key;
            st.key  = arg;
            out.push_back(st);
            continue;
        }
        if (act == "drag") {
            const size_t gt = arg.find('>');
            glm::vec2 a{}, b{};
            if (gt == std::string::npos || !detail::parseXY(arg.substr(0, gt), a) || !detail::parseXY(arg.substr(gt + 1), b)) {
                err = one;
                return false;
            }
            out.push_back({ (int)id, K::Down, a, true, 0.f });
            out.push_back({ (int)id, K::Move, (a + b) * 0.5f, true, 0.f });
            out.push_back({ (int)id, K::Move, b, true, 0.f });
            out.push_back({ (int)id, K::Up, b, true, 0.f });
            continue;
        }
        std::string xy = arg;
        if (act == "wheel") {
            const size_t eq = arg.find('=');
            if (eq == std::string::npos) { err = one; return false; }
            xy = arg.substr(0, eq);
            const std::string v = arg.substr(eq + 1);
            char* ve = nullptr;
            st.value = std::strtof(v.c_str(), &ve);
            if (v.empty() || !ve || *ve != '\0') { err = one; return false; }
        }
        if (!detail::parseXY(xy, st.pos)) { err = one; return false; }
        st.hasPos = true;
        if      (act == "move")  st.kind = K::Move;
        else if (act == "down")  st.kind = K::Down;
        else if (act == "up")    st.kind = K::Up;
        else if (act == "rdown") st.kind = K::RDown;
        else if (act == "rup")   st.kind = K::RUp;
        else if (act == "wheel") st.kind = K::Wheel;
        else if (act == "place") st.kind = K::Place;
        else if (act == "click") {
            st.kind = K::Down;
            out.push_back(st);
            st.kind = K::Up;
        } else { err = one; return false; }
        out.push_back(st);
    }
    return true;
}

// 단계 하나를 그 창의 흉내 값에 적용합니다. 휠은 그 프레임만(부르는 쪽이 프레임마다 scroll 을 0 으로 되돌린 뒤 적용).
// place 는 포인터 값을 바꾸지 않습니다(창 자리 옮기기는 부르는 쪽).
inline void applyStep(const ScriptStep& st, PointerSample& s, bool& focused) {
    using K = ScriptStep::Kind;
    if (st.hasPos && st.kind != K::Place) s.pos = st.pos;
    switch (st.kind) {
        case K::Down:    s.left  = true;  break;
        case K::Up:      s.left  = false; break;
        case K::RDown:   s.right = true;  break;
        case K::RUp:     s.right = false; break;
        case K::Wheel:   s.scroll = st.value; break;
        case K::Focus:   focused = true;  break;
        case K::Unfocus: focused = false; break;
        case K::Move:
        case K::Place:
        case K::Key:
        case K::Wait:    break;
    }
}

}  // namespace hwininput
