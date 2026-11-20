#pragma once
#include "../vfs/LogicalPath.hpp"
#include "Utf8.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

// ── 프로젝트 설정(assets/project.ini) ────────────────────────────────────────────
// 프로젝트에 속한 설정입니다(2026-10-15). 시작 씬처럼 저장소와 함께 버전 관리되어야 하는 값이라, 빌드마다
// 실행 폴더에 새로 생기는 Config/engine.ini(기계별 한도) 대신 프로젝트 폴더의 assets/ 아래에 둡니다.
// Godot 이 res:// 루트의 project.godot 에 시작 씬과 입력 맵을 함께 두는 것과 같은 자리입니다. 에디터
// 빌드는 hproj 로 소스 트리의 파일을, 배포된 실행 파일은 실행 폴더로 복사된 파일을 읽습니다.
//
// 형식은 engine.ini 와 같은 "이름 = 값" 줄입니다. '#' 로 시작하는 줄은 주석입니다.
struct ProjectSettings {
    static constexpr const char* kPath = "/Config/project.ini";

    // 실행하면 처음 여는 씬(논리 경로). 에디터는 마지막으로 연 씬이 있으면 그것을 먼저 엽니다.
    std::string startScene = "/Game/scenes/LightingProbe.hscene";
    // input. 으로 시작하는 줄(입력 바인딩)은 입력 시스템이 따로 해석합니다. 순서를 지켜 넘깁니다.
    std::vector<std::pair<std::string, std::string>> input;
    // physics. 으로 시작하는 줄(충돌 레이어 이름 · 무시 쌍, 2026-11-20)은 물리 레이어 표(physics/PhysicsLayers.hpp)가 해석합니다.
    std::vector<std::pair<std::string, std::string>> physics;
    // XR 리그 높이(2026-10-16). 리그가 없는 씬에서 엔진이 붙이는 보이지 않는 리그가 씁니다. 씬에 둔 리그는
    // 자기 컴포넌트 값을 씁니다.
    //   xr.height    = tracked | fixed   tracked: OpenXR 바닥 기준 공간으로 실제 키, fixed: 아래 눈높이로 고정
    //   xr.eyeHeight = 1.7               fixed 일 때(또는 런타임에 바닥 공간이 없을 때) 바닥에서 눈까지(m)
    bool  xrTrackedHeight = true;
    float xrEyeHeight     = 1.7f;
    // 게임 실행 파일을 VR 로 시작할지(2026-10-19 사용자 결정, UE 의 Start in VR · Unity 의 Initialize XR on Startup 자리).
    //   xr.startInVr = true | false   명령줄 --xr · --no-xr 이 이 값을 덮어씁니다. 에디터는 이 값을 보지 않고 --xr 로만 켭니다.
    bool  xrStartInVr     = false;
    // 후처리 패스 켬/끔(2026-11-01).
    //   render.ao    = true | false   화면 공간 AO(SSAO). 머티리얼 occlusion 텍스처는 그대로입니다.
    //   render.bloom = true | false   블룸.
    // 켜면 지금 화면, 끄면 그 패스를 기록하지 않습니다(GPU 시간을 아낌). 시작할 때 한 번 읽습니다. 명령줄 --ssao · --no-ssao 가
    // render.ao 를 덮어씁니다.
    bool  renderAo        = true;
    bool  renderBloom     = true;
    // 품질 단계(2026-11-18, 성능 안정화 2-2). 기본값은 PC 화면 그대로이고, Quest 값은 아래 플랫폼 접미사로 적습니다.
    //   render.csmCascades       = 1 | 2          방향광 그림자 캐스케이드(1 = 먼 단만, 섀도 패스 하나)
    //   render.shadowDistance    = 8 ~ 200        그림자 거리(m, 머리 중심 구의 반지름)
    //   render.contactShadows    = true | false   라이트의 Contact Shadow Length 를 쓸지(끄면 AO 패스도 덜 돎)
    //   render.heightFog         = true | false   HeightFogComponent 를 쓸지
    //   render.reflectionCapture = true | false   ReflectionCaptureComponent 를 쓸지(끄면 하늘 IBL 만)
    //   render.lightLimit        = 0 ~ 4096       프레임마다 올릴 라이트 수(0 = Config/engine.ini 의 render.maxLights 그대로)
    int      renderCsmCascades       = 2;
    float    renderShadowDistance    = 20.f;
    bool     renderContactShadows    = true;
    bool     renderHeightFog         = true;
    bool     renderReflectionCapture = true;
    uint32_t renderLightLimit        = 0;
    // 플랫폼 접미사(2026-11-18): "이름.android = 값" 은 Android(Quest)에서만 같은 이름의 값을 덮어씁니다(줄 순서와 무관).
    // UE 의 플랫폼 ini 가 Default ini 를 덮는 것과 같은 자리입니다. 다른 플랫폼의 줄은 조용히 건너뜁니다. 입력 바인딩(input.*)에는
    // 붙일 수 없습니다. PC 에서 Quest 값을 시험하려면 명령줄 --settings-platform=android.
};

