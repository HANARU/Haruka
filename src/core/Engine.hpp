#pragma once
#include "World.hpp"
#include "ModuleManager.hpp"
#include "ProjectSettings.hpp"
#include "IEngineFrontend.hpp"
#include "EngineOptions.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

// 전방 선언으로 헤더를 가볍게 유지합니다(구체 타입은 Engine.cpp 에서 포함).
class IWindow;
class Renderer;
class AssetManager;
class IGameModule;
class XrSystem;
class PhysicsSystem;
class AudioSystem;
struct EngineContext;

// 실행 옵션(EngineOptions)은 EngineOptions.hpp 에 있습니다(2026-10-31, 명령줄 해석을 창 없이 시험하려고 옮김).

// ── Engine ──────────────────────────────────────────────────────────────────────
// 엔진의 두 계층을 한곳에서 소유·조립합니다.
//   · 코어 서비스: Window, Renderer, World, AssetManager, Input — 항상 존재하고
//     초기화 순서가 고정입니다(아래 멤버 선언 순서 = 초기화 순서, 역순 파괴).
//   · 동적 모듈: Physics, Audio, (향후 Network) — ModuleManager 로 등록/틱/종료합니다.
// 부팅·메인 루프·teardown 을 Engine 이 소유하므로 main() 은 Engine{}.run() 한 줄입니다.
// 초기화 순서·의존성이 여기 코드로 명시되어, 예전처럼 main() 200줄에 흩어지지 않습니다.
// 에디터는 직접 알지 않고 앞단(IEngineFrontend)으로만 묻습니다(2026-10-19, DEF-12). 실행 파일이 앞단을 골라 넘깁니다.
class Engine {
public:
    // 부팅: 코어 서비스 생성 + 표준 모듈 등록 + 게임 onStart. 실패 시 예외.
    Engine(const EngineOptions& options, std::unique_ptr<IEngineFrontend> frontend);
    ~Engine();   // 역순 teardown(게임→모듈→에셋→앞단→렌더러).
    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    // 메인 루프. 창이 닫힐 때까지 poll→tick→render 를 반복하고 종료 코드를 돌려줍니다(보통 0, 자동 실행의 실패는
    // hopt::kExit*).
    int run();
    // 검증 레이어가 지금까지 보고한 ERROR 수(Debug 빌드). 엔진을 지운 뒤에도 부를 수 있어, main 이 정리(장치 · 인스턴스
    // 파괴) 중에 난 오류(누수 등)까지 자동 실행의 종료 코드에 넣습니다.
    static uint32_t validationErrorCount();

private:
    // 창 없는 자동 실행(--frames · --timeout · --capture)을 마무리합니다: 캡처를 저장하고 요약을 남긴 뒤 종료 코드를 정합니다.
    int finishRun();
    void setupInputBindings();   // 프로젝트 설정 레이어(키 바인딩). 게임 코드와 분리.
    // VR 세션을 Play 에 맞춥니다(2026-10-15 사용자 결정). Play 에 들어가면 세션을 만들고, Stop 하면 런타임에
    // 정상 종료를 요청합니다. UE VR Preview · Unity 처럼 편집 중에는 헤드셋에 그리지 않습니다. 프레임을
    // 시작하기 전에 불러, 진행 중인 XR 프레임이 있을 때 세션을 지우는 일이 없게 합니다.
    void syncXrWithPlay(bool inGame);
    bool startXrSession();   // 세션 + 스왑체인을 렌더러에 연결. 실패하면 이번 Play 는 데스크톱만
    // XR 세션을 끝냅니다(렌더러에서 스왑체인을 떼고 세션을 지움). OpenXR 인스턴스는 그것으로 만든 Vulkan
    // 장치가 살아 있는 동안 남겨 두고 엔진이 끝날 때 지웁니다. 물리 스텝을 60Hz 로 되돌립니다.
    void endXr(const char* reason);
    // OpenXR 인스턴스를 잃었습니다(런타임 재시작 · 갱신 등, 2026-11-01 DEF-155). 이번 실행에서는 다시 열 수 없으므로 XR 을 끄고,
    // XR 이 필수인 플랫폼(Quest, 데스크톱 화면이 없음)은 앱을 끝냅니다.
    void onXrInstanceLost();

    // 프로젝트 설정(assets/project.ini, 2026-10-15). 에셋 루트를 정한 뒤 한 번 읽습니다.
    ProjectSettings                project_;

