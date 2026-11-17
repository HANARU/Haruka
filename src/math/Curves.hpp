#pragma once

// ── 곡선 (B3, 2026-11-17) ─────────────────────────────────────────────────────────
// Hermite · Bezier(3차) · Catmull-Rom(중심형) 곡선과 그 접선, 길이로 고르게 나누는 표입니다. 애니메이션 경로 · 카메라 레일 ·
// 케이블 같은 곳에 씁니다. glm 만 씁니다.
//
// Catmull-Rom 은 Yuksel 등(2011)의 중심형(alpha = 0.5)을 기본으로 둡니다. 균일형(alpha = 0)은 점 사이가 고르지 않으면 고리와
// 뾰족점이 생기고, 중심형은 생기지 않음이 증명되어 있습니다.

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace hmath {

// ── 3차 Hermite ─────────────────────────────────────────────────────────────────
// p0 에서 접선 m0, p1 에서 접선 m1. t ∈ [0, 1].
template <typename T>
inline T hermite(const T& p0, const T& m0, const T& p1, const T& m1, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return p0 * (2.f * t3 - 3.f * t2 + 1.f) + m0 * (t3 - 2.f * t2 + t) + p1 * (-2.f * t3 + 3.f * t2) + m1 * (t3 - t2);
}
template <typename T>
inline T hermiteTangent(const T& p0, const T& m0, const T& p1, const T& m1, float t) {
    const float t2 = t * t;
    return p0 * (6.f * t2 - 6.f * t) + m0 * (3.f * t2 - 4.f * t + 1.f) + p1 * (-6.f * t2 + 6.f * t) + m1 * (3.f * t2 - 2.f * t);
}

// ── 3차 Bezier ──────────────────────────────────────────────────────────────────
template <typename T>
inline T bezier(const T& p0, const T& p1, const T& p2, const T& p3, float t) {
    const float u = 1.f - t;
    return p0 * (u * u * u) + p1 * (3.f * u * u * t) + p2 * (3.f * u * t * t) + p3 * (t * t * t);
}
template <typename T>
inline T bezierTangent(const T& p0, const T& p1, const T& p2, const T& p3, float t) {
    const float u = 1.f - t;
    return (p1 - p0) * (3.f * u * u) + (p2 - p1) * (6.f * u * t) + (p3 - p2) * (3.f * t * t);
}

// ── Catmull-Rom ─────────────────────────────────────────────────────────────────
// p1 ~ p2 구간(p0 · p3 는 이웃 점). alpha 0 균일 · 0.5 중심형 · 1 현 길이. Barry-Goldman 피라미드 식을 Hermite 접선으로 바꿔
// 계산합니다. 같은 점이 겹치면(간격 0) 균일형으로 물러납니다.
inline glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t,
                            float alpha = 0.5f, glm::vec3* tangentOut = nullptr) {
    auto knot = [alpha](const glm::vec3& a, const glm::vec3& b) {
        const float d = glm::length(b - a);
        return d > 1e-6f ? std::pow(d, alpha) : 1.f;
    };
    const float d0 = knot(p0, p1), d1 = knot(p1, p2), d2 = knot(p2, p3);
    // 구간 [p1, p2] 를 t ∈ [0, 1] 로 다시 맞춘 접선(d1 을 곱함).
    glm::vec3 m1 = ((p1 - p0) / d0 - (p2 - p0) / (d0 + d1) + (p2 - p1) / d1) * d1;
    glm::vec3 m2 = ((p2 - p1) / d1 - (p3 - p1) / (d1 + d2) + (p3 - p2) / d2) * d1;
    if (tangentOut) *tangentOut = hermiteTangent(p1, m1, p2, m2, t);
    return hermite(p1, m1, p2, m2, t);
}

// 점 목록을 지나는 Catmull-Rom 스플라인. 양 끝은 끝점을 거울로 늘린 가상 점을 씁니다. u ∈ [0, n − 1](점 번호 단위).
inline glm::vec3 catmullRomSpline(const std::vector<glm::vec3>& pts, float u, float alpha = 0.5f,
                                  glm::vec3* tangentOut = nullptr) {
    const int n = (int)pts.size();
    if (n == 0) return glm::vec3(0.f);
    if (n == 1) { if (tangentOut) *tangentOut = glm::vec3(0.f); return pts[0]; }
    u = std::clamp(u, 0.f, (float)(n - 1));
    int i = std::min((int)u, n - 2);
    const float t = u - (float)i;
    const glm::vec3 a = i > 0 ? pts[i - 1] : 2.f * pts[0] - pts[1];
    const glm::vec3 d = i + 2 < n ? pts[i + 2] : 2.f * pts[n - 1] - pts[n - 2];
    return catmullRom(a, pts[i], pts[i + 1], d, t, alpha, tangentOut);
}

// ── 길이로 고르게 ───────────────────────────────────────────────────────────────
// 매개변수 → 누적 길이 표. 곡선의 t 는 길이에 비례하지 않아(점 사이가 다르면 빨라졌다 느려짐) 일정한 속도로 움직이려면
// 길이 s 를 t 로 바꿔야 합니다. samples 개로 꺾은선 길이를 재고 그 사이를 선형으로 잇습니다.
struct ArcLengthTable {
    std::vector<float> u;   // 매개변수
    std::vector<float> s;   // 누적 길이
    template <typename Eval>
    void build(Eval&& eval, float uMax, int samples = 64) {
        samples = std::max(samples, 2);
        u.assign((size_t)samples + 1, 0.f);
        s.assign((size_t)samples + 1, 0.f);
        glm::vec3 prev = eval(0.f);
        for (int k = 1; k <= samples; ++k) {
            const float uk = uMax * (float)k / (float)samples;
            const glm::vec3 p = eval(uk);
            u[k] = uk;
            s[k] = s[k - 1] + glm::length(p - prev);
            prev = p;
        }
    }
    float length() const { return s.empty() ? 0.f : s.back(); }
    // 길이 → 매개변수.
    float paramAt(float dist) const {
        if (s.size() < 2) return 0.f;
        dist = std::clamp(dist, 0.f, s.back());
        const auto it = std::upper_bound(s.begin(), s.end(), dist);
        const size_t k = std::min((size_t)(it - s.begin()), s.size() - 1);
        const float seg = s[k] - s[k - 1];
        const float f = seg > 0.f ? (dist - s[k - 1]) / seg : 0.f;
        return u[k - 1] + (u[k] - u[k - 1]) * f;
    }
};

} // namespace hmath
