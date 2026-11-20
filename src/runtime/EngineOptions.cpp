#include "EngineOptions.hpp"
#include "../core/ProjectSettings.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace {

// 앞뒤 공백 없는 양의 정수 전체여야 합니다.
bool parseUint(const std::string& s, uint32_t& out) {
    if (s.empty()) return false;
    uint32_t v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end != s.data() + s.size()) return false;
    out = v;
    return true;
}

// 실수는 strtof 로 읽습니다(Android NDK 의 libc++ 는 실수 from_chars 가 없는 판이 있음). 문자열 전체여야 합니다.
bool parsePositiveFloat(const std::string& s, float& out) {
    if (s.empty() || std::isspace((unsigned char)s[0])) return false;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v) || v <= 0.f) return false;
    out = v;
    return true;
}

// "WxH". 한 변은 16 ~ 8192 입니다.
bool parseSize(const std::string& s, uint32_t& w, uint32_t& h) {
    const size_t x = s.find('x');
    if (x == std::string::npos) return false;
    uint32_t a = 0, b = 0;
    if (!parseUint(s.substr(0, x), a) || !parseUint(s.substr(x + 1), b)) return false;
    if (a < 16 || b < 16 || a > 8192 || b > 8192) return false;
    w = a;
    h = b;
    return true;
}

// "--name=value" 이면 value 를 담고 true.
bool valueOf(const std::string& a, const char* name, std::string& value) {
    const std::string prefix = std::string(name) + "=";
    if (a.compare(0, prefix.size(), prefix) != 0) return false;
    value = a.substr(prefix.size());
    return true;
}

} // namespace

