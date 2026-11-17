#pragma once

// ── 시드 난수와 노이즈 (B4, 2026-11-17) ─────────────────────────────────────────────
// 같은 시드면 어느 기기 · 컴파일러에서나 같은 수열이 나오는 난수(PCG32), 해시, 값 · 그라디언트(Perlin) 노이즈, 프랙탈 합(fBm),
// 블루 노이즈 점 뿌리기(Bridson 포아송 원판)입니다. std::mt19937 은 같은 수열을 주지만 std::uniform_real_distribution 은 표준
// 라이브러리마다 결과가 달라 절차 생성의 재현에 쓰지 않습니다.
//
// 참고: O'Neill "PCG"(2014, pcg32 XSH-RR), Perlin "Improving Noise"(2002, 5차 보간), Bridson "Fast Poisson Disk Sampling"(2007).

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hmath {

// ── PCG32 ───────────────────────────────────────────────────────────────────────
class Pcg32 {
public:
    explicit Pcg32(uint64_t seed = 0x853c49e6748fea9bull, uint64_t stream = 0xda3e39cb94b95bdbull) { reseed(seed, stream); }
    void reseed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbull) {
        state_ = 0u;
        inc_   = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }
    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        const uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }
    // [0, bound) 균등 정수(편향 없는 거절 표본, O'Neill).
    uint32_t nextBelow(uint32_t bound) {
        if (bound == 0) return 0;
        const uint32_t threshold = (0u - bound) % bound;
        for (;;) {
            const uint32_t r = next();
            if (r >= threshold) return r % bound;
        }
    }
    // [0, 1) 실수. 위 24비트로 만들어 float 로 정확히 나타납니다.
    float nextFloat() { return (float)(next() >> 8) * (1.f / 16777216.f); }
    float range(float lo, float hi) { return lo + (hi - lo) * nextFloat(); }
    // 단위 구 표면 위 균등한 방향.
    glm::vec3 unitVector() {
        const float z = range(-1.f, 1.f);
        const float a = range(0.f, 6.28318531f);
        const float r = std::sqrt(std::max(0.f, 1.f - z * z));
        return { r * std::cos(a), r * std::sin(a), z };
    }
    // 단위 원판 안 균등한 점.
    glm::vec2 inDisk() {
        const float r = std::sqrt(nextFloat());
        const float a = range(0.f, 6.28318531f);
        return { r * std::cos(a), r * std::sin(a) };
    }
private:
    uint64_t state_ = 0, inc_ = 1;
};

// ── 정수 해시 ───────────────────────────────────────────────────────────────────
// 격자 좌표 → 32비트. 노이즈와 "자리마다 고정된 난수"에 씁니다(PCG 해시, Jarzynski · Olano 2020).
inline uint32_t hash32(uint32_t v) {
    const uint32_t state = v * 747796405u + 2891336453u;
    const uint32_t word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
inline uint32_t hash32(int32_t x, int32_t y, uint32_t seed = 0) {
    return hash32((uint32_t)x ^ hash32((uint32_t)y ^ hash32(seed)));
}
inline uint32_t hash32(int32_t x, int32_t y, int32_t z, uint32_t seed = 0) {
    return hash32((uint32_t)x ^ hash32((uint32_t)y ^ hash32((uint32_t)z ^ hash32(seed))));
}
inline float hashToUnit(uint32_t h) { return (float)(h >> 8) * (1.f / 16777216.f); }

// ── 노이즈 ──────────────────────────────────────────────────────────────────────
namespace detail {
inline float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }   // 5차, 2차 미분까지 이어짐
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
// 해시로 12개 모서리 방향 중 하나를 골라 내적(Perlin 2002).
inline float grad3(uint32_t h, float x, float y, float z) {
    switch (h % 12u) {
        case 0:  return  x + y;  case 1:  return -x + y;  case 2:  return  x - y;  case 3:  return -x - y;
        case 4:  return  x + z;  case 5:  return -x + z;  case 6:  return  x - z;  case 7:  return -x - z;
        case 8:  return  y + z;  case 9:  return -y + z;  case 10: return  y - z;  default: return -y - z;
    }
}
inline float grad2(uint32_t h, float x, float y) {
    switch (h & 7u) {
        case 0: return  x + y; case 1: return -x + y; case 2: return  x - y; case 3: return -x - y;
        case 4: return  x;     case 5: return -x;     case 6: return  y;     default: return -y;
    }
}
} // namespace detail

// 값 노이즈 [0, 1].
inline float valueNoise(glm::vec2 p, uint32_t seed = 0) {
    const glm::vec2 f = glm::floor(p);
    const int32_t x = (int32_t)f.x, y = (int32_t)f.y;
    const float tx = detail::fade(p.x - f.x), ty = detail::fade(p.y - f.y);
    const float a = hashToUnit(hash32(x, y, seed)),     b = hashToUnit(hash32(x + 1, y, seed));
    const float c = hashToUnit(hash32(x, y + 1, seed)), d = hashToUnit(hash32(x + 1, y + 1, seed));
    return detail::lerp(detail::lerp(a, b, tx), detail::lerp(c, d, tx), ty);
}

