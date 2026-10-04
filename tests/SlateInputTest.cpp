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
//  - DEF-56·57(기반 공사): 클립이 없어 넘친 내용이 이웃 패널에 그려졌고, 컨테이너가 휠을
//    전달하지 않아 위젯이 휠을 받지 못했습니다. 스크롤 상자가 둘을 함께 씁니다.
//  - DEF-60: 글자 폭을 "글자 수 x 크기 x 0.6" 으로 잡아 그려지는 폭과 달랐습니다.
//
// 실행: build/Release/HarukaSlateTest.exe

#include "../src/ui/slate/HSlate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

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
// 좌버튼을 누른 채 수정 키와 함께 움직이는 이벤트(DEF-46).
HPointerEvent moveMod(glm::vec2 p, bool shift, bool ctrl) {
    HPointerEvent e = move(p, true); e.shift = shift; e.ctrl = ctrl; return e;
}
bool nearF(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }

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
    check(rig.field->editBuffer() == "1.23456", "버퍼는 지금 값을 넉넉한 자릿수로 채움(1.23456)");
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
    check(rig.field->editBuffer() == "3.0", "입력 모드 버퍼 3.0 (끝의 0 정리)");
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
    auto paintedTexts = [](SNumberField& f) {
        HPaintContext ctx(nullptr);
        f.paint(ctx, f.cachedGeometry());
        std::vector<std::string> out;
        for (const auto& el : ctx.elements)
            if (el.type == HDrawElement::Type::Text) out.push_back(el.text);
        return out;
    };
    auto has = [](const std::vector<std::string>& v, const char* s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    {
        FieldRig rig(-0.00001f);   // 일반 칸(소수 넷째 자리)에서 반올림하면 0
        HSlateRenderer ui;
        ui.setRootWidget(rig.root);
        auto texts = paintedTexts(*rig.field);
        check(has(texts, "0.0") && !has(texts, "-0.0"), "-0.00001 이 0.0 으로 표시됨");
        ui.handlePointerEvent(press(kInA));
        ui.handlePointerEvent(release(kInA));
        check(rig.field->editBuffer() == "-0.00001", "입력 모드에서는 실제 값이 보임");
        rig.field->commitEdit();
        check(rig.value == -0.00001f, "고치지 않고 확정하면 원래 값 유지");
    }
    {
        FieldRig rig(-0.0001f);    // 회전 칸: 오일러 변환에서 생기는 잡음
        rig.field->unit(ENumberUnit::Angle);
        HSlateRenderer ui;
        ui.setRootWidget(rig.root);
        auto texts = paintedTexts(*rig.field);
        check(has(texts, "0.0") && !has(texts, "-0.0"), "회전 칸의 -0.0001 은 0.0");
        ui.handlePointerEvent(press(kInA));
        ui.handlePointerEvent(release(kInA));
        check(rig.field->editBuffer() == "0.0", "회전 칸 입력 버퍼도 잡음 없이 0.0");
        rig.field->commitEdit();
    }
}

// ── DEF-46: 숫자 칸 정밀도 ────────────────────────────────────────────────────
void testDragModifiers() {
    std::printf("\n-- DEF-46: 끌기 배율(UE 식)과 이동마다 누적 --\n");
    FieldRig rig(0.f);
    rig.field->unit(ENumberUnit::Offset);   // 1cm/px
    HSlateRenderer ui;
    ui.setRootWidget(rig.root);

    const glm::vec2 p0{ 20.f, 20.f };
    ui.handlePointerEvent(press(p0));
    ui.handlePointerEvent(move({ p0.x + 3.f, p0.y }, true));
    check(rig.value == 0.f, "데드존(3px) 안에서는 값이 그대로");
    ui.handlePointerEvent(move({ p0.x + 13.f, p0.y }, true));
    check(nearF(rig.value, 0.10f), "데드존을 지난 10px → 0.10 (데드존 거리는 더하지 않음)");
    ui.handlePointerEvent(moveMod({ p0.x + 13.f, p0.y }, true, false));
    check(nearF(rig.value, 0.10f), "제자리에서 Shift 를 눌러도 값이 튀지 않음");
    ui.handlePointerEvent(moveMod({ p0.x + 23.f, p0.y }, true, false));
    check(nearF(rig.value, 1.10f), "Shift 로 10px → +1.0 (10배)");
    ui.handlePointerEvent(moveMod({ p0.x + 33.f, p0.y }, false, true));
    check(nearF(rig.value, 1.11f), "Ctrl 로 10px → +0.01 (0.1배)");
    ui.handlePointerEvent(release({ p0.x + 33.f, p0.y }));
    check(rig.ends == 1, "끝 콜백(undo 기록) 한 번");
}

