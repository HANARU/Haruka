#pragma once
#include "SettingsPanel.hpp"
#include "../core/ProjectSettings.hpp"
#include "../core/ProjectSettingsSchema.hpp"
#include "../core/ProjectDescriptor.hpp"
#include "../runtime/XrRunMode.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// ── 프로젝트 설정 창 (PS1b, 2026-11-24, Docs/Project_Settings_Design.md) ─────────────────────────────
// ProjectSettingsSchema + Config/project.ini + <이름>.hproject + Config/game.pack.txt 를
// SettingsPanel 에 연결합니다.
class ProjectSettingsPanel {
public:
    struct Deps {
        const EditorUiKit* ui = nullptr;
        std::function<void(glm::vec2, std::vector<EditorMenuEntry>)> openMenu;
        std::function<void(const std::shared_ptr<HWidget>&, std::vector<EditorMenuEntry>, int)> openCombo;
        std::function<bool(const HWidget*)> isComboOpen;
        std::function<void()>               closeMenu;
        std::function<void(const std::shared_ptr<HWidget>&)> focusText;
        std::function<void(const std::shared_ptr<HWidget>&, const std::string&, const std::string&,
                           std::function<void(const std::string&)>)> openAssetPicker;
        std::function<void(const std::string&)> browseToAsset;
        std::function<void()>                   onLiveSettingsChanged;
        std::function<const hxrmode::XrRunStatus&()> xrStatus;
    };

    void init(Deps deps);
    std::shared_ptr<HWidget> build();

    void setStartupProjectSettings(const ProjectSettings& ps);
    const ProjectSettings& liveProjectSettings() const { return currentIni_; }

    void reloadFiles();
    bool hasRestartChanges() const;

    // SettingsPanel 위임
    void rebuild() { panel_.rebuild(); }
    void flushRebuild() { panel_.flushRebuild(); }
    void markDirty() { panel_.markDirty(); }
    bool selectCategory(const std::string& id) { return panel_.selectCategory(id); }
    void setSearch(const std::string& text) { panel_.setSearch(text); }
    std::shared_ptr<HWidget> searchWidget() const { return panel_.searchWidget(); }
    TextInputField searchField() { return panel_.searchField(); }
    std::shared_ptr<HWidget> stringWidget() const { return panel_.stringWidget(); }
    TextInputField stringField() { return panel_.stringField(); }
    bool openRowMenuAt(glm::vec2 p) { return panel_.openRowMenuAt(p); }
    bool menuRowCanReset() const { return panel_.menuRowCanReset(); }
    bool menuRowCanPaste() const { return panel_.menuRowCanPaste(); }
    bool menuRowCanCopy()  const { return panel_.menuRowCanCopy(); }
    void resetMenuRow()          { panel_.resetMenuRow(); }
    std::string menuRowText() const { return panel_.menuRowText(); }
    bool pasteMenuRow(const std::string& text) { return panel_.pasteMenuRow(text); }

    // 시험용(--ui-ps-set): 지정한 키를 파일에 씁니다(실제 저장 경로와 동일).
    bool applyTestWrite(const std::string& key, const std::string& valueText);
    uint64_t diskWriteCount() const { return diskWrites_; }

private:
    std::filesystem::path projectIniPath() const;
    std::filesystem::path hprojectPath() const;
    std::string relativePath(const std::filesystem::path& p) const;

    bool writeIniKey(hps_schema::SettingsFile file, const std::string& key, const std::string& valueText);
    bool eraseIniKey(hps_schema::SettingsFile file, const std::string& key);

    std::vector<SettingsRowItem> buildItems();
    PropDesc makePropForKey(const hps_schema::SettingDef& def, const std::string& iniKey,
                            const std::string& label, const ProjectSettings& source,
                            std::function<void()>* outDragEnd = nullptr);

    static std::string xrStatusText(const hxrmode::XrRunStatus& s);

    Deps          deps_;
    SettingsPanel panel_;

    ProjectSettings   startupIni_;
    bool              hasStartupIni_ = false;
    ProjectSettings   currentIni_;
    ProjectSettings   androidIni_;
    std::map<std::string, bool> androidOverrides_;

    ProjectDescriptor startupDesc_;
    bool              hasStartupDesc_ = false;
    ProjectDescriptor currentDesc_;
    std::filesystem::path hprojectFile_;
    std::vector<std::string> labelPool_;
    uint64_t diskWrites_ = 0;
};
