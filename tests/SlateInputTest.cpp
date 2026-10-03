// HSlate 입력 경로(포인터 캡처, 숫자 칸의 프레스 추적)를 화면 없이 검산합니다.
// 렌더러의 입력 처리(handlePointerEvent)는 Vulkan 을 부르지 않으므로, 렌더러를 init 하지
// 않고 위젯 트리만 세워 이벤트를 흘려 넣습니다. 배치는 루트에 onArrangeChildren 을 직접
// 불러 정합니다.
//
// 이 테스트가 존재하는 이유:
//  - DEF-76: 캡처가 raw 포인터라, 캡처한 위젯이 리빌드로 파괴되면 다음 이벤트가 해제된
//    위젯을 불렀습니다. 실기에서는 크래시 위험 때문에 재현하지 못했습니다.
//  - DEF-77: 숫자 칸이 자기 위에서 눌린 적 없는 뗌에도 입력 모드에 들어가, 낡은 시작값으로
//    원래 값을 덮었고 undo 기록도 남지 않았습니다.
// 둘 다 이벤트 순서의 문제라 화면보다 검산이 확실합니다.
//
// 실행: build/Release/HarukaSlateTest.exe

#include "../src/ui/slate/HSlate.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

// ── 관찰용 위젯 ───────────────────────────────────────────────────────────────
// 카운터는 전역에 둡니다. 파괴된 위젯이 잘못 불리더라도 해제된 멤버를 읽지 않고 셀 수
// 있어야, 고치기 전 코드에서 이 테스트가 "불렸다"로 실패할 수 있습니다.
struct Counts {
    int down = 0, up = 0, move = 0, scroll = 0;
};
Counts g_cap;     // 누를 때 캡처를 요청하는 탐침
Counts g_plain;   // 캡처하지 않는 탐침

struct CaptorProbe : HWidget {
    glm::vec2 computeDesiredSize() override { return { 10.f, 10.f }; }
    void paint(HPaintContext&, const HGeometry&) override {}
    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.down) { ++g_cap.down; return HReply::Handled().captureMouse(this); }
        ++g_cap.up;
        return HReply::Handled();
    }
    HReply onMouseMove(const HPointerEvent&) override { ++g_cap.move;   return HReply::Unhandled(); }
    HReply onScroll   (const HPointerEvent&) override { ++g_cap.scroll; return HReply::Handled(); }
};

struct PlainProbe : HWidget {
    glm::vec2 computeDesiredSize() override { return { 10.f, 10.f }; }
    void paint(HPaintContext&, const HGeometry&) override {}
    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.down) ++g_plain.down; else ++g_plain.up;
        return HReply::Handled();
    }
    HReply onMouseMove(const HPointerEvent&) override { ++g_plain.move;   return HReply::Unhandled(); }
    HReply onScroll   (const HPointerEvent&) override { ++g_plain.scroll; return HReply::Unhandled(); }
};

// ── 배치 ──────────────────────────────────────────────────────────────────────
// 400x300 루트 안에 두 칸을 둡니다. A = [10,110) x [10,30), B = [200,300) x [10,30).
// 둘 다 아닌 빈 곳은 (250, 150) 입니다.
constexpr float kW = 400.f, kH = 300.f;
const HPadding kPadA{ 10.f, 10.f, kW - 110.f, kH - 30.f };
const HPadding kPadB{ 200.f, 10.f, kW - 300.f, kH - 30.f };
const glm::vec2 kInA { 50.f, 20.f };
const glm::vec2 kInB { 250.f, 20.f };
const glm::vec2 kEmpty{ 250.f, 150.f };

void arrange(const std::shared_ptr<HWidget>& root) {
    HGeometry g;
    g.size = { kW, kH };
    root->onArrangeChildren(g);
}

HPointerEvent press(glm::vec2 p)   { HPointerEvent e; e.position = p; e.button = 0; e.down = true;  return e; }
HPointerEvent release(glm::vec2 p) { HPointerEvent e; e.position = p; e.button = 0; e.down = false; return e; }
// 이동 이벤트는 좌버튼의 현재 상태를 싣습니다(EditorApp::handleInput 과 같은 규약).
HPointerEvent move(glm::vec2 p, bool held) { HPointerEvent e; e.position = p; e.down = held; return e; }
HPointerEvent wheel(glm::vec2 p, float d)  { HPointerEvent e; e.position = p; e.wheelDelta = d; return e; }

