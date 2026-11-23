#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hprojops {

struct OpResult {
    bool ok = false;
    int exitCode = 1;
    std::string message;
    std::string fixHint;
};

struct TemplateInfo {
    std::string name;
    std::string description;
    std::vector<std::string> tokens;
    std::filesystem::path dir;
};

struct NewProjectOptions {
    std::filesystem::path targetDir;
    std::string name;
    std::string company = "Haruka";
    std::string packageId;
    std::string templateName = "Blank";
    std::filesystem::path engineRoot;
    std::string engineVersion = "0.1.0";
};

struct VsGeneratorInfo {
    std::string generatorName;
    std::string displayName;
};

struct RecentProjectEntry {
    std::string name;
    std::string path;
    std::string engine;
    std::string lastOpened;
};

struct OpenPlan {
    std::filesystem::path projectDir;
    std::filesystem::path editorExe;
    std::string projectName;
    bool needsBuild = false;
    std::string buildReason;
};

struct ProjectResolveResult {
    bool ok = false;
    std::filesystem::path projectDir;
    std::string sourceLog;
    std::string errorMessage;
    std::string fixHint;
};

struct EngineResolveResult {
    bool ok = false;
    std::filesystem::path engineRoot;
    std::filesystem::path engineContentDir;
    std::string sourceLog;
    std::string errorMessage;
    std::string fixHint;
};

// ── Naming & validation ─────────────────────────────────────────────────────
bool isValidProjectName(std::string_view name);
std::string sanitizePackageIdent(std::string_view text, std::string_view fallback = "app");
std::string makeDefaultPackageId(std::string_view company, std::string_view name);
std::string replaceAll(std::string_view input, std::string_view from, std::string_view to);

// ── Engine & template discovery ─────────────────────────────────────────────
std::filesystem::path findEngineRoot(const std::filesystem::path& startDir);
int compareVersions(std::string_view a, std::string_view b);
bool versionMatches(std::string_view ver, std::string_view op, std::string_view req);
ProjectResolveResult resolveEditorProjectDir(const std::string& cliProjectArg,
                                             const std::filesystem::path& exeDir,
                                             const std::string& compileTimeFallback = {});
EngineResolveResult resolveEngineForProject(const std::filesystem::path& projectDir,
                                            const std::filesystem::path& enginesIniOverride = {});
bool loadTemplateInfo(const std::filesystem::path& templateDir, TemplateInfo& out, std::string& outError);
std::vector<TemplateInfo> listTemplates(const std::filesystem::path& engineRoot);

// ── Project creation ────────────────────────────────────────────────────────
OpResult createProjectFromTemplate(const NewProjectOptions& opts);

// ── Solution generation & build (CMake + vswhere) ───────────────────────────
using OutputLineCallback = std::function<void(const std::string& line)>;

bool findVsGenerator(VsGeneratorInfo& out, std::string& outError, std::string& outFixHint);
OpResult generateProjectFiles(const std::filesystem::path& projectDir,
                              const std::filesystem::path& harukaDirOverride = {},
                              const std::filesystem::path& enginesIniOverride = {},
                              const OutputLineCallback& onOutputLine = {});
OpResult buildProjectFiles(const std::filesystem::path& projectDir,
                           const OutputLineCallback& onOutputLine = {});
std::filesystem::path findProjectSolution(const std::filesystem::path& projectDir);

// ── Recent projects registry ────────────────────────────────────────────────
std::filesystem::path resolveUserDir(const std::filesystem::path& overrideDir = {});
std::filesystem::path recentProjectsFile(const std::filesystem::path& userDir);
bool loadRecentProjects(const std::filesystem::path& userDir,
                        std::vector<RecentProjectEntry>& out,
                        std::string& outError);
bool saveRecentProjects(const std::filesystem::path& userDir,
                        const std::vector<RecentProjectEntry>& entries,
                        std::string& outError);
OpResult registerProject(const std::filesystem::path& projectDir,
                         const std::filesystem::path& userDir,
                         RecentProjectEntry* outEntry = nullptr);
OpResult removeRecentProject(const std::filesystem::path& projectDir,
                             const std::filesystem::path& userDir);
std::string formatUtcIsoToLocalMinute(std::string_view isoUtc);
std::filesystem::path defaultProjectsDir();
std::filesystem::path loadLastProjectLocation(const std::filesystem::path& userDir);
bool saveLastProjectLocation(const std::filesystem::path& userDir, const std::filesystem::path& location);

// ── Open project & shell helpers ────────────────────────────────────────────
OpResult planOpenProject(const std::filesystem::path& projectDir, OpenPlan& outPlan);
OpResult openProject(const std::filesystem::path& projectDir,
                     const std::filesystem::path& userDir,
                     bool dryRun,
                     const std::vector<std::string>& extraArgs = {},
                     bool waitForExit = false);
OpResult openFolderInExplorer(const std::filesystem::path& dir);
OpResult openInVisualStudio(const std::filesystem::path& slnPath);
std::filesystem::path pickFolderDialog(const std::filesystem::path& initialDir = {});

// ── Windows file association (.hproject under HKCU\Software\Classes) ────────
struct FileAssociationEntry {
    std::string regKey;
    std::string valueName; // empty = (Default)
    std::string valueData;
};

std::vector<FileAssociationEntry> buildFileAssociationEntries(const std::filesystem::path& launcherExePath);
std::vector<std::string> fileAssociationDeleteKeys();
OpResult associateFileTypes(const std::filesystem::path& launcherExePath, bool dryRun);
OpResult unassociateFileTypes(bool dryRun);

} // namespace hprojops
