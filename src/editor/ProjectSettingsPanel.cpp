#include "ProjectSettingsPanel.hpp"
#include "../core/FileUtil.hpp"
#include "../core/HLog.hpp"
#include "../core/ProjectPaths.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

static constinit HLogCategory LogProjectSettings{ "EditorScene", ELogVerbosity::Log };

namespace fs = std::filesystem;

void ProjectSettingsPanel::init(Deps deps) {
    deps_ = std::move(deps);
    reloadFiles();
    if (!hasStartupDesc_) {
        startupDesc_    = currentDesc_;
        hasStartupDesc_ = true;
    }

    SettingsPanel::Deps sd;
    sd.ui              = deps_.ui;
    sd.openMenu        = deps_.openMenu;
    sd.openCombo       = deps_.openCombo;
    sd.isComboOpen     = deps_.isComboOpen;
    sd.closeMenu       = deps_.closeMenu;
    sd.focusText       = deps_.focusText;
    sd.openAssetPicker = deps_.openAssetPicker;
    sd.browseToAsset   = deps_.browseToAsset;

    SettingsSource src;
    src.groups = {
        { "Project",   HTEXT("Editor.ProjectSettings", "Group.Project",   "Project") },
        { "Engine",    HTEXT("Editor.ProjectSettings", "Group.Engine",    "Engine") },
        { "Platforms", HTEXT("Editor.ProjectSettings", "Group.Platforms", "Platforms") },
    };
    src.categories = {
        { "Description", "Project",
          HTEXT("Editor.ProjectSettings", "Cat.Description", "Description"),
          HTEXT("Editor.ProjectSettings", "CatDesc.Description", "Project identity, company, and package ID."),
          [this] { return relativePath(hprojectPath()); }, {}, {} },
        { "Maps & Modes", "Project",
          HTEXT("Editor.ProjectSettings", "Cat.MapsModes", "Maps & Modes"),
          HTEXT("Editor.ProjectSettings", "CatDesc.MapsModes", "Default scenes loaded on startup."),
          [this] { return relativePath(projectIniPath()); }, {}, {} },
        { "Packaging", "Project",
          HTEXT("Editor.ProjectSettings", "Cat.Packaging", "Packaging"),
          HTEXT("Editor.ProjectSettings", "CatDesc.Packaging", "Asset packing list (edit Config/game.pack.txt directly)."),
          [this] { return "Config/game.pack.txt"; }, {},
          [this](SVerticalBox& host, float sc) {
              using VR = SVerticalBox;
              const fs::path cfgDir = hproj::root().empty() ? fs::path() : (hproj::root() / "Config");
              host.addSlot(
                  HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ 16.f * sc, 12.f * sc })
                      ->content(HNew(STextBlock)
                                    ->text(HTEXT("Editor.ProjectSettings", "Packaging.Info",
                                                 "Edit Config/game.pack.txt to configure map, exclude, and stored rules.").toString())
                                    ->fontSize(12.f * sc)->color(EditorUiKit::kTextBody)->hAlign(EHAlign::Left)),
                  VR::ESizeRule::Fixed, 40.f * sc);
              auto btn = deps_.ui->textButton(
                  HTEXT("Editor.ProjectSettings", "Packaging.OpenExplorer", "Open in Explorer").toString(),
                  12.f, EditorUiKit::kTextBody, { 10.f, 4.f }, EditorTheme::ctlNormal, EditorTheme::ctlHover,
                  [cfgDir] {
#ifdef _WIN32
                      if (!cfgDir.empty())
                          ShellExecuteW(nullptr, L"open", cfgDir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
                      (void)cfgDir;
#endif
                  });
              btn->hAlign(EHAlign::Left);
              host.addSlot(
                  HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ 16.f * sc, 0.f })->content(btn),
                  VR::ESizeRule::Fixed, 28.f * sc);
          } },
        { "General", "Engine",
          HTEXT("Editor.ProjectSettings", "Cat.General", "General"),
          HTEXT("Editor.ProjectSettings", "CatDesc.General", "Simulation tick rate and general runtime settings."),
          [this] { return relativePath(projectIniPath()); }, {}, {} },
        { "Rendering", "Engine",
          HTEXT("Editor.ProjectSettings", "Cat.Rendering", "Rendering"),
          HTEXT("Editor.ProjectSettings", "CatDesc.Rendering", "Post-processing, shadows, atmosphere, and per-platform overrides."),
          [this] { return relativePath(projectIniPath()); }, {}, {} },
        { "XR", "Platforms",
          HTEXT("Editor.ProjectSettings", "Cat.XR", "XR"),
          HTEXT("Editor.ProjectSettings", "CatDesc.XR", "OpenXR auto-detection and tracking origin."),
          [this] { return relativePath(projectIniPath()); },
          [this] { return deps_.xrStatus ? xrStatusText(deps_.xrStatus()) : std::string(); }, {} },
    };
    src.buildItems        = [this] { return buildItems(); };
    src.onReload          = [this] { reloadFiles(); };
    src.hasRestartChanges = [this] { return hasRestartChanges(); };

    panel_.init(std::move(sd), std::move(src));
}

