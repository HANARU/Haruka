#pragma once
// 콘솔 명령 기록 파일 직렬화(C3, 2026-11-23).
// <쓰기 루트>/Config/console_history.txt (UTF-8, 한 줄 한 명령, 최근 64줄, 연속 중복 없음, 머리말 주석 1줄).

#include <algorithm>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace hconsole {

inline constexpr size_t      kMaxHistory    = 64;
inline constexpr const char* kHistoryHeader = "# Haruka Console History v1";

inline void pushHistory(std::vector<std::string>& history, std::string_view rawLine) {
    while (!rawLine.empty() && (rawLine.back() == '\r' || rawLine.back() == '\n')) rawLine.remove_suffix(1);
    if (rawLine.find_first_not_of(" \t") == std::string_view::npos) return;
    if (!history.empty() && history.back() == rawLine) return;
    history.emplace_back(rawLine);
    if (history.size() > kMaxHistory)
        history.erase(history.begin(), history.begin() + (ptrdiff_t)(history.size() - kMaxHistory));
}

inline std::vector<std::string> parseHistory(std::istream& in) {
    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        pushHistory(out, line);
    }
    return out;
}

inline void formatHistory(std::ostream& out, const std::vector<std::string>& history) {
    out << kHistoryHeader << '\n';
    const size_t start = history.size() > kMaxHistory ? history.size() - kMaxHistory : 0;
    for (size_t i = start; i < history.size(); ++i)
        out << history[i] << '\n';
}

// 후보 목록 팝업의 세 열(이름 | 값 | 도움말) 폭 계산(C3 보완).
struct CompletionLayout {
    float nameW = 0.f;
    float valW  = 0.f;
    float gap   = 0.f;
    float helpW = 0.f;
};

inline CompletionLayout computeCompletionLayout(
    const std::vector<float>& nameWidths,
    const std::vector<float>& valWidths,
    float innerW, float gap, float nameCapRatio = 0.45f, float valCapRatio = 0.20f, float minHelpW = 60.f)
{
    CompletionLayout out;
    out.gap = gap;
    float maxName = 0.f;
    for (float w : nameWidths) maxName = std::max(maxName, w);
    float maxVal = 0.f;
    for (float w : valWidths) maxVal = std::max(maxVal, w);

    out.nameW = std::min(maxName, std::max(0.f, innerW * nameCapRatio));
    out.valW  = (maxVal > 0.f) ? std::min(maxVal, std::max(0.f, innerW * valCapRatio)) : 0.f;
    const float gapsTotal = (out.valW > 0.f) ? (gap * 2.f) : gap;
    out.helpW = std::max(minHelpW, innerW - out.nameW - out.valW - gapsTotal);
    return out;
}

} // namespace hconsole
