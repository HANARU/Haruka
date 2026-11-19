#include "CrashHandler.hpp"

#include "HCheck.hpp"
#include "HLog.hpp"
#include "../vfs/PathUtf8.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#pragma comment(lib, "dbghelp.lib")
#elif defined(__ANDROID__) || defined(__linux__)
#include <cxxabi.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unwind.h>
#endif

namespace fs = std::filesystem;

static constinit HLogCategory LogCrash{ "Crash", ELogVerbosity::Log };

namespace hcrash {
namespace {

Options               g_opt;
std::atomic<bool>     g_installed{ false };
std::atomic<uint32_t> g_ensures{ 0 };
std::mutex            g_reportMutex;   // 보통 경로(ensure 의 콜 스택 · writeReport)만 씀
fs::path              g_lastReport;

// 실행 시각 "YYYYMMDD-HHMMSS".
std::string stamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

// 기호 정보의 원본 파일 경로를 보고서에 맞게 줄입니다("…/src/core/X.cpp" → "core/X.cpp").
[[maybe_unused]] std::string trimSource(const std::string& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    for (const char* key : { "/src/", "/tests/", "/Projects/" }) {
        const size_t at = p.rfind(key);
        if (at != std::string::npos) return p.substr(at + std::strlen(key));
    }
    const size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

void prune(const fs::path& root, size_t keep) {
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) dirs.push_back(it->path());
    if (dirs.size() <= keep) return;
    std::sort(dirs.begin(), dirs.end());   // 이름이 시각으로 시작하므로 오래된 것이 앞
    for (size_t i = 0; i + keep < dirs.size(); ++i) fs::remove_all(dirs[i], ec);
}

fs::path newReportDir() {
    std::error_code ec;
    fs::create_directories(g_opt.root, ec);
    const std::string base = stamp() + "_" + g_opt.app;
    fs::path dir = g_opt.root / base;
    for (int n = 2; fs::exists(dir, ec); ++n) dir = g_opt.root / (base + "-" + std::to_string(n));
    fs::create_directories(dir, ec);
    return dir;
}

void writePending(const fs::path& dir) {
    std::ofstream f(g_opt.root / "pending.txt", std::ios::binary | std::ios::trunc);
    f << hpak::utf8Of(dir.filename());
}

std::string recentLog() {
    std::vector<std::string> lines;
    if (!HLog::tryRecentLines(64, lines)) return "  (the log was busy when the program stopped; see the log file)\n";
    std::string out;
    for (const auto& l : lines) out += "  " + l + "\n";
    return out;
}

#if defined(_WIN32)
// ── Windows ──────────────────────────────────────────────────────────────────────────────────
constexpr DWORD kFatalCode = 0xE0484B31;   // 'HK1': HCHECK · terminate · abort 등 우리가 일으킨 끝

std::mutex g_symMutex;   // DbgHelp 는 스레드 안전하지 않음
bool       g_symReady = false;

void symInit() {
    if (g_symReady) return;
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    // 기호 검색 경로: 실행 파일 폴더(PDB 는 실행 파일 옆, 실행 파일에는 PDB 이름만 들어 있음, D4).
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring dir = fs::path(exe).parent_path().wstring();
    g_symReady = SymInitializeW(GetCurrentProcess(), dir.c_str(), TRUE) != FALSE;
}

std::string moduleOf(DWORD64 addr, DWORD64* base) {
    HMODULE mod = nullptr;
    *base = 0;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &mod) || !mod)
        return "?";
    *base = reinterpret_cast<DWORD64>(mod);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(mod, path, MAX_PATH);
    return hpak::utf8Of(fs::path(path).filename());
}