std::shared_ptr<HWidget> ProjectSettingsPanel::build() {
    return panel_.build();
}

void ProjectSettingsPanel::setStartupProjectSettings(const ProjectSettings& ps) {
    startupIni_    = ps;
    hasStartupIni_ = true;
}

fs::path ProjectSettingsPanel::projectIniPath() const {
    return hproj::root().empty() ? fs::path() : (hproj::root() / "Config" / "project.ini");
}

fs::path ProjectSettingsPanel::hprojectPath() const {
    if (!hprojectFile_.empty()) return hprojectFile_;
    if (hproj::root().empty()) return {};
    return project_descriptor::findInDirectory(hproj::root());
}

std::string ProjectSettingsPanel::relativePath(const fs::path& p) const {
    if (p.empty() || hproj::root().empty()) return {};
    std::error_code ec;
    const fs::path rel = fs::relative(p, hproj::root(), ec);
    return ec ? p.filename().generic_string() : rel.generic_string();
}

void ProjectSettingsPanel::reloadFiles() {
    androidOverrides_.clear();
    const fs::path ini = projectIniPath();
    std::string raw;
    if (!ini.empty() && hfile::readConfigText(ini, raw)) {
        std::vector<std::string> msgs;
        currentIni_ = project_settings::parse(raw, msgs, "");
        androidIni_ = project_settings::parse(raw, msgs, settings_platform::kAndroid);

        std::istringstream in(raw);
        std::string line;
        while (std::getline(in, line)) {
            const std::string t = project_settings::trim(line);
            if (t.empty() || t[0] == '#') continue;
            const size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = project_settings::trim(t.substr(0, eq));
            std::string base;
            if (settings_platform::split(k, base) == settings_platform::kAndroid)
                androidOverrides_[base] = true;
        }
    }
    if (!hasStartupIni_) {
        startupIni_    = currentIni_;
        hasStartupIni_ = true;
    }

    hprojectFile_ = hprojectPath();
    if (!hprojectFile_.empty()) {
        std::vector<std::string> w, e;
        project_descriptor::loadFromFile(hprojectFile_, currentDesc_, w, e);
    }
}

bool ProjectSettingsPanel::hasRestartChanges() const {
    if (!hasStartupIni_) return false;
    static const ProjectDescriptor kEmpty{};
    return hps_schema::hasRestartChanges(currentIni_, startupIni_,
                                         hasStartupDesc_ ? currentDesc_ : kEmpty,
                                         hasStartupDesc_ ? startupDesc_ : kEmpty);
}

