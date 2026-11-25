#include "projectops/ProjectOps.hpp"
#include "core/FileUtil.hpp"
#include "core/ProjectDescriptor.hpp"
#include "vfs/PathUtf8.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace fs = std::filesystem;

namespace hprojops {
namespace {

std::string trim(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return std::string(s.substr(a, b - a));
}

bool isAsciiAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isAsciiAlnum(char c) {
    return isAsciiAlpha(c) || (c >= '0' && c <= '9');
}

std::string toLowerAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') out.push_back(char(c - 'A' + 'a'));
        else out.push_back(c);
    }
    return out;
}

std::string currentIsoUtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmUtc{};
#ifdef _WIN32
    gmtime_s(&tmUtc, &t);
#else
    gmtime_r(&t, &tmUtc);
#endif
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
                  tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec);
    return std::string(buf);
}

std::string normalizePathForCompare(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(p, ec);
    if (ec) abs = fs::absolute(p, ec);
    std::string s = hpak::utf8Of(abs.empty() ? p : abs);
#ifdef _WIN32
    s = toLowerAscii(s);
#endif
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::string normalizePathUtf8(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(p, ec);
    if (ec) abs = fs::absolute(p, ec);
    std::string s = hpak::utf8Of(abs.empty() ? p : abs);
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

OpResult makeFail(std::string msg, std::string hint = {}, int code = 1) {
    OpResult r;
    r.ok = false;
    r.exitCode = code;
    r.message = std::move(msg);
    r.fixHint = std::move(hint);
    return r;
}

OpResult makeOk(std::string msg) {
    OpResult r;
    r.ok = true;
    r.exitCode = 0;
    r.message = std::move(msg);
    return r;
}

bool readBytes(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return f.good() || f.eof();
}

bool writeBytes(const fs::path& p, const std::string& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.flush();
    return f.good();
}

bool looksBinary(const std::string& data) {
    const size_t checkLen = std::min<size_t>(data.size(), 4096);
    for (size_t i = 0; i < checkLen; ++i) {
        if (data[i] == '\0') return true;
    }
    return false;
}

std::string readEngineVersionFromCMake(const fs::path& engineRoot) {
    std::string text;
    if (!readBytes(engineRoot / "CMakeLists.txt", text)) return "0.1.0";
    const std::string needle = "project(HarukaEngine VERSION ";
    const size_t pos = text.find(needle);
    if (pos == std::string::npos) return "0.1.0";
    size_t i = pos + needle.size();
    std::string ver;
    while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) {
        ver.push_back(text[i++]);
    }
    return ver.empty() ? "0.1.0" : ver;
}

#ifdef _WIN32
std::wstring utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring w(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), needed);
    return w;
}

std::string wideToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string getEnvUtf8(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return {};
    std::wstring buf(static_cast<size_t>(needed), L'\0');
    const DWORD got = GetEnvironmentVariableW(name, buf.data(), needed);
    if (got == 0) return {};
    buf.resize(got);
    return wideToUtf8(buf);
}

std::wstring quoteWinArg(std::wstring_view arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(arg);
    }
    std::wstring out = L"\"";
    for (size_t i = 0; i < arg.size(); ++i) {
        size_t numBackslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++numBackslashes;
            ++i;
        }
        if (i == arg.size()) {
            out.append(numBackslashes * 2, L'\\');
            break;
        } else if (arg[i] == L'"') {
            out.append(numBackslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(numBackslashes, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

std::wstring buildCommandLine(const std::vector<std::string>& args) {
    std::wstring cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmd.push_back(L' ');
        cmd += quoteWinArg(utf8ToWide(args[i]));
    }
    return cmd;
}

struct ProcessOutput {
    bool launched = false;
    int exitCode = 1;
    std::string stdOutErr;
    std::string launchError;
};

struct ScopedEnvOverride {
    std::wstring key;
    std::wstring oldVal;
    bool hadOld = false;
    bool active = false;

    ScopedEnvOverride(std::wstring k, std::wstring newVal) : key(std::move(k)) {
        if (newVal.empty()) return;
        const DWORD needed = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
        if (needed > 0) {
            oldVal.resize(needed);
            const DWORD got = GetEnvironmentVariableW(key.c_str(), oldVal.data(), needed);
            oldVal.resize(got);
            hadOld = true;
        }
        SetEnvironmentVariableW(key.c_str(), newVal.c_str());
        active = true;
    }

    ~ScopedEnvOverride() {
        if (!active) return;
        if (hadOld) SetEnvironmentVariableW(key.c_str(), oldVal.c_str());
        else SetEnvironmentVariableW(key.c_str(), nullptr);
    }
};

ProcessOutput runProcessCapture(const std::vector<std::string>& args,
                                const fs::path& workingDir = {},
                                const std::string& enginesIniOverride = {},
                                const OutputLineCallback& onOutputLine = {}) {
    ProcessOutput res;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        res.launchError = "CreatePipe failed";
        return res;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = buildCommandLine(args);
    std::wstring workDirW = workingDir.empty() ? std::wstring{} : workingDir.wstring();

    ScopedEnvOverride envGuard(L"HARUKA_ENGINES_INI", utf8ToWide(enginesIniOverride));

    const BOOL ok = CreateProcessW(
        nullptr,
        cmdLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workDirW.empty() ? nullptr : workDirW.c_str(),
        &si,
        &pi
    );
    CloseHandle(writePipe);

    if (!ok) {
        const DWORD err = GetLastError();
        CloseHandle(readPipe);
        res.launchError = "CreateProcessW failed (error " + std::to_string(err) + ")";
        return res;
    }

    res.launched = true;
    char buf[4096];
    DWORD bytesRead = 0;
    std::string pending;
    while (ReadFile(readPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        res.stdOutErr.append(buf, bytesRead);
        if (onOutputLine) {
            pending.append(buf, bytesRead);
            size_t pos = 0;
            while (true) {
                const size_t nl = pending.find('\n', pos);
                if (nl == std::string::npos) break;
                std::string line = pending.substr(pos, nl - pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                onOutputLine(line);
                pos = nl + 1;
            }
            if (pos > 0) pending.erase(0, pos);
        }
    }
    if (onOutputLine && !pending.empty()) {
        if (pending.back() == '\r') pending.pop_back();
        if (!pending.empty()) onOutputLine(pending);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    res.exitCode = static_cast<int>(code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return res;
}

ProcessOutput launchProcess(const std::vector<std::string>& args,
                            const fs::path& workingDir,
                            bool waitForExit) {
    ProcessOutput res;
    const LaunchResult lr = launchExecutable(args, workingDir, waitForExit);
    res.launched = lr.launched;
    res.exitCode = lr.exitCode;
    res.launchError = lr.error;
    return res;
}
#endif

} // namespace

LaunchResult launchExecutable(const std::vector<std::string>& args,
                              const std::filesystem::path& workingDir,
                              bool waitForExit) {
    LaunchResult res;
#ifdef _WIN32
    if (args.empty()) {
        res.error = "No executable arguments";
        return res;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = buildCommandLine(args);
    std::wstring workDirW = workingDir.empty() ? std::wstring{} : workingDir.wstring();

    const BOOL ok = CreateProcessW(
        nullptr,
        cmdLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        workDirW.empty() ? nullptr : workDirW.c_str(),
        &si,
        &pi
    );
    if (!ok) {
        const DWORD err = GetLastError();
        res.error = "CreateProcessW failed (error " + std::to_string(err) + ")";
        return res;
    }
    res.launched = true;
    if (waitForExit) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        res.exitCode = static_cast<int>(code);
    } else {
        res.exitCode = 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    (void)args;
    (void)workingDir;
    (void)waitForExit;
    res.error = "launchExecutable is only supported on Windows";
#endif
    return res;
}

std::string formatCommandLine(const std::vector<std::string>& args) {
#ifdef _WIN32
    return wideToUtf8(buildCommandLine(args));
#else
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out.push_back(' ');
        out += args[i];
    }
    return out;
#endif
}

std::filesystem::path currentExecutablePath() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return {};
        if (len < buf.size()) {
            buf.resize(len);
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
#else
    return {};
#endif
}

uint32_t currentProcessId() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return 0;
#endif
}

bool isValidProjectName(std::string_view name) {
    if (name.empty()) return false;
    if (!(isAsciiAlpha(name[0]) || name[0] == '_')) return false;
    for (size_t i = 1; i < name.size(); ++i) {
        if (!(isAsciiAlnum(name[i]) || name[i] == '_')) return false;
    }
    return true;
}

std::string sanitizePackageIdent(std::string_view text, std::string_view fallback) {
    std::string s;
    s.reserve(text.size());
    for (char c : text) {
        if (c >= 'A' && c <= 'Z') s.push_back(char(c - 'A' + 'a'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) s.push_back(c);
    }
    if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) {
        s = std::string(fallback) + s;
    }
    return s;
}

std::string makeDefaultPackageId(std::string_view company, std::string_view name) {
    return "com." + sanitizePackageIdent(company, "haruka") + "." + sanitizePackageIdent(name, "game");
}

std::string replaceAll(std::string_view input, std::string_view from, std::string_view to) {
    if (from.empty()) return std::string(input);
    std::string out;
    out.reserve(input.size());
    size_t pos = 0;
    while (pos < input.size()) {
        const size_t found = input.find(from, pos);
        if (found == std::string_view::npos) {
            out.append(input.substr(pos));
            break;
        }
        out.append(input.substr(pos, found - pos));
        out.append(to);
        pos = found + from.size();
    }
    return out;
}

static bool isEngineSourceTree(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir / "CMakeLists.txt", ec) ||
        !fs::exists(dir / "cmake" / "HarukaProject.cmake", ec)) {
        return false;
    }
    std::ifstream f(dir / "CMakeLists.txt", std::ios::binary);
    if (!f) return false;
    std::string head(1024, '\0');
    f.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(f.gcount()));
    return head.find("project(HarukaEngine") != std::string::npos;
}

static bool isValidVersionString(std::string_view v) {
    if (v.empty()) return false;
    bool prevDot = true;
    for (char c : v) {
        if (c == '.') {
            if (prevDot) return false;
            prevDot = true;
        } else if (c >= '0' && c <= '9') {
            prevDot = false;
        } else {
            return false;
        }
    }
    return !prevDot;
}

static std::vector<uint64_t> parseVersionParts(std::string_view v) {
    std::vector<uint64_t> parts;
    size_t i = 0;
    while (i < v.size()) {
        uint64_t num = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') {
            num = num * 10 + static_cast<uint64_t>(v[i] - '0');
            ++i;
        }
        parts.push_back(num);
        if (i < v.size() && v[i] == '.') ++i;
        else break;
    }
    return parts;
}

