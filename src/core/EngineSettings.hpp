#pragma once

// ── 엔진 상한 설정 (DEF-10) ─────────────────────────────────────────────────────
// 그리기 행 · 라이트 · 물리 바디의 상한을 실행 폴더의 Config/engine.ini 에서 읽습니다(사용자 결정). 예전에는
// 렌더러와 물리에 상수로 따로 박혀 있어, 한쪽만 올리면 다른 쪽에서 조용히 잘렸습니다. 이제 한 파일에 나란히
// 있습니다. Godot 이 Jolt 바디 상한을 프로젝트 설정(physics/jolt_physics_3d/limits/max_bodies)으로 두는 것과
// 같은 방식입니다. 시작할 때 한 번 읽고(버퍼와 Jolt 를 그 크기로 만듦), 바꾸면 다시 실행해야 합니다.
//
// 파일 형식은 씬의 COMPONENT 블록과 같은 `이름=값` 줄이고 # 로 시작하는 줄은 주석입니다. 파일이 없으면 기본값으로
// 만들어 두므로 찾아서 고칠 수 있습니다. 범위 밖 값은 범위 안으로 고치고, 모르는 이름 · 깨진 값은 알립니다.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ComponentFields.hpp"

struct EngineLimits {
    // 프레임당 transform 행(= 한 프레임에 그릴 수 있는 메시 수, DEF-16). 행 64B, 프레임 슬롯 2개, Zone C(64MB).
    uint32_t maxDrawRows = 16384;
    // GPU 로 올리는 라이트 수(DEF-33). 라이트 64B. 셰이더는 픽셀마다 올린 라이트를 모두 돕니다.
    uint32_t maxLights   = 256;
    // Jolt 가 한꺼번에 다루는 바디 수(깨어 있든 잠들었든).
    uint32_t maxBodies   = 4096;

    static constexpr uint32_t kMinDrawRows = 1024, kMaxDrawRowsCap = 131072;   // 최대 8MB × 2 슬롯
    static constexpr uint32_t kMinLights   = 1,    kMaxLightsCap   = 4096;
    static constexpr uint32_t kMinBodies   = 256,  kMaxBodiesCap   = 65536;
};

namespace engine_settings {

// 파일 내용을 읽습니다. 알릴 것은 messages 에 넣습니다(호출자가 로그로 남김).
inline EngineLimits parse(const std::string& text, std::vector<std::string>& messages) {
    EngineLimits l;
    auto readU = [&](const std::string& key, const std::string& value, uint32_t& out, uint32_t lo, uint32_t hi) {
        int v = 0;
        if (!fieldparse::toInt(value, v)) {
            messages.push_back("'" + key + "' has an invalid value '" + value + "'; using " + std::to_string(out));
            return;
        }
        const uint32_t c = (uint32_t)std::clamp<long long>(v, lo, hi);
        if ((long long)c != (long long)v)
            messages.push_back("'" + key + "' = " + std::to_string(v) + " is out of range [" + std::to_string(lo)
                               + ", " + std::to_string(hi) + "]; using " + std::to_string(c));
        out = c;
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { messages.push_back("line '" + line + "' has no '='; skipped"); continue; }
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        key.erase(0, key.find_first_not_of(" \t"));
        if      (key == "render.maxDrawRows") readU(key, value, l.maxDrawRows, EngineLimits::kMinDrawRows, EngineLimits::kMaxDrawRowsCap);
        else if (key == "render.maxLights")   readU(key, value, l.maxLights,   EngineLimits::kMinLights,   EngineLimits::kMaxLightsCap);
        else if (key == "physics.maxBodies")  readU(key, value, l.maxBodies,   EngineLimits::kMinBodies,   EngineLimits::kMaxBodiesCap);
        else messages.push_back("unknown setting '" + key + "'; skipped");
    }
    return l;
}

// 파일이 없을 때 만들어 두는 내용(기본값과 설명).
inline std::string defaultText() {
    const EngineLimits d;
    return "# Haruka engine limits. Read once at startup; restart the engine after editing.\n"
           "# Lines are name=value. Out-of-range values are clamped and reported in the log.\n"
           "\n"
           "# Meshes drawn per frame (transform rows). 64 bytes per row, 2 frame slots. Range 1024..131072.\n"
           "render.maxDrawRows=" + std::to_string(d.maxDrawRows) + "\n"
           "# Lights uploaded per frame after view culling. Every pixel loops over them. Range 1..4096.\n"
           "render.maxLights=" + std::to_string(d.maxLights) + "\n"
           "# Physics bodies (awake or sleeping). Range 256..65536.\n"
           "physics.maxBodies=" + std::to_string(d.maxBodies) + "\n";
}

} // namespace engine_settings
