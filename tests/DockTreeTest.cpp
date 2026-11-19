// 도크 트리 순수 연산 단위 시험(S6f).
// 직렬화/파싱, 알 수 없는·중복 패널 걸러내기, 5영역 재도킹, 빈 리프 정리, 닫기/다시 열기, 깨진 배치 거부를 검증합니다.
#include "editor/dock/DockTree.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>

static int gPass = 0, gFail = 0;

static void check(bool ok, const char* msg) {
    if (ok) {
        std::printf("  [PASS] %s\n", msg);
        ++gPass;
    } else {
        std::printf("  [FAIL] %s\n", msg);
        ++gFail;
    }
}

static std::unique_ptr<DockNode> makeDefaultTree() {
    std::vector<std::unique_ptr<DockNode>> topKids;
    topKids.push_back(DockNode::makeLeaf({"Outliner"}));
    topKids.push_back(DockNode::makeLeaf({"Viewport"}));
    topKids.push_back(DockNode::makeLeaf({"Details"}));
    auto topRow = DockNode::makeSplit(DockNode::Split::Horizontal, std::move(topKids), {0.20f, 0.60f, 0.20f});
    std::vector<std::unique_ptr<DockNode>> rootKids;
    rootKids.push_back(std::move(topRow));
    rootKids.push_back(DockNode::makeLeaf({"Bottom"}));
    return DockNode::makeSplit(DockNode::Split::Vertical, std::move(rootKids), {0.72f, 0.28f});
}

static bool validPanel(const std::string& id) {
    return id == "Outliner" || id == "Viewport" || id == "Details" || id == "Bottom";
}

int main() {
    std::printf("Haruka 도크 트리 단위 시험\n\n");

    // 1. 직렬화 <-> 파싱 왕복
    std::printf("== 1. 직렬화 <-> 파싱 왕복 ==\n");
    {
        auto t = makeDefaultTree();
        std::string s1 = dockSerializeLayout(t.get());
        auto t2 = dockParseLayout(s1, validPanel);
        check(t2 != nullptr, "기본 레이아웃 파싱 성공");
        std::string s2 = dockSerializeLayout(t2.get());
        check(s1 == s2, "직렬화 -> 파싱 -> 재직렬화 문자열 일치");
        check(dockPanelIsActive(t2.get(), "Viewport"), "Viewport 활성 상태 유지");
    }

    // 2. 모르는 패널 id · 중복 id 걸러내기
    std::printf("\n== 2. 모르는 패널 · 중복 패널 걸러내기 ==\n");
    {
        const std::string text =
            "# comment\n"
            "S H 3 0.3 0.4 0.3\n"
            "L 0 2 UnknownA Outliner\n"
            "L 0 2 Viewport Outliner\n"
            "L 0 1 UnknownB\n";
        auto t = dockParseLayout(text, validPanel);
        check(t != nullptr, "일부 모르는 패널이 있어도 Viewport 가 있으면 성공");
        check(!dockContains(t.get(), "UnknownA") && !dockContains(t.get(), "UnknownB"), "모르는 패널 id 제거");
        check(dockContains(t.get(), "Outliner") && dockContains(t.get(), "Viewport"), "유효 패널 유지");
        DockNode* ol = dockFindLeaf(t.get(), "Outliner");
        DockNode* vp = dockFindLeaf(t.get(), "Viewport");
        check(ol != nullptr && vp != nullptr && ol != vp, "중복 Outliner 는 첫 리프에만 남고 두 번째는 제거");
    }

    // 3. 5영역 재도킹 + 빈 리프 정리
    std::printf("\n== 3. 5영역 재도킹(Center/Left/Right/Top/Bottom) 및 빈 리프 정리 ==\n");
    {
        auto t = makeDefaultTree();
        DockNode* out = dockFindLeaf(t.get(), "Outliner");
        DockNode* vp  = dockFindLeaf(t.get(), "Viewport");
        check(dockRedock(t, out, 0, "Outliner", vp, EDockZone::Center), "Center 재도킹(탭화)");
        check(dockFindLeaf(t.get(), "Outliner") == vp, "Outliner 가 Viewport 리프에 합류");
        check(vp->panelIds.size() == 2 && vp->activeTab == 1, "합류 후 활성 탭은 Outliner");
        check(!dockPanelIsActive(t.get(), "Viewport"), "Viewport 는 비활성 탭이 됨");

        // Left / Right / Top / Bottom 분할 재도킹
        check(dockRedock(t, vp, 1, "Outliner", vp, EDockZone::Left), "Left 재도킹");
        check(dockFindLeaf(t.get(), "Outliner") != vp, "Left 재도킹 후 별도 리프로 분리");
        check(dockPanelIsActive(t.get(), "Viewport"), "Viewport 다시 활성");

        DockNode* det = dockFindLeaf(t.get(), "Details");
        DockNode* bot = dockFindLeaf(t.get(), "Bottom");
        check(dockRedock(t, det, 0, "Details", bot, EDockZone::Right),  "Right 재도킹");
        check(dockRedock(t, dockFindLeaf(t.get(), "Details"), 0, "Details", vp, EDockZone::Top), "Top 재도킹");
        check(dockRedock(t, dockFindLeaf(t.get(), "Details"), 0, "Details", vp, EDockZone::Bottom), "Bottom 재도킹");
        check(dockContains(t.get(), "Details") && dockContains(t.get(), "Viewport"), "재도킹 후 패널 유실 없음");
    }

    // 4. 닫기 -> 다시 열기가 원래 리프로 돌아가는지
    std::printf("\n== 4. 닫기 -> 다시 열기 원래 리프 복원 ==\n");
    {
        auto t = makeDefaultTree();
        DockPanelHome home;
        DockNode* vp = dockFindLeaf(t.get(), "Viewport");
        DockNode* ol = dockFindLeaf(t.get(), "Outliner");
        dockRedock(t, ol, 0, "Outliner", vp, EDockZone::Center);
        check(dockClosePanel(t, home, "Outliner", true), "Outliner 닫기 성공");
        check(!dockContains(t.get(), "Outliner"), "닫은 뒤 트리에서 제거됨");
        check(dockShowPanel(t, home, "Outliner"), "Outliner 다시 열기 성공");
        check(dockFindLeaf(t.get(), "Outliner") == dockFindLeaf(t.get(), "Viewport"),
              "다시 열면 함께 있던 Viewport 리프로 복귀");
        check(!dockClosePanel(t, home, "Viewport", false), "closable=false 패널 닫기 거부");
    }

    // 5. 깨진 배치 문자열 거부
    std::printf("\n== 5. 깨진 배치 문자열 거부 ==\n");
    {
        check(dockParseLayout("", validPanel) == nullptr, "빈 문자열 거부");
        check(dockParseLayout("X 0 1 Viewport\n", validPanel) == nullptr, "알 수 없는 태그 거부");
        check(dockParseLayout("S Z 2 0.5 0.5\nL 0 1 Viewport\nL 0 1 Details\n", validPanel) == nullptr, "잘못된 분할 방향 거부");
        check(dockParseLayout("S H 2 0.5 0.5\nL 0 1 Viewport\n", validPanel) == nullptr, "자식 부족(조기 EOF) 거부");
        check(dockParseLayout("L 0 1 Details\n", validPanel) == nullptr, "Viewport 누락 배치 거부");
    }

    std::printf("\n결과: %s (통과 %d, 실패 %d)\n", gFail == 0 ? "PASS" : "FAIL", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
