#pragma once
#include "../IWindow.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>

class GlfwWindow : public IWindow {
public:
    GlfwWindow(uint32_t width, uint32_t height, const std::string& title);
    ~GlfwWindow() override;

    void init() override;
    void cleanup() override;

    bool shouldClose() const override;
    void pollEvents() override;
    void waitEvents() override;
    
    void getFramebufferSize(int& width, int& height) const override;
    void getWindowSize(int& width, int& height) const override;
    void setTitle(const std::string& title) override;
    bool wasResized() const override;
    void resetResizedFlag() override;

    VkSurfaceKHR createSurface(VkInstance instance) override;
    std::vector<const char*> getRequiredInstanceExtensions() const override;

    void  getMouseDelta(float& dx, float& dy) override;
    void  getRightMouseDelta(float& dx, float& dy) override;
    float getScrollDelta() override;
    bool  isKeyDown(int key) const override;
    bool  isMouseButtonDown(int button) const override;
    void  getMousePosition(float& x, float& y) const override;

    void activateCameraMode()   override;
    void deactivateCameraMode() override;
    bool isCameraModeActive() const override { return cameraMode_; }

    void drainCharQueue(std::vector<unsigned int>& out) override;
    void setCursorShape(int shape) override;

    std::string getClipboardText() const override;
    void setClipboardText(const std::string& text) override;

    GLFWwindow* getNativeWindow() const { return window; }

private:
    static void framebufferResizeCallback(GLFWwindow* w, int width, int height);
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double x, double y);
    static void scrollCallback(GLFWwindow* w, double xoff, double yoff);
    static void charCallback(GLFWwindow* w, unsigned int codepoint);

    uint32_t width;
    uint32_t height;
    std::string title;

    GLFWwindow* window  = nullptr;
    bool        resized = false;

    float  mouseDX        = 0.0f;
    float  mouseDY        = 0.0f;
    float  rMouseDX       = 0.0f;
    float  rMouseDY       = 0.0f;
    float  scrollAccum    = 0.0f;
    double lastMouseX     = 0.0;
    double lastMouseY     = 0.0;
    double rLastMouseX    = 0.0;
    double rLastMouseY    = 0.0;
    double capturedMouseX = 0.0;
    double capturedMouseY = 0.0;
    bool   mouseLeftDown  = false;
    bool   mouseRightDown = false;
    bool   firstMouse     = true;
    bool   rFirstMouse    = true;
    bool   cameraMode_    = false;

    std::vector<unsigned int> charQueue_;

    // 직전 pollEvents 이후 눌린 적이 있는 키·버튼. 폴링만으로는 한 프레임 안에 눌렀다 뗀
    // 입력이 사라지므로(DEF-82), 누름 이벤트를 여기 걸어 두었다가 다음 pollEvents 에서 비웁니다.
    bool keyLatch_[GLFW_KEY_LAST + 1]             = {};
    bool buttonLatch_[GLFW_MOUSE_BUTTON_LAST + 1] = {};

    GLFWcursor* cursorArrow_   = nullptr;
    GLFWcursor* cursorHResize_ = nullptr;
    GLFWcursor* cursorVResize_ = nullptr;
};
