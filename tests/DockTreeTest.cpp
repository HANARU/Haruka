// 도크 트리 순수 연산 단위 시험(S6f).
// 직렬화/파싱, 알 수 없는·중복 패널 걸러내기, 5영역 재도킹, 빈 리프 정리, 닫기/다시 열기, 깨진 배치 거부를 검증합니다.
// 6 ~ 14 는 떠 있는 창(다중 OS 창 M1): 떼기 · 트리 사이 재도킹 · 합치기 · 창 닫기, F 블록 직렬화 · 옛 파서 호환.
// 15 는 탭 끌기 상태 기계의 순수 규칙(M4): 놓을 창 고르기 · 앞뒤 순서 · 할 일 · 거부 · 새 창 자리 · 빈 창 · 번호.
#include "editor/dock/DockDragRules.hpp"
#include "editor/dock/DockTree.hpp"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
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
    return id == "Outliner" || id == "Viewport" || id == "Details" || id == "Bottom"
        || id == "Log" || id == "Stats";
}

static bool floatablePanel(const std::string& id) { return id != "Viewport"; }
static bool closablePanel(const std::string& id)  { return id != "Viewport"; }

// 떠 있는 창을 넣기 전(M1 이전) 빌드의 dockParseLayout 을 그대로 옮긴 "옛 파서"입니다. 새 배치
// 파일을 옛 빌드가 읽으면 메인 트리만 나오는지(결정 D9 하위 호환) 확인하는 데 씁니다.
static std::unique_ptr<DockNode> oldParseLayout(const std::string& text,
                                                const std::function<bool(const std::string&)>& valid) {
    std::istringstream raw(text);
    std::string body, line;
    while (std::getline(raw, line)) {
        if (!line.empty() && line[0] == '#') continue;
        body += line;
        body += '\n';
    }
    std::istringstream in(body);
    bool ok = true;
    auto parsed = dockParse(in, 0, ok);
    if (!ok || !parsed) return nullptr;
    std::unordered_set<std::string> seen;
    auto pruned = dockPrune(std::move(parsed), valid, seen);
    if (!pruned || !dockContains(pruned.get(), "Viewport")) return nullptr;
    return pruned;
}

static int countPanels(const DockNode* n) {
    if (!n) return 0;
    if (n->isLeaf()) return (int)n->panelIds.size();
    int c = 0;
    for (const auto& k : n->children) c += countPanels(k.get());
    return c;
}

// 메인 = Outliner | Viewport 위, Bottom 아래. 떠 있는 창 2개(1: Log, 3: 분할 안에 Stats · Details 탭 + 빈 리프).
static const char* kFloatLayout =
    "# Haruka Dock Layout v1\n"
    "S V 2 0.72 0.28\n"
    "S H 2 0.25 0.75\n"
    "L 0 1 Outliner\n"
    "L 0 1 Viewport\n"
    "L 0 1 Bottom\n"
    "# floating windows\n"
    "F 1 120 80 640 480 0\n"
    "L 0 1 Log\n"
    "F 3 -50 900 800 600 1\n"
    "S H 2 0.5 0.5\n"
    "L 1 2 Stats Details\n"
    "L 0 0\n";

