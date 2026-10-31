#pragma once
#include "../IWindow.hpp"
#include <string>

// Quest 는 화면 표면을 만들지 않고 XR 스왑체인에만 그립니다(Packaging_Design.md 2절). 그래서 이 창은 표면 · 인스턴스 확장을
// 주지 않고 크기도 0x0 입니다(2026-10-31, 창 없는 렌더 경로). 엔진 루프는 최소화와 같은 경로로 시뮬레이션 · 소리 · XR 만 돌고,
// XR 프레임이 없으면 쉬어 갑니다.
//
// 수명주기(2026-10-31, 패키징 3단계): android_main(AndroidMain.cpp)이 android_native_app_glue 의 android_app 을 넘깁니다.
// pollEvents 가 그 루퍼를 기다리지 않고 비우며(명령은 진입점이 둔 처리기가 받음), 시스템이 액티비티를 끝내라고 하면
// (destroyRequested) 닫힙니다. 헤드셋을 벗고 쓰는 일 · 대시보드 종료는 XR 세션 상태가 맡습니다. PC 시험은 app 없이 만들어
// 표면 없는 창의 약속만 검사합니다(이 파일은 NDK 헤더를 __ANDROID__ 안에서만 씀).
class AndroidWindow : public IWindow {
public:
    explicit AndroidWindow(void* androidApp = nullptr);   // android_app*
    ~AndroidWindow() override;

    void init() override;
    void cleanup() override;

    bool shouldClose() const override;
    void setShouldClose(bool close) override { closing = close; }
    void pollEvents() override;
    void waitEvents() override;
    
    void getFramebufferSize(int& width, int& height) const override;
    void getWindowSize(int& width, int& height) const override;
    void setTitle(const std::string&) override {}
    bool wasResized() const override;
    void resetResizedFlag() override;

    bool providesSurface() const override { return false; }
    VkSurfaceKHR createSurface(VkInstance) override;
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

    // 시스템 클립보드는 쓰지 않고 이 창 안에서만 왕복합니다(DEF-103: 예전에는 구현이 없어 추상 클래스였음).
    std::string getClipboardText() const override               { return clipboard; }
    void        setClipboardText(const std::string& t) override { clipboard = t; }

private:
    void* app = nullptr;   // android_app*
    bool initialized = false;
    bool resized = false;
    bool closing = false;
    std::string clipboard;
};
