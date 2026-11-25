#pragma once
#include "HText.hpp"
#include "ProjectSettings.hpp"
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// ── 에디터 개인 설정 정본 표 (PS1d, 2026-11-24, Docs/Project_Settings_Design.md PS-D12) ────────────────────────
// 이 기계의 에디터 상태(Saved/Config/editor_state.txt)에 저장되는 개인 설정의 정본 표와 읽기/쓰기 순수 함수입니다.
// 프로젝트 파일(project.ini / .hproject)에는 쓰지 않습니다.
namespace heprefs {

enum class PrefFile : uint8_t {
    EditorState,   // Saved/Config/editor_state.txt (기계별)
};

enum class PrefKind : uint8_t {
    Toggle,
    Enum,
};

enum class ApplyTiming : uint8_t {
    Live,
    Restart,
};

inline constexpr const char* kStoragePath  = "Saved/Config/editor_state.txt";
inline constexpr const char* kStateRelPath = "Config/editor_state.txt";

inline constexpr float kGizmoSmall  = 0.08f;
inline constexpr float kGizmoMedium = 0.12f;
inline constexpr float kGizmoLarge  = 0.18f;

inline float gizmoFactorFromName(std::string_view name) {
    if (name == "small") return kGizmoSmall;
    if (name == "large") return kGizmoLarge;
    return kGizmoMedium;
}

inline const char* gizmoNameFromFactor(float f) {
    if (f < 0.10f) return "small";
    if (f < 0.15f) return "medium";
    return "large";
}

inline bool isValidGizmoSize(std::string_view name) {
    return name == "small" || name == "medium" || name == "large";
}

inline bool isValidHrtfName(std::string_view name) {
    return name == "auto" || name == "on" || name == "off";
}

struct SettingDef {
    PrefFile                 file = PrefFile::EditorState;
    const char*              key  = "";
    const char*              group    = "";   // General | Audio
    const char*              category = "";   // Region & Language | Viewport | Audio
    HText                    section;
    HText                    label;
    HText                    help;
    PrefKind                 kind = PrefKind::Enum;
    std::vector<const char*> enumOptions;     // 빈 목록이면 실행 중 동적 목록(언어 · 오디오 장치)
    bool                     defaultBool   = false;
    const char*              defaultString = "";
    ApplyTiming              apply         = ApplyTiming::Live;
};

inline const std::vector<SettingDef>& entries() {
    static const std::vector<SettingDef> kEntries = {
        // ── General > Region & Language ──
        { PrefFile::EditorState, "culture", "General", "Region & Language",
          HTEXT("Editor.Preferences", "Sec.Language", "Language"),
          HTEXT("Editor.Preferences", "Key.culture", "Editor Language"),
          HTEXT("Editor.Preferences", "Help.culture", "Display language for editor menus, panels, and dialogs."),
          PrefKind::Enum, {}, false, "en", ApplyTiming::Live },

        // ── General > Viewport ──
        { PrefFile::EditorState, "gizmoSize", "General", "Viewport",
          HTEXT("Editor.Preferences", "Sec.Gizmo", "Gizmo"),
          HTEXT("Editor.Preferences", "Key.gizmoSize", "Gizmo Size"),
          HTEXT("Editor.Preferences", "Help.gizmoSize", "Transform gizmo screen scale (Small, Medium, or Large)."),
          PrefKind::Enum, { "small", "medium", "large" }, false, "medium", ApplyTiming::Live },

        // ── Audio ──
        { PrefFile::EditorState, "audioOutput", "Audio", "Audio",
          HTEXT("Editor.Preferences", "Sec.Output", "Output"),
          HTEXT("Editor.Preferences", "Key.audioOutput", "Output Device"),
          HTEXT("Editor.Preferences", "Help.audioOutput", "Audio playback device used by the editor (empty = system default)."),
          PrefKind::Enum, {}, false, "", ApplyTiming::Live },

        { PrefFile::EditorState, "audioHeadsetInVr", "Audio", "Audio",
          HTEXT("Editor.Preferences", "Sec.VrAudio", "VR Audio"),
          HTEXT("Editor.Preferences", "Key.audioHeadsetInVr", "Headset Audio During VR Play"),
          HTEXT("Editor.Preferences", "Help.audioHeadsetInVr",
                "Route editor audio to the VR headset during VR Play (applies from the next Play)."),
          PrefKind::Toggle, {}, true, "1", ApplyTiming::Live },

        { PrefFile::EditorState, "audioHrtf", "Audio", "Audio",
          HTEXT("Editor.Preferences", "Sec.Spatialization", "Spatialization"),
          HTEXT("Editor.Preferences", "Key.audioHrtf", "3D Audio (HRTF)"),
          HTEXT("Editor.Preferences", "Help.audioHrtf",
                "Binaural HRTF mode: Auto (VR Play only), On (headphones), or Off (stereo panning)."),
          PrefKind::Enum, { "auto", "on", "off" }, false, "auto", ApplyTiming::Live },
    };
    return kEntries;
}

inline const char* storageFile(PrefFile f) {
    (void)f;
    return kStoragePath;
}

// ── Saved/Config/editor_state.txt 읽기 · 쓰기 ─────────────────────────────────
struct EditorStateData {
    std::string lastScene;
    std::string culture          = "en";
    std::string gizmoSize        = "medium";
    std::string audioOutput;
    bool        audioHeadsetInVr = true;
    std::string audioHrtf        = "auto";
};

inline EditorStateData parseEditorState(const std::string& text, std::vector<std::string>& warnings) {
    EditorStateData out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = project_settings::trim(line.substr(0, eq));
        const std::string val = project_settings::trim(line.substr(eq + 1));
        if (key == "lastScene") {
            out.lastScene = val;
        } else if (key == "culture") {
            if (!val.empty()) out.culture = val;
        } else if (key == "gizmoSize") {
            if (isValidGizmoSize(val)) out.gizmoSize = val;
            else warnings.push_back("Editor state: unknown gizmoSize '" + val + "' (small, medium, large); using medium");
        } else if (key == "audioOutput") {
            out.audioOutput = val;
        } else if (key == "audioHeadsetInVr") {
            out.audioHeadsetInVr = (val != "0");
        } else if (key == "audioHrtf") {
            if (isValidHrtfName(val)) out.audioHrtf = val;
            else warnings.push_back("Editor state: unknown audioHrtf '" + val + "' (auto, on, off); using auto");
        }
    }
    return out;
}

inline std::string formatEditorState(const EditorStateData& s) {
    std::string text = "# Haruka editor state for this machine (not part of the project).\n";
    if (!s.lastScene.empty()) text += "lastScene=" + s.lastScene + "\n";
    if (!s.culture.empty() && s.culture != "en") text += "culture=" + s.culture + "\n";
    if (isValidGizmoSize(s.gizmoSize) && s.gizmoSize != "medium")
        text += "gizmoSize=" + s.gizmoSize + "\n";
    if (!s.audioOutput.empty()) text += "audioOutput=" + s.audioOutput + "\n";
    text += std::string("audioHeadsetInVr=") + (s.audioHeadsetInVr ? "1" : "0") + "\n";
    text += "audioHrtf=" + (isValidHrtfName(s.audioHrtf) ? s.audioHrtf : std::string("auto")) + "\n";
    return text;
}

} // namespace heprefs
