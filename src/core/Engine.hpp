#pragma once
#include "World.hpp"
#include "ModuleManager.hpp"
#include <memory>

// 전방 선언으로 헤더를 가볍게 유지합니다(구체 타입은 Engine.cpp 에서 포함).
class IWindow;
class Renderer;
class AssetManager;
class EditorApp;
class IGameModule;
struct EngineContext;

// ── Engine ──────────────────────────────────────────────────────────────────────
// 엔진의 두 계층을 한곳에서 소유·조립합니다.
//   · 코어 서비스: Window, Renderer, World, AssetManager, Input — 항상 존재하고
//     초기화 순서가 고정입니다(아래 멤버 선언 순서 = 초기화 순서, 역순 파괴).
//   · 동적 모듈: Physics, Audio, (향후 Network) — ModuleManager 로 등록/틱/종료합니다.
// 부팅·메인 루프·teardown 을 Engine 이 소유하므로 main() 은 Engine{}.run() 한 줄입니다.
// 초기화 순서·의존성이 여기 코드로 명시되어, 예전처럼 main() 200줄에 흩어지지 않습니다.
class Engine {
public:
    Engine();    // 부팅: 코어 서비스 생성 + 표준 모듈 등록 + 게임 onStart. 실패 시 예외.
    ~Engine();   // 역순 teardown(게임→모듈→에셋→에디터→렌더러).
    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void run();  // 메인 루프. 창이 닫힐 때까지 poll→tick→render 를 반복합니다.

private:
    void setupInputBindings();   // 프로젝트 설정 레이어(키 바인딩). 게임 코드와 분리.

    // ── 코어 서비스 (초기화 순서대로 선언 = 역순으로 파괴) ──────────────────────
    std::unique_ptr<IWindow>       window_;
    std::unique_ptr<Renderer>      renderer_;
    World                          world_;
    std::unique_ptr<AssetManager>  assets_;

    // ── 동적 모듈 레지스트리 (Physics, Audio, …) ────────────────────────────────
    ModuleManager                  modules_;

    // ── 에디터 레이어 (데스크톱 전용. 런타임 분리는 VR 이식 때) ─────────────────
    std::unique_ptr<EditorApp>     editor_;

    // ── 게임 모듈 + 프레임 컨텍스트 ─────────────────────────────────────────────
    std::unique_ptr<IGameModule>   game_;
    std::unique_ptr<EngineContext> ctx_;
};
