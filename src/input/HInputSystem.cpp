#include "HInputSystem.hpp"
#include "../platform/IWindow.hpp"

void HInputSystem::bind(const std::string& action, int keyCode) {
    bindings_[action].keys.push_back(keyCode);
}

void HInputSystem::unbind(const std::string& action) {
    bindings_.erase(action);
}

HInputSystem::Handle HInputSystem::subscribe(const std::string& action, EInputEvent when, Fn fn) {
    Handle h = next_++;
    listeners_[h] = { action, when, std::move(fn) };
    return h;
}

void HInputSystem::unsubscribe(Handle h) {
    listeners_.erase(h);
}

void HInputSystem::update(float dt, IWindow& win) {
    for (auto& [action, binding] : bindings_) {
        bool held = false;
        for (int key : binding.keys)
            if (win.isKeyDown(key)) { held = true; break; }

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