std::string frameText(DWORD64 pc) {
    char line[1024];
    DWORD64 base = 0;
    const std::string mod = moduleOf(pc, &base);
    std::snprintf(line, sizeof(line), "%s+0x%llx", mod.c_str(), (unsigned long long)(pc - base));
    std::string out = line;
    if (g_symReady) {
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512] = {};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 511;
        DWORD64 disp = 0;
        if (SymFromAddr(GetCurrentProcess(), pc, &disp, sym)) {
            std::snprintf(line, sizeof(line), "  %s + 0x%llx", sym->Name, (unsigned long long)disp);
            out += line;
            IMAGEHLP_LINE64 li{};
            li.SizeOfStruct = sizeof(li);
            DWORD ldisp = 0;
            if (SymGetLineFromAddr64(GetCurrentProcess(), pc, &ldisp, &li) && li.FileName)
                out += "  (" + trimSource(li.FileName) + ":" + std::to_string(li.LineNumber) + ")";
        }
    }
    return out;
}

std::string walk(const CONTEXT& start, HANDLE thread, int skip) {
    CONTEXT ctx = start;
    STACKFRAME64 f{};
#if defined(_M_X64)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    f.AddrPC.Offset    = ctx.Rip;
    f.AddrFrame.Offset = ctx.Rbp;
    f.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_ARM64)
    const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
    f.AddrPC.Offset    = ctx.Pc;
    f.AddrFrame.Offset = ctx.Fp;
    f.AddrStack.Offset = ctx.Sp;
#else
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    f.AddrPC.Offset    = ctx.Eip;
    f.AddrFrame.Offset = ctx.Ebp;
    f.AddrStack.Offset = ctx.Esp;
#endif
    f.AddrPC.Mode = f.AddrFrame.Mode = f.AddrStack.Mode = AddrModeFlat;
    std::string out;
    for (int i = 0, shown = 0; i < 64; ++i) {
        if (!StackWalk64(machine, GetCurrentProcess(), thread, &f, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (f.AddrPC.Offset == 0) break;
        if (i < skip) continue;
        out += "  #" + std::to_string(shown++) + "  " + frameText(f.AddrPC.Offset) + "\n";
    }
    return out.empty() ? "  (no frames)\n" : out;
}

const char* exceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
        case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
        case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
        case EXCEPTION_BREAKPOINT:            return "breakpoint";
        case 0xC0000409:                      return "stack buffer overrun / fail fast";
        case 0xC0000374:                      return "heap corruption";
        case 0xE06D7363:                      return "C++ exception";
        case kFatalCode:                      return "fatal error";
        default:                              return "exception";
    }
}

// 도우미 스레드에 넘기는 것.
struct Job {
    EXCEPTION_POINTERS* ep = nullptr;
    DWORD               threadId = 0;
    char                reason[1024] = {};
};
Job              g_job;
HANDLE           g_wake = nullptr, g_done = nullptr;
std::atomic<bool> g_busy{ false };

void writeReportFiles(const Job& job) {
    const fs::path dir = newReportDir();
    // 미니덤프
    {
        HANDLE f = CreateFileW((dir / "crash.dmp").wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId          = job.threadId;
            mei.ExceptionPointers = job.ep;
            mei.ClientPointers    = FALSE;
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
                                                         MiniDumpWithIndirectlyReferencedMemory);
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, type, job.ep ? &mei : nullptr, nullptr, nullptr);
            CloseHandle(f);
        }
    }
    // 글 보고서
    std::string txt = "Haruka crash report\n";
    txt += "app:    " + g_opt.app + "\n";
    txt += "time:   " + stamp() + "\n";
    txt += "thread: " + std::to_string(job.threadId) + "\n";
    std::string reason = job.reason;
    if (job.ep && job.ep->ExceptionRecord) {
        const EXCEPTION_RECORD& r = *job.ep->ExceptionRecord;
        char buf[512];
        DWORD64 base = 0;
        const std::string mod = moduleOf(reinterpret_cast<DWORD64>(r.ExceptionAddress), &base);
        std::snprintf(buf, sizeof(buf), "exception 0x%08lX (%s) at %s+0x%llx", (unsigned long)r.ExceptionCode,
                      exceptionName(r.ExceptionCode), mod.c_str(),
                      (unsigned long long)(reinterpret_cast<DWORD64>(r.ExceptionAddress) - base));
        std::string ex = buf;
        if (r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r.NumberParameters >= 2) {
            std::snprintf(buf, sizeof(buf), ", %s address 0x%llx",
                          r.ExceptionInformation[0] == 0 ? "reading" : r.ExceptionInformation[0] == 1 ? "writing" : "executing",
                          (unsigned long long)r.ExceptionInformation[1]);
            ex += buf;
        }
        if (reason.empty()) reason = ex;
        txt += "exception: " + ex + "\n";
    }
    txt += "reason: " + reason + "\n\ncall stack:\n";
    {
        std::lock_guard<std::mutex> lock(g_symMutex);
        symInit();
        HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, job.threadId);
        if (job.ep && job.ep->ContextRecord) txt += walk(*job.ep->ContextRecord, th ? th : GetCurrentThread(), 0);
        if (th) CloseHandle(th);
    }
    txt += "\nrecent log:\n" + recentLog();
    txt += "\nOpen crash.dmp in Visual Studio or WinDbg with the matching .pdb files next to the executable.\n";
    std::ofstream(dir / "crash.txt", std::ios::binary | std::ios::trunc) << txt;
    writePending(dir);
    g_lastReport = dir;
    // 로그 체계는 멈췄을 수 있으므로 표준 오류 핸들에 바로 씁니다.
    const std::string note = "[Crash] report written to " + hpak::utf8Of(dir) + "\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), note.data(), (DWORD)note.size(), &written, nullptr);
}

