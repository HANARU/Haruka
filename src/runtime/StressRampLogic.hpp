#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hstress {

enum class StressAxis : uint8_t {
    Empty = 0,
    Draws,
    Instances,
    Tris,
    Lights,
    Overdraw,
    Particles,
    Panels,
};

struct StressRange {
    uint32_t start = 1;
    uint32_t step  = 1;
    uint32_t max   = 1;
};

struct StressSpec {
    bool       enabled        = false;
    StressAxis axis           = StressAxis::Empty;
    uint32_t   start          = 1;
    uint32_t   step           = 1;
    uint32_t   max            = 1;
    float      stepSeconds    = 3.0f;
    bool       stepSecondsSet = false;
    float      budgetMs       = 11.1f;
    bool       budgetMsSet    = false;

    float effectiveStepSeconds() const {
        if (stepSecondsSet) return stepSeconds;
        return axis == StressAxis::Empty ? 10.0f : stepSeconds;
    }
};

inline const char* axisName(StressAxis a) {
    switch (a) {
        case StressAxis::Empty:     return "empty";
        case StressAxis::Draws:     return "draws";
        case StressAxis::Instances: return "instances";
        case StressAxis::Tris:      return "tris";
        case StressAxis::Lights:    return "lights";
        case StressAxis::Overdraw:  return "overdraw";
        case StressAxis::Particles: return "particles";
        case StressAxis::Panels:    return "panels";
    }
    return "empty";
}

inline bool parseAxisName(std::string_view s, StressAxis& out) {
    if (s == "empty")     { out = StressAxis::Empty;     return true; }
    if (s == "draws")     { out = StressAxis::Draws;     return true; }
    if (s == "instances") { out = StressAxis::Instances; return true; }
    if (s == "tris")      { out = StressAxis::Tris;      return true; }
    if (s == "lights")    { out = StressAxis::Lights;    return true; }
    if (s == "overdraw")  { out = StressAxis::Overdraw;  return true; }
    if (s == "particles") { out = StressAxis::Particles; return true; }
    if (s == "panels")    { out = StressAxis::Panels;    return true; }
    return false;
}

inline StressRange defaultRange(StressAxis a) {
    switch (a) {
        case StressAxis::Empty:     return { 0,   1,   0     };
        case StressAxis::Draws:     return { 64,  64,  1024  };
        case StressAxis::Instances: return { 128, 128, 4096  };
        case StressAxis::Tris:      return { 8,   8,   96    };
        case StressAxis::Lights:    return { 8,   8,   128   };
        case StressAxis::Overdraw:  return { 4,   4,   64    };
        case StressAxis::Particles: return { 500, 500, 16000 };
        case StressAxis::Panels:    return { 4,   4,   64    };
    }
    return { 0, 1, 0 };
}

inline bool parseUintExact(std::string_view s, uint32_t& out) {
    if (s.empty()) return false;
    uint32_t v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end != s.data() + s.size()) return false;
    out = v;
    return true;
}

// "<axis>" 또는 "<axis>:<start>:<step>:<max>"
inline bool parseStressSpec(std::string_view text, StressSpec& out) {
    if (text.empty()) return false;
    const size_t c1 = text.find(':');
    const std::string_view axStr = (c1 == std::string_view::npos) ? text : text.substr(0, c1);
    StressAxis ax{};
    if (!parseAxisName(axStr, ax)) return false;

    StressRange r = defaultRange(ax);
    if (c1 != std::string_view::npos) {
        const std::string_view rest = text.substr(c1 + 1);
        const size_t c2 = rest.find(':');
        const size_t c3 = (c2 == std::string_view::npos) ? std::string_view::npos : rest.find(':', c2 + 1);
        if (c2 == std::string_view::npos || c3 == std::string_view::npos) return false;
        if (rest.find(':', c3 + 1) != std::string_view::npos) return false;
        uint32_t s0 = 0, st = 0, mx = 0;
        if (!parseUintExact(rest.substr(0, c2), s0) ||
            !parseUintExact(rest.substr(c2 + 1, c3 - c2 - 1), st) ||
            !parseUintExact(rest.substr(c3 + 1), mx))
            return false;
        if (st == 0 || mx < s0) return false;
        r = { s0, st, mx };
    }

    out.enabled = true;
    out.axis    = ax;
    out.start   = r.start;
    out.step    = r.step;
    out.max     = r.max;
    return true;
}

