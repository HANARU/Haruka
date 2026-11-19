#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// ── UTF-8 도우미 (2026-11-19, 글자 체계 T1, Docs/Text_System.md) ─────────────────────────────────────
// 엔진의 글은 std::string UTF-8 하나입니다. 여기에는 코드 포인트 읽기 · 쓰기, 글자(grapheme) 경계, 단어 경계, 한글 NFC 합치기,
// ASCII 대소문자 무시 비교를 둡니다. 글자 경계는 UAX #29 의 확장 글자 묶음을 줄인 것입니다(CR LF · 제어 문자 · 한글 자모 L V T ·
// 결합 문자 · 변형 선택자 · 이모지 수식 · ZWJ 이음 · 국기). 표 전체를 넣지 않고, 편집 칸에서 실제로 만나는 구간만 둡니다.
namespace utf8 {

// 잘못된 바이트 하나를 읽을 때 돌려주는 값(D4 와 같음).
inline constexpr uint32_t kInvalid = '?';

// s[i] 부터 코드 포인트 하나를 읽고 i 를 다음 글자로 옮깁니다. 잘못된 바이트(너무 긴 표현 · 대리 쌍 · 범위 밖 · 끝에서 잘림)는
// '?' 하나로 읽고 한 바이트만 넘깁니다. i 가 끝이면 0 을 돌려주고 그대로 둡니다.
inline uint32_t next(std::string_view s, size_t& i) {
    if (i >= s.size()) return 0;
    const unsigned char c0 = (unsigned char)s[i];
    if (c0 < 0x80) { ++i; return c0; }
    size_t   extra = 0;
    uint32_t cp    = 0;
    if      ((c0 & 0xE0) == 0xC0) { extra = 1; cp = c0 & 0x1Fu; }
    else if ((c0 & 0xF0) == 0xE0) { extra = 2; cp = c0 & 0x0Fu; }
    else if ((c0 & 0xF8) == 0xF0) { extra = 3; cp = c0 & 0x07u; }
    else { ++i; return kInvalid; }
    if (i + extra >= s.size()) { ++i; return kInvalid; }
    for (size_t k = 1; k <= extra; ++k) {
        const unsigned char ck = (unsigned char)s[i + k];
        if ((ck & 0xC0) != 0x80) { ++i; return kInvalid; }
        cp = (cp << 6) | (ck & 0x3Fu);
    }
    static constexpr uint32_t kMin[4] = { 0, 0x80, 0x800, 0x10000 };
    if (cp < kMin[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { ++i; return kInvalid; }
    i += extra + 1;
    return cp;
}

// i 앞의 코드 포인트 시작 자리(이어지는 바이트를 건너뜀). 0 이면 0.
inline size_t prevStart(std::string_view s, size_t i) {
    if (i == 0) return 0;
    if (i > s.size()) i = s.size();
    size_t j = i - 1;
    const size_t lim = i >= 4 ? i - 4 : 0;
    while (j > lim && ((unsigned char)s[j] & 0xC0) == 0x80) --j;
    // 거꾸로 찾은 자리에서 앞으로 읽어 i 에 정확히 닿지 않으면(깨진 바이트) 한 바이트만 물러납니다.
    size_t k = j;
    next(s, k);
    return k == i ? j : i - 1;
}

// 코드 포인트를 UTF-8 로 덧붙입니다. 대리 쌍 · 범위 밖은 '?'.
inline void append(std::string& out, uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kInvalid;
    if (cp < 0x80)       { out.push_back((char)cp); }
    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6)));  out.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

inline std::string encode(uint32_t cp) { std::string s; append(s, cp); return s; }

// 글 전체가 올바른 UTF-8 인지.
inline bool isValid(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const size_t start = i;
        if (next(s, i) == kInvalid && !(i - start == 1 && s[start] == '?')) return false;
    }
    return true;
}

// 글자 중간을 자르지 않는 앞부분 바이트 수(maxBytes 이하).
inline size_t floorBoundary(std::string_view s, size_t maxBytes) {
    if (maxBytes >= s.size()) return s.size();
    size_t n = maxBytes;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;
    return n;
}

inline size_t codepointCount(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++n) next(s, i);
    return n;
}

// 화면에 찍을 수 있는 글자인지(입력으로 받을 글자). 제어 문자(C0 · DEL · C1) · 대리 쌍 · 범위 밖 · 비문자(U+FFFE/F)는 아님.
inline bool isPrintable(uint32_t cp) {
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    if (cp > 0x10FFFF || (cp & 0xFFFE) == 0xFFFE) return false;
    return true;
}

