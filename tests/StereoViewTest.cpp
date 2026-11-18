// 두 눈 렌더링의 수학 검산입니다(DEF-09 · DEF-21 · DEF-31). Vulkan 도 창도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 눈별 비대칭 투영과 두 눈 컬링은 헤드셋 없이 화면으로 확인하기 어렵고,
// 틀리면 한쪽 눈에서만 물체가 사라지거나(양안 경합) 두 눈의 원근이 어긋나 곧바로 멀미로 이어집니다.
//
// 실행: build/Release/HarukaStereoTest.exe

#include "../src/components/CameraComponent.hpp"
#include "../src/math/Projection.hpp"
#include "../src/renderer/FrustumCull.hpp"
#include "../src/xr/SyntheticEyes.hpp"
#include "../src/renderer/ScreenSize.hpp"
#include "../src/components/HeightFogComponent.hpp"
#include "../src/scene/ComponentFields.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

bool nearM(const glm::mat4& a, const glm::mat4& b, float eps) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(a[c][r] - b[c][r]) > eps) return false;
    return true;
}

// 눈 공간의 점을 NDC 로.
glm::vec3 toNdc(const glm::mat4& proj, const glm::vec3& eyePos) {
    const glm::vec4 c = proj * glm::vec4(eyePos, 1.f);
    return glm::vec3(c) / c.w;
}

} // namespace

