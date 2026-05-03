#pragma once
#include "../core/IGameModule.hpp"

class DefaultGameModule : public IGameModule {
public:
    void onStart(EngineContext& ctx)           override;
    void onTick (float dt, EngineContext& ctx) override {}
};
