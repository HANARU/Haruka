// 라이트 선택(DEF-33)과 엔진 상한 설정(DEF-10)의 검산입니다. Vulkan 도 창도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 화면 밖 라이트를 잘못 거르면 보이는 곳의 빛이 사라지고, 상한에서 방향광을 빼면
// 그림자를 던지는 라이트가 바뀝니다. 설정 파일을 잘못 읽으면 버퍼 크기가 틀어집니다. 모두 라이트를 수백 개 둔
// 화면을 보지 않고는 알아채기 어려우므로 숫자로 확인합니다.
//
// 실행: build/Release/HarukaLightTest.exe

#include "../src/core/EngineSettings.hpp"
#include "../src/renderer/LightSelection.hpp"
#include "../src/renderer/SpotCone.hpp"
#include "../src/renderer/ShadowFit.hpp"
#include "../src/renderer/FrustumCull.hpp"
#include "../src/renderer/SkyIbl.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

// 원점에서 -Z 를 보는 눈. 시야 90°, 깊이 [0,1].
std::array<FrustumPlane, 6> eyePlanes(const glm::vec3& eye) {
    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(90.f), 1.f, 0.1f, 100.f);
    proj[1][1] *= -1.f;
    const glm::mat4 view = glm::lookAtRH(eye, eye + glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, 1.f, 0.f));
    return extractFrustumPlanes(proj * view);
}

// 방향광의 위치는 쓰지 않지만 일부러 멀리 둡니다. 거리 순으로만 고르면 방향광이 뒤로 밀려 검사가 실패합니다.
hlight::Candidate dirLight()  { hlight::Candidate c; c.directional = true; c.position = { 0.f, 0.f, 1000.f }; return c; }
hlight::Candidate point(glm::vec3 p, float r, float b = 1.f) {
    hlight::Candidate c; c.position = p; c.radius = r; c.brightness = b; return c;
}

} // namespace

