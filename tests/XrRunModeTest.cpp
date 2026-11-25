// VR 자동 감지 · Play 실행 방식 · project.ini 부분 쓰기 단위 시험 (V1, 2026-11-24).
#include "../src/runtime/XrRunMode.hpp"
#include "../src/core/ProjectSettings.hpp"
#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", what); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", what); }
}

static void testStartupDecision() {
    std::printf("startup decision\n");
    {
        hxrmode::StartupInput in;
        in.commandLine    = true;
        in.projectAllowed = false;
        in.projectValue   = false;
        in.hiddenWindow   = true;
        const auto d = hxrmode::decideStartup(in);
        check(d.initXr && d.reason == hxrmode::StartReason::CommandLineXr,
              "CLI --xr overrides project=false, policy=false, and --hidden-window");
    }
    {
        hxrmode::StartupInput in;
        in.commandLine    = false;
        in.projectAllowed = true;
        in.projectValue   = true;
        const auto d = hxrmode::decideStartup(in);
        check(!d.initXr && d.reason == hxrmode::StartReason::CommandLineNoXr,
              "CLI --no-xr overrides project=true");
    }
    {
        hxrmode::StartupInput in;
        in.projectAllowed = true;
        in.projectValue   = true;
        const auto d = hxrmode::decideStartup(in);
        check(d.initXr && d.reason == hxrmode::StartReason::ProjectOn,
              "project xr.startInVr=true enables XR in normal run");
    }
    {
        hxrmode::StartupInput in;
        in.projectAllowed = true;
        in.projectValue   = false;
        const auto d = hxrmode::decideStartup(in);
        check(!d.initXr && d.reason == hxrmode::StartReason::ProjectOff,
              "project xr.startInVr=false keeps XR off");
    }
    {
        hxrmode::StartupInput in;
        in.projectAllowed = false;
        in.projectValue   = true;
        const auto d = hxrmode::decideStartup(in);
        check(!d.initXr && d.reason == hxrmode::StartReason::FrontendIgnores,
              "policy projectAllowed=false ignores project value");
    }
    for (int k = 0; k < 3; ++k) {
        hxrmode::StartupInput in;
        in.projectAllowed = true;
        in.projectValue   = true;
        if (k == 0) in.hiddenWindow  = true;
        if (k == 1) in.headless      = true;
        if (k == 2) in.syntheticEyes = true;
        const auto d = hxrmode::decideStartup(in);
        check(!d.initXr && d.reason == hxrmode::StartReason::AutomatedRun,
              "automated run (--hidden-window / --headless / --synthetic-eyes) skips XR without --xr");
    }
    {
        hxrmode::StartupInput in;
        in.hiddenWindow = true;
        check(std::string(hxrmode::automatedFlag(in)) == "--hidden-window", "automatedFlag hiddenWindow");
        in = {};
        in.headless = true;
        check(std::string(hxrmode::automatedFlag(in)) == "--headless", "automatedFlag headless");
        in = {};
        in.syntheticEyes = true;
        check(std::string(hxrmode::automatedFlag(in)) == "--synthetic-eyes", "automatedFlag syntheticEyes");
    }
}

static void testPlayDecision() {
    std::printf("play decision\n");
    check(hxrmode::needsHeadsetCheck(hxrmode::PlayMode::Auto, true, false),
          "needsHeadsetCheck true for Auto + usable + !xrRequired");
    check(!hxrmode::needsHeadsetCheck(hxrmode::PlayMode::Desktop, true, false),
          "needsHeadsetCheck false for Play on Desktop");
    check(!hxrmode::needsHeadsetCheck(hxrmode::PlayMode::Auto, false, false),
          "needsHeadsetCheck false when not usable");
    check(!hxrmode::needsHeadsetCheck(hxrmode::PlayMode::Auto, true, true),
          "needsHeadsetCheck false when xrRequired");

    {
        const auto d = hxrmode::decidePlay(hxrmode::PlayMode::Auto, true, false, true);
        check(d.openSession && d.log == hxrmode::PlayLog::None,
              "Auto + usable + headset present opens session with no log");
    }
    {
        const auto d = hxrmode::decidePlay(hxrmode::PlayMode::Auto, true, false, false);
        check(!d.openSession && d.log == hxrmode::PlayLog::NoHeadset,
              "Auto + usable + no headset falls back to desktop with NoHeadset log");
    }
    {
        const auto d = hxrmode::decidePlay(hxrmode::PlayMode::Desktop, true, false, true);
        check(!d.openSession && d.log == hxrmode::PlayLog::DesktopChosen,
              "Play on Desktop does not open session even with headset present");
    }
    {
        const auto d = hxrmode::decidePlay(hxrmode::PlayMode::Auto, false, false, true);
        check(!d.openSession && d.log == hxrmode::PlayLog::None,
              "not usable does not open session");
    }
    {
        const auto d = hxrmode::decidePlay(hxrmode::PlayMode::Desktop, true, true, false);
        check(d.openSession && d.log == hxrmode::PlayLog::None,
              "xrRequired always opens session");
    }
}

