#include "GlfwWindow.hpp"
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#pragma comment(lib, "imm32.lib")
#endif
#include "../../core/Utf8.hpp"

GlfwWindow::GlfwWindow(uint32_t w, uint32_t h, const std::string& t, bool hidden)
    : width(w), height(h), title(t), hidden_(hidden) {}

GlfwWindow::~GlfwWindow() {
    cleanup();
}

void GlfwWindow::init() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (hidden_) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetCharCallback(window, charCallback);

    // Initialize mouse position to current cursor location
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    rLastMouseX = lastMouseX;
    rLastMouseY = lastMouseY;

    installImeHook();

    cursorArrow_   = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    cursorHResize_ = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    cursorVResize_ = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
}

void GlfwWindow::cleanup() {
    if (window) {
        removeImeHook();
        glfwDestroyWindow(window);
        window = nullptr;
    }
    if (cursorArrow_)   { glfwDestroyCursor(cursorArrow_);   cursorArrow_   = nullptr; }
    if (cursorHResize_) { glfwDestroyCursor(cursorHResize_); cursorHResize_ = nullptr; }
    if (cursorVResize_) { glfwDestroyCursor(cursorVResize_); cursorVResize_ = nullptr; }
    glfwTerminate();
}

void GlfwWindow::setCursorShape(int shape) {
    if (!window) return;
    GLFWcursor* cur = (shape == 1) ? cursorHResize_ :
                      (shape == 2) ? cursorVResize_ :
                                     nullptr;  // nullptr = default OS arrow
    glfwSetCursor(window, cur);
}

bool GlfwWindow::shouldClose() const {
    return glfwWindowShouldClose(window);
}

void GlfwWindow::setShouldClose(bool close) {
    glfwSetWindowShouldClose(window, close ? GLFW_TRUE : GLFW_FALSE);
}

void GlfwWindow::pollEvents() {
    // 걸쇠와 누름 횟수는 이번 폴링에서 들어올 이벤트만 담아야 합니다. 비우고 나서 받습니다.
    std::fill(std::begin(keyLatch_), std::end(keyLatch_), false);
    std::fill(std::begin(buttonLatch_), std::end(buttonLatch_), false);
    std::fill(std::begin(keyPresses_), std::end(keyPresses_), uint8_t(0));
    glfwPollEvents();
}

void GlfwWindow::waitEvents() {
    std::fill(std::begin(keyLatch_), std::end(keyLatch_), false);
    std::fill(std::begin(buttonLatch_), std::end(buttonLatch_), false);
    std::fill(std::begin(keyPresses_), std::end(keyPresses_), uint8_t(0));
    glfwWaitEvents();
}

void GlfwWindow::getFramebufferSize(int& w, int& h) const {
    glfwGetFramebufferSize(window, &w, &h);
}

void GlfwWindow::getWindowSize(int& w, int& h) const {
    glfwGetWindowSize(window, &w, &h);
}

void GlfwWindow::setTitle(const std::string& t) {
    glfwSetWindowTitle(window, t.c_str());
}

bool GlfwWindow::wasResized() const {
    return resized;
}

void GlfwWindow::resetResizedFlag() {
    resized = false;
}

VkSurfaceKHR GlfwWindow::createSurface(VkInstance instance) {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }
    return surface;
}

std::vector<const char*> GlfwWindow::getRequiredInstanceExtensions() const {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
}

void GlfwWindow::framebufferResizeCallback(GLFWwindow* w, int /*width*/, int /*height*/) {
    auto app = reinterpret_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
    if (app) {
        app->resized = true;
    }
}

