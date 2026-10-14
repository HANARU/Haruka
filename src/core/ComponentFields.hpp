#pragma once

// ── 컴포넌트 값 직렬화 (DEF-22) ────────────────────────────────────────────────
// 씬 · 프리팹 파일의 COMPONENT 블록 안에서 값마다 이름을 붙여 `이름=값` 한 줄로 씁니다. UE 의 tagged
// property, Godot 의 .tscn, Unity 의 YAML 과 같은 방식이라 순서와 상관없고, 모르는 이름은 경고하고
// 건너뛰며, 없는 이름은 기본값으로 둡니다. 예전 LIGHT: 줄처럼 값의 위치로 뜻을 정하지 않습니다.
//
// 기본값인 값도 모두 씁니다(사용자 결정, Unity 방식). 기본값과 다른 값만 쓰면(UE · Godot) 나중에
// 기본값을 바꿀 때 이미 저장한 씬의 뜻이 조용히 바뀝니다.
//
// 이름은 멤버 이름을 따릅니다. 한 번 저장한 이름은 바꾸지 마십시오. 바꿔야 하면 readField 에서 옛 이름도
// 받으십시오(Unity 의 FormerlySerializedAs 와 같은 역할).

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "TextFloat.hpp"

class AssetManager;

class ComponentFieldWriter {
public:
    using Field = std::pair<std::string, std::string>;

    void f (const char* key, float v)            { put(key, floatToText(v)); }
    void b (const char* key, bool v)             { put(key, v ? "1" : "0"); }
    void i (const char* key, int v)              { put(key, std::to_string(v)); }
    void v3(const char* key, const glm::vec3& v) { put(key, floatToText(v.x) + " " + floatToText(v.y) + " " + floatToText(v.z)); }
    void s (const char* key, std::string v)      { put(key, std::move(v)); }
    // 값에 줄바꿈이 들어가면 한 줄 형식이 깨지므로 공백으로 바꿉니다.
    void put(std::string key, std::string value) {
        for (char& ch : value) if (ch == '\n' || ch == '\r') ch = ' ';
        fields_.emplace_back(std::move(key), std::move(value));
    }
    const std::vector<Field>& fields() const { return fields_; }

private:
    std::vector<Field> fields_;
};

// 블록의 값을 다 읽은 뒤(onFieldsLoaded) 받는 것. 에셋을 불러와야 하는 컴포넌트(메시)가 씁니다.
struct ComponentLoadContext {
    AssetManager* assets = nullptr;
};

// 값 해석. 문자열 전체가 그 형이어야 성공입니다(앞뒤 공백은 허용). 실패하면 out 을 건드리지 않습니다.
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