int compareVersions(std::string_view a, std::string_view b) {
    const std::vector<uint64_t> pa = parseVersionParts(a);
    const std::vector<uint64_t> pb = parseVersionParts(b);
    const size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {
        const uint64_t va = i < pa.size() ? pa[i] : 0;
        const uint64_t vb = i < pb.size() ? pb[i] : 0;
        if (va < vb) return -1;
        if (va > vb) return 1;
    }
    return 0;
}

bool versionMatches(std::string_view ver, std::string_view op, std::string_view req) {
    if (op.empty()) return true;
    const int cmp = compareVersions(ver, req);
    if (op == ">=") return cmp >= 0;
    if (op == ">")  return cmp > 0;
    if (op == "==") return cmp == 0;
    if (op == "<=") return cmp <= 0;
    if (op == "<")  return cmp < 0;
    return false;
}

static bool parseEngineRequirement(std::string_view spec,
                                   std::string& outName,
                                   std::string& outOp,
                                   std::string& outVer) {
    const std::string t = trim(spec);
    if (t.empty()) return false;
    size_t i = 0;
    while (i < t.size() && (isAsciiAlnum(t[i]) || t[i] == '_' || t[i] == '-')) ++i;
    if (i == 0) return false;
    outName = t.substr(0, i);
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i;
    if (i == t.size()) {
        outOp.clear();
        outVer.clear();
        return true;
    }
    if (i + 1 < t.size() && ((t[i] == '>' && t[i + 1] == '=') ||
                             (t[i] == '<' && t[i + 1] == '=') ||
                             (t[i] == '=' && t[i + 1] == '='))) {
        outOp = t.substr(i, 2);
        i += 2;
    } else if (t[i] == '>' || t[i] == '<') {
        outOp = t.substr(i, 1);
        i += 1;
    } else {
        return false;
    }
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i;
    outVer = t.substr(i);
    return isValidVersionString(outVer);
}

static fs::path extractEngineRootFromHarukaConfig(const fs::path& configPath) {
    std::string text;
    if (!readBytes(configPath, text)) return {};
    const std::string needle = "set(HARUKA_ENGINE_ROOT";
    const size_t pos = text.find(needle);
    if (pos == std::string::npos) return {};
    const size_t q1 = text.find('"', pos + needle.size());
    if (q1 == std::string::npos) return {};
    const size_t q2 = text.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    fs::path p;
    if (!hpak::pathFromUtf8(text.substr(q1 + 1, q2 - q1 - 1), p)) return {};
    return p;
}

static fs::path resolveOverrideToEngineRoot(const fs::path& projectDir, const std::string& rawOverride) {
    fs::path p;
    if (!hpak::pathFromUtf8(rawOverride, p) || p.empty()) return {};
    std::error_code ec;
    if (p.is_relative()) {
        const fs::path rel = projectDir / p;
        if (fs::exists(rel, ec)) p = rel;
    }
    if (fs::is_directory(p / "Engine" / "Content", ec)) {
        if (fs::exists(p / "build" / "HarukaConfig.cmake", ec) ||
            fs::exists(p / "Build" / "HarukaConfig.cmake", ec) ||
            isEngineSourceTree(p)) {
            return p;
        }
    }
    if (fs::is_regular_file(p / "HarukaConfig.cmake", ec)) {
        const fs::path er = extractEngineRootFromHarukaConfig(p / "HarukaConfig.cmake");
        if (!er.empty() && fs::is_directory(er / "Engine" / "Content", ec)) {
            return er;
        }
    }
    return {};
}

