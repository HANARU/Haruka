#include "AudioAttenuation.hpp"
#include "../core/TextFloat.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct ModelName { EAudioAttenuation model; const char* name; };

constexpr std::array<ModelName, 3> kModelNames = {{
    { EAudioAttenuation::Inverse,     "inverse"     },
    { EAudioAttenuation::Linear,      "linear"      },
    { EAudioAttenuation::Exponential, "exponential" },
}};

// 토큰 하나 전체가 유한한 실수일 때만 true 입니다. strtof 는 앞쪽만 읽고 멈추므로
// 끝 위치까지 확인해야 "1x" 같은 값이 통과하지 않습니다.
bool parseFloatToken(const std::string& tok, float& out) {
    if (tok.empty()) return false;
    char* end = nullptr;
    const float v = std::strtof(tok.c_str(), &end);
    if (end != tok.c_str() + tok.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

} // namespace

const char* AudioAttenuationSettings::modelName(EAudioAttenuation m) {
    for (const ModelName& e : kModelNames)
        if (e.model == m) return e.name;
    return "inverse";
}

bool AudioAttenuationSettings::modelFromName(std::string_view name, EAudioAttenuation& out) {
    for (const ModelName& e : kModelNames)
        if (name == e.name) { out = e.model; return true; }
    return false;
}

std::string AudioAttenuationSettings::toText() const {
    return std::string(spatialize ? "1" : "0") + " " + modelName(model) + " "
         + floatToText(minDistance) + " " + floatToText(maxDistance) + " " + floatToText(rolloff);
}

bool AudioAttenuationSettings::fromText(std::string_view text, AudioAttenuationSettings& out) {
    // 공백으로 나눕니다. 손으로 고친 파일을 위해 공백이 여러 개여도 받아들입니다.
    std::vector<std::string> toks;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
        const size_t b = i;
        while (i < text.size() && !(text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
        if (i > b) toks.emplace_back(text.substr(b, i - b));
    }
    // 새 값은 이 줄을 늘리지 않고 다른 키로 추가합니다. 그래서 토큰 수를 정확히 요구합니다.
    if (toks.size() != 5) return false;

    AudioAttenuationSettings s;
    if      (toks[0] == "1") s.spatialize = true;
    else if (toks[0] == "0") s.spatialize = false;
    else return false;
    if (!modelFromName(toks[1], s.model)) return false;
    if (!parseFloatToken(toks[2], s.minDistance)) return false;
    if (!parseFloatToken(toks[3], s.maxDistance)) return false;
    if (!parseFloatToken(toks[4], s.rolloff))     return false;
    if (!(s.minDistance > 0.f) || !(s.maxDistance > s.minDistance) || !(s.rolloff >= 0.f)) return false;

    out = s;
    return true;
}
