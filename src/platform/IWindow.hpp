#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include "../core/HLog.hpp"
#include "Gamepad.hpp"

// 키 입력 등 플랫폼 입력 로그. 키 이벤트는 Verbose 라 기본 기록 수준(Log)에서는 남지 않습니다.
inline constinit HLogCategory LogInput{ "Input", ELogVerbosity::Log };

// 키 · 버튼 상수(HInput::KEY_*)는 InputKeys.hpp 에 있습니다(H2b: 창 없이 쓰는 명령 표 · 시험이 Vulkan 머리글 없이 씀).
#include "InputKeys.hpp"

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual void init() = 0;
    virtual void cleanup() = 0;

    virtual bool shouldClose() const = 0;
    // 닫기 요청을 되돌리거나(false) 직접 닫습니다(true). 저장하지 않은 씬이 있으면 에디터가 닫기를 미루고
    // 확인 창을 띄운 뒤, 사용자가 고르면 다시 닫습니다(2026-10-15).
    virtual void setShouldClose(bool close) = 0;
    virtual void pollEvents() = 0;
    virtual void waitEvents() = 0;
    
    virtual void getFramebufferSize(int& width, int& height) const = 0;
    // Window size in screen coordinates (may differ from framebuffer on HiDPI)
    virtual void getWindowSize(int& width, int& height) const = 0;
    virtual void setTitle(const std::string& title) = 0;
    virtual bool wasResized() const = 0;
    virtual void resetResizedFlag() = 0;

    // ── 창 관리(다중 OS 창 H0, 2026-11-21, Docs/Editor_MultiWindow_Design.md) ───────────────────────
    // 에디터의 떠 있는 창(M2 ~ M4)이 씁니다. 모두 기본 구현이 있어 OS 창이 없는 구현(HeadlessWindow · AndroidWindow)은
    // 고치지 않아도 됩니다. 기본값은 "화면 하나를 꽉 채운, 늘 포커스가 있는 창" 입니다.
    // 내용 영역 왼쪽 위의 화면 좌표. 화면 좌표가 없는 창이면 0, 0 과 false.
    virtual bool  getWindowPos(int& x, int& y) const { x = y = 0; return false; }
    virtual void  setWindowPos(int /*x*/, int /*y*/) {}
    virtual void  setWindowSize(int /*w*/, int /*h*/) {}
    // 모니터 내용 배율(glfwGetWindowContentScale 의 x). 100 % 면 1.
    virtual float contentScale() const { return 1.f; }
    virtual bool  isFocused() const { return true; }
    virtual void  focus() {}
    virtual void  show(bool /*visible*/) {}
    virtual bool  isIconified() const { return false; }
    // 마우스가 이 창을 지나 아래 창으로 갑니다(GLFW 3.4 GLFW_MOUSE_PASSTHROUGH). 끌고 다니는 미리보기 창에 씁니다.
    virtual void  setMousePassthrough(bool /*enabled*/) {}

    // 이 창이 Vulkan 표면을 주는지(2026-10-31, 패키징 2단계). 거짓이면 createSurface 는 VK_NULL_HANDLE,
    // getRequiredInstanceExtensions 는 빈 목록, getFramebufferSize 는 0x0 이고, 렌더러는 표면 · 스왑체인 없이 XR 스왑체인
    // (또는 합성 눈)에만 그립니다. Quest 는 화면 표면을 만들지 않습니다(Khronos hello_xr · Meta 예제와 같음).
    virtual bool providesSurface() const = 0;
    virtual VkSurfaceKHR createSurface(VkInstance instance) = 0;
    virtual std::vector<const char*> getRequiredInstanceExtensions() const = 0;

    // Mouse input — consume-on-read (resets after each call)
    virtual void  getMouseDelta(float& dx, float& dy) = 0;
    virtual void  getRightMouseDelta(float& dx, float& dy) = 0;
    virtual float getScrollDelta() = 0;
    // 키·버튼 상태는 "지금 눌려 있거나, 직전 pollEvents 이후 눌린 적이 있음" 입니다. 한
    // 프레임 안에 눌렀다 뗀 입력(터치패드 탭, 낮은 FPS)도 그 프레임에는 눌린 것으로 보여,
    // 프레임마다 상태를 폴링하는 쪽이 놓치지 않습니다.
    virtual bool  isKeyDown(int key) const = 0;
    // 직전 pollEvents 이후 이 키의 누름과 OS 자동 반복이 몇 번 들어왔는지입니다. 누르고 있으면
    // 거듭 동작해야 하는 편집 키(백스페이스)가 씁니다(DEF-58). 반복 지연과 속도는 OS 설정을 따릅니다.
    virtual int   keyPressCount(int key) const = 0;
    virtual bool  isMouseButtonDown(int button) const = 0;
    virtual void  getMousePosition(float& x, float& y) const = 0;

    // 수정자 키는 좌우를 구분하지 않습니다(오른쪽 Ctrl 로도 단축키가 동작해야 합니다).
    bool isCtrlDown() const {
        return isKeyDown(HInput::KEY_LEFT_CTRL) || isKeyDown(HInput::KEY_RIGHT_CTRL);
    }
    bool isShiftDown() const {
        return isKeyDown(HInput::KEY_LEFT_SHIFT) || isKeyDown(HInput::KEY_RIGHT_SHIFT);
    }

    // Camera mode — caller decides when to activate based on viewport hit-test
    virtual void activateCameraMode()   = 0;
    virtual void deactivateCameraMode() = 0;
    virtual bool isCameraModeActive() const = 0;

    // Character input queue — drain once per frame for text editing
    virtual void drainCharQueue(std::vector<unsigned int>& out) = 0;

    // OS clipboard (text)
    virtual std::string getClipboardText() const = 0;
    virtual void setClipboardText(const std::string& text) = 0;

    // Cursor shape: 0=arrow, 1=hresize (↔), 2=vresize (↕), 3=move(탭을 놓으면 새 창, M4), 4=not allowed(놓아도 거부, M4).
    // 3 · 4 는 누른 창이 커서를 잡아 둔 동안 커서가 창 밖에 있어도 보여야 합니다(끄는 탭의 창 밖 미리보기).
    virtual void setCursorShape(int shape) = 0;

    // ── IME(글자 체계 T2, 2026-11-19, Docs/Text_System.md) ─────────────────────────────────────
    // 글 입력 칸에 포커스가 있을 때만 IME 를 켭니다. 꺼 두면 한글 모드에서도 W · E · R 같은 단축키가 IME 에 먹히지 않습니다
    // (UE 도 편집 칸이 없으면 IME 문맥을 떼어 냄). 확정된 글은 지금처럼 문자 큐(drainCharQueue)로 들어옵니다.
    virtual void setTextInputEnabled(bool /*enabled*/) {}
    virtual bool isTextInputEnabled() const { return false; }
    // IME 조합 중인 글(UTF-8)과 그 안의 커서(바이트). 조합 중이 아니면 거짓.
    virtual bool getComposition(std::string& /*text*/, size_t& /*caretBytes*/) const { return false; }
    // 조합 · 후보 창을 놓을 커서 사각형(프레임 버퍼 픽셀, x · y · 폭 · 높이).
    virtual void setTextInputRect(float /*x*/, float /*y*/, float /*w*/, float /*h*/) {}

    // ── 게임패드(2026-11-21, Docs/Input_Extensions.md D1 · D2) ───────────────────────────────────
    // 쓰는 패드 하나의 지금 상태를 채웁니다. 패드가 없거나 이 창이 읽지 않으면(창 없음 · Android) 연결 없음 상태로 false.
    virtual bool readGamepad(HGamepadState& out) { out = {}; return false; }
};