bool ProjectSettingsPanel::writeIniKey(hps_schema::SettingsFile file, const std::string& key, const std::string& valueText) {
    const fs::path path = (file == hps_schema::SettingsFile::HProject) ? hprojectPath() : projectIniPath();
    if (path.empty()) {
        HLOG(LogProjectSettings, Warning) << "Project Settings: no project file path";
        return false;
    }
    std::error_code ec;
    const bool exists = fs::exists(path, ec);
    if (ec) {
        HLOG(LogProjectSettings, Warning) << "Project Settings: cannot inspect " << path.generic_string() << ": " << ec.message();
        return false;
    }
    std::string text;
    std::string readWhy;
    const bool readOk = exists ? hfile::readConfigText(path, text, &readWhy) : false;
    if (project_settings::decideIniWrite(exists, readOk) == project_settings::IniWriteAction::AbortReadError) {
        HLOG(LogProjectSettings, Warning) << "Project Settings: failed to read " << path.generic_string()
                                          << (readWhy.empty() ? std::string() : ": " + readWhy)
                                          << "; keeping existing file";
        return false;
    }
    const std::string updated = project_settings::setIniValue(text, key, valueText);
    std::string err;
    if (!hfile::writeAtomic(path, updated, &err)) {
        HLOG(LogProjectSettings, Warning) << "Project Settings: failed to write " << path.generic_string() << ": " << err;
        return false;
    }
    ++diskWrites_;
    if (file == hps_schema::SettingsFile::ProjectIni) {
        std::vector<std::string> msgs;
        project_settings::parse(updated, msgs, settings_platform::native());
        for (const std::string& m : msgs)
            HLOG(LogProjectSettings, Warning) << "Project Settings: " << m;
        reloadFiles();
        std::string base;
        const std::string plat = settings_platform::split(key, base);
        if (plat.empty()) {
            const auto* def = hps_schema::find(file, base);
            if (def && def->apply == hps_schema::ApplyTiming::Live && deps_.onLiveSettingsChanged)
                deps_.onLiveSettingsChanged();
        }
    } else {
        std::vector<std::string> w, e;
        project_descriptor::parse(updated, w, e);
        for (const std::string& m : w) HLOG(LogProjectSettings, Warning) << "Project Settings: " << m;
        for (const std::string& m : e) HLOG(LogProjectSettings, Warning) << "Project Settings: " << m;
        reloadFiles();
    }
    return true;
}

bool ProjectSettingsPanel::eraseIniKey(hps_schema::SettingsFile file, const std::string& key) {
    const fs::path path = (file == hps_schema::SettingsFile::HProject) ? hprojectPath() : projectIniPath();
    if (path.empty()) return false;
    std::error_code ec;
    const bool exists = fs::exists(path, ec);
    if (ec || !exists) return false;
    std::string text;
    std::string readWhy;
    if (!hfile::readConfigText(path, text, &readWhy)) {
        HLOG(LogProjectSettings, Warning) << "Project Settings: failed to read " << path.generic_string()
                                          << (readWhy.empty() ? std::string() : ": " + readWhy);
        return false;
    }
    const std::string updated = project_settings::removeIniKey(text, key);
    std::string err;
    if (!hfile::writeAtomic(path, updated, &err)) {
        HLOG(LogProjectSettings, Warning) << "Project Settings: failed to write " << path.generic_string() << ": " << err;
        return false;
    }
    ++diskWrites_;
    reloadFiles();
    return true;
}

bool ProjectSettingsPanel::applyTestWrite(const std::string& key, const std::string& valueText) {
    const hps_schema::SettingsFile f = (key == "name" || key == "company" || key == "packageId")
                                     ? hps_schema::SettingsFile::HProject
                                     : hps_schema::SettingsFile::ProjectIni;
    return writeIniKey(f, key, valueText);
}