static void testStatusKind() {
    std::printf("status kind\n");
    hxrmode::XrRunStatus s;
    s.usable = true; s.headset = hxrmode::HeadsetState::Present;
    check(hxrmode::statusKind(s) == hxrmode::StatusKind::HeadsetFound, "status HeadsetFound");
    s.headset = hxrmode::HeadsetState::Absent;
    check(hxrmode::statusKind(s) == hxrmode::StatusKind::NoHeadset, "status NoHeadset");
    s.headset = hxrmode::HeadsetState::Unknown;
    check(hxrmode::statusKind(s) == hxrmode::StatusKind::Ready, "status Ready");

    s = {}; s.initTried = true; s.usable = false;
    check(hxrmode::statusKind(s) == hxrmode::StatusKind::Unavailable, "status Unavailable");
    s = {}; s.initTried = true; s.usable = false; s.standby = true; s.headset = hxrmode::HeadsetState::Absent;
    check(hxrmode::statusKind(s) == hxrmode::StatusKind::Unavailable, "status standby+Absent is Unavailable");
    s.headset = hxrmode::HeadsetState::Present;
    check(hxrmode::statusKind(s) == hxrmode::StatusKind::RestartNeeded, "status standby+Present is RestartNeeded");
    s = {}; s.startReason = hxrmode::StartReason::CommandLineNoXr;
    check(hxrmode::statusKind(s) == hxrmode::StatusKind::DisabledByNoXr, "status DisabledByNoXr");
    s = {}; s.startReason = hxrmode::StartReason::AutomatedRun;
    check(hxrmode::statusKind(s) == hxrmode::StatusKind::AutomatedRun, "status AutomatedRun");
    s = {}; s.startReason = hxrmode::StartReason::ProjectOff;
    check(hxrmode::statusKind(s) == hxrmode::StatusKind::NotPrepared, "status NotPrepared");
}

static void testIniEdit() {
    std::printf("project.ini edit\n");
    {
        const std::string lfIn =
            "# comment 1\n"
            "startScene = /Game/scenes/Start.hscene\n"
            "# comment 2\n"
            "xr.startInVr = false\n"
            "render.ao = true\n";
        const std::string lfOut = project_settings::setIniBool(lfIn, "xr.startInVr", true);
        const std::string expected =
            "# comment 1\n"
            "startScene = /Game/scenes/Start.hscene\n"
            "# comment 2\n"
            "xr.startInVr = true\n"
            "render.ao = true\n";
        check(lfOut == expected, "LF ini preserves comments, order, and other lines");
        std::vector<std::string> msgs;
        check(project_settings::parse(lfOut, msgs).xrStartInVr && msgs.empty(), "parsed updated LF ini is true");
    }
    {
        const std::string crlfIn =
            "# header\r\n"
            "xr.startInVr = true\r\n"
            "render.bloom = true\r\n";
        const std::string crlfOut = project_settings::setIniBool(crlfIn, "xr.startInVr", false);
        const std::string expected =
            "# header\r\n"
            "xr.startInVr = false\r\n"
            "render.bloom = true\r\n";
        check(crlfOut == expected, "CRLF ini preserves CRLF line endings");
    }
    {
        const std::string noKeyIn = "startScene = /Game/scenes/Start.hscene";
        const std::string noKeyOut = project_settings::setIniBool(noKeyIn, "xr.startInVr", true);
        check(noKeyOut == "startScene = /Game/scenes/Start.hscene\nxr.startInVr = true\n",
              "missing key appends at end with newline");
    }
    {
        const std::string commented = "# xr.startInVr = false\nrender.ao = true\n";
        const std::string out = project_settings::setIniBool(commented, "xr.startInVr", true);
        check(out == "# xr.startInVr = false\nrender.ao = true\nxr.startInVr = true\n",
              "commented line is preserved and not treated as active key");
    }
    check(project_settings::decideIniWrite(false, false) == project_settings::IniWriteAction::Write,
          "decideIniWrite allows creating a missing project.ini");
    check(project_settings::decideIniWrite(true, true) == project_settings::IniWriteAction::Write,
          "decideIniWrite allows writing when existing file was read");
    check(project_settings::decideIniWrite(true, false) == project_settings::IniWriteAction::AbortReadError,
          "decideIniWrite aborts when existing file failed to read");
}

static void testLateConnect() {
    std::printf("late connect standby and notification\n");
    check(hxrmode::shouldStandbyForHeadset({ true, true, false, false }),
          "standby true when xrWanted + instanceCreated + !systemReady + !xrRequired");
    check(!hxrmode::shouldStandbyForHeadset({ true, false, false, false }),
          "standby false when no OpenXR runtime (instanceCreated=false)");
    check(!hxrmode::shouldStandbyForHeadset({ true, true, true, false }),
          "standby false when systemReady");
    check(!hxrmode::shouldStandbyForHeadset({ true, true, false, true }),
          "standby false when xrRequired");
    check(!hxrmode::shouldStandbyForHeadset({ false, true, false, false }),
          "standby false when !xrWanted");

    check(hxrmode::shouldPollHeadset(false, true, false), "poll in editor when usable");
    check(hxrmode::shouldPollHeadset(false, false, true), "poll in editor when standby");
    check(!hxrmode::shouldPollHeadset(false, false, false), "no poll when neither usable nor standby");
    check(!hxrmode::shouldPollHeadset(true, true, true), "no poll during Play");

    const auto d1 = hxrmode::decideLateConnect({ true, false, false });
    check(!d1.notify && !d1.markNotified, "no notify when headset absent");
    const auto d2 = hxrmode::decideLateConnect({ true, true, false });
    check(d2.notify && d2.markNotified, "notify once when standby + headset present");
    const auto d3 = hxrmode::decideLateConnect({ true, true, d2.markNotified });
    check(!d3.notify && d3.markNotified, "do not notify again after alreadyNotified");
    const auto d4 = hxrmode::decideLateConnect({ false, true, false });
    check(!d4.notify && !d4.markNotified, "no notify when not in standby");
}

int main() {
    testStartupDecision();
    testPlayDecision();
    testStatusKind();
    testIniEdit();
    testLateConnect();
    std::printf("XrRunModeTest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
