#pragma once

#include "projectops/ProjectOps.hpp"
#include "ui/slate/HSlate.hpp"
#include "ui/slate/HTextEdit.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class LauncherHost;

enum class ELauncherPage {
    Recent,
    New,
    Progress,
};

struct LauncherAppOptions {
    std::filesystem::path userDirOverride;
    std::filesystem::path enginesIniOverride;
    std::filesystem::path engineRoot;
    ELauncherPage initialPage = ELauncherPage::Recent;
    std::string newDir;
    std::string newName;
    std::string newCompany;
    bool autoCreate = false;
    bool hiddenWindow = false;
};

class LauncherApp {
public:
    explicit LauncherApp(const LauncherAppOptions& opts);
    ~LauncherApp();

    void init(LauncherHost& host);
    void tick(float dt);
    bool isAsyncFinished() const;

private:
    enum class ENewField { None, Location, Name, Company, PackageId };
    enum class EProgressStep { Idle, Copying, Generating, Building, Done, Failed };

    void rebuildUi();
    void refreshRecent();
    void handleInput(float dt);
    void handleTextFieldKeys();
    HTextEditState* activeEditState();
    void setFocusedField(ENewField f);
    void updateAutoPackageId();
    void validateNewForm();
    void startCreateProject();
    void startBuildAndOpen(const std::filesystem::path& projDir);
    void joinWorker();

    std::shared_ptr<HWidget> buildHeader();
    std::shared_ptr<HWidget> buildRecentPage();
    std::shared_ptr<HWidget> buildNewPage();
    std::shared_ptr<HWidget> buildProgressPage();
    std::shared_ptr<HWidget> makeEditableRow(const std::string& label,
                                             HTextEditState& state,
                                             ENewField field,
                                             const std::string& placeholder,
                                             const std::string& errorMsg = {},
                                             std::shared_ptr<HWidget> trailing = nullptr);

    LauncherAppOptions opts_;
    LauncherHost* host_ = nullptr;
    std::filesystem::path userDir_;

    ELauncherPage page_ = ELauncherPage::Recent;
    bool dirty_ = true;

    // Input tracking
    bool prevMouseDown_ = false;
    float prevMouseX_ = 0.f;
    float prevMouseY_ = 0.f;

    // Recent page state
    std::vector<hprojops::RecentProjectEntry> recentEntries_;
    std::string recentStatus_;
    float recentScroll_ = 0.f;

    // New project page state
    std::vector<hprojops::TemplateInfo> templates_;
    size_t selectedTemplate_ = 0;
    HTextEditState editLocation_;
    HTextEditState editName_;
    HTextEditState editCompany_;
    HTextEditState editPackageId_;
    bool packageIdManual_ = false;
    bool buildAfterCreate_ = false;
    ENewField focusedField_ = ENewField::None;
    std::string nameError_;
    std::string locationError_;
    std::shared_ptr<SEditableText> wLocation_, wName_, wCompany_, wPackageId_;

    // Progress page state
    std::mutex progressMutex_;
    std::atomic<EProgressStep> progressStep_{ EProgressStep::Idle };
    std::atomic<EProgressStep> failedAtStep_{ EProgressStep::Idle };
    std::atomic<bool> progressBuildSelected_{ false };
    std::vector<std::string> progressLogs_;
    std::string progressSummary_;
    std::filesystem::path activeProjectDir_;
    float progressScroll_ = 0.f;
    std::thread worker_;
};
