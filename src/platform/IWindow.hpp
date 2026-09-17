#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include "../core/HLog.hpp"

// 키 입력 등 플랫폼 입력 로그. 키 이벤트는 Verbose 라 기본 기록 수준(Log)에서는 남지 않습니다.
inline constinit HLogCategory LogInput{ "Input", ELogVerbosity::Log };

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
    constexpr int KEY_LEFT_CTRL  = 341;  // GLFW_KEY_LEFT_CONTROL
    constexpr int KEY_LEFT_SHIFT = 340;  // GLFW_KEY_LEFT_SHIFT
    constexpr int KEY_DELETE    = 261;  // GLFW_KEY_DELETE
    constexpr int KEY_F2        = 291;  // GLFW_KEY_F2
    constexpr int KEY_BACKSPACE = 259;  // GLFW_KEY_BACKSPACE
    constexpr int KEY_ENTER     = 257;
    constexpr int KEY_ESCAPE    = 256;  // GLFW_KEY_ESCAPE  // GLFW_KEY_ENTER
    constexpr int MOUSE_BUTTON_LEFT  = 0;
    constexpr int MOUSE_BUTTON_RIGHT = 1;
}

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual void init() = 0;
    virtual void cleanup() = 0;

    virtual bool shouldClose() const = 0;
    virtual void pollEvents() = 0;
    virtual void waitEvents() = 0;
    
    virtual void getFramebufferSize(int& width, int& height) const = 0;
    // Window size in screen coordinates (may differ from framebuffer on HiDPI)
    virtual void getWindowSize(int& width, int& height) const = 0;
    virtual void setTitle(const std::string& title) = 0;
    virtual bool wasResized() const = 0;
    virtual void resetResizedFlag() = 0;

    virtual VkSurfaceKHR createSurface(VkInstance instance) = 0;
    virtual std::vector<const char*> getRequiredInstanceExtensions() const = 0;

    // Mouse input — consume-on-read (resets after each call)
    virtual void  getMouseDelta(float& dx, float& dy) = 0;
    virtual void  getRightMouseDelta(float& dx, float& dy) = 0;
    virtual float getScrollDelta() = 0;
    virtual bool  isKeyDown(int key) const = 0;
    virtual bool  isMouseButtonDown(int button) const = 0;
    virtual void  getMousePosition(float& x, float& y) const = 0;

    // Camera mode — caller decides when to activate based on viewport hit-test
    virtual void activateCameraMode()   = 0;
    virtual void deactivateCameraMode() = 0;
    virtual bool isCameraModeActive() const = 0;

    // Character input queue — drain once per frame for text editing
    virtual void drainCharQueue(std::vector<unsigned int>& out) = 0;

    // OS clipboard (text)
    virtual std::string getClipboardText() const = 0;
    virtual void setClipboardText(const std::string& text) = 0;

    // Cursor shape: 0=arrow, 1=hresize (↔), 2=vresize (↕)
    virtual void setCursorShape(int shape) = 0;
};
