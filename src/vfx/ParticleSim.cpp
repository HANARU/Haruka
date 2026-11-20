#include "ParticleSim.hpp"
#include "../core/TextFloat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace vfx {

namespace {
constexpr float kTwoPi = 6.28318531f;

template <class K, class V, class Get>
V sampleKeys(const std::vector<K>& keys, float t, V fallback, Get get) {
    if (keys.empty()) return fallback;
    if (!(t > keys.front().t)) return get(keys.front());
    if (t >= keys.back().t) return get(keys.back());
    for (size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].t) {
            const K& a = keys[i - 1];
            const K& b = keys[i];
            const float span = b.t - a.t;
            const float u = span > 0.f ? (t - a.t) / span : 1.f;
            return get(a) + (get(b) - get(a)) * u;
        }
    }
    return get(keys.back());
}

// "a b c; d e f" 를 키마다 숫자 n 개로 읽습니다. 빈 글자는 키 0개.
bool parseKeys(const std::string& s, size_t n, std::vector<std::vector<float>>& out) {
    out.clear();
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(';', start);
        if (end == std::string::npos) end = s.size();
        const std::string part = s.substr(start, end - start);
        if (part.find_first_not_of(" \t") != std::string::npos) {
            std::vector<float> nums;
            const char* p = part.c_str();
            for (;;) {
                while (*p == ' ' || *p == '\t') ++p;
                if (!*p) break;
                char* e = nullptr;
                const float v = std::strtof(p, &e);
                if (e == p || !std::isfinite(v)) return false;
                nums.push_back(v);
                p = e;
            }
            if (nums.size() != n) return false;
            out.push_back(std::move(nums));
        }
        start = end + 1;
    }
    return out.size() <= kMaxCurveKeys;
}
} // namespace

float sampleCurve(const std::vector<FloatKey>& keys, float t) {
    return sampleKeys(keys, t, 1.f, [](const FloatKey& k) { return k.v; });
}
glm::vec4 sampleCurve(const std::vector<ColorKey>& keys, float t) {
    return sampleKeys(keys, t, glm::vec4(1.f), [](const ColorKey& k) { return k.c; });
}

std::string curveToText(const std::vector<FloatKey>& keys) {
    std::string s;
    for (const FloatKey& k : keys) {
        if (!s.empty()) s += "; ";
        s += floatToText(k.t) + " " + floatToText(k.v);
    }
    return s;
}
std::string curveToText(const std::vector<ColorKey>& keys) {
    std::string s;
    for (const ColorKey& k : keys) {
        if (!s.empty()) s += "; ";
        s += floatToText(k.t) + " " + floatToText(k.c.r) + " " + floatToText(k.c.g) + " " + floatToText(k.c.b) + " " +
             floatToText(k.c.a);
    }
    return s;
}
bool curveFromText(const std::string& s, std::vector<FloatKey>& out) {
    std::vector<std::vector<float>> rows;
    if (!parseKeys(s, 2, rows)) return false;
    std::vector<FloatKey> keys;
    for (const auto& r : rows) keys.push_back({ std::clamp(r[0], 0.f, 1.f), r[1] });
    std::stable_sort(keys.begin(), keys.end(), [](const FloatKey& a, const FloatKey& b) { return a.t < b.t; });
    out = std::move(keys);
    return true;
}
bool curveFromText(const std::string& s, std::vector<ColorKey>& out) {
    std::vector<std::vector<float>> rows;
    if (!parseKeys(s, 5, rows)) return false;
    std::vector<ColorKey> keys;
    for (const auto& r : rows) {
        if (r[1] < 0.f || r[2] < 0.f || r[3] < 0.f || r[4] < 0.f || r[4] > 1.f) return false;
        keys.push_back({ std::clamp(r[0], 0.f, 1.f), glm::vec4(r[1], r[2], r[3], r[4]) });
    }
    std::stable_sort(keys.begin(), keys.end(), [](const ColorKey& a, const ColorKey& b) { return a.t < b.t; });
    out = std::move(keys);
    return true;
}

void EmitterDesc::clampValues() {
    rate         = std::clamp(std::isfinite(rate) ? rate : 0.f, 0.f, 100000.f);
    burst        = std::min(burst, kMaxParticles);
    duration     = std::clamp(std::isfinite(duration) ? duration : 1.f, 0.01f, 3600.f);
    maxParticles = std::clamp(maxParticles, 1u, kMaxParticles);
    lifeMin      = std::clamp(lifeMin, 0.01f, 600.f);
    lifeMax      = std::clamp(lifeMax, lifeMin, 600.f);
    speedMin     = std::clamp(speedMin, 0.f, 1000.f);
    speedMax     = std::clamp(speedMax, speedMin, 1000.f);
    coneAngle    = std::clamp(coneAngle, 0.f, 180.f);
    radius       = std::clamp(radius, 0.f, 1000.f);
    size         = std::clamp(size, 0.f, 100.f);
    spinMin      = std::clamp(spinMin, -3600.f, 3600.f);
    spinMax      = std::clamp(spinMax, spinMin, 3600.f);
    drag         = std::clamp(drag, 0.f, 100.f);
    if ((uint8_t)sprite >= kSpriteCount) sprite = Sprite::SoftCircle;
    if (sizeCurve.size() > kMaxCurveKeys)  sizeCurve.resize(kMaxCurveKeys);
    if (colorCurve.size() > kMaxCurveKeys) colorCurve.resize(kMaxCurveKeys);
}

