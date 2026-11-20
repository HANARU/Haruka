#pragma once
#include "../scene/World.hpp"
#include "ModuleManager.hpp"
#include "../core/ProjectSettings.hpp"
#include "../core/UserSettings.hpp"
#include "../core/Accessibility.hpp"
#include "../core/Subtitles.hpp"
#include "../components/SubtitlePanel.hpp"
#include "../core/CVar.hpp"
#include "../core/TraceRecorder.hpp"
#include "IEngineFrontend.hpp"
#include "EngineOptions.hpp"
#include "FrameStats.hpp"
#include "WorldUiDriver.hpp"
#include "../renderer/RenderSceneInput.hpp"
#include <chrono>
#include <cstdint>
#include <map>
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
class XrSessionController;
class XrViewOrigin;
class SaveStore;
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

    // ── 사용자 설정 · 저장 게임 (2026-11-20, Docs/Save_And_Settings.md) ─────────────────────────────
    // 시작할 때 user_settings.ini 를 읽고 --user-setting 을 덮어 저장합니다. 그래픽 품질 · 소리 · 입력 · 편안함을 모두 적용한 뒤,
    // 프레임 처음(streamAssets)에 변경 번호가 바뀌었으면 바로 적용 항목만 다시 적용하고, 마지막 변경 1 초 뒤 · 종료할 때 저장합니다.
    void loadUserSettings();
    // CVar(2026-11-20, Docs/Gameplay_Tools.md D6 ~ D8): ini · --cvar 를 먼저 넣고 엔진 변수 · 명령을 등록합니다.
    void setupCVars();
    CVarRegistry                         cvars_;
    CVarRegistry::CVar*                  cvTimeScale_  = nullptr;   // time.scale
    CVarRegistry::CVar*                  cvMaxDt_      = nullptr;   // time.maxDt
    CVarRegistry::CVar*                  cvDebugLines_ = nullptr;   // debug.lines
    // CPU 추적 파일(2026-11-20, Docs/CPU_Profiling.md D6): --trace · --trace-frames · trace.start · trace.stop.
    TraceRecorder                        trace_;
    void applyLiveUserSettings(bool startup);
    void applyUserInput(bool startup);
    void updateUserSettings();
    void saveUserSettings(const char* why);
    UserSettings                         userSettings_;
    ComfortSettings                      comfort_;            // 월드 서비스(world_.setService), 이동 · 리그 높이가 읽음
    // 접근성(2026-11-21, Docs/Accessibility.md): 설정 · 비네트 · 앉은 자세 상태, 자막 큐와 표시. 모두 월드 서비스입니다.
    AccessibilityState                   access_;
    SubtitleQueue                        subtitles_;
    SubtitlePanel                        subtitlePanel_;
    std::string                          accessLogged_;
    CVarRegistry::CVar*                  cvVignetteDebug_ = nullptr;   // comfort.vignetteDebug(비네트 모양 확인용)
    std::unique_ptr<SaveStore>           saves_;              // <쓰기 루트>/Saves
    uint64_t                             settingsApplied_ = 0;
    bool                                 settingsDirty_   = false;
    std::chrono::steady_clock::time_point settingsChangedAt_{};
    std::map<std::string, std::string>   restartApplied_;     // 시작할 때 쓴 다시 시작 항목의 값(바뀌면 한 번 알림)
    std::string                          audioLogged_, postLogged_;   // 같은 값을 다시 알리지 않게

    // ── 메인 루프의 프레임 단계 (2026-11-18, 엔진 구조 안정화 S2) ──────────────────────────────────────
    // run() 은 아래 단계를 이 순서로 부르기만 합니다. 순서가 규칙입니다(Unity PlayerLoop · UE TickGroup 처럼 단계 목록으로 둠).
    //   1 beginLoop        창 닫기 요청(앞단에 물음) · 창 이벤트 · 최소화 판정
    //   2 beginXrFrame     Play 에 세션 맞춤 · 세션 없을 때 이벤트 · 다시 열기 · 화면 카메라와 리그 공간 · XR 프레임 시작(입력)
    //   3 applyViewPose    화면 카메라의 XR 원점 · 소리 청취자 자세
    //   4 readDesktopInput 프레임 시간 · 창 크기 · 마우스 · 오른쪽 버튼 카메라 모드
    //   5 updatePlayState  헤드셋 초점 일시정지 · Play 시작 · 끝 · 일시정지 알림
    //   6 tickFrame        앞단 tick · 디버그 선 비우기 · 입력 갱신 · 월드 · 게임 · 모듈 · 따라오는 패널 · 월드 UI 포인터
    //   7 frontendInput    앞단 입력(에디터 클릭 처리)
    //   8 renderFrame      XR · 합성 눈 · 데스크톱 중 하나
    //   9 endLoop          자동 실행 끝내기 · FPS 제목 · 프레임 통계 · 최소화 중 쉼
    // FrameState 는 루프 1 회의 값, LoopState 는 루프 사이에 이어지는 값입니다(Engine.cpp).
    struct FrameState;
    struct LoopState;
    bool beginLoop(FrameState& f);   // 거짓이면 루프를 끝냄
    void beginXrFrame(FrameState& f);
    void applyViewPose(FrameState& f);
    void readDesktopInput(FrameState& f, LoopState& loop);
    void updatePlayState(FrameState& f, LoopState& loop);
    void tickFrame(FrameState& f);
    void frontendInput(FrameState& f);
    void renderFrame(FrameState& f);
    void updateAccessibility(const FrameState& f);   // 8 앞: 비네트 · 자막 큐 · 자막 자리(Docs/Accessibility.md)
    void endLoop(FrameState& f, LoopState& loop);

    // 프로젝트 설정(assets/project.ini, 2026-10-15). 에셋 루트를 정한 뒤 한 번 읽습니다.
    ProjectSettings                project_;

    // ── 앞단 정책과 XR 실행 상태 (2026-10-19) ────────────────────────────────────
    IEngineFrontend::Policy          policy_;   // 부팅할 때 앞단에서 한 번 받음(에셋 루트 · VR 시작 · XR 처리 규칙)
    bool                             xrWanted_       = false;   // 이번 실행에서 VR 을 원함(명령줄 또는 정책)
    bool                             xrFocusPaused_  = false;   // 헤드셋 초점을 잃어 게임을 멈춘 중(policy_.pauseOnXrFocusLoss)

    // ── 창 없는 실행 · 합성 눈 · 자동 확인 (2026-10-31, 패키징 2단계) ─────────────────────────
    EngineOptions options_;                    // 부팅할 때 받은 옵션(프레임 수 · 시간 제한 · 캡처 경로)
    bool          headless_       = false;     // 창이 표면을 주지 않음(--headless · Android)
    bool          synthetic_      = false;     // 합성 눈을 붙였음(XR 세션과 배타)
    uint32_t      drawnFrames_    = 0;         // 그린 프레임 수(XR · 합성 눈 · 데스크톱)
    bool          debugCrashDone_ = false;     // --debug-crash 를 한 번 일으켰는지
    size_t        switchNext_     = 0;         // --switch-scene 다음 항목
    size_t        settingNext_    = 0;         // --user-setting-at 다음 항목
    size_t        consoleNext_    = 0;         // --console-at 다음 항목
    void          streamAssets();             // 프레임 처음: 업로드 예산 · 씬 전환(비동기 로딩)
    uint32_t      syntheticImage_ = 0;         // 다음에 그릴 합성 눈 이미지
    uint32_t      lastSynthetic_  = UINT32_MAX;   // 마지막으로 그린 합성 눈 이미지(캡처용)
    bool          timedOut_       = false;

    // ── 프레임 단계가 쓰는 객체 (2026-11-18 S2, 예전 Engine 멤버 · 메인 루프 구간을 옮김) ─────────────────
    FrameStats                           stats_;       // FPS 제목 · 프레임 시간 분포 · 끊김 로그 · 시작 시간
    std::unique_ptr<XrSessionController> xrSession_;   // Play 에 맞춘 세션 · 손실 처리 · 다시 열기(DEF-155)
    std::unique_ptr<XrViewOrigin>        viewOrigin_;  // 화면 카메라의 XR 원점(씬 리그 · 추적 부모 · 보이지 않는 리그)
    WorldUiDriver                        worldUi_;     // 따라오는 패널 · 마우스 · XR 손 포인터

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
    PhysicsSystem*                 physics_ = nullptr;   // modules_ 소유. XR 주사율에 스텝을 맞출 때 · 패널 가림 판정에 씁니다
    AudioSystem*                   audio_   = nullptr;   // modules_ 소유. XR 중 청취자를 머리 자세에 둘 때 씁니다

    // ── 앞단 (에디터 또는 게임, 2026-10-19) ─────────────────────────────────────
    // 생성자 첫머리에 받아 정책을 읽고, init 은 코어 서비스가 생긴 뒤에 부릅니다. 예전 에디터 자리라 파괴 순서도 같습니다
    // (게임 모듈 다음, 동적 모듈 · 에셋 · 월드 · 렌더러보다 먼저).
    std::unique_ptr<IEngineFrontend> frontend_;

    // ── 게임 모듈 + 프레임 컨텍스트 ─────────────────────────────────────────────
    std::unique_ptr<IGameModule>   game_;
    std::unique_ptr<EngineContext> ctx_;
    RenderSceneInput               sceneInput_;
};