// 붙여넣기 · 외부 글을 칸에 넣을 수 있게 고칩니다. 잘못된 바이트는 '?', 탭은 공백, 줄바꿈은 한 줄 칸이면 공백(CR LF 는 하나로),
// 그 밖의 제어 문자는 뺍니다.
inline std::string sanitize(std::string_view s, bool singleLine) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const size_t   start = i;
        const uint32_t cp    = next(s, i);
        if (cp == '\r') {
            if (i < s.size() && s[i] == '\n') continue;   // CR LF → 다음 LF 하나로
            out.push_back(singleLine ? ' ' : '\n');
            continue;
        }
        if (cp == '\n') { out.push_back(singleLine ? ' ' : '\n'); continue; }
        if (cp == '\t') { out.push_back(' '); continue; }
        if (!isPrintable(cp)) continue;
        if (cp == kInvalid && !(i - start == 1 && s[start] == '?')) { out.push_back('?'); continue; }   // 깨진 바이트
        out.append(s.data() + start, i - start);
    }
    return out;
}

// ── 글자 경계 ────────────────────────────────────────────────────────────────────────────────
enum class GB : uint8_t { Other, CR, LF, Control, Extend, ZWJ, RI, Prepend, L, V, T, LV, LVT, Pict };

inline bool inRange(uint32_t c, uint32_t a, uint32_t b) { return c >= a && c <= b; }

inline GB graphemeClass(uint32_t c) {
    if (c == '\r') return GB::CR;
    if (c == '\n') return GB::LF;
    if (c < 0x20 || inRange(c, 0x7F, 0x9F) || c == 0x200B || inRange(c, 0x2028, 0x2029)) return GB::Control;
    if (c == 0x200D) return GB::ZWJ;
    // 한글 자모 · 음절
    if (inRange(c, 0x1100, 0x115F) || inRange(c, 0xA960, 0xA97C)) return GB::L;
    if (inRange(c, 0x1160, 0x11A7) || inRange(c, 0xD7B0, 0xD7C6)) return GB::V;
    if (inRange(c, 0x11A8, 0x11FF) || inRange(c, 0xD7CB, 0xD7FB)) return GB::T;
    if (inRange(c, 0xAC00, 0xD7A3)) return ((c - 0xAC00) % 28 == 0) ? GB::LV : GB::LVT;
    if (inRange(c, 0x1F1E6, 0x1F1FF)) return GB::RI;
    // 결합 문자 · 이음 표시(Extend, SpacingMark 를 함께 둠)
    if (inRange(c, 0x0300, 0x036F) || inRange(c, 0x0483, 0x0489) || inRange(c, 0x0591, 0x05BD) || c == 0x05BF ||
        inRange(c, 0x05C1, 0x05C2) || inRange(c, 0x05C4, 0x05C5) || c == 0x05C7 || inRange(c, 0x0610, 0x061A) ||
        inRange(c, 0x064B, 0x065F) || c == 0x0670 || inRange(c, 0x06D6, 0x06DC) || inRange(c, 0x06DF, 0x06E4) ||
        inRange(c, 0x06E7, 0x06E8) || inRange(c, 0x06EA, 0x06ED) || inRange(c, 0x0900, 0x0903) ||
        inRange(c, 0x093A, 0x094F) || inRange(c, 0x0951, 0x0957) || inRange(c, 0x0962, 0x0963) ||
        inRange(c, 0x0981, 0x0983) || inRange(c, 0x09BC, 0x09D7) || c == 0x0E31 || inRange(c, 0x0E34, 0x0E3A) ||
        inRange(c, 0x0E47, 0x0E4E) || c == 0x0EB1 || inRange(c, 0x0EB4, 0x0EBC) || inRange(c, 0x0EC8, 0x0ECD) ||
        inRange(c, 0x1AB0, 0x1AFF) || inRange(c, 0x1DC0, 0x1DFF) || c == 0x200C || inRange(c, 0x20D0, 0x20FF) ||
        inRange(c, 0x302A, 0x302F) || inRange(c, 0x3099, 0x309A) || inRange(c, 0xFE00, 0xFE0F) ||
        inRange(c, 0xFE20, 0xFE2F) || inRange(c, 0xFF9E, 0xFF9F) || inRange(c, 0x1F3FB, 0x1F3FF) ||
        inRange(c, 0xE0020, 0xE007F) || inRange(c, 0xE0100, 0xE01EF))
        return GB::Extend;
    // 이모지(Extended_Pictographic 의 주요 구간)
    if (c == 0x00A9 || c == 0x00AE || c == 0x203C || c == 0x2049 || c == 0x2122 || c == 0x2139 ||
        inRange(c, 0x2194, 0x2199) || inRange(c, 0x21A9, 0x21AA) || inRange(c, 0x231A, 0x231B) || c == 0x2328 ||
        c == 0x23CF || inRange(c, 0x23E9, 0x23F3) || inRange(c, 0x23F8, 0x23FA) || c == 0x24C2 ||
        inRange(c, 0x25AA, 0x25AB) || c == 0x25B6 || c == 0x25C0 || inRange(c, 0x25FB, 0x25FE) ||
        inRange(c, 0x2600, 0x27BF) || inRange(c, 0x2934, 0x2935) || inRange(c, 0x2B05, 0x2B07) ||
        inRange(c, 0x2B1B, 0x2B1C) || c == 0x2B50 || c == 0x2B55 || c == 0x3030 || c == 0x303D || c == 0x3297 ||
        c == 0x3299 || inRange(c, 0x1F000, 0x1FAFF) || inRange(c, 0x1FC00, 0x1FFFD))
        return GB::Pict;
    return GB::Other;
}

