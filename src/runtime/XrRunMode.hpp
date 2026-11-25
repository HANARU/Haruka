#pragma once
#include <cstdint>
#include <optional>

// ── VR 자동 감지 · Play 실행 방식 (V1, 2026-11-24 사용자 결정) ─────────────────────────────────────────────
// 프로젝트 설정 xr.startInVr("VR auto-detect")가 켜져 있으면 에디터 · 게임이 시작할 때 OpenXR 을 준비하고, Play 때 헤드셋을
// 인식하면 VR 로 · 아니면 데스크톱으로 갑니다. 명령줄 --xr · --no-xr 이 언제나 이깁니다. 자동 시험(숨은 창 · --headless ·
// --synthetic-eyes)은 명령줄 --xr 이 없으면 프로젝트 값과 무관하게 OpenXR 을 켜지 않습니다(헤드셋 · SteamVR 에 닿지 않게).
// 에디터는 Play 버튼 오른쪽 메뉴의 "Play on Desktop" 으로 헤드셋이 있어도 이번 Play 만 데스크톱으로 갈 수 있습니다.
// 판단은 모두 여기의 순수 함수이고 단위 시험(tests/XrRunModeTest.cpp)이 지킵니다. 엔진(Engine · XrSessionController)이 부릅니다.
namespace hxrmode {

// ── 시작 때 OpenXR 을 준비할지 ──
enum class StartReason : uint8_t {
    CommandLineXr,     // 명령줄 --xr (Android 실행 파일은 늘 이것)
    CommandLineNoXr,   // 명령줄 --no-xr
    ProjectOn,         // 프로젝트 설정 xr.startInVr = true
    ProjectOff,        // 프로젝트 설정 xr.startInVr = false (기본)
    FrontendIgnores,   // 앞단 정책이 프로젝트 값을 보지 않음(Policy::startXrFromProject 가 거짓인 시험용 앞단)
    AutomatedRun,      // 프로젝트 값은 켜져 있지만 숨은 창 · --headless · --synthetic-eyes 실행이라 켜지 않음
};

struct StartupInput {
    std::optional<bool> commandLine;            // --xr = true, --no-xr = false, 없으면 비어 있음
    bool                projectAllowed = false; // 앞단 정책 Policy::startXrFromProject
    bool                projectValue   = false; // xr.startInVr
    bool                hiddenWindow   = false; // --hidden-window
    bool                headless       = false; // --headless
    bool                syntheticEyes  = false; // --synthetic-eyes
};

struct StartupDecision {
    bool        initXr = false;
    StartReason reason = StartReason::ProjectOff;
};

inline bool automatedRun(const StartupInput& in) { return in.hiddenWindow || in.headless || in.syntheticEyes; }

inline StartupDecision decideStartup(const StartupInput& in) {
    if (in.commandLine.has_value())
        return { *in.commandLine, *in.commandLine ? StartReason::CommandLineXr : StartReason::CommandLineNoXr };
    if (!in.projectAllowed) return { false, StartReason::FrontendIgnores };
    if (!in.projectValue)   return { false, StartReason::ProjectOff };
    if (automatedRun(in))   return { false, StartReason::AutomatedRun };
    return { true, StartReason::ProjectOn };
}

// AutomatedRun 로그에 적을 옵션 이름(앞의 것 하나).
inline const char* automatedFlag(const StartupInput& in) {
    return in.hiddenWindow ? "--hidden-window" : in.headless ? "--headless" : in.syntheticEyes ? "--synthetic-eyes" : "";
}

// ── Play 때 XR 세션을 열지 ──
enum class PlayMode : uint8_t {
    Auto,      // 세션을 쓸 수 있고 그 순간 헤드셋을 인식하면 VR, 아니면 데스크톱(기본 Play)
    Desktop,   // 헤드셋이 있어도 이번 Play 는 세션을 열지 않음(에디터 Play on Desktop)
};

enum class PlayLog : uint8_t {
    None,            // 남길 것 없음(세션을 열거나, VR 을 준비하지 않은 실행)
    DesktopChosen,   // Play on Desktop 을 골라 데스크톱으로(Log)
    NoHeadset,       // 헤드셋을 인식하지 못해 데스크톱으로(Log, 경고 아님)
};

struct PlayDecision {
    bool    openSession = false;
    PlayLog log         = PlayLog::None;
};

// 세션을 열기 전에 헤드셋을 확인해야 하는지. XR 이 필수인 플랫폼(Quest)은 확인 없이 늘 엽니다(지금과 같음).
inline bool needsHeadsetCheck(PlayMode mode, bool usable, bool xrRequired) {
    return usable && !xrRequired && mode == PlayMode::Auto;
}

// usable: 렌더러가 OpenXR 장치로 만들어져 세션을 열 수 있음. headsetPresent: needsHeadsetCheck 가 참일 때만 봅니다.
// 세션 생성 실패(경고)는 이 판단 뒤 XrSessionController::start 가 맡습니다.
inline PlayDecision decidePlay(PlayMode mode, bool usable, bool xrRequired, bool headsetPresent) {
    if (!usable)                  return { false, PlayLog::None };
    if (xrRequired)               return { true,  PlayLog::None };
    if (mode == PlayMode::Desktop) return { false, PlayLog::DesktopChosen };
    if (!headsetPresent)          return { false, PlayLog::NoHeadset };
    return { true, PlayLog::None };
}

// ── 앞단에 알리는 이번 실행의 XR 상태(에디터 Play 툴팁 · Project Settings 대화상자) ──
enum class HeadsetState : uint8_t { Unknown, Present, Absent };

struct XrRunStatus {
    StartReason  startReason       = StartReason::ProjectOff;
    bool         initTried         = false;   // 시작 때 OpenXR 을 준비하려 했음
    bool         usable            = false;   // 준비가 끝나 Play 에서 세션을 열 수 있음
    bool         standby           = false;   // 런타임은 있으나 시작 때 헤드셋이 없어 데스크톱으로 열림(헤드셋 대기 중)
    bool         projectAutoDetect = false;   // 시작 때 읽은 xr.startInVr
    HeadsetState headset           = HeadsetState::Unknown;   // 편집 중 마지막 헤드셋 확인 결과
    bool operator==(const XrRunStatus& o) const {
        return startReason == o.startReason && initTried == o.initTried && usable == o.usable &&
               standby == o.standby && projectAutoDetect == o.projectAutoDetect && headset == o.headset;
    }
    bool operator!=(const XrRunStatus& o) const { return !(*this == o); }
};

// Play 툴팁 · 대화상자 상태 줄의 종류(글은 앞단이 HTEXT 로 고름).
enum class StatusKind : uint8_t {
    HeadsetFound,     // VR 준비됨, 헤드셋 인식됨 → Play = VR
    NoHeadset,        // VR 준비됨, 헤드셋 없음 → Play = 데스크톱
    Ready,            // VR 준비됨, 아직 헤드셋을 확인하지 않음
    RestartNeeded,    // 시작 때 헤드셋이 없어 데스크톱으로 열렸으나 지금 헤드셋 연결됨 → 다시 시작 필요(PS-D11)
    NotPrepared,      // 프로젝트 값이 꺼져 있음(Project Settings 에서 켬)
    Unavailable,      // 준비하려 했지만 런타임 · 헤드셋이 없었음(이번 실행은 데스크톱만)
    DisabledByNoXr,   // 명령줄 --no-xr
    AutomatedRun,     // 숨은 창 · --headless · --synthetic-eyes 실행이라 준비하지 않음
};

inline StatusKind statusKind(const XrRunStatus& s) {
    if (s.usable) {
        if (s.headset == HeadsetState::Present) return StatusKind::HeadsetFound;
        if (s.headset == HeadsetState::Absent)  return StatusKind::NoHeadset;
        return StatusKind::Ready;
    }
    if (s.initTried) {
        if (s.standby && s.headset == HeadsetState::Present) return StatusKind::RestartNeeded;
        return StatusKind::Unavailable;
    }
    switch (s.startReason) {
        case StartReason::CommandLineNoXr: return StatusKind::DisabledByNoXr;
        case StartReason::AutomatedRun:    return StatusKind::AutomatedRun;
        default:                           return StatusKind::NotPrepared;
    }
}

// ── VR 늦은 연결 감지 · 알림 (PS-D11) ────────────────────────────────────────
struct StandbyInput {
    bool xrWanted        = false;
    bool instanceCreated = false;
    bool systemReady     = false;
    bool xrRequired      = false;
};

// 시작 때 OpenXR 인스턴스는 만들었지만 헤드셋이 없을 때 인스턴스를 남겨 대기할지.
// 런타임 자체가 없으면(instanceCreated == false) 확인하지 않습니다.
inline bool shouldStandbyForHeadset(const StandbyInput& in) {
    return in.xrWanted && in.instanceCreated && !in.systemReady && !in.xrRequired;
}

// 편집 중(Play 아님)에 헤드셋을 주기적으로 확인할지.
inline bool shouldPollHeadset(bool inGame, bool usable, bool standby) {
    return !inGame && (usable || standby);
}

struct LateConnectInput {
    bool standby         = false;
    bool headsetPresent  = false;
    bool alreadyNotified = false;
};

struct LateConnectDecision {
    bool notify       = false;
    bool markNotified = false;
};

// 대기 중 헤드셋이 잡히면 한 번만 알림을 띄웁니다.
inline LateConnectDecision decideLateConnect(const LateConnectInput& in) {
    if (!in.standby || !in.headsetPresent || in.alreadyNotified)
        return { false, in.alreadyNotified };
    return { true, true };
}

// 편집 중 헤드셋 확인 간격(초). 사용자 결정: 2초 이하.
inline constexpr double kHeadsetCheckIntervalSec = 2.0;

} // namespace hxrmode
