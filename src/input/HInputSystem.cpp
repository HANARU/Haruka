#include "HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include <cmath>

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
    axes_[axis].push_back(source);
}

void HInputSystem::unbindAxis(const std::string& axis) {
    axes_.erase(axis);
}

HInputSystem::Handle HInputSystem::subscribe(const std::string& action, EInputEvent when, Fn fn) {
    Handle h = next_++;
    listeners_[h] = { action, when, std::move(fn) };
    return h;
}

void HInputSystem::unsubscribe(Handle h) {
    listeners_.erase(h);
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
    }
    return false;
}

float HInputSystem::axis(const std::string& name) const {
    auto it = axes_.find(name);
    if (it == axes_.end()) return 0.f;
    float best = 0.f;
    for (HXrAxis a : it->second) {
        const float v = axisValue(xr_, a);
        if (std::fabs(v) > std::fabs(best)) best = v;
    }
    return best;
}

void HInputSystem::update(float dt, IWindow& win, bool keyboardCaptured) {
    update(dt, [&win](int key) { return win.isKeyDown(key); }, keyboardCaptured);
}

void HInputSystem::update(float dt, const std::function<bool(int)>& isKeyDown, bool keyboardCaptured) {
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
