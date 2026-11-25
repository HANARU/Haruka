#include "EditorPreferencesPanel.hpp"
#include "../core/TextUtf.hpp"
#include "../core/Utf8.hpp"

void EditorPreferencesPanel::init(Deps deps) {
    deps_ = std::move(deps);

    SettingsPanel::Deps sd;
    sd.ui          = deps_.ui;
    sd.openMenu    = deps_.openMenu;
    sd.openCombo   = deps_.openCombo;
    sd.isComboOpen = deps_.isComboOpen;
    sd.closeMenu   = deps_.closeMenu;
    sd.focusText   = deps_.focusText;

    SettingsSource src;
    src.groups = {
        { "General", HTEXT("Editor.Preferences", "Group.General", "General") },
        { "Audio",   HTEXT("Editor.Preferences", "Group.Audio",   "Audio") },
    };
    src.categories = {
        { "Region & Language", "General",
          HTEXT("Editor.Preferences", "Cat.RegionLanguage", "Region & Language"),
          HTEXT("Editor.Preferences", "CatDesc.RegionLanguage", "Editor display language."),
          [] { return std::string(heprefs::kStoragePath); }, {}, {} },
        { "Viewport", "General",
          HTEXT("Editor.Preferences", "Cat.Viewport", "Viewport"),
          HTEXT("Editor.Preferences", "CatDesc.Viewport", "Viewport gizmo and interaction preferences."),
          [] { return std::string(heprefs::kStoragePath); }, {}, {} },
        { "Audio", "Audio",
          HTEXT("Editor.Preferences", "Cat.Audio", "Audio"),
          HTEXT("Editor.Preferences", "CatDesc.Audio", "Editor audio output device, VR headset routing, and 3D spatialization."),
          [] { return std::string(heprefs::kStoragePath); },
          [this] { return deps_.audioStatusText ? deps_.audioStatusText() : std::string(); }, {} },
    };
    src.buildItems = [this] { return buildItems(); };

    panel_.init(std::move(sd), std::move(src));
}

std::shared_ptr<HWidget> EditorPreferencesPanel::build() {
    return panel_.build();
}

