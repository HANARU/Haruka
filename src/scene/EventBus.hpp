#pragma once
// 이벤트 버스(2026-11-20, Docs/Gameplay_Tools.md D4).
//
// 보내는 쪽과 받는 쪽이 서로 모르고 이벤트 형(구조체)만 압니다. 월드가 소유합니다(World::events()).
//
//   struct DoorOpened { HGuid door; };
//   EventSubscription sub_ = world.events().subscribe<DoorOpened>([this](const DoorOpened& e) { ... });   // 지우면 해제
//   world.events().publish(DoorOpened{ door->guid() });                                                    // 바로 부름
//
// 구독 순서대로 부릅니다. 발행 중에 구독 · 해제 · 다른 발행을 해도 됩니다(해제는 표시만 하고 바깥 발행이 끝나면 정리, 발행 중에 더한
// 구독은 그 발행을 받지 않음). 구독 객체는 이동만 되고, 버스보다 먼저 지워져야 합니다(컴포넌트 멤버면 액터와 함께 지워짐).
// 메인 스레드 전용입니다.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

class EventBus;

class EventSubscription {
public:
    EventSubscription() = default;
    EventSubscription(EventBus* bus, std::type_index type, uint64_t id) : bus_(bus), type_(type), id_(id) {}
    EventSubscription(EventSubscription&& o) noexcept : bus_(o.bus_), type_(o.type_), id_(o.id_) { o.bus_ = nullptr; }
    EventSubscription& operator=(EventSubscription&& o) noexcept {
        if (this != &o) {
            reset();
            bus_ = o.bus_; type_ = o.type_; id_ = o.id_;
            o.bus_ = nullptr;
        }
        return *this;
    }
    EventSubscription(const EventSubscription&)            = delete;
    EventSubscription& operator=(const EventSubscription&) = delete;
    ~EventSubscription() { reset(); }

    inline void reset();
    bool active() const { return bus_ != nullptr; }

private:
    EventBus*       bus_  = nullptr;
    std::type_index type_ = typeid(void);
    uint64_t        id_   = 0;
};

class EventBus {
public:
    template<class T>
    [[nodiscard]] EventSubscription subscribe(std::function<void(const T&)> fn) {
        Channel& ch = channels_[std::type_index(typeid(T))];
        const uint64_t id = nextId_++;
        ch.listeners.push_back({ id, [f = std::move(fn)](const void* e) { f(*static_cast<const T*>(e)); }, false });
        return EventSubscription(this, std::type_index(typeid(T)), id);
    }

    // 받은 리스너 수를 돌려줍니다.
    template<class T>
    int publish(const T& e) {
        auto it = channels_.find(std::type_index(typeid(T)));
        if (it == channels_.end()) return 0;
        Channel& ch = it->second;
        ++ch.depth;
        int called = 0;
        const size_t n = ch.listeners.size();   // 발행 중에 더한 구독은 받지 않음
        for (size_t i = 0; i < n; ++i) {
            if (ch.listeners[i].removed) continue;
            auto fn = ch.listeners[i].fn;   // 리스너가 구독을 더해 벡터가 커져도 안전하게 사본으로 부름
            fn(&e);
            ++called;
        }
        if (--ch.depth == 0) compact(ch);
        return called;
    }

    template<class T>
    size_t listenerCount() const {
        auto it = channels_.find(std::type_index(typeid(T)));
        if (it == channels_.end()) return 0;
        size_t n = 0;
        for (const auto& l : it->second.listeners) n += l.removed ? 0 : 1;
        return n;
    }

    void unsubscribe(std::type_index type, uint64_t id) {
        auto it = channels_.find(type);
        if (it == channels_.end()) return;
        for (auto& l : it->second.listeners)
            if (l.id == id) l.removed = true;
        if (it->second.depth == 0) compact(it->second);
    }

private:
    struct Listener {
        uint64_t                         id;
        std::function<void(const void*)> fn;
        bool                             removed;
    };
    struct Channel {
        std::vector<Listener> listeners;
        int                   depth = 0;
    };
    static void compact(Channel& ch) {
        ch.listeners.erase(std::remove_if(ch.listeners.begin(), ch.listeners.end(), [](const Listener& l) { return l.removed; }),
                           ch.listeners.end());
    }
    std::unordered_map<std::type_index, Channel> channels_;
    uint64_t nextId_ = 1;
};

inline void EventSubscription::reset() {
    if (bus_) bus_->unsubscribe(type_, id_);
    bus_ = nullptr;
}