// 플랫폼 접미사에 쓰는 이름.
namespace settings_platform {
inline constexpr const char* kAndroid = "android";
inline constexpr const char* kWindows = "windows";
// 이 빌드가 도는 플랫폼.
inline const char* native() {
#if defined(__ANDROID__)
    return kAndroid;
#else
    return kWindows;
#endif
}
// key 가 ".android" · ".windows" 로 끝나면 그 플랫폼 이름을 돌려주고 base 에 접미사 앞을 담습니다. 아니면 "".
inline std::string split(const std::string& key, std::string& base) {
    for (const char* p : { kAndroid, kWindows }) {
        const std::string suffix = std::string(".") + p;
        if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
            base = key.substr(0, key.size() - suffix.size());
            return p;
        }
    }
    base = key;
    return {};
}
} // namespace settings_platform

// 리그 눈높이의 허용 범위(m). 프로젝트 설정과 XR Rig 컴포넌트가 함께 씁니다.
namespace xr_rig {
constexpr float kMinEyeHeight     = 0.5f;
constexpr float kMaxEyeHeight     = 2.5f;
constexpr float kDefaultEyeHeight = 1.7f;
} // namespace xr_rig

namespace project_settings {

// 메시지용 숫자(소수점 아래 0 은 뺌).
inline std::string trimNumber(float v) {
    std::string t = std::to_string(v);
    t.erase(t.find_last_not_of('0') + 1);
    if (!t.empty() && t.back() == '.') t.pop_back();
    return t;
}

inline std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// 저장 대화상자의 씬 이름(글자 체계 T1-11, 2026-11-19). 이름은 UTF-8 입니다. ASCII 는 예전 규칙(영문 · 숫자 · 공백 · - _ /), 그 밖의
// 인쇄 가능한 글자(한글 · 한자 등)는 받습니다. 경로는 UTF-8 경로 도우미 · 매니페스트로 Windows 에서도 그대로 왕복합니다
// (DEF-121 · 123). 앞뒤 공백을 떼고, 조합형 한글(macOS)은 완성형으로 합치고, 끝의 .hscene 은 뗍니다. 받을 수 없으면 거짓.
inline bool normalizeSceneName(const std::string& typed, std::string& out) {
    std::string name = utf8::composeHangul(trim(typed));
    if (name.size() > 7 && name.compare(name.size() - 7, 7, ".hscene") == 0) name.resize(name.size() - 7);
    if (name.empty() || !utf8::isValid(name) || name.front() == '/' || name.back() == '/' ||
        name.find("//") != std::string::npos)
        return false;
    for (size_t i = 0; i < name.size();) {
        const uint32_t cp = utf8::next(name, i);
        const bool ok = cp < 0x80 ? ((cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
                                     cp == ' ' || cp == '-' || cp == '_' || cp == '/')
                                  : utf8::isPrintable(cp);
        if (!ok) return false;
    }
    out = std::move(name);
    return true;
}

// 논리 경로 규칙: assets/ 또는 /Game/ 아래의 .hscene 이어야 합니다. 씬 파일은 그 밖에 두지 않습니다.
inline bool isSceneLogicalPath(const std::string& p) {
    if (p.find("..") != std::string::npos || p.find('\\') != std::string::npos) return false;
    const std::string canon = hvfs::toCanonical(p);
    if (!hvfs::isGamePath(canon)) return false;
    const std::string_view rel = hvfs::gameRelative(canon);
    constexpr std::string_view ext = ".hscene";
    return rel.size() > ext.size() &&
           rel.compare(rel.size() - ext.size(), ext.size(), ext) == 0;
}

// platform 은 접미사 줄을 적용할 플랫폼(settings_platform::kAndroid 등)입니다. 비우면 접미사 줄은 모두 건너뜁니다.
inline ProjectSettings parse(const std::string& text, std::vector<std::string>& messages, const std::string& platform = {}) {
    ProjectSettings s;
    std::vector<std::pair<std::string, std::string>> platformLines;   // 이 플랫폼의 접미사 줄(접미사를 뗀 이름, 값)
    auto readBool = [&](const std::string& key, const std::string& value, bool& dst, bool fallback) {
        if      (value == "true")  dst = true;
        else if (value == "false") dst = false;
        else { dst = fallback; messages.push_back("'" + key + "' must be true or false (got '" + value + "'); using "
                                                  + (fallback ? "true" : "false")); }
    };
    auto readNumber = [&](const std::string& key, const std::string& value, float lo, float hi, float& out) {
        char* endp = nullptr;
        const float v = std::strtof(value.c_str(), &endp);
        if (value.empty() || !endp || *endp != '\0' || !(v >= lo && v <= hi)) {
            messages.push_back("'" + key + "' must be a number from " + trimNumber(lo) + " to " + trimNumber(hi)
                               + " (got '" + value + "'); using " + trimNumber(out));
            return false;
        }
        out = v;
        return true;
    };
    // 이름 하나를 적용합니다(접미사를 뗀 이름).
    auto apply = [&](const std::string& key, const std::string& value) {
        if (key == "startScene") {
            if (isSceneLogicalPath(value)) s.startScene = value;
            else messages.push_back("'startScene' must be a .hscene path under /Game/ (got '" + value + "'); using " + s.startScene);
        } else if (key == "xr.height") {
            if      (value == "tracked") s.xrTrackedHeight = true;
            else if (value == "fixed")   s.xrTrackedHeight = false;
            else messages.push_back("'xr.height' must be tracked or fixed (got '" + value + "'); using tracked");
        } else if (key == "xr.eyeHeight") {
            char* endp = nullptr;
            const float v = std::strtof(value.c_str(), &endp);
            if (value.empty() || !endp || *endp != '\0' || !(v >= xr_rig::kMinEyeHeight && v <= xr_rig::kMaxEyeHeight))
                messages.push_back("'xr.eyeHeight' must be a number from 0.5 to 2.5 (got '" + value + "'); using 1.7");
            else
                s.xrEyeHeight = v;
        } else if (key == "xr.startInVr") {
            if      (value == "true")  s.xrStartInVr = true;
            else if (value == "false") s.xrStartInVr = false;
            else messages.push_back("'xr.startInVr' must be true or false (got '" + value + "'); using false");
        } else if (key == "render.ao" || key == "render.bloom") {
            bool& dst = (key == "render.ao") ? s.renderAo : s.renderBloom;
            readBool(key, value, dst, true);
        } else if (key == "render.contactShadows") {
            readBool(key, value, s.renderContactShadows, true);
        } else if (key == "render.heightFog") {
            readBool(key, value, s.renderHeightFog, true);
        } else if (key == "render.reflectionCapture") {
            readBool(key, value, s.renderReflectionCapture, true);
        } else if (key == "render.csmCascades") {
            if      (value == "1") s.renderCsmCascades = 1;
            else if (value == "2") s.renderCsmCascades = 2;
            else messages.push_back("'render.csmCascades' must be 1 or 2 (got '" + value + "'); using "
                                    + std::to_string(s.renderCsmCascades));
        } else if (key == "render.shadowDistance") {
            readNumber(key, value, 8.f, 200.f, s.renderShadowDistance);
        } else if (key == "render.lightLimit") {
            float v = (float)s.renderLightLimit;
            if (readNumber(key, value, 0.f, 4096.f, v)) {
                if (v != std::floor(v)) messages.push_back("'render.lightLimit' must be a whole number (got '" + value + "'); using "
                                                           + std::to_string((uint32_t)v));
                s.renderLightLimit = (uint32_t)v;
            }
        } else {
            messages.push_back("unknown setting '" + key + "'; skipped");
        }
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) { messages.push_back("line '" + t + "' has no '='; skipped"); continue; }
        const std::string key = trim(t.substr(0, eq));
        const std::string value = trim(t.substr(eq + 1));
        std::string base;
        const std::string plat = settings_platform::split(key, base);
        if (base.rfind("input.", 0) == 0) {
            if (!plat.empty()) messages.push_back("'" + key + "': input bindings cannot have a platform suffix; skipped");
            else               s.input.emplace_back(key, value);
        } else if (base.rfind("physics.", 0) == 0) {
            if (!plat.empty()) messages.push_back("'" + key + "': physics layer lines cannot have a platform suffix; skipped");
            else               s.physics.emplace_back(key, value);
        } else if (plat.empty()) {
            apply(key, value);
        } else if (plat == platform) {
            platformLines.emplace_back(base, value);   // 줄 순서와 상관없이 기본 줄 뒤에 덮습니다
        }
    }
    for (const auto& [key, value] : platformLines) apply(key, value);
    return s;
}

} // namespace project_settings
