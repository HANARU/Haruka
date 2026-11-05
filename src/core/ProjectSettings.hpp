#pragma once
#include <cstdlib>
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
    static constexpr const char* kPath = "assets/project.ini";

    // 실행하면 처음 여는 씬(논리 경로). 에디터는 마지막으로 연 씬이 있으면 그것을 먼저 엽니다.
    std::string startScene = "assets/scenes/LightingProbe.hscene";
    // input. 으로 시작하는 줄(입력 바인딩)은 입력 시스템이 따로 해석합니다. 순서를 지켜 넘깁니다.
    std::vector<std::pair<std::string, std::string>> input;
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
    // 켜면 지금 화면, 끄면 그 패스를 기록하지 않습니다(GPU 시간을 아낌). 시작할 때 한 번 읽습니다. 플랫폼별 값
    // (.android 접미사)은 아직 없습니다. 명령줄 --ssao · --no-ssao 가 render.ao 를 덮어씁니다.
    bool  renderAo        = true;
    bool  renderBloom     = true;
};

// 리그 눈높이의 허용 범위(m). 프로젝트 설정과 XR Rig 컴포넌트가 함께 씁니다.
namespace xr_rig {
constexpr float kMinEyeHeight     = 0.5f;
constexpr float kMaxEyeHeight     = 2.5f;
constexpr float kDefaultEyeHeight = 1.7f;
} // namespace xr_rig

namespace project_settings {

inline std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// 논리 경로 규칙: assets/ 아래의 .hscene 이어야 합니다. 씬 파일은 그 밖에 두지 않습니다.
inline bool isSceneLogicalPath(const std::string& p) {
    const std::string ext = ".hscene";
    return p.rfind("assets/", 0) == 0 && p.size() > ext.size() + 7 &&
           p.compare(p.size() - ext.size(), ext.size(), ext) == 0 &&
           p.find("..") == std::string::npos && p.find('\\') == std::string::npos;
}

inline ProjectSettings parse(const std::string& text, std::vector<std::string>& messages) {
    ProjectSettings s;
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
        if (key == "startScene") {
            if (isSceneLogicalPath(value)) s.startScene = value;
            else messages.push_back("'startScene' must be a .hscene path under assets/ (got '" + value + "'); using " + s.startScene);
        } else if (key.rfind("input.", 0) == 0) {
            s.input.emplace_back(key, value);
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
            if      (value == "true")  dst = true;
            else if (value == "false") dst = false;
            else { dst = true; messages.push_back("'" + key + "' must be true or false (got '" + value + "'); using true"); }
        } else {
            messages.push_back("unknown setting '" + key + "'; skipped");
        }
    }
    return s;
}

} // namespace project_settings