// 숫자 칸 하나와 그 값·콜백 횟수. 칸은 A 자리에 있고, B 자리에는 캡처하지 않는 탐침이 있습니다.
struct FieldRig {
    float value = 0.f;
    int   begins = 0, ends = 0;
    std::shared_ptr<SOverlay>     root;
    std::shared_ptr<SNumberField> field;

    explicit FieldRig(float v) : value(v) {
        field = HNew(SNumberField);
        field->get([this] { return value; })
             ->set([this](float x) { value = x; })
             ->onDragBegin([this] { ++begins; })
             ->onDragEnd([this] { ++ends; });
        root = HNew(SOverlay);
        root->addSlot(field, EHAlign::Fill, EVAlign::Fill, kPadA)
            ->addSlot(HNew(PlainProbe), EHAlign::Fill, EVAlign::Fill, kPadB);
        arrange(root);
    }
};

void resetCounts() { g_cap = {}; g_plain = {}; }

// ── DEF-77 ────────────────────────────────────────────────────────────────────
void testStrayReleaseDoesNotEdit() {
    std::printf("\n-- DEF-77: 다른 곳에서 누르고 숫자 칸 위에서 떼기 --\n");
    FieldRig rig(-0.1f);
    HSlateRenderer ui;
    ui.setRootWidget(rig.root);

    ui.handlePointerEvent(press(kEmpty));
    ui.handlePointerEvent(move(kInA, true));
    ui.handlePointerEvent(release(kInA));
    ui.handlePointerEvent(move(kInA, false));

    check(SNumberField::focusedField() == nullptr, "입력 모드에 들어가지 않음");
    check(rig.value == -0.1f, "값이 그대로 (-0.1)");
    check(rig.begins == 0 && rig.ends == 0, "시작·끝 콜백이 불리지 않음");
}

void testClickEditKeepsValueWhenUntouched() {
    std::printf("\n-- 클릭으로 입력 모드, 고치지 않고 확정 --\n");
    FieldRig rig(1.23456f);
    HSlateRenderer ui;
    ui.setRootWidget(rig.root);

    ui.handlePointerEvent(press(kInA));
    ui.handlePointerEvent(move(kInA, true));
    ui.handlePointerEvent(release(kInA));
    ui.handlePointerEvent(move(kInA, false));

    check(SNumberField::focusedField() == rig.field.get(), "입력 모드에 들어감");
    check(rig.field->editBuffer() == "1.235", "버퍼는 지금 값을 표시 정밀도로 채움");
    rig.field->commitEdit();
    check(SNumberField::focusedField() == nullptr, "확정 뒤 포커스 해제");
    check(rig.value == 1.23456f, "글자를 고치지 않으면 값이 잘리지 않음 (1.23456)");
    check(rig.begins == 1 && rig.ends == 0, "값이 그대로라 undo 기록(끝 콜백) 없음");
}

void testTypedEditAndReclick() {
    std::printf("\n-- 입력 후 같은 칸을 다시 눌러도 치던 글자 유지 --\n");
    FieldRig rig(2.f);
    HSlateRenderer ui;
    ui.setRootWidget(rig.root);

    ui.handlePointerEvent(press(kInA));
    ui.handlePointerEvent(release(kInA));
    rig.field->selectAll();
    rig.field->onChar('7');
    check(rig.field->editBuffer() == "7", "전체 선택 후 입력이 버퍼를 교체");

    ui.handlePointerEvent(press(kInA));
    ui.handlePointerEvent(release(kInA));
    check(rig.field->editBuffer() == "7", "다시 눌러도 버퍼 유지");
    check(SNumberField::focusedField() == rig.field.get(), "입력 모드 유지");

    rig.field->commitEdit();
    check(rig.value == 7.f, "확정한 값이 들어감 (7)");
    check(rig.begins == 1 && rig.ends == 1, "시작·끝 콜백이 한 번씩 (undo 기록 1건)");
}

void testRetypedSameTextCommits() {
    std::printf("\n-- 입력 중 값이 바뀐 칸에 처음과 같은 글자를 쳐서 확정 --\n");
    FieldRig rig(3.f);
    HSlateRenderer ui;
    ui.setRootWidget(rig.root);

    ui.handlePointerEvent(press(kInA));
    ui.handlePointerEvent(release(kInA));
    check(rig.field->editBuffer() == "3.000", "입력 모드 버퍼 3.000");
    rig.value = 2.5f;                       // 플레이 중 물리가 값을 바꿈
    rig.field->selectAll();
    for (char c : std::string("3.000")) rig.field->onChar((unsigned char)c);
    rig.field->commitEdit();
    check(rig.value == 3.f, "글자가 처음과 같아도 사용자가 친 값(3)이 들어감");
}