static void testFloatOps() {
    // 6. 탭 떼기 → 새 떠 있는 창
    std::printf("\n== 6. 탭 떼기 -> 새 떠 있는 창 ==\n");
    {
        auto t = makeDefaultTree();
        DockPanelHome home;
        std::vector<DockFloat> floats;
        DockNode* det = dockFindLeaf(t.get(), "Details");
        DockFloat nf;
        bool emptied = true;
        check(dockMoveToFloat(t, true, det, 0, "Details", true, home, nf, emptied), "Details 떼기 성공");
        check(!emptied, "메인 트리는 비지 않음");
        check(nf.root && nf.root->isLeaf() && nf.root->panelIds.size() == 1 && nf.root->panelIds[0] == "Details",
              "새 창 트리 = Details 탭 하나");
        check(!dockContains(t.get(), "Details"), "메인에서 Details 가 빠짐");
        DockNode* top = t->children[0].get();
        check(!top->isLeaf() && top->children.size() == 2, "빈 리프가 접혀 윗줄 분할이 둘로 줄어듦");
        float sum = 0.f;
        for (float r : top->ratios) sum += r;
        check(sum > 0.999f && sum < 1.001f, "접힌 뒤 비율 합 = 1");
        check(home.count("Details") && home["Details"].empty(), "메인에서 뗄 때 home 기록이 남음");
        nf.id = dockNextFloatId(floats);
        floats.push_back(std::move(nf));
        check(floats[0].id == 1 && dockNextFloatId(floats) == 2, "떠 있는 창 번호 1, 다음 번호 2");
        check(dockContainsAll(t.get(), floats, "Details"), "모든 트리 포함 검사가 떠 있는 창을 봄");
        DockLocation loc = dockFindLeafAll(t.get(), floats, "Details");
        check(loc && loc.floatIndex == 0 && loc.leaf == floats[0].root.get(), "모든 트리 찾기 → 창 0 의 리프");
        DockLocation vl = dockFindLeafAll(t.get(), floats, "Viewport");
        check(vl && vl.floatIndex == -1, "Viewport 는 메인(-1)에서 찾음");
        check(!dockFindLeafAll(t.get(), floats, "Nope"), "없는 패널은 빈 위치");

        DockNode* vp = dockFindLeaf(t.get(), "Viewport");
        DockFloat bad;
        check(!dockMoveToFloat(t, true, vp, 0, "Viewport", false, home, bad, emptied),
              "floatable=false(Viewport) 떼기 거부");
        check(!dockMoveToFloat(t, true, vp, 0, "Viewport", true, home, bad, emptied),
              "floatable 이 참으로 와도 메인의 Viewport 떼기 거부");
        check(dockContains(t.get(), "Viewport") && !bad.root, "거부 뒤 트리 그대로");
        DockNode* ol = dockFindLeaf(t.get(), "Outliner");
        check(!dockMoveToFloat(t, true, ol, 0, "Bottom", true, home, bad, emptied),
              "탭 번호와 패널 id 가 어긋나면 거부");

        // 떠 있는 창의 마지막 탭을 다시 떼면 원래 창이 비었다고 알림
        DockFloat nf2;
        check(dockMoveToFloat(floats[0].root, false, floats[0].root.get(), 0, "Details", true, home, nf2, emptied)
              && emptied, "떠 있는 창의 마지막 탭을 떼면 srcEmptied");
        check(dockTreeEmpty(floats[0].root.get()), "비어 버린 창 트리");
    }

    // 7. 트리 사이 재도킹
    std::printf("\n== 7. 트리 사이 재도킹(가운데 · 가장자리) ==\n");
    {
        auto t = makeDefaultTree();
        DockFloat fl;
        fl.id = 1;
        fl.root = DockNode::makeLeaf({ "Log", "Stats" });
        bool emptied = true;

        DockNode* vp = dockFindLeaf(t.get(), "Viewport");
        check(dockRedockAcross(fl.root, false, fl.root.get(), 0, "Log", t, true, vp, EDockZone::Center,
                               true, emptied), "창 → 메인 가운데 재도킹");
        check(!emptied && dockFindLeaf(t.get(), "Log") == vp && vp->activeTab == 1, "Log 가 Viewport 리프의 활성 탭");
        check(!dockContains(fl.root.get(), "Log") && fl.root->panelIds.size() == 1, "창에서는 빠짐");

        DockNode* det = dockFindLeaf(t.get(), "Details");
        check(dockRedockAcross(t, true, det, 0, "Details", fl.root, false, fl.root.get(), EDockZone::Right,
                               true, emptied), "메인 → 창 오른쪽 분할 재도킹");
        check(!fl.root->isLeaf() && fl.root->split == DockNode::Split::Horizontal
              && fl.root->children.size() == 2
              && fl.root->children[1]->panelIds.size() == 1 && fl.root->children[1]->panelIds[0] == "Details",
              "창 트리가 [Stats | Details] 가로 분할");
        check(!dockContains(t.get(), "Details") && !emptied, "메인에서 빠지고 빈 리프 접힘, 메인은 비지 않음");

        vp = dockFindLeaf(t.get(), "Viewport");
        check(!dockRedockAcross(t, true, vp, 0, "Viewport", fl.root, false, fl.root.get()->children[0].get(),
                                EDockZone::Center, false, emptied), "Viewport 를 떠 있는 창으로 옮기기 거부");
        check(!dockRedockAcross(t, true, vp, 0, "Viewport", fl.root, false, fl.root.get()->children[0].get(),
                                EDockZone::Center, true, emptied), "floatable 이 참이어도 메인의 Viewport 는 거부");
        check(!dockRedockAcross(t, true, dockFindLeaf(t.get(), "Bottom"), 0, "Bottom", fl.root, false,
                                fl.root.get(), EDockZone::Center, true, emptied), "분할 노드 가운데에는 거부");

        // 창에 남은 두 탭을 메인으로 모두 옮기면 마지막에 창이 비었다고 알림
        DockNode* st = dockFindLeaf(fl.root.get(), "Stats");
        check(dockRedockAcross(fl.root, false, st, 0, "Stats", t, true, dockFindLeaf(t.get(), "Bottom"),
                               EDockZone::Top, true, emptied) && !emptied, "Stats → 메인 위쪽, 창은 아직 남음");
        check(fl.root->isLeaf() && fl.root->panelIds.size() == 1, "창 분할이 접혀 Details 리프 하나");
        DockNode* d2 = dockFindLeaf(fl.root.get(), "Details");
        check(dockRedockAcross(fl.root, false, d2, 0, "Details", t, true, dockFindLeaf(t.get(), "Outliner"),
                               EDockZone::Bottom, true, emptied) && emptied, "마지막 탭을 옮기면 srcEmptied");
        check(countPanels(t.get()) == 6, "패널 6개가 모두 메인에 있음(유실 · 중복 없음)");

        // 다른 트리의 노드를 target 으로 잘못 주면 거부
        DockFloat other;
        other.root = DockNode::makeLeaf({ "Log2" });
        DockNode* ol = dockFindLeaf(t.get(), "Outliner");
        int olIdx = 0;
        for (int i = 0; i < (int)ol->panelIds.size(); ++i) if (ol->panelIds[i] == "Outliner") olIdx = i;
        check(!dockRedockAcross(t, true, ol, olIdx, "Outliner", fl.root, false, other.root.get(),
                                EDockZone::Center, true, emptied), "target 이 dstRoot 에 없으면 거부");
    }

    // 8. 떠 있는 창 통째로 합치기
    std::printf("\n== 8. 떠 있는 창 통째로 합치기 ==\n");
    {
        auto t = makeDefaultTree();
        DockFloat fl;
        std::vector<std::unique_ptr<DockNode>> kids;
        kids.push_back(DockNode::makeLeaf({ "Log", "Stats" }, 1));
        kids.push_back(DockNode::makeLeaf({ "Extra" }));
        fl.root = DockNode::makeSplit(DockNode::Split::Vertical, std::move(kids), { 0.5f, 0.5f });
        DockNode* det = dockFindLeaf(t.get(), "Details");
        check(dockMergeFloatInto(fl, t, true, det, EDockZone::Center, floatablePanel), "가운데 합치기 성공");
        check(!fl.root, "합친 뒤 창 트리는 빔");
        check(det->panelIds.size() == 4 && det->panelIds[1] == "Log" && det->panelIds[2] == "Stats"
              && det->panelIds[3] == "Extra", "모든 탭이 트리 순서로 붙음");
        check(det->activeTab == 2, "활성 탭 = 창 첫 리프의 활성 탭(Stats)");

        DockFloat f2;
        std::vector<std::unique_ptr<DockNode>> k2;
        k2.push_back(DockNode::makeLeaf({ "A" }));
        k2.push_back(DockNode::makeLeaf({ "B" }));
        f2.root = DockNode::makeSplit(DockNode::Split::Horizontal, std::move(k2), { 0.3f, 0.7f });
        DockNode* bot = dockFindLeaf(t.get(), "Bottom");
        check(dockMergeFloatInto(f2, t, true, bot, EDockZone::Right, floatablePanel), "가장자리(오른쪽) 합치기 성공");
        int bIdx = -1;
        DockNode* parent = dockFindParent(t.get(), bot, bIdx);
        check(parent && parent->split == DockNode::Split::Horizontal && parent->children.size() == 2
              && !parent->children[1]->isLeaf() && parent->children[1]->children.size() == 2,
              "Bottom 오른쪽에 창 트리가 분할째 붙음");
        check(dockContains(t.get(), "A") && dockContains(t.get(), "B") && !f2.root, "창 패널이 모두 메인에");

        DockFloat f3;
        f3.root = DockNode::makeLeaf({ "Q" });
        check(!dockMergeFloatInto(f3, f3.root, false, f3.root.get(), EDockZone::Center, floatablePanel),
              "자기 자신에게 합치기 거부");
        DockFloat f4;
        f4.root = DockNode::makeLeaf({ "Viewport2" });
        DockFloat bad;
        bad.root = DockNode::makeLeaf({ "Viewport" });
        check(!dockMergeFloatInto(bad, f4.root, false, f4.root.get(), EDockZone::Center, floatablePanel)
              && bad.root, "떼기 불가 패널이 든 창을 다른 떠 있는 창에 합치기 거부(트리 보존)");
        check(dockMergeFloatInto(f3, f4.root, false, f4.root.get(), EDockZone::Left, floatablePanel)
              && dockContains(f4.root.get(), "Q"), "떠 있는 창 → 떠 있는 창 합치기");
    }

    // 9. 떠 있는 창 닫기 → Home 기록 → 다시 열기
    std::printf("\n== 9. 떠 있는 창 닫기 -> Home -> 다시 열기 ==\n");
    {
        auto t = makeDefaultTree();
        DockPanelHome home;
        std::vector<DockFloat> floats;
        // Outliner 를 Details 리프에 탭으로 넣은 뒤 Outliner · Details 를 차례로 같은 창으로 뗌
        DockNode* ol = dockFindLeaf(t.get(), "Outliner");
        dockRedock(t, ol, 0, "Outliner", dockFindLeaf(t.get(), "Bottom"), EDockZone::Center);
        DockNode* bot = dockFindLeaf(t.get(), "Bottom");
        DockFloat nf;
        bool emptied = false;
        check(dockMoveToFloat(t, true, bot, 1, "Outliner", true, home, nf, emptied), "Bottom 옆의 Outliner 떼기");
        nf.id = dockNextFloatId(floats);
        floats.push_back(std::move(nf));
        DockNode* det = dockFindLeaf(t.get(), "Details");
        check(dockRedockAcross(t, true, det, 0, "Details", floats[0].root, false, floats[0].root.get(),
                               EDockZone::Center, true, emptied), "Details 를 같은 창으로");
        check(home["Outliner"].size() == 1 && home["Outliner"][0] == "Bottom", "떼기 home: Outliner → Bottom");

        std::vector<std::string> closed;
        check(!dockCloseFloat(floats[0], home, [](const std::string& id) { return id != "Details"; }, &closed)
              && floats[0].root, "닫기 불가 패널이 있으면 창 닫기 거부(트리 보존)");
        check(dockCloseFloat(floats[0], home, closablePanel, &closed), "창 닫기 성공(D4: 탭 모두 닫음)");
        check(closed.size() == 2 && closed[0] == "Outliner" && closed[1] == "Details", "닫힌 패널 목록");
        check(!floats[0].root, "닫은 창 트리는 빔");
        floats.erase(floats.begin());
        check(home["Outliner"].size() == 2 && home["Outliner"][0] == "Bottom" && home["Outliner"][1] == "Details",
              "Outliner home = [Bottom(예전 기록), Details(같은 창)]");
        check(home["Details"].size() == 1 && home["Details"][0] == "Outliner", "Details home = [Outliner]");
        check(!dockContainsAll(t.get(), floats, "Outliner") && !dockContainsAll(t.get(), floats, "Details"),
              "닫은 뒤 어느 트리에도 없음(Window 메뉴 체크 꺼짐)");

        check(dockShowPanel(t, home, "Outliner"), "Outliner 다시 열기");
        check(dockFindLeaf(t.get(), "Outliner") == dockFindLeaf(t.get(), "Bottom"), "메인의 원래 자리(Bottom 리프)로");
        check(dockShowPanel(t, home, "Details"), "Details 다시 열기");
        check(dockFindLeaf(t.get(), "Details") == dockFindLeaf(t.get(), "Outliner"),
              "같은 창에 있던 Outliner 옆으로 다시 모임");

        // 떠 있는 창 안의 탭 하나 닫기
        DockFloat fl;
        fl.root = DockNode::makeLeaf({ "Log", "Stats" });
        check(!dockClosePanelInFloat(fl, home, "Log", false, emptied), "closable=false 탭 닫기 거부");
        check(dockClosePanelInFloat(fl, home, "Log", true, emptied) && !emptied, "창 안 탭 닫기, 창은 남음");
        check(home["Log"].size() == 1 && home["Log"][0] == "Stats", "창 안 탭 닫기 home = 같은 리프 동료");
        check(dockClosePanelInFloat(fl, home, "Stats", true, emptied) && emptied, "마지막 탭 닫기 → 창 비었음");
    }
}

