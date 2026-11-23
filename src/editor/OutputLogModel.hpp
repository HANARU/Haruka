#pragma once
// Output Log 패널의 순수 로직 모델(C2, 2026-11-23).
// 시각 포맷 · 같은 메시지 묶기(Collapse) · 긴 줄 줄 바꿈 · 설정 파일 직렬화를 위젯 · 렌더러 없이 단위 시험합니다.

#include "../core/HLog.hpp"
#include "../core/Utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <istream>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class ELogTimeMode : uint8_t {
    None   = 0,
    Uptime = 1,
    Clock  = 2,
};

struct OutputLogOptions {
    ELogTimeMode timeMode    = ELogTimeMode::Uptime;
    bool         collapse    = false;
    bool         wordWrap    = false;
    bool         clearOnPlay = false;
};

namespace hlogview {

inline const char* timeModeName(ELogTimeMode m) {
    switch (m) {
        case ELogTimeMode::None:   return "none";
        case ELogTimeMode::Uptime: return "uptime";
        case ELogTimeMode::Clock:  return "clock";
    }
    return "uptime";
}

inline bool parseTimeMode(std::string_view s, ELogTimeMode& out) {
    if (s == "none")   { out = ELogTimeMode::None;   return true; }
    if (s == "uptime") { out = ELogTimeMode::Uptime; return true; }
    if (s == "clock")  { out = ELogTimeMode::Clock;  return true; }
    return false;
}

inline float timeColumnWidth(ELogTimeMode m) {
    switch (m) {
        case ELogTimeMode::None:   return 0.f;
        case ELogTimeMode::Uptime: return 52.f;
        case ELogTimeMode::Clock:  return 82.f;
    }
    return 52.f;
}

// 벽시계 밀리초를 지역 시각 "HH:MM:SS.mmm" 으로 만듭니다.
inline std::string formatLocalClockTime(int64_t wallMs) {
    int64_t ms = wallMs % 1000;
    int64_t secTotal = wallMs / 1000;
    if (ms < 0) { ms += 1000; --secTotal; }
    const std::time_t t = (std::time_t)secTotal;
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d",
                  tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, (int)ms);
    return buf;
}

inline std::string formatLogTime(ELogTimeMode mode, float uptimeSec, int64_t startWallMs, bool dumpMode) {
    switch (mode) {
        case ELogTimeMode::None:
            return {};
        case ELogTimeMode::Uptime: {
            if (dumpMode) return "#.##";
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.2f", (double)uptimeSec);
            return buf;
        }
        case ELogTimeMode::Clock: {
            if (dumpMode) return "##:##:##.###";
            const int64_t offsetMs = (int64_t)std::llround((double)uptimeSec * 1000.0);
            return formatLocalClockTime(startWallMs + offsetMs);
        }
    }
    return {};
}

// 제어 문자(< 32, 127)를 '?' 로 치환하되, 유효한 UTF-8 과 줄바꿈('\n')은 그대로 둡니다(줄바꿈 전처리용).
inline std::string sanitizeLogTextPreserveNewlines(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const size_t   start = i;
        const uint32_t cp    = utf8::next(s, i);
        if (cp == '\n') { out.push_back('\n'); continue; }
        if (cp == '\r') continue;
        if (cp < 32 || cp == 127) { out.push_back('?'); continue; }
        if (cp == '?' && (i - start != 1 || s[start] != '?')) { out.push_back('?'); continue; }
        out.append(s.substr(start, i - start));
    }
    return out;
}

// 줄 바꿈 끔일 때: 개행이 있으면 첫 줄만 취하고 뒤에 ".." 를 붙입니다(실제 말줄임은 호출 쪽이 ellipsizeText 로 자름).
inline std::string firstLineForSingleRow(std::string_view sanitizedText, bool& hadNewline) {
    const size_t nl = sanitizedText.find('\n');
    if (nl == std::string_view::npos) {
        hadNewline = false;
        return std::string(sanitizedText);
    }
    hadNewline = true;
    std::string first(sanitizedText.substr(0, nl));
    first += "..";
    return first;
}

