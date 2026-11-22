#pragma once
// ── 색 변환 (H3b, 2026-11-22) ────────────────────────────────────────────────────────────────────────────────
// 디테일 패널 색 선택기가 쓰는 변환입니다. 다른 엔진 모듈을 보지 않습니다(math 바닥 모듈).
//   - 선형 ↔ sRGB: IEC 61966-2-1 의 구간 함수(0.04045 · 0.0031308). 0 ~ 1 밖은 잘라 씁니다(HDR 값은 부르는 쪽이 세기를 따로 다룸).
//   - RGB ↔ HSV: H 는 0 ~ 360 도, S 는 0 ~ 1, V 는 성분 최댓값(1 을 넘을 수 있음 = HDR). 회색(S = 0)이나 검정(V = 0)은 색상이
//     정해지지 않으므로 rgbToHsv 는 hueHint(지금 색상)를 그대로 돌려줍니다(색 선택기가 채도를 0 으로 끌었다 다시 올려도 색상이 남음).
//   - 16진수: "RRGGBB" · "RRGGBBAA" · "RGB" · "RGBA"(앞의 '#' 는 있어도 됨, 대소문자 무관). 값은 0 ~ 1 sRGB 성분입니다.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <glm/glm.hpp>

namespace hcolor {

inline float srgbToLinear(float c) {
    c = std::clamp(c, 0.f, 1.f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgb(float c) {
    c = std::clamp(c, 0.f, 1.f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}
inline glm::vec3 srgbToLinear(const glm::vec3& c) { return { srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b) }; }
inline glm::vec3 linearToSrgb(const glm::vec3& c) { return { linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b) }; }

// rgb(성분 0 이상) → (H 도, S, V). 색상을 정할 수 없으면 hueHint.
inline glm::vec3 rgbToHsv(const glm::vec3& rgb, float hueHint = 0.f) {
    const float r = std::max(0.f, rgb.r), g = std::max(0.f, rgb.g), b = std::max(0.f, rgb.b);
    const float mx = std::max({ r, g, b }), mn = std::min({ r, g, b });
    const float d  = mx - mn;
    const float v  = mx;
    const float s  = mx > 0.f ? d / mx : 0.f;
    float h = hueHint;
    if (d > 0.f && mx > 0.f) {
        if (mx == r)      h = 60.f * std::fmod((g - b) / d, 6.f);
        else if (mx == g) h = 60.f * ((b - r) / d + 2.f);
        else              h = 60.f * ((r - g) / d + 4.f);
        if (h < 0.f) h += 360.f;
    }
    return { h, s, v };
}

// (H 도, S 0 ~ 1, V 0 이상) → rgb. H 는 360 으로 돌립니다.
inline glm::vec3 hsvToRgb(const glm::vec3& hsv) {
    float h = std::fmod(hsv.x, 360.f);
    if (h < 0.f) h += 360.f;
    const float s = std::clamp(hsv.y, 0.f, 1.f);
    const float v = std::max(0.f, hsv.z);
    const float c = v * s;
    const float hp = h / 60.f;
    const float x = c * (1.f - std::abs(std::fmod(hp, 2.f) - 1.f));
    glm::vec3 rgb{ 0.f };
    if      (hp < 1.f) rgb = { c, x, 0.f };
    else if (hp < 2.f) rgb = { x, c, 0.f };
    else if (hp < 3.f) rgb = { 0.f, c, x };
    else if (hp < 4.f) rgb = { 0.f, x, c };
    else if (hp < 5.f) rgb = { x, 0.f, c };
    else               rgb = { c, 0.f, x };
    const float m = v - c;
    return rgb + glm::vec3(m);
}

inline int toByte(float c) { return (int)std::lround(std::clamp(c, 0.f, 1.f) * 255.f); }

// sRGB 성분(0 ~ 1) → "RRGGBB" 또는 "RRGGBBAA"(alpha 가 참이면). 대문자.
inline std::string toHex(const glm::vec4& srgb, bool alpha) {
    char buf[16];
    if (alpha) std::snprintf(buf, sizeof(buf), "%02X%02X%02X%02X", toByte(srgb.r), toByte(srgb.g), toByte(srgb.b), toByte(srgb.a));
    else       std::snprintf(buf, sizeof(buf), "%02X%02X%02X", toByte(srgb.r), toByte(srgb.g), toByte(srgb.b));
    return buf;
}

// 16진수 글 → sRGB 성분(0 ~ 1). 알파 자리가 있으면 *hasAlpha = 참. 모양이 틀리면 거짓(out 그대로).
inline bool parseHex(const std::string& text, glm::vec4& out, bool* hasAlpha = nullptr) {
    std::string t;
    for (char ch : text) if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') t += ch;
    if (!t.empty() && t[0] == '#') t.erase(t.begin());
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.erase(0, 2);
    auto nib = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    for (char ch : t) if (nib(ch) < 0) return false;
    glm::vec4 v{ 0.f, 0.f, 0.f, 1.f };
    bool a = false;
    if (t.size() == 3 || t.size() == 4) {
        for (size_t i = 0; i < t.size(); ++i) v[(int)i] = (float)(nib(t[i]) * 17) / 255.f;
        a = t.size() == 4;
    } else if (t.size() == 6 || t.size() == 8) {
        for (size_t i = 0; i < t.size() / 2; ++i) v[(int)i] = (float)(nib(t[2 * i]) * 16 + nib(t[2 * i + 1])) / 255.f;
        a = t.size() == 8;
    } else {
        return false;
    }
    out = v;
    if (hasAlpha) *hasAlpha = a;
    return true;
}

} // namespace hcolor