fs::path findEngineRoot(const fs::path& startDir) {
    std::error_code ec;
    fs::path cur = startDir.empty() ? fs::current_path(ec) : fs::weakly_canonical(startDir, ec);
    if (cur.empty()) cur = startDir;
    for (int depth = 0; depth < 16 && !cur.empty(); ++depth) {
        if (isEngineSourceTree(cur) && fs::exists(cur / "Templates", ec)) {
            return cur;
        }
        const fs::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return {};
}

ProjectResolveResult resolveEditorProjectDir(const std::string& cliProjectArg,
                                             const fs::path& exeDir,
                                             const std::string& compileTimeFallback) {
    ProjectResolveResult res;
    std::error_code ec;

    // 1. Explicit --project=<folder or .hproject>
    if (!cliProjectArg.empty()) {
        fs::path p;
        if (!hpak::pathFromUtf8(cliProjectArg, p)) {
            res.errorMessage = "Invalid UTF-8 path in --project='" + cliProjectArg + "'.";
            res.fixHint = "Pass a valid project folder or .hproject file path to --project=<path>.";
            return res;
        }
        if (fs::is_regular_file(p, ec)) {
            if (p.extension() != ".hproject") {
                res.errorMessage = "Path passed to --project is a file, not a .hproject file: '" + cliProjectArg + "'.";
                res.fixHint = "Pass a project folder or a .hproject file to --project=<path>.";
                return res;
            }
            p = p.parent_path();
        }
        if (!fs::is_directory(p, ec)) {
            res.errorMessage = "Project folder from --project does not exist: '" + normalizePathUtf8(p) + "'.";
            res.fixHint = "Pass an existing project folder or .hproject file to --project=<path>.";
            return res;
        }
        if (project_descriptor::findInDirectory(p).empty()) {
            res.errorMessage = "Project folder from --project has no .hproject file: '" + normalizePathUtf8(p) + "'.";
            res.fixHint = "Pass a folder containing a .hproject file.";
            return res;
        }
        res.ok = true;
        res.projectDir = p;
        res.sourceLog = "Project selected via --project: " + normalizePathUtf8(p);
        return res;
    }

    // 2. Walk up from executable directory looking for *.hproject (or enclosing engine's Projects/Samples)
    fs::path cur = exeDir.empty() ? fs::path{} : fs::weakly_canonical(exeDir, ec);
    if (cur.empty()) cur = exeDir;
    for (int depth = 0; depth < 16 && !cur.empty(); ++depth) {
        if (!project_descriptor::findInDirectory(cur).empty()) {
            res.ok = true;
            res.projectDir = cur;
            res.sourceLog = "Project found by walking up from executable: " + normalizePathUtf8(cur);
            return res;
        }
        if (isEngineSourceTree(cur)) {
            const fs::path samplesDir = cur / "Projects" / "Samples";
            if (fs::is_directory(samplesDir, ec) && !project_descriptor::findInDirectory(samplesDir).empty()) {
                res.ok = true;
                res.projectDir = samplesDir;
                res.sourceLog = "Project selected via enclosing engine Samples: " + normalizePathUtf8(samplesDir);
                return res;
            }
        }
        const fs::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }

    // 3. Compile-time fallback (HARUKA_PROJECT_DIR, if defined)
    if (!compileTimeFallback.empty()) {
        fs::path fb;
        if (hpak::pathFromUtf8(compileTimeFallback, fb) &&
            fs::is_directory(fb, ec) &&
            !project_descriptor::findInDirectory(fb).empty()) {
            res.ok = true;
            res.projectDir = fb;
            res.sourceLog = "Project selected via compile-time fallback (HARUKA_PROJECT_DIR): " + normalizePathUtf8(fb);
            return res;
        }
    }

    std::string searched = "walked up from '" + normalizePathUtf8(exeDir) + "'";
    if (!compileTimeFallback.empty()) {
        searched += " and checked compile-time fallback '" + compileTimeFallback + "'";
    }
    res.errorMessage = "Could not locate a Haruka project (.hproject); " + searched + ".";
    res.fixHint = "Pass --project=<folder or .hproject>, or run the editor from <Project>/Build/<Config>/.";
    return res;
}

EngineResolveResult resolveEngineForProject(const fs::path& projectDir,
                                            const fs::path& enginesIniOverride) {
    EngineResolveResult res;
    std::error_code ec;

    // 1. Project-local Haruka.user.ini override
    const fs::path userIni = projectDir / "Haruka.user.ini";
    if (fs::is_regular_file(userIni, ec)) {
        std::string text;
        std::string decodeWhy;
        if (!hfile::readConfigText(userIni, text, &decodeWhy)) {
            res.errorMessage = "Could not read '" + normalizePathUtf8(userIni) + "': " + decodeWhy;
            res.fixHint = "Fix or remove '" + normalizePathUtf8(userIni) + "'.";
            return res;
        }
        std::string overrideRaw;
        std::istringstream in(text);
        std::string line;
        int lineNo = 0;
        while (std::getline(in, line)) {
            ++lineNo;
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#' || t[0] == ';') continue;
            const size_t eq = t.find('=');
            if (eq == std::string::npos) {
                res.errorMessage = "Malformed '" + normalizePathUtf8(userIni) + "' at line "
                    + std::to_string(lineNo) + ": expected 'key = value'.";
                res.fixHint = "Fix or remove '" + normalizePathUtf8(userIni) + "'.";
                return res;
            }
            const std::string k = trim(std::string_view(t).substr(0, eq));
            const std::string v = trim(std::string_view(t).substr(eq + 1));
            if (k == "harukaDir" || k == "buildDir" || k == "engineDir") {
                overrideRaw = v;
            }
        }
        if (!overrideRaw.empty()) {
            const fs::path er = resolveOverrideToEngineRoot(projectDir, overrideRaw);
            if (!er.empty()) {
                res.ok = true;
                res.engineRoot = er;
                res.engineContentDir = er / "Engine" / "Content";
                res.sourceLog = "Engine selected via Haruka.user.ini override: " + normalizePathUtf8(er);
                return res;
            }
            res.errorMessage = "Haruka.user.ini points to '" + overrideRaw
                + "', which does not contain a valid Haruka engine (missing Engine/Content or HarukaConfig.cmake).";
            res.fixHint = "Update '" + normalizePathUtf8(userIni) + "' to a valid engine/build folder, or remove it.";
            return res;
        }
    }

    // 2. Walk up from projectDir looking for enclosing engine source tree
    fs::path cur = fs::weakly_canonical(projectDir, ec);
    if (cur.empty()) cur = projectDir;
    for (int depth = 0; depth < 16 && !cur.empty(); ++depth) {
        if (isEngineSourceTree(cur) && fs::is_directory(cur / "Engine" / "Content", ec)) {
            res.ok = true;
            res.engineRoot = cur;
            res.engineContentDir = cur / "Engine" / "Content";
            res.sourceLog = "Engine selected via enclosing engine tree: " + normalizePathUtf8(cur);
            return res;
        }
        const fs::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }

    // 3. .hproject requirement + Engines.ini registry
    const fs::path hproj = project_descriptor::findInDirectory(projectDir);
    if (hproj.empty()) {
        res.errorMessage = "No .hproject file found in '" + normalizePathUtf8(projectDir) + "'.";
        res.fixHint = "Ensure the project folder contains a valid .hproject file.";
        return res;
    }
    ProjectDescriptor desc;
    std::vector<std::string> warnings, errors;
    if (!project_descriptor::loadFromFile(hproj, desc, warnings, errors)) {
        res.errorMessage = "Invalid .hproject '" + normalizePathUtf8(hproj) + "': "
            + (errors.empty() ? "parse error" : errors.front());
        res.fixHint = "Fix the .hproject file.";
        return res;
    }
    std::string engineSpec = desc.engine;
    if (engineSpec.empty()) {
        engineSpec = !desc.engineVersion.empty() ? ("haruka>=" + desc.engineVersion) : "haruka";
    }
    std::string reqName, reqOp, reqVer;
    if (!parseEngineRequirement(engineSpec, reqName, reqOp, reqVer)) {
        res.errorMessage = "Invalid engine requirement '" + engineSpec + "' in '" + normalizePathUtf8(hproj) + "'.";
        res.fixHint = "Set 'engine = haruka>=0.1' in the .hproject file.";
        return res;
    }
    const std::string reqNameLower = toLowerAscii(reqName);

    fs::path iniPath = enginesIniOverride;
    if (iniPath.empty()) {
#ifdef _WIN32
        const std::string envIni = trim(getEnvUtf8(L"HARUKA_ENGINES_INI"));
        if (!envIni.empty()) {
            hpak::pathFromUtf8(envIni, iniPath);
        } else {
            const std::string localApp = trim(getEnvUtf8(L"LOCALAPPDATA"));
            if (!localApp.empty() && hpak::pathFromUtf8(localApp, iniPath)) {
                iniPath /= "Haruka";
                iniPath /= "Engines.ini";
            }
        }
#else
        if (const char* envIni = std::getenv("HARUKA_ENGINES_INI"); envIni && *envIni) {
            iniPath = envIni;
        }
#endif
    }
    if (iniPath.empty()) {
        res.errorMessage = "Cannot locate Engines.ini (neither HARUKA_ENGINES_INI nor LOCALAPPDATA is set).";
        res.fixHint = "Run Setup.bat in the Haruka engine folder or create Haruka.user.ini in the project folder.";
        return res;
    }
    if (!fs::is_regular_file(iniPath, ec)) {
        res.errorMessage = "Engine registry '" + normalizePathUtf8(iniPath) + "' not found while looking for '" + engineSpec + "'.";
        res.fixHint = "Run Setup.bat in the Haruka engine folder to register an engine, or add Haruka.user.ini.";
        return res;
    }

    std::string iniText;
    std::string iniWhy;
    if (!hfile::readConfigText(iniPath, iniText, &iniWhy)) {
        res.errorMessage = "Cannot read engine registry '" + normalizePathUtf8(iniPath) + "': " + iniWhy;
        res.fixHint = "Re-run Setup.bat --skip-build in the Haruka engine folder.";
        return res;
    }

    bool inSec = false;
    std::string secName, secVer, secEdirRaw, secBdirRaw;
    std::string bestVer;
    fs::path bestEngineDir;
    int matchedCount = 0;
    std::string missingExample;

    auto flushSection = [&]() -> bool {
        if (!inSec) return true;
        if (secName.empty() || secVer.empty() || secEdirRaw.empty() || secBdirRaw.empty()) {
            res.errorMessage = "Malformed engine registry '" + normalizePathUtf8(iniPath)
                + "': section missing required key (name, version, engineDir, buildDir).";
            res.fixHint = "Re-run Setup.bat --skip-build in the engine folder to regenerate Engines.ini.";
            return false;
        }
        if (!isValidVersionString(secVer)) {
            res.errorMessage = "Malformed engine registry '" + normalizePathUtf8(iniPath)
                + "': invalid version '" + secVer + "'.";
            res.fixHint = "Re-run Setup.bat --skip-build in the engine folder to regenerate Engines.ini.";
            return false;
        }
        if (toLowerAscii(secName) == reqNameLower && versionMatches(secVer, reqOp, reqVer)) {
            ++matchedCount;
            fs::path edir, bdir;
            if (hpak::pathFromUtf8(secEdirRaw, edir) &&
                hpak::pathFromUtf8(secBdirRaw, bdir) &&
                fs::is_directory(edir / "Engine" / "Content", ec) &&
                fs::exists(bdir / "HarukaConfig.cmake", ec)) {
                if (bestEngineDir.empty() || compareVersions(secVer, bestVer) > 0) {
                    bestVer = secVer;
                    bestEngineDir = edir;
                }
            } else {
                missingExample = secName + " " + secVer + " at '" + secEdirRaw + "'";
            }
        }
        return true;
    };

    std::istringstream iniIn(iniText);
    std::string iniLine;
    int lineNo = 0;
    while (std::getline(iniIn, iniLine)) {
        ++lineNo;
        const std::string t = trim(iniLine);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.size() >= 3 && t.front() == '[' && t.back() == ']') {
            if (!flushSection()) return res;
            inSec = true;
            secName.clear();
            secVer.clear();
            secEdirRaw.clear();
            secBdirRaw.clear();
            continue;
        }
        if (!inSec) {
            res.errorMessage = "Malformed engine registry '" + normalizePathUtf8(iniPath)
                + "' at line " + std::to_string(lineNo) + ": key-value outside [section].";
            res.fixHint = "Re-run Setup.bat --skip-build in the engine folder.";
            return res;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) {
            res.errorMessage = "Malformed engine registry '" + normalizePathUtf8(iniPath)
                + "' at line " + std::to_string(lineNo) + ": expected 'key = value'.";
            res.fixHint = "Re-run Setup.bat --skip-build in the engine folder.";
            return res;
        }
        const std::string k = trim(std::string_view(t).substr(0, eq));
        const std::string v = trim(std::string_view(t).substr(eq + 1));
        if (k == "name") secName = v;
        else if (k == "version") secVer = v;
        else if (k == "engineDir") secEdirRaw = v;
        else if (k == "buildDir") secBdirRaw = v;
        else {
            res.errorMessage = "Malformed engine registry '" + normalizePathUtf8(iniPath)
                + "' at line " + std::to_string(lineNo) + ": unknown key '" + k + "'.";
            res.fixHint = "Re-run Setup.bat --skip-build in the engine folder.";
            return res;
        }
    }
    if (!flushSection()) return res;

    if (!bestEngineDir.empty()) {
        res.ok = true;
        res.engineRoot = bestEngineDir;
        res.engineContentDir = bestEngineDir / "Engine" / "Content";
        res.sourceLog = "Engine selected via Engines.ini (" + reqName + " " + bestVer + "): " + normalizePathUtf8(bestEngineDir);
        return res;
    }

    if (matchedCount > 0) {
        res.errorMessage = "Registered engine (" + missingExample + ") in '" + normalizePathUtf8(iniPath)
            + "' was not found on disk (missing Engine/Content or HarukaConfig.cmake).";
        res.fixHint = "Run Setup.bat in the new engine folder to update the registry.";
        return res;
    }

    res.errorMessage = "No registered engine in '" + normalizePathUtf8(iniPath)
        + "' satisfies '" + engineSpec + "'.";
    res.fixHint = "Run Setup.bat in a compatible Haruka engine folder, or create Haruka.user.ini in the project folder.";
    return res;
}

