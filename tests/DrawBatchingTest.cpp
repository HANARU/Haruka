// 자동 인스턴싱(DEF-16)의 묶기와 행 배정 검산입니다. Vulkan 도 창도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 묶기가 틀리면 화면에서 물체가 사라지거나 다른 물체의 트랜스폼 · 머티리얼로
// 그려집니다. 행 번호와 드로우가 어긋나는 결함(DEF-01)이 예전에 실제로 있었으므로, 행마다 어느 메시가
// 들어갔는지와 드로우가 그 행들을 정확히 덮는지를 직접 확인합니다.
//
// 실행: build/Release/HarukaBatchTest.exe

#include "../src/renderer/DrawBatching.hpp"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

// 머티리얼은 주소만 묶음 키로 쓰므로 아무 객체의 주소면 됩니다.
int gMatA = 0, gMatB = 0;

// 지오메트리 두 종류(메가버퍼 안의 다른 범위).
hbatch::Candidate mesh(int geo, const void* mat, bool inCam, bool inLight,
                       bool transparent = false, float distSq = 0.f) {
    hbatch::Candidate c;
    c.material     = mat;
    c.indexCount   = geo == 0 ? 36 : 960;
    c.firstIndex   = geo == 0 ? 0 : 36;
    c.vertexOffset = geo == 0 ? 0 : 24;
    c.distSq       = distSq;
    c.inCam        = inCam;
    c.inLight      = inLight;
    c.transparent  = transparent;
    return c;
}

// 드로우가 덮는 행들이 실제로 그 지오메트리 · 머티리얼의 메시인지 확인합니다.
bool runMatches(const hbatch::Result& r, const std::vector<hbatch::Candidate>& c,
                const hbatch::Run& run, bool sameMaterial) {
    if (run.firstInstance + run.instanceCount > r.rowSource.size()) return false;
    const auto& first = c[r.rowSource[run.firstInstance]];
    for (uint32_t k = 0; k < run.instanceCount; ++k) {
        const auto& m = c[r.rowSource[run.firstInstance + k]];
        if (m.indexCount != run.indexCount || m.firstIndex != run.firstIndex ||
            m.vertexOffset != run.vertexOffset) return false;
        if (sameMaterial && m.material != first.material) return false;
    }
    return true;
}

} // namespace