// 두 코드 포인트 사이에서 글자가 끊기는지. ri = 앞쪽에 이어진 국기 기호 수, pictZwj = 앞이 "이모지 Extend* ZWJ" 인지.
inline bool isGraphemeBreak(GB a, GB b, int ri, bool pictZwj) {
    if (a == GB::CR && b == GB::LF) return false;                                        // GB3
    if (a == GB::CR || a == GB::LF || a == GB::Control) return true;                     // GB4
    if (b == GB::CR || b == GB::LF || b == GB::Control) return true;                     // GB5
    if (a == GB::L && (b == GB::L || b == GB::V || b == GB::LV || b == GB::LVT)) return false;   // GB6
    if ((a == GB::LV || a == GB::V) && (b == GB::V || b == GB::T)) return false;         // GB7
    if ((a == GB::LVT || a == GB::T) && b == GB::T) return false;                        // GB8
    if (b == GB::Extend || b == GB::ZWJ) return false;                                   // GB9 · GB9a
    if (a == GB::Prepend) return false;                                                  // GB9b
    if (a == GB::ZWJ && b == GB::Pict && pictZwj) return false;                          // GB11
    if (a == GB::RI && b == GB::RI) return (ri % 2) == 0;                                // GB12 · GB13
    return true;                                                                         // GB999
}

// i(글자 경계라고 봄)에서 시작하는 글자의 끝(다음 경계). 끝이면 s.size().
inline size_t nextGrapheme(std::string_view s, size_t i) {
    if (i >= s.size()) return s.size();
    uint32_t cp = next(s, i);
    GB   prev    = graphemeClass(cp);
    int  ri      = prev == GB::RI ? 1 : 0;
    bool inPict  = prev == GB::Pict;   // 이모지 뒤로 Extend 만 이어지는 중
    bool pictZwj = false;
    while (i < s.size()) {
        size_t   j  = i;
        const GB cl = graphemeClass(next(s, j));
        if (isGraphemeBreak(prev, cl, ri, pictZwj)) break;
        pictZwj = (cl == GB::ZWJ && inPict);
        inPict  = (cl == GB::Pict) || (inPict && cl == GB::Extend);
        ri      = cl == GB::RI ? ri + 1 : 0;
        prev    = cl;
        i       = j;
    }
    return i;
}

// i 앞 글자의 시작(이전 경계). 짧은 칸 글을 위해 처음부터 경계를 따라갑니다.
inline size_t prevGrapheme(std::string_view s, size_t i) {
    if (i > s.size()) i = s.size();
    size_t last = 0;
    for (size_t p = 0; p < i;) {
        const size_t n = nextGrapheme(s, p);
        if (n >= i) return p;
        last = n;
        p    = n;
    }
    return last;
}

// pos 를 글자 경계로 내립니다(경계면 그대로).
inline size_t snapGrapheme(std::string_view s, size_t pos) {
    if (pos >= s.size()) return s.size();
    size_t p = 0;
    while (p < s.size()) {
        const size_t n = nextGrapheme(s, p);
        if (n > pos) return p;
        p = n;
    }
    return s.size();
}