// 순수 줄 나눔 함수: '\n' 으로 문단을 나누고, 각 문단이 maxWidth 를 넘으면 공백 우선 · 긴 낱말은 UTF-8 글자 경계에서 나눕니다.
inline std::vector<std::string> wrapLogText(std::string_view text, float maxWidth,
                                            const std::function<float(std::string_view)>& measureWidth) {
    std::vector<std::string> out;
    auto splitParagraphs = [&](auto&& onPara) {
        size_t pos = 0;
        while (true) {
            const size_t nl = text.find('\n', pos);
            std::string_view para = (nl == std::string_view::npos)
                ? text.substr(pos)
                : text.substr(pos, nl - pos);
            if (!para.empty() && para.back() == '\r') para.remove_suffix(1);
            onPara(para);
            if (nl == std::string_view::npos) break;
            pos = nl + 1;
        }
    };

    if (maxWidth <= 0.f || !measureWidth) {
        splitParagraphs([&](std::string_view para) { out.emplace_back(para); });
        if (out.empty()) out.emplace_back();
        return out;
    }

    splitParagraphs([&](std::string_view para) {
        if (para.empty()) {
            out.emplace_back();
            return;
        }
        while (!para.empty()) {
            if (measureWidth(para) <= maxWidth) {
                out.emplace_back(para);
                break;
            }
            // 이분 탐색으로 maxWidth 안에 들어가는 가장 긴 UTF-8 접두사를 찾습니다.
            size_t lo = 1, hi = para.size(), best = 0;
            while (lo <= hi) {
                const size_t mid = lo + (hi - lo) / 2;
                const size_t cut = utf8::floorBoundary(para, mid);
                if (cut > 0 && measureWidth(para.substr(0, cut)) <= maxWidth) {
                    best = cut;
                    lo = mid + 1;
                } else {
                    if (mid <= 1) break;
                    hi = mid - 1;
                }
            }
            if (best == 0) {
                // 폭이 한 글자보다 좁아도 최소 1코드포인트는 진행해 무한 루프를 막습니다.
                size_t next = 0;
                utf8::next(para, next);
                best = next > 0 ? next : 1;
            }
            if (best >= para.size()) {
                out.emplace_back(para);
                break;
            }
            // 바로 다음 문자가 공백이면 단어 끝에서 딱 잘린 것입니다.
            if (para[best] == ' ' || para[best] == '\t') {
                out.emplace_back(para.substr(0, best));
                para.remove_prefix(best + 1);
                continue;
            }
            // 접두사 안의 마지막 공백에서 줄을 나눕니다(맨 앞 공백 제외).
            const size_t sp = para.substr(0, best).find_last_of(" \t");
            if (sp != std::string_view::npos && sp > 0) {
                out.emplace_back(para.substr(0, sp));
                para.remove_prefix(sp + 1);
            } else {
                out.emplace_back(para.substr(0, best));
                para.remove_prefix(best);
            }
        }
    });

    if (out.empty()) out.emplace_back();
    return out;
}

