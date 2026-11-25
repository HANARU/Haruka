#pragma once
#include "SettingsPanel.hpp"
#include "../core/EditorPreferencesSchema.hpp"
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// ── Editor Preferences 창 (PS1d · PS-D12) ─────────────────────────────────────
// Project Settings 와 같은 SettingsPanel 틀을 쓰며, 이 기계의 Saved/Config/editor_state.txt 만 읽고 씁니다.
class EditorPreferencesPanel {
public:
    struct AudioDeviceChoice {
        std::string id;
        std::string label;
    };

    struct Deps {
        const EditorUiKit* ui = nullptr;
        std::function<void(glm::vec2, std::vector<EditorMenuEntry>)> openMenu;
        std::function<void(const std::shared_ptr<HWidget>&, std::vector<EditorMenuEntry>, int)> openCombo;
        std::function<bool(const HWidget*)> isComboOpen;
        std::function<void()>               closeMenu;
        std::function<void(const std::shared_ptr<HWidget>&)> focusText;

        std::function<heprefs::EditorStateData()> getState;
        std::function<void(const std::string&)>   setCulture;
        std::function<void(const std::string&)>   setGizmoSize;
        std::function<void(const std::string&)>   setAudioOutput;
        std::function<void(bool)>                 setAudioHeadsetInVr;
        std::function<void(const std::string&)>   setAudioHrtf;
        std::function<std::vector<AudioDeviceChoice>()> listAudioDevices;
        std::function<std::string()>              audioStatusText;
    };

    void init(Deps deps);
    std::shared_ptr<HWidget> build();

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
    bool menuRowCanCopy() const { return panel_.menuRowCanCopy(); }
    bool menuRowCanPaste() const { return panel_.menuRowCanPaste(); }
    std::string menuRowText() const { return panel_.menuRowText(); }
    void resetMenuRow() { panel_.resetMenuRow(); }
    bool pasteMenuRow(const std::string& text) { return panel_.pasteMenuRow(text); }

private:
    std::vector<SettingsRowItem> buildItems();

    Deps                     deps_;
    SettingsPanel            panel_;
    std::vector<std::string> labelPool_;
    std::deque<std::string>  enumPool_;
    std::vector<std::string> tooltipPool_;
};
