#pragma once
#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>

// ── HDelegate — single-cast, one listener at a time ───────────────────────────
template<typename... Args>
class HDelegate {
public:
    using Fn = std::function<void(Args...)>;

    void bind(Fn fn)  { fn_ = std::move(fn); }
    void clear()      { fn_ = nullptr; }
    bool isBound() const { return bool(fn_); }

    void execute(Args... args) const {
        if (fn_) fn_(std::forward<Args>(args)...);
    }

private:
    Fn fn_;
};

// ── HEvent — multicast, multiple listeners identified by Handle ───────────────
template<typename... Args>
class HEvent {
public:
    using Handle = uint32_t;
    using Fn     = std::function<void(Args...)>;

    Handle add(Fn fn) {
        Handle h = next_++;
        listeners_[h] = std::move(fn);
        return h;
    }

    void remove(Handle h) {
        if (broadcasting_) {
            pendingRemove_.push_back(h);
        } else {
            listeners_.erase(h);
        }
    }

    void broadcast(Args... args) const {
        broadcasting_ = true;
        for (auto& [h, fn] : listeners_)
            fn(std::forward<Args>(args)...);
        broadcasting_ = false;
        for (Handle h : pendingRemove_) listeners_.erase(h);
        pendingRemove_.clear();
    }

    bool empty() const { return listeners_.empty(); }

private:
    mutable std::unordered_map<Handle, Fn> listeners_;
    Handle next_ = 0;
    mutable bool                           broadcasting_  = false;
    mutable std::vector<Handle>            pendingRemove_;
};