void ParticleSim::reset(const EmitterDesc& d) {
    particles_.clear();
    rng_.reseed(d.seed);
    time_ = total_ = carry_ = 0.f;
    burstDone_ = false;
    serial_    = 0;
    dropped_   = 0;
}

void ParticleSim::spawn(const EmitterDesc& d, const glm::mat4& emitterWorld, uint32_t n) {
    const uint32_t cap = std::min(d.maxParticles, kMaxParticles);
    const float cosA = std::cos(glm::radians(d.coneAngle));
    for (uint32_t i = 0; i < n; ++i) {
        if (particles_.size() >= cap) { dropped_ += n - i; return; }
        // 난수는 파티클마다 같은 수만큼 같은 순서로 씁니다(D3).
        Particle p;
        p.life = rng_.range(d.lifeMin, d.lifeMax);
        const float speed = rng_.range(d.speedMin, d.speedMax);
        glm::vec3 dir, offset;
        if (d.shape == Shape::Sphere) {
            dir = rng_.unitVector();
            const float r = d.radius * std::cbrt(rng_.nextFloat());
            offset = rng_.unitVector() * r;
        } else {
            const float c   = rng_.range(cosA, 1.f);
            const float s   = std::sqrt(std::max(0.f, 1.f - c * c));
            const float phi = rng_.range(0.f, kTwoPi);
            dir = { s * std::cos(phi), c, s * std::sin(phi) };
            const glm::vec2 disk = rng_.inDisk() * d.radius;
            offset = { disk.x, 0.f, disk.y };
            (void)rng_.nextFloat();   // 구와 같은 난수 수(파티클마다 9개)
        }
        p.rot  = rng_.range(0.f, kTwoPi);
        p.spin = glm::radians(rng_.range(d.spinMin, d.spinMax));
        if (d.space == Space::World) {
            p.pos = glm::vec3(emitterWorld * glm::vec4(offset, 1.f));
            glm::vec3 w = glm::mat3(emitterWorld) * dir;
            const float len = glm::length(w);
            p.vel = len > 1e-12f ? w * (speed / len) : glm::vec3(0.f, speed, 0.f);
        } else {
            p.pos = offset;
            p.vel = dir * speed;
        }
        p.serial = serial_++;
        particles_.push_back(p);
    }
}

void ParticleSim::step(const EmitterDesc& d, float dt, const glm::mat4& emitterWorld, bool emitting) {
    if (!(dt > 0.f)) dt = 0.f;
    dt = std::min(dt, kMaxStepDt);

    // 1. 갱신: 나이 → 제거(순서 유지) → 힘 · 감쇠 → 이동 → 회전. 난수를 쓰지 않습니다.
    if (dt > 0.f && !particles_.empty()) {
        glm::vec3 g = d.gravity;
        if (d.space == Space::Local) {
            const glm::mat3 m(emitterWorld);
            if (std::fabs(glm::determinant(m)) > 1e-12f) g = glm::inverse(m) * g;   // 월드 중력을 로컬로(D6)
        }
        const float damp = d.drag > 0.f ? std::exp(-d.drag * dt) : 1.f;
        size_t w = 0;
        for (size_t i = 0; i < particles_.size(); ++i) {
            Particle p = particles_[i];
            p.age += dt;
            if (p.age >= p.life) continue;
            p.vel  = (p.vel + g * dt) * damp;
            p.pos += p.vel * dt;
            p.rot += p.spin * dt;
            particles_[w++] = p;
        }
        particles_.resize(w);
    }

    // 2. 생성: 주기 시작의 터뜨리기 + 비율(소수 부분은 다음 틱으로).
    total_ += dt;
    if (!emitting) return;
    if (!d.loop && time_ >= d.duration) return;
    if (!burstDone_) {
        burstDone_ = true;
        if (d.burst) spawn(d, emitterWorld, d.burst);
    }
    float active = dt;
    if (!d.loop) active = std::min(dt, d.duration - time_);
    time_ += dt;
    if (d.rate > 0.f && active > 0.f) {
        carry_ += d.rate * active;
        const float whole = std::floor(carry_);
        carry_ -= whole;
        if (whole > 0.f) spawn(d, emitterWorld, (uint32_t)std::min(whole, (float)kMaxParticles));
    }
    if (d.loop && time_ >= d.duration) {
        time_ = std::fmod(time_, d.duration);
        burstDone_ = false;   // 다음 틱에 다시 터뜨림
    }
}

