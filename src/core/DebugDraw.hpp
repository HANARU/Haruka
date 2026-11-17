#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cmath>
#include <vector>

// ── 디버그 선 (2026-10-15) ─────────────────────────────────────────────────────
// 컴포넌트 · 게임 코드가 이번 프레임에 그릴 선분을 모아 두는 곳입니다. 에디터가 매 프레임 가져가 렌더러의 라인
// 오버레이(헤드셋 두 눈과 뷰포트)에 함께 그리고 비웁니다. 코어가 렌더러를 모르도록 자기 형식을 씁니다.
namespace hdebug {

struct Line { glm::vec3 a, b; glm::vec4 color; };

inline std::vector<Line>& lines() {
    static std::vector<Line> v;
    return v;
}
inline void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) { lines().push_back({ a, b, color }); }
// 축 셋(X 빨강 · Y 초록 · Z 파랑). m 은 월드 행렬(크기 무시하고 축 방향만 씀).
inline void axes(const glm::mat4& m, float length) {
    const glm::vec3 o(m[3]);
    const glm::vec4 c[3] = { { 1.f, 0.25f, 0.25f, 1.f }, { 0.25f, 1.f, 0.25f, 1.f }, { 0.3f, 0.5f, 1.f, 1.f } };
    for (int k = 0; k < 3; ++k) {
        glm::vec3 d(m[k]);
        const float len = glm::length(d);
        if (len > 1e-6f) d /= len;
        line(o, o + d * length, c[k]);
    }
}

// 원(중심, 법선, 반지름). 에디터 범위 표시(drawEditorVisual, 2026-11-17)가 씁니다.
inline void circle(const glm::vec3& c, const glm::vec3& n, float r, const glm::vec4& color, int segments = 32) {
    const glm::vec3 nn  = glm::normalize(n);
    const glm::vec3 ref = std::abs(nn.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 u   = glm::normalize(glm::cross(nn, ref)), v = glm::cross(nn, u);
    glm::vec3 prev = c + u * r;
    for (int i = 1; i <= segments; ++i) {
        const float t = 6.28318531f * (float)i / (float)segments;
        const glm::vec3 p = c + (u * std::cos(t) + v * std::sin(t)) * r;
        line(prev, p, color);
        prev = p;
    }
}
// 구(축 원 셋).
inline void sphere(const glm::vec3& c, float r, const glm::vec4& color) {
    circle(c, { 1, 0, 0 }, r, color); circle(c, { 0, 1, 0 }, r, color); circle(c, { 0, 0, 1 }, r, color);
}
// 상자: m 은 월드 행렬, 로컬 [-half, half] 를 center 만큼 옮긴 상자.
inline void box(const glm::mat4& m, const glm::vec3& center, const glm::vec3& half, const glm::vec4& color) {
    glm::vec3 p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = glm::vec3(m * glm::vec4(center + half * glm::vec3((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, (i & 4) ? 1 : -1), 1.f));
    const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    for (const auto& k : e) line(p[k[0]], p[k[1]], color);
}

} // namespace hdebug
