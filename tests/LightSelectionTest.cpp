// 라이트 선택(DEF-33)과 엔진 상한 설정(DEF-10)의 검산입니다. Vulkan 도 창도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 화면 밖 라이트를 잘못 거르면 보이는 곳의 빛이 사라지고, 상한에서 방향광을 빼면
// 그림자를 던지는 라이트가 바뀝니다. 설정 파일을 잘못 읽으면 버퍼 크기가 틀어집니다. 모두 라이트를 수백 개 둔
// 화면을 보지 않고는 알아채기 어려우므로 숫자로 확인합니다.
//
// 실행: build/Release/HarukaLightTest.exe

#include "../src/core/EngineSettings.hpp"
#include "../src/renderer/LightSelection.hpp"

#include <glm/gtc/matrix_transform.hpp>

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

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
