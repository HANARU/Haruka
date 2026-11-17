#include "AudioAttenuation.hpp"
#include "AudioPropagation.hpp"
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

// 공백으로 나눕니다. 손으로 고친 파일을 위해 공백이 여러 개여도 받아들입니다.
std::vector<std::string> splitTokens(std::string_view text) {
    std::vector<std::string> toks;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
        const size_t b = i;
        while (i < text.size() && !(text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
        if (i > b) toks.emplace_back(text.substr(b, i - b));
    }
    return toks;
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
    const std::vector<std::string> toks = splitTokens(text);
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

// ── 차폐 설정 (2026-11-06) ───────────────────────────────────────────────────────

std::string AudioOcclusionSettings::toText() const {
    return std::string(enabled ? "1" : "0") + " " + floatToText(lowPassHz) + " " + floatToText(volume) + " "
         + floatToText(smoothTime) + " " + floatToText(radius);
}

bool AudioOcclusionSettings::fromText(std::string_view text, AudioOcclusionSettings& out) {
    const std::vector<std::string> toks = splitTokens(text);
    if (toks.size() != 5) return false;

    AudioOcclusionSettings s;
    if      (toks[0] == "1") s.enabled = true;
    else if (toks[0] == "0") s.enabled = false;
    else return false;
    if (!parseFloatToken(toks[1], s.lowPassHz))  return false;
    if (!parseFloatToken(toks[2], s.volume))     return false;
    if (!parseFloatToken(toks[3], s.smoothTime)) return false;
    if (!parseFloatToken(toks[4], s.radius))     return false;
    if (!(s.lowPassHz >= kMinHz && s.lowPassHz <= kMaxHz))     return false;
    if (!(s.volume >= 0.f && s.volume <= 1.f))                return false;
    if (!(s.smoothTime >= 0.f && s.smoothTime <= kMaxSmooth)) return false;
    if (!(s.radius >= 0.f && s.radius <= kMaxRadius))         return false;

    out = s;
    return true;
}

// ── 차폐 추가 값 · 전파 설정 (2026-11-17, S1 · S2 · S4 · S5) ──────────────────────────────

std::string AudioOcclusionSettings::extraToText() const {
    return floatToText(fullThickness) + " " + (rotateSamples ? "1" : "0");
}

bool AudioOcclusionSettings::extraFromText(std::string_view text) {
    const std::vector<std::string> toks = splitTokens(text);
    if (toks.size() != 2) return false;
    float th = 0.f;
    if (!parseFloatToken(toks[0], th) || !(th >= 0.f && th <= kMaxThickness)) return false;
    bool rot = false;
    if      (toks[1] == "1") rot = true;
    else if (toks[1] == "0") rot = false;
    else return false;
    fullThickness = th;
    rotateSamples = rot;
    return true;
}

std::string AudioPropagationSettings::toText() const {
    return floatToText(dopplerFactor) + " " + (airAbsorption ? "1" : "0") + " " + floatToText(airAbsorptionHz);
}

bool AudioPropagationSettings::fromText(std::string_view text, AudioPropagationSettings& out) {
    const std::vector<std::string> toks = splitTokens(text);
    if (toks.size() != 3) return false;
    AudioPropagationSettings s;
    if (!parseFloatToken(toks[0], s.dopplerFactor) || !(s.dopplerFactor >= 0.f && s.dopplerFactor <= kMaxDoppler)) return false;
    if      (toks[1] == "1") s.airAbsorption = true;
    else if (toks[1] == "0") s.airAbsorption = false;
    else return false;
    if (!parseFloatToken(toks[2], s.airAbsorptionHz) || !(s.airAbsorptionHz >= kMinHz && s.airAbsorptionHz <= kMaxHz)) return false;
    out = s;
    return true;
}

std::string AudioConeSettings::toText() const {
    return floatToText(innerDeg) + " " + floatToText(outerDeg) + " " + floatToText(outerGain);
}

bool AudioConeSettings::fromText(std::string_view text, AudioConeSettings& out) {
    const std::vector<std::string> toks = splitTokens(text);
    if (toks.size() != 3) return false;
    AudioConeSettings c;
    if (!parseFloatToken(toks[0], c.innerDeg) || !parseFloatToken(toks[1], c.outerDeg) || !parseFloatToken(toks[2], c.outerGain))
        return false;
    if (!(c.innerDeg >= 0.f && c.innerDeg <= 360.f) || !(c.outerDeg >= c.innerDeg && c.outerDeg <= 360.f) ||
        !(c.outerGain >= 0.f && c.outerGain <= 1.f))
        return false;
    out = c;
    return true;
}