// 단계 일정: 모든 축이 먼저 n = 0(기준 단계)을 재고, 이후 start 부터 step 씩 증가합니다.
// 첫 discardSec(기본 1초)는 버리고, stepSec 도달 시 단계를 마칩니다.
// max(예산, 기준 ms) 의 130% 를 2단계 연속 넘거나 max 에 이르면 멈춥니다.
struct StressSchedule {
    static constexpr float kDiscardSec = 1.0f;
    static constexpr float kOverFactor = 1.3f;

    uint32_t currentN           = 0;
    float    baseGpuMs          = 0.f;
    bool     hasBase            = false;
    uint32_t consecutiveOver130 = 0;
    bool     finished           = false;

    void begin(const StressSpec&) {
        currentN           = 0;
        baseGpuMs          = 0.f;
        hasBase            = false;
        consecutiveOver130 = 0;
        finished           = false;
    }

    static bool inDiscardWindow(float elapsedSec, float stepSec) {
        const float discard = (stepSec > kDiscardSec) ? kDiscardSec : 0.f;
        return elapsedSec < discard;
    }

    static float stopThresholdMs(float budgetMs, float baseMs) {
        return std::max(budgetMs, baseMs) * kOverFactor;
    }

    // 단계가 끝났을 때 호출. 계속 진행하면 true(currentN 갱신), 멈추면 false(finished=true).
    bool advanceAfterStep(float gpuTotalMs, float budgetMs, const StressSpec& spec) {
        if (!hasBase && currentN == 0) {
            baseGpuMs = gpuTotalMs;
            hasBase   = true;
            if (spec.max == 0) {
                finished = true;
                return false;
            }
            currentN = (spec.start > 0) ? spec.start : std::min(spec.step, spec.max);
            return true;
        }

        const float limit = stopThresholdMs(budgetMs, baseGpuMs);
        if (gpuTotalMs > limit)
            ++consecutiveOver130;
        else
            consecutiveOver130 = 0;

        if (consecutiveOver130 >= 2 || currentN >= spec.max || currentN + spec.step > spec.max) {
            finished = true;
            return false;
        }
        currentN += spec.step;
        return true;
    }
};

struct StressStepRecord {
    uint32_t n          = 0;
    uint32_t tris       = 0;
    float    gpuTotalMs = 0.f;
};

struct StressRegression {
    bool   hasSlope     = false;
    double slopePerUnit = 0.0;    // 1 개당(tris 축은 1 삼각형당) ms
    float  unitCostMs   = 0.f;    // 100 개당(tris 축은 10만 삼각형당) ms
    bool   overAtBase   = false;
    bool   hasEstLimit  = false;
    double estLimit     = 0.0;    // 예산까지 남은 여유 ÷ 기울기
};

inline StressRegression computeRegression(const std::vector<StressStepRecord>& steps, StressAxis axis,
                                          float budgetMs, float baseMs) {
    StressRegression out;
    out.overAtBase = (baseMs > budgetMs);
    if (steps.size() < 2) return out;

    double sx = 0.0, sy = 0.0;
    for (const StressStepRecord& s : steps) {
        const double x = (axis == StressAxis::Tris) ? static_cast<double>(s.tris) : static_cast<double>(s.n);
        sx += x;
        sy += static_cast<double>(s.gpuTotalMs);
    }
    const double invN = 1.0 / static_cast<double>(steps.size());
    const double mx   = sx * invN;
    const double my   = sy * invN;

    double num = 0.0, den = 0.0;
    for (const StressStepRecord& s : steps) {
        const double x = (axis == StressAxis::Tris) ? static_cast<double>(s.tris) : static_cast<double>(s.n);
        const double dx = x - mx;
        const double dy = static_cast<double>(s.gpuTotalMs) - my;
        num += dx * dy;
        den += dx * dx;
    }
    if (den <= 1e-12) return out;

    out.hasSlope     = true;
    out.slopePerUnit = num / den;
    const double unitScale = (axis == StressAxis::Tris) ? 100000.0 : 100.0;
    out.unitCostMs   = static_cast<float>(out.slopePerUnit * unitScale);
    if (!out.overAtBase && out.slopePerUnit > 1e-9) {
        const double headroom = static_cast<double>(budgetMs) - static_cast<double>(baseMs);
        out.hasEstLimit = true;
        out.estLimit    = headroom / out.slopePerUnit;
    }
    return out;
}

