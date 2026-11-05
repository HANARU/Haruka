#include "EngineOptions.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace {

// 앞뒤 공백 없는 양의 정수 전체여야 합니다.
bool parseUint(const std::string& s, uint32_t& out) {
    if (s.empty()) return false;
    uint32_t v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end != s.data() + s.size()) return false;
    out = v;
    return true;
}

// 실수는 strtof 로 읽습니다(Android NDK 의 libc++ 는 실수 from_chars 가 없는 판이 있음). 문자열 전체여야 합니다.
bool parsePositiveFloat(const std::string& s, float& out) {
    if (s.empty() || std::isspace((unsigned char)s[0])) return false;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v) || v <= 0.f) return false;
    out = v;
    return true;
}

// "WxH". 한 변은 16 ~ 8192 입니다.
bool parseSize(const std::string& s, uint32_t& w, uint32_t& h) {
    const size_t x = s.find('x');
    if (x == std::string::npos) return false;
    uint32_t a = 0, b = 0;
    if (!parseUint(s.substr(0, x), a) || !parseUint(s.substr(x + 1), b)) return false;
    if (a < 16 || b < 16 || a > 8192 || b > 8192) return false;
    w = a;
    h = b;
    return true;
}

// "--name=value" 이면 value 를 담고 true.
bool valueOf(const std::string& a, const char* name, std::string& value) {
    const std::string prefix = std::string(name) + "=";
    if (a.compare(0, prefix.size(), prefix) != 0) return false;
    value = a.substr(prefix.size());
    return true;
}

} // namespace

namespace hopt {

EngineOptions parse(const std::vector<std::string>& args, std::vector<std::string>& unknown) {
    EngineOptions o;
    for (const std::string& a : args) {
        std::string v;
        if      (a == "--xr")             o.xr            = true;
        else if (a == "--no-xr")          o.xr            = false;
        else if (a == "--emulate-stereo") o.emulateStereo = true;
        else if (a == "--loose")          o.loose         = true;
        else if (a == "--headless")       o.headless      = true;
        else if (a == "--synthetic-eyes") o.syntheticEyes = true;
        else if (a == "--vk-no-sync2")    o.vkNoSync2     = true;
        else if (a == "--vk-dedicated-msaa") o.vkDedicatedMsaa = true;
        else if (a == "--ssao")           o.ssao          = true;
        else if (a == "--no-ssao")        o.ssao          = false;
        else if (valueOf(a, "--synthetic-eyes", v)) {
            if (parseSize(v, o.syntheticEyeW, o.syntheticEyeH)) o.syntheticEyes = true;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--frames", v)) {
            uint32_t n = 0;
            if (parseUint(v, n) && n > 0) o.frames = n;
            else unknown.push_back(a);
        }
        else if (valueOf(a, "--timeout", v)) {
            if (!parsePositiveFloat(v, o.timeoutSec)) unknown.push_back(a);
        }
        else if (valueOf(a, "--capture", v)) {
            if (!v.empty()) o.capturePath = v;
            else unknown.push_back(a);
        }
        else unknown.push_back(a);
    }
    return o;
}

} // namespace hopt
