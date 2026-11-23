#include "LauncherApp.hpp"
#include "LauncherHost.hpp"
#include "projectops/ProjectOps.hpp"
#include "vfs/PathUtf8.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
std::string wideToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::vector<std::string> getUtf8Args() {
    std::vector<std::string> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(wideToUtf8(argv[i]));
    }
    LocalFree(argv);
    return args;
}

fs::path getExePath() {
    wchar_t buf[MAX_PATH * 4]{};
    const DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (len == 0) return {};
    return fs::path(std::wstring(buf, len));
}

fs::path getExeDir() {
    const fs::path p = getExePath();
    return p.empty() ? fs::current_path() : p.parent_path();
}

void attachConsoleForCli() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
    }
    SetConsoleOutputCP(CP_UTF8);
}
#else
fs::path getExePath() { return {}; }
fs::path getExeDir() { return fs::current_path(); }
void attachConsoleForCli() {}
#endif

void printUsage() {
    std::printf(
        "HarukaLauncher - Haruka Engine Project Launcher\n"
        "Usage:\n"
        "  HarukaLauncher                         (open GUI window)\n"
        "  HarukaLauncher <file.hproject>         (open project)\n"
        "  HarukaLauncher new <folder> <name> [--template=Blank] [--company=Haruka] [--package-id=...] [--user-dir=...]\n"
        "  HarukaLauncher generate <project> [--haruka-dir=...] [--engines-ini=...]\n"
        "  HarukaLauncher register <project> [--user-dir=...]\n"
        "  HarukaLauncher open <project> [--dry-run] [--wait] [--user-dir=...] [-- <editor args>...]\n"
        "  HarukaLauncher list [--user-dir=...]\n"
        "  HarukaLauncher associate [--dry-run]\n"
        "  HarukaLauncher unassociate [--dry-run]\n"
    );
}

int printResult(const hprojops::OpResult& res) {
    if (res.ok) {
        if (!res.message.empty()) {
            std::printf("%s\n", res.message.c_str());
        }
        return 0;
    }
    std::fprintf(stderr, "error: %s%s%s\n",
                 res.message.c_str(),
                 res.fixHint.empty() ? "" : " ",
                 res.fixHint.empty() ? "" : ("(" + res.fixHint + ")").c_str());
    return res.exitCode != 0 ? res.exitCode : 1;
}

bool parsePathArg(std::string_view utf8, fs::path& out, std::string_view label) {
    if (!hpak::pathFromUtf8(utf8, out) || out.empty()) {
        std::fprintf(stderr, "error: invalid UTF-8 path for %.*s\n", static_cast<int>(label.size()), label.data());
        return false;
    }
    return true;
}

void normalizeProjectPath(fs::path& p) {
    std::error_code ec;
    if (fs::is_regular_file(p, ec) || p.extension() == ".hproject") {
        if (p.has_parent_path()) p = p.parent_path();
    }
}

bool matchOptValue(const std::vector<std::string>& args, size_t& i, std::string_view prefixEq, std::string_view exactFlag, std::string& outVal) {
    const std::string_view a = args[i];
    if (a.rfind(prefixEq, 0) == 0) {
        outVal = std::string(a.substr(prefixEq.size()));
        return true;
    }
    if (a == exactFlag && i + 1 < args.size()) {
        outVal = args[++i];
        return true;
    }
    return false;
}

bool isCliCommand(std::string_view cmd) {
    return cmd == "new" || cmd == "generate" || cmd == "register" || cmd == "open" ||
           cmd == "list" || cmd == "associate" || cmd == "unassociate" ||
           cmd == "--help" || cmd == "-h" || cmd == "help";
}

bool endsWithHproject(std::string_view s) {
    if (s.size() < 9) return false;
    const std::string_view tail = s.substr(s.size() - 9);
    const char* target = ".hproject";
    for (size_t i = 0; i < 9; ++i) {
        char c = tail[i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c != target[i]) return false;
    }
    return true;
}