std::vector<SettingsRowItem> EditorPreferencesPanel::buildItems() {
    std::vector<SettingsRowItem> out;
    labelPool_.clear();
    enumPool_.clear();
    tooltipPool_.clear();

    const auto& defs = heprefs::entries();
    labelPool_.reserve(defs.size());
    tooltipPool_.reserve(defs.size());

    auto poolStr = [this](std::string s) -> const char* {
        enumPool_.push_back(std::move(s));
        return enumPool_.back().c_str();
    };

    const auto cultures = htext::knownCultures();
    const std::vector<AudioDeviceChoice> devices = deps_.listAudioDevices
                                                 ? deps_.listAudioDevices()
                                                 : std::vector<AudioDeviceChoice>{};

    for (const heprefs::SettingDef& def : defs) {
        labelPool_.push_back(def.label.toString());
        const std::string& label = labelPool_.back();
        tooltipPool_.push_back(def.help.toString());
        const std::string& tip = tooltipPool_.back();

        SettingsRowItem item;
        item.categoryId  = def.category;
        item.section     = def.section.toString();
        item.englishName = def.label.source();
        item.tooltip     = tip;

        const std::string key = def.key;
        if (key == "culture") {
            std::vector<std::string> codes;
            std::vector<const char*> opts;
            for (const htext::CultureInfo& c : cultures) {
                codes.push_back(c.code);
                opts.push_back(poolStr(c.nativeName));
            }
            auto getI = [codes]() -> int {
                const std::string cur = htext::culture();
                for (size_t i = 0; i < codes.size(); ++i)
                    if (codes[i] == cur) return static_cast<int>(i);
                return 0;
            };
            auto setI = [this, codes](int idx) {
                if (idx >= 0 && idx < static_cast<int>(codes.size()) && deps_.setCulture)
                    deps_.setCulture(codes[static_cast<size_t>(idx)]);
            };
            item.prop = makePropEnum(label.c_str(), std::move(opts), getI, setI).withDefault(0.0);
        } else if (key == "gizmoSize") {
            std::vector<const char*> opts = {
                poolStr(HTEXT("Editor.Preferences", "GizmoSize.Small",  "Small").toString()),
                poolStr(HTEXT("Editor.Preferences", "GizmoSize.Medium", "Medium").toString()),
                poolStr(HTEXT("Editor.Preferences", "GizmoSize.Large",  "Large").toString()),
            };
            const std::vector<std::string> vals = { "small", "medium", "large" };
            auto getI = [this, vals]() -> int {
                const std::string cur = deps_.getState ? deps_.getState().gizmoSize : std::string("medium");
                for (size_t i = 0; i < vals.size(); ++i)
                    if (vals[i] == cur) return static_cast<int>(i);
                return 1;
            };
            auto setI = [this, vals](int idx) {
                if (idx >= 0 && idx < static_cast<int>(vals.size()) && deps_.setGizmoSize)
                    deps_.setGizmoSize(vals[static_cast<size_t>(idx)]);
            };
            item.prop = makePropEnum(label.c_str(), std::move(opts), getI, setI).withDefault(1.0);
        } else if (key == "audioOutput") {
            std::vector<std::string> ids = { "" };
            std::vector<const char*> opts = {
                poolStr(HTEXT("Editor.Preferences", "Audio.SystemDefault", "System default").toString()),
            };
            for (const AudioDeviceChoice& d : devices) {
                ids.push_back(d.id);
                opts.push_back(poolStr(d.label));
            }
            auto getI = [this, ids]() -> int {
                const std::string cur = deps_.getState ? deps_.getState().audioOutput : std::string();
                if (cur.empty()) return 0;
                for (size_t i = 1; i < ids.size(); ++i)
                    if (hutf::equalsIgnoreCase(ids[i], cur)) return static_cast<int>(i);
                return 0;
            };
            auto setI = [this, ids](int idx) {
                if (idx >= 0 && idx < static_cast<int>(ids.size()) && deps_.setAudioOutput)
                    deps_.setAudioOutput(ids[static_cast<size_t>(idx)]);
            };
            item.prop = makePropEnum(label.c_str(), std::move(opts), getI, setI).withDefault(0.0);
        } else if (key == "audioHeadsetInVr") {
            auto getB = [this]() -> bool {
                return deps_.getState ? deps_.getState().audioHeadsetInVr : true;
            };
            auto setB = [this](bool v) {
                if (deps_.setAudioHeadsetInVr) deps_.setAudioHeadsetInVr(v);
            };
            item.prop = makePropToggle(label.c_str(), getB, setB).withDefault(def.defaultBool ? 1.0 : 0.0);
        } else if (key == "audioHrtf") {
            std::vector<const char*> opts = {
                poolStr(HTEXT("Editor.Preferences", "Hrtf.Auto", "Auto (VR Play only)").toString()),
                poolStr(HTEXT("Editor.Preferences", "Hrtf.On",   "On (headphones)").toString()),
                poolStr(HTEXT("Editor.Preferences", "Hrtf.Off",  "Off (stereo panning)").toString()),
            };
            const std::vector<std::string> vals = { "auto", "on", "off" };
            auto getI = [this, vals]() -> int {
                const std::string cur = deps_.getState ? deps_.getState().audioHrtf : std::string("auto");
                for (size_t i = 0; i < vals.size(); ++i)
                    if (vals[i] == cur) return static_cast<int>(i);
                return 0;
            };
            auto setI = [this, vals](int idx) {
                if (idx >= 0 && idx < static_cast<int>(vals.size()) && deps_.setAudioHrtf)
                    deps_.setAudioHrtf(vals[static_cast<size_t>(idx)]);
            };
            item.prop = makePropEnum(label.c_str(), std::move(opts), getI, setI).withDefault(0.0);
        }

        out.push_back(std::move(item));
    }
    return out;
}
