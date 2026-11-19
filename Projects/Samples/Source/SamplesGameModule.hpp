#pragma once
#include "runtime/IGameModule.hpp"

class SamplesGameModule : public IGameModule {
public:
    void onRegister()                          override;
    void onStart(EngineContext& ctx)           override;
    void onTick (float dt, EngineContext& ctx) override {}
};