static const char* specialKeyName(int key) {
    switch (key) {
        case GLFW_KEY_SPACE:        return "SPACE";
        case GLFW_KEY_ESCAPE:       return "ESCAPE";
        case GLFW_KEY_ENTER:        return "ENTER";
        case GLFW_KEY_TAB:          return "TAB";
        case GLFW_KEY_BACKSPACE:    return "BACKSPACE";
        case GLFW_KEY_INSERT:       return "INSERT";
        case GLFW_KEY_DELETE:       return "DELETE";
        case GLFW_KEY_RIGHT:        return "RIGHT";
        case GLFW_KEY_LEFT:         return "LEFT";
        case GLFW_KEY_DOWN:         return "DOWN";
        case GLFW_KEY_UP:           return "UP";
        case GLFW_KEY_PAGE_UP:      return "PAGE_UP";
        case GLFW_KEY_PAGE_DOWN:    return "PAGE_DOWN";
        case GLFW_KEY_HOME:         return "HOME";
        case GLFW_KEY_END:          return "END";
        case GLFW_KEY_CAPS_LOCK:    return "CAPS_LOCK";
        case GLFW_KEY_LEFT_SHIFT:   return "LEFT_SHIFT";
        case GLFW_KEY_RIGHT_SHIFT:  return "RIGHT_SHIFT";
        case GLFW_KEY_LEFT_CONTROL: return "LEFT_CTRL";
        case GLFW_KEY_RIGHT_CONTROL:return "RIGHT_CTRL";
        case GLFW_KEY_LEFT_ALT:     return "LEFT_ALT";
        case GLFW_KEY_RIGHT_ALT:    return "RIGHT_ALT";
        case GLFW_KEY_LEFT_SUPER:   return "LEFT_SUPER";
        case GLFW_KEY_RIGHT_SUPER:  return "RIGHT_SUPER";
        case GLFW_KEY_F1:  return "F1";  case GLFW_KEY_F2:  return "F2";
        case GLFW_KEY_F3:  return "F3";  case GLFW_KEY_F4:  return "F4";
        case GLFW_KEY_F5:  return "F5";  case GLFW_KEY_F6:  return "F6";
        case GLFW_KEY_F7:  return "F7";  case GLFW_KEY_F8:  return "F8";
        case GLFW_KEY_F9:  return "F9";  case GLFW_KEY_F10: return "F10";
        case GLFW_KEY_F11: return "F11"; case GLFW_KEY_F12: return "F12";
        default: return nullptr;
    }
}

void GlfwWindow::getMouseDelta(float& dx, float& dy) {
    dx = mouseDX; dy = mouseDY;
    mouseDX = mouseDY = 0.0f;
}

void GlfwWindow::getRightMouseDelta(float& dx, float& dy) {
    dx = rMouseDX; dy = rMouseDY;
    rMouseDX = rMouseDY = 0.0f;
}

float GlfwWindow::getScrollDelta() {
    float v = scrollAccum;
    scrollAccum = 0.0f;
    return v;
}

// 숨긴 창(--hidden-window, 에디터 UI 덤프)은 사용자 입력을 받지 않습니다. 마우스는 창 밖 고정 위치 · 버튼과 키는 늘 떼어 둔 상태입니다.
// 예전에는 만들 때 읽은 실제 커서 위치(창 기준)가 남아, 커서가 우연히 놓인 자리의 위젯이 호버로 그려져 덤프가 실행마다 달랐습니다(2026-11-19).
static constexpr double kHiddenMouse = -10000.0;

bool GlfwWindow::isKeyDown(int key) const {
    if (hidden_) return false;
    if (key >= 0 && key <= GLFW_KEY_LAST && keyLatch_[key]) return true;
    return glfwGetKey(window, key) == GLFW_PRESS;
}

int GlfwWindow::keyPressCount(int key) const {
    return (key >= 0 && key <= GLFW_KEY_LAST) ? keyPresses_[key] : 0;
}

bool GlfwWindow::isMouseButtonDown(int button) const {
    if (hidden_) return false;
    if (button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && buttonLatch_[button]) return true;
    return glfwGetMouseButton(window, button) == GLFW_PRESS;
}

void GlfwWindow::getMousePosition(float& x, float& y) const {
    if (hidden_) { x = y = (float)kHiddenMouse; return; }
    x = (float)lastMouseX;
    y = (float)lastMouseY;
}

void GlfwWindow::mouseButtonCallback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto self = reinterpret_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
    bool pressing = (action == GLFW_PRESS);
    if (pressing && button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST)
        self->buttonLatch_[button] = true;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (pressing && !self->mouseLeftDown) self->firstMouse = true;
        self->mouseLeftDown = pressing;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        self->mouseRightDown = pressing;
    }
}