int main() {
    std::printf("Haruka 자동 인스턴싱 검산\n");

    std::printf("\n== 1. 같은 지오메트리 · 머티리얼은 드로우 하나 ==\n");
    {
        const std::vector<hbatch::Candidate> c = {
            mesh(0, &gMatA, true, true), mesh(1, &gMatA, true, true), mesh(0, &gMatA, true, true),
        };
        const auto r = hbatch::build(c, 1024);
        check(r.opaque.size() == 2, "불투명 드로우 2개(상자 묶음 1 + 구 1)");
        check(r.opaqueInstances == 3, "그리는 메시는 3개");
        check(!r.opaque.empty() && r.opaque[0].instanceCount == 2, "첫 드로우는 상자 2개를 인스턴스로 그림");
        check(!r.opaque.empty() && r.opaque[0].firstInstance == 0 && r.rowSource.size() == 3 &&
              r.rowSource[0] == 0 && r.rowSource[1] == 2, "상자 두 개가 연속된 행 0 · 1 을 받음");
        bool all = true;
        for (const auto& run : r.opaque) all = all && runMatches(r, c, run, true);
        check(all, "모든 드로우가 같은 지오메트리 · 머티리얼의 행만 덮음");
    }

    std::printf("\n== 2. 머티리얼이 다르면 묶지 않음 ==\n");
    {
        const std::vector<hbatch::Candidate> c = { mesh(0, &gMatA, true, false), mesh(0, &gMatB, true, false) };
        const auto r = hbatch::build(c, 1024);
        check(r.opaque.size() == 2 && r.opaque[0].instanceCount == 1 && r.opaque[1].instanceCount == 1,
              "지오메트리가 같아도 머티리얼이 다르면 드로우 2개");
    }

    std::printf("\n== 3. 같은 것이 없으면 묶기 전과 똑같음 ==\n");
    {
        // 행 번호와 드로우 순서가 예전(메시마다 행 하나, 순회 순서)과 같아야 화면이 픽셀까지 같습니다.
        const std::vector<hbatch::Candidate> c = {
            mesh(0, &gMatA, true, true), mesh(1, &gMatB, true, false), mesh(1, &gMatA, true, true),
        };
        const auto r = hbatch::build(c, 1024);
        bool same = r.opaque.size() == 3;
        for (uint32_t i = 0; same && i < 3; ++i)
            same = r.opaque[i].instanceCount == 1 && r.opaque[i].firstInstance == i && r.rowSource[i] == i;
        check(same, "드로우마다 인스턴스 1개, 행 번호 = 순회 순서");
        check(r.shadow.size() == 2 && r.shadow[0].firstInstance == 0 && r.shadow[1].firstInstance == 2,
              "섀도는 라이트 안의 메시만, 같은 행으로");
    }

    std::printf("\n== 4. 섀도는 묶음의 앞부분 ==\n");
    {
        // 라이트 밖 · 안 · 밖 · 안 순서로 넣으면 묶음 안에서 라이트 안의 둘이 앞으로 옵니다.
        const std::vector<hbatch::Candidate> c = {
            mesh(0, &gMatA, true, false), mesh(0, &gMatA, true, true),
            mesh(0, &gMatA, true, false), mesh(0, &gMatA, true, true),
        };
        const auto r = hbatch::build(c, 1024);
        check(r.opaque.size() == 1 && r.opaque[0].instanceCount == 4, "카메라 드로우는 4개 묶음 하나");
        check(r.shadow.size() == 1 && r.shadow[0].firstInstance == r.opaque[0].firstInstance &&
              r.shadow[0].instanceCount == 2, "섀도 드로우는 같은 첫 행에서 2개만");
        bool litFirst = r.rowSource.size() == 4 && c[r.rowSource[0]].inLight && c[r.rowSource[1]].inLight &&
                        !c[r.rowSource[2]].inLight && !c[r.rowSource[3]].inLight;
        check(litFirst, "라이트 안의 인스턴스가 행 앞쪽에 모임");
        check(r.rowSource.size() == 4 && r.rowSource[0] == 1 && r.rowSource[1] == 3,
              "라이트 안끼리는 원래 순서를 지킴(안정 정렬)");
    }

    std::printf("\n== 5. 투명은 묶지 않고 뒤에서 앞으로 ==\n");
    {
        const std::vector<hbatch::Candidate> c = {
            mesh(0, &gMatA, true, false, true, 4.f), mesh(0, &gMatA, true, false, true, 25.f),
            mesh(0, &gMatA, true, false, true, 9.f),
        };
        const auto r = hbatch::build(c, 1024);
        check(r.opaque.empty() && r.transparent.size() == 3, "투명 3개는 드로우 3개");
        bool order = r.transparent.size() == 3 &&
                     r.rowSource[r.transparent[0].firstInstance] == 1 &&
                     r.rowSource[r.transparent[1].firstInstance] == 2 &&
                     r.rowSource[r.transparent[2].firstInstance] == 0;
        check(order, "먼 것부터(25 → 9 → 4)");
        const auto u = hbatch::build(c, 1024, false);
        check(u.transparent.size() == 3 && u.transparent[0].firstInstance == 0 &&
              u.transparent[2].firstInstance == 2, "카메라가 없으면 행 순서 그대로");
    }

    std::printf("\n== 6. 라이트에만 보이는 캐스터 ==\n");
    {
        // 머티리얼이 달라도 섀도는 지오메트리만으로 묶습니다. 행은 카메라 행 뒤에 붙습니다.
        const std::vector<hbatch::Candidate> c = {
            mesh(0, &gMatA, false, true), mesh(1, &gMatA, true, false),
            mesh(0, &gMatB, false, true),
        };
        const auto r = hbatch::build(c, 1024);
        check(r.opaque.size() == 1 && r.opaque[0].firstInstance == 0, "카메라 메시가 행 0");
        check(r.shadow.size() == 1 && r.shadow[0].firstInstance == 1 && r.shadow[0].instanceCount == 2,
              "캐스터 둘은 행 1 · 2 에서 섀도 드로우 하나");
        check(r.rowSource.size() == 3 && r.rowSource[1] == 0 && r.rowSource[2] == 2,
              "캐스터 행마다 원래 메시(머티리얼)를 기록");
    }

    std::printf("\n== 7. 행 예산 ==\n");
    {
        // 예산 3: 카메라 쪽 4개 중 3개만 그리고, 라이트 전용 캐스터는 남은 행이 없어 못 그립니다.
        const std::vector<hbatch::Candidate> c = {
            mesh(0, &gMatA, false, true),
            mesh(0, &gMatA, true, true), mesh(0, &gMatA, true, true), mesh(0, &gMatA, true, true),
            mesh(1, &gMatA, true, false),
        };
        const auto r = hbatch::build(c, 3);
        check(r.rowSource.size() == 3, "행은 예산만큼만");
        check(r.opaque.size() == 1 && r.opaque[0].instanceCount == 3, "묶음이 예산에서 잘림");
        check(r.droppedCam == 1, "카메라에 보이지만 못 그린 메시 1");
        check(r.droppedShadow == 1, "라이트 전용 캐스터는 카메라 뒤라 못 그림 1");
        check(r.opaqueInstances == 3, "그린 메시 3");
    }

    std::printf("\n== 8. 행마다 메시가 한 번씩 ==\n");
    {
        std::vector<hbatch::Candidate> c;
        for (int i = 0; i < 40; ++i)
            c.push_back(mesh(i % 2, (i % 3) ? (const void*)&gMatA : (const void*)&gMatB,
                             i % 5 != 0, i % 4 != 0, i % 7 == 0, (float)i));
        const auto r = hbatch::build(c, 1024);
        std::set<uint32_t> seen(r.rowSource.begin(), r.rowSource.end());
        uint32_t expect = 0;
        for (const auto& m : c) if (m.inCam || m.inLight) ++expect;
        check(seen.size() == r.rowSource.size() && r.rowSource.size() == expect,
              "보이는 메시는 모두 정확히 한 행씩 받음");
        bool all = true;
        for (const auto& run : r.opaque) all = all && runMatches(r, c, run, true);
        for (const auto& run : r.transparent) all = all && run.instanceCount == 1 && runMatches(r, c, run, true);
        for (const auto& run : r.shadow) all = all && runMatches(r, c, run, false);
        check(all, "모든 드로우가 자기 지오메트리의 행만 덮음");
        uint32_t covered = 0;
        for (const auto& run : r.opaque) covered += run.instanceCount;
        uint32_t camOpaque = 0;
        for (const auto& m : c) if (m.inCam && !m.transparent) ++camOpaque;
        check(covered == camOpaque && r.opaqueInstances == camOpaque, "카메라에 보이는 불투명 메시를 빠짐없이 그림");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
