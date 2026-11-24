#pragma once

// ── XR 세션 제어 (2026-11-18 Engine::run 에서 떼어 냄, 엔진 구조 안정화 S2) ─────────────────────────────
// VR 세션을 Play 에 맞춰 열고 닫고(2026-10-15 사용자 결정: UE VR Preview · Unity 처럼 편집 중에는 헤드셋에 그리지 않음),
// 런타임이 세션을 끝낸 이유에 따라 데스크톱으로 계속하거나 · 다시 열기를 기다리거나 · 앱을 끝냅니다(DEF-155). 프레임마다
// XR 프레임을 시작하고 입력을 읽습니다. 로직은 예전 Engine 의 syncXrWithPlay · startXrSession · endXr · onXrInstanceLost 와
// 메인 루프의 XR 구간을 그대로 옮긴 것입니다.

#include "IEngineFrontend.hpp"
#include "XrRecovery.hpp"
#include "XrRunMode.hpp"
#include "../xr/XrSystem.hpp"

#include <functional>

class IWindow;
class Renderer;
class AudioSystem;

class XrSessionController {
public:
    struct Deps {
        XrSystem*                      xr       = nullptr;   // 없으면(데스크톱) 모든 호출이 아무 일도 하지 않음
        Renderer*                      renderer = nullptr;
        AudioSystem*                   audio    = nullptr;   // 세션 동안 헤드셋 소리 장치 · HRTF Auto
        IEngineFrontend*               frontend = nullptr;
        IWindow*                       window   = nullptr;   // 앱을 끝낼 때 닫기 요청
        const IEngineFrontend::Policy* policy   = nullptr;
        bool                           xrRequired = false;   // 데스크톱 화면이 없는 플랫폼(Quest)
        // 세션을 지운 직후(렌더러 · 입력을 되돌린 뒤) 부릅니다. Engine 이 프레임 시간 분포를 남기고 리그 원점을 초기화합니다.
        std::function<void()>          onSessionEnded;
    };
    void bind(const Deps& d) { d_ = d; }

    // 렌더러가 OpenXR 이 고른 장치로 만들어져 세션을 열 수 있음(부팅 때 한 번).
    void setUsable(bool on) { usable_ = on; }
    bool usable() const     { return usable_; }
    bool retryPending() const { return recovery_.pending; }   // 헤드셋 연결이 끊겨 다시 열기를 기다리는 중

    // ── 프레임 앞 (메인 루프 순서대로) ──
    // Play 상태와 실행 방식(Auto · Desktop)에 세션을 맞춥니다. Play 상태는 앞 프레임의 앞단 입력이 바꾸므로,
    // 프레임을 시작하기 전에 불러 진행 중인 XR 프레임이 있을 때 세션을 지우는 일이 없게 합니다.
    void syncWithPlay(bool inGame, hxrmode::PlayMode mode = hxrmode::PlayMode::Auto);
    // 세션이 없을 때도 XR 이벤트를 받습니다(DEF-155). 다시 열기를 기다리는 동안 인스턴스를 잃으면 그것을 알아야 끝없이 다시
    // 시도하지 않습니다.
    void pollWithoutSession();
    // 헤드셋 연결이 끊겼다가 돌아오면 세션을 다시 엽니다(정해진 간격, XR 필수 플랫폼은 마감 뒤 앱을 끝냄).
    void retryIfDue();
    // 세션이 있으면 이벤트를 받고, 잃었으면 이유에 따라 처리하고, 돌고 있으면 프레임을 시작해 입력을 읽습니다. 입력은 매
    // 프레임 새로 넣습니다(세션이 멈추면 빈 상태). 프레임을 시작했으면 참입니다(그러면 이번 프레임 안에 endFrame 이 필요함).
    bool beginFrame(XrSystem::FrameInfo& frame);

    // 세션을 끝냅니다(렌더러에서 스왑체인을 떼고 세션을 지움). OpenXR 인스턴스는 그것으로 만든 Vulkan 장치가 살아 있는 동안
    // 남겨 두고 엔진이 끝날 때 지웁니다. 물리 스텝을 60Hz 로 되돌립니다.
    void end(const char* reason);

private:
    bool start();            // 세션 + 스왑체인을 렌더러에 연결. 실패하면 이번 Play 는 데스크톱만
    void onInstanceLost();   // 이번 실행에서는 다시 열 수 없으므로 XR 을 끄고, XR 필수 플랫폼은 앱을 끝냄(DEF-155)

    Deps            d_;
    bool            usable_        = false;
    bool            inPlay_        = false;   // 현재 Play(일시정지 포함) 상태
    bool            sessionWanted_ = false;   // 이번 Play 에서 XR 세션을 원함
    hxr::XrRecovery recovery_;
};
