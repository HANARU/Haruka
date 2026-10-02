#pragma once
#include "IEngineModule.hpp"
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

class ModuleManager {
public:
    // Register and start a module. Returns raw pointer for convenience.
    template<typename T>
    T* registerModule(std::unique_ptr<T> mod) {
        static_assert(std::is_base_of_v<IEngineModule, T>,
                      "T must derive from IEngineModule");
        T* ptr = mod.get();
        order_.push_back(ptr);
        map_[std::type_index(typeid(T))] = std::move(mod);
        ptr->startup();
        return ptr;
    }

    template<typename T>
    T* get() const {
        auto it = map_.find(std::type_index(typeid(T)));
        return it != map_.end() ? static_cast<T*>(it->second.get()) : nullptr;
    }

    void tickAll(float dt) {
        for (auto* m : order_) m->tick(dt);
    }

    void beginPlayAll() {
        for (auto* m : order_) m->onBeginPlay();
    }

    void endPlayAll() {
        for (auto* m : order_) m->onEndPlay();
    }

    void pauseChangedAll(bool paused) {
        for (auto* m : order_) m->onPauseChanged(paused);
    }

    // Shuts down in reverse registration order, then clears all modules.
    void shutdownAll() {
        for (int i = (int)order_.size() - 1; i >= 0; --i)
            order_[i]->shutdown();
        map_.clear();
        order_.clear();
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<IEngineModule>> map_;
    std::vector<IEngineModule*> order_;
};