inline size_t graphemeCount(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++n) i = nextGrapheme(s, i);
    return n;
}

// ── 단어 경계(Ctrl+화살표 · Ctrl+Backspace) ───────────────────────────────────────────────────
// 공백 · 문장 부호 · 단어 글자(그 밖 전부: 영문 · 숫자 · 한글 · 한자 등) 세 갈래로 나눕니다. Windows 편집 칸처럼 오른쪽은
// "지금 무리를 지나고 공백을 건넌 자리", 왼쪽은 "공백을 건넌 뒤 무리의 시작"입니다.
enum class WordClass : uint8_t { Space, Punct, Word };

inline WordClass wordClassOf(uint32_t c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x3000 || c == 0xA0) return WordClass::Space;
    if ((c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) || (c >= 0x5B && c <= 0x60 && c != '_') ||
        (c >= 0x7B && c <= 0x7E) || (c >= 0x2000 && c <= 0x206F) || (c >= 0x3001 && c <= 0x3003) ||
        (c >= 0x3008 && c <= 0x3011) || (c >= 0xFF01 && c <= 0xFF0F))
        return WordClass::Punct;
    return WordClass::Word;
}

inline WordClass wordClassAt(std::string_view s, size_t i) {
    return wordClassOf(next(s, i));
}

inline size_t nextWord(std::string_view s, size_t i) {
    if (i >= s.size()) return s.size();
    const WordClass c0 = wordClassAt(s, i);
    if (c0 != WordClass::Space)
        while (i < s.size() && wordClassAt(s, i) == c0) i = nextGrapheme(s, i);
    while (i < s.size() && wordClassAt(s, i) == WordClass::Space) i = nextGrapheme(s, i);
    return i;
}

inline size_t prevWord(std::string_view s, size_t i) {
    if (i > s.size()) i = s.size();
    while (i > 0) {
        const size_t p = prevGrapheme(s, i);
        if (wordClassAt(s, p) != WordClass::Space) break;
        i = p;
    }
    if (i == 0) return 0;
    const WordClass c0 = wordClassAt(s, prevGrapheme(s, i));
    while (i > 0) {
        const size_t p = prevGrapheme(s, i);
        if (wordClassAt(s, p) != c0) break;
        i = p;
    }
    return i;
}

// ── 한글 NFC ─────────────────────────────────────────────────────────────────────────────────
// 조합형 자모 순서(초성 U+1100 ~ 1112 + 중성 U+1161 ~ 1175 [+ 종성 U+11A8 ~ 11C2])와 LV 음절 + 종성을 완성형 음절로 합칩니다.
// macOS 가 파일 이름을 NFD 로 주면 "한" 이 자모 셋으로 들어옵니다. 그 밖의 정규화(라틴 결합 문자 등)는 하지 않습니다.
inline std::string composeHangul(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const size_t start = i;
        uint32_t     cp    = next(s, i);
        if (cp >= 0x1100 && cp <= 0x1112 && i < s.size()) {
            size_t         j = i;
            const uint32_t v = next(s, j);
            if (v >= 0x1161 && v <= 0x1175) {
                cp = 0xAC00 + ((cp - 0x1100) * 21 + (v - 0x1161)) * 28;
                i  = j;
            }
        }
        if (cp >= 0xAC00 && cp <= 0xD7A3 && (cp - 0xAC00) % 28 == 0 && i < s.size()) {
            size_t         j = i;
            const uint32_t t = next(s, j);
            if (t >= 0x11A8 && t <= 0x11C2) { cp += t - 0x11A7; i = j; }
        }
        if (cp == kInvalid && !(i - start == 1 && s[start] == '?')) { out.push_back('?'); continue; }
        append(out, cp);
    }
    return out;
}

// ── ASCII 대소문자 무시 ───────────────────────────────────────────────────────────────────────
// A ~ Z 만 접습니다(UTF-8 바이트는 그대로). ::tolower(char) 처럼 음수 char 를 넘기는 미정의 동작이 없습니다.
inline char asciiLower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

inline std::string asciiLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = asciiLower(c);
    return out;
}

// needle 을 hay 에서 ASCII 대소문자 무시로 찾습니다. 빈 needle 은 참.
inline bool containsIgnoreCase(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t k = 0;
        while (k < needle.size() && asciiLower(hay[i + k]) == asciiLower(needle[k])) ++k;
        if (k == needle.size()) return true;
    }
    return false;
}

} // namespace utf8