PropDesc ProjectSettingsPanel::makePropForKey(const hps_schema::SettingDef& def, const std::string& iniKey,
                                              const std::string& label, const ProjectSettings& /*src*/,
                                              std::function<void()>* outDragEnd) {
    const std::string k = iniKey;
    const hps_schema::SettingsFile file = def.file;
    const bool isAndroid = (iniKey != def.key);

    auto readPs = [this, isAndroid]() -> const ProjectSettings& { return isAndroid ? androidIni_ : currentIni_; };

    if (file == hps_schema::SettingsFile::HProject) {
        auto getS = [this, k]() -> std::string {
            if (k == "name")          return currentDesc_.name;
            if (k == "company")       return currentDesc_.company;
            if (k == "packageId")     return currentDesc_.packageId;
            if (k == "engineVersion") return currentDesc_.engineVersion;
            if (k == "engine")        return currentDesc_.engine;
            return {};
        };
        auto setS = [this, file, k](const std::string& v) { writeIniKey(file, k, v); };
        return makePropString(label.c_str(), getS, setS);
    }

    switch (def.kind) {
        case hps_schema::SettingKind::Toggle: {
            auto getB = [readPs, base = std::string(def.key)]() -> bool {
                const auto& p = readPs();
                if (base == "render.ao")                return p.renderAo;
                if (base == "render.bloom")             return p.renderBloom;
                if (base == "render.contactShadows")    return p.renderContactShadows;
                if (base == "render.heightFog")         return p.renderHeightFog;
                if (base == "render.reflectionCapture") return p.renderReflectionCapture;
                if (base == "xr.startInVr")             return p.xrStartInVr;
                return false;
            };
            auto setB = [this, file, k](bool v) { writeIniKey(file, k, v ? "true" : "false"); };
            return makePropToggle(label.c_str(), getB, setB).withDefault(def.defaultBool ? 1.0 : 0.0);
        }
        case hps_schema::SettingKind::Float:
        case hps_schema::SettingKind::Int: {
            const std::string base = def.key;
            auto getF = [readPs, base]() -> float {
                const auto& p = readPs();
                if (base == "sim.tickRate")          return p.simTickRate;
                if (base == "render.csmCascades")    return (float)p.renderCsmCascades;
                if (base == "render.shadowDistance") return p.renderShadowDistance;
                if (base == "render.lightLimit")     return (float)p.renderLightLimit;
                if (base == "xr.eyeHeight")          return p.xrEyeHeight;
                return 0.f;
            };
            // 끄는 동안에는 메모리 값만 바꾸고(Live 항목이면 화면에 바로 반영), 파일 쓰기는 끄기/입력이 끝날 때(onDragEnd) 한 번만 합니다.
            auto setF = [this, isAndroid, base, def](float v) {
                if (def.hasRange) v = (float)std::clamp((double)v, def.minVal, def.maxVal);
                ProjectSettings& dst = isAndroid ? androidIni_ : currentIni_;
                if (base == "sim.tickRate")               dst.simTickRate = v;
                else if (base == "render.csmCascades")    dst.renderCsmCascades = (int)std::lround((double)v);
                else if (base == "render.shadowDistance") dst.renderShadowDistance = v;
                else if (base == "render.lightLimit")     dst.renderLightLimit = (uint32_t)(std::max)(0L, std::lround((double)v));
                else if (base == "xr.eyeHeight")          dst.xrEyeHeight = v;
                if (!isAndroid && def.apply == hps_schema::ApplyTiming::Live && deps_.onLiveSettingsChanged)
                    deps_.onLiveSettingsChanged();
            };
            if (outDragEnd) {
                *outDragEnd = [this, file, k, def, getF] {
                    float v = getF();
                    if (def.hasRange) v = (float)std::clamp((double)v, def.minVal, def.maxVal);
                    if (def.kind == hps_schema::SettingKind::Int) {
                        const int iv = (int)std::lround((double)v);
                        writeIniKey(file, k, std::to_string(iv));
                    } else {
                        writeIniKey(file, k, project_settings::trimNumber(v));
                    }
                };
            }
            const PropUnit unit = (def.kind == hps_schema::SettingKind::Int) ? PropUnit::Integer : PropUnit::Scalar;
            return makePropFloat(label.c_str(), PropColor::Neutral, def.step, getF, setF, unit)
                .withDefault(def.defaultNumber);
        }
        case hps_schema::SettingKind::Enum: {
            auto getI = [readPs]() -> int { return readPs().xrTrackedHeight ? 0 : 1; };
            auto setI = [this, file, k, opts = def.enumOptions](int idx) {
                if (idx >= 0 && idx < (int)opts.size()) writeIniKey(file, k, opts[(size_t)idx]);
            };
            return makePropEnum(label.c_str(), def.enumOptions, getI, setI).withDefault(0.0);
        }
        case hps_schema::SettingKind::AssetPath: {
            auto getS = [readPs]() -> std::string { return readPs().startScene; };
            auto setS = [this, file, k](const std::string& v) { writeIniKey(file, k, v); };
            return makePropAssetPath(label.c_str(), def.assetType, getS, setS).withDefault(def.defaultString);
        }
        case hps_schema::SettingKind::String:
        default: {
            auto getS = [readPs]() -> std::string { return readPs().startScene; };
            auto setS = [this, file, k](const std::string& v) { writeIniKey(file, k, v); };
            return makePropString(label.c_str(), getS, setS).withDefault(def.defaultString);
        }
    }
}