void GlfwWindow::activateCameraMode() {
    if (cameraMode_) return;
    cameraMode_    = true;
    rFirstMouse    = true;
    capturedMouseX = lastMouseX;
    capturedMouseY = lastMouseY;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
#ifdef _WIN32
    SetCursor(nullptr);
#endif
}

void GlfwWindow::deactivateCameraMode() {
    if (!cameraMode_) return;
    cameraMode_ = false;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetCursorPos(window, capturedMouseX, capturedMouseY);
#ifdef _WIN32
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
#endif
}

void GlfwWindow::cursorPosCallback(GLFWwindow* w, double x, double y) {
    auto self = reinterpret_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));

    if (self->mouseLeftDown && !self->firstMouse) {
        self->mouseDX += (float)(x - self->lastMouseX);
        self->mouseDY += (float)(y - self->lastMouseY);
    }
    if (self->cameraMode_ && !self->rFirstMouse) {
        self->rMouseDX += (float)(x - self->rLastMouseX);
        self->rMouseDY += (float)(y - self->rLastMouseY);
    }

    self->lastMouseX  = x;  self->lastMouseY  = y;  self->firstMouse  = false;
    self->rLastMouseX = x;  self->rLastMouseY = y;  self->rFirstMouse = false;
}

void GlfwWindow::scrollCallback(GLFWwindow* w, double /*xoff*/, double yoff) {
    auto self = reinterpret_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
    self->scrollAccum += (float)yoff;
}

void GlfwWindow::charCallback(GLFWwindow* w, unsigned int codepoint) {
    auto self = reinterpret_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
    self->charQueue_.push_back(codepoint);
}

void GlfwWindow::drainCharQueue(std::vector<unsigned int>& out) {
    out = std::move(charQueue_);
    charQueue_.clear();
}

std::string GlfwWindow::getClipboardText() const {
    const char* s = glfwGetClipboardString(window);
    return s ? std::string(s) : std::string();
}

void GlfwWindow::setClipboardText(const std::string& text) {
    glfwSetClipboardString(window, text.c_str());
}

static std::string keyLabel(int key) {
    const char* name = glfwGetKeyName(key, 0);
    if (!name) name = specialKeyName(key);
    return name ? std::string(name) : "key=" + std::to_string(key);
}

void GlfwWindow::keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    auto self = reinterpret_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
    const bool known = (key >= 0 && key <= GLFW_KEY_LAST);
    // 누름과 OS 자동 반복을 셉니다. 예전에는 반복을 여기서 버려, 백스페이스를 누르고 있어도
    // 한 글자만 지워졌습니다(DEF-58). 키 상태(걸쇠)와 로그에는 반복을 넣지 않습니다.
    if (known && (action == GLFW_PRESS || action == GLFW_REPEAT) && self->keyPresses_[key] < 255)
        ++self->keyPresses_[key];
    if (action == GLFW_REPEAT) return;
    if (action == GLFW_PRESS && known) self->keyLatch_[key] = true;

    // Esc 는 다른 키와 똑같이 흘려보냅니다. 예전에는 여기서 곧바로 창을 닫았는데,
    // 저장 여부를 묻는 경로가 없어 편집 중이던 씬이 그대로 사라졌습니다. 에디터에서
    // Esc 의 자연스러운 의미는 "지금 편집을 취소" 이고, 창 닫기는 OS 닫기 버튼이 맡습니다.

    // 키 로그는 Input 카테고리의 Verbose 입니다. 누를 때와 뗄 때 한 줄씩 쌓여 이름 하나만
    // 타이핑해도 다른 로그를 전부 밀어내므로, 기본 기록 수준(Log)에서는 남지 않고 키 이름
    // 조회도 일어나지 않습니다. 조사할 때 Input 의 기록 수준을 Verbose 로 올리십시오.
    HLOG(LogInput, Verbose) << (action == GLFW_PRESS ? "PRESS " : "RELEASE ") << keyLabel(key);
}