// 줄 바꿈 캐시 정리: 캐시 크기가 maxSize(기본 2 * kCapacity)를 넘을 때만 minKeepSeq 보다 오래된 항목을 지웁니다.
template <class Map, class KeepPred>
inline size_t pruneWrapCache(Map& cache, uint64_t minKeepSeq, size_t maxSize, KeepPred&& keepExtra) {
    if (cache.size() <= maxSize) return 0;
    size_t removed = 0;
    for (auto it = cache.begin(); it != cache.end(); ) {
        if (it->first < minKeepSeq && !keepExtra(it->first)) {
            it = cache.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

// ── 같은 메시지 묶기(Collapse) ───────────────────────────────────────────────
struct CollapseKey {
    uint16_t      category  = 0;
    ELogVerbosity verbosity = ELogVerbosity::Log;
    std::string   text;

    bool operator==(const CollapseKey& o) const {
        return category == o.category && verbosity == o.verbosity && text == o.text;
    }
};

struct CollapseKeyHash {
    size_t operator()(const CollapseKey& k) const {
        size_t h = std::hash<std::string>{}(k.text);
        h ^= (size_t)k.category * 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= (size_t)k.verbosity * 0x517cc1b727220a95ULL + (h << 6) + (h >> 2);
        return h;
    }
};

struct CollapsedEntry {
    uint16_t      category  = 0;
    ELogVerbosity verbosity = ELogVerbosity::Log;
    std::string   text;
    float         lastTime  = 0.f;
    uint64_t      firstSeq  = 0;
    uint64_t      lastSeq   = 0;
    uint32_t      count     = 1;
};

class CollapsedLogList {
public:
    void clear() {
        entries_.clear();
        indexByKey_.clear();
    }

    // 새 레코드를 증분으로 더합니다. 처음 나온 자리에 두고 시각 · lastSeq · count 만 갱신합니다.
    void push(const HLogRecord& r) {
        CollapseKey key{ r.category, r.verbosity, r.text };
        if (auto it = indexByKey_.find(key); it != indexByKey_.end()) {
            CollapsedEntry& e = entries_[it->second];
            ++e.count;
            e.lastTime = r.time;
            e.lastSeq  = r.seq;
            return;
        }
        const size_t idx = entries_.size();
        entries_.push_back(CollapsedEntry{ r.category, r.verbosity, r.text, r.time, r.seq, r.seq, 1 });
        indexByKey_.emplace(std::move(key), idx);
    }

    // 링 버퍼 사본에서 맨 앞 레코드가 밀려날 때 호출합니다.
    void evict(const HLogRecord& r) {
        CollapseKey key{ r.category, r.verbosity, r.text };
        auto it = indexByKey_.find(key);
        if (it == indexByKey_.end()) return;
        const size_t idx = it->second;
        if (entries_[idx].count > 1) {
            --entries_[idx].count;
            return;
        }
        indexByKey_.erase(it);
        entries_.erase(entries_.begin() + (ptrdiff_t)idx);
        for (size_t i = idx; i < entries_.size(); ++i) {
            CollapseKey k{ entries_[i].category, entries_[i].verbosity, entries_[i].text };
            indexByKey_[std::move(k)] = i;
        }
    }

    // firstSeq > seq 인 첫 원소의 인덱스(이분 탐색).
    size_t firstAfter(uint64_t seq) const {
        return (size_t)(std::upper_bound(entries_.begin(), entries_.end(), seq,
                                         [](uint64_t v, const CollapsedEntry& e) { return v < e.firstSeq; })
                        - entries_.begin());
    }

    const std::vector<CollapsedEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }

private:
    std::vector<CollapsedEntry>                        entries_;
    std::unordered_map<CollapseKey, size_t, CollapseKeyHash> indexByKey_;
};

// ── 설정 파일 직렬화(Config/editor_log.txt) ──────────────────────────────────
// v1(severity, hide) 과 v2(time_mode, collapse, word_wrap, clear_on_play) 모두 읽고, 모르는 줄은 무시합니다.
inline void parseLogOptions(std::istream& in, OutputLogOptions& opts, bool showVerb[5],
                            std::set<std::string>& hiddenNames) {
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key, a, b;
        ls >> key >> a >> b;
        ELogVerbosity v = ELogVerbosity::Off;
        if (key == "severity" && parseLogVerbosity(a, v) && v != ELogVerbosity::Off &&
            (b == "on" || b == "off")) {
            showVerb[(int)v] = (b == "on");
        } else if (key == "hide" && !a.empty()) {
            hiddenNames.insert(a);
        } else if (key == "time_mode") {
            ELogTimeMode tm = ELogTimeMode::Uptime;
            if (parseTimeMode(a, tm)) opts.timeMode = tm;
        } else if (key == "collapse" && (a == "on" || a == "off")) {
            opts.collapse = (a == "on");
        } else if (key == "word_wrap" && (a == "on" || a == "off")) {
            opts.wordWrap = (a == "on");
        } else if (key == "clear_on_play" && (a == "on" || a == "off")) {
            opts.clearOnPlay = (a == "on");
        }
    }
}

inline void formatLogOptions(std::ostream& out, const OutputLogOptions& opts, const bool showVerb[5],
                             const std::set<std::string>& hiddenNames) {
    constexpr ELogVerbosity kLogSeverities[] = {
        ELogVerbosity::Error, ELogVerbosity::Warning, ELogVerbosity::Log, ELogVerbosity::Verbose
    };
    out << "# Haruka Output Log display filter v2. Search text is not saved.\n";
    for (ELogVerbosity v : kLogSeverities)
        out << "severity " << logVerbosityName(v) << (showVerb[(int)v] ? " on\n" : " off\n");
    out << "time_mode "     << timeModeName(opts.timeMode) << '\n';
    out << "collapse "      << (opts.collapse    ? "on\n" : "off\n");
    out << "word_wrap "     << (opts.wordWrap    ? "on\n" : "off\n");
    out << "clear_on_play " << (opts.clearOnPlay ? "on\n" : "off\n");
    for (const std::string& name : hiddenNames)
        out << "hide " << name << '\n';
}

} // namespace hlogview
