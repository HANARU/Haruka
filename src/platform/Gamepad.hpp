#pragma once
// 데스크톱 게임패드 상태(2026-11-21, Docs/Input_Extensions.md D1 · D2).
//
// 플랫폼 층(창)이 장치를 읽어 채우고, 입력 층(HInputSystem::setGamepad)이 동작 · 축으로 바꿉니다. 시험은 이 구조체를 직접 채워
// 가짜 장치로 씁니다. 배치는 GLFW 3.4 의 게임패드 표준 매핑(SDL 게임 컨트롤러 DB)과 같은 순서입니다.
// 값 규칙: 스틱은 -1 ~ 1 이고 **Y 는 위가 +**(XR 엄지 스틱과 같음), 트리거는 0 ~ 1. GLFW 원래 값(Y 아래가 +, 트리거 -1 ~ 1)은
// 창 구현이 바꿉니다.

#include <cstdint>
#include <string>

// 면 버튼 이름은 Xbox 배치입니다(아래 = A). 순서는 GLFW_GAMEPAD_BUTTON_* 와 같습니다.
enum class HPadButton : uint8_t {
    A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide, LeftThumb, RightThumb,
    DpadUp, DpadRight, DpadDown, DpadLeft,
    Count,
    // 트리거를 버튼처럼 쓸 때(절반 넘게 당기면 눌림). HGamepadState::buttons 에는 없습니다.
    LeftTrigger = Count, RightTrigger,
};

// 순서는 GLFW_GAMEPAD_AXIS_* 와 같습니다.
enum class HPadAxis : uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

struct HGamepadState {
    bool        connected = false;
    std::string name;                                   // 장치 이름(로그용)
    bool        buttons[size_t(HPadButton::Count)] = {};
    float       axes[size_t(HPadAxis::Count)]      = {};
};

namespace hgamepad {

constexpr int kMaxPads = 16;   // GLFW_JOYSTICK_LAST + 1

// 쓸 패드를 고릅니다(D2). current 가 아직 게임패드로 붙어 있으면 그대로, 아니면 번호가 가장 작은 게임패드, 없으면 -1.
// isPad[i] 는 i 번 조이스틱이 붙어 있고 게임패드 매핑이 있는지입니다.
inline int pick(int current, const bool (&isPad)[kMaxPads]) {
    if (current >= 0 && current < kMaxPads && isPad[current]) return current;
    for (int i = 0; i < kMaxPads; ++i)
        if (isPad[i]) return i;
    return -1;
}

} // namespace hgamepad
