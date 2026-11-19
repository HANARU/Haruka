// 크래시 보고(Docs/Crash_Reporting.md)를 검산합니다. 이 실행 파일이 자기 자신을 자식 프로세스로 띄워(--child <종류> <폴더>) 일부러
// 크래시시키고, 부모가 보고서 폴더 · crash.txt · crash.dmp · 다음 실행 알림 · 보관 수를 확인합니다. 창 · GPU 가 필요 없습니다.
//
// 실행: build/Release/HarukaCrashTest.exe

#include "../src/core/CrashHandler.hpp"
#include "../src/core/HCheck.hpp"
#include "../src/core/HLog.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

int g_failed = 0, g_passed = 0;
void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed; else ++g_failed;
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

#if defined(_WIN32)
fs::path selfPath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf);
}
// 자식을 띄워 끝날 때까지 기다리고 종료 코드를 돌려줍니다.
unsigned long runChild(const std::string& kind, const fs::path& root) {
    std::wstring cmd = L"\"" + selfPath().wstring() + L"\" --child " + std::wstring(kind.begin(), kind.end()) + L" \"" + root.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 0xFFFFFFFF;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 0xFFFFFFFF;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}
#endif

std::vector<fs::path> reportDirs(const fs::path& root) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) out.push_back(it->path());
    return out;
}

// ── 자식 ──────────────────────────────────────────────────────────────────────────
[[noreturn]] void throwUncaught() { throw std::runtime_error("uncaught test exception"); }

int child(const std::string& kind, const fs::path& root) {
    HLog::setConsoleSinkEnabled(false);
    HLog::openFileSink((root / "child.log").string());
    if (!hcrash::install({ root, "child", 10 })) return 90;
    static constinit HLogCategory LogTest{ "CrashTest", ELogVerbosity::Log };
    HLOG(LogTest, Log) << "child marker 4711";
    if (kind == "prune") return 0;
    if (kind == "caught") {
        try { hcrash::debugCrash("throw"); }
        catch (const std::exception& e) { hcrash::writeReport(std::string("FATAL: ") + e.what()); return 1; }
        return 91;
    }
    if (kind == "uncaught") throwUncaught();
    if (kind == "ensure") {
        hcrash::debugCrash("ensure");
        std::ofstream(root / "ensure.txt") << hcrash::ensureCount();
        HLog::closeFileSink();
        return 0;
    }
    hcrash::debugCrash(kind);
    return 92;   // 돌아오면 실패
}

// ── 부모 ──────────────────────────────────────────────────────────────────────────
struct Kind { const char* name; const char* keyword; bool dump; };

void testKinds(const fs::path& base) {
#if defined(_WIN32)
    const Kind kinds[] = {
        { "segv",      "access violation",                    true },
        { "stack",     "stack overflow",                      true },
        { "purecall",  "pure virtual function call",          true },
        { "abort",     "abort()",                             true },
        { "terminate", "std::terminate",                      true },
        { "uncaught",  "uncaught test exception|C++ exception", true },
        { "check",     "Check failed: kind.empty() (core/CrashHandler.cpp:", true },
        { "caught",    "FATAL: debug crash (--debug-crash=throw)", false },
    };
    for (const Kind& k : kinds) {
        std::printf("\n== %s ==\n", k.name);
        const fs::path root = base / k.name;
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
        const unsigned long code = runChild(k.name, root);
        const bool failedCode = code != 0 && code != 0xFFFFFFFFul && !(code >= 90 && code <= 92);
        check(failedCode, std::string("자식이 실패 코드로 끝남(") + std::to_string(code) + ")");
        const auto dirs = reportDirs(root);
        check(dirs.size() == 1, "보고서 폴더 하나");
        if (dirs.size() != 1) continue;
        const std::string txt = readAll(dirs[0] / "crash.txt");
        bool reasonFound = false;
        for (std::string alt : { std::string(k.keyword) }) {
            size_t pos = 0;
            while (pos <= alt.size()) {
                const size_t bar = alt.find('|', pos);
                const std::string one = alt.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
                reasonFound = reasonFound || txt.find(one) != std::string::npos;
                if (bar == std::string::npos) break;
                pos = bar + 1;
            }
        }
        check(reasonFound, std::string("crash.txt 에 이유: ") + k.keyword);
        check(txt.find("child marker 4711") != std::string::npos, "crash.txt 에 최근 로그");
        check(txt.find("call stack") != std::string::npos && txt.find("#0") != std::string::npos, "콜 스택 프레임");
        // PDB 가 있으면 이 시험의 함수 이름이 보입니다(Release 도 PDB 를 만듦, D4).
        check(txt.find("hcrash::") != std::string::npos || txt.find("::child") != std::string::npos ||
              txt.find("throwUncaught") != std::string::npos, "콜 스택에 함수 이름(기호)");
        check(txt.find(":\\") == std::string::npos && txt.find(":/") == std::string::npos, "보고서에 디스크 절대 경로 없음");
        if (k.dump) {
            std::error_code e2;
            check(fs::file_size(dirs[0] / "crash.dmp", e2) > 1000 && !e2, "crash.dmp(미니덤프)");
        }
        const fs::path pending = hcrash::takePending(root);
        check(pending == dirs[0] && hcrash::takePending(root).empty(), "다음 실행 알림은 한 번만");
    }
#else
    (void)base;
#endif
}