bool loadTemplateInfo(const fs::path& templateDir, TemplateInfo& out, std::string& outError) {
    const fs::path iniPath = templateDir / "template.ini";
    std::string text;
    std::string decodeWhy;
    if (!hfile::readConfigText(iniPath, text, &decodeWhy)) {
        outError = decodeWhy.empty()
            ? ("cannot read template.ini at '" + hpak::utf8Of(iniPath) + "'")
            : decodeWhy;
        return false;
    }

    out = TemplateInfo{};
    out.dir = templateDir;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(std::string_view(t).substr(0, eq));
        const std::string val = trim(std::string_view(t).substr(eq + 1));
        if (key == "name") out.name = val;
        else if (key == "description") out.description = val;
        else if (key == "tokens") {
            std::istringstream ts(val);
            std::string tok;
            while (std::getline(ts, tok, ',')) {
                const std::string tt = trim(tok);
                if (!tt.empty()) out.tokens.push_back(tt);
            }
        }
    }

    if (out.name.empty()) {
        outError = "template.ini missing required key 'name'";
        return false;
    }
    return true;
}

std::vector<TemplateInfo> listTemplates(const fs::path& engineRoot) {
    std::vector<TemplateInfo> result;
    const fs::path templatesDir = engineRoot / "Templates";
    std::error_code ec;
    if (!fs::is_directory(templatesDir, ec)) return result;

    for (const auto& entry : fs::directory_iterator(templatesDir, ec)) {
        if (!entry.is_directory(ec)) continue;
        TemplateInfo info;
        std::string err;
        if (loadTemplateInfo(entry.path(), info, err)) {
            result.push_back(std::move(info));
        }
    }
    std::sort(result.begin(), result.end(), [](const TemplateInfo& a, const TemplateInfo& b) {
        return a.name < b.name;
    });
    return result;
}

