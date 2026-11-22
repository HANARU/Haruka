#pragma once
// ── 디테일 속성 검색 (H3b, 2026-11-22) ─────────────────────────────────────────────────────────────────────────
// 디테일 패널 위 검색 칸의 거르기 규칙(UE 의 디테일 검색과 같은 생각). 화면 없이 시험합니다(tests/TextTest.cpp).
//   - 검색어를 공백으로 나눈 낱말이 모두 행 이름에 들어 있어야 맞습니다(순서 무관). 낱말마다 지금 문화권 이름 또는 영어 원문 이름
//     중 하나에 있으면 됩니다(한국어 화면에서 "Location" 으로도, "위치" 로도 찾음).
//   - 비교는 ASCII 대소문자를 무시하고, 한글 등은 그대로 부분 글로 봅니다.
//   - 빈 검색어 = 모두 맞음(거르지 않음).
#include <string>
#include <vector>

namespace hdsearch {

inline std::string foldAscii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

inline std::vector<std::string> terms(const std::string& query) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : query) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) out.push_back(foldAscii(cur)); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(foldAscii(cur));
    return out;
}

inline bool active(const std::string& query) { return !terms(query).empty(); }

// 행 이름(지금 문화권 · 영어 원문)이 검색어에 맞는가.
inline bool matches(const std::string& query, const std::string& shown, const std::string& english) {
    const std::vector<std::string> ts = terms(query);
    if (ts.empty()) return true;
    const std::string a = foldAscii(shown), b = foldAscii(english);
    for (const std::string& t : ts)
        if (a.find(t) == std::string::npos && b.find(t) == std::string::npos) return false;
    return true;
}

// 접을 수 있는 묶음(트랜스폼): 검색 중이면 맞는 행이 있을 때만 펼침, 아니면 사용자가 정한 접힘 상태(기억한 값은 바꾸지 않음).
inline bool sectionExpanded(bool searching, int matches, bool remembered) { return searching ? matches > 0 : remembered; }
// 컴포넌트 묶음: 머리 줄은 늘 보이고, 검색 중에는 맞는 행만 · 읽기 정보 줄은 숨김. 맞는 행이 없으면 머리 줄만(접힌 모양).
inline bool showInfoLine(bool searching) { return !searching; }
// 맞는 행이 하나도 없음을 알리는 줄.
inline bool showNoMatch(bool searching, int totalMatches) { return searching && totalMatches == 0; }

} // namespace hdsearch