void ParticleSim::buildInstances(const EmitterDesc& d, const glm::mat4& emitterWorld,
                                 std::vector<ParticleInstance>& out) const {
    out.resize(particles_.size());
    const bool local = d.space == Space::Local;
    const float cell = (float)(uint8_t)d.sprite;
    for (size_t i = 0; i < particles_.size(); ++i) {
        const Particle& p = particles_[i];
        const float t = p.life > 0.f ? std::min(p.age / p.life, 1.f) : 1.f;
        const glm::vec3 pos = local ? glm::vec3(emitterWorld * glm::vec4(p.pos, 1.f)) : p.pos;
        ParticleInstance& o = out[i];
        o.posSize = glm::vec4(pos, d.size * sampleCurve(d.sizeCurve, t));
        o.color   = sampleCurve(d.colorCurve, t);
        o.params  = glm::vec4(p.rot, cell, 0.f, 0.f);
    }
}

void buildSpriteAtlas(std::vector<uint8_t>& rgba) {
    constexpr int cell = kAtlasSize / 2;
    rgba.assign((size_t)kAtlasSize * kAtlasSize * 4, 255);   // RGB 는 어디나 흰색(밉이 가장자리를 어둡게 하지 않게)
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 0;
    auto smooth = [](float e0, float e1, float x) {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    for (int c = 0; c < (int)kSpriteCount; ++c) {
        const int ox = (c & 1) * cell, oy = (c >> 1) * cell;
        for (int y = 2; y < cell - 2; ++y) {
            for (int x = 2; x < cell - 2; ++x) {
                // 칸 안 좌표 [-1, 1] (텍셀 가운데), 가장자리 2 텍셀은 비움
                const float u = ((float)x + 0.5f) / (float)cell * 2.f - 1.f;
                const float v = ((float)y + 0.5f) / (float)cell * 2.f - 1.f;
                const float r = std::sqrt(u * u + v * v);
                float a = 0.f;
                switch ((Sprite)c) {
                    case Sprite::SoftCircle: { const float k = std::max(0.f, 1.f - r * r); a = k * k; break; }
                    case Sprite::Circle:     a = 1.f - smooth(0.82f, 0.9f, r); break;
                    case Sprite::Ring:       a = smooth(0.5f, 0.6f, r) * (1.f - smooth(0.78f, 0.88f, r)); break;
                    case Sprite::Star: {
                        // 가운데 빛 + 가로 · 세로 빛줄기
                        const float core  = std::exp(-r * r * 18.f);
                        const float beamX = std::exp(-v * v * 900.f) * std::max(0.f, 1.f - std::fabs(u));
                        const float beamY = std::exp(-u * u * 900.f) * std::max(0.f, 1.f - std::fabs(v));
                        a = std::min(1.f, core + 0.8f * (beamX + beamY));
                        break;
                    }
                }
                uint8_t* px = &rgba[((size_t)(oy + y) * kAtlasSize + (size_t)(ox + x)) * 4];
                px[3] = (uint8_t)std::lround(std::clamp(a, 0.f, 1.f) * 255.f);
            }
        }
    }
}

void sortBackToFront(ParticleInstance* inst, size_t n, const glm::vec3& eye, SortScratch& s) {
    if (n < 2) return;
    s.k0.resize(n); s.k1.resize(n); s.i0.resize(n); s.i1.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 d = glm::vec3(inst[i].posSize) - eye;
        float q = glm::dot(d, d);
        if (!(q >= 0.f)) q = 0.f;   // NaN 은 가장 가까운 것으로
        uint32_t bits = 0;
        std::memcpy(&bits, &q, sizeof(bits));   // 0 이상 float 는 비트 순서 = 크기 순서
        s.k0[i] = ~bits;                         // 오름차순 정렬이 먼 것부터가 되게
        s.i0[i] = (uint32_t)i;
    }
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        uint32_t count[257] = {};
        for (size_t i = 0; i < n; ++i) ++count[((s.k0[i] >> shift) & 255u) + 1];
        bool single = false;   // 모두 같은 칸이면 이 자리는 건너뜀
        for (uint32_t b = 1; b <= 256; ++b) if (count[b] == n) { single = true; break; }
        if (single) continue;
        for (uint32_t b = 1; b <= 256; ++b) count[b] += count[b - 1];
        for (size_t i = 0; i < n; ++i) {
            const uint32_t dst = count[(s.k0[i] >> shift) & 255u]++;
            s.k1[dst] = s.k0[i];
            s.i1[dst] = s.i0[i];
        }
        s.k0.swap(s.k1);
        s.i0.swap(s.i1);
    }
    s.tmp.assign(inst, inst + n);
    for (size_t i = 0; i < n; ++i) inst[i] = s.tmp[s.i0[i]];
}

} // namespace vfx
