// 공통 수학 계층(src/math, 2026-11-17)의 검산입니다. Vulkan 도 창도 필요 없습니다.
//
// 이 시험이 존재하는 이유: 판정 · 스프링 · 노이즈는 틀려도 조용히 "조금 이상한" 결과만 내므로 눈으로는 잡기 어렵습니다.
// 해석해가 있는 경우는 해석해와, 없으면 성질(dt 를 나눠도 같음, 같은 시드는 같은 수열, 최소 거리 지킴 등)로 확인합니다.
//
// 실행: build/Release/HarukaMathTest.exe

#include "../src/math/Curves.hpp"
#include "../src/math/Damping.hpp"
#include "../src/math/Filter.hpp"
#include "../src/math/Geometry.hpp"
#include "../src/math/Ik.hpp"
#include "../src/math/Random.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) <= eps; }
bool nearRot(const glm::quat& a, const glm::quat& b, float eps = 1e-4f) { return 1.f - std::fabs(glm::dot(a, b)) <= eps; }

} // namespace

int main() {
    using namespace hmath;
    std::printf("Haruka 공통 수학 검산\n");

    std::printf("\n== 1. 광선 판정 (B1) ==\n");
    {
        float t = 0.f;
        check(intersectRayPlane({ { 0, 5, 0 }, { 0, -1, 0 } }, Plane::fromPointNormal({ 0, 1, 0 }, { 0, 1, 0 }), t) && near(t, 4.f),
              "아래로 쏜 광선이 y = 1 평면에 t = 4");
        check(!intersectRayPlane({ { 0, 5, 0 }, { 0, 1, 0 } }, Plane::fromPointNormal({ 0, 1, 0 }, { 0, 1, 0 }), t), "평면에서 멀어지는 광선은 실패");
        check(!intersectRayPlane({ { 0, 5, 0 }, { 1, 0, 0 } }, Plane{}, t), "나란한 광선은 실패");
        check(intersectLinePlane({ { 0, 5, 0 }, { 0, 1, 0 } }, Plane{}, t) && near(t, -5.f), "직선 판정은 뒤쪽 교점(t = −5)도 받음");
        const float nanv = std::nanf("");
        check(!intersectRayPlane({ { 0, nanv, 0 }, { 0, -1, 0 } }, Plane{}, t), "NaN 입력은 실패");

        check(intersectRaySphere({ { 0, 0, 10 }, { 0, 0, -1 } }, { { 0, 0, 0 }, 2.f }, t) && near(t, 8.f), "구 앞면 t = 8");
        check(intersectRaySphere({ { 0, 0, 0 }, { 0, 0, -1 } }, { { 0, 0, 0 }, 2.f }, t) && near(t, 2.f), "구 안에서 쏘면 나가는 점 t = 2");
        check(!intersectRaySphere({ { 0, 3, 10 }, { 0, 0, -1 } }, { { 0, 0, 0 }, 2.f }, t), "빗나간 구");
        check(intersectRaySphere({ { 0, 0, 10 }, { 0, 0, -2 } }, { { 0, 0, 0 }, 2.f }, t) && near(t, 4.f), "정규화하지 않은 방향은 매개변수 t(= 4)");

        float te = 0.f, tx = 0.f;
        const Aabb box{ { -1, -1, -1 }, { 1, 1, 1 } };
        check(intersectRayAabb({ { -5, 0, 0 }, { 1, 0, 0 } }, box, te, tx) && near(te, 4.f) && near(tx, 6.f), "AABB 들어감 4 · 나감 6");
        check(intersectRayAabb({ { 0, 0, 0 }, { 1, 0, 0 } }, box, te, tx) && te < 0.f && near(tx, 1.f), "AABB 안에서 쏘면 들어감 < 0");
        check(!intersectRayAabb({ { -5, 2, 0 }, { 1, 0, 0 } }, box, te, tx), "축과 나란히 슬랩 밖이면 실패");
        check(!intersectRayAabb({ { 5, 0, 0 }, { 1, 0, 0 } }, box, te, tx), "상자가 뒤에 있으면 실패");
        check(intersectRayAabb({ { -5, 1, 0 }, { 1, 0, 0 } }, box, te, tx), "모서리 면 위를 스치면 맞음(경계 포함)");

        const glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.f), { 10, 0, 0 }), glm::radians(45.f), glm::vec3(0, 1, 0));
        const Obb obb = Obb::fromLocalAabb(box, glm::scale(m, glm::vec3(2.f, 1.f, 1.f)));
        check(near(obb.half, { 2, 1, 1 }), "OBB 반변에 비균등 크기 반영");
        check(intersectRayObb({ { 10, 0, 10 }, { 0, 0, -1 } }, obb, te, tx), "회전한 OBB 를 맞춤");
        // 45° 돌린 2×1 상자: z 축으로 들어오는 광선이 만나는 깊이는 2·(1/cos45 와 2/cos45 중 작은 쪽의 투영) … 대칭이므로 tx − te 로 확인
        check(tx - te > 2.f, "OBB 를 지나는 길이가 회전 전(2)보다 김");

        float u = 0.f, v = 0.f;
        check(intersectRayTriangle({ { 0.25f, 0.25f, 1 }, { 0, 0, -1 } }, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, t, u, v) &&
              near(t, 1.f) && near(u, 0.25f) && near(v, 0.25f), "삼각형 t = 1, 무게중심 (0.25, 0.25)");
        check(intersectRayTriangle({ { 0.25f, 0.25f, -1 }, { 0, 0, 1 } }, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, t, u, v), "삼각형 뒷면도 맞음(양면)");
        check(!intersectRayTriangle({ { 0.8f, 0.8f, 1 }, { 0, 0, -1 } }, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, t, u, v), "삼각형 밖");
        check(!intersectRayTriangle({ { 0, 0, 1 }, { 0, 0, -1 } }, { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, t, u, v), "퇴화 삼각형은 실패");
    }

    std::printf("\n== 2. 겹침 · 변환 · 투영 (B1) ==\n");
    {
        const Aabb a{ { 0, 0, 0 }, { 1, 1, 1 } };
        check(overlapAabbAabb(a, { { 1, 1, 1 }, { 2, 2, 2 } }), "모서리가 닿는 AABB 는 겹침");
        check(!overlapAabbAabb(a, { { 1.01f, 0, 0 }, { 2, 1, 1 } }), "떨어진 AABB");
        check(overlapSphereAabb({ { 2, 0.5f, 0.5f }, 1.f }, a) && !overlapSphereAabb({ { 2.1f, 0.5f, 0.5f }, 1.f }, a), "구 · AABB 경계");
        check(overlapSphereSphere({ { 0, 0, 0 }, 1 }, { { 2, 0, 0 }, 1 }) && !overlapSphereSphere({ { 0, 0, 0 }, 1 }, { { 2.01f, 0, 0 }, 1 }), "구 · 구 경계");
        check(classifySpherePlane({ { 0, 2, 0 }, 1 }, Plane{}) == 1 && classifySpherePlane({ { 0, -2, 0 }, 1 }, Plane{}) == -1 &&
              classifySpherePlane({ { 0, 0.5f, 0 }, 1 }, Plane{}) == 0, "구 · 평면 앞 · 뒤 · 걸침");

        // Arvo AABB 변환 = 꼭짓점 여덟 개를 옮겨 감싼 것
        const glm::mat4 m = glm::scale(glm::rotate(glm::translate(glm::mat4(1.f), { 3, -2, 1 }), 0.7f, glm::normalize(glm::vec3(1, 2, 3))),
                                       glm::vec3(1.5f, 0.5f, 2.f));
        const Aabb b{ { -1, -2, -0.5f }, { 2, 1, 0.5f } };
        Aabb brute;
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 c((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z);
            brute.expand(glm::vec3(m * glm::vec4(c, 1.f)));
        }
        const Aabb fast = transformAabb(b, m);
        check(near(fast.min, brute.min) && near(fast.max, brute.max), "Arvo AABB 변환이 꼭짓점 8개 결과와 같음");
        check(near(distancePointSegment({ 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }), 1.f) &&
              near(distancePointSegment({ 3, 0, 0 }, { -1, 0, 0 }, { 1, 0, 0 }), 2.f), "점 · 선분 거리(안 · 끝 밖)");
        check(safeNormalize(glm::vec3(0.f), { 0, 1, 0 }) == glm::vec3(0, 1, 0), "길이 0 정규화는 대체값");

        const glm::mat4 proj = glm::perspective(glm::radians(60.f), 1.f, 0.1f, 100.f);
        const glm::mat4 view = glm::lookAt(glm::vec3(0, 2, 5), glm::vec3(0, 2, 0), glm::vec3(0, 1, 0));
        Ray r;
        check(rayFromNdc(glm::inverse(proj * view), { 0, 0 }, r) && near(r.dir, { 0, 0, -1 }, 1e-3f) && near(r.origin.y, 2.f, 1e-3f),
              "화면 가운데 광선은 시선 방향");
        const glm::vec4 clip = proj * view * glm::vec4(1.f, 2.5f, -3.f, 1.f);
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        rayFromNdc(glm::inverse(proj * view), ndc, r);
        const glm::vec3 toP = glm::normalize(glm::vec3(1.f, 2.5f, -3.f) - r.origin);
        check(glm::dot(toP, r.dir) > 0.99999f, "투영한 점의 NDC 로 만든 광선이 그 점을 지남");
        check(!rayFromNdc(glm::mat4(0.f), { 0, 0 }, r), "퇴화 투영은 실패");
    }

    std::printf("\n== 3. 감쇠 · 스프링 · 이징 (B2) ==\n");
    {
        check(near(damp(0.f, 1.f, 0.5f, 0.5f), 0.5f), "반감기 뒤 절반");
        const float once = damp(0.f, 1.f, 0.3f, 0.2f);
        const float twice = damp(damp(0.f, 1.f, 0.3f, 0.1f), 1.f, 0.3f, 0.1f);
        check(near(once, twice, 1e-6f), "지수 감쇠는 dt 를 나눠도 같음(프레임률 독립)");
        check(damp(0.f, 1.f, 0.f, 0.01f) == 1.f, "반감기 0 은 바로 닿음");

        SpringF s1; s1.value = 0.f;
        SpringF s2 = s1;
        s1.update(1.f, 0.2f, 0.1f);
        for (int i = 0; i < 10; ++i) s2.update(1.f, 0.2f, 0.01f);
        check(near(s1.value, s2.value, 1e-5f) && near(s1.velocity, s2.velocity, 1e-4f), "임계 스프링도 dt 를 나눠도 같음(정확한 해)");
        SpringF s3;
        float maxV = 0.f;
        for (int i = 0; i < 400; ++i) { s3.update(1.f, 0.2f, 1.f / 90.f); maxV = std::max(maxV, s3.value); }
        check(maxV <= 1.f + 1e-6f && near(s3.value, 1.f, 1e-3f), "임계 감쇠는 넘치지 않고 닿음");
        SpringV3 sv; sv.update(glm::vec3(1, 2, 3), 0.1f, 2.f);
        check(near(sv.value, { 1, 2, 3 }, 1e-3f), "벡터 스프링도 닿음");

        DampedSpring<float> under;
        float peak = 0.f;
        for (int i = 0; i < 400; ++i) { under.update(1.f, 2.f, 0.3f, 1.f / 90.f); peak = std::max(peak, under.value); }
        check(peak > 1.2f && near(under.value, 1.f, 0.05f), "감쇠비 0.3 은 넘쳤다가 자리 잡음");
        DampedSpring<float> big;
        big.update(1.f, 5.f, 1.f, 0.5f);
        check(std::isfinite(big.value) && big.value <= 1.5f, "큰 dt 에서도 터지지 않음(나눠 적분)");

        RotationSpring rs;
        const glm::quat goal = glm::angleAxis(1.2f, glm::normalize(glm::vec3(0, 1, 1)));
        for (int i = 0; i < 300; ++i) rs.update(goal, 0.1f, 1.f / 90.f);
        check(nearRot(rs.value, goal, 1e-5f), "회전 스프링이 목표 회전에 닿음");
        RotationSpring ra, rb;
        ra.update(goal, 0.15f, 0.1f);
        for (int i = 0; i < 10; ++i) rb.update(goal, 0.15f, 0.01f);
        check(nearRot(ra.value, rb.value, 1e-4f), "회전 스프링도 dt 를 나눠도 거의 같음");

        check(ease::smoothstep(0.f) == 0.f && ease::smoothstep(1.f) == 1.f && near(ease::smoothstep(0.5f), 0.5f), "smoothstep 끝점 · 가운데");
        check(near(ease::inOutCubic(0.5f), 0.5f) && ease::outBack(0.8f) > 1.f && near(ease::outElastic(1.f), 1.f), "이징 성질");
        check(ease::inQuad(-1.f) == 0.f && ease::inQuad(2.f) == 1.f, "범위 밖 t 는 잘림");
    }

    std::printf("\n== 4. 곡선 (B3) ==\n");
    {
        const glm::vec3 p0(0), p1(1, 2, 0), p2(3, 2, 0), p3(4, 0, 0);
        check(near(bezier(p0, p1, p2, p3, 0.f), p0) && near(bezier(p0, p1, p2, p3, 1.f), p3), "Bezier 끝점");
        check(near(bezierTangent(p0, p1, p2, p3, 0.f), 3.f * (p1 - p0)), "Bezier 시작 접선 3(p1 − p0)");
        const float h = 1e-3f;
        const glm::vec3 numT = (bezier(p0, p1, p2, p3, 0.5f + h) - bezier(p0, p1, p2, p3, 0.5f - h)) / (2.f * h);
        check(near(numT, bezierTangent(p0, p1, p2, p3, 0.5f), 1e-2f), "Bezier 접선 = 수치 미분");
        const glm::vec3 m0(1, 0, 0), m1(0, 1, 0);
        check(near(hermite(p0, m0, p3, m1, 0.f), p0) && near(hermite(p0, m0, p3, m1, 1.f), p3) &&
              near(hermiteTangent(p0, m0, p3, m1, 1.f), m1), "Hermite 끝점 · 끝 접선");
        const std::vector<glm::vec3> pts{ { 0, 0, 0 }, { 1, 0, 0 }, { 1.1f, 0, 0 }, { 3, 1, 0 }, { 4, 0, 0 } };
        bool through = true;
        for (int i = 0; i < 5; ++i) through = through && near(catmullRomSpline(pts, (float)i), pts[i], 1e-4f);
        check(through, "Catmull-Rom 은 모든 점을 지남");
        // 중심형은 짧은 구간(1 → 1.1)에서 x 가 되돌아가지 않음(고리 없음)
        bool mono = true;
        float prevX = 1.f;
        for (int k = 1; k <= 20; ++k) { const float x = catmullRomSpline(pts, 1.f + 0.05f * k).x; mono = mono && x >= prevX - 1e-5f; prevX = x; }
        check(mono, "중심형 Catmull-Rom 은 촘촘한 점 사이에서 되돌아가지 않음");
        const glm::vec3 straight = catmullRomSpline({ { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } }, 0.5f);
        check(near(straight, { 0.5f, 0, 0 }), "직선 위 점은 직선을 따름");

        ArcLengthTable tab;
        tab.build([&](float u) { return bezier(p0, p1, p2, p3, u); }, 1.f, 256);
        const float len = tab.length();
        check(len > glm::length(p3 - p0) && len < glm::length(p1 - p0) + glm::length(p2 - p1) + glm::length(p3 - p2), "곡선 길이가 현과 제어 다각형 사이");
        const float uh = tab.paramAt(0.5f * len);
        ArcLengthTable half;
        half.build([&](float u) { return bezier(p0, p1, p2, p3, u); }, uh, 128);
        check(near(half.length(), 0.5f * len, 0.01f * len), "길이의 절반에 해당하는 매개변수");
    }

    std::printf("\n== 5. 난수 · 노이즈 (B4) ==\n");
    {
        Pcg32 a(42), b(42), c(43);
        bool same = true, diff = false;
        for (int i = 0; i < 100; ++i) { const uint32_t x = a.next(); same = same && x == b.next(); diff = diff || x != c.next(); }
        check(same && diff, "같은 시드는 같은 수열, 다른 시드는 다른 수열");
        Pcg32 ref(42u, 54u);
        check(ref.next() == 0xa15c02b7u && ref.next() == 0x7b47f409u, "PCG32 기준 수열(pcg32-demo 시드 42 · 수열 54)");
        Pcg32 r(7);
        bool inRange = true; double sum = 0;
        uint32_t hist[10] = {};
        for (int i = 0; i < 100000; ++i) {
            const float f = r.nextFloat();
            inRange = inRange && f >= 0.f && f < 1.f;
            sum += f;
            ++hist[r.nextBelow(10)];
        }
        bool flat = true;
        for (uint32_t h : hist) flat = flat && h > 9500 && h < 10500;
        check(inRange && std::fabs(sum / 100000.0 - 0.5) < 0.005, "nextFloat ∈ [0, 1), 평균 0.5");
        check(flat, "nextBelow(10) 이 고르게 나옴(±5 %)");
        const glm::vec3 uv = r.unitVector();
        check(near(glm::length(uv), 1.f), "unitVector 길이 1");

        check(near(perlin(glm::vec2(3.f, -2.f)), 0.f) && near(perlin(glm::vec3(1.f, 2.f, 3.f)), 0.f), "Perlin 은 격자점에서 0");
        float mn = 1e9f, mx = -1e9f, mnv = 1e9f, mxv = -1e9f;
        Pcg32 q(9);
        for (int i = 0; i < 20000; ++i) {
            const glm::vec2 p(q.range(-50.f, 50.f), q.range(-50.f, 50.f));
            const float n = perlin(p);
            mn = std::min(mn, n); mx = std::max(mx, n);
            const float vn = valueNoise(p);
            mnv = std::min(mnv, vn); mxv = std::max(mxv, vn);
        }
        check(mn >= -1.f && mx <= 1.f && mx - mn > 1.f, "2D Perlin 은 [−1, 1] 안에서 넓게 퍼짐");
        check(mnv >= 0.f && mxv <= 1.f, "값 노이즈는 [0, 1]");
        const float e = 1e-3f;
        check(std::fabs(perlin(glm::vec2(0.3f, 0.7f)) - perlin(glm::vec2(0.3f + e, 0.7f))) < 0.01f, "Perlin 은 이어짐");
        check(perlin(glm::vec2(0.3f, 0.7f), 1) != perlin(glm::vec2(0.3f, 0.7f), 2), "시드가 다르면 다른 노이즈");
        const float f1 = fbm(glm::vec2(1.3f, 2.7f)), f1b = fbm(glm::vec2(1.3f, 2.7f));
        check(f1 == f1b && f1 >= -1.f && f1 <= 1.f, "fBm 재현 · 범위");

        const auto pts = poissonDisk({ 10.f, 6.f }, 0.5f, 123);
        float minD = 1e9f;
        for (size_t i = 0; i < pts.size(); ++i)
            for (size_t j = i + 1; j < pts.size(); ++j) minD = std::min(minD, glm::length(pts[i] - pts[j]));
        bool inside = true;
        for (const auto& p : pts) inside = inside && p.x >= 0.f && p.y >= 0.f && p.x < 10.f && p.y < 6.f;
        // 꽉 찬 포아송 원판의 밀도는 대략 0.6 ~ 0.7 / (π r²/4 … ) — 점 수가 넓이 / (r² · 2) 보다 많으면 비는 곳이 적음
        check(minD >= 0.5f - 1e-5f && inside, "포아송 원판: 모든 점이 최소 거리 이상, 영역 안");
        check(pts.size() > (size_t)(60.f / (0.5f * 0.5f) * 0.5f), "포아송 원판이 영역을 채움(점 " + std::to_string(pts.size()) + "개)");
        check(poissonDisk({ 10.f, 6.f }, 0.5f, 123).size() == pts.size(), "같은 시드면 같은 점");
    }

    std::printf("\n== 6. 포즈 필터 · 속도 추정 (D1) ==\n");
    {
        OneEuroFilter<float> f;
        f.minCutoff = 1.f; f.beta = 0.f;
        Pcg32 r(5);
        double errRaw = 0, errF = 0;
        for (int i = 0; i < 900; ++i) {
            const float noisy = 1.f + r.range(-0.01f, 0.01f);
            const float y = f.filter(noisy, 1.f / 90.f);
            if (i > 200) { errRaw += std::fabs(noisy - 1.f); errF += std::fabs(y - 1.f); }
        }
        check(errF < errRaw * 0.3, "정지한 신호의 떨림을 크게 줄임");
        OneEuroFilter<float> slow, fast;
        slow.beta = 0.f; fast.beta = 1.f;
        float ys = 0, yf = 0;
        for (int i = 0; i < 45; ++i) { const float x = 2.f * (float)i / 90.f; ys = slow.filter(x, 1.f / 90.f); yf = fast.filter(x, 1.f / 90.f); }
        const float truth = 2.f * 44.f / 90.f;
        check(std::fabs(truth - yf) < std::fabs(truth - ys) * 0.5f, "beta 를 올리면 빠른 움직임에 덜 늦음");
        OneEuroFilter<glm::vec3> fv;
        check(fv.filter(glm::vec3(1, 2, 3), 0.011f) == glm::vec3(1, 2, 3), "첫 표본은 그대로");

        OneEuroRotationFilter fr;
        const glm::quat g = glm::angleAxis(0.5f, glm::vec3(0, 1, 0));
        glm::quat out{};
        for (int i = 0; i < 400; ++i) out = fr.filter(g, 1.f / 90.f);
        check(nearRot(out, g, 1e-5f), "회전 필터가 정지 목표에 닿음");

        LinearVelocityEstimator<8> est;
        Pcg32 n(11);
        const glm::vec3 v(1.5f, -0.5f, 2.f);
        float t = 0.f;
        for (int i = 0; i < 8; ++i) { t += (i % 2) ? 0.010f : 0.012f; est.push(t, v * t + glm::vec3(n.range(-1e-4f, 1e-4f))); }
        check(near(est.velocity(), v, 0.05f), "고르지 않은 간격에서도 직선 맞춤 속도가 맞음");
        LinearVelocityEstimator<4> one;
        one.push(0.f, glm::vec3(1.f));
        check(one.velocity() == glm::vec3(0.f), "표본 하나면 속도 0");
    }

    std::printf("\n== 7. 두 뼈 IK · 스윙-트위스트 (D2) ==\n");
    {
        const glm::vec3 root(0), target(0.4f, 0.1f, -0.3f), pole(0, -1, 0);
        const TwoBoneResult r = solveTwoBone(root, 0.3f, 0.3f, target, pole);
        check(r.reached && near(r.end, target), "닿는 목표에 끝점이 닿음");
        check(near(glm::length(r.mid - root), 0.3f) && near(glm::length(r.end - r.mid), 0.3f), "뼈 길이 유지");
        check(glm::dot(r.mid - 0.5f * (root + target), pole) > 0.f, "팔꿈치가 pole 쪽으로 굽음");
        const TwoBoneResult far = solveTwoBone(root, 0.3f, 0.3f, glm::vec3(0, 0, -2), pole);
        check(!far.reached && near(far.end, { 0, 0, -0.6f }, 1e-3f), "먼 목표는 그쪽으로 쭉 폄");
        const TwoBoneResult lined = solveTwoBone(root, 0.3f, 0.2f, glm::vec3(0, 0, -0.4f), glm::vec3(0, 0, -5));
        check(lined.reached && std::isfinite(lined.mid.x), "pole 이 목표 방향과 나란해도 풀림");

        const glm::vec3 axis(0, 0, 1);
        const glm::quat twist = glm::angleAxis(0.8f, axis);
        const glm::quat swing = glm::angleAxis(0.5f, glm::vec3(1, 0, 0));
        glm::quat s, tw;
        swingTwist(swing * twist, axis, s, tw);
        check(nearRot(tw, twist) && nearRot(s, swing), "swing · twist 로 만든 회전을 그대로 나눔");
        check(near(twistAngle(glm::angleAxis(-1.1f, axis), axis), -1.1f), "비틀림 각 부호");
        check(nearRot(s * tw, swing * twist), "다시 곱하면 원래 회전");
        check(nearRot(rotationBetween({ 1, 0, 0 }, { 0, 1, 0 }), glm::angleAxis(glm::radians(90.f), glm::vec3(0, 0, 1))), "두 방향 사이 최소 회전");
        const glm::quat flip = rotationBetween({ 1, 0, 0 }, { -1, 0, 0 });
        check(near(flip * glm::vec3(1, 0, 0), { -1, 0, 0 }), "반대 방향도 180° 로 돌림");
    }

    std::printf("\n결과: %s (검사 %d, 실패 %d)\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
