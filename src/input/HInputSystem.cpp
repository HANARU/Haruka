#include "HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

void HInputSystem::bind(const std::string& action, int keyCode) {
    bindings_[action].keys.push_back(keyCode);
}

void HInputSystem::bindXrButton(const std::string& action, HXrButton button) {
    bindings_[action].buttons.push_back(button);
}

void HInputSystem::unbind(const std::string& action) {
    bindings_.erase(action);
}

void HInputSystem::bindAxis(const std::string& axis, HXrAxis source) {
    axes_[axis].xr.push_back(source);
}

void HInputSystem::bindKeyAxis(const std::string& axis, int posKey, int negKey) {
    axes_[axis].keys.push_back({ posKey, negKey });
}

void HInputSystem::bindMouseAxis(const std::string& axis, HMouseAxis which, float scale) {
    axes_[axis].mouse.push_back({ which, scale });
}

void HInputSystem::unbindAxis(const std::string& axis) {
    axes_.erase(axis);
}

void HInputSystem::clearBindings() {
    bindings_.clear();
    axes_.clear();
}

HInputSystem::Handle HInputSystem::subscribe(const std::string& action, EInputEvent when, Fn fn) {
    Handle h = next_++;
    listeners_[h] = { action, when, std::move(fn) };
    return h;
}

void HInputSystem::unsubscribe(Handle h) {
    listeners_.erase(h);
}

bool HInputSystem::held(const std::string& action) const {
    auto it = bindings_.find(action);
    return it != bindings_.end() && it->second.prevHeld;
}

float HInputSystem::axisValue(const HXrInputState& s, HXrAxis a) {
    if (!s.focused) return 0.f;
    const HHandInput& L = s.hands[0];
    const HHandInput& R = s.hands[1];
    switch (a) {
        case HXrAxis::LeftTrigger:  return L.trigger;
        case HXrAxis::RightTrigger: return R.trigger;
        case HXrAxis::LeftSqueeze:  return L.squeeze;
        case HXrAxis::RightSqueeze: return R.squeeze;
        case HXrAxis::LeftStickX:   return L.thumbstick.x;
        case HXrAxis::LeftStickY:   return L.thumbstick.y;
        case HXrAxis::RightStickX:  return R.thumbstick.x;
        case HXrAxis::RightStickY:  return R.thumbstick.y;
    }
    return 0.f;
}

bool HInputSystem::buttonDown(const HXrInputState& s, HXrButton b) {
    if (!s.focused) return false;
    const HHandInput& L = s.hands[0];
    const HHandInput& R = s.hands[1];
    switch (b) {
        case HXrButton::LeftPrimary:     return L.primary;
        case HXrButton::LeftSecondary:   return L.secondary;
        case HXrButton::LeftStickClick:  return L.thumbstickClick;
        case HXrButton::LeftMenu:        return L.menu;
        case HXrButton::RightPrimary:    return R.primary;
        case HXrButton::RightSecondary:  return R.secondary;
        case HXrButton::RightStickClick: return R.thumbstickClick;
        case HXrButton::LeftTrigger:     return L.trigger > 0.5f;
        case HXrButton::LeftGrip:        return L.squeeze > 0.5f;
        case HXrButton::RightTrigger:    return R.trigger > 0.5f;
        case HXrButton::RightGrip:       return R.squeeze > 0.5f;
    }
    return false;
}

float HInputSystem::axis(const std::string& name) const {
    auto it = axes_.find(name);
    if (it == axes_.end()) return 0.f;
    const AxisBinding& b = it->second;
    float best = 0.f;
    for (HXrAxis a : b.xr) {
        const float v = axisValue(xr_, a);
        if (std::fabs(v) > std::fabs(best)) best = v;
    }
    float mouse = 0.f;
    for (const MouseAxis& m : b.mouse) mouse += (m.which == HMouseAxis::X ? lookDx_ : lookDy_) * m.scale;
    return best + b.keyValue + mouse;
}

void HInputSystem::update(float dt, IWindow& win, bool keyboardCaptured) {
    update(dt, [&win](int key) { return win.isKeyDown(key); }, keyboardCaptured);
}

