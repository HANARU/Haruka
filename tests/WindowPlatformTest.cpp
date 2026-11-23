// GLFW 플랫폼 수명 · 이벤트 펌프 시험(다중 OS 창 H0, 2026-11-21, Docs/Editor_MultiWindow_Design.md).
//
// 이 시험이 존재하는 이유: 예전에는 창 객체가 glfwInit · glfwTerminate 를 직접 불러, 창을 하나 더 만들었다가 닫으면 GLFW 가
// 통째로 끝났습니다. 또 창마다 자기 걸쇠만 비우고 전역 펌프를 불러, 창이 둘이면 한 창의 걸쇠가 비워지지 않거나 이벤트가 들어온
// 뒤에 비워졌습니다(DEF-82 재발). 숨은 창 둘로 수명 · 펌프 · 창 관리 함수를 확인합니다. 창 시스템이 없어 glfwInit 이 실패하면
// 실패가 아니라 건너뜀입니다.
//
// 펌프 시험은 Win32 키 메시지를 숨은 창에 직접 넣습니다(GLFW 창 프로시저 → 키 콜백 → 누름 횟수). 다른 OS 에서는 그 부분만
// 건너뜁니다.
//
// 떠 있는 창 자리 끼우기(M3a, hwinplace::clampToWorkAreas)는 OS 없이 도는 순수 계산이라 창 시스템이 없어도 먼저 확인합니다.
//
// 실행: build/Release/HarukaWindowTest.exe

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "../src/platform/glfw/GlfwPlatform.hpp"
#include "../src/platform/glfw/GlfwWindow.hpp"
#include "../src/platform/WindowPlacement.hpp"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

// GLFW 가 초기화되지 않았을 때 부르면 GLFW_NOT_INITIALIZED 오류가 납니다. glfwGetError 는 초기화 없이도 됩니다.
bool glfwLooksTerminated() {
    glfwGetError(nullptr);                 // 앞의 오류를 비움
    (void)glfwGetPrimaryMonitor();
    return glfwGetError(nullptr) == GLFW_NOT_INITIALIZED;
}