void testSizeStepFollowsValue() {
    std::printf("\n-- DEF-46: Size 칸은 시작값에 비례한 감도 --\n");
    {
        FieldRig rig(10.f);
        rig.field->unit(ENumberUnit::Size)->step(0.01f);
        HSlateRenderer ui;
        ui.setRootWidget(rig.root);
        ui.handlePointerEvent(press({ 20.f, 20.f }));
        ui.handlePointerEvent(move({ 33.f, 20.f }, true));   // 데드존 뒤 10px
        check(nearF(rig.value, 11.f), "10 에서 시작: 10px → +1.0 (값의 1%/px)");
        ui.handlePointerEvent(release({ 33.f, 20.f }));
    }
    {
        FieldRig rig(0.f);
        rig.field->unit(ENumberUnit::Size)->step(0.01f);
        HSlateRenderer ui;
        ui.setRootWidget(rig.root);
        ui.handlePointerEvent(press({ 20.f, 20.f }));
        ui.handlePointerEvent(move({ 33.f, 20.f }, true));
        check(nearF(rig.value, 0.01f, 1e-6f), "0 에서 시작해도 멈추지 않음: 10px → +0.01 (step 의 1/10 /px)");
        ui.handlePointerEvent(release({ 33.f, 20.f }));
    }
}

void testSmallValueDisplay() {
    std::printf("\n-- DEF-46: 1mm 미만 값 표시와 끝의 0 정리 --\n");
    check(SNumberField::formatTrimmed(0.0004f, 5)  == "0.0004",  "0.4mm 가 0.0004 로 보임(예전 0.000)");
    check(SNumberField::formatTrimmed(0.00002f, 5) == "0.00002", "0.02mm 도 보임");
    check(SNumberField::formatTrimmed(1.f, 5)      == "1.0",     "끝의 0 은 지우고 한 자리는 남김");
    check(SNumberField::formatTrimmed(12.3456f, 2) == "12.35",   "각도는 둘째 자리");
    check(SNumberField::formatTrimmed(-0.004f, 2)  == "0.0",     "반올림하면 0 인 음수는 0.0(-0.0 아님)");
    check(SNumberField::formatTrimmed(1234567.f, 5) == "1234567.0", "큰 값은 유효 숫자 7자리까지");
}

void testUndoRecordsTinyEdit() {
    std::printf("\n-- DEF-46: 아주 작은 값의 편집도 undo 기록 --\n");
    FieldRig rig(0.f);
    rig.field->unit(ENumberUnit::Offset);
    HSlateRenderer ui;
    ui.setRootWidget(rig.root);
    ui.handlePointerEvent(press(kInA));
    ui.handlePointerEvent(release(kInA));
    rig.field->selectAll();
    for (char c : std::string("0.0000005")) rig.field->onChar((unsigned char)c);
    rig.field->commitEdit();
    check(nearF(rig.value, 0.0000005f, 1e-10f), "0.0000005 가 들어감");
    check(rig.ends == 1, "undo 기록(끝 콜백)이 남음 (예전 절대 기준 1e-6 에서는 빠짐)");
}