static void testFloatSerialize() {
    // 10. 직렬화 왕복(메인 + 창 2개)
    std::printf("\n== 10. 직렬화 왕복(메인 + 떠 있는 창 2개) ==\n");
    {
        DockLayoutData d = dockParseLayoutAll(kFloatLayout, validPanel, floatablePanel);
        check(d.main != nullptr && d.floats.size() == 2, "메인 + 창 2개 읽음");
        if (d.floats.size() == 2) {
            const DockFloat& a = d.floats[0];
            const DockFloat& b = d.floats[1];
            check(a.id == 1 && a.x == 120 && a.y == 80 && a.w == 640 && a.h == 480 && !a.maximized,
                  "창 1 번호 · 위치 · 크기 · 최대화");
            check(b.id == 3 && b.x == -50 && b.y == 900 && b.w == 800 && b.h == 600 && b.maximized,
                  "창 3 음수 위치 그대로(끼우기는 M3) · 최대화 1");
            check(b.root && b.root->isLeaf() && b.root->panelIds.size() == 2 && b.root->activeTab == 1,
                  "빈 리프가 걸러져 창 3 트리가 접히고 활성 탭 유지");
        }
        std::string s1 = dockSerializeLayout(d.main.get(), d.floats);
        DockLayoutData d2 = dockParseLayoutAll(s1, validPanel, floatablePanel);
        std::string s2 = dockSerializeLayout(d2.main.get(), d2.floats);
        check(s1 == s2, "직렬화 -> 파싱 -> 재직렬화 문자열 일치");
        check(s1.rfind("# Haruka Dock Layout v1\n", 0) == 0, "머리말은 v1 그대로");
        check(s1.find("# floating windows\nF 1 120 80 640 480 0\nL 0 1 Log\nF 3 -50 900 800 600 1\nL 1 2 Stats Details\n")
              != std::string::npos, "F 블록 형식(주석 줄 + F 줄 + 트리)");

        std::vector<DockFloat> none;
        check(dockSerializeLayout(d.main.get(), none) == dockSerializeLayout(d.main.get()),
              "떠 있는 창이 없으면 v1 직렬화와 같은 글");
        std::vector<DockFloat> emptyOnes;
        emptyOnes.push_back(DockFloat{});
        emptyOnes.back().root = DockNode::makeLeaf({});
        check(dockSerializeLayout(d.main.get(), emptyOnes) == dockSerializeLayout(d.main.get()),
              "빈 창은 쓰지 않음");
    }

    // 11. F 블록 없는 v1 파일을 새 파서로
    std::printf("\n== 11. v1 파일(F 블록 없음)을 새 파서로 ==\n");
    {
        auto t = makeDefaultTree();
        const std::string v1 = dockSerializeLayout(t.get());
        DockLayoutData d = dockParseLayoutAll(v1, validPanel, floatablePanel);
        check(d.main && d.floats.empty(), "메인만, 떠 있는 창 없음");
        auto old = dockParseLayout(v1, validPanel);
        check(old && dockSerializeLayout(old.get()) == dockSerializeLayout(d.main.get()),
              "새 파서의 메인 = 기존 dockParseLayout 결과");
        check(dockSerializeLayout(d.main.get(), d.floats) == v1, "다시 쓰면 바이트까지 같음");
    }

    // 12. 새 파일을 옛 파서로
    std::printf("\n== 12. 새 파일을 옛 파서로(메인 트리만) ==\n");
    {
        auto old = oldParseLayout(kFloatLayout, validPanel);
        check(old != nullptr, "옛 파서가 새 파일을 거부하지 않음");
        DockLayoutData d = dockParseLayoutAll(kFloatLayout, validPanel, floatablePanel);
        check(old && d.main && dockSerializeLayout(old.get()) == dockSerializeLayout(d.main.get()),
              "옛 파서 결과 = 새 파서의 메인 트리");
        check(old && !dockContains(old.get(), "Log") && !dockContains(old.get(), "Stats"),
              "떠 있던 패널은 옛 빌드에서 닫힘으로 보임");
        auto viaApi = dockParseLayout(kFloatLayout, validPanel);
        check(viaApi && dockSerializeLayout(viaApi.get()) == dockSerializeLayout(old.get()),
              "dockParseLayout(메인 전용 API)도 같은 결과");
    }

    // 13. 중복 패널 제거(메인 우선) · 창 안 Viewport 거르기
    std::printf("\n== 13. 중복 패널 제거(메인 우선) · 창 안 Viewport 거르기 ==\n");
    {
        const std::string text =
            "# Haruka Dock Layout v1\n"
            "S H 2 0.5 0.5\n"
            "L 0 1 Viewport\n"
            "L 0 2 Details Log\n"
            "F 1 0 0 400 300 0\n"
            "L 0 3 Log Stats Viewport\n"
            "F 2 0 0 400 300 0\n"
            "L 0 2 Stats Details\n"
            "F 4 0 0 400 300 0\n"
            "L 0 1 Viewport\n";
        DockLayoutData d = dockParseLayoutAll(text, validPanel, floatablePanel);
        check(d.main && dockContains(d.main.get(), "Log") && dockContains(d.main.get(), "Details"),
              "메인의 패널은 그대로");
        check(d.floats.size() == 1 && d.floats[0].id == 1, "창 2(모두 중복) · 창 4(Viewport 만) 버림");
        if (!d.floats.empty())
            check(d.floats[0].root->panelIds.size() == 1 && d.floats[0].root->panelIds[0] == "Stats",
                  "창 1 = Stats 만(Log 는 메인에, Viewport 는 거름)");
        int vpCount = 0;
        if (d.main) vpCount += dockContains(d.main.get(), "Viewport") ? 1 : 0;
        for (const auto& f : d.floats) vpCount += dockContains(f.root.get(), "Viewport") ? 1 : 0;
        check(vpCount == 1, "Viewport 는 메인에만 하나");

        // 메인이 먼저 읽혀 Viewport 는 seen 으로도 걸러지므로, floatable 거름 자체는 다른 패널로 확인합니다.
        DockLayoutData nf = dockParseLayoutAll(text, validPanel,
                                               [](const std::string& id) { return id != "Viewport" && id != "Stats"; });
        check(nf.main && nf.floats.empty(), "floatable=false 패널(Stats)은 창 안에서 걸러지고 빈 창은 버림");

        const std::string noMainVp =
            "L 0 1 Details\n"
            "F 1 0 0 400 300 0\n"
            "L 0 1 Viewport\n";
        DockLayoutData r = dockParseLayoutAll(noMainVp, validPanel, floatablePanel);
        check(!r.main && r.floats.empty(), "메인에 Viewport 가 없으면 창에 있어도 전체 거부");
    }

    // 14. 깨진 F 블록만 버림 · 크기 · 번호 고치기
    std::printf("\n== 14. 깨진 F 블록만 버림 · 크기 · 번호 고치기 ==\n");
    {
        const std::string text =
            "# Haruka Dock Layout v1\n"
            "L 0 1 Viewport\n"
            "F 1 10 10 300 200\n"            // 머리 줄 값 부족
            "L 0 1 Log\n"
            "F 2 10 10 300 200 0\n"          // 트리가 깨짐(자식 부족)
            "S H 2 0.5 0.5\n"
            "L 0 1 Stats\n"
            "F 3 10 10 300 200 7\n"          // maximized 가 0/1 아님
            "L 0 1 Outliner\n"
            "F 5 10 20 0 -4 0\n"             // 크기 0 이하 → 최소 크기
            "L 0 1 Stats\n"
            "F 5 1 2 99999 300 0\n"          // 번호 겹침 → 새 번호, 너무 큰 크기 → 최대
            "L 0 1 Details\n"
            "F x 1 2 3 4 0\n"                // 번호가 숫자 아님
            "L 0 1 Bottom\n"
            "F 0 1 2 300 300 0\n"            // 번호 0 → 새 번호
            "L 0 1 Log\n";
        DockLayoutData d = dockParseLayoutAll(text, validPanel, floatablePanel);
        check(d.main && d.main->isLeaf() && d.main->panelIds[0] == "Viewport", "메인 트리는 살아남음");
        check(d.floats.size() == 3, "깨진 블록 4개만 버리고 3개 남음");
        if (d.floats.size() == 3) {
            check(d.floats[0].id == 5 && d.floats[0].w == kDockFloatMinW && d.floats[0].h == kDockFloatMinH
                  && d.floats[0].root->panelIds[0] == "Stats", "크기 0 이하 → 최소 크기, 깨진 블록의 Stats 는 seen 에 안 들어감");
            check(d.floats[1].id == 6 && d.floats[1].w == kDockFloatMaxDim && d.floats[1].root->panelIds[0] == "Details",
                  "겹친 번호 → 새 번호 6, 큰 크기 → 최대");
            check(d.floats[2].id == 7 && d.floats[2].root->panelIds[0] == "Log", "번호 0 → 새 번호 7, 깨진 블록의 Log 를 살림");
        }
        check(d.main && !dockContains(d.main.get(), "Outliner") && !dockContainsAll(d.main.get(), d.floats, "Bottom"),
              "버린 블록의 패널은 어디에도 없음(Window 메뉴로 다시 열 수 있음)");

        const std::string brokenMain =
            "S H 2 0.5 0.5\n"
            "L 0 1 Viewport\n"
            "F 1 0 0 400 300 0\n"
            "L 0 1 Log\n";
        check(!dockParseLayoutAll(brokenMain, validPanel, floatablePanel).main,
              "메인 트리가 F 줄에서 끊기면 거부(옛 파서와 같음)");
        check(!oldParseLayout(brokenMain, validPanel), "옛 파서도 같은 파일을 거부");
    }
}

