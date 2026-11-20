#include "ImportSettings.hpp"
#include "../vfs/LogicalPath.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
    while (!s.empty() && std::isspace((unsigned char)s.back()))  s.remove_suffix(1);
    return s;
}

bool parseBool(std::string_view v, bool& out) {
    std::string l;
    for (char c : v) l.push_back(lowerAscii(c));
    if (l == "true" || l == "1" || l == "on" || l == "yes")  { out = true;  return true; }
    if (l == "false" || l == "0" || l == "off" || l == "no") { out = false; return true; }
    return false;
}

bool parseFloat(std::string_view v, float& out) {
    const std::string s(v);
    char* end = nullptr;
    const float f = std::strtof(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !std::isfinite(f)) return false;
    out = f;
    return true;
}

// 재귀 없이 역추적하는 glob 입니다. '**' 의 되돌아갈 자리와 '*' 의 되돌아갈 자리를 따로 둡니다('*' 는 '/' 에서 멈춤).
bool matchFrom(std::string_view p, size_t pi, std::string_view s, size_t si) {
    while (pi < p.size()) {
        if (p[pi] == '*') {
            const bool deep = pi + 1 < p.size() && p[pi + 1] == '*';
            const size_t next = pi + (deep ? 2 : 1);
            if (next == p.size()) {
                if (deep) return true;
                return s.find('/', si) == std::string_view::npos;
            }
            for (size_t k = si; k <= s.size(); ++k) {
                if (matchFrom(p, next, s, k)) return true;
                if (k < s.size() && !deep && s[k] == '/') break;
            }
            return false;
        }
        if (si >= s.size()) return false;
        if (p[pi] == '?') {
            if (s[si] == '/') return false;
        } else if (lowerAscii(p[pi]) != lowerAscii(s[si])) {
            return false;
        }
        ++pi; ++si;
    }
    return si == s.size();
}

} // namespace

namespace himport {
bool globMatch(std::string_view pattern, std::string_view path) { return matchFrom(pattern, 0, path, 0); }
}

uint64_t ImportSettings::hash() const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    const uint32_t version = 1;   // 해석이 바뀌면 올립니다(캐시를 다시 만듦)
    mix(&version, sizeof(version));
    float s = scale;
    if (s == 0.f) s = 0.f;        // -0 과 0 을 같게
    mix(&s, sizeof(s));
    const uint8_t flags = uint8_t((autoLod ? 1u : 0u) | (srgb ? 2u : 0u));
    mix(&flags, 1);
    return h;
}

ImportSettingsTable ImportSettingsTable::parse(std::string_view text, std::vector<std::string>& messages) {
    ImportSettingsTable t;
    Rule* cur = nullptr;
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;
        if (lineNo == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line.remove_prefix(3);   // UTF-8 BOM
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            if (eol == text.size()) break;
            continue;
        }
        auto warn = [&](const std::string& why) {
            messages.push_back("line " + std::to_string(lineNo) + ": " + why);
        };
        if (line[0] == '[') {
            const size_t close = line.find(']');
            if (close == std::string_view::npos || close == 1) {
                warn("section must be [pattern]");
                cur = nullptr;
            } else {
                Rule r;
                r.pattern = hvfs::toCanonical(trim(line.substr(1, close - 1)));
                r.line    = lineNo;
                t.rules_.push_back(std::move(r));
                cur = &t.rules_.back();
            }
        } else {
            const size_t eq = line.find('=');
            if (eq == std::string_view::npos) {
                warn("expected key = value");
            } else if (!cur) {
                warn("key outside a [pattern] section");
            } else {
                std::string key;
                for (char c : trim(line.substr(0, eq))) key.push_back(lowerAscii(c));
                std::string_view value = line.substr(eq + 1);
                const size_t hashPos = value.find('#');
                if (hashPos != std::string_view::npos) value = value.substr(0, hashPos);
                value = trim(value);
                if (key == "scale") {
                    float f = 0.f;
                    if (!parseFloat(value, f) || f < 0.0001f || f > 10000.f) warn("scale must be a number in 0.0001 .. 10000");
                    else cur->scale = f;
                } else if (key == "autolod" || key == "srgb") {
                    bool b = false;
                    if (!parseBool(value, b)) warn(key + " must be true or false");
                    else if (key == "autolod") cur->autoLod = b;
                    else cur->srgb = b;
                } else {
                    warn("unknown key '" + key + "' (scale, autoLod, srgb)");
                }
            }
        }
        if (eol == text.size()) break;
    }
    return t;
}

ImportSettings ImportSettingsTable::resolve(std::string_view path) const {
    ImportSettings s;
    if (rules_.empty()) return s;
    const std::string canon = hvfs::toCanonical(path);
    for (const Rule& r : rules_) {
        if (!himport::globMatch(r.pattern, canon)) continue;
        if (r.scale)   s.scale   = *r.scale;
        if (r.autoLod) s.autoLod = *r.autoLod;
        if (r.srgb)    s.srgb    = *r.srgb;
    }
    return s;
}
