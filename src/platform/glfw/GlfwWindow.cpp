#include "GlfwWindow.hpp"
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

GlfwWindow::GlfwWindow(uint32_t w, uint32_t h, const std::string& t)
    : width(w), height(h), title(t) {}

GlfwWindow::~GlfwWindow() {
    cleanup();
}

void GlfwWindow::init() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
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

    cursorArrow_   = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    cursorHResize_ = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    cursorVResize_ = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
}

void GlfwWindow::cleanup() {
    if (window) {
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

bool GlfwWindow::isKeyDown(int key) const {
    if (key >= 0 && key <= GLFW_KEY_LAST && keyLatch_[key]) return true;
    return glfwGetKey(window, key) == GLFW_PRESS;
}

int GlfwWindow::keyPressCount(int key) const {
    return (key >= 0 && key <= GLFW_KEY_LAST) ? keyPresses_[key] : 0;
}

bool GlfwWindow::isMouseButtonDown(int button) const {
    if (button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && buttonLatch_[button]) return true;
    return glfwGetMouseButton(window, button) == GLFW_PRESS;
}

void GlfwWindow::getMousePosition(float& x, float& y) const {
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