// 15. 탭 끌기 상태 기계의 순수 규칙(다중 OS 창 M4): 놓을 창 고르기 · 앞뒤 순서 · 영역 · 할 일 · 거부 · 새 창 자리 · 빈 창 · 번호.
static void testDragRules() {
    using namespace hdockdrag;
    std::printf("\n== 15. 탭 끌기 규칙(M4) ==\n");

    // 놓을 창: 커서 아래 창들 중 가장 앞(front 가 큰 것), 어느 창 위도 아니면 -1.
    std::vector<DropWindow> wins = {
        { 0, { 0, 0, 1600, 900 }, 1, true },     // 메인
        { 3, { 100, 100, 600, 700 }, 2, true },  // 메인 위에 겹친 떠 있는 창(더 앞)
        { 5, { 1700, 100, 400, 300 }, 3, true }, // 메인 오른쪽 떠 있는 창
    };
    check(pickDropWindow(50.f, 50.f, wins) == 0, "놓을 창: 메인만 있는 자리 → 메인");
    check(pickDropWindow(300.f, 300.f, wins) == 3, "놓을 창: 겹친 자리 → 더 앞의 떠 있는 창");
    wins[0].front = 9;
    check(pickDropWindow(300.f, 300.f, wins) == 0, "놓을 창: 메인이 더 앞이면 메인(메인을 누르면 떠 있는 창 위로 올라옴)");
    check(pickDropWindow(1800.f, 200.f, wins) == 5, "놓을 창: 메인 밖 떠 있는 창");
    check(pickDropWindow(1650.f, 50.f, wins) == -1 && pickDropWindow(-1.f, 10.f, wins) == -1, "놓을 창: 어느 창 위도 아님 → -1");
    check(pickDropWindow(1600.f, 899.f, wins) == -1 && pickDropWindow(1599.f, 899.f, wins) == 0, "놓을 창: 오른쪽 · 아래 끝은 열린 구간");
    wins[2].usable = false;
    check(pickDropWindow(1800.f, 200.f, wins) == -1, "놓을 창: 최소화된(쓸 수 없는) 창은 후보가 아님");
    {
        std::vector<DropWindow> tie = { { 1, { 0, 0, 10, 10 }, 4, true }, { 2, { 0, 0, 10, 10 }, 4, true } };
        check(pickDropWindow(5.f, 5.f, tie) == 1, "놓을 창: 앞뒤가 같으면 목록 순서로 처음 것");
        check(pickDropWindow(5.f, 5.f, {}) == -1, "놓을 창: 후보 없음 → -1");
        check(findWindow(tie, 2) == &tie[1] && findWindow(tie, 7) == nullptr, "창 찾기");
    }
    check(!ScreenRect{ 0, 0, 0, 10 }.contains(0.f, 0.f), "크기 0 사각형은 아무것도 담지 않음");

    // 앞뒤 순서(Z 순서 대안): 처음 본 창 · 포커스가 새로 온 창 · bump 는 맨 앞.
    {
        FrontOrder fo;
        fo.note(0, true);
        fo.note(3, false);
        fo.note(5, false);
        check(fo.front(5) > fo.front(3) && fo.front(3) > fo.front(0), "처음 보는 창은 맨 앞(만든 순서)");
        fo.note(0, true);
        check(fo.front(5) > fo.front(0), "포커스가 계속 있으면 그대로(엣지만 셈)");
        fo.note(0, false);
        fo.note(3, true);
        check(fo.front(3) > fo.front(5) && fo.front(3) > fo.front(0), "포커스가 새로 온 창은 맨 앞");
        fo.note(3, true);
        fo.note(0, true);
        check(fo.front(0) > fo.front(3), "메인을 누르면(포커스) 메인이 맨 앞");
        fo.bump(5);
        check(fo.front(5) > fo.front(0), "bump(에디터가 앞으로 가져옴)는 맨 앞");
        fo.bump(8);
        check(fo.front(8) > fo.front(5), "모르는 창 bump 는 새 창으로 맨 앞");
        fo.retain({ 0, 5 });
        check(fo.front(3) == 0 && fo.front(8) == 0 && fo.front(5) > 0 && fo.front(0) > 0, "retain: 없어진 창은 잊음(0)");
        const uint64_t f0 = fo.front(0);
        fo.note(3, false);
        check(fo.front(3) > f0 && fo.front(3) > fo.front(5), "같은 번호가 다시 생기면 새 창으로 맨 앞");
    }

    // 영역: 가장자리 28% = 분할(미리보기 반쪽), 가운데 = 탭(미리보기 전체).
    {
        const ZoneRect leaf{ 100.f, 50.f, 400.f, 200.f };
        ZoneRect hi;
        check(zoneInLeaf(110.f, 150.f, leaf, hi) == EDockZone::Left && hi.x == 100.f && hi.w == 200.f && hi.h == 200.f, "왼쪽 가장자리 → Left, 왼쪽 반");
        check(zoneInLeaf(490.f, 150.f, leaf, hi) == EDockZone::Right && hi.x == 300.f && hi.w == 200.f, "오른쪽 가장자리 → Right, 오른쪽 반");
        check(zoneInLeaf(300.f, 55.f, leaf, hi) == EDockZone::Top && hi.y == 50.f && hi.h == 100.f, "위 가장자리 → Top, 위 반");
        check(zoneInLeaf(300.f, 245.f, leaf, hi) == EDockZone::Bottom && hi.y == 150.f && hi.h == 100.f, "아래 가장자리 → Bottom, 아래 반");
        check(zoneInLeaf(300.f, 150.f, leaf, hi) == EDockZone::Center && hi.x == 100.f && hi.w == 400.f && hi.h == 200.f, "가운데 → Center, 리프 전체");
        check(zoneInLeaf(110.f, 55.f, leaf, hi) == EDockZone::Left, "모서리는 가로 판정이 먼저(예전 코드와 같음)");
    }

    // 할 일(상태 기계의 놓기 전이) · 거부 규칙.
    {
        DropQuery q;
        q.srcWin = 0;
        check(decideDrop(q) == DropAction::Activate, "끌지 않고 뗌 → 탭 누르기");
        q.dragActive = true;
        q.overWin = 0; q.overLeaf = true;
        check(decideDrop(q) == DropAction::RedockSame, "같은 창 리프 → 같은 창 재도킹(원래 창도 후보)");
        q.overLeaf = false;
        check(decideDrop(q) == DropAction::None, "같은 창의 리프 아닌 곳 → 아무 일 없음");
        q.overWin = 3; q.overLeaf = true;
        check(decideDrop(q) == DropAction::RedockAcross, "메인 → 떠 있는 창 리프 → 창 사이 재도킹");
        q.overLeaf = false;
        check(decideDrop(q) == DropAction::None, "다른 창의 리프 아닌 곳(메뉴 줄 등) → 아무 일 없음(새 창 아님)");
        q.overWin = -1;
        check(decideDrop(q) == DropAction::NewFloat, "어느 창 위도 아님 → 새 떠 있는 창");
        q.floatable = false;
        check(decideDrop(q) == DropAction::Rejected, "D8: 떼기 금지 패널(Viewport)을 창 밖에 → 거부");
        q.overWin = 3; q.overLeaf = true;
        check(decideDrop(q) == DropAction::Rejected, "D8: 떼기 금지 패널을 떠 있는 창에 → 거부");
        q.overWin = 0; q.srcWin = 0;
        check(decideDrop(q) == DropAction::RedockSame, "떼기 금지 패널도 메인 안에서는 재도킹");
        q.srcWin = 3; q.floatable = true; q.overWin = 0; q.overLeaf = true;
        check(decideDrop(q) == DropAction::RedockAcross, "떠 있는 창 → 메인 리프");
        q.overWin = 5;
        check(decideDrop(q) == DropAction::RedockAcross, "떠 있는 창 → 다른 떠 있는 창 리프");
        // 창 통째 끌기.
        DropQuery m;
        m.mergeDrag = true; m.srcWin = 3;
        check(decideDrop(m) == DropAction::None, "통째 끌기: 끌지 않고 뗌 → 아무 일 없음(탭 누르기 아님)");
        m.dragActive = true; m.overWin = 0; m.overLeaf = true;
        check(decideDrop(m) == DropAction::MergeFloat, "통째 끌기: 메인 리프 → 합치기");
        m.overWin = 3;
        check(decideDrop(m) == DropAction::None, "통째 끌기: 자기 창 → 아무 일 없음");
        m.overWin = -1; m.overLeaf = false;
        check(decideDrop(m) == DropAction::None, "통째 끌기: 창 밖 → 아무 일 없음(창 옮기기는 OS 제목 표시줄)");
        m.overWin = 5; m.overLeaf = true; m.floatable = false;
        check(decideDrop(m) == DropAction::Rejected, "통째 끌기: 떼기 금지 패널이 든 창을 떠 있는 창에 → 거부");
        m.overWin = 0;
        check(decideDrop(m) == DropAction::MergeFloat, "통째 끌기: 메인에는 합침");
        check(dragCursor(DropAction::NewFloat) == 3 && dragCursor(DropAction::Rejected) == 4 &&
              dragCursor(DropAction::RedockAcross) == 0 && dragCursor(DropAction::None) == 0, "커서 모양: 새 창 = 이동, 거부 = 금지, 나머지 = 화살표");
    }

    // 새 창 자리 · 크기: 잡은 자리가 커서 아래, 크기는 원래 리프 기준(최소 · 최대), 모르면 기본값.
    {
        ScreenRect r = newFloatRect(1000.f, 500.f, 30.f, 12.f, 600.f, 400.f);
        check(r.x == 970 && r.y == 488 && r.w == 600 && r.h == 400, "새 창: 잡은 자리 = 커서, 크기 = 원래 리프");
        r = newFloatRect(1000.f, 500.f, 30.f, 12.f, 1600.f, 100.f);
        check(r.w == kNewFloatMaxW && r.h == kNewFloatMinH, "새 창: 너무 넓은 · 낮은 리프는 최대 · 최소로");
        r = newFloatRect(10.f, 20.f, 5.f, 5.f, 0.f, 0.f);
        check(r.w == kNewFloatDefaultW && r.h == kNewFloatDefaultH && r.x == 5 && r.y == 15, "새 창: 리프 크기를 모르면 기본값");
        r = newFloatRect(100.f, 100.f, 5000.f, -3.f, 400.f, 300.f);
        check(r.x == 100 - 399 && r.y == 100, "새 창: 잡은 자리는 창 안으로 고침");
    }

    // 빈 창 · 번호.
    {
        std::vector<DockFloat> fl(3);
        fl[0].id = 2; fl[0].root = DockNode::makeLeaf({ "Outliner" });
        fl[1].id = 4; fl[1].root = DockNode::makeLeaf({});
        fl[2].id = 7; fl[2].root = nullptr;
        const std::vector<int> e = emptyFloats(fl);
        check(e.size() == 2 && e[0] == 4 && e[1] == 7, "빈 창: 탭 없는 리프 · 루트 없음");
        int last = 0;
        check(issueFloatId(fl, last) == 8 && last == 8, "번호: 지금 쓰는 번호 다음");
        fl.pop_back();
        check(issueFloatId(fl, last) == 9, "번호: 지운 창(7) · 준 번호(8)를 되쓰지 않음");
        last = 0;
        fl.clear();
        check(issueFloatId(fl, last) == 1, "번호: 창이 없으면 1");
    }
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

    testFloatOps();
    testFloatSerialize();
    testDragRules();

    std::printf("\n결과: %s (통과 %d, 실패 %d)\n", gFail == 0 ? "PASS" : "FAIL", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