// ── IME (글자 체계 T2, 2026-11-19, Docs/Text_System.md) ──────────────────────────────────────────
// GLFW 3.4 는 조합 중인 글(preedit)을 알려 주지 않으므로 Win32 창 프로시저를 감싸 WM_IME_* 를 직접 받습니다. 확정 글은
// DefWindowProc 이 WM_IME_CHAR → WM_CHAR 로 바꿔 GLFW 문자 콜백으로 들어오므로 건드리지 않습니다. 기본 조합 창은 숨기고
// (에디터 칸이 조합 글을 밑줄로 그림), 후보 창(한자 변환)은 칸의 커서 아래에 둡니다. 글 칸에 포커스가 없으면 IME 문맥을 떼어
// 한글 모드에서도 W · E · R 같은 단축키가 IME 에 먹히지 않게 합니다(UE 도 같은 방식).
#ifdef _WIN32
namespace {
const wchar_t* kImeProp = L"HarukaGlfwWindow";

std::string wideToUtf8(const wchar_t* w, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        uint32_t c = w[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            ++i;
        }
        utf8::append(out, c);
    }
    return out;
}

// UTF-16 단위 자리 → 같은 글의 UTF-8 바이트 자리.
size_t utf16ToUtf8Offset(const wchar_t* w, size_t n, size_t units) {
    size_t bytes = 0;
    for (size_t i = 0; i < n && i < units; ++i) {
        uint32_t c = w[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n) { bytes += 4; ++i; continue; }
        bytes += c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
    }
    return bytes;
}
} // namespace

long long __stdcall GlfwWindow::imeWndProc(void* hwndV, unsigned int msg, unsigned long long wp, long long lp) {
    HWND hwnd = static_cast<HWND>(hwndV);
    auto* self = static_cast<GlfwWindow*>(GetPropW(hwnd, kImeProp));
    if (!self || !self->prevWndProc_)
        return DefWindowProcW(hwnd, msg, (WPARAM)wp, (LPARAM)lp);
    const WNDPROC prev = reinterpret_cast<WNDPROC>(self->prevWndProc_);
    switch (msg) {
        case WM_IME_SETCONTEXT:
            lp &= ~(long long)ISC_SHOWUICOMPOSITIONWINDOW;   // 기본 조합 창은 그리지 않음
            break;
        case WM_IME_STARTCOMPOSITION:
            self->composition_.clear();
            self->compositionCaret_ = 0;
            self->applyImeRect();
            return 0;   // 기본 조합 창을 열지 않음
        case WM_IME_COMPOSITION:
            if (HIMC himc = ImmGetContext(hwnd)) {
                if (lp & GCS_COMPSTR) {
                    const LONG bytes = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
                    std::wstring w(bytes > 0 ? (size_t)bytes / sizeof(wchar_t) : 0, L'\0');
                    if (bytes > 0) ImmGetCompositionStringW(himc, GCS_COMPSTR, w.data(), (DWORD)bytes);
                    LONG cur = (LONG)w.size();
                    if (lp & GCS_CURSORPOS) cur = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
                    self->composition_      = wideToUtf8(w.data(), w.size());
                    self->compositionCaret_ = utf16ToUtf8Offset(w.data(), w.size(), cur < 0 ? w.size() : (size_t)cur);
                } else if (lp & GCS_RESULTSTR) {
                    self->composition_.clear();   // 확정만 있고 새 조합이 없음(결과는 WM_CHAR 로 들어옴)
                    self->compositionCaret_ = 0;
                }
                ImmReleaseContext(hwnd, himc);
            }
            break;   // 확정 글(GCS_RESULTSTR)은 DefWindowProc 이 WM_IME_CHAR 로 보냄
        case WM_IME_ENDCOMPOSITION:
            self->composition_.clear();
            self->compositionCaret_ = 0;
            break;
        default:
            break;
    }
    return CallWindowProcW(prev, hwnd, msg, (WPARAM)wp, (LPARAM)lp);
}
#endif