struct KneePoint {
    bool     hasLastUnder = false;
    uint32_t lastUnderN   = 0;
    float    lastUnderMs  = 0.f;
    bool     hasFirstOver = false;
    uint32_t firstOverN   = 0;
    float    firstOverMs  = 0.f;
};

inline KneePoint computeKneePoint(const std::vector<StressStepRecord>& steps, float budgetMs) {
    KneePoint kp;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].gpuTotalMs > budgetMs) {
            kp.hasFirstOver = true;
            kp.firstOverN   = steps[i].n;
            kp.firstOverMs  = steps[i].gpuTotalMs;
            if (i > 0) {
                kp.hasLastUnder = true;
                kp.lastUnderN   = steps[i - 1].n;
                kp.lastUnderMs  = steps[i - 1].gpuTotalMs;
            }
            return kp;
        }
    }
    if (!steps.empty()) {
        kp.hasLastUnder = true;
        kp.lastUnderN   = steps.back().n;
        kp.lastUnderMs  = steps.back().gpuTotalMs;
    }
    return kp;
}

struct StressVec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

// 결정적 격자 배치: 카메라(origin, forward, right, up) 기준 3 ~ 8 m 앞 시야 안에 놓습니다.
inline StressVec3 placeStressGrid(uint32_t index, uint32_t count,
                                  StressVec3 origin, StressVec3 forward,
                                  StressVec3 right, StressVec3 up) {
    const uint32_t n      = std::max(1u, count);
    const uint32_t layers = std::min(4u, std::max(1u, (n + 15u) / 16u));
    const uint32_t perLayer = (n + layers - 1u) / layers;
    uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(perLayer))));
    cols = std::max(1u, cols);
    const uint32_t rows = std::max(1u, (perLayer + cols - 1u) / cols);

    const uint32_t layer = index / perLayer;
    const uint32_t rem   = index % perLayer;
    const uint32_t row   = rem / cols;
    const uint32_t col   = rem % cols;

    // 결정적 해시 지터(시각 무관)
    uint32_t h = index * 2654435761u ^ 0x5EED1234u;
    h ^= h >> 16;
    const float jx = (static_cast<float>(h & 0xFFu) / 255.f - 0.5f) * 0.08f;
    const float jy = (static_cast<float>((h >> 8) & 0xFFu) / 255.f - 0.5f) * 0.08f;

    const float tLayer = (layers <= 1u) ? 0.5f : static_cast<float>(layer) / static_cast<float>(layers - 1u);
    const float dist   = 3.2f + tLayer * 4.5f;   // 3.2 ~ 7.7 m

    const float u = (cols <= 1u) ? 0.f : (static_cast<float>(col) / static_cast<float>(cols - 1u) - 0.5f);
    const float v = (rows <= 1u) ? 0.f : (static_cast<float>(row) / static_cast<float>(rows - 1u) - 0.5f);
    const float halfSpanX = dist * 0.42f;
    const float halfSpanY = dist * 0.30f;

    const float rx = u * 2.f * halfSpanX + jx;
    const float uy = -v * 2.f * halfSpanY + jy;

    return {
        origin.x + forward.x * dist + right.x * rx + up.x * uy,
        origin.y + forward.y * dist + right.y * rx + up.y * uy,
        origin.z + forward.z * dist + right.z * rx + up.z * uy,
    };
}

} // namespace hstress