namespace hopt {

EngineOptions parse(const std::vector<std::string>& args, std::vector<std::string>& unknown) {
    EngineOptions o;
    for (const std::string& a : args) {
        std::string v;
        if      (a == "--xr")             o.xr            = true;
        else if (a == "--no-xr")          o.xr            = false;
        else if (a == "--emulate-stereo") o.emulateStereo = true;
        else if (a == "--loose")          o.loose         = true;
        else if (a == "--headless")       o.headless      = true;
        else if (a == "--synthetic-eyes") o.syntheticEyes = true;
        else if (a == "--vk-no-sync2")    o.vkNoSync2     = true;
        else if (a == "--vk-dedicated-msaa") o.vkDedicatedMsaa = true;
        else if (a == "--no-multiview")   o.multiview     = false;
        else if (a == "--ssao")           o.ssao          = true;
        else if (a == "--no-ssao")        o.ssao          = false;
        else if (a == "--no-auto-lod")    o.autoLod       = false;
        else if (a == "--hidden-window")  o.hiddenWindow  = true;
        else if (a == "--ui-log-categories") o.uiLogCategories = true;
        else if (a == "--ui-play")           o.uiPlay          = true;
        else if (a == "--ui-local-space")    o.uiLocalSpace    = true;
        else if (valueOf(a, "--window-size", v)) {
            if (!parseSize(v, o.windowW, o.windowH)) unknown.push_back(a);
        }
        else if (valueOf(a, "--write-root", v)) {
            if (!v.empty()) o.writeRoot = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-dump", v)) {
            if (!v.empty()) o.uiDumpPath = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-select", v)) {
            if (!v.empty()) o.uiSelect = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-open-menu", v)) {
            if (!v.empty()) o.uiOpenMenu = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-bottom-tab", v)) {
            if (!v.empty()) o.uiBottomTab = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-console-recall", v)) {
            uint32_t n = 0;
            if (parseUint(v, n) && n <= 64) o.uiConsoleRecall = (int)n;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-console", v)) {
            if (!v.empty()) o.uiConsole.push_back(v);
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-outliner-search", v)) {
            if (!v.empty()) o.uiOutlinerSearch = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-rename", v)) {
            if (!v.empty()) o.uiRename = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--debug-crash", v)) {
            static const char* const kKinds[] = { "segv", "stack", "purecall", "abort", "terminate", "throw", "check", "ensure" };
            bool known = false;
            for (const char* k : kKinds) known = known || v == k;
            if (known) o.debugCrash = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-asset-actions", v)) {
            if (!v.empty()) o.uiAssetActions = v;
            else unknown.push_back(a);
        }
        else if (a == "--ui-actor-picker") o.uiActorPicker = true;
        else if (valueOf(a, "--ui-move-asset", v)) {
            if (v.find('|') != std::string::npos) o.uiMoveAsset = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--culture", v)) {
            if (!v.empty()) o.culture = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-type", v)) {
            if (!v.empty()) o.uiType = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-ime-compose", v)) {
            if (!v.empty()) o.uiImeCompose = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-caret-left", v)) {
            o.uiCaretLeft = std::max(0, std::atoi(v.c_str()));
        }
        else if (valueOf(a, "--ui-select-left", v)) {
            o.uiSelectLeft = std::max(0, std::atoi(v.c_str()));
        }
        else if (valueOf(a, "--ui-outliner-collapse", v)) {
            if (!v.empty()) o.uiOutlinerCollapse = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-layout", v)) {
            if (!v.empty()) o.uiLayout = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--viewport-dump", v)) {
            if (!v.empty()) o.viewportDumpPath = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-gizmo-mode", v)) {
            if (!v.empty()) o.uiGizmoMode = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-pick", v)) {
            if (!v.empty()) o.uiPick = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--ui-hover", v)) {
            if (!v.empty()) o.uiHover = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--synthetic-eyes", v)) {
            if (parseSize(v, o.syntheticEyeW, o.syntheticEyeH)) o.syntheticEyes = true;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--frames", v)) {
            uint32_t n = 0;
            if (parseUint(v, n) && n > 0) o.frames = n;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--timeout", v)) {
            if (!parsePositiveFloat(v, o.timeoutSec)) unknown.push_back(a);
        }
        else if (valueOf(a, "--switch-scene", v)) {
            bool good = !v.empty();
            size_t start = 0;
            while (good && start <= v.size()) {
                const size_t comma = v.find(',', start);
                const std::string item = v.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                const size_t at = item.rfind('@');
                uint32_t frame = 0;
                if (at == std::string::npos || at == 0 || !parseUint(item.substr(at + 1), frame)) { good = false; break; }
                o.switchScenes.push_back({ item.substr(0, at), frame });
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            if (!good) { o.switchScenes.clear(); unknown.push_back(a); }
        }
        else if (a == "--switch-sync") o.switchSync = true;
        else if (valueOf(a, "--user-setting", v)) {
            const size_t eq = v.find('=');
            if (eq == std::string::npos || eq == 0) unknown.push_back(a);
            else o.userSettings.push_back({ v.substr(0, eq), v.substr(eq + 1) });
        }
        else if (valueOf(a, "--cvar", v)) {
            const size_t eq = v.find('=');
            if (eq == std::string::npos || eq == 0) unknown.push_back(a);
            else o.cvars.push_back({ v.substr(0, eq), v.substr(eq + 1) });
        }
        else if (valueOf(a, "--console-at", v)) {
            const size_t colon = v.find(':');
            uint32_t frame = 0;
            if (colon == std::string::npos || colon + 1 >= v.size() || !parseUint(v.substr(0, colon), frame)) unknown.push_back(a);
            else o.consoleAt.push_back({ frame, v.substr(colon + 1) });
        }
        else if (valueOf(a, "--user-setting-at", v)) {
            const size_t colon = v.find(':'), eq = v.find('=');
            uint32_t frame = 0;
            if (colon == std::string::npos || eq == std::string::npos || eq < colon + 2 || !parseUint(v.substr(0, colon), frame))
                unknown.push_back(a);
            else o.userSettingsAt.push_back({ frame, v.substr(colon + 1, eq - colon - 1), v.substr(eq + 1) });
        }
        else if (valueOf(a, "--fixed-dt", v)) {
            if (!parsePositiveFloat(v, o.fixedDt) || o.fixedDt > 0.05f) unknown.push_back(a);
        }
        else if (valueOf(a, "--capture", v)) {
            if (!v.empty()) o.capturePath = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--capture-eye", v)) {
            if (v == "left" || v == "right") o.captureEye = (v == "right") ? 1u : 0u;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--settings-platform", v)) {
            if (v == settings_platform::kAndroid || v == settings_platform::kWindows) o.settingsPlatform = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--gpu", v)) {
            uint32_t n = 0;
            if (parseUint(v, n) && n < 64) o.gpuIndex = (int)n;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--scene", v)) {
            if (project_settings::isSceneLogicalPath(v)) o.scenePath = v;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--project", v)) {
            if (!v.empty()) o.projectDir = v;
            else unknown.push_back(a);
        }
        else unknown.push_back(a);
    }
    return o;
}

} // namespace hopt
