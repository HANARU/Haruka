#pragma once
#include "../IWindow.hpp"
#include <string>

// Placeholder for Android NDK specific includes
// #include <android_native_app_glue.h>

class AndroidWindow : public IWindow {
public:
    AndroidWindow();
    ~AndroidWindow() override;

    void init() override;
    void cleanup() override;

    bool shouldClose() const override;
    void pollEvents() override;
    void waitEvents() override;
    
    void getFramebufferSize(int& width, int& height) const override;
    void getWindowSize(int& width, int& height) const override;
    void setTitle(const std::string& title) override {}
    bool wasResized() const override;
    void resetResizedFlag() override;

    VkSurfaceKHR createSurface(VkInstance instance) override;
    std::vector<const char*> getRequiredInstanceExtensions() const override;

    // ── Input stubs — Quest 입력은 OpenXR 컨트롤러로 들어오므로
    //    마우스/키보드/커서 계열은 no-op으로 인터페이스만 충족한다 ──────────────
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
    void  setCursorShape(int) override                        {}

private:
    bool initialized = false;
    bool resized = false;
    bool closing = false;
    int currentWidth = 1024;
    int currentHeight = 1024;
};
