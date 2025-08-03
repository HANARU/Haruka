#pragma once
#include "ModuleManager.hpp"

class World;
class AssetManager;
class IWindow;

// ── EngineContext — subsystem references passed each frame ────────────────────
struct EngineContext {
    World&         world;
    AssetManager&  assets;
    IWindow&       window;
    ModuleManager& modules;  // audio, physics, network, etc.
};

// ── IGameModule ────────────────────────────────────────────────────────────────
class IGameModule {
public:
    virtual ~IGameModule() = default;

    virtual void onStart    (EngineContext& ctx)          = 0;
    virtual void onBeginPlay(EngineContext& ctx)           {}
    virtual void onTick     (float dt, EngineContext& ctx) {}
    virtual void onEndPlay  ()                             {}
    virtual void onShutdown ()                             {}
};
