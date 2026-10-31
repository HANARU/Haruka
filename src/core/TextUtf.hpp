#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// ── 문자열 변환 도우미 (2026-10-16) ─────────────────────────────────────────────
// 운영체제가 UTF-16 으로 주는 문자열(WASAPI 장치 ID, OpenXR 이 알려 주는 헤드셋 소리 장치 ID)을 엔진의
// UTF-8 문자열로 바꿉니다. windows.h 없이 씁니다.
namespace hutf {

// 0 으로 끝나는 UTF-16 문자열을 UTF-8 로. C 는 2바이트 단위(Windows 의 wchar_t, char16_t, uint16_t)입니다.
// 짝이 맞지 않는 서로게이트는 U+FFFD 로 바꿉니다.
template <class C>
std::string fromUtf16(const C* s) {
    std::string out;
    if (!s) return out;
    auto put = [&out](uint32_t cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    };
    for (size_t i = 0; s[i] != 0; ++i) {
        const uint32_t u = uint32_t(uint16_t(s[i]));
        if (u >= 0xD800 && u <= 0xDBFF) {
            const uint32_t v = s[i + 1] ? uint32_t(uint16_t(s[i + 1])) : 0;
            if (v >= 0xDC00 && v <= 0xDFFF) {
                put(0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00));
                ++i;
            } else {
                put(0xFFFD);
            }
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            put(0xFFFD);
        } else {
            put(u);
        }
    }
    return out;
}

// ASCII 글자만 대소문자를 무시하고 비교합니다(장치 ID 의 GUID 는 런타임마다 대소문자가 다를 수 있음).
inline bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

// 에디터 UI 는 ASCII 만 그립니다. 장치 이름에서 ASCII 가 아닌 글자를 빼고 공백을 정리합니다. Windows 의 한국어
// 장치 이름 "헤드폰(Oculus Virtual Audio Device)" 처럼 남은 글이 괄호 하나로 감싸여 있으면 괄호도 벗깁니다.
// 남는 글이 없으면 fallback 을 돌려줍니다.
inline std::string asciiLabel(const std::string& utf8, const std::string& fallback) {
    std::string t;
    bool space = false;
    for (unsigned char c : utf8) {
        if (c >= 0x80 || c < 0x20) continue;
        if (c == ' ') { space = !t.empty(); continue; }
        if (space) { t += ' '; space = false; }
        t += char(c);
    }
    // 맨 앞 '(' 의 짝이 맨 끝 ')' 일 때만 벗깁니다("(Realtek(R) Audio)" 는 벗기고 "(A) (B)" 는 그대로).
    if (t.size() >= 2 && t.front() == '(' && t.back() == ')') {
        int depth = 0;
        size_t close = std::string::npos;
        for (size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '(') ++depth;
            else if (t[i] == ')' && --depth == 0) { close = i; break; }
        }
        if (close == t.size() - 1) t = t.substr(1, t.size() - 2);
    }
    return t.empty() ? fallback : t;
}

} // namespace hutf
