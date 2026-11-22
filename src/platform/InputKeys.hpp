#pragma once
// 플랫폼과 무관한 키 · 마우스 단추 값(GLFW · ASCII 와 같은 값이라 바꾸지 않고 씀). IWindow.hpp 가 이 파일을 include 합니다.
// H2b(2026-11-22): 에디터 명령 표(EditorCommandRegistry.hpp)와 그 시험이 창 · Vulkan 머리글 없이 키 값을 쓰도록 떼어 냄.

// Platform-agnostic input constants (values match GLFW/ASCII so no translation needed)
namespace HInput {
    constexpr int KEY_W = 87;
    constexpr int KEY_A = 65;
    constexpr int KEY_S = 83;
    constexpr int KEY_D = 68;
    constexpr int KEY_Q = 81;
    constexpr int KEY_E = 69;
    constexpr int KEY_R = 82;
    constexpr int KEY_Z = 90;
    constexpr int KEY_Y = 89;
    constexpr int KEY_C = 67;
    constexpr int KEY_V = 86;
    constexpr int KEY_F = 70;
    constexpr int KEY_N = 78;
    constexpr int KEY_O = 79;
    constexpr int KEY_X = 88;
    constexpr int KEY_RIGHT = 262;  // GLFW_KEY_RIGHT (글자 체계 T1: 편집 칸 커서)
    constexpr int KEY_LEFT  = 263;  // GLFW_KEY_LEFT
    constexpr int KEY_HOME  = 268;  // GLFW_KEY_HOME
    constexpr int KEY_END   = 269;  // GLFW_KEY_END
    constexpr int KEY_LEFT_CTRL   = 341;  // GLFW_KEY_LEFT_CONTROL
    constexpr int KEY_LEFT_SHIFT  = 340;  // GLFW_KEY_LEFT_SHIFT
    constexpr int KEY_RIGHT_CTRL  = 345;  // GLFW_KEY_RIGHT_CONTROL
    constexpr int KEY_RIGHT_SHIFT = 344;  // GLFW_KEY_RIGHT_SHIFT
    constexpr int KEY_DELETE    = 261;  // GLFW_KEY_DELETE
    constexpr int KEY_F2        = 291;  // GLFW_KEY_F2
    constexpr int KEY_BACKSPACE = 259;  // GLFW_KEY_BACKSPACE
    constexpr int KEY_ENTER     = 257;
    constexpr int KEY_UP        = 265;  // GLFW_KEY_UP
    constexpr int KEY_DOWN      = 264;  // GLFW_KEY_DOWN
    constexpr int KEY_ESCAPE    = 256;  // GLFW_KEY_ESCAPE  // GLFW_KEY_ENTER
    constexpr int MOUSE_BUTTON_LEFT  = 0;
    constexpr int MOUSE_BUTTON_RIGHT = 1;
}
