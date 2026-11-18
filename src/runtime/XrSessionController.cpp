#include "XrSessionController.hpp"

#include "../audio/AudioSystem.hpp"
#include "../core/FrameTimings.hpp"
#include "../core/HLog.hpp"
#include "../input/HInputSystem.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../platform/IWindow.hpp"
#include "../renderer/Renderer.hpp"

#include <cmath>
#include <stdexcept>

void XrSessionController::syncWithPlay(bool inGame) {
    if (!usable_ || inGame == playWanted_) return;
    playWanted_ = inGame;
    XrSystem& xr = *d_.xr;
    if (inGame) {
        // 앞 Play 의 세션이 아직 끝나는 중이면(Stop 직후 곧바로 Play) 다 끝난 뒤 루프가 다시 엽니다. XR 이 필수인 플랫폼
        // (Quest)에서 첫 세션을 못 만들면 이유를 남기고 멈춥니다. 다시 시도해도 같은 이유로 실패하기 때문입니다(그래픽
        // 연결 · 액션 바인딩). 연결이 잠시 끊겼다 돌아오는 경우는 아래의 다시 열기가 맡습니다.
        if (!xr.hasSession() && !start() && d_.xrRequired)
            throw std::runtime_error("XR is required on this platform, but the XR session could not be created "
                                     "(see the XR log lines above)");
    } else if (xr.hasSession() && !xr.requestExit()) {
        end("Play stopped");   // 돌고 있지 않으면(헤드셋을 벗음 등) 요청할 것이 없어 바로 지웁니다
    }
}

bool XrSessionController::start() {
    XrSystem& xr = *d_.xr;
    Renderer& renderer = *d_.renderer;
    if (!xr.createSession(renderer.getInstance(), renderer.getPhysDevice(), renderer.getDevice(),
                          renderer.graphicsQueueFamily(), renderer.getSwapFormat())) {
        xr.destroySession();
        HLOG(LogEngine, Warning) << "XR session could not be created; this Play runs on the desktop only";
        return false;
    }
    renderer.attachXrSwapchain(xr.swapchainImages(), xr.swapchainFormat(), xr.eyeExtent());
    HLOG(LogEngine, Log) << "XR session created for Play; drawing starts when the runtime reports READY";
    // XR Play 동안 소리를 헤드셋 장치로(2026-10-16, 에디터 설정에서 끌 수 있음). 세션이 끝나면 고른 장치로 돌아갑니다.
    if (d_.audio && d_.frontend->headsetAudioInVr() && !xr.headsetAudioOutputId().empty())
        d_.audio->setOutputOverride(xr.headsetAudioOutputId());
    if (d_.audio) d_.audio->setXrActive(true);   // HRTF 모드 Auto 는 XR 세션 동안 켜짐(2026-11-06)
    return true;
}

void XrSessionController::end(const char* reason) {
    if (!d_.xr || !d_.xr->hasSession()) return;
    d_.renderer->detachXrSwapchain();   // 장치가 쉬기를 기다린 뒤 이미지 뷰를 지움
    d_.xr->destroySession();
    HInputSystem::get().setXrInput({});
    if (d_.physics) d_.physics->setStepRate(60.f);
    if (d_.onSessionEnded) d_.onSessionEnded();   // 프레임 시간 분포 로그, 다음 Play 에서 바닥 · 원점 안내를 다시
    if (d_.audio && !d_.audio->outputOverride().empty()) d_.audio->setOutputOverride("");   // 고른 출력 장치로 돌아감
    if (d_.audio) d_.audio->setXrActive(false);
    HLOG(LogEngine, Log) << "XR session ended (" << reason << ")";
}

void XrSessionController::onInstanceLost() {
    end("the OpenXR instance was lost");
    usable_ = false;   // 다음 Play 에서도 세션을 만들지 않습니다(잃은 인스턴스로는 만들 수 없음)
    recovery_.cancel();
    if (d_.xrRequired) {
        HLOG(LogEngine, Error) << "XR: the OpenXR instance was lost and this platform has no desktop view; closing";
        d_.window->setShouldClose(true);
    } else {
        HLOG(LogEngine, Warning) << "XR: the OpenXR instance was lost; VR is off for the rest of this run";
    }
}

void XrSessionController::pollWithoutSession() {
    // 예전에는 세션이 있을 때만 받아 인스턴스 손실을 영영 받지 못했습니다(DEF-155).
    if (d_.xr && usable_ && !d_.xr->hasSession()) {
        d_.xr->pollEvents();
        if (d_.xr->lossReason() == XrSystem::LossReason::InstanceLost) onInstanceLost();
    }
}

