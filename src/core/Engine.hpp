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
class XrSystem;
class PhysicsSystem;
class AudioSystem;
struct EngineContext;

// 실행 옵션(명령줄). main() 이 채워 Engine 에 넘깁니다.
struct EngineOptions {
    bool xr            = false;   // --xr : OpenXR 세션으로 헤드셋에 그림(런타임이 없으면 데스크톱)
    bool emulateStereo = false;   // --emulate-stereo : 헤드셋 없이 두 눈 경로 확인(DEF-09)
};

// ── Engine ──────────────────────────────────────────────────────────────────────
// 엔진의 두 계층을 한곳에서 소유·조립합니다.
//   · 코어 서비스: Window, Renderer, World, AssetManager, Input — 항상 존재하고
//     초기화 순서가 고정입니다(아래 멤버 선언 순서 = 초기화 순서, 역순 파괴).
//   · 동적 모듈: Physics, Audio, (향후 Network) — ModuleManager 로 등록/틱/종료합니다.
// 부팅·메인 루프·teardown 을 Engine 이 소유하므로 main() 은 Engine{}.run() 한 줄입니다.
// 초기화 순서·의존성이 여기 코드로 명시되어, 예전처럼 main() 200줄에 흩어지지 않습니다.
class Engine {
public:
    explicit Engine(const EngineOptions& options = {});   // 부팅: 코어 서비스 생성 + 표준 모듈 등록 + 게임 onStart. 실패 시 예외.
    ~Engine();   // 역순 teardown(게임→모듈→에셋→에디터→렌더러).
    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void run();  // 메인 루프. 창이 닫힐 때까지 poll→tick→render 를 반복합니다.

private:
    void setupInputBindings();   // 프로젝트 설정 레이어(키 바인딩). 게임 코드와 분리.
    // XR 세션을 끝냅니다(렌더러에서 스왑체인을 떼고 세션을 지움). OpenXR 인스턴스는 그것으로 만든 Vulkan
    // 장치가 살아 있는 동안 남겨 두고 엔진이 끝날 때 지웁니다. 물리 스텝을 60Hz 로 되돌립니다.
    void endXr();

    // ── 코어 서비스 (초기화 순서대로 선언 = 역순으로 파괴) ──────────────────────
    std::unique_ptr<IWindow>       window_;
    // XR(--xr). 렌더러보다 먼저 만듭니다(OpenXR 이 Vulkan 장치를 고름). 세션은 렌더러보다 먼저
    // 지워야 하므로 소멸자가 순서를 직접 정합니다. 런타임 · 헤드셋이 없으면 null(데스크톱).
    std::unique_ptr<XrSystem>      xr_;
    std::unique_ptr<Renderer>      renderer_;
    World                          world_;
    std::unique_ptr<AssetManager>  assets_;

    // ── 동적 모듈 레지스트리 (Physics, Audio, …) ────────────────────────────────
    ModuleManager                  modules_;
    PhysicsSystem*                 physics_ = nullptr;   // modules_ 소유. XR 주사율에 스텝을 맞출 때 씁니다
    AudioSystem*                   audio_   = nullptr;   // modules_ 소유. XR 중 청취자를 머리 자세에 둘 때 씁니다

    // ── 에디터 레이어 (데스크톱 전용. 런타임 분리는 VR 이식 때) ─────────────────
    std::unique_ptr<EditorApp>     editor_;

    // ── 게임 모듈 + 프레임 컨텍스트 ─────────────────────────────────────────────
    std::unique_ptr<IGameModule>   game_;
    std::unique_ptr<EngineContext> ctx_;
};
