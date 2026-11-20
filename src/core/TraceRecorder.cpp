#include "TraceRecorder.hpp"

#include "AppPaths.hpp"
#include "CVar.hpp"
#include "HLog.hpp"
#include "Profiler.hpp"

#include <chrono>
#include <ctime>
#include <vector>

// 추적 기록의 시작 · 끝 알림(기록할 때만 처음 쓰므로 평소 로그 분류 목록에는 나오지 않습니다).
namespace {
constinit HLogCategory LogTrace{ "Trace", ELogVerbosity::Log };
}

TraceRecorder::~TraceRecorder() { finish(); }

std::filesystem::path TraceRecorder::resolve(std::string_view rel, std::string* why) {
    auto fail = [&](const char* reason) {
        if (why) *why = reason;
        return std::filesystem::path{};
    };
    if (rel.empty()) return fail("empty file name");
    const std::filesystem::path p = std::filesystem::path(std::u8string(rel.begin(), rel.end())).lexically_normal();
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory())
        return fail("only paths relative to Traces/ under the write root are allowed");
    for (const auto& part : p)
        if (part == "..") return fail("'..' is not allowed in trace paths");
    if (!p.has_filename()) return fail("the path has no file name");
    return happ::inWriteRoot(std::filesystem::path("Traces") / p);
}

std::string TraceRecorder::defaultFileName() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "trace_%Y%m%d_%H%M%S.json", &tm);
    return buf;
}

bool TraceRecorder::start(std::string_view file, std::string* why) {
    if (active_ || hprof::recording()) {
        if (why) *why = "a trace is already recording" + (active_ ? " (" + path_.generic_string() + ")" : std::string());
        return false;
    }
    const std::string name = file.empty() ? defaultFileName() : std::string(file);
    std::string reason;
    std::filesystem::path p = resolve(name, &reason);
    if (p.empty()) {
        if (why) *why = name + ": " + reason;
        return false;
    }
    if (!hprof::start()) {
        if (why) *why = "a trace is already recording";
        return false;
    }
    active_ = true;
    path_   = std::move(p);
#if !HARUKA_PROFILER
    HLOG(LogTrace, Warning) << "This build has no profiling scopes (HARUKA_PROFILER=0); the trace will only hold engine timers";
#endif
    HLOG(LogTrace, Log) << "Trace started: " << path_.generic_string();
    return true;
}

bool TraceRecorder::stop(std::string* summary) {
    if (!active_) {
        if (summary) *summary = "no trace is recording";
        return false;
    }
    active_ = false;
    const hprof::Capture cap = hprof::stop();
    std::string why;
    const bool ok = hprof::writeChromeJson(path_, cap, &why);
    std::string text;
    if (ok) {
        const double ms = (double)(cap.endNs - cap.startNs) / 1e6;
        text = "Trace written: " + path_.generic_string() + " (" + std::to_string(cap.eventCount()) + " events, " +
               std::to_string(cap.threads.size()) + " threads, " + std::to_string((long long)(ms + 0.5)) + " ms, dropped " +
               std::to_string(cap.droppedCount()) + ")";
        HLOG(LogTrace, Log) << text;
    } else {
        text = "Trace not written: " + why;
        HLOG(LogTrace, Error) << text;
    }
    if (summary) *summary = text;
    return ok;
}

void TraceRecorder::configure(std::string_view file, uint32_t fromFrame, uint32_t toFrame) {
    if (fromFrame == 0 && toFrame == 0) {
        if (file.empty()) return;
        std::string why;
        if (!start(file, &why)) {
            HLOG(LogTrace, Warning) << "--trace: " << why;
        }
        return;
    }
    // --trace-frames 만 있으면 기본 이름으로 씁니다.
    pendingFile_ = file.empty() ? defaultFileName() : std::string(file);
    from_        = fromFrame;
    to_          = toFrame;
    framesDone_  = false;
}

void TraceRecorder::onFrameDrawn(uint32_t drawn) {
    if (pendingFile_.empty() || framesDone_) return;
    if (!active_ && drawn >= from_ && drawn < to_) {
        std::string why;
        if (!start(pendingFile_, &why)) {
            HLOG(LogTrace, Warning) << "--trace-frames: " << why;
            framesDone_ = true;
        } else {
            HLOG(LogTrace, Log) << "Trace frames " << from_ << "-" << to_ << ": started after frame " << drawn;
        }
        return;
    }
    if (active_ && drawn >= to_) {
        stop();
        framesDone_ = true;
    }
}

void TraceRecorder::finish() {
    if (active_) stop();
}

void TraceRecorder::registerCommands(CVarRegistry& cvars) {
    cvars.addCommand("trace.start", "trace.start [file]: record a CPU trace (Chrome trace.json) to Traces/ under the write root",
                     [this](const std::vector<std::string>& args) -> std::string {
                         if (args.size() > 1) return "usage: trace.start [file]";
                         std::string why;
                         if (!start(args.empty() ? std::string_view{} : std::string_view(args[0]), &why))
                             return "cannot start: " + why;
                         return "recording " + path_.generic_string();
                     });
    cvars.addCommand("trace.stop", "trace.stop: stop the CPU trace and write the file",
                     [this](const std::vector<std::string>& args) -> std::string {
                         if (!args.empty()) return "usage: trace.stop";
                         std::string summary;
                         stop(&summary);
                         return summary;
                     });
}