void HInputSystem::update(float dt, const std::function<bool(int)>& isKeyDown, bool keyboardCaptured) {
    // 키 축은 여기서 한 번 계산해 둡니다(axis() 는 창 없이 읽음).
    for (auto& [name, ax] : axes_) {
        float v = 0.f;
        if (!keyboardCaptured)
            for (const KeyAxis& k : ax.keys)
                v += (isKeyDown(k.pos) ? 1.f : 0.f) - (isKeyDown(k.neg) ? 1.f : 0.f);
        ax.keyValue = std::clamp(v, -1.f, 1.f);
    }

    for (auto& [action, binding] : bindings_) {
        bool held = false;
        if (!keyboardCaptured)
            for (int key : binding.keys)
                if (isKeyDown(key)) { held = true; break; }
        if (!held)
            for (HXrButton b : binding.buttons)
                if (buttonDown(xr_, b)) { held = true; break; }

        const bool pressed  = held && !binding.prevHeld;
        const bool released = !held && binding.prevHeld;
        binding.prevHeld = held;

        if (!pressed && !released && !held) continue;

        for (auto& [h, lst] : listeners_) {
            if (lst.action != action) continue;
            switch (lst.when) {
                case EInputEvent::Pressed:  if (pressed)  lst.fn(dt); break;
                case EInputEvent::Released: if (released) lst.fn(dt); break;
                case EInputEvent::Held:     if (held)     lst.fn(dt); break;
            }
        }
    }
}

// ── 설정 줄 해석 (2026-10-15) ────────────────────────────────────────────────────
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

// GLFW 키 코드(IWindow 의 HInput 값과 같음). 글자 · 숫자는 대문자 ASCII 그대로입니다.
bool keyCode(const std::string& name, int& out) {
    const std::string n = lower(name);
    if (n.size() == 1 && std::isalpha((unsigned char)n[0])) { out = std::toupper((unsigned char)n[0]); return true; }
    if (n.size() == 1 && std::isdigit((unsigned char)n[0])) { out = n[0]; return true; }
    struct K { const char* n; int c; };
    static const K table[] = {
        { "space", 32 }, { "enter", 257 }, { "escape", 256 }, { "tab", 258 }, { "backspace", 259 },
        { "right", 262 }, { "left", 263 }, { "down", 264 }, { "up", 265 },
        { "shift", 340 }, { "ctrl", 341 }, { "alt", 342 },
    };
    for (const K& k : table) if (n == k.n) { out = k.c; return true; }
    if (n.size() >= 2 && n[0] == 'f') {
        const int f = std::atoi(n.c_str() + 1);
        if (f >= 1 && f <= 12 && n == "f" + std::to_string(f)) { out = 289 + f; return true; }
    }
    return false;
}

bool xrButton(const std::string& name, HXrButton& out) {
    struct B { const char* n; HXrButton b; };
    static const B table[] = {
        { "left.x", HXrButton::LeftPrimary }, { "left.y", HXrButton::LeftSecondary },
        { "left.stick", HXrButton::LeftStickClick }, { "left.menu", HXrButton::LeftMenu },
        { "left.trigger", HXrButton::LeftTrigger }, { "left.grip", HXrButton::LeftGrip },
        { "right.a", HXrButton::RightPrimary }, { "right.b", HXrButton::RightSecondary },
        { "right.stick", HXrButton::RightStickClick },
        { "right.trigger", HXrButton::RightTrigger }, { "right.grip", HXrButton::RightGrip },
    };
    const std::string n = lower(name);
    for (const B& b : table) if (n == b.n) { out = b.b; return true; }
    return false;
}