void testMissedReleaseFinishesDrag() {
    std::printf("\n-- 드래그 중 뗌을 놓치고 버튼이 떨어진 이동만 온 경우 --\n");
    resetCounts();
    FieldRig rig(0.f);
    HSlateRenderer ui;
    ui.setRootWidget(rig.root);

    ui.handlePointerEvent(press({ 20.f, 20.f }));
    ui.handlePointerEvent(move({ 60.f, 20.f }, true));
    check(rig.value > 0.1f, "스크럽으로 값이 바뀜");
    ui.handlePointerEvent(move({ 60.f, 20.f }, false));   // 뗌 이벤트 없이 버튼이 떨어짐

    check(SNumberField::focusedField() == nullptr, "입력 모드에 들어가지 않음");
    check(rig.ends == 1, "드래그 끝 콜백(undo 기록)이 한 번 불림");

    ui.handlePointerEvent(release(kInB));
    check(g_plain.up == 1, "캡처가 풀려 다음 뗌은 보통 경로로 감");
}

// ── DEF-76 ────────────────────────────────────────────────────────────────────
void testCaptorDestroyedMidPress() {
    std::printf("\n-- DEF-76: 누르는 도중 캡처한 위젯이 파괴됨 --\n");
    resetCounts();
    auto holder = HNew(SVerticalBox);
    std::weak_ptr<HWidget> captorWeak;
    {
        auto captor = std::make_shared<CaptorProbe>();
        captorWeak  = captor;
        holder->addSlot(captor, SVerticalBox::ESizeRule::Fill, 1.f);
    }
    auto root = HNew(SOverlay);
    root->addSlot(holder, EHAlign::Fill, EVAlign::Fill, kPadA)
        ->addSlot(HNew(PlainProbe), EHAlign::Fill, EVAlign::Fill, kPadB);
    arrange(root);

    HSlateRenderer ui;
    ui.setRootWidget(root);
    ui.handlePointerEvent(press(kInA));
    check(g_cap.down == 1, "누름이 캡처 탐침에 도달");

    holder->clearSlots();   // 리빌드: 캡처한 위젯이 파괴됩니다
    check(captorWeak.expired(), "캡처한 위젯이 실제로 파괴됨");

    ui.handlePointerEvent(move(kInB, true));
    ui.handlePointerEvent(wheel(kInB, 1.f));
    check(g_cap.move == 0 && g_cap.scroll == 0, "파괴된 위젯으로 이동·휠이 가지 않음");
    check(g_plain.move == 1, "이동은 보통 경로로 가 호버가 갱신됨");

    ui.handlePointerEvent(release(kInB));
    check(g_cap.up == 0, "파괴된 위젯으로 뗌이 가지 않음");
    check(g_plain.up == 0, "주인 없는 뗌을 누른 적 없는 위젯에 보내지 않음");

    ui.handlePointerEvent(press(kInB));
    ui.handlePointerEvent(release(kInB));
    check(g_plain.down == 1 && g_plain.up == 1, "그다음 누름·뗌은 정상 전달");
}