OpResult createProjectFromTemplate(const NewProjectOptions& opts) {
    if (!isValidProjectName(opts.name)) {
        return makeFail(
            "Invalid project name '" + opts.name + "'.",
            "Use a valid C++/CMake identifier matching [A-Za-z_][A-Za-z0-9_]* (e.g. MyGame)."
        );
    }

    if (opts.targetDir.empty()) {
        return makeFail("Target project folder is empty.", "Specify a non-existing target directory path.");
    }

    std::error_code ec;
    if (fs::exists(opts.targetDir, ec)) {
        return makeFail(
            "Target folder already exists (refusing to overwrite): '" + hpak::utf8Of(opts.targetDir) + "'.",
            "Choose a new folder path or remove the existing folder first."
        );
    }

    if (opts.engineRoot.empty() || !fs::is_directory(opts.engineRoot, ec)) {
        return makeFail(
            "Engine root directory not found.",
            "Run HarukaLauncher from an engine build folder or pass a valid engine root."
        );
    }

    const std::string tplName = opts.templateName.empty() ? std::string("Blank") : opts.templateName;
    const fs::path tplDir = opts.engineRoot / "Templates" / tplName;
    if (!fs::is_directory(tplDir, ec)) {
        return makeFail(
            "Template '" + tplName + "' not found at '" + hpak::utf8Of(tplDir) + "'.",
            "Use --template=Blank or check the engine Templates/ directory."
        );
    }

    TemplateInfo tplInfo;
    std::string tplErr;
    if (!loadTemplateInfo(tplDir, tplInfo, tplErr)) {
        return makeFail("Invalid template '" + tplName + "': " + tplErr, "Fix template.ini in the template folder.");
    }

    const std::string company = trim(opts.company).empty() ? std::string("Haruka") : trim(opts.company);
    const std::string packageId = trim(opts.packageId).empty()
        ? makeDefaultPackageId(company, opts.name)
        : trim(opts.packageId);
    const std::string engineVersion = trim(opts.engineVersion).empty()
        ? readEngineVersionFromCMake(opts.engineRoot)
        : trim(opts.engineVersion);

    const std::vector<std::pair<std::string, std::string>> allTokens = {
        { "{{NAME}}", opts.name },
        { "{{COMPANY}}", company },
        { "{{PACKAGE_ID}}", packageId },
        { "{{ENGINE_VERSION}}", engineVersion },
    };

    // If template.ini specifies a `tokens = ...` list, only apply those tokens.
    std::vector<std::pair<std::string, std::string>> activeTokens;
    if (tplInfo.tokens.empty()) {
        activeTokens = allTokens;
    } else {
        for (const auto& kv : allTokens) {
            if (std::find(tplInfo.tokens.begin(), tplInfo.tokens.end(), kv.first) != tplInfo.tokens.end()) {
                activeTokens.push_back(kv);
            }
        }
    }

    auto applyTokens = [&](std::string s) {
        for (const auto& [k, v] : activeTokens) {
            s = replaceAll(s, k, v);
        }
        return s;
    };

    // Create target directory; clean up on any failure from here on.
    if (!fs::create_directories(opts.targetDir, ec) || ec) {
        return makeFail(
            "Failed to create target folder '" + hpak::utf8Of(opts.targetDir) + "': " + ec.message(),
            "Check parent directory permissions and path characters."
        );
    }

    auto cleanupAndFail = [&](const std::string& msg, const std::string& hint) {
        std::error_code rmEc;
        fs::remove_all(opts.targetDir, rmEc);
        return makeFail(msg, hint);
    };

    for (fs::recursive_directory_iterator it(tplDir, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            return cleanupAndFail("Failed while reading template '" + tplName + "': " + ec.message(),
                                  "Check template directory permissions.");
        }
        const fs::path srcPath = it->path();
        const fs::path relPath = fs::relative(srcPath, tplDir, ec);
        if (ec) {
            return cleanupAndFail("Failed to compute relative template path: " + ec.message(),
                                  "Check template directory structure.");
        }
        if (relPath == "template.ini") continue;

        const std::string relUtf8 = applyTokens(hpak::utf8Of(relPath));
        fs::path dstRel;
        if (!hpak::pathFromUtf8(relUtf8, dstRel)) {
            return cleanupAndFail("Invalid UTF-8 path after token substitution: " + relUtf8,
                                  "Check project name and template filenames.");
        }
        const fs::path dstPath = opts.targetDir / dstRel;

        if (it->is_directory(ec)) {
            fs::create_directories(dstPath, ec);
            if (ec) {
                return cleanupAndFail("Failed to create directory '" + hpak::utf8Of(dstPath) + "': " + ec.message(),
                                      "Check disk space and permissions.");
            }
            continue;
        }

        if (it->is_regular_file(ec)) {
            fs::create_directories(dstPath.parent_path(), ec);
            if (ec) {
                return cleanupAndFail("Failed to create parent directory for '" + hpak::utf8Of(dstPath) + "': " + ec.message(),
                                      "Check disk space and permissions.");
            }
            std::string content;
            if (!readBytes(srcPath, content)) {
                return cleanupAndFail("Failed to read template file '" + hpak::utf8Of(srcPath) + "'.",
                                      "Check template file permissions.");
            }
            if (!looksBinary(content)) {
                content = applyTokens(std::move(content));
            }
            if (!writeBytes(dstPath, content)) {
                return cleanupAndFail("Failed to write project file '" + hpak::utf8Of(dstPath) + "'.",
                                      "Check disk space and permissions.");
            }
        }
    }

    // Copy cmake/HarukaEngineFinder.cmake from engine root if not already provided by template.
    const fs::path engineFinderSrc = opts.engineRoot / "cmake" / "HarukaEngineFinder.cmake";
    const fs::path engineFinderDst = opts.targetDir / "cmake" / "HarukaEngineFinder.cmake";
    if (fs::exists(engineFinderSrc, ec) && !fs::exists(engineFinderDst, ec)) {
        fs::create_directories(engineFinderDst.parent_path(), ec);
        if (ec || !fs::copy_file(engineFinderSrc, engineFinderDst, fs::copy_options::overwrite_existing, ec) || ec) {
            return cleanupAndFail("Failed to copy HarukaEngineFinder.cmake: " + ec.message(),
                                  "Check engine cmake/ directory.");
        }
    }

    // Verify generated .hproject descriptor is valid and has no unsubstituted tokens.
    const fs::path hproj = project_descriptor::findInDirectory(opts.targetDir);
    if (hproj.empty()) {
        return cleanupAndFail("Template did not produce a .hproject file.",
                              "Ensure template contains '{{NAME}}.hproject'.");
    }
    ProjectDescriptor desc;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    if (!project_descriptor::loadFromFile(hproj, desc, warnings, errors)) {
        return cleanupAndFail("Generated .hproject is invalid: " + (errors.empty() ? "unknown error" : errors.front()),
                              "Check template .hproject and template.ini tokens.");
    }
    if (desc.name != opts.name ||
        desc.name.find("{{") != std::string::npos ||
        desc.company.find("{{") != std::string::npos ||
        desc.packageId.find("{{") != std::string::npos ||
        desc.engineVersion.find("{{") != std::string::npos) {
        return cleanupAndFail("Generated .hproject has unsubstituted template tokens.",
                              "Check template.ini 'tokens' list.");
    }

    return makeOk("Created Haruka project '" + opts.name + "' at '" + normalizePathUtf8(opts.targetDir) + "'");
}

bool findVsGenerator(VsGeneratorInfo& out, std::string& outError, std::string& outFixHint) {
#ifdef _WIN32
    std::string pf86 = getEnvUtf8(L"ProgramFiles(x86)");
    if (pf86.empty()) pf86 = "C:/Program Files (x86)";
    fs::path pf86Path;
    hpak::pathFromUtf8(pf86, pf86Path);
    const fs::path vswhere = pf86Path / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
    std::error_code ec;
    if (!fs::exists(vswhere, ec)) {
        outError = "Visual Studio Installer (vswhere.exe) not found.";
        outFixHint = "Install Visual Studio 2022 or 2026 with 'Desktop development with C++'.";
        return false;
    }

    auto query = [&](bool prerelease) -> ProcessOutput {
        std::vector<std::string> args = {
            hpak::utf8Of(vswhere),
            "-latest",
            "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationVersion",
        };
        if (prerelease) args.push_back("-prerelease");
        return runProcessCapture(args);
    };

    ProcessOutput res = query(false);
    std::string ver = trim(res.stdOutErr);
    if (res.exitCode != 0 || ver.empty()) {
        res = query(true);
        ver = trim(res.stdOutErr);
    }
    if (res.exitCode != 0 || ver.empty()) {
        outError = "MSVC C++ toolset not found via vswhere.";
        outFixHint = "Install Visual Studio 2022 or 2026 with 'Desktop development with C++'.";
        return false;
    }

    // Take first line if multiple
    const size_t nl = ver.find('\n');
    if (nl != std::string::npos) ver = trim(std::string_view(ver).substr(0, nl));

    int major = 0;
    const size_t dot = ver.find('.');
    const std::string majorStr = (dot == std::string::npos) ? ver : ver.substr(0, dot);
    try {
        major = std::stoi(majorStr);
    } catch (...) {
        major = 0;
    }

    if (major >= 18) {
        out.generatorName = "Visual Studio 18 2026";
        out.displayName = "Visual Studio 2026 (" + ver + ")";
        return true;
    }
    if (major == 17) {
        out.generatorName = "Visual Studio 17 2022";
        out.displayName = "Visual Studio 2022 (" + ver + ")";
        return true;
    }

    outError = "Unsupported Visual Studio version '" + ver + "'.";
    outFixHint = "Install Visual Studio 2022 (v17) or 2026 (v18).";
    return false;
#else
    out = VsGeneratorInfo{ "Ninja", "Ninja" };
    (void)outError;
    (void)outFixHint;
    return true;
#endif
}

