#include "AudioMix.hpp"

#include "../core/FieldParse.hpp"
#include "../core/TextFloat.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace hmix {

// ── 스냅숏 ──────────────────────────────────────────────────────────────────────
bool stepSnapshot(ActiveSnapshot& a, const Snapshot& s, float dt) {
    if (a.on) {
        a.weight = s.fadeIn > 0.f ? std::min(1.f, a.weight + dt / s.fadeIn) : 1.f;
        return false;
    }
    a.weight = s.fadeOut > 0.f ? std::max(0.f, a.weight - dt / s.fadeOut) : 0.f;
    return a.weight <= 0.f;
}

void blendSnapshots(const std::vector<Snapshot>& defs, const std::vector<ActiveSnapshot>& active, std::vector<float>& out) {
    std::fill(out.begin(), out.end(), 1.f);
    std::vector<const ActiveSnapshot*> order;
    order.reserve(active.size());
    for (const ActiveSnapshot& a : active)
        if (a.snapshot >= 0 && a.snapshot < (int)defs.size() && a.weight > 0.f) order.push_back(&a);
    std::stable_sort(order.begin(), order.end(), [&](const ActiveSnapshot* x, const ActiveSnapshot* y) {
        const int px = defs[x->snapshot].priority, py = defs[y->snapshot].priority;
        return px != py ? px < py : x->order < y->order;
    });
    for (const ActiveSnapshot* a : order)
        for (const auto& [bus, vol] : defs[a->snapshot].volumes)
            if (bus >= 0 && bus < (int)out.size()) {
                float& v = out[bus];
                v = a->weight >= 1.f ? vol : v + (vol - v) * a->weight;   // 무게 1 이면 정확히 그 값
            }
}

// ── 더킹 ────────────────────────────────────────────────────────────────────────
float stepDuck(DuckState& st, const DuckRule& r, float sourcePeak, float dt) {
    const float threshold = std::pow(10.f, r.thresholdDb / 20.f);
    if (sourcePeak > threshold) {
        st.holdLeft = r.hold;
        st.env = r.attack > 0.f ? std::min(1.f, st.env + dt / r.attack) : 1.f;
    } else if (st.holdLeft > 0.f) {
        st.holdLeft -= dt;
        if (st.holdLeft < 1e-5f) st.holdLeft = 0.f;   // 0.2 − 0.05 × 4 가 부동소수로 남아 한 틱 더 유지되지 않게
    } else {
        st.env = r.release > 0.f ? std::max(0.f, st.env - dt / r.release) : 0.f;
    }
    return duckGain(st, r);
}

// ── 피크 리미터 ─────────────────────────────────────────────────────────────────
float limiterReleaseCoef(float releaseSec, uint32_t sampleRate) {
    if (!(releaseSec > 0.f) || sampleRate == 0) return 1.f;
    return 1.f - std::exp(-1.f / (releaseSec * (float)sampleRate));
}

void limitBlock(float* io, uint32_t frames, uint32_t channels, float ceiling, float releaseCoef, LimiterState& st) {
    if (!(ceiling > 0.f)) ceiling = 0.f;
    float g = st.gain;
    for (uint32_t i = 0; i < frames; ++i) {
        float* f = io + (size_t)i * channels;
        float p = 0.f;
        for (uint32_t c = 0; c < channels; ++c) p = std::max(p, std::fabs(f[c]));
        // 이 프레임에 필요한 게인. 넘으면 바로 그 값으로(즉시 공격), 아니면 1 을 향해 천천히(해제).
        const float need = p > ceiling ? (p > 0.f ? ceiling / p : 1.f) : 1.f;
        if (need < g) g = need;
        else          g += (1.f - g) * releaseCoef;
        if (g > 1.f) g = 1.f;
        if (g < 1.f || p > ceiling)
            for (uint32_t c = 0; c < channels; ++c) f[c] = std::clamp(f[c] * g, -ceiling, ceiling);
    }
    st.gain = g;
}

// ── 랜덤 ────────────────────────────────────────────────────────────────────────
uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

float uniform01(uint64_t& state) {
    return (float)(splitmix64(state) >> 40) * (1.f / 16777216.f);   // 24비트 → [0, 1)
}

uint64_t fnv1a64(std::string_view s) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (unsigned char ch : s) { h ^= ch; h *= 0x100000001B3ull; }
    return h;
}

// ── 사운드 큐 ───────────────────────────────────────────────────────────────────
namespace {

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return std::string(s.substr(b, e - b));
}

// "최소 [최대]" 를 읽습니다. 하나면 둘 다 그 값.
bool readRange(const std::string& v, float lo, float hi, float& outMin, float& outMax) {
    std::istringstream is(v);
    std::string a, b, extra;
    is >> a >> b >> extra;
    float x = 0.f, y = 0.f;
    if (a.empty() || !extra.empty() || !fieldparse::toFloat(a, x)) return false;
    if (b.empty()) y = x;
    else if (!fieldparse::toFloat(b, y)) return false;
    if (!(x >= lo && x <= hi && y >= lo && y <= hi) || y < x) return false;
    outMin = x;
    outMax = y;
    return true;
}

} // namespace