bool sameRect(const hwinplace::Rect& a, int x, int y, int w, int h) {
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

// 모니터 작업 영역 끼우기(M3a). 모니터 둘: 왼쪽 0,0 1920x1040(작업 표시줄 40 제외), 오른쪽 1920,-200 1280x1024(위로 어긋남).
void testWorkAreaClamp() {
    using hwinplace::Rect;
    using hwinplace::clampToWorkAreas;
    std::printf("\n== 작업 영역 끼우기 ==\n");
    const std::vector<Rect> two = { { 0, 0, 1920, 1040 }, { 1920, -200, 1280, 1024 } };
    const int T = hwinplace::kTitleBarPx;

    check(sameRect(clampToWorkAreas({ -5000, -3000, 400, 700 }, {}), -5000, -3000, 400, 700),
          "영역이 없으면(모니터 정보 없음) 그대로");
    check(sameRect(clampToWorkAreas({ 120, 80, 420, 640 }, two), 120, 80, 420, 640), "첫 모니터 안의 창은 그대로");
    check(sameRect(clampToWorkAreas({ 2000, 100, 600, 400 }, two), 2000, 100, 600, 400),
          "둘째 모니터(음수 y 영역) 안의 창도 그대로");
    check(sameRect(clampToWorkAreas({ 1700, 300, 600, 400 }, two), 1700, 300, 600, 400),
          "두 모니터에 걸친 창은 제목 줄이 충분히 보이면 옮기지 않음");
    // 제목 줄이 첫 모니터 위로 넘어감(y = 10 이면 제목 줄 띠는 -22 ~ 10) → 아래로 내려 제목 줄이 영역 안.
    check(sameRect(clampToWorkAreas({ 300, 10, 400, 300 }, two), 300, T, 400, 300),
          "제목 줄이 화면 위로 잘리면 제목 줄 높이만큼 내림");
    // 화면 밖(모니터를 뺀 뒤): 가장 가까운 모니터로.
    check(sameRect(clampToWorkAreas({ -5000, -3000, 400, 700 }, two), 0, T, 400, 700),
          "모든 모니터 밖이면 가장 가까운 모니터의 모서리로 옮김");
    check(sameRect(clampToWorkAreas({ 9000, 500, 400, 300 }, two), 3200 - 400, 500, 400, 300),
          "오른쪽 멀리 밖이면 오른쪽 모니터 안 오른쪽 끝으로");
    // 제목 줄이 가로로 조금만(64 픽셀 미만) 걸치면 그 모니터 안으로 옮김.
    check(sameRect(clampToWorkAreas({ 1900, 900, 300, 100 }, { { 0, 0, 1920, 1040 } }), 1920 - 300, 900, 300, 100),
          "제목 줄이 20 픽셀만 보이면 영역 안으로 당김");
    // 크기가 영역보다 큼: 줄이고 영역 안으로.
    const Rect big = clampToWorkAreas({ 100, 100, 5000, 3000 }, two);
    check(big.w == 1920 && big.h == 1040 - T && big.x == 0 && big.y == T,
          "영역보다 큰 창은 영역 크기로 줄임(제목 줄 몫 포함)");
    const Rect bigOff = clampToWorkAreas({ -9000, 50, 3000, 2000 }, two);
    check(bigOff.w == 1920 && bigOff.h == 1040 - T && bigOff.x == 0 && bigOff.y == T,
          "화면 밖의 큰 창도 줄이고 옮김");
    check(clampToWorkAreas({ 10, 50, 0, -5 }, two).w >= 1 && clampToWorkAreas({ 10, 50, 0, -5 }, two).h >= 1,
          "크기 0 · 음수는 1 이상으로");
}

#ifdef _WIN32
// A 키의 누름과 뗌을 한 번에 넣습니다(한 프레임 안의 누름 · 뗌, DEF-82). 스캔 코드 0x1E = A.
void postKeyTap(IWindow& w) {
    HWND hwnd = glfwGetWin32Window(static_cast<GlfwWindow&>(w).getNativeWindow());
    const LPARAM down = 1 | (0x1E << 16);
    const LPARAM up   = (LPARAM)(1 | (0x1E << 16) | (1u << 30) | (1u << 31));
    PostMessageW(hwnd, WM_KEYDOWN, 'A', down);
    PostMessageW(hwnd, WM_KEYUP, 'A', up);
}
#endif

void testPump(IWindow& main, IWindow& aux) {
    std::printf("\n== 펌프 ==\n");
#ifdef _WIN32
    // 앞 시험에서 남은 메시지를 비웁니다.
    main.pollEvents();
    postKeyTap(main);
    postKeyTap(aux);
    main.pollEvents();   // 메인 창의 펌프 한 번이 두 창의 이벤트를 모두 받습니다
    check(main.keyPressCount(GLFW_KEY_A) == 1, "메인 창: 펌프 한 번 안에 눌렀다 뗀 키가 누름 횟수에 남음");
    check(aux.keyPressCount(GLFW_KEY_A) == 1, "보조 창: 메인 창의 펌프로 이벤트를 받음");
    main.pollEvents();   // 새 이벤트 없이 한 번 더
    check(main.keyPressCount(GLFW_KEY_A) == 0, "다음 펌프에서 메인 창 걸쇠가 비워짐");
    check(aux.keyPressCount(GLFW_KEY_A) == 0, "다음 펌프에서 보조 창 걸쇠도 비워짐(펌프 한 번에 모든 창)");

    postKeyTap(aux);
    aux.waitEvents();    // 기다리는 펌프도 같은 규칙(이미 메시지가 있어 곧바로 돌아옴)
    check(aux.keyPressCount(GLFW_KEY_A) == 1, "waitEvents 도 비운 다음 받음");
    main.pollEvents();
    check(aux.keyPressCount(GLFW_KEY_A) == 0, "waitEvents 로 받은 누름도 다음 펌프에서 비워짐");
#else
    (void)main; (void)aux;
    std::printf("  [SKIP] Win32 가 아니라 키 메시지를 넣지 못함\n");
#endif
}

void testWindowOps(IWindow& w, const char* name) {
    std::printf("\n== 창 관리 (%s) ==\n", name);
    glfwGetError(nullptr);     // 앞 시험의 오류를 비움(이 묶음의 오류만 봄)
    int x = -1, y = -1;
    check(w.getWindowPos(x, y), std::string(name) + ": getWindowPos 가 화면 좌표를 돌려줌");
    w.setWindowPos(x + 16, y + 8);
    int x2 = 0, y2 = 0;
    check(w.getWindowPos(x2, y2), std::string(name) + ": setWindowPos 뒤에도 위치를 읽음");
    w.setWindowSize(320, 200);
    int ww = 0, wh = 0;
    w.getWindowSize(ww, wh);
    check(ww > 0 && wh > 0, std::string(name) + ": setWindowSize 뒤 창 크기가 양수");
    const float s = w.contentScale();
    check(s > 0.f && s < 8.f, std::string(name) + ": contentScale 이 양수(" + std::to_string(s) + ")");
    check(!w.isIconified(), std::string(name) + ": 최소화되지 않음");
    w.focus();                 // 숨은 창은 포커스를 가져가지 않음(오류 없이 돌아옴)
    w.show(true);              // 숨은 창은 보이지 않음
    w.setMousePassthrough(true);
    w.setMousePassthrough(false);
    GLFWwindow* g = static_cast<GlfwWindow&>(w).getNativeWindow();
    check(glfwGetWindowAttrib(g, GLFW_VISIBLE) == GLFW_FALSE, std::string(name) + ": 숨은 창은 show(true) 뒤에도 보이지 않음");
    check(glfwGetError(nullptr) == GLFW_NO_ERROR, std::string(name) + ": 창 관리 함수가 GLFW 오류를 내지 않음");
    float mx = 0.f, my = 0.f;
    w.getMousePosition(mx, my);
    check(mx < -1000.f && my < -1000.f, std::string(name) + ": 숨은 창의 마우스는 창 밖 고정(덤프 결정성)");
}

} // namespace