std::vector<SettingsRowItem> ProjectSettingsPanel::buildItems() {
    std::vector<SettingsRowItem> out;
    labelPool_.clear();
    labelPool_.reserve(hps_schema::entries().size() * 2);

    for (const auto& def : hps_schema::entries()) {
        labelPool_.push_back(def.label.toString());
        const char* lblPtr = labelPool_.back().c_str();

        SettingsRowItem item;
        item.categoryId  = def.category;
        item.section     = def.section.toString();
        item.englishName = def.label.source();
        item.tooltip     = def.help.toString();
        item.readOnly    = def.readOnly;
        item.prop        = makePropForKey(def, def.key, lblPtr, currentIni_, &item.onDragEnd);
        item.prop.label  = lblPtr;

        if (def.allowPlatformOverride) {
            item.allowPlatformOverride  = true;
            item.platformOverrideActive = androidOverrides_.count(def.key) > 0;
            const std::string aKey = hps_schema::androidKey(def.key);
            labelPool_.push_back(HTEXT("Editor.ProjectSettings", "AndroidOverride", "Android").toString());
            const char* aLbl = labelPool_.back().c_str();
            item.platformOverrideProp = makePropForKey(def, aKey, aLbl, androidIni_, &item.onPlatformDragEnd);
            item.platformOverrideProp.label = aLbl;
            item.onAddPlatformOverride = [this, def, aKey] {
                const std::string defVal = hprop::valueText(makePropForKey(def, def.key, "", currentIni_));
                writeIniKey(def.file, aKey, defVal);
            };
            item.onRemovePlatformOverride = [this, def, aKey] { eraseIniKey(def.file, aKey); };
        }
        out.push_back(std::move(item));
    }
    return out;
}

std::string ProjectSettingsPanel::xrStatusText(const hxrmode::XrRunStatus& s) {
    switch (hxrmode::statusKind(s)) {
        case hxrmode::StatusKind::HeadsetFound:
            return HTEXT("Editor.Dialog", "ProjectSettings.Status.HeadsetFound",
                         "This run: VR ready (headset detected; Play opens VR)").toString();
        case hxrmode::StatusKind::NoHeadset:
            return HTEXT("Editor.Dialog", "ProjectSettings.Status.NoHeadset",
                         "This run: VR ready (no headset detected; Play opens desktop)").toString();
        case hxrmode::StatusKind::Ready:
            return HTEXT("Editor.Dialog", "ProjectSettings.Status.Ready",
                         "This run: VR ready").toString();
        case hxrmode::StatusKind::Unavailable:
            return HTEXT("Editor.Dialog", "ProjectSettings.Status.Unavailable",
                         "This run: VR unavailable (desktop only)").toString();
        case hxrmode::StatusKind::DisabledByNoXr:
            return HTEXT("Editor.Dialog", "ProjectSettings.Status.DisabledByNoXr",
                         "This run: disabled by --no-xr").toString();
        case hxrmode::StatusKind::AutomatedRun:
            return HTEXT("Editor.Dialog", "ProjectSettings.Status.AutomatedRun",
                         "This run: skipped for automated run").toString();
        case hxrmode::StatusKind::NotPrepared:
        default:
            return HTEXT("Editor.Dialog", "ProjectSettings.Status.NotPrepared",
                         "This run: VR not prepared").toString();
    }
}