// ── DEF-57: 휠 경로 ───────────────────────────────────────────────────────────
// 예전에는 어떤 컨테이너도 onScroll 을 전달하지 않아, 위젯이 휠을 받을 수 없었습니다.
void testWheelRouting() {
    std::printf("\n-- DEF-57: 휠이 컨테이너를 거쳐 커서 아래 위젯에 닿음 --\n");
    resetCounts();
    int   hookCalls = 0;
    float hookDelta = 0.f;
    auto root = HNew(SOverlay);
    root->addSlot(HNew(SVerticalBox)->addSlot(HNew(SBorder)->content(std::make_shared<CaptorProbe>()),
                                              SVerticalBox::ESizeRule::Fill, 1.f),
                  EHAlign::Fill, EVAlign::Fill, kPadA)
        ->addSlot(HNew(SBorder)->content(std::make_shared<PlainProbe>())
                      ->onWheel([&](float d) { ++hookCalls; hookDelta = d; return true; }),
                  EHAlign::Fill, EVAlign::Fill, kPadB);
    arrange(root);
    HSlateRenderer ui;
    ui.setRootWidget(root);

    HReply r = ui.handlePointerEvent(wheel(kInA, 1.f));
    check(r.handled && g_cap.scroll == 1, "A 자리 휠이 오버레이·상자·테두리를 거쳐 안쪽 위젯에 닿음");
    r = ui.handlePointerEvent(wheel(kInB, -2.f));
    check(r.handled && g_plain.scroll == 1 && hookCalls == 1 && hookDelta == -2.f,
          "자식이 받지 않은 휠은 테두리의 휠 처리기가 받음 (자식이 먼저)");
    r = ui.handlePointerEvent(wheel(kEmpty, 1.f));
    check(!r.handled && g_cap.scroll == 1 && hookCalls == 1,
          "빈 곳의 휠은 아무도 받지 않음 (에디터가 카메라 돌리로 넘김)");

    // 같은 자리에 겹친 슬롯은 위(뒤에 추가한 슬롯)부터 받습니다. 열린 메뉴의 캐처가 아래 패널보다
    // 먼저 받아야 메뉴가 열린 동안 패널이 굴러가지 않습니다.
    int below = 0, above = 0;
    auto stack = HNew(SOverlay);
    stack->addSlot(HNew(SBorder)->onWheel([&](float) { ++below; return true; }), EHAlign::Fill, EVAlign::Fill, kPadA)
         ->addSlot(HNew(SBorder)->onWheel([&](float) { ++above; return true; }), EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(stack);
    HSlateRenderer ui2;
    ui2.setRootWidget(stack);
    ui2.handlePointerEvent(wheel(kInA, 1.f));
    check(above == 1 && below == 0, "겹친 슬롯은 위 슬롯이 먼저 받고 거기서 멈춤");
}

// ── DEF-56 · DEF-57: 스크롤 상자 ──────────────────────────────────────────────
// 높이 30 짜리 행. 누름 횟수와 마지막 이동이 자기 위였는지(호버)를 기록합니다.
struct RowProbe : HWidget {
    int  downs   = 0;
    bool hovered = false;
    glm::vec2 computeDesiredSize() override { return { 10.f, 30.f }; }
    void paint(HPaintContext& ctx, const HGeometry& geo) override { ctx.drawRect(geo, { 1.f, 1.f, 1.f, 1.f }); }
    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.down && cachedGeo_.contains(e.position)) ++downs;
        return HReply::Handled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        hovered = cachedGeo_.contains(e.position);
        return HReply::Unhandled();
    }
};

// 루트 [머리(높이 50) / 스크롤 상자(높이 100)] 안에 행 10개(내용 300). 휠 한 칸 = 30px.
struct ScrollRig {
    std::shared_ptr<SVerticalBox>          root, list;
    std::shared_ptr<SScrollBox>            scroll;
    std::shared_ptr<PlainProbe>            header;
    std::vector<std::shared_ptr<RowProbe>> rows;
    explicit ScrollRig(int rowCount) {
        list = HNew(SVerticalBox);
        for (int i = 0; i < rowCount; ++i) {
            rows.push_back(std::make_shared<RowProbe>());
            list->addSlot(rows.back(), SVerticalBox::ESizeRule::Fixed, 30.f);
        }
        scroll = HNew(SScrollBox)->wheelStep(30.f)->content(list);
        header = std::make_shared<PlainProbe>();
        root   = HNew(SVerticalBox);
        root->addSlot(header, SVerticalBox::ESizeRule::Fixed, 50.f)
            ->addSlot(scroll, SVerticalBox::ESizeRule::Fixed, 100.f)
            ->addSlot(HNew(SBorder), SVerticalBox::ESizeRule::Fill, 1.f);
        arrange(root);
    }
};
const glm::vec2 kInScroll{ 60.f, 100.f };   // 스크롤 상자 영역 = [0,400) x [50,150)