DWORD WINAPI helperMain(void*) {
    WaitForSingleObject(g_wake, INFINITE);
    writeReportFiles(g_job);
    SetEvent(g_done);
    return 0;
}

// 도우미 스레드에 보고서를 맡기고 끝날 때까지 기다립니다. 이미 다른 크래시를 처리 중이면 false.
bool handOff(EXCEPTION_POINTERS* ep, const char* reason) {
    if (g_busy.exchange(true)) return false;
    g_job.ep       = ep;
    g_job.threadId = GetCurrentThreadId();
    if (reason) strncpy_s(g_job.reason, reason, _TRUNCATE);
    SetEvent(g_wake);
    WaitForSingleObject(g_done, 60000);
    return true;
}

LONG WINAPI onUnhandled(EXCEPTION_POINTERS* ep) {
    if (!handOff(ep, nullptr)) return EXCEPTION_CONTINUE_SEARCH;
    return EXCEPTION_EXECUTE_HANDLER;   // 프로세스를 예외 코드로 끝냄(오류 대화상자 없음)
}

// 예외가 아닌 끝(HCHECK · terminate · abort …): 지금 상태를 떠서 같은 길로 보냅니다.
[[noreturn]] void fatalHere(const char* reason) {
    static CONTEXT ctx;   // 스택을 아끼려고 정적
    RtlCaptureContext(&ctx);
    static EXCEPTION_RECORD rec;
    rec.ExceptionCode    = kFatalCode;
    rec.ExceptionAddress = reinterpret_cast<void*>(
#if defined(_M_X64)
        ctx.Rip
#elif defined(_M_ARM64)
        ctx.Pc
#else
        ctx.Eip
#endif
    );
    static EXCEPTION_POINTERS ep;
    ep.ExceptionRecord = &rec;
    ep.ContextRecord   = &ctx;
    handOff(&ep, reason);
    TerminateProcess(GetCurrentProcess(), 1);
    std::_Exit(1);
}

void onTerminate() {
    std::string why = "std::terminate";
    if (auto ex = std::current_exception()) {
        try { std::rethrow_exception(ex); }
        catch (const std::exception& e) { why = std::string("unhandled C++ exception: ") + e.what(); }
        catch (...) { why = "unhandled C++ exception (not std::exception)"; }
    }
    fatalHere(why.c_str());
}
void onPureCall() { fatalHere("pure virtual function call"); }
void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    fatalHere("invalid parameter passed to a C runtime function");
}
void onAbortSignal(int) { fatalHere("abort()"); }

bool installPlatform() {
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_wake || !g_done) return false;
    HANDLE t = CreateThread(nullptr, 256 * 1024, helperMain, nullptr, 0, nullptr);
    if (!t) return false;
    CloseHandle(t);
    // Windows 오류 대화상자를 띄우지 않습니다(숨은 창 자동 실행이 대화상자에서 멈추지 않게).
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetUnhandledExceptionFilter(onUnhandled);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
    std::signal(SIGABRT, onAbortSignal);
    // 스택 넘침 때 처리기가 쓸 자리(이 스레드). 덤프 자체는 도우미 스레드가 씁니다.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    return true;
}