    // ── 앞단 정책과 XR 실행 상태 (2026-10-19) ────────────────────────────────────
    IEngineFrontend::Policy          policy_;   // 부팅할 때 앞단에서 한 번 받음(에셋 루트 · VR 시작 · XR 처리 규칙)
    bool                             xrWanted_       = false;   // 이번 실행에서 VR 을 원함(명령줄 또는 정책)
    bool                             xrFocusPaused_  = false;   // 헤드셋 초점을 잃어 게임을 멈춘 중(policy_.pauseOnXrFocusLoss)
    // 헤드셋 연결이 끊긴 뒤 다시 열기를 기다리는 중(policy_.retryXrSession). 정해진 간격으로 조용히 확인합니다.
    bool                                  xrRetryPending_ = false;
    std::chrono::steady_clock::time_point xrRetryAt_{};
    // 처음 잃은 때부터 다시 연 세션이 5초 동안 이어서 돌 때까지 복구 중입니다(DEF-155). 다시 연 세션이 시작하지 못하거나 곧 다시
    // 잃어도 마감 시각은 처음 것을 유지하고, 다시 열기 간격은 2초에서 30초까지 두 배씩 늘립니다. 첫 프레임 하나로 복구를 판정하면
    // 곧 잃을 세션(성공 코드 XR_SESSION_LOSS_PENDING)에서 마감 시각이 다시 잡혔습니다(2026-11-01 재검토).
    bool                                  xrRecovering_   = false;
    std::chrono::seconds                  xrRetryDelay_{ 2 };
    std::chrono::steady_clock::time_point xrRecoveredRunSince_{};   // 다시 연 세션이 처음 프레임을 시작한 때(비면 아직)
    // XR 이 필수인 플랫폼(options_.xrRequired)은 이때까지 돌아오지 않으면 끝냅니다(DEF-155). 데스크톱으로 계속할 수 없습니다.
    std::chrono::steady_clock::time_point xrRetryGiveUpAt_{};

    // ── 창 없는 실행 · 합성 눈 · 자동 확인 (2026-10-31, 패키징 2단계) ─────────────────────────
    EngineOptions options_;                    // 부팅할 때 받은 옵션(프레임 수 · 시간 제한 · 캡처 경로)
    bool          headless_       = false;     // 창이 표면을 주지 않음(--headless · Android)
    bool          synthetic_      = false;     // 합성 눈을 붙였음(XR 세션과 배타)
    uint32_t      drawnFrames_    = 0;         // 그린 프레임 수(XR · 합성 눈 · 데스크톱)
    uint32_t      syntheticImage_ = 0;         // 다음에 그릴 합성 눈 이미지
    uint32_t      lastSynthetic_  = UINT32_MAX;   // 마지막으로 그린 합성 눈 이미지(캡처용)
    bool          timedOut_       = false;

    // ── 코어 서비스 (초기화 순서대로 선언 = 역순으로 파괴) ──────────────────────
    std::unique_ptr<IWindow>       window_;
    // XR(--xr). 렌더러보다 먼저 만듭니다(OpenXR 이 Vulkan 장치를 고름). 세션은 렌더러보다 먼저
    // 지워야 하므로 소멸자가 순서를 직접 정합니다. 런타임 · 헤드셋이 없으면 null(데스크톱).
    std::unique_ptr<XrSystem>      xr_;
    bool                           xrUsable_     = false;   // 렌더러가 OpenXR 이 고른 장치로 만들어져 세션을 열 수 있음
    bool                           xrPlayWanted_ = false;   // Play(일시정지 포함) 중이라 세션이 있어야 함
    // 리그가 없는 씬의 보이지 않는 리그(2026-10-16): 바닥을 찾은 카메라 액터(0 = 아직)와 카메라에서 바닥까지의 거리.
    uint64_t                       implicitRigCam_  = 0;
    float                          implicitRigDrop_ = 0.f;
    // 머리 추적 카메라의 부모를 원점으로 쓴다고 알린 카메라 액터(0 = 아직, DEF-144). 같은 카메라는 한 번만 알립니다.
    uint64_t                       trackedOriginCam_ = 0;
    std::unique_ptr<Renderer>      renderer_;
    World                          world_;
    std::unique_ptr<AssetManager>  assets_;

    // ── 동적 모듈 레지스트리 (Physics, Audio, …) ────────────────────────────────
    ModuleManager                  modules_;
    PhysicsSystem*                 physics_ = nullptr;   // modules_ 소유. XR 주사율에 스텝을 맞출 때 씁니다
    AudioSystem*                   audio_   = nullptr;   // modules_ 소유. XR 중 청취자를 머리 자세에 둘 때 씁니다

    // ── 앞단 (에디터 또는 게임, 2026-10-19) ─────────────────────────────────────
    // 생성자 첫머리에 받아 정책을 읽고, init 은 코어 서비스가 생긴 뒤에 부릅니다. 예전 에디터 자리라 파괴 순서도 같습니다
    // (게임 모듈 다음, 동적 모듈 · 에셋 · 월드 · 렌더러보다 먼저).
    std::unique_ptr<IEngineFrontend> frontend_;

    // ── 게임 모듈 + 프레임 컨텍스트 ─────────────────────────────────────────────
    std::unique_ptr<IGameModule>   game_;
    std::unique_ptr<EngineContext> ctx_;
};
