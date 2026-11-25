#pragma once

#include <cstdint>
#include <string_view>

namespace hxr {

enum class XrPerfLevel : uint8_t {
    Unset = 0,
    PowerSavings,
    SustainedLow,
    SustainedHigh,
    Boost,
};

struct XrPerfLevelPair {
    XrPerfLevel cpu = XrPerfLevel::Unset;
    XrPerfLevel gpu = XrPerfLevel::Unset;

    bool set() const {
        return cpu != XrPerfLevel::Unset && gpu != XrPerfLevel::Unset;
    }
};

inline const char* perfLevelName(XrPerfLevel lv) {
    switch (lv) {
        case XrPerfLevel::PowerSavings:  return "power_savings";
        case XrPerfLevel::SustainedLow:  return "sustained_low";
        case XrPerfLevel::SustainedHigh: return "sustained_high";
        case XrPerfLevel::Boost:         return "boost";
        case XrPerfLevel::Unset:         return "unset";
    }
    return "unset";
}

inline bool parsePerfLevel(std::string_view s, XrPerfLevel& out) {
    if (s == "power_savings")  { out = XrPerfLevel::PowerSavings;  return true; }
    if (s == "sustained_low")  { out = XrPerfLevel::SustainedLow;  return true; }
    if (s == "sustained_high") { out = XrPerfLevel::SustainedHigh; return true; }
    if (s == "boost")          { out = XrPerfLevel::Boost;         return true; }
    return false;
}

// "<cpu>:<gpu>"
inline bool parsePerfLevelPair(std::string_view s, XrPerfLevelPair& out) {
    const size_t colon = s.find(':');
    if (colon == std::string_view::npos) return false;
    if (s.find(':', colon + 1) != std::string_view::npos) return false;
    XrPerfLevel c{}, g{};
    if (!parsePerfLevel(s.substr(0, colon), c) || !parsePerfLevel(s.substr(colon + 1), g))
        return false;
    out.cpu = c;
    out.gpu = g;
    return true;
}

} // namespace hxr