OpResult generateProjectFiles(const fs::path& projectDir,
                              const fs::path& harukaDirOverride,
                              const fs::path& enginesIniOverride,
                              const OutputLineCallback& onOutputLine) {
    std::error_code ec;
    if (!fs::is_directory(projectDir, ec)) {
        return makeFail("Project directory does not exist: '" + hpak::utf8Of(projectDir) + "'.",
                        "Pass an existing project folder containing a .hproject file.");
    }
    const fs::path hproj = project_descriptor::findInDirectory(projectDir);
    if (hproj.empty()) {
        return makeFail("No .hproject file found in '" + hpak::utf8Of(projectDir) + "'.",
                        "Create a project first with 'HarukaLauncher new'.");
    }

    VsGeneratorInfo vs;
    std::string vsErr, vsHint;
    if (!findVsGenerator(vs, vsErr, vsHint)) {
        return makeFail(vsErr, vsHint);
    }

#ifdef _WIN32
    const fs::path buildDir = projectDir / "Build";
    std::vector<std::string> args = {
        "cmake",
        "-S", normalizePathUtf8(projectDir),
        "-B", normalizePathUtf8(buildDir),
        "-G", vs.generatorName,
        "-A", "x64",
    };
    if (!harukaDirOverride.empty()) {
        args.push_back("-DHaruka_DIR=" + normalizePathUtf8(harukaDirOverride));
    }

    const ProcessOutput out = runProcessCapture(args, projectDir, hpak::utf8Of(enginesIniOverride), onOutputLine);
    if (!out.launched) {
        return makeFail("Failed to run cmake: " + out.launchError,
                        "Ensure CMake 3.21+ is installed and in PATH.");
    }
    if (out.exitCode != 0) {
        std::string lastLine;
        std::istringstream iss(out.stdOutErr);
        for (std::string l; std::getline(iss, l);) {
            const std::string t = trim(l);
            if (!t.empty()) lastLine = t;
        }
        return makeFail(
            "CMake configure failed (exit code " + std::to_string(out.exitCode) + ")"
            + (lastLine.empty() ? "" : ": " + lastLine),
            "Run Setup.bat in the engine folder or pass -DHaruka_DIR=<engine>/build."
        );
    }

    return makeOk("Generated '" + vs.displayName + "' solution in '" + normalizePathUtf8(buildDir) + "'");
#else
    (void)harukaDirOverride;
    (void)enginesIniOverride;
    (void)onOutputLine;
    return makeFail("generateProjectFiles is only implemented on Windows.", "Run CMake manually.");
#endif
}

OpResult buildProjectFiles(const fs::path& projectDir,
                           const OutputLineCallback& onOutputLine) {
    std::error_code ec;
    const fs::path buildDir = projectDir / "Build";
    if (!fs::is_directory(buildDir, ec)) {
        return makeFail("Build directory does not exist: '" + hpak::utf8Of(buildDir) + "'.",
                        "Generate project files first.");
    }
#ifdef _WIN32
    const std::vector<std::string> args = {
        "cmake",
        "--build", normalizePathUtf8(buildDir),
        "--config", "Release",
        "--parallel",
    };
    const ProcessOutput out = runProcessCapture(args, projectDir, {}, onOutputLine);
    if (!out.launched) {
        return makeFail("Failed to run cmake --build: " + out.launchError,
                        "Ensure CMake 3.21+ is installed and in PATH.");
    }
    if (out.exitCode != 0) {
        return makeFail("Project build failed (exit code " + std::to_string(out.exitCode) + ").",
                        "Check the build log for compiler or linker errors.");
    }
    return makeOk("Built project in '" + normalizePathUtf8(buildDir) + "'");
#else
    (void)onOutputLine;
    return makeFail("buildProjectFiles is only implemented on Windows.", "Run CMake manually.");
#endif
}

fs::path findProjectSolution(const fs::path& projectDir) {
    std::error_code ec;
    const fs::path buildDir = projectDir / "Build";
    if (!fs::is_directory(buildDir, ec)) return {};
    for (const auto& entry : fs::directory_iterator(buildDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = toLowerAscii(hpak::utf8Of(entry.path().extension()));
        if (ext == ".sln" || ext == ".slnx") return entry.path();
    }
    return {};
}

fs::path resolveUserDir(const fs::path& overrideDir) {
    if (!overrideDir.empty()) return overrideDir;
#ifdef _WIN32
    const std::string envOverride = trim(getEnvUtf8(L"HARUKA_LAUNCHER_USER_DIR"));
    if (!envOverride.empty()) {
        fs::path p;
        if (hpak::pathFromUtf8(envOverride, p)) return p;
    }
    const std::string localApp = trim(getEnvUtf8(L"LOCALAPPDATA"));
    if (!localApp.empty()) {
        fs::path p;
        if (hpak::pathFromUtf8(localApp, p)) return p / "Haruka" / "Launcher";
    }
#else
    if (const char* envOverride = std::getenv("HARUKA_LAUNCHER_USER_DIR")) {
        if (*envOverride) return fs::path(envOverride);
    }
#endif
    return fs::current_path() / ".haruka_launcher";
}

fs::path recentProjectsFile(const fs::path& userDir) {
    return userDir / "recent_projects.ini";
}

bool loadRecentProjects(const fs::path& userDir,
                        std::vector<RecentProjectEntry>& out,
                        std::string& outError) {
    out.clear();
    const fs::path file = recentProjectsFile(userDir);
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;

    std::string text;
    std::string why;
    if (!hfile::readConfigText(file, text, &why)) {
        outError = why.empty() ? ("cannot open '" + hpak::utf8Of(file) + "'") : why;
        return false;
    }

    bool inSection = false;
    RecentProjectEntry cur;
    auto flush = [&]() {
        if (!inSection) return;
        if (!cur.name.empty() && !cur.path.empty()) {
            out.push_back(cur);
        }
        cur = RecentProjectEntry{};
    };

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.size() > 2 && t.front() == '[' && t.back() == ']') {
            flush();
            inSection = true;
            cur = RecentProjectEntry{};
            continue;
        }
        if (!inSection) continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(std::string_view(t).substr(0, eq));
        const std::string val = trim(std::string_view(t).substr(eq + 1));
        if (key == "name") cur.name = val;
        else if (key == "path") cur.path = val;
        else if (key == "engine") cur.engine = val;
        else if (key == "lastOpened") cur.lastOpened = val;
    }
    flush();
    return true;
}

bool saveRecentProjects(const fs::path& userDir,
                        const std::vector<RecentProjectEntry>& entries,
                        std::string& outError) {
    std::error_code ec;
    fs::create_directories(userDir, ec);
    if (ec) {
        outError = "cannot create launcher user directory '" + hpak::utf8Of(userDir) + "': " + ec.message();
        return false;
    }

    std::ostringstream ss;
    ss << "# Haruka Launcher Recent Projects (UTF-8)\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        ss << "[project." << i << "]\n";
        ss << "name = " << entries[i].name << "\n";
        ss << "path = " << entries[i].path << "\n";
        ss << "engine = " << entries[i].engine << "\n";
        ss << "lastOpened = " << entries[i].lastOpened << "\n";
    }

    const fs::path file = recentProjectsFile(userDir);
    const fs::path tmp = userDir / "recent_projects.ini.tmp";
    if (!writeBytes(tmp, ss.str())) {
        outError = "cannot write '" + hpak::utf8Of(tmp) + "'";
        return false;
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        ec.clear();
        fs::rename(tmp, file, ec);
        if (ec) {
            outError = "cannot replace '" + hpak::utf8Of(file) + "': " + ec.message();
            return false;
        }
    }
    return true;
}