void testWheelDuringCapture() {
    std::printf("\n-- 캡처 중 휠 --\n");
    resetCounts();
    auto root = HNew(SOverlay);
    root->addSlot(std::make_shared<CaptorProbe>(), EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(root);

    HSlateRenderer ui;
    ui.setRootWidget(root);
    ui.handlePointerEvent(press(kInA));
    const int movesBefore = g_cap.move;
    ui.handlePointerEvent(wheel(kInA, 1.f));
    check(g_cap.scroll == 1, "휠이 캡처 위젯의 onScroll 로 감");
    check(g_cap.move == movesBefore, "휠이 이동 이벤트로 가지 않음");
    ui.handlePointerEvent(move(kInA, true));
    check(g_cap.move == movesBefore + 1, "휠 뒤에도 캡처가 유지됨 (버튼이 눌린 채)");
    ui.handlePointerEvent(release(kInA));
    check(g_cap.up == 1, "뗌이 캡처 위젯에 도달");
}

void testRootSwapMidPress() {
    std::printf("\n-- 누르는 도중 루트 교체 (uiScale 변경 경로) --\n");
    resetCounts();
    auto oldRoot = HNew(SOverlay);
    oldRoot->addSlot(std::make_shared<CaptorProbe>(), EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(oldRoot);

    HSlateRenderer ui;
    ui.setRootWidget(oldRoot);
    ui.handlePointerEvent(press(kInA));

    auto newRoot = HNew(SOverlay);
    newRoot->addSlot(HNew(PlainProbe), EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(newRoot);
    ui.setRootWidget(newRoot);
    oldRoot.reset();

    ui.handlePointerEvent(release(kInA));
    check(g_cap.up == 0 && g_plain.up == 0, "그 누름의 뗌은 새 트리로도 옛 위젯으로도 가지 않음");
    ui.handlePointerEvent(press(kInA));
    ui.handlePointerEvent(release(kInA));
    check(g_plain.down == 1 && g_plain.up == 1, "새 트리의 누름·뗌은 정상 전달");
}

// ── DEF-63 ────────────────────────────────────────────────────────────────────
void testCollapseHeaderPressTracking() {
    std::printf("\n-- DEF-63: 접힘 섹션 헤더의 프레스 추적 --\n");
    resetCounts();
    int  toggles  = 0;
    bool lastOpen = true;
    auto section = HNew(SCollapseSection)->title("T")->headerHeight(20.f)
        ->body(HNew(PlainProbe))
        ->onToggle([&](bool open) { ++toggles; lastOpen = open; });
    auto root = HNew(SOverlay);
    root->addSlot(section, EHAlign::Fill, EVAlign::Fill, HPadding{ 10.f, 10.f, kW - 110.f, kH - 110.f });
    arrange(root);   // 섹션 = [10,110) x [10,110), 헤더 = y [10,30)

    HSlateRenderer ui;
    ui.setRootWidget(root);
    const glm::vec2 hdr{ 50.f, 20.f };

    ui.handlePointerEvent(press(kEmpty));
    ui.handlePointerEvent(release(hdr));
    check(toggles == 0 && section->isExpanded(), "다른 곳에서 누르고 헤더에서 떼면 접히지 않음");

    ui.handlePointerEvent(press(hdr));
    check(g_plain.down == 0, "헤더의 누름이 body 로 새지 않음");
    ui.handlePointerEvent(release(hdr));
    check(toggles == 1 && !section->isExpanded() && !lastOpen, "헤더에서 누르고 떼면 접히고 콜백이 새 상태를 받음");
}

void testTabBarPressTracking() {
    std::printf("\n-- DEF-63: 탭 바의 프레스 추적 --\n");
    int active = 0, changes = 0;
    auto bar = HNew(STabBar)->addTab("A")->addTab("B")->tabWidth(50.f)->height(20.f)
        ->activeTab([&] { return active; })
        ->onTabChanged([&](int i) { active = i; ++changes; });
    auto root = HNew(SOverlay);
    root->addSlot(bar, EHAlign::Fill, EVAlign::Fill, HPadding{ 0.f, 0.f, 0.f, kH - 20.f });
    arrange(root);   // 탭 A = x [0,50), 탭 B = x [50,100), y [0,20)

    HSlateRenderer ui;
    ui.setRootWidget(root);
    const glm::vec2 tabA{ 25.f, 10.f }, tabB{ 75.f, 10.f };

    ui.handlePointerEvent(press(kEmpty));
    ui.handlePointerEvent(release(tabB));
    check(changes == 0 && active == 0, "다른 곳에서 누르고 탭에서 떼면 바뀌지 않음");

    ui.handlePointerEvent(press(tabA));
    ui.handlePointerEvent(release(tabB));
    check(changes == 0 && active == 0, "A 에서 누르고 B 에서 떼면 바뀌지 않음");

    ui.handlePointerEvent(press(tabB));
    ui.handlePointerEvent(release(tabB));
    check(changes == 1 && active == 1, "B 에서 누르고 떼면 B 로 바뀜");
}

// ── DEF-65 ────────────────────────────────────────────────────────────────────
// 첫 이동에서 캡처를 요청하는 탐침. 컨테이너가 이동 응답을 버리면 캡처가 걸리지 않습니다.
struct MoveCaptorProbe : HWidget {
    int downs = 0;
    bool asked = false;
    glm::vec2 computeDesiredSize() override { return { 10.f, 10.f }; }
    void paint(HPaintContext&, const HGeometry&) override {}
    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.down) ++downs;
        return HReply::Handled();
    }
    HReply onMouseMove(const HPointerEvent&) override {
        if (asked) return HReply::Unhandled();
        asked = true;
        return HReply::Handled().captureMouse(this);
    }
};

void testMoveReplyPropagates() {
    std::printf("\n-- DEF-65: 이동 응답이 컨테이너를 거쳐 올라옴 --\n");
    resetCounts();
    auto probe = std::make_shared<MoveCaptorProbe>();
    auto vbox  = HNew(SVerticalBox);
    vbox->addSlot(HNew(SBorder)->content(probe), SVerticalBox::ESizeRule::Fill, 1.f);
    auto root = HNew(SOverlay);
    root->addSlot(vbox, EHAlign::Fill, EVAlign::Fill, kPadA)
        ->addSlot(HNew(PlainProbe), EHAlign::Fill, EVAlign::Fill, kPadB);
    arrange(root);

    HSlateRenderer ui;
    ui.setRootWidget(root);
    ui.handlePointerEvent(move(kInA, false));   // SOverlay → SVerticalBox → SBorder → 탐침
    ui.handlePointerEvent(press(kInB));
    check(probe->downs == 1 && g_plain.down == 0, "이동에서 요청한 캡처가 걸려 누름이 탐침으로 감");
    ui.handlePointerEvent(release(kInB));
}

// ── DEF-62 ────────────────────────────────────────────────────────────────────
void testFillSlotPadding() {
    std::printf("\n-- DEF-62: 패딩 준 Fill 슬롯 --\n");
    auto fillChild  = HNew(PlainProbe);
    auto fixedChild = HNew(PlainProbe);
    auto vbox = HNew(SVerticalBox);
    vbox->addSlot(fillChild, SVerticalBox::ESizeRule::Fill, 1.f, HPadding{ 0.f, 10.f, 0.f, 10.f })
        ->addSlot(fixedChild, SVerticalBox::ESizeRule::Fixed, 20.f);
    HGeometry g;
    g.size = { 50.f, 100.f };
    vbox->onArrangeChildren(g);
    const HGeometry& fg = fillChild->cachedGeometry();
    const HGeometry& xg = fixedChild->cachedGeometry();
    check(fg.position.y == 10.f && fg.size.y == 60.f, "Fill 슬롯 = 남은 높이에서 패딩을 뺀 60");
    check(xg.position.y + xg.size.y <= 100.f, "뒤 슬롯이 컨테이너 안에 머묾");

    auto hFill  = HNew(PlainProbe);
    auto hFixed = HNew(PlainProbe);
    auto hbox = HNew(SHorizontalBox);
    hbox->addSlot(hFill, SHorizontalBox::ESizeRule::Fill, 1.f, HPadding{ 10.f, 0.f, 10.f, 0.f })
        ->addSlot(hFixed, SHorizontalBox::ESizeRule::Fixed, 20.f);
    g.size = { 100.f, 50.f };
    hbox->onArrangeChildren(g);
    const HGeometry& hx = hFixed->cachedGeometry();
    check(hFill->cachedGeometry().size.x == 60.f, "가로도 같음: Fill 슬롯 = 60");
    check(hx.position.x + hx.size.x <= 100.f, "가로 뒤 슬롯도 컨테이너 안");
}

// ── DEF-88 ────────────────────────────────────────────────────────────────────
void testNegativeZeroDisplay() {
    std::printf("\n-- DEF-88: 0 에 가까운 음수 표시 --\n");
    FieldRig rig(-0.0001f);
    HSlateRenderer ui;
    ui.setRootWidget(rig.root);

    HPaintContext ctx(nullptr);
    rig.field->paint(ctx, rig.field->cachedGeometry());
    bool sawZero = false, sawNegZero = false;
    for (const auto& el : ctx.elements) {
        if (el.type != HDrawElement::Type::Text) continue;
        if (el.text == "0.000")  sawZero    = true;
        if (el.text == "-0.000") sawNegZero = true;
    }
    check(sawZero && !sawNegZero, "-0.0001 이 0.000 으로 표시됨");

    ui.handlePointerEvent(press(kInA));
    ui.handlePointerEvent(release(kInA));
    check(rig.field->editBuffer() == "0.000", "입력 버퍼도 0.000");
    rig.field->commitEdit();
    check(rig.value == -0.0001f, "고치지 않고 확정하면 원래 값 유지");
}

} // namespace

int main() {
    std::printf("== HSlate input ==\n");
    testStrayReleaseDoesNotEdit();
    testClickEditKeepsValueWhenUntouched();
    testTypedEditAndReclick();
    testRetypedSameTextCommits();
    testMissedReleaseFinishesDrag();
    testCaptorDestroyedMidPress();
    testWheelDuringCapture();
    testRootSwapMidPress();
    testCollapseHeaderPressTracking();
    testTabBarPressTracking();
    testMoveReplyPropagates();
    testFillSlotPadding();
    testNegativeZeroDisplay();

    if (g_failed) {
        std::printf("\n%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
