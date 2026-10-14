#pragma once
#include "ModuleManager.hpp"

class World;
class AssetManager;
class IWindow;
class IRenderExtensionHost;
class ComponentRegistry;

// ── EngineContext — subsystem references passed each frame ────────────────────
struct EngineContext {
    World&         world;
    AssetManager&  assets;
    IWindow&       window;
    ModuleManager& modules;  // audio, physics, network, etc.
    // 렌더 확장 등록 통로(DEF-18). 렌더러 전체가 아니라 등록 · 해제만 엽니다(renderer/RenderExtension.hpp).
    IRenderExtensionHost& render;
    // 컴포넌트 타입 등록(DEF-22). Layer 2 컴포넌트를 여기에 등록하면 씬 · 프리팹 저장과 에디터 추가가 됩니다.
    // onStart 에서 씬을 읽기 전에 등록하십시오(core/ComponentRegistry.hpp).
    ComponentRegistry&    components;
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