void XrSessionController::retryIfDue() {
    // 헤드셋 연결이 끊겼다가 돌아오면 세션을 다시 엽니다(게임 실행 파일, 2026-10-19). 정해진 간격(처음 2초)으로 조용히
    // 확인합니다. XR 이 필수인 플랫폼은 데스크톱으로 계속할 수 없으므로, 처음 잃은 때부터 30초 안에 다시 돌지 않으면 끝냅니다
    // (DEF-155). 마감 시각은 다시 연 세션이 시작하지 못하고 또 잃어도 처음 것을 씁니다.
    if (!(recovery_.pending && d_.xr && !d_.xr->hasSession() && playWanted_)) return;
    const auto now = hxr::XrRecovery::Clock::now();
    if (recovery_.expired(now, d_.xrRequired)) {
        recovery_.cancel();
        HLOG(LogEngine, Error) << "XR: the session did not come back and this platform has no desktop view; closing";
        d_.window->setShouldClose(true);
    } else if (recovery_.due(now)) {
        if (d_.xr->headsetAvailable() && start()) {
            recovery_.pending = false;
            HLOG(LogEngine, Log) << "XR: the headset is back; session reopened";
        }
    }
}

bool XrSessionController::beginFrame(XrSystem::FrameInfo& frame) {
    if (!d_.xr || !d_.xr->hasSession()) return false;
    XrSystem& xr = *d_.xr;
    bool begun = false;
    xr.pollEvents();
    // 입력은 매 프레임 새로 넣습니다. 세션이 멈추면(헤드셋을 벗음 등) 빈 상태가 들어가, 마지막
    // 버튼 · 스틱 값이 눌린 채 남지 않습니다.
    HXrInputState xin;
    if (xr.sessionLost()) {
        const bool                 requested = xr.exitRequested();
        const XrSystem::LossReason why       = xr.lossReason();
        if (why == XrSystem::LossReason::InstanceLost) {
            // 인스턴스를 잃으면 무엇을 요청했든 이번 실행에서는 다시 열 수 없습니다(DEF-155).
            onInstanceLost();
        } else if (requested) {
            // Stop 으로 끝냈습니다. 끝내는 중에 다시 Play 를 눌렀으면 곧바로 엽니다.
            end("Play stopped");
            if (playWanted_) start();
        } else if (why == XrSystem::LossReason::Exiting && d_.policy->quitOnXrExit) {
            // 게임 실행 파일: 대시보드에서 종료하는 등 런타임이 끝내라고 하면 앱도 끝냅니다(2026-10-19, OpenXR 권고).
            end("the runtime asked the app to exit");
            d_.window->setShouldClose(true);
        } else if (why == XrSystem::LossReason::Disconnected && d_.policy->retryXrSession) {
            // 게임 실행 파일: 헤드셋 연결이 끊겼습니다. 데스크톱으로 계속하면서 돌아오기를 기다립니다.
            // 복구 중에 또 잃었으면(다시 연 세션이 시작하지 못함) 마감 시각은 그대로 두고 간격을 늘립니다(DEF-155).
            // 예전에는 손실 때마다 마감 시각을 새로 잡아, 시작에 계속 실패하는 세션이 2초마다 다시 열리며 끝나지 않았습니다.
            end("the headset disconnected; continuing on the desktop until it is back");
            if (recovery_.onDisconnected(hxr::XrRecovery::Clock::now())) {
                HLOG(LogEngine, Warning) << "XR: the reopened session was lost before it started; next try in "
                                         << recovery_.delay.count() << " s";
            }
        } else if (d_.xrRequired) {
            // XR 이 필수인 플랫폼(Quest)은 데스크톱으로 계속할 수 없으므로 끝냅니다(DEF-155). 예전에는 아래 에디터
            // 갈래로 가서 세션 없이 빈 화면인 채 끝나지 않았습니다.
            end("ended by the runtime");
            HLOG(LogEngine, Error) << "XR: the session ended and this platform has no desktop view; closing";
            d_.window->setShouldClose(true);
        } else {
            // 에디터: 런타임이 Play 중에 끝냈으면(대시보드에서 종료 등) Play 는 데스크톱으로 계속하고, 다음
            // Play 에서 다시 엽니다.
            end("ended by the runtime; Play continues on the desktop");
        }
    } else if (xr.sessionRunning()) {
        {
            ScopedCpuTimer tw(gFrameTimings().xrWait);
            begun = xr.beginFrame(frame);
        }
        if (begun) xr.syncInput(frame.displayTime, xin);
        // 다시 연 세션이 5초 동안 이어서 돌면 복구가 끝난 것으로 봅니다(DEF-155). 그 전에 또 잃으면 처음 마감 시각을 씁니다.
        if (begun && recovery_.onFrameStarted(hxr::XrRecovery::Clock::now())) {
            HLOG(LogEngine, Log) << "XR: the reopened session is running";
        }
        // 물리 스텝을 헤드셋 주사율에 맞춥니다(1/60 이면 72 · 90Hz 에서 물체가 떨려 보임).
        const float hz = xr.displayRefreshRate();
        if (d_.physics && hz > 1.f && std::abs(d_.physics->stepRate() - hz) > 0.5f) {
            d_.physics->setStepRate(hz);
            HLOG(LogEngine, Log) << "Physics step matched to the headset: " << hz << " Hz";
        }
    }
    HInputSystem::get().setXrInput(xin);
    return begun;
}
