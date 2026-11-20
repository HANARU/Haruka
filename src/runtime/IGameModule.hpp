#pragma once
#include "ModuleManager.hpp"
#include <functional>
#include <memory>
#include <string>

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
    // 씬 전환(2026-11-20, Docs/Async_Loading.md D6): 모델을 백그라운드로 읽는 동안 지금 씬을 계속 그리고, 다 되면 바꿉니다.
    // 게임 실행 파일만 지원합니다(에디터는 false). 씬 글을 읽지 못해도 false.
    std::function<bool(const std::string&)> requestScene;
};

// ── IGameModule ────────────────────────────────────────────────────────────────
class IGameModule {
public:
    virtual ~IGameModule() = default;

    virtual void onRegister ()                             {}
    virtual void onStart    (EngineContext& ctx)          = 0;
    virtual void onBeginPlay(EngineContext& ctx)           {}
    virtual void onTick     (float dt, EngineContext& ctx) {}
    virtual void onEndPlay  ()                             {}
    virtual void onShutdown ()                             {}
};

// 게임 모듈 선택(2026-11-18, 엔진 구조 안정화 S3). 앞단의 createEngineFrontend 처럼 링크한 쪽이 정의합니다. 지금은
// game/DefaultGameModule.cpp 하나입니다. 예전에는 Engine 이 DefaultGameModule 을 직접 만들어 runtime 이 game 을 include 했습니다.
std::unique_ptr<IGameModule> createGameModule();
