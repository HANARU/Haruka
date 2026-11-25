// HarukaProjectOps unit tests (E4, 2026-11-23). Runs without GPU.
#include "projectops/ProjectOps.hpp"
#include "core/ProjectDescriptor.hpp"
#include "vfs/PathUtf8.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef HARUKA_SOURCE_DIR
#error "HARUKA_SOURCE_DIR must point to the repository root"
#endif

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

std::string readFileText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string trimText(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

void writeFileText(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

fs::path makeTempRoot() {
    fs::path base = fs::temp_directory_path() / "haruka_projectops_test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base;
}

} // namespace

int main() {
    std::printf("=== HarukaProjectOpsTest ===\n");

    // 1. Naming & validation
    check(hprojops::isValidProjectName("MyGame"), "valid project name MyGame");
    check(hprojops::isValidProjectName("_Game2"), "valid project name _Game2");
    check(!hprojops::isValidProjectName(""), "reject empty project name");
    check(!hprojops::isValidProjectName("1Game"), "reject leading digit in project name");
    check(!hprojops::isValidProjectName("My-Game"), "reject hyphen in project name");
    check(!hprojops::isValidProjectName("My Game"), "reject space in project name");

    check(hprojops::sanitizePackageIdent("My Studio") == "mystudio", "sanitizePackageIdent strips spaces/caps");
    check(hprojops::sanitizePackageIdent("123", "haruka") == "haruka123", "sanitizePackageIdent prefixes fallback on digit");
    check(hprojops::sanitizePackageIdent("", "app") == "app", "sanitizePackageIdent fallback on empty");
    check(hprojops::makeDefaultPackageId("Haruka", "MyGame") == "com.haruka.mygame", "default packageId");
    check(hprojops::makeDefaultPackageId("123 Co", "9Game") == "com.haruka123co.game9game", "default packageId with leading digits");
    check(hprojops::replaceAll("A {{NAME}} B {{NAME}}", "{{NAME}}", "X") == "A X B X", "replaceAll replaces all occurrences");

    const fs::path engineRoot = fs::path(HARUKA_SOURCE_DIR);
    const fs::path tmp = makeTempRoot();

    // 2. Create project from Templates/Blank in a Korean + space path
    fs::path koProjDir;
    const std::string koRelUtf8 = "\xed\x95\x9c\xea\xb8\x80 \xea\xb2\xbd\xeb\xa1\x9c/My Game Proj"; // "한글 경로/My Game Proj"
    fs::path koRelPath;
    check(hpak::pathFromUtf8(koRelUtf8, koRelPath), "pathFromUtf8 Korean+space path");
    koProjDir = tmp / koRelPath;

    hprojops::NewProjectOptions opts;
    opts.targetDir = koProjDir;
    opts.name = "MyGame";
    opts.company = "Test Co";
    opts.templateName = "Blank";
    opts.engineRoot = engineRoot;

    const hprojops::OpResult createRes = hprojops::createProjectFromTemplate(opts);
    check(createRes.ok, "createProjectFromTemplate succeeds in Korean+space path");
    check(!fs::exists(koProjDir / "template.ini"), "template.ini is not copied to target project");
    check(fs::exists(koProjDir / "cmake" / "HarukaEngineFinder.cmake"), "HarukaEngineFinder.cmake is copied");
    check(fs::exists(koProjDir / "Config" / "project.ini"), "Config/project.ini exists");
    check(fs::exists(koProjDir / "Content" / "scenes" / "Start.hscene"), "Content/scenes/Start.hscene exists");

    const fs::path hprojFile = koProjDir / "MyGame.hproject";
    check(fs::exists(hprojFile), "MyGame.hproject exists");
    ProjectDescriptor desc;
    std::vector<std::string> warnings, errors;
    check(project_descriptor::loadFromFile(hprojFile, desc, warnings, errors), "generated .hproject loads cleanly");
    check(desc.name == "MyGame", "descriptor name == MyGame");
    check(desc.company == "Test Co", "descriptor company == Test Co");
    check(desc.packageId == "com.testco.mygame", "descriptor packageId == com.testco.mygame");
    check(desc.engine == "haruka>=0.1", "descriptor engine == haruka>=0.1");

    const std::string cmakeText = readFileText(koProjDir / "CMakeLists.txt");
    check(cmakeText.find("project(MyGame LANGUAGES CXX)") != std::string::npos, "CMakeLists.txt substitutes {{NAME}}");
    check(cmakeText.find("MyGameGameModule.cpp") != std::string::npos, "CMakeLists.txt references MyGameGameModule.cpp");
    check(cmakeText.find("{{") == std::string::npos, "CMakeLists.txt has no leftover tokens");

    const fs::path modCpp = koProjDir / "Source" / "MyGameGameModule.cpp";
    check(fs::exists(modCpp), "Source/MyGameGameModule.cpp exists");
    const std::string modText = readFileText(modCpp);
    check(modText.find("class MyGameGameModule") != std::string::npos, "GameModule class renamed to MyGameGameModule");

    const std::string gitignoreText = readFileText(koProjDir / ".gitignore");
    check(gitignoreText.find("Build/") != std::string::npos &&
          gitignoreText.find("Saved/") != std::string::npos &&
          gitignoreText.find("Haruka.user.ini") != std::string::npos,
          ".gitignore includes Build/, Saved/, Haruka.user.ini");

    // 3. Refuse existing target folder
    writeFileText(koProjDir / "keep.txt", "keep");
    const hprojops::OpResult dupRes = hprojops::createProjectFromTemplate(opts);
    check(!dupRes.ok, "createProjectFromTemplate refuses existing folder");
    check(readFileText(koProjDir / "keep.txt") == "keep", "existing folder contents untouched on refusal");

    // 4. Cleanup on failure (missing {{NAME}} token in template.ini)
    const fs::path badEngine = tmp / "bad_engine";
    const fs::path badTpl = badEngine / "Templates" / "Broken";
    writeFileText(badTpl / "template.ini", "name = Broken\ntokens = {{COMPANY}}\n");
    writeFileText(badTpl / "{{NAME}}.hproject", "name = {{NAME}}\ncompany = {{COMPANY}}\npackageId = com.a.b\nengineVersion = 0.1.0\n");

    const fs::path failTarget = tmp / "should_clean_up";
    hprojops::NewProjectOptions badOpts;
    badOpts.targetDir = failTarget;
    badOpts.name = "FailProj";
    badOpts.engineRoot = badEngine;
    badOpts.templateName = "Broken";
    const hprojops::OpResult failRes = hprojops::createProjectFromTemplate(badOpts);
    check(!failRes.ok, "createProjectFromTemplate fails when {{NAME}} token is missing");
    check(!fs::exists(failTarget), "failed project creation cleans up target directory");

    // 5. Recent projects read/write & deduplication
    const fs::path userDir = tmp / "launcher_user";
    std::vector<hprojops::RecentProjectEntry> recent;
    std::string err;
    check(hprojops::loadRecentProjects(userDir, recent, err) && recent.empty(), "loadRecentProjects empty when file missing");

    check(hprojops::registerProject(koProjDir, userDir, nullptr).ok, "registerProject succeeds for Korean+space path");
    check(hprojops::loadRecentProjects(userDir, recent, err) && recent.size() == 1, "recent list has 1 entry");
    check(recent[0].name == "MyGame", "recent entry name == MyGame");

    // Re-registering same project deduplicates
    check(hprojops::registerProject(koProjDir, userDir, nullptr).ok, "re-registering same project succeeds");
    check(hprojops::loadRecentProjects(userDir, recent, err) && recent.size() == 1, "recent list deduplicates same path");

    // Register a second project -> appears first
    const fs::path proj2 = tmp / "SecondProj";
    hprojops::NewProjectOptions opts2 = opts;
    opts2.targetDir = proj2;
    opts2.name = "SecondProj";
    check(hprojops::createProjectFromTemplate(opts2).ok, "create SecondProj succeeds");
    check(hprojops::registerProject(proj2, userDir, nullptr).ok, "register SecondProj succeeds");
    check(hprojops::loadRecentProjects(userDir, recent, err) && recent.size() == 2 && recent[0].name == "SecondProj",
          "most recently registered project is first");

    // 6. planOpenProject (needsBuild when missing, up-to-date, and stale)
    hprojops::OpenPlan plan;
    check(hprojops::planOpenProject(koProjDir, plan).ok && plan.needsBuild, "planOpenProject reports needsBuild when exe missing");

    const fs::path fakeExe = koProjDir / "Build" / "Release" / "MyGameEditor.exe";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    writeFileText(fakeExe, "MZ");
    check(hprojops::planOpenProject(koProjDir, plan).ok && !plan.needsBuild, "planOpenProject reports ready when exe is newer than Source");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    writeFileText(modCpp, readFileText(modCpp) + "\n// touched\n");
    check(hprojops::planOpenProject(koProjDir, plan).ok && plan.needsBuild, "planOpenProject reports needsBuild when Source is newer than exe");

    // 7. Version comparison & matching (E5)
    check(hprojops::compareVersions("0.1", "0.1.0") == 0, "compareVersions 0.1 == 0.1.0");
    check(hprojops::compareVersions("0.2.0", "0.1.9") > 0, "compareVersions 0.2.0 > 0.1.9");
    check(hprojops::compareVersions("0.10.0", "0.2.0") > 0, "compareVersions 0.10.0 > 0.2.0");
    check(hprojops::versionMatches("0.2.0", ">=", "0.1"), "versionMatches 0.2.0 >= 0.1");
    check(!hprojops::versionMatches("0.1.0", ">", "0.1"), "versionMatches 0.1.0 > 0.1 is false");
    check(hprojops::versionMatches("0.1.0", "==", "0.1"), "versionMatches 0.1.0 == 0.1");
    check(hprojops::versionMatches("0.1.0", "<=", "0.2"), "versionMatches 0.1.0 <= 0.2");
    check(!hprojops::versionMatches("0.2.0", "<", "0.2"), "versionMatches 0.2.0 < 0.2 is false");

    // 8. Editor project resolution (E5)
    {
        const fs::path exeDir = koProjDir / "Build" / "Release";
        fs::create_directories(exeDir);
        const auto rWalk = hprojops::resolveEditorProjectDir("", exeDir);
        check(rWalk.ok && fs::equivalent(rWalk.projectDir, koProjDir),
              "resolveEditorProjectDir walks up from Build/Release to projectDir");

        const auto rCliDir = hprojops::resolveEditorProjectDir(hpak::utf8Of(proj2), exeDir);
        check(rCliDir.ok && fs::equivalent(rCliDir.projectDir, proj2),
              "resolveEditorProjectDir prefers --project=<dir>");

        const auto rCliFile = hprojops::resolveEditorProjectDir(hpak::utf8Of(proj2 / "SecondProj.hproject"), exeDir);
        check(rCliFile.ok && fs::equivalent(rCliFile.projectDir, proj2),
              "resolveEditorProjectDir accepts --project=<.hproject>");

        const auto rBadCli = hprojops::resolveEditorProjectDir(hpak::utf8Of(tmp / "nonexistent_proj"), exeDir);
        check(!rBadCli.ok && !rBadCli.errorMessage.empty() && !rBadCli.fixHint.empty(),
              "resolveEditorProjectDir fails cleanly on bad --project");

        const auto rSamples = hprojops::resolveEditorProjectDir("", engineRoot / "build" / "Release");
        check(rSamples.ok && fs::equivalent(rSamples.projectDir, engineRoot / "Projects" / "Samples"),
              "resolveEditorProjectDir selects Samples when run inside engine tree");

        const fs::path isolatedDir = tmp / "isolated" / "a" / "b";
        fs::create_directories(isolatedDir);
        const auto rMissing = hprojops::resolveEditorProjectDir("", isolatedDir);
        check(!rMissing.ok && !rMissing.errorMessage.empty(),
              "resolveEditorProjectDir stops at root and fails when no .hproject exists");
    }

    // 9. Engine resolution & parity with HarukaEngineFinder.cmake (E5)
    {
        auto makeFakeEngine = [&](const fs::path& dir, const std::string& ver) {
            writeFileText(dir / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.21)\nproject(HarukaEngine VERSION " + ver + ")\n");
            writeFileText(dir / "cmake" / "HarukaProject.cmake", "# stub\n");
            fs::create_directories(dir / "Engine" / "Content");
            const fs::path bdir = dir / "build";
            fs::create_directories(bdir);
            writeFileText(bdir / "HarukaConfig.cmake",
                          "set(HARUKA_ENGINE_ROOT \"" + hpak::utf8Of(dir) + "\")\n");
        };

        const fs::path eng1 = tmp / "eng_0_1";
        const fs::path eng2 = tmp / "eng_0_2";
        const fs::path eng3 = tmp / "eng_0_3";
        makeFakeEngine(eng1, "0.1.0");
        makeFakeEngine(eng2, "0.2.0");
        makeFakeEngine(eng3, "0.3.0");

        const fs::path iniFile = tmp / "Engines.ini";
        writeFileText(iniFile,
            "[engine.0]\n"
            "name = haruka\n"
            "version = 0.1.0\n"
            "engineDir = " + hpak::utf8Of(eng1) + "\n"
            "buildDir = " + hpak::utf8Of(eng1 / "build") + "\n"
            "[engine.1]\n"
            "name = haruka\n"
            "version = 0.2.0\n"
            "engineDir = " + hpak::utf8Of(eng2) + "\n"
            "buildDir = " + hpak::utf8Of(eng2 / "build") + "\n"
            "[engine.2]\n"
            "name = haruka\n"
            "version = 0.3.0\n"
            "engineDir = " + hpak::utf8Of(eng3) + "\n"
            "buildDir = " + hpak::utf8Of(eng3 / "build") + "\n");

        // (a) Registry selection: engine = haruka>=0.1 -> picks highest (0.3.0)
        const auto eReg = hprojops::resolveEngineForProject(proj2, iniFile);
        check(eReg.ok && fs::equivalent(eReg.engineRoot, eng3),
              "resolveEngineForProject picks highest matching version in Engines.ini (0.3.0)");

        // (b) Registry selection with upper bound: haruka<0.3 -> picks 0.2.0
        const fs::path boundedProj = tmp / "BoundedProj";
        hprojops::NewProjectOptions bOpts = opts;
        bOpts.targetDir = boundedProj;
        bOpts.name = "BoundedProj";
        check(hprojops::createProjectFromTemplate(bOpts).ok, "create BoundedProj");
        writeFileText(boundedProj / "BoundedProj.hproject",
                      "name = BoundedProj\ncompany = Test\npackageId = com.test.bounded\nengineVersion = 0.1.0\nengine = haruka<0.3\n");
        const auto eBounded = hprojops::resolveEngineForProject(boundedProj, iniFile);
        check(eBounded.ok && fs::equivalent(eBounded.engineRoot, eng2),
              "resolveEngineForProject respects <0.3 and picks 0.2.0");

        // (c) Haruka.user.ini pointing to buildDir -> resolves eng1 via HARUKA_ENGINE_ROOT
        writeFileText(boundedProj / "Haruka.user.ini", "buildDir = " + hpak::utf8Of(eng1 / "build") + "\n");
        const auto eUser = hprojops::resolveEngineForProject(boundedProj, iniFile);
        check(eUser.ok && fs::equivalent(eUser.engineRoot, eng1),
              "resolveEngineForProject honors Haruka.user.ini buildDir override");

        // (d) Haruka.user.ini pointing to wrong path -> fails immediately (does not fall through to Engines.ini)
        writeFileText(boundedProj / "Haruka.user.ini", "buildDir = " + hpak::utf8Of(tmp / "does_not_exist") + "\n");
        const auto eBadUser = hprojops::resolveEngineForProject(boundedProj, iniFile);
        check(!eBadUser.ok && eBadUser.errorMessage.find("Haruka.user.ini") != std::string::npos,
              "resolveEngineForProject fails immediately on bad Haruka.user.ini");
        std::error_code rmUserEc;
        fs::remove(boundedProj / "Haruka.user.ini", rmUserEc);

        // (e) Enclosing engine tree
        const fs::path nestedProj = eng1 / "Sub" / "NestedProj";
        hprojops::NewProjectOptions nOpts = opts;
        nOpts.targetDir = nestedProj;
        nOpts.name = "NestedProj";
        check(hprojops::createProjectFromTemplate(nOpts).ok, "create NestedProj inside eng1");
        const auto eEnclosing = hprojops::resolveEngineForProject(nestedProj, iniFile);
        check(eEnclosing.ok && fs::equivalent(eEnclosing.engineRoot, eng1),
              "resolveEngineForProject selects enclosing engine tree over Engines.ini");

        // (f) Parity with cmake/HarukaEngineFinder.cmake on shared test table
        struct ParityCase {
            const char* label;
            fs::path projectDir;
            fs::path expectedBuildDir;
        };
        const ParityCase parityCases[] = {
            { "registry highest (0.3.0)", proj2, eng3 / "build" },
            { "registry bounded (<0.3 -> 0.2.0)", boundedProj, eng2 / "build" },
            { "enclosing tree (eng1)", nestedProj, eng1 / "build" },
        };
        for (const auto& pc : parityCases) {
            const fs::path parityDir = tmp / "parity_cmake";
            const fs::path parityBuild = parityDir / "b";
            std::error_code pEc;
            fs::remove_all(parityDir, pEc);
            fs::create_directories(parityDir, pEc);
            const fs::path outFile = parityDir / "finder_out.txt";
            writeFileText(parityDir / "CMakeLists.txt",
                "cmake_minimum_required(VERSION 3.21)\n"
                "project(ParityCheck NONE)\n"
                "set(CMAKE_CURRENT_SOURCE_DIR \"" + hpak::utf8Of(pc.projectDir) + "\")\n"
                "set(ENV{HARUKA_ENGINES_INI} \"" + hpak::utf8Of(iniFile) + "\")\n"
                "include(\"" + hpak::utf8Of(engineRoot / "cmake" / "HarukaEngineFinder.cmake") + "\")\n"
                "file(WRITE \"" + hpak::utf8Of(outFile) + "\" \"${Haruka_DIR}\")\n");
            const std::string cmd = "cmake -S \"" + hpak::utf8Of(parityDir) + "\" -B \"" + hpak::utf8Of(parityBuild) + "\" >nul 2>&1";
            const int rc = std::system(cmd.c_str());
            const std::string cmakeDir = trimText(readFileText(outFile));
            const auto cppRes = hprojops::resolveEngineForProject(pc.projectDir, iniFile);
            check(rc == 0 && cppRes.ok &&
                  fs::equivalent(fs::path(cmakeDir), pc.expectedBuildDir) &&
                  fs::equivalent(cppRes.engineRoot / "build", fs::path(cmakeDir)),
                  std::string("CMake & C++ parity: ") + pc.label);
        }
    }

    // 7. E6: File association model, dry-run, removeRecentProject, findProjectSolution
    {
        const fs::path fakeExe = tmp / "bin" / "HarukaLauncher.exe";
        const auto entries = hprojops::buildFileAssociationEntries(fakeExe);
        check(entries.size() == 7, "buildFileAssociationEntries returns 7 HKCU entries");
        auto hasKeyVal = [&](std::string_view key, std::string_view sub) {
            for (const auto& e : entries) {
                if (e.regKey == key && e.valueData.find(sub) != std::string::npos) return true;
            }
            return false;
        };
        check(hasKeyVal("Software\\Classes\\.hproject", "Haruka.Project"), ".hproject maps to Haruka.Project");
        check(hasKeyVal("Software\\Classes\\Haruka.Project\\DefaultIcon", "HarukaLauncher.exe"), "DefaultIcon points to HarukaLauncher.exe");
        check(hasKeyVal("Software\\Classes\\Haruka.Project\\shell\\open\\command", "\"%1\""), "open command passes %1");
        check(hasKeyVal("Software\\Classes\\Haruka.Project\\shell\\generate\\command", "generate \"%1\""), "generate command passes generate %1");

        const auto delKeys = hprojops::fileAssociationDeleteKeys();
        check(delKeys.size() == 2, "fileAssociationDeleteKeys returns 2 HKCU root keys");

        const auto assocDry = hprojops::associateFileTypes(fakeExe, true);
        check(assocDry.ok && assocDry.message.find("[dry-run]") != std::string::npos &&
              assocDry.message.find("Software\\Classes\\.hproject") != std::string::npos,
              "associateFileTypes(--dry-run) formats HKCU entries without writing");

        const auto unassocDry = hprojops::unassociateFileTypes(true);
        check(unassocDry.ok && unassocDry.message.find("[dry-run]") != std::string::npos &&
              unassocDry.message.find("Software\\Classes\\Haruka.Project") != std::string::npos,
              "unassociateFileTypes(--dry-run) formats HKCU delete keys");

        const fs::path rmUserDir = tmp / "rm_user";
        const fs::path pA = tmp / "proj_rm_a";
        const fs::path pB = tmp / "proj_rm_b";
        writeFileText(pA / "A.hproject", "name = ProjA\ncompany = Haruka\npackageId = com.haruka.a\nengineVersion = 0.1.0\n");
        writeFileText(pB / "B.hproject", "name = ProjB\ncompany = Haruka\npackageId = com.haruka.b\nengineVersion = 0.1.0\n");
        hprojops::registerProject(pA, rmUserDir, nullptr);
        hprojops::registerProject(pB, rmUserDir, nullptr);
        const auto rmRes = hprojops::removeRecentProject(pA, rmUserDir);
        std::vector<hprojops::RecentProjectEntry> remaining;
        std::string loadErr;
        check(rmRes.ok && hprojops::loadRecentProjects(rmUserDir, remaining, loadErr) &&
              remaining.size() == 1 && remaining[0].name == "ProjB",
              "removeRecentProject removes target project and keeps others");

        writeFileText(pA / "Build" / "ProjA.sln", "");
        const fs::path foundSln = hprojops::findProjectSolution(pA);
        check(!foundSln.empty() && foundSln.filename() == "ProjA.sln",
              "findProjectSolution locates .sln in Build/");

        // E6b: UTC->local minute formatting, defaultProjectsDir, lastLocation round-trip
        const std::string localFmt = hprojops::formatUtcIsoToLocalMinute("2026-11-23T12:28:53Z");
        check(localFmt.size() == 16 && localFmt[4] == '-' && localFmt[7] == '-' &&
              localFmt[10] == ' ' && localFmt[13] == ':',
              "formatUtcIsoToLocalMinute formats as YYYY-MM-DD HH:MM");
        check(hprojops::formatUtcIsoToLocalMinute("invalid") == "invalid",
              "formatUtcIsoToLocalMinute preserves non-ISO input");

        const fs::path defDir = hprojops::defaultProjectsDir();
        check(!defDir.empty() && defDir.filename() == "Haruka Projects",
              "defaultProjectsDir ends with 'Haruka Projects'");

        const fs::path settingsUserDir = tmp / "settings_user";
        const fs::path savedLoc = tmp / "MyCustomProjects";
        std::error_code mkEc;
        fs::create_directories(savedLoc, mkEc);
        check(hprojops::saveLastProjectLocation(settingsUserDir, savedLoc) &&
              fs::equivalent(hprojops::loadLastProjectLocation(settingsUserDir), savedLoc),
              "saveLastProjectLocation / loadLastProjectLocation round-trip");

        // PS1c: shared command line quoting & process helpers
        const std::string cmd = hprojops::formatCommandLine({
            "C:\\Program Files\\HarukaEngine.exe",
            "--project=C:\\내 폴더\\My \"Proj\"",
            "--restart-wait-pid=42"
        });
        check(cmd == "\"C:\\Program Files\\HarukaEngine.exe\" \"--project=C:\\내 폴더\\My \\\"Proj\\\"\" --restart-wait-pid=42",
              "formatCommandLine quotes spaces, Korean, and embedded quotes");
        check(hprojops::currentProcessId() != 0 && !hprojops::currentExecutablePath().empty(),
              "currentProcessId and currentExecutablePath are non-empty");
    }

    std::error_code rmEc;
    fs::remove_all(tmp, rmEc);

    std::printf("Summary: %d/%d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
