// 두 눈 렌더링의 수학 검산입니다(DEF-09 · DEF-21 · DEF-31). Vulkan 도 창도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 눈별 비대칭 투영과 두 눈 컬링은 헤드셋 없이 화면으로 확인하기 어렵고,
// 틀리면 한쪽 눈에서만 물체가 사라지거나(양안 경합) 두 눈의 원근이 어긋나 곧바로 멀미로 이어집니다.
//
// 실행: build/Release/HarukaStereoTest.exe

#include "../src/components/CameraComponent.hpp"
#include "../src/renderer/FrustumCull.hpp"
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
        const glm::mat4 fromFov = CameraComponent::projFromFov(-std::atan(tanR), std::atan(tanR),
                                                               std::atan(tanUp), -std::atan(tanUp),
                                                               cam.zNear, cam.zFar);
        check(nearM(fromFov, cam.projMatrix(aspect), 1e-5f), "대칭 시야각으로 만든 투영이 projMatrix 와 같음");
    }

    // 2. 비대칭 시야(OpenXR XrFovf 형식): 각 방향의 끝이 NDC 가장자리에, near/far 가 깊이 0/1 에 갑니다.
    //    Vulkan 은 클립 Y 가 아래라 위쪽 끝이 y = -1 입니다.
    std::printf("\n== 2. 비대칭 시야 ==\n");
    {
        const float L = glm::radians(-52.f), R = glm::radians(42.f);
        const float U = glm::radians(45.f),  D = glm::radians(-55.f);
        const float n = 0.1f, f = 100.f;
        const glm::mat4 P = CameraComponent::projFromFov(L, R, U, D, n, f);
        const float z = -2.f;   // 눈 앞 2m (눈은 -Z 를 봄)
        auto at = [&](float ax, float ay) { return glm::vec3(std::tan(ax) * 2.f, std::tan(ay) * 2.f, z); };
        const glm::vec3 left  = toNdc(P, at(L, 0.f)), right = toNdc(P, at(R, 0.f));
        const glm::vec3 up    = toNdc(P, at(0.f, U)), down  = toNdc(P, at(0.f, D));
        check(std::fabs(left.x + 1.f) < 1e-4f && std::fabs(right.x - 1.f) < 1e-4f, "왼쪽 · 오른쪽 끝이 x = -1 · +1");
        check(std::fabs(up.y + 1.f) < 1e-4f && std::fabs(down.y - 1.f) < 1e-4f, "위쪽 끝이 y = -1, 아래쪽 끝이 y = +1(Vulkan)");
        const glm::vec3 nearPt = toNdc(P, glm::vec3(0.f, 0.f, -n)), farPt = toNdc(P, glm::vec3(0.f, 0.f, -f));
        check(std::fabs(nearPt.z) < 1e-5f && std::fabs(farPt.z - 1.f) < 1e-4f, "near · far 가 깊이 0 · 1");
        const glm::vec3 ahead = toNdc(P, glm::vec3(0.f, 0.f, z));
        check(ahead.x > 0.f, "바깥(왼쪽)이 넓은 왼쪽 눈에서는 정면이 화면 가운데보다 오른쪽");
    }

    // 3. 두 눈 컬링(DEF-31). 왼쪽 눈은 왼쪽이 넓고 오른쪽 눈은 오른쪽이 넓습니다. 오른쪽 눈에만
    //    보이는 물체를 왼쪽 눈 절두체 하나로 거르면 버려집니다. 예전 단안 컬링이 바로 그 경우입니다.
    std::printf("\n== 3. 두 눈 컬링 ==\n");
    {
        const float n = 0.1f, f = 100.f, ipd = 0.064f;
        const glm::mat4 projL = CameraComponent::projFromFov(glm::radians(-52.f), glm::radians(42.f),
                                                             glm::radians(45.f), glm::radians(-50.f), n, f);
        const glm::mat4 projR = CameraComponent::projFromFov(glm::radians(-42.f), glm::radians(52.f),
                                                             glm::radians(45.f), glm::radians(-50.f), n, f);
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

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
