#pragma once

// ── XR 세션 다시 열기 시각 (DEF-155, 2026-11-18 Engine::run 에서 떼어 냄) ─────────────────────────────
// 헤드셋 연결이 끊긴 뒤 세션을 다시 여는 시각과 복구 판정만 담은 순수 계산입니다(XrSessionController 가 씀). 헤드셋 없이
// 시험하려고 시각을 인자로 받습니다.
//   · 처음 잃은 때부터 다시 연 세션이 5초 동안 이어서 돌 때까지 "복구 중"입니다. 다시 연 세션이 시작하지 못하거나 곧 다시
//     잃어도 마감 시각은 처음 것을 유지하고, 다시 열기 간격은 2초에서 30초까지 두 배씩 늘립니다. 첫 프레임 하나로 복구를
//     판정하면 곧 잃을 세션(성공 코드 XR_SESSION_LOSS_PENDING)에서 마감 시각이 다시 잡혔습니다(2026-11-01 재검토).
//   · XR 이 필수인 플랫폼은 처음 잃은 때부터 30초 안에 다시 돌지 않으면 끝냅니다(데스크톱으로 계속할 수 없음).

#include <algorithm>
#include <chrono>

namespace hxr {

struct XrRecovery {
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static constexpr std::chrono::seconds kFirstDelay{ 2 };
    static constexpr std::chrono::seconds kMaxDelay{ 30 };
    static constexpr std::chrono::seconds kGiveUpAfter{ 30 };
    static constexpr std::chrono::seconds kRecoveredAfter{ 5 };

    bool                 pending    = false;   // 다시 열기를 기다리는 중
    TimePoint            retryAt{};            // 다음에 다시 열어 볼 시각
    bool                 recovering = false;   // 처음 잃은 때부터 복구가 끝날 때까지
    std::chrono::seconds delay{ kFirstDelay };
    TimePoint            runSince{};           // 다시 연 세션이 처음 프레임을 시작한 때(비면 아직)
    TimePoint            giveUpAt{};           // XR 필수 플랫폼의 마감 시각

    // 연결이 끊겨 세션을 지웠습니다. 복구 중에 또 잃었으면 참을 돌려줍니다(마감 시각은 그대로, 간격은 두 배).
    bool onDisconnected(TimePoint now) {
        runSince = {};
        bool again = false;
        if (!recovering) {
            recovering = true;
            delay      = kFirstDelay;
            giveUpAt   = now + kGiveUpAfter;
        } else {
            delay = std::min(delay * 2, kMaxDelay);
            again = true;
        }
        pending = true;
        retryAt = now + delay;
        return again;
    }

    // 다시 열기를 그만둡니다(인스턴스 손실 · 마감).
    void cancel() {
        pending    = false;
        recovering = false;
        runSince   = {};
    }

    // 필수 플랫폼의 마감을 넘겼는지.
    bool expired(TimePoint now, bool required) const { return required && now >= giveUpAt; }

    // 다시 열어 볼 때인지. 참이면 다음 시도 시각을 미리 잡습니다(이번 시도가 실패해도 delay 뒤에 다시).
    bool due(TimePoint now) {
        if (now < retryAt) return false;
        retryAt = now + delay;
        return true;
    }

    // 다시 연 세션이 프레임을 시작했습니다. 5초 동안 이어서 돌아 복구가 끝난 순간 참을 돌려줍니다.
    bool onFrameStarted(TimePoint now) {
        if (!recovering) return false;
        if (runSince == TimePoint{}) {
            runSince = now;
        } else if (now - runSince >= kRecoveredAfter) {
            recovering = false;
            runSince   = {};
            return true;
        }
        return false;
    }
};

} // namespace hxr