std::string currentStack(int skip) {
    CONTEXT ctx{};
    RtlCaptureContext(&ctx);
    std::lock_guard<std::mutex> lock(g_symMutex);
    symInit();
    return walk(ctx, GetCurrentThread(), skip + 1);
}

#else
// ── Android · Linux ────────────────────────────────────────────────────────────────────────
// 시그널 처리기 안에서는 메모리 할당 · 잠금 · stdio 를 쓰지 않습니다. 경로는 설치할 때 만들어 둡니다.
constexpr int kSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
struct sigaction g_old[sizeof(kSignals) / sizeof(kSignals[0])];
char             g_dirPath[1024];
char             g_txtPath[1100];
char             g_pendingPath[1100];
char             g_dirName[256];
char             g_reason[512];
std::atomic<bool> g_inHandler{ false };
alignas(16) char g_altStack[64 * 1024];

void put(int fd, const char* s) { if (s) { const ssize_t r = write(fd, s, strlen(s)); (void)r; } }
void putHex(int fd, uintptr_t v) {
    char buf[2 + 16 + 1];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; ++i) buf[2 + i] = "0123456789abcdef"[(v >> ((15 - i) * 4)) & 0xF];
    buf[18] = 0;
    put(fd, buf);
}
void putDec(int fd, long v) {
    char buf[24]; int n = 0; bool neg = v < 0; unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    do { buf[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22);
    if (neg) buf[n++] = '-';
    char out[24]; for (int i = 0; i < n; ++i) out[i] = buf[n - 1 - i]; out[n] = 0;
    put(fd, out);
}
const char* signalName(int s) {
    switch (s) {
        case SIGSEGV: return "SIGSEGV (bad memory access)";
        case SIGBUS:  return "SIGBUS (bus error)";
        case SIGFPE:  return "SIGFPE (arithmetic error)";
        case SIGILL:  return "SIGILL (illegal instruction)";
        case SIGABRT: return "SIGABRT (abort)";
        default:      return "signal";
    }
}

struct UnwindState { int fd; int n; };
_Unwind_Reason_Code unwindOne(_Unwind_Context* ctx, void* arg) {
    auto* st = static_cast<UnwindState*>(arg);
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (!pc || st->n >= 64) return _URC_END_OF_STACK;
    put(st->fd, "  #"); putDec(st->fd, st->n++); put(st->fd, "  pc "); putHex(st->fd, pc);
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
        const char* base = strrchr(info.dli_fname, '/');
        put(st->fd, "  "); put(st->fd, base ? base + 1 : info.dli_fname);
        put(st->fd, "+"); putHex(st->fd, pc - reinterpret_cast<uintptr_t>(info.dli_fbase));
        if (info.dli_sname) { put(st->fd, "  "); put(st->fd, info.dli_sname); }
    }
    put(st->fd, "\n");
    return _URC_NO_REASON;
}

void onSignal(int sig, siginfo_t* info, void* uctx) {
    if (!g_inHandler.exchange(true)) {
        mkdir(g_dirPath, 0770);
        const int fd = open(g_txtPath, O_WRONLY | O_CREAT | O_TRUNC, 0660);
        if (fd >= 0) {
            put(fd, "Haruka crash report\napp:    "); put(fd, g_opt.app.c_str());
            put(fd, "\nsignal: "); put(fd, signalName(sig));
            put(fd, "\ncode:   "); putDec(fd, info ? info->si_code : 0);
            put(fd, "\nfault address: "); putHex(fd, info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0);
            put(fd, "\nreason: "); put(fd, g_reason[0] ? g_reason : signalName(sig));
            put(fd, "\n\ncall stack (from the signal handler; the frames after the handler are the program):\n");
            UnwindState st{ fd, 0 };
            _Unwind_Backtrace(unwindOne, &st);
            put(fd, "\nThe system tombstone (adb bugreport) has the full register state.\n");
            close(fd);
            const int pf = open(g_pendingPath, O_WRONLY | O_CREAT | O_TRUNC, 0660);
            if (pf >= 0) { put(pf, g_dirName); close(pf); }
        }
    }
    (void)uctx;
    // 원래 처리기로 돌려 시스템 tombstone 도 남깁니다.
    for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i)
        if (kSignals[i] == sig) sigaction(sig, &g_old[i], nullptr);
    raise(sig);
}