bool parseCue(std::string_view text, const std::string& cueDir, SoundCue& out, std::vector<std::string>& messages) {
    out = SoundCue{};
    size_t pos = 0;
    int lineNo = 0;
    bool sawHeader = false;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        const std::string line = trim(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        const std::string where = "line " + std::to_string(lineNo) + ": ";
        if (!sawHeader) {
            sawHeader = true;
            if (line.rfind("HCUE", 0) == 0) {
                if (trim(line.substr(4)) != "1") messages.push_back(where + "unknown cue version '" + line + "' (reading as version 1)");
                continue;
            }
            messages.push_back(where + "missing 'HCUE 1' header");
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { messages.push_back(where + "expected key = value"); continue; }
        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::string val = trim(std::string_view(line).substr(eq + 1));
        bool ok = true;
        if (key == "variant") {
            SoundCue::Variant var;
            std::string path = val;
            const size_t sp = val.find_last_of(" \t");
            float w = 1.f;
            if (sp != std::string::npos && fieldparse::toFloat(val.substr(sp + 1), w)) path = trim(std::string_view(val).substr(0, sp));
            else w = 1.f;
            if (path.empty() || !(w >= 0.f) || !std::isfinite(w)) ok = false;
            else {
                var.path   = (path[0] == '/' || cueDir.empty()) ? path : cueDir + "/" + path;
                var.weight = w;
                out.variants.push_back(var);
            }
        } else if (key == "bus") {
            ok = !val.empty();
            if (ok) out.bus = val;
        } else if (key == "volume") {
            ok = readRange(val, 0.f, SoundCue::kMaxVolume, out.volumeMin, out.volumeMax);
        } else if (key == "pitch") {
            ok = readRange(val, SoundCue::kMinPitch, SoundCue::kMaxPitch, out.pitchMin, out.pitchMax);
        } else if (key == "maxInstances") {
            int n = 0;
            ok = fieldparse::toInt(val, n) && n >= 0 && n <= 256;
            if (ok) out.maxInstances = n;
        } else if (key == "overflow") {
            if      (val == "stopOldest") out.overflow = ECueOverflow::StopOldest;
            else if (val == "rejectNew")  out.overflow = ECueOverflow::RejectNew;
            else ok = false;
        } else if (key == "minInterval") {
            float t = 0.f;
            ok = fieldparse::toFloat(val, t) && t >= 0.f && t <= 60.f;
            if (ok) out.minInterval = t;
        } else if (key == "seed") {
            char* e = nullptr;
            const unsigned long long s = std::strtoull(val.c_str(), &e, 10);
            ok = e != val.c_str() && fieldparse::onlySpaceFrom(e);
            if (ok) { out.seed = s; out.hasSeed = true; }
        } else {
            messages.push_back(where + "unknown key '" + key + "'");
            continue;
        }
        if (!ok) messages.push_back(where + "bad value for " + key + ": '" + val + "'");
    }
    if (out.variants.empty()) {
        messages.push_back("no variant lines");
        return false;
    }
    return true;
}

std::string cueToText(const SoundCue& c) {
    std::string t = "HCUE 1\n";
    for (const auto& v : c.variants) t += "variant = " + v.path + " " + floatToText(v.weight) + "\n";
    if (!c.bus.empty()) t += "bus = " + c.bus + "\n";
    t += "volume = " + floatToText(c.volumeMin) + " " + floatToText(c.volumeMax) + "\n";
    t += "pitch = " + floatToText(c.pitchMin) + " " + floatToText(c.pitchMax) + "\n";
    t += "maxInstances = " + std::to_string(c.maxInstances) + "\n";
    t += std::string("overflow = ") + (c.overflow == ECueOverflow::RejectNew ? "rejectNew" : "stopOldest") + "\n";
    t += "minInterval = " + floatToText(c.minInterval) + "\n";
    if (c.hasSeed) t += "seed = " + std::to_string(c.seed) + "\n";
    return t;
}

CuePick pickCue(const SoundCue& c, uint64_t& rng) {
    CuePick p;
    double total = 0.0;
    for (const auto& v : c.variants) total += v.weight;
    const float r = uniform01(rng);
    if (total > 0.0) {
        double acc = 0.0;
        const double x = (double)r * total;
        for (size_t i = 0; i < c.variants.size(); ++i) {
            acc += c.variants[i].weight;
            if (c.variants[i].weight > 0.f && (x < acc || i + 1 == c.variants.size())) { p.variant = (int)i; break; }
        }
        // 맨 끝 변형의 가중치가 0 인 채로 끝까지 왔으면 가중치가 있는 마지막 것
        if (p.variant < 0)
            for (int i = (int)c.variants.size() - 1; i >= 0; --i)
                if (c.variants[i].weight > 0.f) { p.variant = i; break; }
    }
    const float rv = uniform01(rng);
    const float rp = uniform01(rng);
    p.volume = c.volumeMin + (c.volumeMax - c.volumeMin) * rv;
    p.pitch  = c.pitchMin  + (c.pitchMax  - c.pitchMin)  * rp;
    return p;
}

} // namespace hmix