void testEnsure(const fs::path& base) {
#if defined(_WIN32)
    std::printf("\n== ensure ==\n");
    const fs::path root = base / "ensure";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    check(runChild("ensure", root) == 0, "HENSURE 는 계속 진행(종료 코드 0)");
    check(readAll(root / "ensure.txt") == "2", "실패 수 2(같은 자리 두 번)");
    const std::string log = readAll(root / "child.log");
    size_t n = 0;
    for (size_t at = log.find("Ensure failed"); at != std::string::npos; at = log.find("Ensure failed", at + 1)) ++n;
    check(n == 1, "로그는 한 번만");
    check(log.find("kind.empty() (core/CrashHandler.cpp:") != std::string::npos, "식 · 줄인 파일 이름");
    check(reportDirs(root).empty(), "보고서 폴더 없음");
#else
    (void)base;
#endif
}

void testPrune(const fs::path& base) {
#if defined(_WIN32)
    std::printf("\n== prune ==\n");
    const fs::path root = base / "prune";
    std::error_code ec;
    fs::remove_all(root, ec);
    for (int i = 0; i < 12; ++i) {
        char name[64];
        std::snprintf(name, sizeof(name), "20000101-0000%02d_old", i);
        fs::create_directories(root / name, ec);
    }
    check(runChild("prune", root) == 0, "자식 정상 종료");
    const auto dirs = reportDirs(root);
    check(dirs.size() == 9, "보관 10개 = 남은 9 + 이번 실행 자리 1(남은 " + std::to_string(dirs.size()) + ")");
    check(!fs::exists(root / "20000101-000000_old") && fs::exists(root / "20000101-000011_old"), "오래된 것부터 지움");
#else
    (void)base;
#endif
}

void testMacros() {
    std::printf("\n== macros ==\n");
    check(std::string(HCRASH_FILE) == "tests/CrashTest.cpp" || std::string(HCRASH_FILE) == "CrashTest.cpp",
          std::string("파일 이름을 줄임: ") + HCRASH_FILE);
    check(hcrash::detail::trimPos("E:\\x\\src\\core\\A.cpp") == 9 && hcrash::detail::trimPos("/a/b/c.cpp") == 3,
          "줄이는 자리");
    int n = 0;
    const bool ok = HENSURE(++n == 1);
    check(ok && n == 1, "HENSURE: 참이면 그대로, 식은 한 번 계산");
    HCHECK(n == 1);
    HCHECK_SLOW(n == 1);
    check(true, "HCHECK: 참이면 지나감");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--child") return child(argv[2], fs::path(argv[3]));
    HLog::setConsoleSinkEnabled(false);
    const fs::path base = fs::temp_directory_path() / "haruka_crash_test";
    testMacros();
    testKinds(base);
    testEnsure(base);
    testPrune(base);
    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("\n%s (%d passed, %d failed)\n", g_failed ? "FAILED" : "ALL PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
