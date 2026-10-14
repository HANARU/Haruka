#pragma once
#include "ModuleManager.hpp"

class World;
class AssetManager;
class IWindow;
class IRenderExtensionHost;

// ── EngineContext — subsystem references passed each frame ────────────────────
struct EngineContext {
    World&         world;
    AssetManager&  assets;
    IWindow&       window;
    ModuleManager& modules;  // audio, physics, network, etc.
    // 렌더 확장 등록 통로(DEF-18). 렌더러 전체가 아니라 등록 · 해제만 엽니다(renderer/RenderExtension.hpp).
    IRenderExtensionHost& render;
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