bool parseWindowSize(std::string_view s, uint32_t& w, uint32_t& h) {
    const size_t x = s.find('x');
    if (x == std::string_view::npos) return false;
    const int pw = std::atoi(std::string(s.substr(0, x)).c_str());
    const int ph = std::atoi(std::string(s.substr(x + 1)).c_str());
    if (pw <= 0 || ph <= 0) return false;
    w = static_cast<uint32_t>(pw);
    h = static_cast<uint32_t>(ph);
    return true;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    const std::vector<std::string> args = getUtf8Args();
#else
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
#endif

    // Check for CLI subcommand or direct .hproject argument (no GPU/Vulkan/window)
    if (args.size() >= 2 && (isCliCommand(args[1]) || endsWithHproject(args[1]))) {
        attachConsoleForCli();

        const std::string cmd = endsWithHproject(args[1]) ? std::string("open") : args[1];
        const size_t argStart = endsWithHproject(args[1]) ? 1 : 2;

        if (cmd == "--help" || cmd == "-h" || cmd == "help") {
            printUsage();
            return 0;
        }

        fs::path userDirOverride;
        fs::path enginesIniOverride;

        if (cmd == "new") {
            std::vector<std::string> pos;
            hprojops::NewProjectOptions opts;
            for (size_t i = 2; i < args.size(); ++i) {
                std::string val;
                if (matchOptValue(args, i, "--template=", "--template", val)) {
                    opts.templateName = val;
                } else if (matchOptValue(args, i, "--company=", "--company", val)) {
                    opts.company = val;
                } else if (matchOptValue(args, i, "--package-id=", "--package-id", val)) {
                    opts.packageId = val;
                } else if (matchOptValue(args, i, "--user-dir=", "--user-dir", val)) {
                    if (!parsePathArg(val, userDirOverride, "--user-dir")) return 1;
                } else if (!args[i].empty() && args[i][0] == '-') {
                    std::fprintf(stderr, "error: unknown option '%s' for 'new' (run HarukaLauncher --help)\n", args[i].c_str());
                    return 1;
                } else {
                    pos.push_back(args[i]);
                }
            }
            if (pos.size() != 2) {
                std::fprintf(stderr, "error: 'new' requires <folder> <name> (usage: HarukaLauncher new <folder> <name>)\n");
                return 1;
            }
            if (!parsePathArg(pos[0], opts.targetDir, "folder")) return 1;
            opts.name = pos[1];
            opts.engineRoot = hprojops::findEngineRoot(getExeDir());
            if (opts.engineRoot.empty()) {
                opts.engineRoot = hprojops::findEngineRoot(fs::current_path());
            }

            const hprojops::OpResult res = hprojops::createProjectFromTemplate(opts);
            if (!res.ok) return printResult(res);

            const fs::path userDir = hprojops::resolveUserDir(userDirOverride);
            hprojops::registerProject(opts.targetDir, userDir, nullptr);
            return printResult(res);
        }

        if (cmd == "generate") {
            std::vector<std::string> pos;
            fs::path harukaDirOverride;
            for (size_t i = 2; i < args.size(); ++i) {
                std::string val;
                if (matchOptValue(args, i, "--haruka-dir=", "--haruka-dir", val)) {
                    if (!parsePathArg(val, harukaDirOverride, "--haruka-dir")) return 1;
                } else if (matchOptValue(args, i, "--engines-ini=", "--engines-ini", val)) {
                    if (!parsePathArg(val, enginesIniOverride, "--engines-ini")) return 1;
                } else if (!args[i].empty() && args[i][0] == '-') {
                    std::fprintf(stderr, "error: unknown option '%s' for 'generate' (run HarukaLauncher --help)\n", args[i].c_str());
                    return 1;
                } else {
                    pos.push_back(args[i]);
                }
            }
            if (pos.size() != 1) {
                std::fprintf(stderr, "error: 'generate' requires <project> (usage: HarukaLauncher generate <project>)\n");
                return 1;
            }
            fs::path projDir;
            if (!parsePathArg(pos[0], projDir, "project")) return 1;
            normalizeProjectPath(projDir);
            return printResult(hprojops::generateProjectFiles(projDir, harukaDirOverride, enginesIniOverride));
        }

        if (cmd == "register") {
            std::vector<std::string> pos;
            for (size_t i = 2; i < args.size(); ++i) {
                std::string val;
                if (matchOptValue(args, i, "--user-dir=", "--user-dir", val)) {
                    if (!parsePathArg(val, userDirOverride, "--user-dir")) return 1;
                } else if (!args[i].empty() && args[i][0] == '-') {
                    std::fprintf(stderr, "error: unknown option '%s' for 'register' (run HarukaLauncher --help)\n", args[i].c_str());
                    return 1;
                } else {
                    pos.push_back(args[i]);
                }
            }
            if (pos.size() != 1) {
                std::fprintf(stderr, "error: 'register' requires <project> (usage: HarukaLauncher register <project>)\n");
                return 1;
            }
            fs::path projDir;
            if (!parsePathArg(pos[0], projDir, "project")) return 1;
            normalizeProjectPath(projDir);
            const fs::path userDir = hprojops::resolveUserDir(userDirOverride);
            return printResult(hprojops::registerProject(projDir, userDir, nullptr));
        }

        if (cmd == "list") {
            for (size_t i = 2; i < args.size(); ++i) {
                std::string val;
                if (matchOptValue(args, i, "--user-dir=", "--user-dir", val)) {
                    if (!parsePathArg(val, userDirOverride, "--user-dir")) return 1;
                } else {
                    std::fprintf(stderr, "error: unknown option '%s' for 'list' (run HarukaLauncher --help)\n", args[i].c_str());
                    return 1;
                }
            }
            const fs::path userDir = hprojops::resolveUserDir(userDirOverride);
            std::vector<hprojops::RecentProjectEntry> entries;
            std::string err;
            if (!hprojops::loadRecentProjects(userDir, entries, err)) {
                std::fprintf(stderr, "error: failed to load recent projects: %s (check %s)\n",
                             err.c_str(), hpak::utf8Of(hprojops::recentProjectsFile(userDir)).c_str());
                return 1;
            }
            if (entries.empty()) {
                std::printf("No recent projects.\n");
                return 0;
            }
            for (const auto& e : entries) {
                std::printf("%s\t%s\t%s\t%s\n",
                            e.name.c_str(),
                            e.path.c_str(),
                            e.engine.c_str(),
                            e.lastOpened.c_str());
            }
            return 0;
        }

        if (cmd == "associate") {
            bool dryRun = false;
            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "--dry-run") {
                    dryRun = true;
                } else {
                    std::fprintf(stderr, "error: unknown option '%s' for 'associate'\n", args[i].c_str());
                    return 1;
                }
            }
            return printResult(hprojops::associateFileTypes(getExePath(), dryRun));
        }

        if (cmd == "unassociate") {
            bool dryRun = false;
            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "--dry-run") {
                    dryRun = true;
                } else {
                    std::fprintf(stderr, "error: unknown option '%s' for 'unassociate'\n", args[i].c_str());
                    return 1;
                }
            }
            return printResult(hprojops::unassociateFileTypes(dryRun));
        }

        if (cmd == "open") {
            std::vector<std::string> pos;
            std::vector<std::string> extraArgs;
            bool dryRun = false;
            bool waitForExit = false;
            bool afterDashDash = false;
            for (size_t i = argStart; i < args.size(); ++i) {
                if (afterDashDash) {
                    extraArgs.push_back(args[i]);
                    continue;
                }
                if (args[i] == "--") {
                    afterDashDash = true;
                    continue;
                }
                std::string val;
                if (args[i] == "--dry-run") {
                    dryRun = true;
                } else if (args[i] == "--wait") {
                    waitForExit = true;
                } else if (matchOptValue(args, i, "--user-dir=", "--user-dir", val)) {
                    if (!parsePathArg(val, userDirOverride, "--user-dir")) return 1;
                } else if (args[i] == "--hidden-window" ||
                           args[i].rfind("--frames=", 0) == 0 ||
                           args[i].rfind("--gpu=", 0) == 0 ||
                           args[i].rfind("--write-root=", 0) == 0) {
                    extraArgs.push_back(args[i]);
                    if (args[i] == "--hidden-window") waitForExit = true;
                } else if (!args[i].empty() && args[i][0] == '-') {
                    std::fprintf(stderr, "error: unknown option '%s' for 'open' (pass after '--' to forward to editor)\n", args[i].c_str());
                    return 1;
                } else {
                    pos.push_back(args[i]);
                }
            }
            if (pos.size() != 1) {
                std::fprintf(stderr, "error: 'open' requires <project> (usage: HarukaLauncher open <project> [--dry-run])\n");
                return 1;
            }
            fs::path projDir;
            if (!parsePathArg(pos[0], projDir, "project")) return 1;
            normalizeProjectPath(projDir);
            const fs::path userDir = hprojops::resolveUserDir(userDirOverride);
            return printResult(hprojops::openProject(projDir, userDir, dryRun, extraArgs, waitForExit));
        }
    }

    // GUI mode
    LauncherHostOptions hostOpts;
    hostOpts.exeDir = getExeDir();
    hostOpts.engineRoot = hprojops::findEngineRoot(hostOpts.exeDir);
    if (hostOpts.engineRoot.empty()) {
        hostOpts.engineRoot = hprojops::findEngineRoot(fs::current_path());
    }

    LauncherAppOptions appOpts;
    appOpts.engineRoot = hostOpts.engineRoot;

    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        std::string val;
        if (a == "--hidden-window") {
            hostOpts.hiddenWindow = true;
            appOpts.hiddenWindow = true;
        } else if (a == "--ui-create") {
            appOpts.autoCreate = true;
        } else if (matchOptValue(args, i, "--window-size=", "--window-size", val)) {
            parseWindowSize(val, hostOpts.width, hostOpts.height);
        } else if (matchOptValue(args, i, "--gpu=", "--gpu", val)) {
            hostOpts.preferredGpu = std::atoi(val.c_str());
        } else if (matchOptValue(args, i, "--frames=", "--frames", val)) {
            const int f = std::atoi(val.c_str());
            if (f > 0) hostOpts.maxFrames = static_cast<uint32_t>(f);
        } else if (matchOptValue(args, i, "--ui-dump=", "--ui-dump", val)) {
            hostOpts.uiDumpPath = val;
        } else if (matchOptValue(args, i, "--capture=", "--capture", val)) {
            hostOpts.capturePath = val;
        } else if (matchOptValue(args, i, "--culture=", "--culture", val) ||
                   matchOptValue(args, i, "--lang=", "--lang", val)) {
            hostOpts.culture = val;
        } else if (matchOptValue(args, i, "--user-dir=", "--user-dir", val)) {
            parsePathArg(val, appOpts.userDirOverride, "--user-dir");
        } else if (matchOptValue(args, i, "--engines-ini=", "--engines-ini", val)) {
            parsePathArg(val, appOpts.enginesIniOverride, "--engines-ini");
        } else if (matchOptValue(args, i, "--ui-page=", "--ui-page", val)) {
            if (val == "recent") appOpts.initialPage = ELauncherPage::Recent;
            else if (val == "new") appOpts.initialPage = ELauncherPage::New;
            else if (val == "progress") appOpts.initialPage = ELauncherPage::Progress;
        } else if (matchOptValue(args, i, "--ui-new-dir=", "--ui-new-dir", val)) {
            appOpts.newDir = val;
        } else if (matchOptValue(args, i, "--ui-new-name=", "--ui-new-name", val)) {
            appOpts.newName = val;
        } else if (matchOptValue(args, i, "--ui-new-company=", "--ui-new-company", val)) {
            appOpts.newCompany = val;
        } else {
            attachConsoleForCli();
            std::fprintf(stderr, "error: unknown option '%s' (run HarukaLauncher --help)\n", a.c_str());
            return 1;
        }
    }

    LauncherHost host;
    if (!host.init(hostOpts)) {
        return 1;
    }

    LauncherApp app(appOpts);
    app.init(host);

    const int rc = host.run(
        [&app](float dt) { app.tick(dt); },
        [&app]() { return app.isAsyncFinished(); }
    );
    return rc;
}
