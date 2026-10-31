#include "AppPaths.hpp"

#include <system_error>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path findExePath() {
#ifdef _WIN32
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) break;
        if (n < buf.size()) return fs::path(std::wstring(buf.data(), n));
        buf.resize(buf.size() * 2);   // 긴 경로(MAX_PATH 초과)
    }
#elif defined(__linux__) || defined(__ANDROID__)
    // Android 는 app_process64 가 나옵니다. 진입점이 setPlatformDirs 로 다시 정합니다.
    std::error_code linkEc;
    const fs::path p = fs::read_symlink("/proc/self/exe", linkEc);
    if (!linkEc) return p;
#endif
    std::error_code ec;
    return fs::current_path(ec) / "unknown";
}

struct State {
    fs::path    exePath = findExePath();
    fs::path    exeDir  = exePath.parent_path();
    std::string exeStem = exePath.stem().string();
    fs::path    writeRoot;          // 비어 있으면 실행 폴더
    bool        platformDirs = false;   // 진입점이 폴더를 정함(Android). 그 뒤 initWriteRoot 는 아무것도 하지 않음
    std::vector<fs::path> extraPackDirs;
};

State& state() {
    static State s;
    return s;
}

#ifdef _WIN32
fs::path localAppData() {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &p)) && p) out = fs::path(p);
    if (p) CoTaskMemFree(p);
    return out;
}
#endif

} // namespace

namespace happ {

const fs::path& exeDir() { return state().exeDir; }
const std::string& exeStem() { return state().exeStem; }

bool initWriteRoot(bool userDataFolder) {
    State& s = state();
    if (s.platformDirs) return true;   // Android 진입점이 이미 정함
    s.writeRoot.clear();
    if (!userDataFolder) return true;
#ifdef _WIN32
    const fs::path base = localAppData();
    if (base.empty()) return false;
    const fs::path root = base / "Haruka" / s.exeStem;
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec || !fs::is_directory(root, ec)) return false;
    s.writeRoot = root;
    return true;
#else
    return false;   // Android 는 진입점이 외부 앱 폴더를 넘겨 줄 때 정합니다(Android 단계)
#endif
}

const fs::path& writeRoot() {
    const State& s = state();
    return s.writeRoot.empty() ? s.exeDir : s.writeRoot;
}

bool writeRootIsExeDir() { return state().writeRoot.empty(); }

void setPlatformDirs(const fs::path& contentDir, const fs::path& writeRoot, const std::string& stem) {
    State& s = state();
    s.exeDir    = contentDir;
    s.exePath   = contentDir / stem;
    s.exeStem   = stem;
    s.writeRoot = writeRoot;
    s.platformDirs = true;
    std::error_code ec;
    fs::create_directories(writeRoot, ec);
}

void addPackDir(const fs::path& dir) {
    if (!dir.empty()) state().extraPackDirs.push_back(dir);
}

std::vector<fs::path> packDirs() {
    std::vector<fs::path> dirs{ state().exeDir };
    for (const auto& d : state().extraPackDirs)
        if (d != dirs.front()) dirs.push_back(d);
    return dirs;
}

} // namespace happ
