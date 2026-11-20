#include "Accessibility.hpp"

#include "UserSettings.hpp"
#include "Utf8.hpp"

#include <algorithm>
#include <cmath>

AccessSettings AccessSettings::from(const UserSettings& s) {
    AccessSettings a;
    a.subtitles          = s.getBool("access.subtitles", false);
    a.subtitleSize       = std::clamp(s.getFloat("access.subtitleSize", 1.f), 0.5f, 3.f);
    a.subtitleBackground = std::clamp(s.getFloat("access.subtitleBackground", 0.6f), 0.f, 1.f);
    a.textScale          = std::clamp(s.getFloat("access.textScale", 1.f), 0.5f, 3.f);
    a.leftHanded         = s.getString("access.hand", "right") == "left";
    const std::string cf = s.getString("access.colorFilter", "none");
    a.colorFilter = cf == "protanopia" ? 1 : cf == "deuteranopia" ? 2 : cf == "tritanopia" ? 3 : 0;
    return a;
}

namespace haccess {

static float ramp(float x, float lo, float hi) {
    if (!(x > lo)) return 0.f;
    if (x >= hi) return 1.f;
    return (x - lo) / (hi - lo);
}

float vignetteTarget(float linearSpeed, float yawRateDeg, float strength) {
    if (!(strength > 0.f)) return 0.f;
    const float m = std::max(ramp(std::fabs(linearSpeed), kVignetteLinLo, kVignetteLinHi),
                             ramp(std::fabs(yawRateDeg), kVignetteYawLo, kVignetteYawHi));
    return std::clamp(strength, 0.f, 1.f) * m;
}

float vignetteStep(float current, float target, float dt) {
    if (!(dt > 0.f)) return current;
    const float tau = target > current ? kVignetteAttack : kVignetteRelease;
    const float k   = 1.f - std::exp(-dt / tau);
    float v = current + (target - current) * k;
    if (std::fabs(v - target) < 1e-3f) v = target;   // 끝에 닿으면 정확히 0 이 되어 셰이더 단계를 건너뜀
    return std::clamp(v, 0.f, 1.f);
}

float vignetteMask(float r, float v) {
    if (!(v > 0.f)) return 1.f;
    const float inner = 0.95f + (0.3f - 0.95f) * v;   // mix(0.95, 0.3, v)
    const float outer = inner + 0.45f;
    float t = std::clamp((r - inner) / (outer - inner), 0.f, 1.f);
    t = t * t * (3.f - 2.f * t);                          // smoothstep
    return 1.f - std::min(2.f * v, 1.f) * t;
}

float seatedLift(float standingEye, float seatedHead) {
    if (!(standingEye > 0.f) || !(seatedHead > 0.f)) return 0.f;
    return std::clamp(standingEye - seatedHead, 0.f, kMaxSeatedLift);
}

// glm::mat3 은 열 우선이라 행을 적은 표를 옮겨 넣습니다.
static glm::mat3 rows(float a, float b, float c, float d, float e, float f, float g, float h, float i) {
    return glm::transpose(glm::mat3(a, b, c, d, e, f, g, h, i));
}

glm::mat3 colorSimulation(int mode) {
    switch (mode) {
    case 1: return rows( 0.152286f, 1.052583f, -0.204868f,
                         0.114503f, 0.786281f,  0.099216f,
                        -0.003882f, -0.048116f, 1.051998f);
    case 2: return rows( 0.367322f, 0.860646f, -0.227968f,
                         0.280085f, 0.672501f,  0.047413f,
                        -0.011820f, 0.042940f,  0.968881f);
    case 3: return rows( 1.255528f, -0.076749f, -0.178779f,
                        -0.078411f,  0.930809f,  0.147602f,
                         0.004733f,  0.691367f,  0.303900f);
    default: return glm::mat3(1.f);
    }
}

glm::mat3 colorCorrection(int mode) {
    if (mode < 1 || mode > 3) return glm::mat3(1.f);
    // 오차 옮기기(Fidaner): 적록은 빨강 오차를 초록 · 파랑으로, 청황은 파랑 오차를 빨강 · 초록으로.
    const glm::mat3 shift = mode == 3 ? rows(1.f, 0.f, 0.7f,
                                             0.f, 1.f, 0.7f,
                                             0.f, 0.f, 0.f)
                                      : rows(0.f, 0.f, 0.f,
                                             0.7f, 1.f, 0.f,
                                             0.7f, 0.f, 1.f);
    const glm::mat3 I(1.f);
    return I + shift * (I - colorSimulation(mode));
}

float subtitleDuration(std::string_view text) {
    size_t i = 0, n = 0;
    while (i < text.size()) { utf8::next(text, i); ++n; }
    return std::clamp(kSubtitleMinSeconds + (float)n / kSubtitleCharsPerSec, kSubtitleMinSeconds, kSubtitleMaxSeconds);
}

std::vector<std::string> wrapText(const std::string& text, float maxWidth, int maxRows,
                                  const std::function<float(std::string_view)>& measure) {
    std::vector<std::string> out;
    if (maxRows < 1) return out;
    bool truncated = false;
    auto pushRow = [&](std::string row) {
        if ((int)out.size() >= maxRows) { truncated = true; return; }
        out.push_back(std::move(row));
    };
    size_t p = 0;
    while (p <= text.size() && !truncated) {
        size_t nl = text.find('\n', p);
        if (nl == std::string::npos) nl = text.size();
        const std::string_view para(text.data() + p, nl - p);
        std::string row;
        size_t w = 0;
        while (w < para.size()) {
            while (w < para.size() && para[w] == ' ') ++w;
            if (w >= para.size()) break;
            size_t e = para.find(' ', w);
            if (e == std::string_view::npos) e = para.size();
            std::string word(para.substr(w, e - w));
            w = e;
            const std::string cand = row.empty() ? word : row + " " + word;
            if (measure(cand) <= maxWidth) { row = cand; continue; }
            if (!row.empty()) { pushRow(row); row.clear(); }
            // 한 줄에 들어가지 않는 단어는 글자 단위로 자릅니다.
            while (!word.empty() && measure(word) > maxWidth) {
                size_t cut = 0, k = 0;
                while (k < word.size()) {
                    size_t nk = k;
                    utf8::next(word, nk);
                    if (cut > 0 && measure(std::string_view(word.data(), nk)) > maxWidth) break;
                    cut = k = nk;
                }
                if (cut == 0) cut = word.size();
                pushRow(word.substr(0, cut));
                word.erase(0, cut);
            }
            row = word;
        }
        pushRow(row);
        p = nl + 1;
    }
    if (truncated && !out.empty()) {
        std::string& last = out.back();
        static const std::string kEllipsis = "\xE2\x80\xA6";   // …
        while (!last.empty() && measure(last + kEllipsis) > maxWidth) last.erase(utf8::prevStart(last, last.size()));
        last += kEllipsis;
    }
    return out;
}

} // namespace haccess

void AccessibilityState::reportMotion(float linearSpeed, float yawRateDeg) {
    pendingLin_ = std::max(pendingLin_, std::fabs(linearSpeed));
    pendingYaw_ = std::max(pendingYaw_, std::fabs(yawRateDeg));
}

void AccessibilityState::tickVignette(float dt) {
    const float target = haccess::vignetteTarget(pendingLin_, pendingYaw_, vignetteStrength);
    vignette_ = vignetteStrength > 0.f ? haccess::vignetteStep(vignette_, target, dt) : 0.f;
    pendingLin_ = pendingYaw_ = 0.f;
}

float AccessibilityState::seatedLiftFor(bool seated, bool floorSpace, float standingEye, bool headValid, float headY) {
    if (!seated || !floorSpace) {
        seatedHead_  = 0.f;
        recalibrate_ = false;
        return 0.f;
    }
    if ((seatedHead_ <= 0.f || recalibrate_) && headValid && headY > 0.f) {
        seatedHead_  = headY;
        recalibrate_ = false;
    }
    return haccess::seatedLift(standingEye, seatedHead_);
}
