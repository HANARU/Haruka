#pragma once

// ── `이름=값` 줄의 값 해석 ──────────────────────────────────────────────────────
// 씬 · 프리팹의 COMPONENT 블록(scene/ComponentFields.hpp)과 Config/engine.ini(EngineSettings.hpp)가 함께 씁니다.
// 2026-11-18 구조 정리에서 ComponentFields.hpp 에서 떼어 냈습니다(기반 모듈의 설정 파일이 장면 모듈을 보지 않게).
// 문자열 전체가 그 형이어야 성공입니다(앞뒤 공백은 허용). 실패하면 out 을 건드리지 않습니다.

#include <cstdlib>
#include <string>

#include <glm/glm.hpp>

namespace fieldparse {

inline bool onlySpaceFrom(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return *p == '\0';
}

inline bool toFloat(const std::string& s, float& out) {
    const char* b = s.c_str();
    char* e = nullptr;
    const float v = std::strtof(b, &e);
    if (e == b || !onlySpaceFrom(e)) return false;
    out = v;
    return true;
}

inline bool toInt(const std::string& s, int& out) {
    const char* b = s.c_str();
    char* e = nullptr;
    const long v = std::strtol(b, &e, 10);
    if (e == b || !onlySpaceFrom(e)) return false;
    out = (int)v;
    return true;
}

inline bool toBool(const std::string& s, bool& out) {
    int v = 0;
    if (toInt(s, v) && (v == 0 || v == 1)) { out = (v == 1); return true; }
    if (s == "true")  { out = true;  return true; }
    if (s == "false") { out = false; return true; }
    return false;
}

inline bool toVec3(const std::string& s, glm::vec3& out) {
    const char* p = s.c_str();
    glm::vec3 v{};
    for (int k = 0; k < 3; ++k) {
        char* e = nullptr;
        v[k] = std::strtof(p, &e);
        if (e == p) return false;
        p = e;
    }
    if (!onlySpaceFrom(p)) return false;
    out = v;
    return true;
}

} // namespace fieldparse
