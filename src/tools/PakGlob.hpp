#pragma once
#include <string>
#include <vector>

// ── 묶음 목록의 경로 패턴 (2026-10-24, 2026-10-31 시험을 위해 HarukaPak.cpp 에서 옮김) ───────────────
// '*' 는 '/' 를 뺀 아무 글자, '**' 는 '/' 를 포함한 아무 글자, '?' 는 '/' 가 아닌 한 글자입니다. "**/" 는 폴더 0개 이상이고
// 나머지는 구성요소의 시작에서만 맞춥니다. 비교는 ASCII 대소문자를 무시합니다. 숨김 폴더 아래를 빼려면 "**/.*/**" 를 씁니다
// ("**/.*" 는 이름이 '.' 으로 시작하는 파일만 뺌).
namespace hpak {

inline bool globMatch(const char* p, const char* s) {
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
    while (*p) {
        if (p[0] == '*' && p[1] == '*') {
            p += 2;
            if (*p == '/') {
                // "**/" 는 폴더가 0개 이상: 나머지는 지금 자리나 '/' 바로 뒤(구성요소의 시작)에서만 맞춥니다.
                // 아무 자리에서나 맞추면 "**/.*" 가 "assets/project.ini" 의 ".ini" 에도 맞아 숨김 파일이 아닌 것까지 뺍니다.
                ++p;
                for (const char* t = s;; ++t) {
                    if ((t == s || t[-1] == '/') && globMatch(p, t)) return true;
                    if (!*t) return false;
                }
            }
            for (const char* t = s;; ++t) {
                if (globMatch(p, t)) return true;
                if (!*t) return false;
            }
        }
        if (*p == '*') {
            ++p;
            for (const char* t = s;; ++t) {
                if (globMatch(p, t)) return true;
                if (!*t || *t == '/') return false;
            }
        }
        if (!*s) return false;
        if (*p == '?') { if (*s == '/') return false; }
        else if (lower(*p) != lower(*s)) return false;
        ++p; ++s;
    }
    return *s == 0;
}

// '/' 가 없는 패턴은 파일 이름에만, 있는 패턴은 논리 경로 전체에 맞춥니다.
inline bool anyMatch(const std::vector<std::string>& patterns, const std::string& path) {
    const size_t slash = path.rfind('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    for (const auto& pat : patterns) {
        const bool full = pat.find('/') != std::string::npos;
        if (globMatch(pat.c_str(), (full ? path : name).c_str())) return true;
    }
    return false;
}

} // namespace hpak
