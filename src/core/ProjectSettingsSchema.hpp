#pragma once
#include "HText.hpp"
#include "ProjectSettings.hpp"
#include "ProjectDescriptor.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ── 프로젝트 설정 정본 표 (PS1a, 2026-11-24, Docs/Project_Settings_Design.md PS-D3) ─────────────────────────────
// Project Settings 창이 그리는 모든 키의 정본 표입니다. 범위 · 기본값은 ProjectSettings::parse 와 같아야 하며,
// 단위 시험(tests/ProjectSettingsTest.cpp)이 두 쪽의 일치를 지킵니다.
namespace hps_schema {

enum class SettingsFile : uint8_t {
    ProjectIni,   // Config/project.ini
    HProject,     // <이름>.hproject
};

enum class SettingKind : uint8_t {
    Toggle,
    Float,
    Int,
    Enum,
    String,
    AssetPath,
};

enum class ApplyTiming : uint8_t {
    Live,      // 실행 중 바로 반영
    Restart,   // 에디터를 다시 시작해야 반영
};

struct SettingDef {
    SettingsFile             file = SettingsFile::ProjectIni;
    const char*              key  = "";
    const char*              group    = "";   // Project | Engine | Platforms
    const char*              category = "";   // Description | Maps & Modes | General | Rendering | XR
    HText                    section;
    HText                    label;
    HText                    help;
    SettingKind              kind = SettingKind::String;
    bool                     hasRange = false;
    double                   minVal   = 0.0;
    double                   maxVal   = 0.0;
    float                    step     = 1.f;
    std::vector<const char*> enumOptions;
    const char*              assetType = "";
    bool                     defaultBool   = false;
    double                   defaultNumber = 0.0;
    const char*              defaultString = "";
    ApplyTiming              apply         = ApplyTiming::Restart;
    bool                     allowPlatformOverride = false;
    bool                     readOnly              = false;
};

inline const std::vector<SettingDef>& entries() {
    static const std::vector<SettingDef> kEntries = {
        // ── Project > Description (.hproject) ──
        { SettingsFile::HProject, "name", "Project", "Description",
          HTEXT("Editor.ProjectSettings", "Sec.About", "About"),
          HTEXT("Editor.ProjectSettings", "Key.name", "Project Name"),
          HTEXT("Editor.ProjectSettings", "Help.name", "Display name of the project."),
          SettingKind::String, false, 0.0, 0.0, 1.f, {}, "", false, 0.0, "",
          ApplyTiming::Restart, false, false },
        { SettingsFile::HProject, "company", "Project", "Description",
          HTEXT("Editor.ProjectSettings", "Sec.About", "About"),
          HTEXT("Editor.ProjectSettings", "Key.company", "Company Name"),
          HTEXT("Editor.ProjectSettings", "Help.company", "Company or publisher name used for packaging and write folders."),
          SettingKind::String, false, 0.0, 0.0, 1.f, {}, "", false, 0.0, "",
          ApplyTiming::Restart, false, false },
        { SettingsFile::HProject, "packageId", "Project", "Description",
          HTEXT("Editor.ProjectSettings", "Sec.About", "About"),
          HTEXT("Editor.ProjectSettings", "Key.packageId", "Package ID"),
          HTEXT("Editor.ProjectSettings", "Help.packageId", "Android application package ID (e.g. com.company.app). Reconfigure Android after changing."),
          SettingKind::String, false, 0.0, 0.0, 1.f, {}, "", false, 0.0, "",
          ApplyTiming::Restart, false, false },
        { SettingsFile::HProject, "engineVersion", "Project", "Description",
          HTEXT("Editor.ProjectSettings", "Sec.Engine", "Engine"),
          HTEXT("Editor.ProjectSettings", "Key.engineVersion", "Engine Version"),
          HTEXT("Editor.ProjectSettings", "Help.engineVersion", "Minimum Haruka engine version recorded in the project file."),
          SettingKind::String, false, 0.0, 0.0, 1.f, {}, "", false, 0.0, "",
          ApplyTiming::Restart, false, true },
        { SettingsFile::HProject, "engine", "Project", "Description",
          HTEXT("Editor.ProjectSettings", "Sec.Engine", "Engine"),
          HTEXT("Editor.ProjectSettings", "Key.engine", "Engine Association"),
          HTEXT("Editor.ProjectSettings", "Help.engine", "Engine association constraint (e.g. haruka>=0.1)."),
          SettingKind::String, false, 0.0, 0.0, 1.f, {}, "", false, 0.0, "",
          ApplyTiming::Restart, false, true },

        // ── Project > Maps & Modes (project.ini) ──
        { SettingsFile::ProjectIni, "startScene", "Project", "Maps & Modes",
          HTEXT("Editor.ProjectSettings", "Sec.DefaultMaps", "Default Maps"),
          HTEXT("Editor.ProjectSettings", "Key.startScene", "Game Default Scene"),
          HTEXT("Editor.ProjectSettings", "Help.startScene", "Logical /Game/ path to the .hscene opened on startup."),
          SettingKind::AssetPath, false, 0.0, 0.0, 1.f, {}, "Scene", false, 0.0, "/Game/scenes/LightingProbe.hscene",
          ApplyTiming::Restart, false, false },

        // ── Engine > General (project.ini) ──
        { SettingsFile::ProjectIni, "sim.tickRate", "Engine", "General",
          HTEXT("Editor.ProjectSettings", "Sec.Simulation", "Simulation"),
          HTEXT("Editor.ProjectSettings", "Key.sim.tickRate", "Simulation Tick Rate (Hz)"),
          HTEXT("Editor.ProjectSettings", "Help.sim.tickRate", "Fixed simulation step frequency in Hz (72 to 240)."),
          SettingKind::Float, true, 72.0, 240.0, 1.f, {}, "", false, 72.0, "",
          ApplyTiming::Restart, false, false },

        // ── Engine > Rendering (project.ini) ──
        { SettingsFile::ProjectIni, "render.ao", "Engine", "Rendering",
          HTEXT("Editor.ProjectSettings", "Sec.PostProcess", "Post Processing"),
          HTEXT("Editor.ProjectSettings", "Key.render.ao", "Screen-Space Ambient Occlusion"),
          HTEXT("Editor.ProjectSettings", "Help.render.ao", "Enable screen-space ambient occlusion pass (can be overridden per platform)."),
          SettingKind::Toggle, false, 0.0, 0.0, 1.f, {}, "", true, 0.0, "",
          ApplyTiming::Live, true, false },
        { SettingsFile::ProjectIni, "render.bloom", "Engine", "Rendering",
          HTEXT("Editor.ProjectSettings", "Sec.PostProcess", "Post Processing"),
          HTEXT("Editor.ProjectSettings", "Key.render.bloom", "Bloom"),
          HTEXT("Editor.ProjectSettings", "Help.render.bloom", "Enable bloom post-process pass around bright areas."),
          SettingKind::Toggle, false, 0.0, 0.0, 1.f, {}, "", true, 0.0, "",
          ApplyTiming::Live, true, false },
        { SettingsFile::ProjectIni, "render.csmCascades", "Engine", "Rendering",
          HTEXT("Editor.ProjectSettings", "Sec.Shadows", "Shadows"),
          HTEXT("Editor.ProjectSettings", "Key.render.csmCascades", "Shadow Cascades"),
          HTEXT("Editor.ProjectSettings", "Help.render.csmCascades", "Number of directional shadow map cascades (1 or 2)."),
          SettingKind::Int, true, 1.0, 2.0, 1.f, {}, "", false, 2.0, "",
          ApplyTiming::Restart, true, false },
        { SettingsFile::ProjectIni, "render.shadowDistance", "Engine", "Rendering",
          HTEXT("Editor.ProjectSettings", "Sec.Shadows", "Shadows"),
          HTEXT("Editor.ProjectSettings", "Key.render.shadowDistance", "Shadow Distance (m)"),
          HTEXT("Editor.ProjectSettings", "Help.render.shadowDistance", "Directional shadow coverage radius around the camera in meters (8 to 200)."),
          SettingKind::Float, true, 8.0, 200.0, 0.5f, {}, "", false, 20.0, "",
          ApplyTiming::Restart, true, false },
        { SettingsFile::ProjectIni, "render.contactShadows", "Engine", "Rendering",
          HTEXT("Editor.ProjectSettings", "Sec.Shadows", "Shadows"),
          HTEXT("Editor.ProjectSettings", "Key.render.contactShadows", "Contact Shadows"),
          HTEXT("Editor.ProjectSettings", "Help.render.contactShadows", "Enable screen-space contact shadows on lights."),
          SettingKind::Toggle, false, 0.0, 0.0, 1.f, {}, "", true, 0.0, "",
          ApplyTiming::Restart, true, false },
        { SettingsFile::ProjectIni, "render.heightFog", "Engine", "Rendering",
          HTEXT("Editor.ProjectSettings", "Sec.Atmosphere", "Atmosphere & Reflections"),
          HTEXT("Editor.ProjectSettings", "Key.render.heightFog", "Height Fog"),
          HTEXT("Editor.ProjectSettings", "Help.render.heightFog", "Enable exponential height fog rendering."),
          SettingKind::Toggle, false, 0.0, 0.0, 1.f, {}, "", true, 0.0, "",
          ApplyTiming::Restart, true, false },
        { SettingsFile::ProjectIni, "render.reflectionCapture", "Engine", "Rendering",
          HTEXT("Editor.ProjectSettings", "Sec.Atmosphere", "Atmosphere & Reflections"),
          HTEXT("Editor.ProjectSettings", "Key.render.reflectionCapture", "Reflection Captures"),
          HTEXT("Editor.ProjectSettings", "Help.render.reflectionCapture", "Use ReflectionCapture components (off uses sky IBL only)."),
          SettingKind::Toggle, false, 0.0, 0.0, 1.f, {}, "", true, 0.0, "",
          ApplyTiming::Restart, true, false },
        { SettingsFile::ProjectIni, "render.lightLimit", "Engine", "Rendering",
          HTEXT("Editor.ProjectSettings", "Sec.Lighting", "Lighting"),
          HTEXT("Editor.ProjectSettings", "Key.render.lightLimit", "Per-Frame Light Limit"),
          HTEXT("Editor.ProjectSettings", "Help.render.lightLimit", "Max lights uploaded per frame (0 uses Config/engine.ini render.maxLights; max 4096)."),
          SettingKind::Int, true, 0.0, 4096.0, 1.f, {}, "", false, 0.0, "",
          ApplyTiming::Restart, true, false },

        // ── Platforms > XR (project.ini) ──
        { SettingsFile::ProjectIni, "xr.startInVr", "Platforms", "XR",
          HTEXT("Editor.ProjectSettings", "Sec.XrStartup", "Startup"),
          HTEXT("Editor.ProjectSettings", "Key.xr.startInVr", "VR Auto-Detect"),
          HTEXT("Editor.ProjectSettings", "Help.xr.startInVr", "Prepare OpenXR on startup; Play opens VR when a headset is found."),
          SettingKind::Toggle, false, 0.0, 0.0, 1.f, {}, "", false, 0.0, "",
          ApplyTiming::Restart, false, false },
        { SettingsFile::ProjectIni, "xr.height", "Platforms", "XR",
          HTEXT("Editor.ProjectSettings", "Sec.XrRig", "Tracking Origin"),
          HTEXT("Editor.ProjectSettings", "Key.xr.height", "XR Height Mode"),
          HTEXT("Editor.ProjectSettings", "Help.xr.height", "tracked uses headset floor space; fixed places eyes at XR Eye Height."),
          SettingKind::Enum, false, 0.0, 0.0, 1.f, { "tracked", "fixed" }, "", false, 0.0, "tracked",
          ApplyTiming::Live, false, false },
        { SettingsFile::ProjectIni, "xr.eyeHeight", "Platforms", "XR",
          HTEXT("Editor.ProjectSettings", "Sec.XrRig", "Tracking Origin"),
          HTEXT("Editor.ProjectSettings", "Key.xr.eyeHeight", "XR Eye Height (m)"),
          HTEXT("Editor.ProjectSettings", "Help.xr.eyeHeight", "Floor-to-eye height in meters when XR Height Mode is fixed (0.5 to 2.5)."),
          SettingKind::Float, true, xr_rig::kMinEyeHeight, xr_rig::kMaxEyeHeight, 0.05f, {}, "", false, xr_rig::kDefaultEyeHeight, "",
          ApplyTiming::Live, false, false },
        { SettingsFile::ProjectIni, "debug.vrStatsButton", "Platforms", "XR",
          HTEXT("Editor.ProjectSettings", "Sec.XrDebug", "In-Headset Debug"),
          HTEXT("Editor.ProjectSettings", "Key.debug.vrStatsButton", "VR Stats Overlay Button"),
          HTEXT("Editor.ProjectSettings", "Help.debug.vrStatsButton", "Left-hand controller button that toggles the in-headset performance overlay (x, y, or none)."),
          SettingKind::Enum, false, 0.0, 0.0, 1.f, { "x", "y", "none" }, "", false, 0.0, "x",
          ApplyTiming::Live, false, false },
    };
    return kEntries;
}

inline const SettingDef* find(SettingsFile file, std::string_view key) {
    for (const SettingDef& d : entries())
        if (d.file == file && d.key == key) return &d;
    return nullptr;
}

// ProjectSettings::parse 가 해석하는 모든 키(PS2·PS3 에서 다룰 input.* · physics.* 접두사 제외).
inline const std::vector<const char*>& parseKnownScalarKeys() {
    static const std::vector<const char*> kKeys = {
        "startScene", "sim.tickRate",
        "render.ao", "render.bloom", "render.csmCascades", "render.shadowDistance",
        "render.contactShadows", "render.heightFog", "render.reflectionCapture", "render.lightLimit",
        "xr.startInVr", "xr.height", "xr.eyeHeight", "debug.vrStatsButton",
    };
    return kKeys;
}

inline std::string androidKey(std::string_view key) {
    return std::string(key) + ".android";
}

inline bool hasRestartChanges(const ProjectSettings& a, const ProjectSettings& b,
                              const ProjectDescriptor& da, const ProjectDescriptor& db) {
    if (a.startScene              != b.startScene)              return true;
    if (std::fabs(a.simTickRate - b.simTickRate) > 1e-3f)     return true;
    if (a.renderCsmCascades       != b.renderCsmCascades)       return true;
    if (std::fabs(a.renderShadowDistance - b.renderShadowDistance) > 1e-3f) return true;
    if (a.renderContactShadows    != b.renderContactShadows)    return true;
    if (a.renderHeightFog         != b.renderHeightFog)         return true;
    if (a.renderReflectionCapture != b.renderReflectionCapture) return true;
    if (a.renderLightLimit        != b.renderLightLimit)        return true;
    if (a.xrStartInVr             != b.xrStartInVr)             return true;
    if (da.name      != db.name)      return true;
    if (da.company   != db.company)   return true;
    if (da.packageId != db.packageId) return true;
    return false;
}

} // namespace hps_schema
