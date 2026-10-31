#include "AndroidWindow.hpp"

#ifdef __ANDROID__
#include <android/looper.h>
#include <android_native_app_glue.h>
#endif

AndroidWindow::AndroidWindow(void* androidApp) : app(androidApp) {}

AndroidWindow::~AndroidWindow() {
    cleanup();
}

void AndroidWindow::init() {
    initialized = true;
}

void AndroidWindow::cleanup() {
    initialized = false;
}

bool AndroidWindow::shouldClose() const {
#ifdef __ANDROID__
    if (app && static_cast<android_app*>(app)->destroyRequested) return true;
#endif
    return closing;
}

// 루퍼에 쌓인 이벤트를 기다리지 않고 모두 처리합니다. 명령(RESUME · PAUSE · 창 생성 · 종료)은 android_main 이 둔 onAppCmd 가
// 받습니다. ALooper_pollAll 은 최근 NDK 에서 쓸 수 없어 pollOnce 를 되풀이합니다.
void AndroidWindow::pollEvents() {
#ifdef __ANDROID__
    auto* a = static_cast<android_app*>(app);
    if (!a) return;
    for (int i = 0; i < 64 && !a->destroyRequested; ++i) {
        int events = 0;
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source)) < 0) break;
        if (source) source->process(a, source);
    }
#endif
}

void AndroidWindow::waitEvents() {
    pollEvents();
}

// 표면이 없으므로 0x0 입니다. 엔진 루프는 최소화와 같은 경로로 시뮬레이션 · 소리 · XR 만 돌립니다(DEF-106).
void AndroidWindow::getFramebufferSize(int& width, int& height) const {
    width = 0;
    height = 0;
}

void AndroidWindow::getWindowSize(int& width, int& height) const {
    width = 0;
    height = 0;
}

bool AndroidWindow::wasResized() const {
    return resized;
}

void AndroidWindow::resetResizedFlag() {
    resized = false;
}

// 화면 표면을 만들지 않습니다(providesSurface 거짓). 렌더러는 이 함수를 부르지 않습니다.
VkSurfaceKHR AndroidWindow::createSurface(VkInstance) {
    return VK_NULL_HANDLE;
}

std::vector<const char*> AndroidWindow::getRequiredInstanceExtensions() const {
    return {};
}