OpResult registerProject(const fs::path& projectDir,
                         const fs::path& userDir,
                         RecentProjectEntry* outEntry) {
    std::error_code ec;
    if (!fs::is_directory(projectDir, ec)) {
        return makeFail("Project directory does not exist: '" + hpak::utf8Of(projectDir) + "'.",
                        "Pass a valid project directory.");
    }
    const fs::path hproj = project_descriptor::findInDirectory(projectDir);
    if (hproj.empty()) {
        return makeFail("No .hproject file found in '" + hpak::utf8Of(projectDir) + "'.",
                        "Pass a directory containing a .hproject file.");
    }

    ProjectDescriptor desc;
    std::vector<std::string> warnings, errors;
    if (!project_descriptor::loadFromFile(hproj, desc, warnings, errors)) {
        return makeFail("Invalid .hproject: " + (errors.empty() ? "unknown error" : errors.front()),
                        "Fix the .hproject file.");
    }

    std::vector<RecentProjectEntry> entries;
    std::string loadErr;
    if (!loadRecentProjects(userDir, entries, loadErr)) {
        return makeFail("Failed to load recent projects: " + loadErr,
                        "Check or remove recent_projects.ini in '" + hpak::utf8Of(userDir) + "'.");
    }

    const std::string normNew = normalizePathForCompare(projectDir);
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](const RecentProjectEntry& e) {
            fs::path p;
            if (!hpak::pathFromUtf8(e.path, p)) return false;
            return normalizePathForCompare(p) == normNew;
        }),
        entries.end()
    );

    RecentProjectEntry entry;
    entry.name = desc.name;
    entry.path = normalizePathUtf8(projectDir);
    entry.engine = desc.engine.empty() ? ("haruka>=" + desc.engineVersion) : desc.engine;
    entry.lastOpened = currentIsoUtcTimestamp();
    entries.insert(entries.begin(), entry);

    std::string saveErr;
    if (!saveRecentProjects(userDir, entries, saveErr)) {
        return makeFail("Failed to save recent projects: " + saveErr,
                        "Check write permissions for '" + hpak::utf8Of(userDir) + "'.");
    }

    if (outEntry) *outEntry = entry;
    return makeOk("Registered project '" + entry.name + "' (" + entry.path + ")");
}

OpResult removeRecentProject(const fs::path& projectDir, const fs::path& userDir) {
    std::vector<RecentProjectEntry> entries;
    std::string loadErr;
    if (!loadRecentProjects(userDir, entries, loadErr)) {
        return makeFail("Failed to load recent projects: " + loadErr);
    }
    const std::string normTarget = normalizePathForCompare(projectDir);
    const size_t before = entries.size();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](const RecentProjectEntry& e) {
            fs::path p;
            if (!hpak::pathFromUtf8(e.path, p)) return false;
            return normalizePathForCompare(p) == normTarget;
        }),
        entries.end()
    );
    if (entries.size() == before) {
        return makeOk("Project was not in recent list.");
    }
    std::string saveErr;
    if (!saveRecentProjects(userDir, entries, saveErr)) {
        return makeFail("Failed to save recent projects: " + saveErr);
    }
    return makeOk("Removed project from recent list.");
}

std::string formatUtcIsoToLocalMinute(std::string_view isoUtc) {
    if (isoUtc.size() < 16) return std::string(isoUtc);
    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
    const std::string s(isoUtc);
#ifdef _WIN32
    const int parsed = sscanf_s(s.c_str(), "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec);
#else
    const int parsed = std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec);
#endif
    if (parsed < 5) return s;

    std::tm tmUtc{};
    tmUtc.tm_year = year - 1900;
    tmUtc.tm_mon = mon - 1;
    tmUtc.tm_mday = day;
    tmUtc.tm_hour = hour;
    tmUtc.tm_min = min;
    tmUtc.tm_sec = (parsed >= 6) ? sec : 0;
    tmUtc.tm_isdst = 0;

#ifdef _WIN32
    const std::time_t t = _mkgmtime(&tmUtc);
    if (t == static_cast<std::time_t>(-1)) return s;
    std::tm tmLocal{};
    if (localtime_s(&tmLocal, &t) != 0) return s;
#else
    const std::time_t t = timegm(&tmUtc);
    if (t == static_cast<std::time_t>(-1)) return s;
    std::tm tmLocal{};
    if (!localtime_r(&t, &tmLocal)) return s;
#endif
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                  tmLocal.tm_year + 1900, tmLocal.tm_mon + 1, tmLocal.tm_mday,
                  tmLocal.tm_hour, tmLocal.tm_min);
    return std::string(buf);
}

fs::path defaultProjectsDir() {
#ifdef _WIN32
    PWSTR docsPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docsPath)) && docsPath) {
        fs::path p(docsPath);
        CoTaskMemFree(docsPath);
        if (!p.empty()) return p / "Haruka Projects";
    }
    const std::string userProf = trim(getEnvUtf8(L"USERPROFILE"));
    if (!userProf.empty()) {
        fs::path p;
        if (hpak::pathFromUtf8(userProf, p)) return p / "Documents" / "Haruka Projects";
    }
#endif
    return fs::current_path() / "Haruka Projects";
}

fs::path loadLastProjectLocation(const fs::path& userDir) {
    const fs::path ini = userDir / "launcher_settings.ini";
    std::string text;
    if (!readBytes(ini, text)) return {};
    std::istringstream iss(text);
    for (std::string line; std::getline(iss, line);) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        if (trim(t.substr(0, eq)) == "lastLocation") {
            const std::string val = trim(t.substr(eq + 1));
            fs::path p;
            if (!val.empty() && hpak::pathFromUtf8(val, p)) return p;
        }
    }
    return {};
}

bool saveLastProjectLocation(const fs::path& userDir, const fs::path& location) {
    if (userDir.empty() || location.empty()) return false;
    std::error_code ec;
    fs::create_directories(userDir, ec);
    const fs::path ini = userDir / "launcher_settings.ini";
    const std::string data = "# Haruka Launcher settings\nlastLocation = " + normalizePathUtf8(location) + "\n";
    return writeBytes(ini, data);
}

OpResult planOpenProject(const fs::path& projectDir, OpenPlan& outPlan) {
    outPlan = OpenPlan{};
    outPlan.projectDir = projectDir;

    std::error_code ec;
    if (!fs::is_directory(projectDir, ec)) {
        return makeFail("Project directory does not exist: '" + hpak::utf8Of(projectDir) + "'.",
                        "Pass an existing project folder.");
    }
    const fs::path hproj = project_descriptor::findInDirectory(projectDir);
    if (hproj.empty()) {
        return makeFail("No .hproject file found in '" + hpak::utf8Of(projectDir) + "'.",
                        "Create a project first with 'HarukaLauncher new'.");
    }

    ProjectDescriptor desc;
    std::vector<std::string> warnings, errors;
    if (!project_descriptor::loadFromFile(hproj, desc, warnings, errors)) {
        return makeFail("Invalid .hproject: " + (errors.empty() ? "unknown error" : errors.front()),
                        "Fix the .hproject file.");
    }
    outPlan.projectName = desc.name;

    const std::string exeName = desc.name + "Editor.exe";
    const fs::path relExe = projectDir / "Build" / "Release" / exeName;
    const fs::path dbgExe = projectDir / "Build" / "Debug" / exeName;

    if (fs::is_regular_file(relExe, ec)) {
        outPlan.editorExe = relExe;
    } else if (fs::is_regular_file(dbgExe, ec)) {
        outPlan.editorExe = dbgExe;
    } else {
        outPlan.needsBuild = true;
        outPlan.buildReason = "Editor executable '" + exeName + "' not found in Build/Release or Build/Debug";
        return makeOk("Build required");
    }

    const auto exeTime = fs::last_write_time(outPlan.editorExe, ec);
    if (ec) {
        outPlan.needsBuild = true;
        outPlan.buildReason = "Could not inspect editor executable timestamp";
        return makeOk("Build required");
    }

    auto checkNewer = [&](const fs::path& f) -> bool {
        std::error_code fec;
        if (!fs::is_regular_file(f, fec)) return false;
        const auto ft = fs::last_write_time(f, fec);
        return !fec && ft > exeTime;
    };

    if (checkNewer(hproj) || checkNewer(projectDir / "CMakeLists.txt")) {
        outPlan.needsBuild = true;
        outPlan.buildReason = "Project configuration is newer than '" + exeName + "'";
        return makeOk("Build required");
    }

    const fs::path sourceDir = projectDir / "Source";
    if (fs::is_directory(sourceDir, ec)) {
        for (fs::recursive_directory_iterator it(sourceDir, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file(ec) && checkNewer(it->path())) {
                outPlan.needsBuild = true;
                outPlan.buildReason = "Source file '" + hpak::utf8Of(it->path().filename()) + "' is newer than '" + exeName + "'";
                return makeOk("Build required");
            }
        }
    }

    return makeOk("Ready to open '" + outPlan.projectName + "'");
}

