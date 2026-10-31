#pragma once
#include "../IWindow.hpp"
#include <string>

// ── 창 없는 IWindow (패키징 2단계, 2026-10-31) ──────────────────────────────────────────────────
// OS 창 없이 엔진을 돌립니다(게임 실행 파일의 --headless). 표면을 주지 않으므로 렌더러는 스왑체인 없이 XR 스왑체인이나
// 합성 눈(--synthetic-eyes)에만 그립니다. 크기가 0x0 이라 엔진 루프는 최소화와 같은 경로(DEF-106)를 타서 에디터 UI
// 배치를 부르지 않고 시뮬레이션 · 소리 · XR 만 돌립니다. 입력은 모두 비어 있습니다(XR 입력은 OpenXR 로 따로 들어옴).
// GLFW 를 쓰지 않아 Android 에서도 컴파일됩니다.
class HeadlessWindow : public IWindow {
public:
    void init() override {}
    void cleanup() override {}

    bool shouldClose() const override        { return closing_; }
    void setShouldClose(bool close) override { closing_ = close; }
    void pollEvents() override {}
    void waitEvents() override {}

    void getFramebufferSize(int& width, int& height) const override { width = 0; height = 0; }
    void getWindowSize(int& width, int& height) const override      { width = 0; height = 0; }
    void setTitle(const std::string&) override {}
    bool wasResized() const override { return false; }
    void resetResizedFlag() override {}

    bool providesSurface() const override                                   { return false; }
    VkSurfaceKHR createSurface(VkInstance) override                          { return VK_NULL_HANDLE; }
    std::vector<const char*> getRequiredInstanceExtensions() const override { return {}; }

    void  getMouseDelta(float& dx, float& dy) override        { dx = 0.f; dy = 0.f; }
    void  getRightMouseDelta(float& dx, float& dy) override   { dx = 0.f; dy = 0.f; }
    float getScrollDelta() override                           { return 0.f; }
    bool  isKeyDown(int) const override                       { return false; }
    int   keyPressCount(int) const override                   { return 0; }
    bool  isMouseButtonDown(int) const override               { return false; }
    void  getMousePosition(float& x, float& y) const override { x = 0.f; y = 0.f; }
    void  activateCameraMode() override                       {}
    void  deactivateCameraMode() override                     {}
    bool  isCameraModeActive() const override                 { return false; }
    void  drainCharQueue(std::vector<unsigned int>& out) override { out.clear(); }

    // 시스템 클립보드가 없으므로 이 창 안에서만 왕복합니다.
    std::string getClipboardText() const override            { return clipboard_; }
    void        setClipboardText(const std::string& t) override { clipboard_ = t; }
    void        setCursorShape(int) override                 {}

private:
    bool        closing_ = false;
    std::string clipboard_;
};
