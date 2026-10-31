#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
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

} // namespace hdebug
