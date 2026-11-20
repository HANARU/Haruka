#pragma once
// 사용자 충돌 레이어 표(2026-11-20, Docs/Physics_Joints_And_Events.md D1).
//
// 슬롯 16개(0 = Default 고정)와 쌍별 충돌 여부(대칭)입니다. 기본은 모두 부딪힙니다(레이어가 없던 예전 동작과 같음). 프로젝트 설정
// (project.ini)의 physics. 줄로 이름을 붙이고 부딪히지 않을 쌍을 뺍니다.
//
//   physics.layer.1 = Debris          슬롯 1 ~ 15 의 이름(영문 · 숫자 · _ · -, 32자까지)
//   physics.ignore  = Debris, Debris  이 두 레이어는 서로 부딪히지 않음(여러 줄)
//
// 정적 몸끼리는 표와 상관없이 부딪히지 않습니다(Jolt). 씬 파일 · 컴포넌트는 슬롯 번호를 씁니다.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

struct PhysicsLayerTable {
    static constexpr int      kCount   = 16;
    static constexpr uint16_t kAllMask = 0xFFFFu;

    std::array<std::string, kCount> names;
    std::array<uint16_t, kCount>    collide;   // collide[a] 의 비트 b = a 와 b 가 부딪힘

    PhysicsLayerTable() {
        names[0] = "Default";
        for (int i = 1; i < kCount; ++i) names[(size_t)i] = "Layer" + std::to_string(i);
        collide.fill(kAllMask);
    }

    static bool valid(int layer) { return layer >= 0 && layer < kCount; }

    bool collides(int a, int b) const {
        if (!valid(a) || !valid(b)) return false;
        return (collide[(size_t)a] >> b) & 1u;
    }
    void setCollides(int a, int b, bool on) {
        if (!valid(a) || !valid(b)) return;
        const uint16_t ba = (uint16_t)(1u << b), bb = (uint16_t)(1u << a);
        if (on) { collide[(size_t)a] |= ba; collide[(size_t)b] |= bb; }
        else    { collide[(size_t)a] = (uint16_t)(collide[(size_t)a] & ~ba); collide[(size_t)b] = (uint16_t)(collide[(size_t)b] & ~bb); }
    }
    // 이름(대소문자 구분) 또는 슬롯 번호 글자로 찾습니다. 없으면 -1.
    int find(const std::string& name) const {
        for (int i = 0; i < kCount; ++i)
            if (names[(size_t)i] == name) return i;
        if (!name.empty() && name.size() <= 2) {
            char* end = nullptr;
            const long v = std::strtol(name.c_str(), &end, 10);
            if (end == name.c_str() + name.size() && valid((int)v)) return (int)v;
        }
        return -1;
    }
    static uint16_t bit(int layer) { return valid(layer) ? (uint16_t)(1u << layer) : (uint16_t)0; }

    // project.ini 의 physics. 줄(이름, 값 · 순서대로)을 해석합니다. 잘못된 줄은 건너뛰고 messages 에 알립니다.
    static PhysicsLayerTable parse(const std::vector<std::pair<std::string, std::string>>& lines,
                                   std::vector<std::string>& messages) {
        PhysicsLayerTable t;
        std::vector<std::pair<std::string, std::string>> ignores;   // 이름을 모두 정한 뒤 해석(줄 순서와 무관)
        static constexpr const char* kLayerPrefix = "physics.layer.";
        for (const auto& [key, value] : lines) {
            if (key.rfind(kLayerPrefix, 0) == 0) {
                const std::string idx = key.substr(std::string(kLayerPrefix).size());
                char* end = nullptr;
                const long n = std::strtol(idx.c_str(), &end, 10);
                if (idx.empty() || end != idx.c_str() + idx.size() || n < 1 || n >= kCount) {
                    messages.push_back("'" + key + "': layer slot must be 1 to " + std::to_string(kCount - 1) + "; skipped");
                    continue;
                }
                if (!validName(value)) {
                    messages.push_back("'" + key + "': layer name must be 1-32 letters, digits, '_' or '-' (got '" + value + "'); skipped");
                    continue;
                }
                const int other = t.find(value);
                if (other >= 0 && other != (int)n && t.names[(size_t)other] == value) {
                    messages.push_back("'" + key + "': layer name '" + value + "' is already slot " + std::to_string(other) + "; skipped");
                    continue;
                }
                t.names[(size_t)n] = value;
            } else if (key == "physics.ignore") {
                const size_t comma = value.find(',');
                if (comma == std::string::npos) {
                    messages.push_back("'physics.ignore' must be two layer names separated by ',' (got '" + value + "'); skipped");
                    continue;
                }
                ignores.emplace_back(trim(value.substr(0, comma)), trim(value.substr(comma + 1)));
            } else {
                messages.push_back("unknown setting '" + key + "'; skipped");
            }
        }
        for (const auto& [a, b] : ignores) {
            const int la = t.find(a), lb = t.find(b);
            if (la < 0 || lb < 0) {
                messages.push_back("'physics.ignore = " + a + ", " + b + "': unknown layer '" + (la < 0 ? a : b) + "'; skipped");
                continue;
            }
            t.setCollides(la, lb, false);
        }
        return t;
    }

private:
    static std::string trim(const std::string& s) {
        const size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return {};
        const size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }
    static bool validName(const std::string& s) {
        if (s.empty() || s.size() > 32) return false;
        for (char c : s)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        // 숫자만인 이름은 슬롯 번호와 헷갈리므로 받지 않습니다.
        return s.find_first_not_of("0123456789") != std::string::npos;
    }
};