void testScrollBox() {
    std::printf("\n-- DEF-56 · DEF-57: 스크롤 상자 --\n");
    resetCounts();
    ScrollRig rig(10);
    HSlateRenderer ui;
    ui.setRootWidget(rig.root);
    check(rig.scroll->maxScrollOffset() == 200.f, "넘치는 양 = 내용 300 - 영역 100");

    HReply r = ui.handlePointerEvent(wheel(kInScroll, -1.f));   // 아래로 한 칸
    arrange(rig.root);
    check(r.handled && rig.scroll->scrollOffset() == 30.f, "휠 아래로 한 칸 = 30px 스크롤");
    check(rig.rows[1]->cachedGeometry().position.y == 50.f, "둘째 행이 영역 맨 위로 올라옴");
    check(rig.rows[1]->cachedGeometry().size.x == 392.f, "넘칠 때는 막대 자리(4 + 4px)를 비우고 배치함");

    ui.handlePointerEvent(press({ 60.f, 55.f }));
    ui.handlePointerEvent(release({ 60.f, 55.f }));
    check(rig.rows[1]->downs == 1 && rig.rows[0]->downs == 0, "스크롤한 뒤의 누름이 그 자리에 보이는 행에 감");

    // 위로 밀려 잘린 첫 행은 머리 자리(y 20~50)에 있습니다. 그 자리의 이동·누름을 받으면 안 됩니다.
    ui.handlePointerEvent(move({ 60.f, 40.f }, false));
    check(rig.rows[0]->cachedGeometry().contains({ 60.f, 40.f }) && !rig.rows[0]->hovered,
          "잘려 안 보이는 행은 그 자리의 이동에 호버를 켜지 않음");
    ui.handlePointerEvent(press({ 60.f, 40.f }));
    ui.handlePointerEvent(release({ 60.f, 40.f }));
    check(g_plain.down == 1 && rig.rows[0]->downs == 0, "그 자리의 누름은 머리가 받음");

    for (int i = 0; i < 20; ++i) ui.handlePointerEvent(wheel(kInScroll, -1.f));
    arrange(rig.root);
    check(rig.scroll->scrollOffset() == 200.f, "끝에서 멈춤");
    r = ui.handlePointerEvent(wheel(kInScroll, 0.5f));
    arrange(rig.root);
    check(r.handled && rig.scroll->scrollOffset() == 185.f, "작은 휠 값(터치패드)은 비율만큼 움직임");

    // 내용이 줄면 다음 배치에서 위치를 범위 안으로 당기고, 넘치지 않으면 휠을 받지 않습니다.
    rig.list->clearSlots();
    rig.list->addSlot(HNew(RowProbe), SVerticalBox::ESizeRule::Fixed, 30.f);
    arrange(rig.root);
    check(rig.scroll->scrollOffset() == 0.f && rig.scroll->maxScrollOffset() == 0.f,
          "내용이 줄면 스크롤 위치가 범위 안으로 돌아옴");
    check(rig.list->cachedGeometry().size.x == 400.f, "넘치지 않으면 막대 자리 없이 전체 폭");
    r = ui.handlePointerEvent(wheel(kInScroll, -1.f));
    check(!r.handled, "넘치지 않는 상자는 휠을 받지 않음");
}

void testNestedScrollFirst() {
    std::printf("\n-- 스크롤 상자 안의 스크롤 상자 --\n");
    ScrollRig inner(10);
    auto outerList = HNew(SVerticalBox);
    outerList->addSlot(inner.root, SVerticalBox::ESizeRule::Fixed, 300.f)
             ->addSlot(HNew(RowProbe), SVerticalBox::ESizeRule::Fixed, 300.f);
    auto outer = HNew(SScrollBox)->wheelStep(30.f)->content(outerList);
    arrange(outer);   // 영역 400x300, 내용 600
    HSlateRenderer ui;
    ui.setRootWidget(outer);
    ui.handlePointerEvent(wheel(kInScroll, -1.f));
    arrange(outer);
    check(inner.scroll->scrollOffset() == 30.f && outer->scrollOffset() == 0.f,
          "커서 아래 안쪽 상자가 먼저 스크롤함");
    for (int i = 0; i < 10; ++i) ui.handlePointerEvent(wheel(kInScroll, -1.f));
    arrange(outer);
    check(inner.scroll->scrollOffset() == 200.f, "안쪽 상자는 끝에서 멈춤");
}