OpResult openProject(const fs::path& projectDir,
                     const fs::path& userDir,
                     bool dryRun,
                     const std::vector<std::string>& extraArgs,
                     bool waitForExit) {
    OpenPlan plan;
    OpResult planRes = planOpenProject(projectDir, plan);
    if (!planRes.ok) return planRes;

    if (plan.needsBuild) {
        const std::string projUtf8 = normalizePathUtf8(projectDir);
        return makeFail(
            "Build required for '" + plan.projectName + "': " + plan.buildReason + ".",
            "Run: HarukaLauncher generate \"" + projUtf8 + "\" && cmake --build \"" + projUtf8 + "/Build\" --config Release --parallel"
        );
    }

    std::vector<std::string> cmdArgs;
    cmdArgs.push_back(normalizePathUtf8(plan.editorExe));
    for (const auto& a : extraArgs) cmdArgs.push_back(a);

    if (dryRun) {
        std::string line = "Would launch:";
        for (const auto& a : cmdArgs) {
            line += " \"" + a + "\"";
        }
        return makeOk(line);
    }

    registerProject(projectDir, userDir, nullptr);

#ifdef _WIN32
    const ProcessOutput proc = launchProcess(cmdArgs, plan.editorExe.parent_path(), waitForExit);
    if (!proc.launched) {
        return makeFail("Failed to launch editor: " + proc.launchError,
                        "Rebuild the project and try again.");
    }
    if (waitForExit && proc.exitCode != 0) {
        return makeFail("Editor exited with code " + std::to_string(proc.exitCode) + ".",
                        "Check project Saved/Logs for details.", proc.exitCode);
    }
    return makeOk("Launched '" + normalizePathUtf8(plan.editorExe) + "'");
#else
    (void)waitForExit;
    return makeFail("openProject is only implemented on Windows.", "Launch the editor executable directly.");
#endif
}

OpResult openFolderInExplorer(const fs::path& dir) {
#ifdef _WIN32
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return makeFail("Folder does not exist: '" + hpak::utf8Of(dir) + "'.");
    }
    const std::wstring wdir = dir.wstring();
    const INT_PTR rc = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (rc <= 32) {
        return makeFail("Failed to open folder (ShellExecute error " + std::to_string(rc) + ").");
    }
    return makeOk("Opened folder '" + hpak::utf8Of(dir) + "'");
#else
    (void)dir;
    return makeFail("openFolderInExplorer is only implemented on Windows.");
#endif
}

OpResult openInVisualStudio(const fs::path& slnPath) {
#ifdef _WIN32
    std::error_code ec;
    if (!fs::is_regular_file(slnPath, ec)) {
        return makeFail("Solution file does not exist: '" + hpak::utf8Of(slnPath) + "'.");
    }
    const std::wstring wsln = slnPath.wstring();
    const INT_PTR rc = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wsln.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (rc <= 32) {
        return makeFail("Failed to open Visual Studio solution (ShellExecute error " + std::to_string(rc) + ").");
    }
    return makeOk("Opened '" + hpak::utf8Of(slnPath) + "'");
#else
    (void)slnPath;
    return makeFail("openInVisualStudio is only implemented on Windows.");
#endif
}

fs::path pickFolderDialog(const fs::path& initialDir) {
#ifdef _WIN32
    fs::path result;
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool didInit = SUCCEEDED(initHr);
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg))) && dlg) {
        DWORD opts = 0;
        if (SUCCEEDED(dlg->GetOptions(&opts))) {
            dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }
        std::error_code ec;
        if (!initialDir.empty() && fs::is_directory(initialDir, ec)) {
            IShellItem* folderItem = nullptr;
            const std::wstring wInit = initialDir.wstring();
            if (SUCCEEDED(SHCreateItemFromParsingName(wInit.c_str(), nullptr, IID_PPV_ARGS(&folderItem))) && folderItem) {
                dlg->SetFolder(folderItem);
                folderItem->Release();
            }
        }
        if (SUCCEEDED(dlg->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR pathStr = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathStr)) && pathStr) {
                    result = fs::path(pathStr);
                    CoTaskMemFree(pathStr);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (didInit) CoUninitialize();
    return result;
#else
    (void)initialDir;
    return {};
#endif
}

std::vector<FileAssociationEntry> buildFileAssociationEntries(const fs::path& launcherExePath) {
    std::string exe = hpak::utf8Of(launcherExePath);
#ifdef _WIN32
    for (char& c : exe) {
        if (c == '/') c = '\\';
    }
#endif
    const std::string quotedExe = "\"" + exe + "\"";
    return {
        { "Software\\Classes\\.hproject", "", "Haruka.Project" },
        { "Software\\Classes\\Haruka.Project", "", "Haruka Engine Project" },
        { "Software\\Classes\\Haruka.Project\\DefaultIcon", "", quotedExe + ",0" },
        { "Software\\Classes\\Haruka.Project\\shell\\open", "", "Open with Haruka Launcher" },
        { "Software\\Classes\\Haruka.Project\\shell\\open\\command", "", quotedExe + " \"%1\"" },
        { "Software\\Classes\\Haruka.Project\\shell\\generate", "", "Generate Visual Studio project files" },
        { "Software\\Classes\\Haruka.Project\\shell\\generate\\command", "", quotedExe + " generate \"%1\"" },
    };
}

std::vector<std::string> fileAssociationDeleteKeys() {
    return {
        "Software\\Classes\\.hproject",
        "Software\\Classes\\Haruka.Project",
    };
}

OpResult associateFileTypes(const fs::path& launcherExePath, bool dryRun) {
    if (launcherExePath.empty()) {
        return makeFail("Launcher executable path is empty.");
    }
    const auto entries = buildFileAssociationEntries(launcherExePath);
    if (dryRun) {
        std::string out = "[dry-run] Would write HKCU registry entries:\n";
        for (const auto& e : entries) {
            out += "  HKCU\\" + e.regKey + " (" + (e.valueName.empty() ? "(Default)" : e.valueName) + ") = " + e.valueData + "\n";
        }
        if (!out.empty() && out.back() == '\n') out.pop_back();
        return makeOk(out);
    }
#ifdef _WIN32
    for (const auto& e : entries) {
        const std::wstring wKey = utf8ToWide(e.regKey);
        const std::wstring wVal = utf8ToWide(e.valueData);
        const std::wstring wName = e.valueName.empty() ? std::wstring{} : utf8ToWide(e.valueName);
        HKEY hKey = nullptr;
        const LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, wKey.c_str(), 0, nullptr,
                                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
        if (rc != ERROR_SUCCESS) {
            return makeFail("RegCreateKeyExW failed for HKCU\\" + e.regKey + " (error " + std::to_string(rc) + ").");
        }
        const DWORD bytes = static_cast<DWORD>((wVal.size() + 1) * sizeof(wchar_t));
        const LONG setRc = RegSetValueExW(hKey, e.valueName.empty() ? nullptr : wName.c_str(), 0, REG_SZ,
                                           reinterpret_cast<const BYTE*>(wVal.c_str()), bytes);
        RegCloseKey(hKey);
        if (setRc != ERROR_SUCCESS) {
            return makeFail("RegSetValueExW failed for HKCU\\" + e.regKey + " (error " + std::to_string(setRc) + ").");
        }
    }
    return makeOk("Associated .hproject files under HKCU\\Software\\Classes.");
#else
    return makeFail("associateFileTypes is only implemented on Windows.");
#endif
}

OpResult unassociateFileTypes(bool dryRun) {
    const auto keys = fileAssociationDeleteKeys();
    if (dryRun) {
        std::string out = "[dry-run] Would delete HKCU registry keys:\n";
        for (const auto& k : keys) {
            out += "  HKCU\\" + k + "\n";
        }
        if (!out.empty() && out.back() == '\n') out.pop_back();
        return makeOk(out);
    }
#ifdef _WIN32
    for (const auto& k : keys) {
        const std::wstring wKey = utf8ToWide(k);
        const LONG rc = RegDeleteTreeW(HKEY_CURRENT_USER, wKey.c_str());
        if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
            return makeFail("RegDeleteTreeW failed for HKCU\\" + k + " (error " + std::to_string(rc) + ").");
        }
    }
    return makeOk("Removed .hproject file association from HKCU\\Software\\Classes.");
#else
    return makeFail("unassociateFileTypes is only implemented on Windows.");
#endif
}

} // namespace hprojops
