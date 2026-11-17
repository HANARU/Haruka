#pragma once

// ── 시간 독립 감쇠 · 스프링 · 이징 (B2, 2026-11-17) ─────────────────────────────────
// 프레임 길이(dt)가 달라도 같은 시간에 같은 곳에 닿는 따라가기입니다. "x += (target − x) · k" 처럼 프레임마다 같은 비율을
// 쓰면 90 Hz 와 72 Hz 에서 빠르기가 달라집니다(Quest 는 주사율을 바꿈). 여기의 식은 연속 시간 해를 그대로 써서 dt 를
// 둘로 나눠 두 번 부르든 한 번에 부르든 결과가 같습니다(시험: tests/MathTest.cpp).
//
// 참고: Daniel Holden "Spring-It-On"(2021), Game Programming Gems 4 "Critically Damped Ease-In/Out Smoothing".

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace hmath {

// ── 지수 감쇠 ───────────────────────────────────────────────────────────────────
// 남은 거리가 halfLife 초마다 절반이 됩니다. halfLife ≤ 0 이면 바로 닿습니다.
inline float dampFactor(float halfLife, float dt) {
    if (!(halfLife > 0.f)) return 1.f;
    return 1.f - std::exp2(-dt / halfLife);
}
template <typename T>
inline T damp(const T& current, const T& target, float halfLife, float dt) {
    return current + (target - current) * dampFactor(halfLife, dt);
}
// 회전은 slerp 로(최단 경로).
inline glm::quat dampRotation(const glm::quat& current, const glm::quat& target, float halfLife, float dt) {
    const glm::quat t = glm::dot(current, target) < 0.f ? -target : target;
    return glm::normalize(glm::slerp(current, t, dampFactor(halfLife, dt)));
}
// 시간 상수 tau(1/e 로 줄어드는 시간)로 주는 형태. halfLife = tau · ln 2.
inline float halfLifeFromTau(float tau) { return tau * 0.69314718f; }

// ── 임계 감쇠 스프링 ────────────────────────────────────────────────────────────
// 넘치지 않고 가장 빨리 목표에 닿는 2차 시스템 x'' = −ω²(x − g) − 2ω x' 의 정확한 해입니다. 속도를 함께 가지므로
// 목표가 움직여도 매끄럽게(가속도가 이어지게) 따라갑니다. halfLife 는 Holden 의 규약(ω = 2 · ln2 / halfLife)이라 정지 상태에서
// halfLife 뒤 남은 거리는 약 60 % 입니다(지수 감쇠의 50 % 보다 느리게 출발하고 넘치지 않음).
template <typename T>
struct CriticalSpring {
    T value{};
    T velocity{};
    void update(const T& goal, float halfLife, float dt) {
        if (!(halfLife > 0.f)) { value = goal; velocity = T{}; return; }
        const float y  = 2.f * 0.69314718f / halfLife;   // Holden: damping = 4 ln2 / halfLife, y = damping / 2
        const T     j0 = value - goal;
        const T     j1 = velocity + j0 * y;
        const float e  = std::exp(-y * dt);
        value    = e * (j0 + j1 * dt) + goal;
        velocity = e * (velocity - j1 * y * dt);
    }
};
using SpringF  = CriticalSpring<float>;
using SpringV3 = CriticalSpring<glm::vec3>;

// 감쇠비 ζ 를 고를 수 있는 스프링(ζ < 1 이면 넘쳐 흔들림, > 1 이면 느림). 반감기 대신 고유 진동수(Hz)로 줍니다.
// 반암시적 오일러 대신 정확한 해를 쓰려면 경우가 셋으로 나뉘어, 여기서는 dt 를 잘게(최대 1/240 s) 나눈 반암시적 적분을 씁니다.
// 진동수 × dt 가 작으면 정확한 해와 거의 같고, 큰 dt 에서도 불안정해지지 않습니다.
template <typename T>
struct DampedSpring {
    T value{};
    T velocity{};
    void update(const T& goal, float frequencyHz, float dampingRatio, float dt) {
        const float w = 2.f * 3.14159265f * std::max(frequencyHz, 0.f);
        if (!(w > 0.f) || !(dt > 0.f)) return;
        const int   n = std::max(1, (int)std::ceil(dt * 240.f));
        const float h = dt / (float)n;
        for (int i = 0; i < n; ++i) {
            const T acc = (goal - value) * (w * w) - velocity * (2.f * dampingRatio * w);
            velocity = velocity + acc * h;
            value    = value + velocity * h;
        }
    }
};

// 쿼터니언 스프링(임계 감쇠). 각속도(rad/s, 월드 축)를 함께 가집니다.
struct RotationSpring {
    glm::quat value{ 1.f, 0.f, 0.f, 0.f };
    glm::vec3 angularVelocity{ 0.f };
    void update(const glm::quat& goal, float halfLife, float dt) {
        if (!(halfLife > 0.f)) { value = goal; angularVelocity = glm::vec3(0.f); return; }
        glm::quat g = glm::dot(value, goal) < 0.f ? -goal : goal;
        // 회전 차이를 축-각 벡터로(로그 사상)
        const glm::quat diff = value * glm::inverse(g);
        glm::vec3 j0 = glm::vec3(diff.x, diff.y, diff.z);
        const float s = glm::length(j0);
        if (s > 1e-9f) j0 *= 2.f * std::atan2(s, diff.w) / s; else j0 *= 2.f;
        const float y  = 2.f * 0.69314718f / halfLife;   // CriticalSpring 과 같은 규약
        const glm::vec3 j1 = angularVelocity + j0 * y;
        const float e  = std::exp(-y * dt);
        const glm::vec3 r = e * (j0 + j1 * dt);
        angularVelocity   = e * (angularVelocity - j1 * y * dt);
        const float ang = glm::length(r);
        const glm::quat q = ang > 1e-9f ? glm::angleAxis(ang, r / ang) : glm::quat(1.f, 0.f, 0.f, 0.f);
        value = glm::normalize(q * g);
    }
};

// ── 이징 ────────────────────────────────────────────────────────────────────────
// t ∈ [0, 1] → [0, 1]. 범위 밖 t 는 잘라 씁니다. 이름은 easings.net(Penner)과 같습니다.
namespace ease {
inline float clamp01(float t) { return std::clamp(t, 0.f, 1.f); }
inline float linear(float t)       { return clamp01(t); }
inline float smoothstep(float t)   { t = clamp01(t); return t * t * (3.f - 2.f * t); }
inline float smootherstep(float t) { t = clamp01(t); return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
inline float inQuad(float t)       { t = clamp01(t); return t * t; }
inline float outQuad(float t)      { t = clamp01(t); return 1.f - (1.f - t) * (1.f - t); }
inline float inOutQuad(float t)    { t = clamp01(t); return t < 0.5f ? 2.f * t * t : 1.f - 2.f * (1.f - t) * (1.f - t); }
inline float inCubic(float t)      { t = clamp01(t); return t * t * t; }
inline float outCubic(float t)     { t = clamp01(t); const float u = 1.f - t; return 1.f - u * u * u; }
inline float inOutCubic(float t)   { t = clamp01(t); return t < 0.5f ? 4.f * t * t * t : 1.f - 4.f * (1.f - t) * (1.f - t) * (1.f - t); }
inline float outBack(float t, float s = 1.70158f) {
    t = clamp01(t) - 1.f;
    return 1.f + t * t * ((s + 1.f) * t + s);
}
inline float outElastic(float t) {
    t = clamp01(t);
    if (t == 0.f || t == 1.f) return t;
    return std::exp2(-10.f * t) * std::sin((t * 10.f - 0.75f) * (2.f * 3.14159265f / 3.f)) + 1.f;
}
} // namespace ease

} // namespace hmath
