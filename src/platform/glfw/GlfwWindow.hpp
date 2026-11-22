#pragma once
#include "../IWindow.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>

class GlfwWindow : public IWindow {
public:
    // aux = 보조 창(createAuxWindow, H0): 3D 뷰포트가 없는 UI 창이라 카메라 모드와 게임패드를 쓰지 않습니다.
    GlfwWindow(uint32_t width, uint32_t height, const std::string& title, bool hidden = false, bool aux = false);
    ~GlfwWindow() override;

    void init() override;
    void cleanup() override;

    bool shouldClose() const override;
    void setShouldClose(bool close) override;
    // 둘 다 GlfwPlatform::pumpEvents 를 부릅니다. 등록된 모든 창의 걸쇠를 비우고 모든 창의 이벤트를 받으므로, 창이 여럿이어도
    // 프레임마다 한 창(메인)에서 한 번만 부릅니다. 보조 창의 pollEvents 를 또 부르면 펌프가 두 번 돌아, 앞 펌프에서 걸린
    // 누름(한 프레임 안의 누름 · 뗌, DEF-82)과 반복 횟수(DEF-58)가 읽히기 전에 지워집니다.
    void pollEvents() override;
    void waitEvents() override;
    
    void getFramebufferSize(int& width, int& height) const override;
    void getWindowSize(int& width, int& height) const override;
    void setTitle(const std::string& title) override;
    bool wasResized() const override;
    void resetResizedFlag() override;

    // 창 관리(H0). 숨은 창도 OS 창은 있으므로 위치 · 크기 · 배율은 실제 값입니다.
    bool  getWindowPos(int& x, int& y) const override;
    void  setWindowPos(int x, int y) override;
    void  setWindowSize(int w, int h) override;
    float contentScale() const override;
    bool  isFocused() const override;
    void  focus() override;
    void  show(bool visible) override;
    bool  isIconified() const override;
    void  setMousePassthrough(bool enabled) override;

    bool providesSurface() const override { return true; }
    VkSurfaceKHR createSurface(VkInstance instance) override;
    std::vector<const char*> getRequiredInstanceExtensions() const override;

    void  getMouseDelta(float& dx, float& dy) override;
    void  getRightMouseDelta(float& dx, float& dy) override;
    float getScrollDelta() override;
    bool  isKeyDown(int key) const override;
    int   keyPressCount(int key) const override;
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

    // IME(글자 체계 T2, Win32 IMM). 숨은 창은 IME 를 켜지 않습니다(덤프가 결정적이도록).
    void setTextInputEnabled(bool enabled) override;
    bool isTextInputEnabled() const override { return imeEnabled_; }
    bool getComposition(std::string& text, size_t& caretBytes) const override;
    void setTextInputRect(float x, float y, float w, float h) override;
    bool readGamepad(HGamepadState& out) override;

private:
    friend class GlfwPlatform;
    // 걸쇠 · 누름 횟수를 비웁니다. GlfwPlatform::pumpEvents 가 glfwPollEvents 직전에 모든 창에 대해 부릅니다.
    void clearInputLatches();

    static void framebufferResizeCallback(GLFWwindow* w, int width, int height);
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double x, double y);
    static void scrollCallback(GLFWwindow* w, double xoff, double yoff);
    static void charCallback(GLFWwindow* w, unsigned int codepoint);

    uint32_t width;
    uint32_t height;
    std::string title;
    bool hidden_ = false;
    bool aux_    = false;
    bool platformAcquired_ = false;   // GlfwPlatform::acquire 가 성공했고 아직 release 하지 않음
    int  padJid_ = -1;   // 지금 쓰는 게임패드의 GLFW 조이스틱 번호(D2), 없으면 -1

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
    // 직전 pollEvents 이후 들어온 누름과 OS 자동 반복(GLFW_REPEAT)의 횟수입니다(DEF-58).
    uint8_t keyPresses_[GLFW_KEY_LAST + 1]        = {};

    // IME 상태(T2). Win32 창 프로시저를 감싸 WM_IME_* 를 받습니다(GLFW 3.4 에는 조합 글 API 가 없음).
    bool        imeEnabled_     = false;
    std::string composition_;          // UTF-8
    size_t      compositionCaret_ = 0; // composition_ 안의 바이트
    float       imeRect_[4]     = { -1.f, -1.f, -1.f, -1.f };
    void*       prevWndProc_    = nullptr;
    void        installImeHook();
    void        removeImeHook();
    void        applyImeRect();
#ifdef _WIN32
    static long long __stdcall imeWndProc(void* hwnd, unsigned int msg, unsigned long long wp, long long lp);
#endif

    GLFWcursor* cursorArrow_   = nullptr;
    GLFWcursor* cursorHResize_ = nullptr;
    GLFWcursor* cursorVResize_ = nullptr;
    GLFWcursor* cursorMove_    = nullptr;   // 3: 탭을 놓으면 새 창(M4)
    GLFWcursor* cursorNo_      = nullptr;   // 4: 놓아도 거부(M4)
    int         capturedShape_ = 0;         // 커서를 잡아 둔 동안 OS 커서에 직접 넣은 끌기 모양(0 = 없음)
};
