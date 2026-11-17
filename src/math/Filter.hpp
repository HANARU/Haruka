#pragma once

// ── 포즈 필터와 속도 추정 (D1, 2026-11-17) ──────────────────────────────────────────
// 추적 자세의 떨림을 줄이는 One Euro 필터(Casiez · Roussel · Vogel, CHI 2012)와, 표본 몇 개로 속도를 추정하는 최소제곱
// 직선 맞춤입니다. One Euro 는 느릴 때 강하게(떨림 제거), 빠를 때 약하게(지연 제거) 거릅니다. 차단 주파수 = minCutoff +
// beta · |속도|. 손 추적 · 시선 · 포인터 레이에 씁니다.
//
// 속도 추정: 프레임 두 개의 차이는 떨림에 약하고, 평균은 늦습니다. 최근 N 표본에 직선 x(t) = a + v·t 를 최소제곱으로 맞추면
// 같은 지연에서 떨림이 1/√N 보다 더 줄어듭니다(시간 간격이 고르지 않아도 됨). 던지기 속도의 다음 단계 후보입니다.

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace hmath {

// 1차 저역 통과의 지수 계수: 표본 간격 dt 에서 차단 주파수 fc(Hz) 의 평활 비율 α = 1 / (1 + τ/dt), τ = 1/(2π fc).
inline float lowPassAlpha(float cutoffHz, float dt) {
    if (!(dt > 0.f)) return 1.f;
    const float tau = 1.f / (2.f * 3.14159265f * std::max(cutoffHz, 1e-6f));
    return 1.f / (1.f + tau / dt);
}

// T 는 float · glm::vec3 등(덧셈 · 스칼라 곱 · 길이 함수가 있는 형).
template <typename T>
class OneEuroFilter {
public:
    float minCutoff = 1.0f;    // Hz. 낮을수록 정지 때 떨림이 줄고 느린 움직임이 늦어짐
    float beta      = 0.007f;  // 속도에 따른 차단 주파수 증가. 높을수록 빠른 움직임에 덜 늦음
    float dCutoff   = 1.0f;    // 속도 추정 자체의 차단 주파수

    void reset() { has_ = false; }
    T filter(const T& x, float dt) {
        if (!has_ || !(dt > 0.f)) { prev_ = x; dPrev_ = T{}; has_ = true; return x; }
        const T dx = (x - prev_) / dt;
        const float ad = lowPassAlpha(dCutoff, dt);
        dPrev_ = dPrev_ + (dx - dPrev_) * ad;
        const float cutoff = minCutoff + beta * magnitude(dPrev_);
        const float a = lowPassAlpha(cutoff, dt);
        prev_ = prev_ + (x - prev_) * a;
        return prev_;
    }
    const T& value() const { return prev_; }
private:
    static float magnitude(float v) { return std::fabs(v); }
    template <typename V> static float magnitude(const V& v) { return glm::length(v); }
    T    prev_{};
    T    dPrev_{};
    bool has_ = false;
};

// 회전용 One Euro: 각속도 크기로 차단 주파수를 정하고 slerp 로 거릅니다.
class OneEuroRotationFilter {
public:
    float minCutoff = 1.0f, beta = 0.007f, dCutoff = 1.0f;
    void reset() { has_ = false; }
    glm::quat filter(const glm::quat& q, float dt) {
        if (!has_ || !(dt > 0.f)) { prev_ = q; speed_ = 0.f; has_ = true; return q; }
        const glm::quat t = glm::dot(prev_, q) < 0.f ? -q : q;
        const float angle = 2.f * std::acos(std::min(1.f, std::fabs(glm::dot(prev_, t))));
        speed_ += (angle / dt - speed_) * lowPassAlpha(dCutoff, dt);
        const float a = lowPassAlpha(minCutoff + beta * speed_, dt);
        prev_ = glm::normalize(glm::slerp(prev_, t, a));
        return prev_;
    }
private:
    glm::quat prev_{ 1.f, 0.f, 0.f, 0.f };
    float     speed_ = 0.f;
    bool      has_   = false;
};

// 최근 N 개 (시각, 위치) 표본에 직선을 최소제곱으로 맞춘 기울기(속도). 표본이 둘 미만이거나 시각이 모두 같으면 0.
template <size_t N>
class LinearVelocityEstimator {
public:
    void reset() { count_ = 0; head_ = 0; }
    void push(float time, const glm::vec3& pos) {
        t_[head_] = time; p_[head_] = pos;
        head_ = (head_ + 1) % N;
        if (count_ < N) ++count_;
    }
    size_t count() const { return count_; }
    glm::vec3 velocity() const {
        if (count_ < 2) return glm::vec3(0.f);
        double tm = 0.0; glm::dvec3 pm(0.0);
        for (size_t i = 0; i < count_; ++i) { tm += t_[i]; pm += glm::dvec3(p_[i]); }
        tm /= (double)count_; pm /= (double)count_;
        double stt = 0.0; glm::dvec3 stp(0.0);
        for (size_t i = 0; i < count_; ++i) {
            const double dt = t_[i] - tm;
            stt += dt * dt;
            stp += dt * (glm::dvec3(p_[i]) - pm);
        }
        if (stt <= 1e-12) return glm::vec3(0.f);
        return glm::vec3(stp / stt);
    }
private:
    std::array<float, N>     t_{};
    std::array<glm::vec3, N> p_{};
    size_t count_ = 0, head_ = 0;
};

} // namespace hmath