bool xrAxis(const std::string& name, HXrAxis& out) {
    struct A { const char* n; HXrAxis a; };
    static const A table[] = {
        { "left.trigger", HXrAxis::LeftTrigger }, { "right.trigger", HXrAxis::RightTrigger },
        { "left.grip", HXrAxis::LeftSqueeze }, { "right.grip", HXrAxis::RightSqueeze },
        { "left.stick.x", HXrAxis::LeftStickX }, { "left.stick.y", HXrAxis::LeftStickY },
        { "right.stick.x", HXrAxis::RightStickX }, { "right.stick.y", HXrAxis::RightStickY },
    };
    const std::string n = lower(name);
    for (const A& a : table) if (n == a.n) { out = a.a; return true; }
    return false;
}

} // namespace

bool HInputSystem::applyBinding(const std::string& key, const std::string& value, std::string& error) {
    const bool isAction = key.rfind("input.action.", 0) == 0;
    const bool isAxis   = key.rfind("input.axis.", 0) == 0;
    if (!isAction && !isAxis) { error = "expected input.action.NAME or input.axis.NAME"; return false; }
    const std::string name = key.substr(isAction ? 13 : 11);
    if (name.empty()) { error = "missing name"; return false; }

    // 먼저 모두 읽어 보고, 다 맞으면 바꿉니다(틀린 줄이 있던 바인딩을 반쯤 지우지 않게).
    Binding     act;
    AxisBinding ax;
    std::stringstream ss(value);
    for (std::string part; std::getline(ss, part, ',');) {
        const std::string src = trim(part);
        if (src.empty()) continue;
        const size_t colon = src.find(':');
        if (colon == std::string::npos) { error = "source '" + src + "' has no kind (key: · keys: · xr: · mouse:)"; return false; }
        const std::string kind = lower(src.substr(0, colon));
        const std::string arg  = trim(src.substr(colon + 1));
        if (isAction) {
            int code = 0; HXrButton b{};
            if (kind == "key" && keyCode(arg, code))      act.keys.push_back(code);
            else if (kind == "xr" && xrButton(arg, b))    act.buttons.push_back(b);
            else { error = "unknown action source '" + src + "'"; return false; }
        } else {
            HXrAxis a{};
            if (kind == "xr" && xrAxis(arg, a)) { ax.xr.push_back(a); continue; }
            if (kind == "keys") {
                const size_t slash = arg.find('/');
                int p = 0, n = 0;
                if (slash != std::string::npos && keyCode(trim(arg.substr(0, slash)), p) && keyCode(trim(arg.substr(slash + 1)), n)) {
                    ax.keys.push_back({ p, n });
                    continue;
                }
            }
            if (kind == "mouse") {
                std::string axisName = lower(arg);
                float scale = 1.f;
                if (const size_t star = axisName.find('*'); star != std::string::npos) {
                    char* end = nullptr;
                    const std::string num = trim(axisName.substr(star + 1));
                    scale = std::strtof(num.c_str(), &end);
                    if (num.empty() || !end || *end != '\0') { error = "bad scale in '" + src + "'"; return false; }
                    axisName = trim(axisName.substr(0, star));
                }
                if (axisName == "x" || axisName == "y") {
                    ax.mouse.push_back({ axisName == "x" ? HMouseAxis::X : HMouseAxis::Y, scale });
                    continue;
                }
            }
            error = "unknown axis source '" + src + "'";
            return false;
        }
    }
    if (isAction) {
        if (act.keys.empty() && act.buttons.empty()) { error = "no sources"; return false; }
        bindings_[name] = std::move(act);
    } else {
        if (ax.xr.empty() && ax.keys.empty() && ax.mouse.empty()) { error = "no sources"; return false; }
        axes_[name] = std::move(ax);
    }
    return true;
}

std::vector<std::pair<std::string, std::string>> HInputSystem::defaultBindings() {
    return {
        { "input.axis.MoveX",       "keys:D/A, xr:left.stick.x" },
        { "input.axis.MoveY",       "keys:W/S, xr:left.stick.y" },
        { "input.axis.MoveUp",      "keys:E/Q" },
        { "input.axis.TurnX",       "xr:right.stick.x" },
        { "input.axis.LookX",       "mouse:x" },
        { "input.axis.LookY",       "mouse:y" },
        { "input.action.GrabLeft",  "xr:left.grip" },
        { "input.action.GrabRight", "xr:right.grip" },
    };
}