// ── DEF-56: 클립 ──────────────────────────────────────────────────────────────
void testClip() {
    std::printf("\n-- DEF-56: 클립 --\n");
    ScrollRig rig(10);
    HPaintContext ctx(nullptr);
    rig.root->paint(ctx, rig.root->cachedGeometry());
    int rowRects = 0, clippedToBox = 0;
    for (const auto& el : ctx.elements) {
        if (el.color != glm::vec4{ 1.f, 1.f, 1.f, 1.f }) continue;   // 행이 그린 사각형만
        ++rowRects;
        if (el.clipped && el.clip == glm::vec4{ 0.f, 50.f, 400.f, 100.f }) ++clippedToBox;
    }
    check(rowRects == 10 && clippedToBox == 10, "스크롤 상자 안의 요소는 모두 상자 영역으로 잘림");
    check(!ctx.hasClip(), "그린 뒤 클립 스택이 비어 있음 (push·pop 짝)");

    HPaintContext nested(nullptr);
    HGeometry a, b;
    a.position = { 0.f, 0.f };   a.size = { 100.f, 100.f };
    b.position = { 50.f, 50.f }; b.size = { 100.f, 100.f };
    nested.pushClip(a);
    nested.pushClip(b);
    nested.drawRect(a, { 1.f, 0.f, 0.f, 1.f });
    nested.popClip();
    nested.popClip();
    nested.drawRect(a, { 1.f, 0.f, 0.f, 1.f });
    check(nested.elements[0].clipped && nested.elements[0].clip == glm::vec4{ 50.f, 50.f, 50.f, 50.f },
          "안쪽 클립은 바깥 클립과의 교집합");
    check(!nested.elements[1].clipped, "클립을 모두 빼면 자르지 않음");

    glm::vec4 rect{ 0.f, 0.f, 100.f, 20.f }, uv{ 0.f, 0.f, 1.f, 1.f };
    const bool kept = HClipQuad(rect, uv, { 50.f, -10.f, 100.f, 100.f });
    check(kept && rect == glm::vec4{ 50.f, 0.f, 50.f, 20.f } && uv == glm::vec4{ 0.5f, 0.f, 1.f, 1.f },
          "잘린 사각형은 UV 도 같은 비율로 줄어듦 (글리프가 찌그러지지 않음)");
    glm::vec4 out{ 200.f, 0.f, 10.f, 10.f }, uv2{ 0.f, 0.f, 1.f, 1.f };
    check(!HClipQuad(out, uv2, { 0.f, 0.f, 100.f, 100.f }), "영역 밖 사각형은 버림");

    // 자르는 테두리: 영역 밖 이동은 "바깥" 으로 알림
    auto row  = std::make_shared<RowProbe>();
    auto clip = HNew(SBorder)->clipChildren(true)->content(row);
    HGeometry g;
    g.size = { 100.f, 20.f };
    clip->onArrangeChildren(g);
    row->onArrangeChildren(HGeometry{ { 0.f, 0.f }, { 100.f, 60.f }, 1.f });   // 테두리보다 큰 자식
    clip->onMouseMove(move({ 50.f, 40.f }, false));
    check(!row->hovered, "자르는 테두리 밖으로 넘친 자식은 그 자리의 이동에 호버를 켜지 않음");
}

// ── DEF-60: 글자 폭 ───────────────────────────────────────────────────────────
// 맨 마지막에 돌립니다. 폰트를 읽는 동안에는 모든 STextBlock 이 실측을 씁니다.
void testTextMeasure() {
    std::printf("\n-- DEF-60: 글자 폭 실측 --\n");
    HFontAtlas font;
    if (!font.loadFont()) {
        std::printf("  (폰트 파일이 없어 건너뜀)\n");
        return;
    }
    check(HFontAtlas::metrics() == &font, "폰트를 읽으면 레이아웃의 글자 폭 기준으로 등록됨");

    const std::string text = "Window Details";
    const float px = 13.f;
    float sum = 0.f;
    for (char c : text) sum += font.glyphAdvance(c, px);
    check(std::fabs(font.measureText(text, px) - sum) < 1e-3f,
          "측정 폭 = 그리기가 글자마다 더하는 advance 의 합");
    check(std::fabs(font.measureText("ab\ncdefg", px) - font.measureText("cdefg", px)) < 1e-3f,
          "여러 줄이면 가장 긴 줄의 폭");

    auto tb = HNew(STextBlock)->text(text)->fontSize(px);
    check(std::fabs(tb->computeDesiredSize().x - sum) < 1e-3f, "STextBlock 의 원하는 폭이 실측과 같음");
    check(std::fabs((float)text.size() * px * 0.6f - sum) > 1.f, "예전 근사(글자 수 x 크기 x 0.6)와는 다름 (전제)");

    auto right = HNew(STextBlock)->text(text)->fontSize(px)->hAlign(EHAlign::Right);
    HGeometry g;
    g.size = { 200.f, 20.f };
    right->onArrangeChildren(g);
    HPaintContext ctx(nullptr);
    right->paint(ctx, g);
    check(ctx.elements.size() == 1 && std::fabs(ctx.elements[0].rect.x + sum - 200.f) < 1e-3f,
          "오른쪽 정렬 텍스트의 끝이 상자 오른쪽 끝에 맞음");

    font.unloadFont();
    check(HFontAtlas::metrics() == nullptr, "폰트를 내리면 등록이 풀림");
    {
        HFontAtlas scoped;
        scoped.loadFont();
    }
    check(HFontAtlas::metrics() == nullptr, "폰트를 읽은 아틀라스가 사라지면 등록도 풀림 (낡은 주소가 남지 않음)");
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
    testDragModifiers();
    testSizeStepFollowsValue();
    testSmallValueDisplay();
    testUndoRecordsTinyEdit();
    testWheelRouting();
    testScrollBox();
    testNestedScrollFirst();
    testClip();
    testTextMeasure();

    if (g_failed) {
        std::printf("\n%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