void GlfwWindow::installImeHook() {
#ifdef _WIN32
    if (!window || hidden_) return;
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) return;
    SetPropW(hwnd, kImeProp, this);
    prevWndProc_ = reinterpret_cast<void*>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                                             reinterpret_cast<LONG_PTR>(&GlfwWindow::imeWndProc)));
    imeEnabled_ = true;
    setTextInputEnabled(false);   // 시작은 글 칸 포커스 없음
#endif
}

void GlfwWindow::removeImeHook() {
#ifdef _WIN32
    if (!window || !prevWndProc_) return;
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) return;
    ImmAssociateContextEx(hwnd, nullptr, IACE_DEFAULT);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(prevWndProc_));
    RemovePropW(hwnd, kImeProp);
    prevWndProc_ = nullptr;
#endif
}

void GlfwWindow::setTextInputEnabled(bool enabled) {
#ifdef _WIN32
    if (!window || hidden_ || !prevWndProc_) return;
    if (enabled == imeEnabled_) return;
    HWND hwnd = glfwGetWin32Window(window);
    if (!enabled) {
        // 조합 중이던 글은 버립니다(포커스가 다른 곳으로 갔으므로 확정 글자가 엉뚱한 곳에 들어가지 않게).
        if (HIMC himc = ImmGetContext(hwnd)) {
            ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(hwnd, himc);
        }
        ImmAssociateContextEx(hwnd, nullptr, 0);   // 문맥을 떼어 IME 를 끔
        composition_.clear();
        compositionCaret_ = 0;
    } else {
        ImmAssociateContextEx(hwnd, nullptr, IACE_DEFAULT);
        imeRect_[0] = -1.f;   // 다음 자리 알림을 다시 보냄
    }
    imeEnabled_ = enabled;
#else
    (void)enabled;
#endif
}

bool GlfwWindow::getComposition(std::string& text, size_t& caretBytes) const {
    if (!imeEnabled_ || composition_.empty()) return false;
    text       = composition_;
    caretBytes = (std::min)(compositionCaret_, composition_.size());
    return true;
}

void GlfwWindow::setTextInputRect(float x, float y, float w, float h) {
    if (x == imeRect_[0] && y == imeRect_[1] && w == imeRect_[2] && h == imeRect_[3]) return;
    imeRect_[0] = x; imeRect_[1] = y; imeRect_[2] = w; imeRect_[3] = h;
    applyImeRect();
}

void GlfwWindow::applyImeRect() {
#ifdef _WIN32
    if (!window || !imeEnabled_ || imeRect_[3] <= 0.f) return;
    HWND hwnd = glfwGetWin32Window(window);
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return;
    // 렌더 타겟(프레임 버퍼) 픽셀 → 창 클라이언트 좌표. Windows 에서는 보통 같지만 배율이 다르면 비율로 바꿉니다.
    int fw = 1, fh = 1, ww = 1, wh = 1;
    glfwGetFramebufferSize(window, &fw, &fh);
    glfwGetWindowSize(window, &ww, &wh);
    const float sx = fw > 0 ? (float)ww / (float)fw : 1.f;
    const float sy = fh > 0 ? (float)wh / (float)fh : 1.f;
    const LONG x0 = (LONG)(imeRect_[0] * sx), y0 = (LONG)(imeRect_[1] * sy);
    const LONG x1 = (LONG)((imeRect_[0] + imeRect_[2]) * sx), y1 = (LONG)((imeRect_[1] + imeRect_[3]) * sy);
    COMPOSITIONFORM cf{};
    cf.dwStyle      = CFS_POINT;
    cf.ptCurrentPos = { x0, y0 };
    ImmSetCompositionWindow(himc, &cf);
    CANDIDATEFORM cand{};
    cand.dwIndex      = 0;
    cand.dwStyle      = CFS_EXCLUDE;   // 커서 사각형을 피해 그 아래(또는 위)에 둠
    cand.ptCurrentPos = { x0, y1 };
    cand.rcArea       = { x0, y0, x1, y1 };
    ImmSetCandidateWindow(himc, &cand);
    ImmReleaseContext(hwnd, himc);
#endif
}