[[noreturn]] void fatalHere(const char* reason) {
    if (reason) { strncpy(g_reason, reason, sizeof(g_reason) - 1); g_reason[sizeof(g_reason) - 1] = 0; }
    std::abort();
}

void onTerminate() {
    std::string why = "std::terminate";
    if (auto ex = std::current_exception()) {
        try { std::rethrow_exception(ex); }
        catch (const std::exception& e) { why = std::string("unhandled C++ exception: ") + e.what(); }
        catch (...) { why = "unhandled C++ exception (not std::exception)"; }
    }
    fatalHere(why.c_str());
}

bool installPlatform() {
    // 보고서 자리는 시작 시각으로 미리 정합니다(시그널 안에서는 시각을 글로 바꾸지 않음).
    const fs::path dir = g_opt.root / (stamp() + "_" + g_opt.app);
    std::error_code ec;
    fs::create_directories(g_opt.root, ec);
    const std::string d = dir.string(), name = dir.filename().string();
    const std::string txt = (dir / "crash.txt").string(), pending = (g_opt.root / "pending.txt").string();
    if (d.size() >= sizeof(g_dirPath) || txt.size() >= sizeof(g_txtPath) || pending.size() >= sizeof(g_pendingPath)) return false;
    std::strcpy(g_dirPath, d.c_str());
    std::strcpy(g_txtPath, txt.c_str());
    std::strcpy(g_pendingPath, pending.c_str());
    std::strncpy(g_dirName, name.c_str(), sizeof(g_dirName) - 1);
    stack_t ss{};
    ss.ss_sp    = g_altStack;
    ss.ss_size  = sizeof(g_altStack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);
    for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i) {
        struct sigaction sa{};
        sa.sa_sigaction = onSignal;
        sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(kSignals[i], &sa, &g_old[i]);
    }
    std::set_terminate(onTerminate);
    g_lastReport = dir;   // 쓰이면 이 자리
    return true;
}

struct StackState { std::string* out; int skip; int n; };
_Unwind_Reason_Code stackOne(_Unwind_Context* ctx, void* arg) {
    auto* st = static_cast<StackState*>(arg);
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (!pc || st->n >= 64) return _URC_END_OF_STACK;
    if (st->skip-- > 0) return _URC_NO_REASON;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "  #%d  pc 0x%zx", st->n++, (size_t)pc);
    *st->out += buf;
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
        const char* base = strrchr(info.dli_fname, '/');
        std::snprintf(buf, sizeof(buf), "+0x%zx", (size_t)(pc - reinterpret_cast<uintptr_t>(info.dli_fbase)));
        *st->out += std::string("  ") + (base ? base + 1 : info.dli_fname) + buf;
        if (info.dli_sname) {
            int status = 0;
            char* dem = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            *st->out += std::string("  ") + (status == 0 && dem ? dem : info.dli_sname);
            std::free(dem);
        }
    }
    *st->out += "\n";
    return _URC_NO_REASON;
}

std::string currentStack(int skip) {
    std::string out;
    StackState st{ &out, skip + 1, 0 };
    _Unwind_Backtrace(stackOne, &st);
    return out.empty() ? "  (no frames)\n" : out;
}
#endif

} // namespace

// ── 공통 ─────────────────────────────────────────────────────────────────────────────────────
bool install(const Options& options) {
    if (g_installed.load()) return true;
    g_opt = options;
    if (g_opt.app.empty()) g_opt.app = "app";
    std::error_code ec;
    fs::create_directories(g_opt.root, ec);
    prune(g_opt.root, g_opt.keep > 0 ? g_opt.keep - 1 : 0);   // 이번 실행이 쓸 자리 하나를 남김
    if (!installPlatform()) return false;
    g_installed = true;
    return true;
}