int main() {
    std::printf("Haruka 라이트 선택 · 상한 설정 검산\n");
    const std::array<FrustumPlane, 6> mono = eyePlanes({ 0.f, 0.f, 0.f });

    std::printf("\n== 1. 화면 밖 거르기 ==\n");
    {
        const std::vector<hlight::Candidate> L = {
            point({ 0.f, 0.f, -10.f }, 2.f),    // 0: 앞
            point({ 0.f, 0.f,  10.f }, 2.f),    // 1: 뒤, 영향 구가 눈까지 닿지 않음
            dirLight(),                         // 2: 방향광은 늘 남음
            point({ 0.f, 0.f,  1.5f }, 3.f),    // 3: 뒤지만 영향 구가 눈 앞까지 덮음
        };
        const auto r = hlight::select(L, &mono, 1, { 0.f, 0.f, 0.f }, 256);
        check(r.selected == std::vector<uint32_t>({ 0, 2, 3 }), "뒤에 있고 닿지 않는 점광원만 거름(순서 그대로)");
        check(r.culled == 1 && r.overBudget == 0, "거른 수 1, 상한은 안 넘음");
        const auto none = hlight::select(L, nullptr, 0, { 0.f, 0.f, 0.f }, 256);
        check(none.selected.size() == 4 && none.culled == 0, "카메라가 없으면 거르지 않음");
    }

    std::printf("\n== 2. 두 눈 ==\n");
    {
        // 오른쪽 눈(x=+3)에만 보이는 위치. 깊이 2m 에서 오른쪽 눈의 시야는 x 1~5, 왼쪽 눈(x=-3)은 x -5~-1 입니다.
        const std::array<std::array<FrustumPlane, 6>, 2> eyes = { eyePlanes({ -3.f, 0.f, 0.f }), eyePlanes({ 3.f, 0.f, 0.f }) };
        const std::vector<hlight::Candidate> L = { point({ 3.5f, 0.f, -2.f }, 0.5f) };
        check(hlight::select(L, eyes.data(), 1, {}, 256).selected.empty(), "왼쪽 눈만 보면 거름");
        check(hlight::select(L, eyes.data(), 2, {}, 256).selected.size() == 1, "두 눈이면 남김(한쪽에라도 보이면)");
    }

    std::printf("\n== 3. 상한 ==\n");
    {
        // 상한 3: 방향광 2개 + 점광원 4개(모두 보임). 방향광이 먼저, 그다음 거리/반경이 작은 하나.
        const std::vector<hlight::Candidate> L = {
            point({ 0.f, 0.f, -40.f }, 50.f),   // 0: 0.8
            dirLight(),                         // 1
            point({ 0.f, 0.f, -5.f }, 2.f),     // 2: 2.5
            point({ 0.f, 0.f, -20.f }, 50.f),   // 3: 0.4 ← 가장 안쪽
            dirLight(),                         // 4
            point({ 0.f, 0.f, -30.f }, 50.f),   // 5: 0.6
        };
        const auto r = hlight::select(L, &mono, 1, { 0.f, 0.f, 0.f }, 3);
        check(r.selected == std::vector<uint32_t>({ 1, 3, 4 }), "방향광 둘 + 가장 가까운(거리/반경) 점광원, 원래 순서로");
        check(r.overBudget == 3, "상한을 넘어 뺀 수 3");
        const auto under = hlight::select(L, &mono, 1, { 0.f, 0.f, 0.f }, 6);
        check(under.selected == std::vector<uint32_t>({ 0, 1, 2, 3, 4, 5 }), "상한 안이면 순서를 바꾸지 않음");
        const auto tight = hlight::select(L, &mono, 1, { 0.f, 0.f, 0.f }, 1);
        check(tight.selected == std::vector<uint32_t>({ 1 }), "상한이 방향광 수보다 작으면 앞의 방향광부터");
        // 거리/반경이 같으면 밝은 쪽.
        const std::vector<hlight::Candidate> T = { point({ 0.f, 0.f, -10.f }, 5.f, 1.f), point({ 0.f, 0.f, -10.f }, 5.f, 4.f) };
        check(hlight::select(T, &mono, 1, {}, 1).selected == std::vector<uint32_t>({ 1 }), "같은 거리면 밝은 라이트");
    }

    std::printf("\n== 4. 상한 설정 파일(DEF-10) ==\n");
    {
        std::vector<std::string> msgs;
        const EngineLimits d = engine_settings::parse(engine_settings::defaultText(), msgs);
        const EngineLimits def;
        check(msgs.empty() && d.maxDrawRows == def.maxDrawRows && d.maxLights == def.maxLights &&
              d.maxBodies == def.maxBodies, "기본 파일을 읽으면 기본값, 알림 없음");

        msgs.clear();
        const EngineLimits l = engine_settings::parse(
            "# comment\n"
            "render.maxDrawRows = 32768\r\n"
            "physics.maxBodies=100\n"          // 범위 밖 → 256
            "render.maxLights=many\n"          // 깨진 값 → 기본 유지
            "render.fancy=1\n"                 // 모르는 이름
            "no equals here\n", msgs);
        check(l.maxDrawRows == 32768, "공백 · CRLF 가 있어도 읽음");
        check(l.maxBodies == EngineLimits::kMinBodies, "범위 밖 값은 범위 안으로");
        check(l.maxLights == def.maxLights, "깨진 값은 기본값 유지");
        check(msgs.size() == 4, "범위 · 깨진 값 · 모르는 이름 · '=' 없는 줄을 모두 알림(" + std::to_string(msgs.size()) + ")");

        msgs.clear();
        const EngineLimits big = engine_settings::parse("render.maxDrawRows=99999999\n", msgs);
        check(big.maxDrawRows == EngineLimits::kMaxDrawRowsCap && msgs.size() == 1, "너무 큰 행 수는 상한(Zone C 예산)으로");
    }

    std::printf("\n== 5. 스팟 원뿔 계수 ==\n");
    {
        auto t = [](glm::vec2 p, float c) { return std::clamp(c * p.x + p.y, 0.f, 1.f); };
        auto cd = [](float deg) { return std::cos(glm::radians(deg)); };
        const glm::vec2 def = hlight::spotConeParams(15.f, 30.f);
        check(std::fabs(t(def, cd(22.5f)) - 0.579f) < 1e-3f && t(def, cd(15.f)) > 0.9999f && t(def, cd(30.f)) < 1e-4f,
              "기본값(15, 30): 22.5° 에서 0.579, 15° 에서 1, 30° 에서 0");
        const glm::vec2 inv = hlight::spotConeParams(40.f, 30.f), eq = hlight::spotConeParams(30.f, 30.f);
        check(inv == eq && std::fabs(eq.x - 1000.f) < 1e-3f && std::fabs(eq.y + 866.03f) < 0.01f,
              "inner > outer (40, 30) 은 (30, 30) 과 같음(scale 1000, offset -866.03)");
        const glm::vec2 zero = hlight::spotConeParams(0.f, 0.f);
        check(std::fabs(zero.x - 1000.f) < 1e-3f && std::fabs(zero.y + 1000.f) < 1e-3f && t(zero, 0.9999f) == 0.f,
              "(0, 0): scale 1000 · offset -1000, 축 밖은 0");
        const glm::vec2 wide = hlight::spotConeParams(0.f, 89.f);
        check(std::fabs(wide.x - 1.0178f) < 1e-3f && std::fabs(wide.y + 0.01776f) < 1e-4f, "(0, 89): scale 1.0178, offset -0.01776");

        // 예전 셰이더 식과 같은 값인지(double 로 계산한 예전 식과 비교)
        const float pairs[][2] = { {15, 30}, {40, 30}, {30, 30}, {0, 0}, {0, 89}, {0, 30}, {28, 30} };
        double worst = 0.0;
        for (const auto& pr : pairs) {
            const glm::vec2 p = hlight::spotConeParams(pr[0], pr[1]);
            const double ci = std::cos(pr[0] * 3.14159265358979 / 180.0), co = std::cos(pr[1] * 3.14159265358979 / 180.0);
            for (int i = 0; i <= 2000; ++i) {
                const double c = -1.0 + i * 0.001;
                const double oldT = std::clamp((c - co) / std::max(ci - co, 0.001), 0.0, 1.0);
                worst = std::max(worst, std::fabs(oldT - (double)t(p, (float)c)));
            }
        }
        check(worst <= 2e-4, "예전 식 clamp((cos - cosO) / max(cosI - cosO, 0.001)) 과 차이 2e-4 이하(" + std::to_string(worst) + ")");
    }

    std::printf("\n== 6. 방향광 섀도 맞춤(ShadowFit, 머리 중심 추종) ==\n");
    {
        const glm::vec3 dir = glm::normalize(glm::vec3(0.37139067f, 0.74278134f, 0.557086f));   // LightingProbe 태양
        const float     R   = hshadow::kRadius;
        const uint32_t  N   = 4096;
        const float     t   = 2.f * R / (float)N;
        auto ndc = [](const glm::mat4& m, const glm::vec3& p) {
            const glm::vec4 c = m * glm::vec4(p, 1.f);
            return glm::vec3(c) / c.w;
        };
        // 빛 행렬의 x · y 축(ShadowFit 의 lookAt 과 같은 규칙). 머리 위치를 빛 공간 텍셀 좌표로 정해 시험합니다.
        const glm::vec3 ax = glm::normalize(glm::cross(hshadow::lightUp(dir), dir));
        const glm::vec3 ay = glm::cross(dir, ax);
        const glm::vec3 head = ax * (100.5f * t) + ay * (37.5f * t) + dir * 5.f;   // 텍셀 칸의 한가운데

        const hshadow::DirectionalFit fit = hshadow::fitDirectional(head, dir, R, N, true);
        const hshadow::DirectionalFit nc  = hshadow::fitDirectional(head, dir, R, N, false);

        check(std::fabs(fit.texelWorld - t) < 1e-7f, "텍셀 = 2R / N(R 20 m, 4096 이면 약 9.8 mm)");
        check(std::fabs(fit.depthRange - 2.f * R) < 1e-4f && std::fabs(nc.depthRange - (2.f * R + hshadow::kCasterReach)) < 1e-4f,
              "깊이 범위 = 2R(depthClamp) · 2R + kCasterReach(없을 때)");
        check(std::fabs(hshadow::receiverBiasNdc(t, fit.depthRange) * fit.depthRange - t) < 1e-7f,
              "리시버 바이어스 NDC 를 깊이 범위로 되돌리면 월드 길이");
        check(std::fabs(fit.fadeStart - 0.8f * R) < 1e-5f && std::fabs(fit.fadeEnd - (R - 2.f * t)) < 1e-5f,
              "페이드: 0.8R 에서 시작, R - 2텍셀에서 끝");

        // 머리 중심 반지름 R - 1텍셀 구(스냅으로 맵이 최대 1텍셀 어긋남)가 XY · 깊이 모두 맵 안
        bool inside = true;
        for (const glm::vec3& d : { ax, -ax, ay, -ay, dir, -dir })
            for (const hshadow::DirectionalFit* f : { &fit, &nc }) {
                const glm::vec3 q = ndc(f->viewProj, head + d * (R - t));
                inside = inside && q.x >= -1.f && q.x <= 1.f && q.y >= -1.f && q.y <= 1.f && q.z >= 0.f && q.z <= 1.f;
            }
        check(inside, "머리 중심 반지름 R - 1텍셀 구가 XY · 깊이 모두 맵 안(depthClamp 유무 두 경로)");

        // 텍셀 스냅: 칸 안의 작은 이동은 행렬 그대로, 큰 이동은 고정점이 정확히 정수 텍셀만큼 옮겨짐(격자가 월드에 고정)
        const hshadow::DirectionalFit small = hshadow::fitDirectional(head + ax * (0.4f * t) - ay * (0.4f * t), dir, R, N, true);
        check(small.viewProj == fit.viewProj, "텍셀보다 작은 머리 이동에는 행렬이 비트까지 그대로");
        const glm::vec3 fixedP(1.f, 0.f, -2.f);
        const hshadow::DirectionalFit big = hshadow::fitDirectional(head + ax * (3.7f * t) + ay * (11.2f * t), dir, R, N, true);
        const glm::vec3 a = ndc(fit.viewProj, fixedP) * (0.5f * (float)N), b = ndc(big.viewProj, fixedP) * (0.5f * (float)N);
        const float sx = a.x - b.x, sy = a.y - b.y;
        check(std::fabs(sx - std::round(sx)) < 1e-2f && std::fabs(sy - std::round(sy)) < 1e-2f && std::round(sx) != 0.f,
              "큰 머리 이동에는 같은 월드 점이 정수 텍셀만큼만 옮겨짐(" + std::to_string(sx) + ", " + std::to_string(sy) + ")");

        // 구보다 빛 쪽의 높은 캐스터: 컬링 행렬은 kCasterReach 까지 받음. depthClamp 경로의 투영 깊이는 0 아래(눌러 담김),
        // 대체 경로는 투영 깊이 범위 안.
        const glm::vec3 high = head + dir * (R + 50.f), tooHigh = head + dir * (R + hshadow::kCasterReach + 30.f);
        check(sphereInFrustum(extractFrustumPlanes(fit.cullViewProj), high, 1.75f) &&
              !sphereInFrustum(extractFrustumPlanes(fit.cullViewProj), tooHigh, 1.75f),
              "구보다 50 m 빛 쪽 캐스터는 컬링 통과, kCasterReach 를 넘으면 버림");
        check(ndc(fit.viewProj, high).z < 0.f && ndc(nc.viewProj, high).z >= 0.f && nc.cullViewProj == nc.viewProj,
              "depthClamp 경로는 near 앞(깊이 0 에 눌러 담김), 대체 경로는 투영 = 컬링 행렬로 범위 안");
        check(hshadow::lightUp(glm::vec3(0, 1, 0)) == glm::vec3(1, 0, 0) && hshadow::lightUp(dir) == glm::vec3(0, 1, 0),
              "거의 수직인 빛은 (1,0,0) 을 위로 씀");
    }

    std::printf("\n== 7. 하늘 IBL 굽기(SkyIbl) ==\n");
    {
        // Renderer.cpp skyColor 와 같아야 합니다(세 색, 0.30 배 = lighting_calibration::kSkyRadianceScale).
        auto sky = [](glm::vec3 d) {
            d = glm::normalize(d);
            const float t = d.y * 0.5f + 0.5f;
            const glm::vec3 top{ 0.17f, 0.40f, 0.80f }, hor{ 0.82f, 0.78f, 0.70f }, gnd{ 0.38f, 0.32f, 0.24f };
            const glm::vec3 c = (t > 0.5f) ? glm::mix(hor, top, (t - 0.5f) * 2.f) : glm::mix(gnd, hor, t * 2.f);
            return c * 0.30f;
        };
        auto lum = [](glm::vec3 c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
        auto basis = [](glm::vec3 N, glm::vec3& T, glm::vec3& B) {
            const glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
            T = glm::normalize(glm::cross(up, N)); B = glm::cross(N, T);
        };
        // 코사인 가중 Hammersley 4096 샘플로 구한 평균 복사휘도(기준)
        auto refIrr = [&](glm::vec3 N) {
            glm::vec3 T, B; basis(N, T, B);
            glm::vec3 s(0.f);
            const uint32_t n = 4096;
            for (uint32_t i = 0; i < n; ++i) {
                const float phi = 2.f * 3.14159265f * (float)i / n, ct = std::sqrt(1.f - hibl::radicalInverse(i));
                const float st = std::sqrt(std::max(0.f, 1.f - ct * ct));
                s += sky(T * (st * std::cos(phi)) + B * (st * std::sin(phi)) + N * ct);
            }
            return s / (float)n;
        };

        const glm::vec3 faces[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
        bool facesOk = true;
        for (int f = 0; f < 6; ++f) facesOk = facesOk && hibl::cubeDir(f, 0.f, 0.f) == faces[f];
        check(facesOk, "cubeDir 면 중심이 +X, -X, +Y, -Y, +Z, -Z");

        const glm::vec3 c0{ 0.3f, 0.5f, 0.7f };
        const hibl::SH9Rgb shU = hibl::projectSH9([&](glm::vec3) { return c0; }, 32);
        float worstU = 0.f;
        for (const glm::vec3& n : faces) worstU = std::max(worstU, glm::length(hibl::shMeanRadiance(shU, n) - c0) / glm::length(c0));
        check(worstU < 1e-3f, "균일 하늘이면 SH 평균 복사휘도 = 하늘 값(π 규약), 상대 " + std::to_string(worstU));

        const hibl::SH9Rgb sh = hibl::projectSH9(sky, 32);
        double se = 0.0, mx = 0.0, sumA = 0.0, sumB = 0.0; int cnt = 0;
        for (int f = 0; f < 6; ++f)
            for (int iy = 0; iy < 32; ++iy)
                for (int ix = 0; ix < 32; ++ix) {
                    const glm::vec3 N = glm::normalize(hibl::cubeDir(f, (ix + 0.5f) / 16.f - 1.f, (iy + 0.5f) / 16.f - 1.f));
                    const float a = lum(hibl::shMeanRadiance(sh, N)), b = lum(refIrr(N));
                    const double e = (a - b) / b;
                    se += e * e; mx = std::max(mx, std::fabs(e)); sumA += a; sumB += b; ++cnt;
                }
        const double rms = std::sqrt(se / cnt);
        check(rms <= 0.005 && mx <= 0.015, "SH9 조도가 4096 샘플 기준과 상대 RMS " + std::to_string(rms) + " · 최대 " + std::to_string(mx) + " (≤ 0.5% · 1.5%)");
        check(std::fabs(sumA / sumB - 1.0) <= 0.005, "텍셀 전체 평균이 기준과 0.5% 이내(하드튜닝 밝기 유지)");

        const glm::vec3 R0 = glm::normalize(glm::vec3(0.3f, 0.4f, -0.8f));
        check(hibl::prefilterGGX(sky, R0, 0.f, 64) == sky(R0), "프리필터 거칠기 0 이면 하늘 값 그대로");
        float worstC = 0.f;
        for (float pr : { 0.25f, 0.5f, 1.f })
            for (const glm::vec3& n : faces)
                worstC = std::max(worstC, glm::length(hibl::prefilterGGX([&](glm::vec3) { return c0; }, n, pr, 64) - c0));
        check(worstC < 1e-4f, "균일 하늘이면 모든 거칠기에서 프리필터가 상수");

        double pse = 0.0; int pcnt = 0;
        for (int mip = 1; mip <= 4; ++mip) {
            const int sz = 64 >> mip, step = (mip == 1) ? 2 : 1;
            for (int f = 0; f < 6; ++f)
                for (int iy = 0; iy < sz; iy += step)
                    for (int ix = 0; ix < sz; ix += step) {
                        const glm::vec3 R = glm::normalize(hibl::cubeDir(f, (ix + 0.5f) / sz * 2.f - 1.f, (iy + 0.5f) / sz * 2.f - 1.f));
                        const float a = lum(hibl::prefilterGGX(sky, R, mip / 4.f, 64)), b = lum(hibl::prefilterGGX(sky, R, mip / 4.f, 4096));
                        pse += ((a - b) / b) * ((a - b) / b); ++pcnt;
                    }
        }
        const double prms = std::sqrt(pse / pcnt);
        check(prms <= 0.01, "GGX 프리필터 64 샘플이 4096 샘플과 상대 RMS " + std::to_string(prms) + " (≤ 1%, mip 1 ~ 4)");
        const float up = lum(hibl::prefilterGGX(sky, { 0, 1, 0 }, 0.5f, 4096));
        const float hz = lum(hibl::prefilterGGX(sky, { 1, 0, 0 }, 0.5f, 4096));
        const float dn = lum(hibl::prefilterGGX(sky, { 0, -1, 0 }, 1.0f, 4096));
        check(std::fabs(up / 0.1300f - 1.f) < 0.01f && std::fabs(hz / 0.2006f - 1.f) < 0.01f && std::fabs(dn / 0.1436f - 1.f) < 0.01f,
              "기준점 휘도(위 0.5: " + std::to_string(up) + ", 수평 0.5: " + std::to_string(hz) + ", 아래 1.0: " + std::to_string(dn) + ")");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