// 그라디언트(Perlin) 노이즈. 2D 는 [−1, 1] 을 넘지 않고(대각 그라디언트 길이 √2 × √(2/4)), 3D 는 대략 [−1, 1] 입니다.
// 정수 격자점에서 0 입니다.
inline float perlin(glm::vec2 p, uint32_t seed = 0) {
    const glm::vec2 f = glm::floor(p);
    const int32_t x = (int32_t)f.x, y = (int32_t)f.y;
    const float fx = p.x - f.x, fy = p.y - f.y;
    const float u = detail::fade(fx), v = detail::fade(fy);
    const float n00 = detail::grad2(hash32(x, y, seed), fx, fy);
    const float n10 = detail::grad2(hash32(x + 1, y, seed), fx - 1.f, fy);
    const float n01 = detail::grad2(hash32(x, y + 1, seed), fx, fy - 1.f);
    const float n11 = detail::grad2(hash32(x + 1, y + 1, seed), fx - 1.f, fy - 1.f);
    return detail::lerp(detail::lerp(n00, n10, u), detail::lerp(n01, n11, u), v);
}
inline float perlin(glm::vec3 p, uint32_t seed = 0) {
    const glm::vec3 f = glm::floor(p);
    const int32_t x = (int32_t)f.x, y = (int32_t)f.y, z = (int32_t)f.z;
    const float fx = p.x - f.x, fy = p.y - f.y, fz = p.z - f.z;
    const float u = detail::fade(fx), v = detail::fade(fy), w = detail::fade(fz);
    auto g = [&](int dx, int dy, int dz) {
        return detail::grad3(hash32(x + dx, y + dy, z + dz, seed), fx - (float)dx, fy - (float)dy, fz - (float)dz);
    };
    const float x00 = detail::lerp(g(0, 0, 0), g(1, 0, 0), u), x10 = detail::lerp(g(0, 1, 0), g(1, 1, 0), u);
    const float x01 = detail::lerp(g(0, 0, 1), g(1, 0, 1), u), x11 = detail::lerp(g(0, 1, 1), g(1, 1, 1), u);
    return detail::lerp(detail::lerp(x00, x10, v), detail::lerp(x01, x11, v), w);
}

// 프랙탈 합(fBm): 옥타브마다 주파수 × lacunarity, 진폭 × gain. 결과를 진폭 합으로 나눠 범위를 한 옥타브와 같게 둡니다.
template <typename V>
inline float fbm(V p, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f, uint32_t seed = 0) {
    float sum = 0.f, amp = 1.f, norm = 0.f;
    for (int i = 0; i < octaves; ++i) {
        sum  += amp * perlin(p, seed + (uint32_t)i * 0x9E3779B9u);
        norm += amp;
        p    *= lacunarity;
        amp  *= gain;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

// ── 블루 노이즈(포아송 원판) ─────────────────────────────────────────────────────
// 직사각형 [0, size) 안에 서로 minDist 이상 떨어진 점을 고르게 뿌립니다(Bridson 2007, 시도 k = 30). 같은 시드면 같은 결과.
// 풀 · 돌 · 소품을 겹치지 않게 흩을 때 씁니다(균등 난수는 뭉치고 비는 곳이 생김).
inline std::vector<glm::vec2> poissonDisk(glm::vec2 size, float minDist, uint64_t seed = 1, int k = 30) {
    std::vector<glm::vec2> pts;
    if (!(minDist > 0.f) || !(size.x > 0.f) || !(size.y > 0.f)) return pts;
    const float cell = minDist / 1.41421356f;
    const int gw = (int)std::ceil(size.x / cell), gh = (int)std::ceil(size.y / cell);
    std::vector<int> grid((size_t)gw * (size_t)gh, -1);
    std::vector<int> active;
    Pcg32 rng(seed);
    auto cellOf = [&](glm::vec2 p) { return glm::ivec2(std::min((int)(p.x / cell), gw - 1), std::min((int)(p.y / cell), gh - 1)); };
    auto add = [&](glm::vec2 p) {
        const glm::ivec2 c = cellOf(p);
        grid[(size_t)c.y * gw + c.x] = (int)pts.size();
        active.push_back((int)pts.size());
        pts.push_back(p);
    };
    add({ rng.range(0.f, size.x), rng.range(0.f, size.y) });
    const float r2 = minDist * minDist;
    while (!active.empty()) {
        const uint32_t ai = rng.nextBelow((uint32_t)active.size());
        const glm::vec2 base = pts[(size_t)active[ai]];
        bool placed = false;
        for (int t = 0; t < k && !placed; ++t) {
            const float a = rng.range(0.f, 6.28318531f);
            const float r = minDist * std::sqrt(rng.range(1.f, 4.f));   // [r, 2r] 고리 안 면적 균등
            const glm::vec2 c = base + r * glm::vec2(std::cos(a), std::sin(a));
            if (c.x < 0.f || c.y < 0.f || c.x >= size.x || c.y >= size.y) continue;
            const glm::ivec2 cc = cellOf(c);
            bool ok = true;
            for (int y = std::max(cc.y - 2, 0); y <= std::min(cc.y + 2, gh - 1) && ok; ++y)
                for (int x = std::max(cc.x - 2, 0); x <= std::min(cc.x + 2, gw - 1) && ok; ++x) {
                    const int idx = grid[(size_t)y * gw + x];
                    if (idx >= 0) {
                        const glm::vec2 d = pts[(size_t)idx] - c;
                        if (glm::dot(d, d) < r2) ok = false;
                    }
                }
            if (ok) { add(c); placed = true; }
        }
        if (!placed) { active[ai] = active.back(); active.pop_back(); }
    }
    return pts;
}

} // namespace hmath