int main() {
    std::printf("GLFW 플랫폼 수명 · 펌프 시험\n");
    testWorkAreaClamp();
    check(GlfwPlatform::monitorWorkAreas().empty(), "GLFW 가 켜지기 전에는 작업 영역 목록이 빔(끼우지 않음)");
    if (!GlfwPlatform::acquire()) {
        std::printf("  [SKIP] glfwInit 실패(창 시스템 없음)\n\n결과: PASS (건너뜀)\n");
        return 0;
    }
    GlfwPlatform::release();
    check(!GlfwPlatform::isInitialized(), "acquire · release 한 쌍 뒤에는 GLFW 가 끝남");

    std::printf("\n== 수명 ==\n");
    auto main = std::make_unique<GlfwWindow>(256, 128, "H0 main", /*hidden=*/true);
    main->init();
    check(GlfwPlatform::isInitialized() && GlfwPlatform::windowCount() == 1, "메인 창을 만들면 GLFW 가 켜지고 창 1개 등록");
    {
        auto aux = createAuxWindow(200, 100, "H0 aux", /*hidden=*/true);
        check(aux != nullptr, "createAuxWindow 가 숨은 보조 창을 만듦");
        if (!aux) { std::printf("\n결과: FAIL (실패 %d)\n", g_failed); return 1; }
        check(GlfwPlatform::windowCount() == 2, "보조 창도 등록됨(창 2개)");
        GLFWwindow* g = static_cast<GlfwWindow&>(*aux).getNativeWindow();
        check(glfwGetWindowAttrib(g, GLFW_CLIENT_API) == GLFW_NO_API, "보조 창도 메인 창과 같은 힌트(GLFW_NO_API)");
        check(glfwGetWindowAttrib(g, GLFW_VISIBLE) == GLFW_FALSE, "숨은 보조 창은 보이지 않음");

        {
            const auto areas = GlfwPlatform::monitorWorkAreas();
            bool ok = true;
            for (const auto& a : areas) ok = ok && a.w > 0 && a.h > 0;
            check(ok, "모니터 작업 영역은 모두 넓이가 있음(" + std::to_string(areas.size()) + "개)");
        }
        testPump(*main, *aux);
        testWindowOps(*main, "메인");
        testWindowOps(*aux, "보조");

        std::printf("\n== 보조 창 닫기 ==\n");
        aux->cleanup();
        aux->cleanup();   // 두 번 불러도 참조 수가 두 번 줄지 않음
        check(GlfwPlatform::windowCount() == 1, "보조 창을 닫으면 창 1개");
    }   // 소멸자의 cleanup 도 한 번 더
    check(GlfwPlatform::isInitialized(), "보조 창을 닫아도 GLFW 가 켜져 있음");
    GLFWwindow* g = main->getNativeWindow();
    glfwGetError(nullptr);
    check(glfwGetWindowAttrib(g, GLFW_CLIENT_API) == GLFW_NO_API && glfwGetError(nullptr) == GLFW_NO_ERROR,
          "보조 창을 닫은 뒤에도 메인 창 속성을 오류 없이 읽음");
    check(!main->shouldClose(), "메인 창은 닫히지 않음");
    main->pollEvents();
    check(glfwGetError(nullptr) == GLFW_NO_ERROR, "보조 창을 닫은 뒤 펌프가 오류 없이 돔");

    std::printf("\n== 마지막 창 ==\n");
    main->cleanup();
    check(!GlfwPlatform::isInitialized() && GlfwPlatform::windowCount() == 0, "마지막 창을 닫으면 참조 수 0 · 등록 0");
    check(glfwLooksTerminated(), "마지막 창을 닫으면 GLFW 가 끝남(GLFW_NOT_INITIALIZED)");
    main->pollEvents();   // 끝난 뒤의 펌프는 아무 일도 하지 않음
    main.reset();

    auto again = createAuxWindow(64, 64, "H0 again", /*hidden=*/true);
    check(again != nullptr && GlfwPlatform::isInitialized(), "다시 창을 만들면 GLFW 가 다시 초기화됨");
    if (again) {
        glfwGetError(nullptr);
        (void)glfwGetPrimaryMonitor();
        check(glfwGetError(nullptr) == GLFW_NO_ERROR, "다시 초기화된 GLFW 의 함수가 오류 없이 동작");
    }
    again.reset();
    check(!GlfwPlatform::isInitialized(), "그 창도 없애면 다시 끝남");

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