int main() {
    std::printf("Haruka 두 눈 렌더링 검산\n");

    // 1. 대칭 시야면 projFromFov 가 데스크톱 투영(projMatrix)과 같아야 합니다.
    std::printf("\n== 1. 대칭 시야 = 데스크톱 투영 ==\n");
    {
        CameraComponent cam;   // fovY 45, near 0.1, far 100
        const float aspect = 1.6f;
        const float tanUp  = std::tan(glm::radians(cam.fovY) * 0.5f);
        const float tanR   = tanUp * aspect;
        const glm::mat4 fromFov = hmath::perspectiveFromFov(-std::atan(tanR), std::atan(tanR),
                                                          std::atan(tanUp), -std::atan(tanUp),
                                                          cam.zNear);
        check(nearM(fromFov, cam.projMatrix(aspect), 1e-5f), "대칭 시야각으로 만든 투영이 projMatrix 와 같음");
    }

    // 2. 비대칭 시야(OpenXR XrFovf 형식): 각 방향의 끝이 NDC 가장자리에, near/far 가 깊이 0/1 에 갑니다.
    //    Vulkan 은 클립 Y 가 아래라 위쪽 끝이 y = -1 입니다.
    std::printf("\n== 2. 비대칭 시야 ==\n");
    {
        const float L = glm::radians(-52.f), R = glm::radians(42.f);
        const float U = glm::radians(45.f),  D = glm::radians(-55.f);
        const float n = 0.1f, f = 100.f;
        const glm::mat4 P = hmath::perspectiveFromFov(L, R, U, D, n);
        const float z = -2.f;   // 눈 앞 2m (눈은 -Z 를 봄)
        auto at = [&](float ax, float ay) { return glm::vec3(std::tan(ax) * 2.f, std::tan(ay) * 2.f, z); };
        const glm::vec3 left  = toNdc(P, at(L, 0.f)), right = toNdc(P, at(R, 0.f));
        const glm::vec3 up    = toNdc(P, at(0.f, U)), down  = toNdc(P, at(0.f, D));
        check(std::fabs(left.x + 1.f) < 1e-4f && std::fabs(right.x - 1.f) < 1e-4f, "왼쪽 · 오른쪽 끝이 x = -1 · +1");
        check(std::fabs(up.y + 1.f) < 1e-4f && std::fabs(down.y - 1.f) < 1e-4f, "위쪽 끝이 y = -1, 아래쪽 끝이 y = +1(Vulkan)");
        const glm::vec3 nearPt = toNdc(P, glm::vec3(0.f, 0.f, -n)), farPt = toNdc(P, glm::vec3(0.f, 0.f, -f));
        // Reverse-Z 무한 원평면(C1): 깊이 = near / 거리. 가까운 면 1, zFar(100 m) 0.001, 무한히 먼 곳 0.
        check(std::fabs(nearPt.z - 1.f) < 1e-5f && std::fabs(farPt.z - n / f) < 1e-6f, "Reverse-Z: 가까운 면 깊이 1, 100 m 는 near / 100");
        const glm::vec4 inf = P * glm::vec4(0.f, 0.f, -1.f, 0.f);   // -Z 방향의 무한히 먼 점
        check(std::fabs(inf.z) < 1e-7f && inf.w > 0.f, "무한히 먼 점의 깊이 0(무한 원평면)");
        // 절두체: 먼 쪽 평면은 법선 0(늘 통과) → replaceInfinitePlane 으로 zFar 평면
        auto planes = extractFrustumPlanes(P);
        int degenerate = 0;
        for (const auto& pl : planes) degenerate += glm::dot(pl.normal, pl.normal) == 0.f;
        check(degenerate == 1 && sphereInFrustum(planes, glm::vec3(0.f, 0.f, -1e6f), 1.f), "무한 원평면 투영의 먼 평면은 늘 통과");
        replaceInfinitePlane(planes, glm::mat4(1.f), f);
        check(sphereInFrustum(planes, glm::vec3(0.f, 0.f, -99.f), 0.5f) && !sphereInFrustum(planes, glm::vec3(0.f, 0.f, -101.f), 0.5f),
              "zFar 평면으로 바꾸면 100 m 너머는 컬링");
        const glm::mat4 view = glm::lookAt(glm::vec3(3.f, 1.f, 2.f), glm::vec3(3.f, 1.f, -5.f), glm::vec3(0.f, 1.f, 0.f));
        auto wp = extractFrustumPlanes(P * view);
        replaceInfinitePlane(wp, view, 10.f);
        check(sphereInFrustum(wp, glm::vec3(3.f, 1.f, -7.f), 0.1f) && !sphereInFrustum(wp, glm::vec3(3.f, 1.f, -8.5f), 0.1f),
              "옮긴 카메라도 zFar(10 m) 평면이 월드 자리에 놓임");
        const glm::vec3 ahead = toNdc(P, glm::vec3(0.f, 0.f, z));
        check(ahead.x > 0.f, "바깥(왼쪽)이 넓은 왼쪽 눈에서는 정면이 화면 가운데보다 오른쪽");
    }

    // 3. 두 눈 컬링(DEF-31). 왼쪽 눈은 왼쪽이 넓고 오른쪽 눈은 오른쪽이 넓습니다. 오른쪽 눈에만
    //    보이는 물체를 왼쪽 눈 절두체 하나로 거르면 버려집니다. 예전 단안 컬링이 바로 그 경우입니다.
    std::printf("\n== 3. 두 눈 컬링 ==\n");
    {
        const float n = 0.1f, f = 100.f, ipd = 0.064f;
        const glm::mat4 projL = hmath::perspectiveFromFov(glm::radians(-52.f), glm::radians(42.f),
                                                        glm::radians(45.f), glm::radians(-50.f), n);
        const glm::mat4 projR = hmath::perspectiveFromFov(glm::radians(-42.f), glm::radians(52.f),
                                                        glm::radians(45.f), glm::radians(-50.f), n);
        // 머리는 원점에서 -Z 를 봅니다. 눈은 좌우로 IPD 의 절반씩.
        const glm::mat4 viewL = glm::translate(glm::mat4(1.f), glm::vec3( ipd * 0.5f, 0.f, 0.f));
        const glm::mat4 viewR = glm::translate(glm::mat4(1.f), glm::vec3(-ipd * 0.5f, 0.f, 0.f));
        const std::array<std::array<FrustumPlane, 6>, 2> planes = {
            extractFrustumPlanes(projL * viewL), extractFrustumPlanes(projR * viewR) };

        // 오른쪽 47°, 5m 앞: 왼쪽 눈(오른쪽 42°)에는 안 보이고 오른쪽 눈(오른쪽 52°)에는 보임.
        const float a = glm::radians(47.f);
        const glm::vec3 rightOnly(5.f * std::sin(a), 0.f, -5.f * std::cos(a));
        const float r = 0.05f;
        check(!sphereInFrustum(planes[0], rightOnly, r), "오른쪽 47° 물체는 왼쪽 눈 절두체 밖");
        check( sphereInFrustum(planes[1], rightOnly, r), "같은 물체가 오른쪽 눈 절두체 안");
        check( sphereInAnyView(planes.data(), 2, rightOnly, r), "두 눈 판정은 그린다(예전 단안 판정은 버렸음)");

        // 두 눈 모두 밖(오른쪽 60°)이면 버립니다.
        const float b = glm::radians(60.f);
        const glm::vec3 outside(5.f * std::sin(b), 0.f, -5.f * std::cos(b));
        check(!sphereInAnyView(planes.data(), 2, outside, r), "두 눈 모두 밖이면 버린다");

        // 눈 하나(단안)면 그 눈 판정과 같습니다.
        check(sphereInAnyView(planes.data(), 1, rightOnly, r) == sphereInFrustum(planes[0], rightOnly, r),
              "뷰가 하나면 단안 판정과 같음");
    }

    // 4. 합성 눈(창 없는 렌더 경로의 --synthetic-eyes). XR 런타임이 주는 형식으로 스테레오 흉내와 같은 두 눈을 만듭니다.
    std::printf("\n== 4. 합성 눈 ==\n");
    {
        const float ipd = hxr::kSyntheticIpd, headY = 1.6f;
        const float tanUp = std::tan(glm::radians(45.f) * 0.5f), aspect = 1.f;
        const std::array<XrEyePose, 2> e = hxr::syntheticEyes(ipd, tanUp, aspect, headY);
        check(e[0].position == glm::vec3(-ipd * 0.5f, headY, 0.f) && e[1].position == glm::vec3(ipd * 0.5f, headY, 0.f),
              "왼쪽 눈 x = -IPD/2, 오른쪽 +IPD/2, 높이 headY");
        check(e[0].qw == 1.f && e[0].qx == 0.f && e[0].qy == 0.f && e[0].qz == 0.f &&
              e[1].qw == 1.f && e[1].qx == 0.f && e[1].qy == 0.f && e[1].qz == 0.f,
              "두 눈 방향이 단위 쿼터니언(정면 -Z)");

        // 스테레오 흉내(Renderer::gatherViews)와 같은 공식: 가로 시야 = 2 · tanUp · 비율, 바깥 55% · 코 쪽 45%.
        const float tanWidth = 2.f * tanUp * aspect;
        check(e[0].angleLeft == std::atan(-0.55f * tanWidth) && e[0].angleRight == std::atan(0.45f * tanWidth),
              "왼쪽 눈: 왼쪽(바깥) = -atan(55%), 오른쪽(코 쪽) = +atan(45%)");
        check(e[1].angleLeft == -e[0].angleRight && e[1].angleRight == -e[0].angleLeft, "오른쪽 눈은 왼쪽 눈의 거울");
        check(e[0].angleUp == -e[0].angleDown && e[1].angleUp == -e[1].angleDown && e[0].angleUp == std::atan(tanUp),
              "위 = -아래 = atan(tanUp)");

        CameraComponent cam;   // near 0.1 · far 100
        bool finite = true, ahead = true;
        for (const XrEyePose& p : e) {
            const glm::mat4 proj = hmath::perspectiveFromFov(p.angleLeft, p.angleRight, p.angleUp, p.angleDown,
                                                             cam.zNear);
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r) finite = finite && std::isfinite(proj[c][r]);
            const glm::mat4 view = glm::inverse(glm::translate(glm::mat4(1.f), p.position));
            ahead = ahead && sphereInFrustum(extractFrustumPlanes(proj * view), glm::vec3(0.f, headY, -2.f), 0.001f);
        }
        check(finite, "두 눈 투영 행렬이 모두 유한");
        check(ahead, "정면 2m 의 점이 두 눈 절두체에 모두 들어감");

        const std::array<XrEyePose, 2> wide = hxr::syntheticEyes(ipd, tanUp, 4.f / 3.f, 0.f);
        check(wide[0].angleLeft < e[0].angleLeft && wide[0].position.y == 0.f,
              "화면 비율이 넓으면 가로 시야가 넓어짐, 리그가 없으면 높이 0");
    }

    // 화면 크기 · LOD · 작은 물체(수학 보강 O6 · O7).
    std::printf("\n== 화면 크기 · LOD 이력 띠 · 작은 물체 컬링 ==\n");
    {
        // 시야 90°(projY = 1): 거리 10m 의 반지름 1m 구는 지름 ÷ 높이 = 0.1.
        CameraComponent cam;
        cam.fovY = 90.f;
        const float py = std::fabs(cam.projMatrix(1.f)[1][1]);
        check(std::fabs(py - 1.f) < 1e-5f, "시야 90° 투영의 y 배율은 1");
        const float s = hscreen::screenSize(1.f, 10.f, py);
        check(std::fabs(s - 0.1f) < 1e-6f, "거리 10m · 반지름 1m · 시야 90° → 화면 크기 0.1");
        const float edge = std::tan(std::asin(1.f / 10.f));   // 구의 실제 투영 가장자리(tan 공간, 시선 위)
        check(std::fabs(s - edge) / edge < 0.01f, "시선 가까이에서 근사 오차 1% 미만");
        check(hscreen::screenSize(1.f, 0.5f, py) == 1.f, "카메라가 구 안이면 화면을 덮음(1 · projY)");
        cam.fovY = 45.f;
        check(hscreen::screenSize(1.f, 10.f, std::fabs(cam.projMatrix(1.f)[1][1])) > s, "시야가 좁으면 같은 물체가 더 크게 보임");

        const float th[2] = { 0.5f, 0.2f };
        int lod = 0;
        lod = hscreen::selectLod(0.6f, th, lod);   check(lod == 0, "크기 0.6 → LOD 0");
        lod = hscreen::selectLod(0.47f, th, lod);  check(lod == 0, "경계 0.5 아래라도 띠(0.45 ~ 0.55) 안이면 유지");
        lod = hscreen::selectLod(0.44f, th, lod);  check(lod == 1, "0.45 아래로 내려가면 LOD 1");
        lod = hscreen::selectLod(0.53f, th, lod);  check(lod == 1, "다시 0.5 를 넘어도 띠 안이면 LOD 1 유지(깜빡임 없음)");
        lod = hscreen::selectLod(0.56f, th, lod);  check(lod == 0, "0.55 를 넘으면 LOD 0 으로");
        lod = hscreen::selectLod(0.01f, th, lod);  check(lod == 2, "아주 작으면 가장 거친 단계(경계 수)");
        lod = hscreen::selectLod(0.9f, th, lod);   check(lod == 0, "아주 크면 한 번에 가장 고운 단계");

        check(hscreen::tooSmall(0.0004f, 1920.f) && !hscreen::tooSmall(0.0006f, 1920.f),
              "높이 1920 화소에서 지름 1 화소(크기 약 0.00052)를 경계로 작은 물체를 가름");
    }

    std::printf("\n== 지수 고도 안개 · 노출 보정(2026-11-17) ==\n");
    {
        using hfog::amount;
        // 수평 광선(높이 일정): 적분 = 밀도 × 거리 → 안개 = 1 − exp(−ρ·d)
        const float a = amount({ 0, 0, 0 }, { 0, 0, -50 }, 0.02f, 0.2f, 0.f, 0.f, 1.f);
        check(std::fabs(a - (1.f - std::exp(-0.02f * 50.f))) < 1e-5f, "수평 광선: 1 − exp(−밀도 × 거리)");
        // 높이 h 의 수평 광선: 밀도 × exp(−k·h)
        const float hi = amount({ 0, 10, 0 }, { 0, 10, -50 }, 0.02f, 0.2f, 0.f, 0.f, 1.f);
        check(std::fabs(hi - (1.f - std::exp(-0.02f * std::exp(-2.f) * 50.f))) < 1e-5f, "높이 10 m 에서는 밀도 × exp(−0.2 × 10)");
        // 기울어진 광선: 수치 적분과 비교
        const glm::vec3 e(0, 1, 0), p(30, 25, -40);
        double integral = 0.0;
        const int N = 20000;
        for (int i = 0; i < N; ++i) {
            const glm::vec3 q = e + (p - e) * ((i + 0.5f) / N);
            integral += 0.05 * std::exp(-0.15 * (q.y - 2.0)) * glm::length(p - e) / N;
        }
        const float slant = amount(e, p, 0.05f, 0.15f, 2.f, 0.f, 1.f);
        check(std::fabs(slant - (float)(1.0 - std::exp(-integral))) < 1e-4f, "기울어진 광선의 해석 적분 = 수치 적분");
        check(amount({ 0, 0, 0 }, { 0, 0, -5 }, 0.02f, 0.2f, 0.f, 10.f, 1.f) == 0.f, "시작 거리 안은 안개 없음");
        check(amount({ 0, 0, 0 }, { 0, 0, -5000 }, 0.5f, 0.2f, 0.f, 0.f, 0.6f) == 0.6f, "최대 불투명도에서 멈춤");
        check(amount({ 0, 0, 0 }, { 0, 0, -50 }, 0.f, 0.2f, 0.f, 0.f, 1.f) == 0.f, "밀도 0 이면 안개 없음");
        const float flat = amount({ 0, 0, 0 }, { 0, 1e-6f, -50 }, 0.02f, 0.2f, 0.f, 0.f, 1.f);
        check(std::isfinite(flat) && std::fabs(flat - a) < 1e-4f, "거의 수평인 광선도 0 으로 나누지 않음");

        CameraComponent cam;
        ComponentFieldWriter w0;
        cam.writeFields(w0);
        bool hasEv = false;
        for (const auto& kv : w0.fields()) hasEv = hasEv || kv.first == "exposureEv";
        check(!hasEv, "노출 보정 0 이면 쓰지 않음");
        cam.exposureEv = -1.5f;
        ComponentFieldWriter w1;
        cam.writeFields(w1);
        CameraComponent back;
        for (const auto& kv : w1.fields()) back.readField(kv.first, kv.second);
        check(back.exposureEv == -1.5f, "노출 보정 왕복");
        check(back.readField("exposureEv", "20") && back.exposureEv == -1.5f, "범위 밖 노출 보정은 거부");
    }

    // ── S5c: LOD 고르기 순수 함수(pickLodMesh) ──────────────────────────────────
    {
        std::printf("\n== S5c LOD 고르기 순수 함수 ==\n");
        const auto* fine = reinterpret_cast<const MeshHandle*>(uintptr_t(0x100));
        auto*       lod1 = reinterpret_cast<MeshHandle*>(uintptr_t(0x200));
        auto*       lod2 = reinterpret_cast<MeshHandle*>(uintptr_t(0x300));
        MeshHandle* lods[2] = { lod1, lod2 };
        const float thr[2]  = { 0.5f, 0.2f };

        // 이력 띠: 0.5*(1-0.1)=0.45 아래로 내려가야 1단계, 0.5*(1+0.1)=0.55 위로 올라가야 0단계
        const auto pStay0 = hscreen::pickLodMesh(0.47f, fine, lods, 2, thr, 2, 0, true);
        const auto pDrop1 = hscreen::pickLodMesh(0.44f, fine, lods, 2, thr, 2, 0, true);
        const auto pStay1 = hscreen::pickLodMesh(0.52f, fine, lods, 2, thr, 2, pDrop1.level, true);
        const auto pBack0 = hscreen::pickLodMesh(0.56f, fine, lods, 2, thr, 2, pStay1.level, true);
        check(pStay0.level == 0 && pStay0.mesh == fine &&
              pDrop1.level == 1 && pDrop1.mesh == lod1 &&
              pStay1.level == 1 && pStay1.mesh == lod1 &&
              pBack0.level == 0 && pBack0.mesh == fine,
              "이력 띠: 0.47 유지 -> 0.44 단계1 -> 0.52 유지 -> 0.56 단계0");

        const auto pOff = hscreen::pickLodMesh(0.05f, fine, lods, 2, thr, 2, 2, false);
        check(pOff.level == 0 && pOff.mesh == fine, "autoLod 꺼짐: 늘 단계 0 · 가장 고운 메시");

        MeshHandle* sparse[2] = { lod1, nullptr };
        const auto pFallback = hscreen::pickLodMesh(0.05f, fine, sparse, 2, thr, 2, 2, true);
        check(pFallback.level == 2 && pFallback.mesh == lod1, "빈 단계 물러남: lod2 비면 lod1 반환");

        // 메시가 바뀌어 current 를 0 으로 되돌렸을 때 이력 띠 안(0.47)이면 0 유지
        const auto pReset = hscreen::pickLodMesh(0.47f, fine, lods, 2, thr, 2, 0, true);
        check(pReset.level == 0 && pReset.mesh == fine, "메시 교체 시 0 으로 되돌려 고운 메시 유지");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