bool installed() { return g_installed.load(); }

void fatal(const std::string& reason) {
    if (!g_installed.load()) {
        std::fprintf(stderr, "FATAL: %s\n", reason.c_str());
        std::abort();
    }
    fatalHere(reason.c_str());
}

fs::path writeReport(const std::string& reason) {
    if (!g_installed.load()) return {};
    std::lock_guard<std::mutex> lock(g_reportMutex);
    const fs::path dir = newReportDir();
    std::string txt = "Haruka crash report\napp:    " + g_opt.app + "\ntime:   " + stamp() + "\nreason: " + reason +
                      "\n\ncall stack (where the error was caught):\n" + currentStack(1) + "\nrecent log:\n" + recentLog();
    std::ofstream(dir / "crash.txt", std::ios::binary | std::ios::trunc) << txt;
    writePending(dir);
    g_lastReport = dir;
    return dir;
}

fs::path lastReport() { return g_lastReport; }

fs::path takePending(const fs::path& root) {
    std::error_code ec;
    const fs::path pending = root / "pending.txt";
    std::ifstream f(pending, std::ios::binary);
    if (!f) return {};
    std::string name((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    fs::remove(pending, ec);
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ')) name.pop_back();
    fs::path rel;
    if (name.empty() || !hpak::pathFromUtf8(name, rel)) return {};
    const fs::path dir = root / rel;
    return fs::is_directory(dir, ec) ? dir : fs::path{};
}

std::string callStack(int skip) { return currentStack(skip + 1); }

uint32_t ensureCount() { return g_ensures.load(); }

void checkFailed(const char* expr, const char* file, int line, const std::string& message) {
    std::string reason = std::string("Check failed: ") + expr + " (" + file + ":" + std::to_string(line) + ")";
    if (!message.empty()) reason += ": " + message;
    HLOG(LogCrash, Error) << reason;
    fatal(reason);
}

bool ensureFailed(const char* expr, const char* file, int line, std::atomic<bool>& reported) {
    g_ensures.fetch_add(1);
    if (reported.exchange(true)) return false;
    std::string stack;
    {
        std::lock_guard<std::mutex> lock(g_reportMutex);
        stack = currentStack(2);
    }
    HLOG(LogCrash, Error) << "Ensure failed: " << expr << " (" << file << ":" << line << "); reported once, the program goes on\n"
                          << stack;
#if defined(_WIN32)
    if (IsDebuggerPresent()) __debugbreak();
#endif
    return false;
}

// ── 개발 스위치 ───────────────────────────────────────────────────────────────────────────────
namespace {
struct PureBase {
    PureBase() { callIt(); }
    virtual ~PureBase() = default;
    virtual void pure() = 0;
    void callIt() { pure(); }   // 생성 중이라 순수 가상 호출이 됨
};
struct PureDerived : PureBase { void pure() override {} };

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4717)   // 모든 경로가 자기를 부름(의도)
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winfinite-recursion"
#endif
int recurse(int depth) {
    volatile char pad[4096];
    pad[0] = (char)depth;
    return recurse(depth + 1) + pad[0];
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace

bool debugCrash(const std::string& kind) {
    HLOG(LogCrash, Warning) << "Debug crash requested: " << kind;
    if (kind == "segv") {
        volatile int* p = nullptr;
        *p = 1;
    } else if (kind == "stack") {
        recurse(0);
    } else if (kind == "purecall") {
        PureDerived d;
        (void)d;
    } else if (kind == "abort") {
        std::abort();
    } else if (kind == "terminate") {
        std::terminate();
    } else if (kind == "throw") {
        throw std::runtime_error("debug crash (--debug-crash=throw)");
    } else if (kind == "check") {
        HCHECK_MSG(kind.empty(), "debug crash (--debug-crash=check)");
    } else if (kind == "ensure") {
        for (int i = 0; i < 2; ++i) HENSURE(kind.empty());
        return true;
    } else {
        return false;
    }
    return true;
}

} // namespace hcrash
