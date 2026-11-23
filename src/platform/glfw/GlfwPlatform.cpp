#include "GlfwPlatform.hpp"
#include "GlfwWindow.hpp"
#include "../PlatformAlert.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <volk.h>
#include <algorithm>
#include <exception>
#include <vector>

namespace {
// GLFW 라이브러리 전체에 하나인 상태입니다(헤더 주석). 파일 밖에서는 GlfwPlatform 의 함수로만 닿습니다.
struct GlfwState {
    int                      refs = 0;
    std::vector<GlfwWindow*> windows;
};
GlfwState g_glfw;
} // namespace

bool GlfwPlatform::acquire() {
    if (g_glfw.refs == 0) {
        if (!vkGetInstanceProcAddr) volkInitialize();
        if (vkGetInstanceProcAddr) glfwInitVulkanLoader(vkGetInstanceProcAddr);
        if (glfwInit() != GLFW_TRUE) return false;
    }
    ++g_glfw.refs;
    return true;
}

void GlfwPlatform::release() {
    if (g_glfw.refs <= 0) return;
    if (--g_glfw.refs == 0) {
        // 등록이 남아 있으면 창이 release 보다 늦게 사라지는 순서 오류입니다. glfwTerminate 가 남은 창을 모두 없애므로
        // 목록도 비워 다음 펌프가 죽은 포인터를 보지 않게 합니다.
        g_glfw.windows.clear();
        glfwTerminate();
    }
}

void GlfwPlatform::pumpEvents(bool wait) {
    // 걸쇠는 이번 펌프에서 들어올 이벤트만 담아야 합니다. 모든 창을 먼저 비운 다음 받습니다(DEF-82).
    for (GlfwWindow* w : g_glfw.windows) w->clearInputLatches();
    if (g_glfw.refs == 0) return;
    if (wait) glfwWaitEvents();
    else      glfwPollEvents();
}

bool GlfwPlatform::isInitialized() { return g_glfw.refs > 0; }

int GlfwPlatform::windowCount() { return (int)g_glfw.windows.size(); }

std::vector<hwinplace::Rect> GlfwPlatform::monitorWorkAreas() {
    std::vector<hwinplace::Rect> out;
    if (g_glfw.refs <= 0) return out;
    int count = 0;
    GLFWmonitor** mons = glfwGetMonitors(&count);
    for (int i = 0; mons && i < count; ++i) {
        hwinplace::Rect r;
        glfwGetMonitorWorkarea(mons[i], &r.x, &r.y, &r.w, &r.h);
        if (r.w > 0 && r.h > 0) out.push_back(r);
    }
    return out;
}

void GlfwPlatform::addWindow(GlfwWindow* w) {
    if (std::find(g_glfw.windows.begin(), g_glfw.windows.end(), w) == g_glfw.windows.end()) g_glfw.windows.push_back(w);
}

void GlfwPlatform::removeWindow(GlfwWindow* w) {
    g_glfw.windows.erase(std::remove(g_glfw.windows.begin(), g_glfw.windows.end(), w), g_glfw.windows.end());
}

std::unique_ptr<IWindow> createAuxWindow(int width, int height, const std::string& title, bool hidden) {
    auto win = std::make_unique<GlfwWindow>((uint32_t)std::max(width, 1), (uint32_t)std::max(height, 1), title, hidden,
                                            /*aux=*/true);
    try {
        win->init();
    } catch (const std::exception& e) {
        HLOG(LogInput, Warning) << "Aux window '" << title << "' could not be created: " << e.what();
        return nullptr;
    }
    return win;
}

namespace hplat {

void showFatalError(const char* title, const char* message) {
#ifdef _WIN32
    auto toWide = [](const char* s) -> std::wstring {
        if (!s || !*s) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if (n <= 0) return {};
        std::wstring out((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
        if (!out.empty() && out.back() == L'\0') out.pop_back();
        return out;
    };
    const std::wstring wTitle = toWide(title ? title : "Haruka Engine");
    const std::wstring wMsg   = toWide(message ? message : "");
    MessageBoxW(nullptr, wMsg.c_str(), wTitle.c_str(), MB_OK | MB_ICONERROR);
#else
    (void)title;
    (void)message;
#endif
}

} // namespace hplat
