#pragma once

// ── 두 뼈 IK 와 스윙-트위스트 분해 (D2, 2026-11-17) ────────────────────────────────
// 팔 · 다리(어깨-팔꿈치-손목)처럼 관절 둘인 사슬을 목표점에 닿게 하는 해석적 해(코사인 법칙)와, 회전을 한 축 둘레의 비틀림
// (twist)과 나머지(swing)로 나누는 분해입니다. 손목 비틀림을 팔뚝 뼈에 나눠 싣거나, 관절 각도를 축별로 제한할 때 씁니다.
//
// 참고: UE TwoBoneIK(AnimationCore), Dobrowolski "Swing-twist decomposition"(2015).

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace hmath {

// q = swing · twist, twist 는 axis(단위) 둘레의 회전입니다. 축과 직각인 180° 회전처럼 비틀림 성분이 없으면 twist 는 항등.
inline void swingTwist(const glm::quat& q, const glm::vec3& axis, glm::quat& swing, glm::quat& twist) {
    const glm::vec3 r(q.x, q.y, q.z);
    const glm::vec3 p = glm::dot(r, axis) * axis;   // 회전 축 성분을 비틀림 축에 투영
    twist = glm::quat(q.w, p.x, p.y, p.z);
    const float len = glm::length(twist);
    if (len < 1e-9f) twist = glm::quat(1.f, 0.f, 0.f, 0.f);
    else             twist = twist / len;
    swing = q * glm::inverse(twist);
}

// 비틀림 각(라디안, −π ~ π). 축 방향으로 보았을 때 반시계가 양수.
inline float twistAngle(const glm::quat& q, const glm::vec3& axis) {
    glm::quat s, t;
    swingTwist(q, axis, s, t);
    float a = 2.f * std::atan2(glm::dot(glm::vec3(t.x, t.y, t.z), axis), t.w);
    if (a > 3.14159265f) a -= 6.28318531f;
    if (a < -3.14159265f) a += 6.28318531f;
    return a;
}

// 두 뼈 IK 결과: 가운데 관절의 월드 위치와 끝점(닿지 못하면 목표 방향으로 쭉 편 자리).
struct TwoBoneResult {
    glm::vec3 mid{ 0.f };
    glm::vec3 end{ 0.f };
    bool      reached = false;
};

// root(어깨) · 길이 lenA(위팔) · lenB(아래팔) · 목표 target · 굽힘 방향 힌트 pole(팔꿈치가 향할 쪽의 월드 점).
// 닿을 수 없으면(너무 멀거나 너무 가까움) 가능한 가장 가까운 자세를 줍니다. 길이는 바뀌지 않습니다.
inline TwoBoneResult solveTwoBone(const glm::vec3& root, float lenA, float lenB, const glm::vec3& target,
                                  const glm::vec3& pole) {
    TwoBoneResult r;
    glm::vec3 toT = target - root;
    float dist = glm::length(toT);
    const glm::vec3 dir = dist > 1e-6f ? toT / dist : glm::vec3(0.f, 0.f, -1.f);
    const float maxReach = lenA + lenB, minReach = std::fabs(lenA - lenB);
    r.reached = dist <= maxReach && dist >= minReach;
    dist = std::clamp(dist, minReach + 1e-5f, maxReach - 1e-5f);
    // 굽힘 평면: dir 과 pole 쪽 방향이 이루는 평면. pole 이 dir 과 나란하면 아무 직각 방향.
    glm::vec3 bend = (pole - root) - glm::dot(pole - root, dir) * dir;
    if (glm::dot(bend, bend) < 1e-10f) {
        bend = glm::cross(dir, glm::vec3(0.f, 1.f, 0.f));
        if (glm::dot(bend, bend) < 1e-10f) bend = glm::cross(dir, glm::vec3(1.f, 0.f, 0.f));
    }
    bend = glm::normalize(bend);
    // 코사인 법칙: 어깨에서 dir 과 위팔이 이루는 각.
    const float cosA = std::clamp((lenA * lenA + dist * dist - lenB * lenB) / (2.f * lenA * dist), -1.f, 1.f);
    const float sinA = std::sqrt(std::max(0.f, 1.f - cosA * cosA));
    r.mid = root + lenA * (cosA * dir + sinA * bend);
    r.end = root + dir * dist;
    return r;
}

// 뼈의 원래 방향 from 을 새 방향 to 로 돌리는 최소 회전(두 단위 벡터 사이). 반대 방향이면 임의의 직각 축으로 180°.
inline glm::quat rotationBetween(const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 a = glm::normalize(from), b = glm::normalize(to);
    const float d = glm::dot(a, b);
    if (d < -0.999999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1.f, 0.f, 0.f), a);
        if (glm::dot(axis, axis) < 1e-6f) axis = glm::cross(glm::vec3(0.f, 1.f, 0.f), a);
        return glm::angleAxis(3.14159265f, glm::normalize(axis));
    }
    const glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.f + d, c.x, c.y, c.z));
}

} // namespace hmath
