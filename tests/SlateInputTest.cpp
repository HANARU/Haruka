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
#include "../src/components/UIPanelComponent.hpp"
#include "../src/components/UIPanelPointer.hpp"
#include "../src/scene/Actor.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../src/scene/ComponentFields.hpp"
#include "../src/editor/EditorIcons.hpp"
#include "../src/ui/slate/widgets/SIcon.hpp"
#include "../src/vfs/Vfs.hpp"
#include "../src/vfs/LogicalPath.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    // 둥근 사각형 클리핑: 잘리지 않은 반대쪽 모서리의 localPos 가 찌그러지지 않고 유지되어야 함
    glm::vec4 rRect{ 0.f, 0.f, 100.f, 20.f }, rUv{ 0.f, 0.f, 1.f, 1.f };
    glm::vec4 rLocal{ -50.f, -10.f, 50.f, 10.f };
    const bool rKept = HClipQuad(rRect, rUv, rLocal, { 50.f, -10.f, 100.f, 100.f });
    check(rKept && rRect == glm::vec4{ 50.f, 0.f, 50.f, 20.f }
          && rLocal == glm::vec4{ 0.f, -10.f, 50.f, 10.f },
          "잘린 둥근 사각형도 localPos 가 클립 비율만큼만 줄어 둥근 모서리가 찌그러지지 않음");
    // 그림자 확장 사각형 클리핑: pad 포함 quad 를 잘라도 중심 기준 localPos 좌표가 보존됨
    glm::vec4 sRect{ -16.f, -16.f, 128.f, 64.f }, sUv{ 0.f, 0.f, 1.f, 1.f };
    glm::vec4 sLocal{ -64.f, -32.f, 64.f, 32.f };
    const bool sKept = HClipQuad(sRect, sUv, sLocal, { 0.f, 0.f, 80.f, 48.f });
    check(sKept && sRect == glm::vec4{ 0.f, 0.f, 80.f, 48.f }
          && sLocal == glm::vec4{ -48.f, -16.f, 32.f, 32.f },
          "잘린 그림자 확장 사각형도 localPos 가 유지되어 블러 형태가 깨지지 않음");
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

    // 3D UI 구간 1: 아틀라스가 가득 차면 오류는 한 번, 넘친 글자는 수만 셈
    {
        HFontAtlas full;
        if (full.initCpuOnly()) {
            // 400 px 글자는 2048 x 512 페이지에 한 줄만 들어감. 페이지(최대 8, 글자 체계 T4-3)를 다 채우도록 크기 셋으로 넣음
            for (float px : { 400.f, 380.f, 360.f })
                for (int c = 33; c < 127; ++c) full.getGlyph((char)c, px);
            const uint32_t dropped = full.droppedGlyphs();
            check(dropped > 0, "큰 글자를 많이 올리면 아틀라스가 넘침 (전제)");
            const HGlyphInfo g = full.getGlyph('~', 360.f);   // 마지막 크기의 끝 글자는 넘친 쪽
            check(g.uvMin == g.uvMax && g.advance > 0.f, "넘친 글자는 그리지 않지만 자리(advance)는 차지함");
            check(full.droppedGlyphs() == dropped, "넘친 글자를 다시 찾으면 캐시에서 나옴 (수가 늘지 않음)");
            full.initCpuOnly();
            check(full.droppedGlyphs() == 0, "다시 초기화하면 넘친 수도 0");
            full.unloadFont();
        }
    }
}

// ── P0: 그리기 목록 고정 (4-1절) ──────────────────────────────────────────────
uint64_t hashDrawList(const HSlateDrawList& dl, VkImageView whiteView, VkImageView atlasView) {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](const void* data, size_t n) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
    };
    const uint64_t vc = dl.vertices.size();
    mix(&vc, sizeof(vc));
    if (!dl.vertices.empty())
        mix(dl.vertices.data(), dl.vertices.size() * sizeof(UIVertex));
    const uint64_t bc = dl.batches.size();
    mix(&bc, sizeof(bc));
    for (const auto& b : dl.batches) {
        uint8_t tag = 2;
        if (b.texture == whiteView) tag = 0;
        else if (atlasView != VK_NULL_HANDLE && b.texture == atlasView) tag = 1;
        mix(&tag, sizeof(tag));
        mix(&b.firstQuad, sizeof(b.firstQuad));
        mix(&b.quadCount, sizeof(b.quadCount));
    }
    const uint8_t ov = dl.overflowed ? 1 : 0;
    mix(&ov, sizeof(ov));
    return h;
}

struct LayeredProbe : HWidget {
    int layerValue = 0;
    glm::vec4 color{ 0.3f, 0.6f, 0.9f, 1.f };
    LayeredProbe(int l, glm::vec4 c) : layerValue(l), color(c) {}
    glm::vec2 computeDesiredSize() override { return { 20.f, 20.f }; }
    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        ctx.pushLayer(layerValue);
        ctx.drawRect(geo, color);
        ctx.popLayer(layerValue);
    }
};

void testDrawListFixed() {
    std::printf("\n-- P0: 그리기 목록 고정 --\n");
    const VkImageView kWhite = reinterpret_cast<VkImageView>(uintptr_t(0x1111));
    const VkImageView kImg   = reinterpret_cast<VkImageView>(uintptr_t(0x2222));

    // (a) 루트 없음 · 접힌 루트 · 그릴 것 없는 루트 -> false
    {
        HSlateRenderer ui;
        HSlateDrawList dl;
        check(!ui.buildDrawList({ 400.f, 300.f }, kWhite, dl), "루트 없음 -> false");

        auto box = HNew(SBox)->widthOverride(100.f)->heightOverride(40.f);
        ui.setRootWidget(box);
        check(!ui.buildDrawList({ 400.f, 300.f }, kWhite, dl), "그릴 것 없음 -> false");

        auto border = HNew(SBorder)->background({ 0.2f, 0.3f, 0.4f, 1.f });
        border->visibility = EVisibility::Collapsed;
        ui.setRootWidget(border);
        check(!ui.buildDrawList({ 400.f, 300.f }, kWhite, dl), "접힌 루트 -> false");
    }

    // (b) 복합 위젯 트리 고정 해시
    {
        HSlateRenderer ui;
        const bool hasFont = ui.fontAtlas().initCpuOnly();
        if (!hasFont) {
            std::printf("  (폰트 파일이 없어 고정 해시 검사를 건너뜀)\n");
        } else {
            auto vbox = HNew(SVerticalBox);
            auto hbox = HNew(SHorizontalBox);
            hbox->addSlot(HNew(SButton)->content(HNew(STextBlock)->text("OK")->fontSize(13.f)),
                          SHorizontalBox::ESizeRule::Auto)
                ->addSlot(HNew(SImage)->image(kImg)->desiredSize({ 24.f, 24.f }),
                          SHorizontalBox::ESizeRule::Fixed, 24.f);

            auto scroll = HNew(SScrollBox);
            auto inner  = HNew(SVerticalBox);
            for (int i = 0; i < 10; ++i)
                inner->addSlot(HNew(SBorder)->background({ 0.1f * i, 0.2f, 0.3f, 1.f })
                    ->content(HNew(SBox)->heightOverride(30.f)), SVerticalBox::ESizeRule::Auto);
            scroll->content(inner);

            auto text = HNew(STextBlock)->text("Line 1\nLine 2 spaced")->fontSize(14.f);

            auto overlay = HNew(SOverlay);
            overlay->addSlot(std::make_shared<LayeredProbe>(2, glm::vec4{ 0.8f, 0.2f, 0.2f, 1.f }))
                   ->addSlot(HNew(SBorder)->background({ 0.5f, 0.5f, 0.5f, 1.f }))
                   ->addSlot(std::make_shared<LayeredProbe>(-1, glm::vec4{ 0.2f, 0.8f, 0.2f, 1.f }))
                   ->addSlot(HNew(STextBlock)->text("Top")->fontSize(12.f));

            vbox->addSlot(HNew(SBorder)->background({ 0.1f, 0.1f, 0.1f, 1.f })->content(hbox),
                          SVerticalBox::ESizeRule::Auto)
                ->addSlot(text, SVerticalBox::ESizeRule::Auto)
                ->addSlot(scroll, SVerticalBox::ESizeRule::Fixed, 80.f)
                ->addSlot(overlay, SVerticalBox::ESizeRule::Fill, 1.f);

            ui.setRootWidget(vbox);
            HSlateDrawList dl;
            const bool ok = ui.buildDrawList({ 400.f, 300.f }, kWhite, dl);
            const uint64_t h = hashDrawList(dl, kWhite, ui.fontAtlas().atlasView());
            // 기본 UIVertex{pos,uv,color} 영역만 따로 해시해도 D1 기준(0xdd11fcb176aa175b)과 같아야 함
            uint64_t hBase = 14695981039346656037ULL;
            auto mixBase = [&](const void* data, size_t n) {
                const auto* p = static_cast<const uint8_t*>(data);
                for (size_t i = 0; i < n; ++i) { hBase ^= p[i]; hBase *= 1099511628211ULL; }
            };
            const uint64_t vc = dl.vertices.size();
            mixBase(&vc, sizeof(vc));
            for (const auto& v : dl.vertices) {
                struct BaseVtx { glm::vec2 pos, uv; glm::vec4 color; } b{ v.pos, v.uv, v.color };
                mixBase(&b, sizeof(b));
            }
            const uint64_t bc = dl.batches.size();
            mixBase(&bc, sizeof(bc));
            for (const auto& b : dl.batches) {
                uint8_t tag = 2;
                if (b.texture == kWhite) tag = 0;
                else if (ui.fontAtlas().atlasView() != VK_NULL_HANDLE && b.texture == ui.fontAtlas().atlasView()) tag = 1;
                mixBase(&tag, sizeof(tag));
                mixBase(&b.firstQuad, sizeof(b.firstQuad));
                mixBase(&b.quadCount, sizeof(b.quadCount));
            }
            const uint8_t ov = dl.overflowed ? 1 : 0;
            mixBase(&ov, sizeof(ov));
            constexpr uint64_t kExpectedBaseHash = 0xdd11fcb176aa175bULL;
            constexpr uint64_t kExpectedHash     = 0x4f444091dd5d8b8bULL;
            check(ok && !dl.overflowed && dl.vertices.size() == 128 && dl.batches.size() == 3 &&
                  hBase == kExpectedBaseHash && h == kExpectedHash,
                  "복합 트리 그리기 목록 고정 (verts=128, batches=3, base=0xdd11fcb176aa175b, full=0x4f444091dd5d8b8b)");
        }
    }

    // (c) 정점 예산 초과 (사각형 16,385개) -> overflowed=true, 정점 65,536
    {
        auto vbox = HNew(SVerticalBox);
        for (int i = 0; i < 16385; ++i)
            vbox->addSlot(HNew(SBorder)->background({ 1.f, 0.f, 0.f, 1.f })
                ->content(HNew(SBox)->widthOverride(4.f)->heightOverride(4.f)),
                SVerticalBox::ESizeRule::Auto);
        HSlateRenderer ui;
        ui.setRootWidget(vbox);
        HSlateDrawList dl;
        const bool ok = ui.buildDrawList({ 400.f, 70000.f }, kWhite, dl);
        check(ok && dl.overflowed && dl.vertices.size() == HSlateRenderer::maxVerts(),
              "예산 초과(16,385 사각형) -> overflowed=true, 정점 65,536");
    }
}

void testMultiContextCapture() {
    std::printf("\n-- P0: 여러 문맥 포인터 캡처 분리 --\n");
    resetCounts();
    auto rootA = HNew(SOverlay);
    rootA->addSlot(std::make_shared<CaptorProbe>(), EHAlign::Fill, EVAlign::Fill, kPadA);
    auto rootB = HNew(SOverlay);
    rootB->addSlot(std::make_shared<PlainProbe>(), EHAlign::Fill, EVAlign::Fill, kPadB);
    arrange(rootA);
    arrange(rootB);

    HSlateContext ctxA, ctxB;
    ctxA.setRootWidget(rootA);
    ctxB.setRootWidget(rootB);

    ctxA.handlePointerEvent(press(kInA));
    check(g_cap.down == 1, "문맥 A 에서 누름 -> 캡처 시작");

    // 다른 문맥 B 로 이동 · 뗌을 보내도 문맥 A 의 캡처가 풀리거나 B 가 영향을 주지 않음
    ctxB.handlePointerEvent(move(kInB, true));
    ctxB.handlePointerEvent(release(kInB));
    check(g_cap.move == 0 && g_cap.up == 0 && g_plain.up == 1, "문맥 B 의 입력은 문맥 B 로만 전달");

    // 문맥 A 로 이동 · 뗌을 보내면 여전히 캡처한 위젯이 받음
    ctxA.handlePointerEvent(move(kInB, true));
    ctxA.handlePointerEvent(release(kInB));
    check(g_cap.move == 1 && g_cap.up == 1, "문맥 A 의 캡처가 유지되었다가 A 의 뗌에서 해제됨");

    // 한쪽 루트 교체 시 그 문맥의 캡처만 정리되고 다른 문맥은 유지
    resetCounts();
    auto rootB2 = HNew(SOverlay);
    rootB2->addSlot(std::make_shared<CaptorProbe>(), EHAlign::Fill, EVAlign::Fill, kPadB);
    arrange(rootB2);
    ctxB.setRootWidget(rootB2);

    ctxA.handlePointerEvent(press(kInA));
    ctxB.handlePointerEvent(press(kInB));
    ctxA.setRootWidget(HNew(SBox));
    ctxB.handlePointerEvent(release(kEmpty));
    check(g_cap.up == 1, "문맥 A 루트 교체 뒤에도 문맥 B 의 캡처는 유지됨");
}

void testSlotVertexOffset() {
    std::printf("\n-- P0: 슬롯 정점 오프셋 --\n");
    uint32_t slotOffset = 0;
    const uint32_t off1 = HSlateRenderer::advanceSlotVertexOffset(slotOffset, 120);
    const uint32_t off2 = HSlateRenderer::advanceSlotVertexOffset(slotOffset, 64);
    check(off1 == 0 && off2 == 120 && slotOffset == 184,
          "같은 슬롯에 두 번 더하면 이어 씀");

    // 슬롯 0 · 1 · 0 · 1 · 0 순서로 beginFrame → 한 번 더하기 반복 시 매번 0 부터 시작(DEF 재현)
    HSlateRenderer ui;
    uint32_t simSlots[HSlateRenderer::kFramesInFlight] = { 999, 999 };
    const uint32_t seq[] = { 0, 1, 0, 1, 0 };
    bool allZero = true;
    for (uint32_t s : seq) {
        ui.beginFrame(s);
        if (ui.slotVertexOffset(s) != 0) allZero = false;
        simSlots[s] = ui.slotVertexOffset(s);
        const uint32_t base = HSlateRenderer::advanceSlotVertexOffset(simSlots[s], 120);
        if (base != 0 || simSlots[s] != 120) allZero = false;
    }
    check(allZero, "슬롯 0·1·0·1·0 순환에서 beginFrame 뒤 매번 오프셋 0 으로 시작");
}

void testLayerSortStability() {
    std::printf("\n-- P0: 층 정렬 안정성 (40개 동층 요소) --\n");
    auto overlay = HNew(SOverlay);
    overlay->addSlot(std::make_shared<LayeredProbe>(1, glm::vec4{ 1.f, 1.f, 1.f, 1.f }));
    for (int i = 0; i < 40; ++i) {
        const float c = (i + 1) / 41.f;
        overlay->addSlot(std::make_shared<LayeredProbe>(0, glm::vec4{ c, 0.f, 0.f, 1.f }));
    }
    overlay->addSlot(std::make_shared<LayeredProbe>(-1, glm::vec4{ 0.f, 1.f, 0.f, 1.f }));

    HSlateRenderer ui;
    ui.setRootWidget(overlay);
    HSlateDrawList dl;
    const VkImageView kWhite = reinterpret_cast<VkImageView>(uintptr_t(0x1111));
    const bool ok = ui.buildDrawList({ 200.f, 200.f }, kWhite, dl);
    bool ordered = ok && dl.vertices.size() == 42 * 4;
    if (ordered) {
        // 인덱스 0 은 layer -1 (green=1), 1..40 은 layer 0 (red 오름차순), 41 은 layer 1 (white)
        if (dl.vertices[0].color.g != 1.f) ordered = false;
        for (int i = 0; i < 40; ++i) {
            const float expectedR = (i + 1) / 41.f;
            if (std::fabs(dl.vertices[(1 + i) * 4].color.r - expectedR) > 1e-6f) {
                ordered = false;
                break;
            }
        }
    }
    check(ordered, "동일 층 40개 요소의 그리기 순서가 stable_sort 로 보존됨");
}

// ── 3D UI 패널 P1b: UIPanelComponent(텍스처 크기 · 직렬화 · 확정 시점 · 내용) ─────────────────────
void testUIPanelComponent() {
    std::printf("\n-- P1b: UIPanelComponent --\n");
    using P = UIPanelComponent;

    // (a) 텍스처 크기: 1,250 px/m 기본, 한 변 4096 상한, 크기 · 해상도는 범위로 자름
    check(P::computeTextureSize(0.8f, 0.5f, 1250.f) == glm::uvec2(1000, 625), "0.8 x 0.5 m @ 1250 -> 1000 x 625");
    check(P::computeTextureSize(10.f, 1.f, 1250.f) == glm::uvec2(4096, 1250), "긴 변은 4096 으로 자름");
    check(P::computeTextureSize(0.001f, 0.001f, 1250.f) == glm::uvec2(13, 13), "최소 크기 0.01 m -> 13 px");
    check(P::computeTextureSize(1.f, 1.f, 50.f) == glm::uvec2(100, 100), "해상도 하한 100 px/m");

    // (b) 직렬화 왕복과 깨진 값
    {
        P a;
        a.width = 1.2f; a.height = 0.3f; a.pixelsPerMeter = 2000.f; a.twoSided = true; a.content = "Demo";
        ComponentFieldWriter w;
        a.writeFields(w);
        P b;
        bool allKnown = true;
        for (const auto& f : w.fields()) allKnown = b.readField(f.first, f.second) && allKnown;
        check(allKnown && w.fields().size() == 9 && b.width == 1.2f && b.height == 0.3f &&
              b.pixelsPerMeter == 2000.f && b.twoSided && b.content == "Demo" && b.brightness == 1.f,
              "값 아홉 개(밝기 · 배치 포함)가 왕복함");

        P c;
        const bool handled = c.readField("width", "-1") && c.readField("pixelsPerMeter", "99999");
        check(handled && c.width == 0.8f && c.pixelsPerMeter == P::kDefaultPixelsPerMeter, "범위 밖 값은 기본값 유지");
        check(!c.readField("direction", "0 0 1"), "모르는 이름은 false(직렬화기가 경고)");
    }

    // (c) 텍스처 크기는 확정할 때만 바뀜(끄는 동안 GPU 를 기다리지 않음)
    {
        P p;
        p.onAttach();
        const glm::uvec2 before = p.textureSize();
        p.width = 1.6f;
        const bool unchanged = p.textureSize() == before;
        p.onPropsCommitted();
        check(unchanged && p.textureSize() == glm::uvec2(2000, 625), "편집 중에는 그대로, 확정하면 2000 x 625");
    }

    // (d) 선택 판정 상자: 얇은 판(두께 1 cm), 액터 원점 가운데
    {
        P p;
        glm::vec3 mn, mx;
        const bool ok = p.localBounds(mn, mx);
        check(ok && mn == glm::vec3(-0.4f, -0.25f, -0.005f) && mx == glm::vec3(0.4f, 0.25f, 0.005f),
              "localBounds = (+-0.4, +-0.25, +-0.005)");
    }

    // (e) 내용 등록 API: 엔진 기본 등록은 비어 있고, 게임이 등록한 내용만 열거 · 생성됨. 모르는 이름은 안내 화면
    {
        check(P::contentNames().empty(), "엔진 기본 UIPanel 내용 등록소는 비어 있음(P3b)");
        P::registerContent("Demo", [](P& panel, float s) {
            auto root = std::make_shared<SBorder>();
            root->background(glm::vec4(0.07f, 0.08f, 0.11f, 1.f))
                ->content(std::make_shared<STextBlock>()
                              ->text(panel.paramAttr("title", "Haruka UI Panel"))
                              ->fontSize(32.f * s));
            return std::shared_ptr<HWidget>(root);
        });
        P p;
        const VkImageView kWhite = reinterpret_cast<VkImageView>(uintptr_t(0x1111));
        HSlateRenderer ui;
        const bool hasFont = ui.fontAtlas().initCpuOnly();
        HSlateContext& ctx = p.slate();
        const auto demoRoot = ctx.rootWidget();
        check(demoRoot != nullptr && p.takeDirty() && !p.takeDirty(), "등록한 Demo 트리를 만들고 한 번만 더러움");
        if (hasFont) {
            HSlateDrawList dl;
            const bool ok = ctx.buildDrawList({ 1000.f, 625.f }, ui.fontAtlas(), kWhite, HSlateRenderer::maxVerts(), dl);
            check(ok && !dl.overflowed && dl.vertices.size() > 6, "Demo 그리기 목록에 배경 · 글자가 있음");
        }
        p.content = "NoSuchPanel";
        p.slate();
        check(ctx.rootWidget() != demoRoot && p.takeDirty(), "모르는 내용 -> 안내 화면으로 다시 만들고 더러움");
        const auto names = P::contentNames();
        check(std::find(names.begin(), names.end(), "Demo") != names.end(), "등록된 내용에 Demo 가 있음");
    }

    // (f) 3D UI 구간 3: 해상도와 배치 분리. 배치는 1,250 px/m 기준 픽셀 × 배율이라 해상도를 바꿔도 미터 기준으로 같음
    {
        check(P::computeLayoutScale(0.8f, 0.5f, 1250.f) == 1.f, "1250 px/m -> 배율 1");
        check(P::computeLayoutScale(0.8f, 0.5f, 2500.f) == 2.f, "2500 px/m -> 배율 2");
        check(P::computeLayoutScale(0.81f, 0.5f, 1250.f) == 1.f, "반올림(1012.5 -> 1013 px)이 있어도 배율은 정확히 1");
        check(std::fabs(P::computeLayoutScale(10.f, 1.f, 1250.f) - 4096.f / 12500.f) < 1e-6f,
              "긴 변이 4096 에 잘리면 그 변에 맞게 줄임 (0.328)");

        // 등록소는 전역이라 팩토리가 지역 변수를 붙잡지 않게 static 에 기록합니다.
        static P*    seen      = nullptr;
        static float seenScale = 0.f;
        P::registerContent("ScaleProbe", [](P& panel, float s) {
            seen = &panel;
            seenScale = s;
            return std::shared_ptr<HWidget>(HNew(SBox)->widthOverride(100.f * s)->heightOverride(50.f * s));
        });
        P p;
        p.content = "ScaleProbe";
        p.pixelsPerMeter = 2500.f;
        p.onPropsCommitted();
        const auto r1 = p.slate().rootWidget();
        check(seen == &p && seenScale == 2.f, "팩토리가 패널 자신과 배율 2 를 받음");
        check(r1 && r1->computeDesiredSize() == glm::vec2(200.f, 100.f), "배율을 곱한 크기로 트리를 만듦");
        p.takeDirty();
        p.onPropsCommitted();   // 같은 값으로 다시 확정
        check(p.slate().rootWidget() == r1, "배율이 같으면 트리를 다시 만들지 않음");
        p.pixelsPerMeter = 1250.f;
        p.slate();
        check(p.slate().rootWidget() == r1, "편집 중(확정 전)에는 트리 그대로");
        p.onPropsCommitted();
        const auto r2 = p.slate().rootWidget();
        check(r2 != r1 && seenScale == 1.f && p.takeDirty(), "확정하면 새 배율로 다시 만들고 더러움");
        check(p.readOnlyInfo() == "Texture 1000 x 625 px, layout scale 1.00", "디테일 정보에 배율 표시");

        // Demo 는 해상도를 두 배로 하면 텍스처 픽셀 기준 크기도 두 배(화면에서는 같은 크기)
        HSlateRenderer ui;
        ui.fontAtlas().initCpuOnly();
        P d1, d2;
        d2.pixelsPerMeter = 2500.f;
        d1.onPropsCommitted();
        d2.onPropsCommitted();
        const glm::vec2 s1 = d1.slate().rootWidget()->computeDesiredSize();
        const glm::vec2 s2 = d2.slate().rootWidget()->computeDesiredSize();
        check(s1.x > 0.f && std::fabs(s2.x / s1.x - 2.f) < 0.02f && std::fabs(s2.y / s1.y - 2.f) < 0.02f,
              "Demo 원하는 크기가 배율에 비례 (2500 / 1250 = 2)");
    }

    // (g) 3D UI 구간 4: 게임 API(패널별 값 · 동작)와 밝기
    {
        P p;
        check(p.setParam("title", "Hello") && p.param("title") == "Hello", "값 넣고 읽기");
        check(!p.setParam("bad name", "x") && !p.setParam("", "x") && !p.setParam("a=b", "x"), "공백 · 빈 이름 · '=' 이름은 거부");
        p.setParam("multi", "line1\nline2");
        check(p.param("multi") == "line1 line2", "값의 줄바꿈은 공백(씬 파일 한 줄)");
        check(p.param("none", "fb") == "fb", "없는 값은 대체값");
        p.setParam("score", "12.5");
        check(p.paramFloat("score", 0.f) == 12.5f && p.paramFloat("title", -1.f) == -1.f && p.paramFloat("none", 3.f) == 3.f,
              "숫자로 읽기, 숫자가 아니거나 없으면 대체값");

        auto attr = p.paramAttr("title", "untitled");
        p.setParam("title", "Changed");
        check(attr.get() == "Changed", "paramAttr 는 그릴 때마다 지금 값을 읽음");
        p.clearParam("title");
        check(attr.get() == "untitled", "값을 지우면 대체값");

        // 직렬화: param.이름 줄로 저장, 이름 순, 깨진 이름은 읽을 때 건너뜀
        p.setParam("title", "Hi");
        p.brightness = 0.5f;
        ComponentFieldWriter w;
        p.writeFields(w);
        std::vector<std::string> keys;
        for (const auto& f : w.fields()) keys.push_back(f.first);
        check(keys.size() == 12 && keys[9] == "param.multi" && keys[10] == "param.score" && keys[11] == "param.title",
              "param.이름 줄이 이름 순으로 저장됨");
        P q;
        for (const auto& f : w.fields()) q.readField(f.first, f.second);
        check(q.params() == p.params() && q.brightness == 0.5f, "값 · 밝기가 왕복함");
        P r;
        const bool handled = r.readField("param.bad name", "x") && r.readField("brightness", "3");
        check(handled && r.params().empty() && r.brightness == 1.f, "깨진 이름 · 범위 밖 밝기는 무시(경고)");
        check(p.readOnlyInfo().find(", 3 param(s)") != std::string::npos, "디테일 정보에 값 개수");

        // 동작: 팩토리가 버튼에 actionCallback 을 연결, 게임 코드는 setActionHandler 로 받음
        P::registerContent("ActionProbe", [](P& panel, float) {
            return std::shared_ptr<HWidget>(HNew(SButton)->onClick(panel.actionCallback("go")));
        });
        P a;
        a.content = "ActionProbe";
        std::string got;
        P* from = nullptr;
        a.setActionHandler([&](P& panel, const std::string& act) { from = &panel; got = act; });
        HSlateContext& ctx = a.slate();
        ctx.arrange({ 400.f, 200.f });
        HPointerEvent e;
        e.position = { 200.f, 100.f };
        e.button   = 0;
        e.down     = true;
        ctx.handlePointerEvent(e);
        e.down = false;
        ctx.handlePointerEvent(e);
        check(got == "go" && from == &a, "버튼을 누르고 떼면 처리기가 동작 이름과 패널을 받음");
        a.setActionHandler(nullptr);
        a.fireAction("go");   // 처리기가 없으면 아무 일도 없음(죽지 않음)
        check(true, "처리기 없이 동작을 내도 안전");
    }
}

// ── 3D UI 구간 2: 자동 다시 그리기(그리기 목록 해시) ──────────────────────────────
// 렌더러는 프레임마다 패널의 그리기 목록을 만들어 drawListHash 가 직전과 다를 때만 GPU 로 다시 그립니다.
void testPanelAutoRedraw() {
    std::printf("\n-- 3D UI 구간 2: 그리기 목록 해시 --\n");
    const VkImageView kWhite = reinterpret_cast<VkImageView>(uintptr_t(0x1111));
    HSlateRenderer ui;
    const bool hasFont = ui.fontAtlas().initCpuOnly();

    std::string label = "Score 1";
    glm::vec4   ink{ 1.f, 1.f, 1.f, 1.f };
    bool        badge = true;
    auto badgeBox = HNew(SBorder)->background({ 0.9f, 0.1f, 0.1f, 1.f })->padding(HPadding(8.f));
    badgeBox->visibility.bind([&] { return badge ? EVisibility::Visible : EVisibility::Collapsed; });
    auto text = HNew(STextBlock)->text(Attr<std::string>(std::function<std::string()>([&] { return label; })))
                                ->color(Attr<glm::vec4>(std::function<glm::vec4()>([&] { return ink; })))
                                ->fontSize(30.f);
    auto column = std::make_shared<SVerticalBox>();
    column->addSlot(text, SVerticalBox::ESizeRule::Auto)->addSlot(badgeBox, SVerticalBox::ESizeRule::Auto);
    auto root = HNew(SBorder)->background({ 0.1f, 0.2f, 0.3f, 1.f })->content(column);
    HSlateContext ctx;
    ctx.setRootWidget(root);
    auto hashNow = [&] {
        HSlateDrawList dl;
        ctx.buildDrawList({ 400.f, 200.f }, ui.fontAtlas(), kWhite, HSlateRenderer::maxVerts(), dl);
        return drawListHash(dl);
    };
    const uint64_t h1 = hashNow();
    check(hashNow() == h1, "아무것도 바뀌지 않으면 해시가 같음 -> GPU 로 다시 그리지 않음");
    badge = false;
    check(hashNow() != h1, "보이기 Attr 이 바뀌면 해시가 바뀜 (markDirty 없이)");
    badge = true;
    check(hashNow() == h1, "되돌리면 처음 해시와 같음");
    if (hasFont) {
        label = "Score 2";
        check(hashNow() != h1, "글자 Attr 이 바뀌면 해시가 바뀜");
        label = "Score 1";
        ink   = { 1.f, 0.8f, 0.2f, 1.f };
        check(hashNow() != h1, "글자 색 Attr 이 바뀌면 해시가 바뀜");
        ink   = { 1.f, 1.f, 1.f, 1.f };
        check(hashNow() == h1, "같은 글자 · 색으로 돌아오면 같은 해시");
    }
    HSlateDrawList a, b;
    check(drawListHash(a) == drawListHash(b), "빈 목록끼리는 같은 해시");
    a.batches.push_back({ kWhite, 0, 1 });
    check(drawListHash(a) != drawListHash(b), "배치만 달라도 해시가 다름");
}

// ── 3D UI 구간 5: 월드 패널 포인터(광선 → UV → HPointerEvent) ─────────────────────
void testPanelPointer() {
    std::printf("\n-- 3D UI 구간 5: 패널 포인터 --\n");
    using P   = UIPanelComponent;
    using Ptr = UIPanelPointer;
    static int taps = 0;
    P::registerContent("PointerProbe", [](P& panel, float) {
        return std::shared_ptr<HWidget>(HNew(SButton)->onClick(panel.actionCallback("tap"))
                                            ->normalColor({ 0.2f, 0.2f, 0.2f, 1.f })
                                            ->hoveredColor({ 0.5f, 0.5f, 0.5f, 1.f }));
    });

    // (a) 판정: 0.8 x 0.5 m 패널, 원점, 앞면 +Z
    Actor a;
    P* p = a.addComponent<P>();
    p->content = "PointerProbe";
    Ptr::Hit h;
    check(Ptr::intersect(*p, { 0.f, 0.f, 2.f }, { 0.f, 0.f, -1.f }, h) && h.uv == glm::vec2(0.5f) &&
          std::fabs(h.distance - 2.f) < 1e-5f && h.pixel == glm::vec2(500.f, 312.5f) && h.front,
          "정면 가운데 -> uv (0.5, 0.5), 거리 2, 픽셀 (500, 312.5)");
    check(Ptr::intersect(*p, { 0.2f, 0.125f, 2.f }, { 0.f, 0.f, -1.f }, h) &&
          glm::length(h.uv - glm::vec2(0.75f, 0.25f)) < 1e-5f, "오른쪽 위 -> uv (0.75, 0.25)(v 는 아래로)");
    check(!Ptr::intersect(*p, { 0.f, 0.f, 2.f }, { 0.f, 0.f, 1.f }, h), "패널 반대쪽을 보면 맞지 않음");
    check(!Ptr::intersect(*p, { 0.5f, 0.f, 2.f }, { 0.f, 0.f, -1.f }, h), "사각형 밖은 맞지 않음");
    check(Ptr::intersect(*p, { 0.5f, 0.f, 2.f }, { 0.f, 0.f, -1.f }, h, false) && h.uv.x > 1.f,
          "캡처용(bounded=false)은 밖이어도 평면 좌표");
    check(!Ptr::intersect(*p, { 0.2f, 0.f, -2.f }, { 0.f, 0.f, 1.f }, h), "한 면 패널은 뒤에서 맞지 않음");
    p->twoSided = true;
    check(Ptr::intersect(*p, { 0.2f, 0.f, -2.f }, { 0.f, 0.f, 1.f }, h) && !h.front &&
          std::fabs(h.uv.x - 0.25f) < 1e-5f, "양면 패널을 뒤에서 맞으면 u 를 뒤집음(글자가 바로 읽히는 쪽)");
    p->twoSided = false;
    {
        Actor b;
        b.setLocalPosition({ 1.f, 0.f, 0.f });
        b.setLocalRotation(glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f)));   // 앞면이 +X
        b.setLocalScale({ 2.f, 2.f, 2.f });
        P* q = b.addComponent<P>();
        check(Ptr::intersect(*q, { 3.f, 0.25f, -0.4f }, { -1.f, 0.f, 0.f }, h) && std::fabs(h.distance - 2.f) < 1e-4f &&
              glm::length(h.uv - glm::vec2(0.75f, 0.25f)) < 1e-4f, "돌리고 키운 액터도 로컬 평면으로 판정(크기 2배면 1.6 x 1 m)");
    }

    // (b) 광선 만들기: 화면 가운데는 카메라 앞쪽
    {
        // 엔진 카메라 투영(Reverse-Z 무한 원평면, C1)과 같은 행렬
        glm::mat4 proj(0.f);
        const float fy = 1.f / std::tan(glm::radians(60.f) * 0.5f);
        proj[0][0] = fy / 1.5f; proj[1][1] = -fy; proj[3][2] = 0.1f; proj[2][3] = -1.f;
        const glm::mat4 vp = proj * glm::lookAt(glm::vec3(0.f, 1.f, 3.f), glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f));
        Ptr::Ray r;
        check(Ptr::rayFromScreen(vp, { 0.5f, 0.5f }, r) && glm::length(r.dir - glm::vec3(0.f, 0.f, -1.f)) < 1e-4f,
              "화면 가운데 광선 = 카메라 앞쪽");
        check(!Ptr::rayFromScreen(vp, { 1.2f, 0.5f }, r) && !r.valid, "화면 밖이면 광선 없음");
    }

    // (c) 호버 · 클릭: 누르고 떼면 동작, 호버가 그리기 목록을 바꿈
    HSlateRenderer ui;
    const VkImageView kWhite = reinterpret_cast<VkImageView>(uintptr_t(0x1111));
    auto listHash = [&](P& panel) {
        HSlateDrawList dl;
        panel.slate().buildDrawList(glm::vec2(panel.textureSize()), ui.fontAtlas(), kWhite, HSlateRenderer::maxVerts(), dl);
        return drawListHash(dl);
    };
    p->setActionHandler([](P&, const std::string& act) { if (act == "tap") ++taps; });
    const uint64_t idle = listHash(*p);
    std::vector<P*> panels{ p };
    Ptr ptr;
    Ptr::Ray on;  on.origin  = { 0.f, 0.f, 2.f };  on.dir  = { 0.f, 0.f, -1.f }; on.valid = true;
    Ptr::Ray off; off.origin = { 3.f, 0.f, 2.f };  off.dir = { 0.f, 0.f, -1.f }; off.valid = true;
    ptr.update(panels, on, {});
    check(ptr.overPanel() && ptr.hit().panel == p && listHash(*p) != idle, "패널을 가리키면 호버(그리기 목록이 바뀜)");
    ptr.update(panels, on, { true, 0.f });
    check(ptr.capturing() && taps == 0, "누르면 캡처, 아직 동작 없음");
    ptr.update(panels, on, { false, 0.f });
    check(taps == 1 && !ptr.capturing(), "떼면 동작 한 번");
    ptr.update(panels, off, {});
    check(!ptr.overPanel() && listHash(*p) == idle, "벗어나면 바깥 이동으로 호버가 풀림");

    // (d) 캡처: 패널에서 눌러 밖에서 떼면 뗌은 그 패널이 받고, 클릭은 아님(버튼 규칙)
    ptr.update(panels, on, { true, 0.f });
    ptr.update(panels, off, { true, 0.f });
    check(ptr.capturing() && ptr.hit().panel == p && ptr.hit().uv.x > 1.f, "누르는 동안 밖으로 나가도 누른 패널의 평면 좌표");
    ptr.update(panels, off, { false, 0.f });
    check(taps == 1 && !ptr.capturing(), "밖에서 떼면 클릭이 아님");
    ptr.update(panels, off, {});
    check(!ptr.overPanel(), "뗀 뒤에는 밖이므로 가리키지 않음");
    // 패널 밖에서 누르고 들어오면 누름이 아님
    ptr.update(panels, off, { true, 0.f });
    ptr.update(panels, on, { true, 0.f });
    ptr.update(panels, on, { false, 0.f });
    check(taps == 1, "밖에서 누르고 들어와 떼도 클릭이 아님");

    // (e) 가림 · 가장 가까운 패널 · 거리
    {
        Actor wall;
        Ptr::Occluder blocked = [&](const glm::vec3&, const glm::vec3&, float, float& d, Actor*& who) {
            d = 1.f; who = &wall; return true;
        };
        Ptr::Occluder self = [&](const glm::vec3&, const glm::vec3&, float, float& d, Actor*& who) {
            d = 1.99f; who = &a; return true;   // 패널 자신의 충돌체
        };
        Ptr q;
        q.update(panels, on, {}, blocked);
        check(!q.overPanel(), "패널보다 앞에 물체가 있으면 가리키지 않음");
        q.update(panels, on, {}, self);
        check(q.overPanel(), "패널 액터 자신의 충돌체는 가리지 않음");

        Actor nearA;
        nearA.setLocalPosition({ 0.f, 0.f, 1.f });
        P* nearP = nearA.addComponent<P>();
        nearP->content = "PointerProbe";
        std::vector<P*> two{ p, nearP };
        q.update(two, on, {});
        check(q.hit().panel == nearP && std::fabs(q.hit().distance - 1.f) < 1e-5f, "겹치면 가까운 패널");
        q.maxDistance = 0.5f;
        q.update(two, on, {});
        check(!q.overPanel(), "maxDistance 보다 멀면 가리키지 않음");
        q.maxDistance = 20.f;

        // (f) 목록에서 빠진 패널(지워짐 · 꺼짐)은 붙잡지 않음
        q.update(two, on, { true, 0.f });
        check(q.capturing() && q.hit().panel == nearP, "가까운 패널을 누름");
        std::vector<P*> onlyFar{ p };
        q.update(onlyFar, on, { true, 0.f });
        check(!q.capturing() || q.hit().panel != nearP, "누른 패널이 목록에서 빠지면 캡처를 버림");
        q.update(onlyFar, on, { false, 0.f });
        q.reset();
        check(!q.overPanel() && !q.capturing(), "reset 은 상태를 비움");
        Ptr::Ray none;
        q.update(onlyFar, none, {});
        check(!q.overPanel(), "광선이 없으면(추적 잃음) 가리키지 않음");
    }
}

// ── 3D UI 구간 6: XR 컨트롤러(조준 광선 · 트리거 · 스틱 휠 · 레이저) ──────────────────
struct WheelProbe : HWidget {
    float total = 0.f;
    glm::vec2 computeDesiredSize() override { return { 10.f, 10.f }; }
    void paint(HPaintContext&, const HGeometry&) override {}
    HReply onScroll(const HPointerEvent& e) override { total += e.wheelDelta; return HReply::Handled(); }
};

void testPanelPointerXr() {
    std::printf("\n-- 3D UI 구간 6: XR 컨트롤러 포인터 --\n");
    using P   = UIPanelComponent;
    using Ptr = UIPanelPointer;

    // (a) 조준 자세 -> 월드 광선: XR 원점(위치 + 요)을 곱하고, 조준 방향은 -Z
    {
        const glm::mat4 origin = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 0.f, 0.f)) *
                                 glm::mat4_cast(glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f)));
        const Ptr::Ray r = Ptr::rayFromAim(origin, { 0.f, 1.5f, 0.f }, glm::quat(1.f, 0.f, 0.f, 0.f));
        check(r.valid && glm::length(r.origin - glm::vec3(10.f, 1.5f, 0.f)) < 1e-5f &&
              glm::length(r.dir - glm::vec3(-1.f, 0.f, 0.f)) < 1e-5f, "원점 요 90도면 조준 -Z 가 월드 -X");
        const Ptr::Ray down = Ptr::rayFromAim(glm::mat4(1.f), {}, glm::angleAxis(glm::radians(-90.f), glm::vec3(1.f, 0.f, 0.f)));
        check(glm::length(down.dir - glm::vec3(0.f, -1.f, 0.f)) < 1e-5f, "손목을 아래로 꺾으면 아래를 가리킴");
    }

    // (b) 트리거: 0.55 넘으면 누름, 0.35 아래로 가야 뗌
    {
        Ptr::TriggerLatch t;
        const bool a = t.update(0.5f), b = t.update(0.6f), c = t.update(0.4f), d = t.update(0.3f);
        check(!a && b && c && !d, "트리거 히스테리시스(0.5 안 누름, 0.6 누름, 0.4 유지, 0.3 뗌)");
    }

    // (c) 스틱 휠: 가운데 0.25 무시, 끝까지 밀면 초당 8 칸, 아래로 밀면 음수
    check(Ptr::stickWheel(0.2f, 0.1f) == 0.f, "스틱 0.2 는 무시");
    check(std::fabs(Ptr::stickWheel(1.f, 0.5f) - 4.f) < 1e-5f, "끝까지 0.5 초 -> 4 칸");
    check(Ptr::stickWheel(-1.f, 0.1f) < 0.f && std::fabs(Ptr::stickWheel(-0.625f, 1.f) + 4.f) < 1e-4f,
          "아래로 밀면 음수, 0.625 는 절반 속도");

    // (d) 휠은 가리키는 패널의 위젯이 받음
    static WheelProbe* probe = nullptr;
    P::registerContent("WheelProbe", [](P&, float) {
        auto w = std::make_shared<WheelProbe>();
        probe = w.get();
        return std::shared_ptr<HWidget>(w);
    });
    Actor a;
    P* p = a.addComponent<P>();
    p->content = "WheelProbe";
    p->slate();
    std::vector<P*> panels{ p };
    Ptr::Ray on;  on.origin = { 0.f, 0.f, 2.f }; on.dir = { 0.f, 0.f, -1.f }; on.valid = true;
    Ptr::Ray off = on; off.origin.x = 5.f;
    Ptr ptr;
    ptr.update(panels, on, { false, 1.5f });
    check(probe && std::fabs(probe->total - 1.5f) < 1e-6f, "가리키는 패널의 위젯이 휠을 받음");
    ptr.update(panels, off, { false, 2.f });
    check(std::fabs(probe->total - 1.5f) < 1e-6f, "패널을 가리키지 않으면 휠을 보내지 않음");

    // (e) 레이저 · 커서: 가리킬 때만, 누르는 중이면 흰색
    std::vector<Ptr::Segment> segs;
    ptr.appendLaser(off.origin, segs);
    check(segs.empty(), "아무것도 가리키지 않으면 레이저 없음");
    ptr.update(panels, on, {});
    ptr.appendLaser(on.origin, segs);
    check(segs.size() == 3 && glm::length(segs[0].b - glm::vec3(0.f)) < 1e-5f && segs[0].color.r < 1.f,
          "가리키면 시작점 -> 맞은 점 레이저 + 커서 십자 둘(파란색)");
    check(std::fabs(glm::length(segs[1].b - segs[1].a) - 0.015f) < 1e-5f && segs[1].a.z > 0.f,
          "커서 십자 한 변 1.5 cm, 패널 앞쪽으로 띄움");
    segs.clear();
    ptr.update(panels, on, { true, 0.f });
    ptr.appendLaser(on.origin, segs);
    check(segs.size() == 3 && segs[0].color == glm::vec4(1.f), "누르는 중이면 흰색");
    ptr.reset();
    check(!ptr.trigger.down && !ptr.capturing(), "reset 은 트리거 래치도 풂");
}

// ── 3D UI 구간 7: 머리를 따라오는 패널 ───────────────────────────────────────────
void testPanelFollow() {
    std::printf("\n-- 3D UI 구간 7: 따라오는 패널 --\n");
    using P = UIPanelComponent;
    const glm::vec3 head{ 0.f, 1.6f, 0.f };
    const glm::vec3 up{ 0.f, 1.f, 0.f };
    auto facing = [](const glm::quat& q) { return q * glm::vec3(0.f, 0.f, 1.f); };   // 패널 앞면 방향

    // (a) 목표: 머리 앞 수평 거리, 눈높이 + 높이, 앞면이 머리를 향함(요만)
    {
        const P::Pose t = P::followTarget(head, glm::quat(1.f, 0.f, 0.f, 0.f), 1.f, -0.15f, {});
        check(glm::length(t.position - glm::vec3(0.f, 1.45f, -1.f)) < 1e-5f && glm::length(facing(t.rotation) - glm::vec3(0.f, 0.f, 1.f)) < 1e-5f,
              "정면: (0, 1.45, -1), 앞면 +Z(머리 쪽)");
        const P::Pose r = P::followTarget(head, glm::angleAxis(glm::radians(90.f), up), 1.f, 0.f, {});
        check(glm::length(r.position - glm::vec3(-1.f, 1.6f, 0.f)) < 1e-5f && glm::length(facing(r.rotation) - glm::vec3(1.f, 0.f, 0.f)) < 1e-5f,
              "왼쪽(요 90도)을 보면 -X 쪽, 앞면이 +X(머리 쪽)");
        const glm::quat down60 = glm::angleAxis(glm::radians(-60.f), glm::vec3(1.f, 0.f, 0.f));
        const P::Pose d = P::followTarget(head, down60, 1.f, 0.f, {});
        check(glm::length(d.position - glm::vec3(0.f, 1.6f, -1.f)) < 1e-4f, "고개를 숙여도 수평 거리 · 높이는 그대로(기울이지 않음)");
        const glm::quat down90 = glm::angleAxis(glm::radians(-90.f), glm::vec3(1.f, 0.f, 0.f));
        const P::Pose f = P::followTarget(head, down90, 1.f, 0.f, { 1.f, 0.f, 0.f });
        check(glm::length(f.position - glm::vec3(1.f, 1.6f, 0.f)) < 1e-4f, "바로 아래를 보면 대체 방향(지금 패널 쪽)을 앞으로");
    }

    // (b) 천천히 따라오기: 처음엔 바로, 조금 돌면 그대로, 크게 돌면 움직여 닿으면 멈춤
    {
        Actor a;
        P* p = a.addComponent<P>();
        p->placement = P::Placement::FollowHead;
        p->stepFollow(head, glm::quat(1.f, 0.f, 0.f, 0.f), 1.f / 60.f);
        check(glm::length(a.worldPosition() - glm::vec3(0.f, 1.45f, -1.f)) < 1e-5f && !p->followMoving(), "처음 부르면 목표로 바로");
        const glm::vec3 before = a.worldPosition();
        p->stepFollow(head, glm::angleAxis(glm::radians(20.f), up), 1.f / 60.f);
        check(a.worldPosition() == before && !p->followMoving(), "20도 돌면 그대로(30도 안)");
        const glm::quat turned = glm::angleAxis(glm::radians(60.f), up);
        const bool moving = p->stepFollow(head, turned, 1.f / 60.f);
        const glm::vec3 step1 = a.worldPosition();
        check(moving && step1 != before && glm::length(step1 - before) < 0.2f, "60도 돌면 움직이기 시작(한 번에 뛰지 않음)");
        int frames = 1;
        while (p->stepFollow(head, turned, 1.f / 60.f) && frames < 600) ++frames;
        const P::Pose target = P::followTarget(head, turned, 1.f, -0.15f, {});
        check(!p->followMoving() && glm::length(a.worldPosition() - target.position) < 0.011f && frames > 30 && frames < 120,
              "목표 1 cm 안에 닿으면 멈춤(약 1 초, 실제 " + std::to_string(frames) + " 프레임)");
        check(glm::length(facing(a.worldRotation()) - facing(target.rotation)) < 0.02f, "움직이며 머리 쪽으로 돌아섬");
        const glm::vec3 rest = a.worldPosition();
        p->stepFollow(head + glm::vec3(0.f, 0.f, 0.2f), turned, 1.f / 60.f);
        check(a.worldPosition() == rest, "20 cm 물러서는 정도는 그대로(35 cm 안)");
        p->stepFollow(head + glm::vec3(0.f, 0.f, 0.f) - (turned * glm::vec3(0.f, 0.f, -1.f)) * 0.6f, turned, 1.f / 60.f);
        check(p->followMoving(), "60 cm 물러서면 따라오기 시작");
    }

    // (c) 직렬화 · 범위
    {
        P a;
        a.placement = P::Placement::FollowHead;
        a.followDistance = 1.5f;
        a.followHeight = 0.2f;
        ComponentFieldWriter w;
        a.writeFields(w);
        P b;
        for (const auto& f : w.fields()) b.readField(f.first, f.second);
        check(b.placement == P::Placement::FollowHead && b.followDistance == 1.5f && b.followHeight == 0.2f, "배치 · 거리 · 높이가 왕복함");
        P c;
        const bool handled = c.readField("placement", "sideways") && c.readField("followDistance", "9") && c.readField("followHeight", "-3");
        check(handled && c.placement == P::Placement::World && c.followDistance == 1.f && c.followHeight == -0.15f,
              "모르는 배치 · 범위 밖 값은 기본값 유지");
    }
}

} // namespace

static void testRoundedAndShadowPaint() {
    HGeometry geo;
    geo.position = { 100.f, 200.f };
    geo.size     = { 160.f, 40.f };

    HPaintContext ctx(nullptr);
    ctx.drawRect(geo, { 0.2f, 0.3f, 0.4f, 1.f });
    ctx.drawRoundedRect(geo, { 0.1f, 0.1f, 0.1f, 1.f }, 6.f, { 0.5f, 0.6f, 0.7f, 1.f }, 1.f);
    ctx.drawShadow(geo, 6.f, { 0.f, 4.f }, 12.f, { 0.f, 0.f, 0.f, 0.5f });

    check(ctx.elements.size() == 3, "drawRect/drawRoundedRect/drawShadow 3개 요소 생성");
    check(ctx.elements[0].type == HDrawElement::Type::Rect
          && ctx.elements[0].cornerRadius == 0.f
          && ctx.elements[0].borderWidth == 0.f
          && ctx.elements[0].shadowBlur == 0.f,
          "기본 drawRect 는 radius=0, border=0, shadowBlur=0");
    check(ctx.elements[1].type == HDrawElement::Type::Rect
          && ctx.elements[1].cornerRadius == 6.f
          && ctx.elements[1].borderWidth == 1.f
          && ctx.elements[1].borderColor == glm::vec4{ 0.5f, 0.6f, 0.7f, 1.f }
          && ctx.elements[1].shadowBlur == 0.f,
          "drawRoundedRect 는 반경과 테두리 정보를 기록");
    check(ctx.elements[2].type == HDrawElement::Type::Shadow
          && ctx.elements[2].rect == glm::vec4{ 100.f, 204.f, 160.f, 40.f }
          && ctx.elements[2].cornerRadius == 6.f
          && ctx.elements[2].shadowBlur == 12.f
          && ctx.elements[2].color == glm::vec4{ 0.f, 0.f, 0.f, 0.5f },
          "drawShadow 는 오프셋이 반영된 rect 와 블러·반경 기록");

    // SBorder: 기본은 사각형 경로, cornerRadius + shadow 지정 시 그림자 먼저 + 둥근 사각형
    auto plainBorder = HNew(SBorder)->background({ 1.f, 1.f, 1.f, 1.f });
    plainBorder->onArrangeChildren(geo);
    HPaintContext cPlain(nullptr);
    plainBorder->paint(cPlain, geo);
    check(cPlain.elements.size() == 1 && cPlain.elements[0].cornerRadius == 0.f && cPlain.elements[0].shadowBlur == 0.f,
          "기본 SBorder 는 둥근 모서리 · 그림자 없이 기존 사각형 유지");

    auto popupBorder = HNew(SBorder)
        ->background({ 0.1f, 0.1f, 0.12f, 1.f })
        ->borderColor({ 0.3f, 0.3f, 0.35f, 1.f })->borderThickness(1.f)
        ->cornerRadius(6.f)
        ->shadow({ 0.f, 4.f }, 12.f, { 0.f, 0.f, 0.f, 0.5f });
    popupBorder->onArrangeChildren(geo);
    HPaintContext cPopup(nullptr);
    popupBorder->paint(cPopup, geo);
    check(cPopup.elements.size() == 2
          && cPopup.elements[0].type == HDrawElement::Type::Shadow
          && cPopup.elements[1].type == HDrawElement::Type::Rect
          && cPopup.elements[1].cornerRadius == 6.f
          && cPopup.elements[1].borderWidth == 1.f,
          "둥근/그림자 SBorder 는 Shadow 를 먼저 그리고 둥근 사각형을 그림");

    // 위젯 Style 기본값 보존(게임 UI 무변경 보장)
    check(SButton::Style{}.cornerRadius == 0.f,      "SButton::Style 기본 cornerRadius 는 0");
    check(SToggleButton::Style{}.cornerRadius == 0.f,"SToggleButton::Style 기본 cornerRadius 는 0");
    check(STabBar::Style{}.tabCornerRadius == 0.f,   "STabBar::Style 기본 tabCornerRadius 는 0");
    check(SSearchBox::Style{}.cornerRadius == 0.f && SSearchBox::Style{}.borderWidth == 1.f,
          "SSearchBox::Style 기본값 보존");
    check(SNumberField::Style{}.cornerRadius == 0.f && SNumberField::Style{}.borderWidth == 0.f,
          "SNumberField::Style 기본값 보존");
}

// D3(2026-11-19): 에디터 아이콘. /Engine/ 마운트 · 모든 아이콘 읽기 · 배율별 래스터화 · 아틀라스 자리 · 그리기 요소 · 글자로 돌아가기.
static void testEditorIcons() {
    hvfs::reset();
    hvfs::mountDirectoryAs(HARUKA_ENGINE_CONTENT_DIR, "/Engine/", 10);
    hvfs::mountDirectory(HARUKA_ENGINE_CONTENT_DIR, "", 0);   // 접두어 없는 마운트가 /Engine/ 을 가로채지 않는지 보려고 붙임
    check(hvfs::exists("/Engine/Editor/Icons/play.svg"), "/Engine/ 논리 경로로 엔진 콘텐츠 파일을 찾음");
    check(hvfs::exists("/engine/Editor/Icons/play.svg"), "/Engine/ 접두어는 대소문자를 가리지 않음");
    check(!hvfs::exists("/Engine/Editor/Icons/nope.svg"), "없는 엔진 파일은 없음");
    check(!hvfs::exists("/Engine/../Editor/Icons/play.svg"), "/Engine/ 아래 '..' 은 거절");
    check(hvfs::toCanonical("/engine/Editor/x.svg") == "/Engine/Editor/x.svg", "/Engine/ 정식 표기로 바꿈");
    check(hvfs::isVirtualLogicalPath("/Engine/a") && !hvfs::isVirtualLogicalPath("/Enginex/a"),
          "/Engine/ 은 가상 논리 경로(절대 경로로 보지 않음)");

    EditorIcons icons;
    check(icons.load(), "에디터 아이콘을 /Engine/Editor/Icons 에서 읽음");
    check(icons.count() == EditorIcons::names().size(), "쓰는 아이콘 이름이 모두 읽힘 (" +
          std::to_string(icons.count()) + "/" + std::to_string(EditorIcons::names().size()) + ")");
    bool allDrawn = true, sizesOk = true;
    std::string empty;
    for (const char* n : EditorIcons::names()) {
        for (int px : { 16, 24, 32 }) {   // 16px 아이콘의 배율 1 · 1.5 · 2
            std::vector<uint8_t> a;
            if (!icons.rasterize(n, px, a) || a.size() != (size_t)px * px) { sizesOk = false; continue; }
            size_t covered = 0;
            for (uint8_t v : a) covered += v > 0;
            if (covered == 0 || covered * 10 > a.size() * 9) { allDrawn = false; empty += std::string(n) + "@" + std::to_string(px) + " "; }
        }
    }
    check(sizesOk, "아이콘 래스터 크기 = px x px (16 · 24 · 32)");
    check(allDrawn, "모든 아이콘이 비지 않고 꽉 차지도 않게 그려짐 (선 아이콘) " + empty);
    std::vector<uint8_t> none;
    check(!icons.has("no-such-icon") && !icons.rasterize("no-such-icon", 16, none), "모르는 이름은 없음");
    check(!icons.addSvg("broken", "<svg"), "깨진 SVG 는 등록하지 않음");

    HFontAtlas atlas;
    if (atlas.initCpuOnly()) {
        HGlyphInfo g{};
        check(!atlas.getIcon("play", 16, g), "아이콘 공급자가 없으면 아이콘 없음");
        atlas.setIconSource(&icons);
        check(atlas.hasIcon("play") && !atlas.hasIcon("no-such-icon"), "공급자를 붙이면 이름으로 찾음");
        HGlyphInfo a{}, b{}, c{};
        check(atlas.getIcon("play", 16, a) && a.pixelSize == glm::vec2(16.f, 16.f) && a.uvMax.x > a.uvMin.x,
              "아이콘이 아틀라스 자리(16 x 16)를 받음");
        check(atlas.getIcon("play", 16, b) && b.uvMin == a.uvMin && b.uvMax == a.uvMax, "같은 이름 · 크기는 캐시에서 같은 자리");
        check(atlas.getIcon("play", 32, c) && c.uvMin != a.uvMin && c.pixelSize.x == 32.f, "크기가 다르면 다른 자리");

        HPaintContext ctx(&atlas);
        HGeometry geo;
        geo.position = { 10.3f, 20.6f };
        geo.size     = { 30.f, 20.f };
        check(ctx.drawIcon(geo, "play", { 1.f, 0.f, 0.f, 1.f }) && ctx.elements.size() == 1 &&
              ctx.elements[0].type == HDrawElement::Type::Icon && ctx.elements[0].text == "play" &&
              ctx.elements[0].rect.z == 20.f && ctx.elements[0].rect.x == std::round(ctx.elements[0].rect.x),
              "drawIcon: 짧은 변 크기의 정수 위치 아이콘 요소");
        check(!ctx.drawIcon(geo, "no-such-icon", { 1.f, 1.f, 1.f, 1.f }) && ctx.elements.size() == 1,
              "모르는 아이콘은 요소를 만들지 않고 거짓(호출한 쪽이 글자로 그림)");

        // SIcon: 아이콘이 있으면 정사각 칸에 아이콘, 없으면 대신할 글자
        auto withIcon = HNew(SIcon)->icon(std::string("eye"))->size(16.f)->fallback("O", 12.f);
        auto noIcon   = HNew(SIcon)->icon(std::string("no-such-icon"))->size(16.f)->fallback("Play", 13.f);
        check(withIcon->computeDesiredSize() == glm::vec2(16.f, 16.f), "SIcon: 아이콘이 있으면 size x size");
        check(noIcon->computeDesiredSize().x > 16.f, "SIcon: 아이콘이 없으면 대신할 글자 폭");
        HPaintContext ctx2(&atlas);
        HGeometry box; box.position = { 0.f, 0.f }; box.size = { 40.f, 20.f };
        withIcon->paint(ctx2, box);
        noIcon->paint(ctx2, box);
        check(ctx2.elements.size() == 2 && ctx2.elements[0].type == HDrawElement::Type::Icon &&
              ctx2.elements[1].type == HDrawElement::Type::Text && ctx2.elements[1].text == "Play",
              "SIcon: 아이콘 요소 · 글자 요소");
        atlas.setIconSource(nullptr);
        atlas.unloadFont();
    }
    hvfs::reset();
}

// D4(2026-11-19): UTF-8 글자 · 한글 · 굵은 글꼴(Pretendard, /Engine/Fonts).
static void testUtf8AndBoldFont() {
    // UTF-8 읽기
    {
        const std::string s = "A\xED\x95\x9C\xEA\xB8\x80!";   // "A한글!"
        size_t i = 0;
        const uint32_t a = HFontAtlas::nextCodepoint(s, i);
        const uint32_t h = HFontAtlas::nextCodepoint(s, i);
        const uint32_t g = HFontAtlas::nextCodepoint(s, i);
        const uint32_t e = HFontAtlas::nextCodepoint(s, i);
        check(a == 'A' && h == 0xD55C && g == 0xAE00 && e == '!' && i == s.size(), "UTF-8: A 한 글 ! 를 코드 포인트로 읽음");
        const std::string bad = "\xFF" "a" "\xE2\x82";   // 잘못된 바이트, 끝에서 잘린 글자
        size_t j = 0;
        const uint32_t b0 = HFontAtlas::nextCodepoint(bad, j);
        const uint32_t b1 = HFontAtlas::nextCodepoint(bad, j);
        const uint32_t b2 = HFontAtlas::nextCodepoint(bad, j);
        check(b0 == '?' && b1 == 'a' && b2 == '?' && j < bad.size(), "UTF-8: 깨진 바이트는 '?' 하나씩, 멈추지 않음");
        const std::string over = "\xC0\xAF";   // 너무 긴 표현의 '/'
        size_t k = 0;
        check(HFontAtlas::nextCodepoint(over, k) == '?', "UTF-8: 너무 긴 표현은 거절");
        check(HFontAtlas::utf8Floor(s, 2) == 1 && HFontAtlas::utf8Floor(s, 4) == 4 && HFontAtlas::utf8Floor(s, 99) == s.size(),
              "UTF-8: 자를 때 글자 중간을 피함");
    }

    hvfs::reset();
    hvfs::mountDirectoryAs(HARUKA_ENGINE_CONTENT_DIR, "/Engine/", 10);
    HFontAtlas atlas;
    atlas.setAtlasHeight(1024);
    check(atlas.atlasHeight() == 1024, "아틀라스 높이를 init 전에 바꿈");
    if (!atlas.initCpuOnly("/Engine/Fonts/Pretendard-Regular.ttf")) {
        check(false, "Pretendard 보통 글꼴을 /Engine/Fonts 에서 읽음");
        hvfs::reset();
        return;
    }
    check(!atlas.hasBoldFont(), "굵은 글꼴은 따로 읽기 전에는 없음");
    const float boldBefore = atlas.measureText("TRANSFORM", 13.f, 1);
    check(boldBefore == atlas.measureText("TRANSFORM", 13.f, 0), "굵은 글꼴이 없으면 굵게도 보통 폭");
    check(atlas.loadBoldFont("/Engine/Fonts/Pretendard-SemiBold.ttf") && atlas.hasBoldFont(), "Pretendard 굵은 글꼴을 읽음");
    check(!atlas.loadBoldFont("/Engine/Fonts/none.ttf") && !atlas.hasBoldFont(), "없는 굵은 글꼴은 거짓, 보통으로 돌아감");
    atlas.loadBoldFont("/Engine/Fonts/Pretendard-SemiBold.ttf");
    check(atlas.measureText("TRANSFORM", 13.f, 1) > atlas.measureText("TRANSFORM", 13.f, 0), "굵은 글자가 보통보다 넓음");

    const std::string han = "\xEC\x94\xAC \xEC\x97\xB4\xEA\xB8\xB0";   // "씬 열기"
    const float hanW = atlas.measureText(han, 13.f);
    check(hanW > 13.f * 2.f && hanW < 13.f * 6.f, "한글 폭을 잼 (씬 열기, " + std::to_string(hanW) + " px)");
    const HGlyphInfo gh = atlas.getGlyph(0xD55C, 13.f);   // 한
    check(gh.pixelSize.x > 4.f && gh.pixelSize.y > 4.f, "한글 글리프가 아틀라스에 그려짐");
    const HGlyphInfo gb = atlas.getGlyph('A', 13.f, 1);
    const HGlyphInfo gr = atlas.getGlyph('A', 13.f, 0);
    check(gb.uvMin != gr.uvMin, "굵은 글자 · 보통 글자는 다른 아틀라스 자리");

    HPaintContext ctx(&atlas);
    HGeometry g; g.position = { 0.f, 0.f }; g.size = { 100.f, 20.f };
    ctx.drawText(g, "Bold", 13.f, { 1.f, 1.f, 1.f, 1.f }, 1);
    ctx.drawText(g, "Body", 13.f, { 1.f, 1.f, 1.f, 1.f });
    check(ctx.elements.size() == 2 && ctx.elements[0].fontFace == 1 && ctx.elements[1].fontFace == 0,
          "drawText: 굵게 · 보통 면을 요소에 남김");
    auto tb = HNew(STextBlock)->text("TRANSFORM")->fontSize(13.f);
    const float plainW = tb->computeDesiredSize().x;
    tb->bold();
    check(tb->computeDesiredSize().x > plainW, "STextBlock::bold: 굵게 바꾸면 다시 재서 더 넓음");
    atlas.unloadFont();
    hvfs::reset();
}

void testCheckBoxAndScrollStyle() {
    std::printf("testCheckBoxAndScrollStyle\n");
    bool state = false;
    SCheckBox::Style st;
    st.uncheckedBg     = { 0.01f, 0.01f, 0.01f, 1.f };
    st.uncheckedBorder = { 0.10f, 0.10f, 0.10f, 1.f };
    st.checkedBg       = { 0.02f, 0.15f, 0.67f, 1.f };
    st.hoverBorder     = { 0.07f, 0.23f, 0.81f, 1.f };
    st.checkColor      = { 1.f, 1.f, 1.f, 1.f };
    st.boxSize         = 16.f;
    st.cornerRadius    = 3.f;
    st.borderWidth     = 1.f;
    st.checkIcon       = "check";

    auto cb = HNew(SCheckBox)->style(st)
        ->checked([&]{ return state; })
        ->onClick([&]{ state = !state; });
    check(cb->computeDesiredSize() == glm::vec2(16.f, 16.f), "SCheckBox: 기본 크기 16x16");

    HGeometry box; box.position = { 10.f, 20.f }; box.size = { 16.f, 24.f };
    cb->onArrangeChildren(box);

    // 바깥에서 누르고 안에서 떼면 토글되지 않음
    cb->onMouseButton(press({ 0.f, 0.f }));
    cb->onMouseButton(release({ 18.f, 32.f }));
    check(!state, "SCheckBox: 바깥 프레스 + 안쪽 릴리즈 무시");

    // 안에서 누르고 바깥에서 떼면 토글되지 않음
    cb->onMouseButton(press({ 18.f, 32.f }));
    cb->onMouseButton(release({ 0.f, 0.f }));
    check(!state, "SCheckBox: 안쪽 프레스 + 바깥 릴리즈 무시");

    // 호버 상태에서 거짓 그리기 → 테두리 hoverBorder
    cb->onMouseMove(move({ 18.f, 32.f }, false));
    HPaintContext pNoIcon(nullptr);
    cb->paint(pNoIcon, box);
    check(pNoIcon.elements.size() == 1 &&
          pNoIcon.elements[0].cornerRadius == 3.f &&
          pNoIcon.elements[0].borderWidth == 1.f &&
          pNoIcon.elements[0].borderColor == st.hoverBorder,
          "SCheckBox: 거짓 호버 테두리 = hoverBorder, 반경 3");

    // 안쪽 프레스 + 릴리즈 → 참 전환, 아이콘 공급자 없으면 "v" 폴백
    cb->onMouseButton(press({ 18.f, 32.f }));
    cb->onMouseButton(release({ 18.f, 32.f }));
    check(state, "SCheckBox: 안쪽 프레스+릴리즈 토글");

    pNoIcon.elements.clear();
    cb->paint(pNoIcon, box);
    check(pNoIcon.elements.size() == 2 &&
          pNoIcon.elements[0].color == st.checkedBg &&
          pNoIcon.elements[1].type == HDrawElement::Type::Text &&
          pNoIcon.elements[1].text == "v",
          "SCheckBox: 참 + 아이콘 없음 → \"v\" 폴백");

    // 아이콘 공급자에 "check" 가 있으면 Icon 요소 생성
    HFontAtlas atlas;
    EditorIcons icons;
    icons.addSvg("check", R"(<svg viewBox="0 0 24 24"><path d="M20 6 9 17l-5-5"/></svg>)");
    atlas.setIconSource(&icons);
    HPaintContext pIcon(&atlas);
    cb->paint(pIcon, box);
    check(pIcon.elements.size() == 2 &&
          pIcon.elements[1].type == HDrawElement::Type::Icon &&
          pIcon.elements[1].text == "check",
          "SCheckBox: 참 + check 아이콘 → Icon 요소");
    atlas.setIconSource(nullptr);

    // SScrollBox::Style 기본/커스텀 검사
    auto sbChild = HNew(SVerticalBox);
    sbChild->addSlot(HNew(SBorder), SVerticalBox::ESizeRule::Fixed, 400.f);
    auto sb = HNew(SScrollBox)->style({ {0,0,0,0}, {0.2f,0.2f,0.2f,1.f}, {0.5f,0.5f,0.5f,1.f}, 6.f, 3.f })
        ->content(sbChild);
    HGeometry sbGeo; sbGeo.position = { 0.f, 0.f }; sbGeo.size = { 100.f, 100.f };
    sb->onArrangeChildren(sbGeo);
    sb->onMouseMove(move({ 95.f, 10.f }, false));
    HPaintContext pSb(nullptr);
    sb->paint(pSb, sbGeo);
    check(!pSb.elements.empty() &&
          pSb.elements.back().cornerRadius == 3.f &&
          pSb.elements.back().color == glm::vec4(0.5f, 0.5f, 0.5f, 1.f),
          "SScrollBox: 스타일 폭 6 · 반경 3 · 손잡이 호버 색");
}

void testTooltipAndDisabled() {
    std::printf("testTooltipAndDisabled\n");

    int disBtnClicks = 0, disTogClicks = 0, disChkClicks = 0;

    auto btnA = HNew(SButton)
        ->style({ {0.2f,0.2f,0.2f,1.f}, {0.8f,0.8f,0.8f,1.f}, {0.1f,0.1f,0.1f,1.f}, 4.f, {0.4f,0.4f,0.4f,1.f} })
        ->content(HNew(STextBlock)->text("DisBtn")->color({1.f,1.f,1.f,1.f}))
        ->onClick([&] { ++disBtnClicks; });
    btnA->enabled.set(false);
    btnA->tooltip.set("Tip A");

    auto togB = HNew(SToggleButton)
        ->style({ {0.2f,0.2f,0.2f,1.f}, {0.7f,0.7f,0.7f,1.f}, {0.9f,0.1f,0.1f,1.f}, {0.f,1.f,0.f,1.f}, 0.f, {0.4f,0.4f,0.4f,1.f} })
        ->isActive([] { return true; })
        ->content(HNew(STextBlock)->text("DisTog")->color({1.f,1.f,1.f,1.f}))
        ->onClick([&] { ++disTogClicks; });
    togB->enabled.set(false);
    togB->tooltip.set("Tip B");

    auto edgeBtn = HNew(SButton)->content(HNew(STextBlock)->text("Edge"));
    edgeBtn->tooltip.set("Edge Tooltip Long Text");

    auto root = HNew(SOverlay);
    root->addSlot(HNew(SBox)->padding(kPadA)->content(btnA))
        ->addSlot(HNew(SBox)->padding(kPadB)->content(togB))
        ->addSlot(HNew(SBox)->padding(HPadding{ 350.f, 270.f, 0.f, 0.f })->content(edgeBtn));

    HSlateContext ctx;
    ctx.setRootWidget(root);
    ctx.arrange({ kW, kH });

    // 1. 머무름: 0.3초 < 0.5초 -> 안 뜸, +0.25초 -> 뜸 (비활성 버튼에도 뜸)
    ctx.handlePointerEvent(move(kInA, false));
    ctx.tickTooltip(0.30f);
    check(!ctx.isTooltipVisible(), "툴팁: 0.30초에는 아직 안 뜸");
    ctx.tickTooltip(0.25f);
    check(ctx.isTooltipVisible(),  "툴팁: 0.55초 머무르면 비활성 단추 위에서도 뜸");

    std::vector<HDrawElement> elems;
    ctx.collectElements({ kW, kH }, nullptr, elems);
    const bool hasTipA = std::any_of(elems.begin(), elems.end(), [](const HDrawElement& e) {
        return e.type == HDrawElement::Type::Text && e.text == "Tip A";
    });
    check(hasTipA, "툴팁: collectElements 에 Tip A 포함");

    // 2. 누르면 닫히고, 벗어나서 다시 머물러야 뜸
    ctx.handlePointerEvent(press(kInA));
    ctx.handlePointerEvent(release(kInA));
    check(disBtnClicks == 0,       "비활성 SButton: 클릭 무시");
    check(!ctx.isTooltipVisible(), "툴팁: 누르면 즉시 닫힘");
    ctx.tickTooltip(0.60f);
    check(!ctx.isTooltipVisible(), "툴팁: 누른 뒤 벗어나지 않으면 다시 안 뜸");
    ctx.handlePointerEvent(move(kEmpty, false));
    ctx.tickTooltip(0.60f);
    check(!ctx.isTooltipVisible(), "툴팁: 빈 곳에서는 안 뜸");
    ctx.handlePointerEvent(move(kInA, false));
    ctx.tickTooltip(0.55f);
    check(ctx.isTooltipVisible(),  "툴팁: 다시 들어와 0.55초 머무르면 뜸");

    // 3. 위젯 바뀜 -> 타이머 리셋
    ctx.handlePointerEvent(move(kInB, false));
    check(!ctx.isTooltipVisible(), "툴팁: 다른 위젯으로 옮기면 닫힘");
    ctx.tickTooltip(0.30f);
    check(!ctx.isTooltipVisible(), "툴팁: 위젯 바뀐 뒤 0.30초에는 아직 안 뜸");
    ctx.tickTooltip(0.25f);
    check(ctx.isTooltipVisible(),  "툴팁: 위젯 바뀐 뒤 0.55초에 Tip B 뜸");

    // 4. 화면 가장자리 위치 조정
    ctx.handlePointerEvent(move({ 390.f, 290.f }, false));
    ctx.tickTooltip(0.55f);
    elems.clear();
    ctx.collectElements({ kW, kH }, nullptr, elems);
    bool edgeInside = false;
    for (const auto& e : elems) {
        if (e.type == HDrawElement::Type::Rect && e.layer >= 10) {
            edgeInside = (e.rect.x >= 0.f && e.rect.y >= 0.f &&
                          e.rect.x + e.rect.z <= kW && e.rect.y + e.rect.w <= kH);
        }
    }
    check(edgeInside, "툴팁: 화면 오른쪽 아래 가장자리에서 안쪽으로 조정");

    // 5. 비활성 모양 검사 (SButton · SToggleButton · SCheckBox)
    HPaintContext pBtn(nullptr);
    btnA->paint(pBtn, btnA->cachedGeometry());
    check(pBtn.elements.size() >= 2 &&
          pBtn.elements[0].color == glm::vec4(0.2f, 0.2f, 0.2f, 1.f) &&
          pBtn.elements[1].color == glm::vec4(0.4f, 0.4f, 0.4f, 1.f),
          "SButton 비활성: 기본 바탕 + disabledContent 글자 색 (호버 색 미사용)");

    ctx.handlePointerEvent(press(kInB));
    ctx.handlePointerEvent(release(kInB));
    check(disTogClicks == 0, "비활성 SToggleButton: 클릭 무시");
    HPaintContext pTog(nullptr);
    togB->paint(pTog, togB->cachedGeometry());
    check(pTog.elements.size() == 2 &&
          pTog.elements[0].color == glm::vec4(0.2f, 0.2f, 0.2f, 1.f) &&
          pTog.elements[1].color == glm::vec4(0.4f, 0.4f, 0.4f, 1.f),
          "SToggleButton 비활성: 기본 바탕 + activeBar 미출력 + disabledContent 글자 색");

    auto chk = HNew(SCheckBox)
        ->style({ {0.1f,0.1f,0.1f,1.f}, {0.3f,0.3f,0.3f,1.f}, {0.2f,0.5f,0.9f,1.f},
                  {0.5f,0.7f,1.0f,1.f}, {1.f,1.f,1.f,1.f}, 16.f, 3.f, 1.f, "", {0.4f,0.4f,0.4f,1.f} })
        ->checked([] { return true; })
        ->onClick([&] { ++disChkClicks; });
    chk->enabled.set(false);
    HGeometry chkGeo; chkGeo.position = { 0.f, 0.f }; chkGeo.size = { 16.f, 16.f };
    chk->onArrangeChildren(chkGeo);
    chk->onMouseMove(move({ 8.f, 8.f }, false));
    chk->onMouseButton(press({ 8.f, 8.f }));
    chk->onMouseButton(release({ 8.f, 8.f }));
    check(disChkClicks == 0, "비활성 SCheckBox: 클릭 무시");
    HPaintContext pChk(nullptr);
    chk->paint(pChk, chkGeo);
    check(pChk.elements.size() == 2 &&
          pChk.elements[0].color == glm::vec4(0.1f, 0.1f, 0.1f, 1.f) &&
          pChk.elements[0].borderColor == glm::vec4(0.3f, 0.3f, 0.3f, 1.f) &&
          pChk.elements[1].color == glm::vec4(0.4f, 0.4f, 0.4f, 1.f),
          "SCheckBox 비활성: 기본 바탕 + 기본 테두리 + disabledColor 체크 색");
}

// 글자 체계 T1-7 · T1-9(2026-11-19): 편집 칸의 x ↔ 커서, 커서 · 선택 · 조합 밑줄 그리기, 누르기 · 끌기로 커서 · 선택.
static void testEditableText() {
    hvfs::reset();
    hvfs::mountDirectoryAs(HARUKA_ENGINE_CONTENT_DIR, "/Engine/", 10);
    HFontAtlas atlas;
    if (!atlas.initCpuOnly("/Engine/Fonts/Pretendard-Regular.ttf")) {
        check(false, "편집 칸: Pretendard 를 읽음");
        hvfs::reset();
        return;
    }
    const std::string han = "\xED\x95\x9C";   // 한
    HTextEditState st;
    st.setText("ab" + han + "cd");
    const float fs = 14.f;
    const float xAb  = atlas.measureText("ab", fs);
    const float xHan = atlas.measureText("ab" + han, fs);
    check(st.caretFromX(&atlas, fs, 0, 0.f) == 0 && st.caretFromX(&atlas, fs, 0, 999.f) == st.text.size(),
          "caretFromX: 왼쪽 끝 · 오른쪽 끝");
    check(st.caretFromX(&atlas, fs, 0, xAb + (xHan - xAb) * 0.3f) == 2, "caretFromX: 한글 앞쪽 30% 는 글자 앞");
    check(st.caretFromX(&atlas, fs, 0, xAb + (xHan - xAb) * 0.7f) == 2 + han.size(), "caretFromX: 한글 뒤쪽 70% 는 글자 뒤");
    check(HTextEditState::xOf(&atlas, st.text, fs, 0, 2 + han.size()) == xHan, "xOf: 그리기 · 폭 재기와 같은 값");

    // 그리기: 포커스 + 선택(한) → 선택 사각형 폭 = 한글 폭, 커서 막대 하나, 글 하나
    st.setCaret(2);
    st.setCaret(2 + han.size(), true);
    HPaintContext ctx(&atlas);
    HGeometry g; g.position = { 100.f, 50.f }; g.size = { 200.f, 20.f };
    SEditableText::Style es;
    es.caretWidth = 2.f;
    SEditableText::paintState(ctx, g, &st, true, "", fs, 18.f, es);
    int rects = 0, texts = 0;
    float selW = -1.f, caretX = -1.f;
    for (const auto& e : ctx.elements) {
        if (e.type == HDrawElement::Type::Text) ++texts;
        if (e.type == HDrawElement::Type::Rect) {
            ++rects;
            if (e.color == es.selection) selW = e.rect.z;
            if (e.color == es.caret)     caretX = e.rect.x;
        }
    }
    check(texts == 1 && rects == 2, "paintState: 선택 · 글 · 커서(요소 " + std::to_string(ctx.elements.size()) + ")");
    check(std::abs(selW - (xHan - xAb)) < 0.01f, "paintState: 선택 폭 = 한글 한 글자 폭");
    check(std::abs(caretX - (100.f + xHan)) < 0.01f, "paintState: 커서는 선택 끝(한 뒤)");
    check(st.caretRect.x == caretX && st.caretRect.z == 2.f, "paintState: 커서 사각형을 상태에 남김(IME 자리)");

    // 포커스 없음: 커서 · 선택 없이 글만
    HPaintContext ctx2(&atlas);
    SEditableText::paintState(ctx2, g, &st, false, "", fs, 18.f, es);
    check(ctx2.elements.size() == 1 && ctx2.elements[0].type == HDrawElement::Type::Text, "포커스 없음: 글만");

    // 조합 글: 밑줄 하나(조합 글 폭), 선택 사각형 없음
    st.setText("ab");
    st.setComposition(han, han.size());
    HPaintContext ctx3(&atlas);
    SEditableText::paintState(ctx3, g, &st, true, "", fs, 18.f, es);
    float underW = -1.f;
    for (const auto& e : ctx3.elements)
        if (e.type == HDrawElement::Type::Rect && e.color == es.composition && e.rect.w <= 2.f) underW = e.rect.z;
    check(std::abs(underW - (xHan - xAb)) < 0.01f, "조합 글: 밑줄 폭 = 조합 글 폭");
    check(!ctx3.elements.empty() && ctx3.elements[0].text == "ab" + han, "조합 글: 커서 자리에 끼워 그림");

    // 긴 글: 커서가 칸 밖이면 글을 왼쪽으로 밀어 커서가 칸 안에 보임
    HTextEditState lng;
    lng.setText(std::string(60, 'W'));
    HPaintContext ctx4(&atlas);
    SEditableText::paintState(ctx4, g, &lng, true, "", fs, 18.f, es);
    float cx = -1.f;
    for (const auto& e : ctx4.elements) if (e.type == HDrawElement::Type::Rect && e.color == es.caret) cx = e.rect.x;
    check(cx >= g.position.x && cx + es.caretWidth <= g.position.x + g.size.x + 0.01f, "긴 글: 커서가 칸 안");

    // 검색 칸 위젯: 누르면 포커스를 얻고 그 자리에 커서, 끌면 선택
    HTextEditState sst;
    sst.setText("hello world");
    bool foc = false;
    int edited = 0;
    auto box = HNew(SSearchBox)->edit(&sst)->focused([&] { return foc; })->onClick([&] { foc = true; sst.selectAll(); })
                   ->onEdited([&] { ++edited; })->icon(false)->fontSize(10.f);
    HGeometry bg; bg.position = { 0.f, 0.f }; bg.size = { 300.f, 24.f };
    box->onArrangeChildren(bg);
    HPaintContext pc(&atlas);
    box->paint(pc, bg);   // 글 칸 자리를 정함
    const float textX = 6.f;   // icon(false) 의 글 시작
    const float xHello = atlas.measureText("hello", 10.f);
    HPointerEvent down;
    down.button = 0; down.down = true; down.position = { textX + xHello + 0.5f, 12.f };
    box->onMouseButton(down);
    check(foc && !sst.hasSelection() && sst.caret == 5, "검색 칸 누르기: 포커스 + 그 자리 커서(전체 선택 대신)");
    HPointerEvent mv = down;
    mv.button = -1; mv.position.x = textX + 1.f;
    box->onMouseMove(mv);
    check(sst.selectedText() == "hello", "검색 칸 끌기: 선택");
    HPointerEvent up = down;
    up.down = false;
    box->onMouseButton(up);
    HPointerEvent sh = down;
    sh.shift = true; sh.position.x = textX + atlas.measureText("hello wor", 10.f) + 0.5f;
    box->onMouseButton(sh);
    check(sst.selBegin() == 5 && sst.selEnd() == 9 && edited >= 3, "Shift+누르기: 커서에서 선택을 늘림");
    // T3-6: 표시 글 위젯은 문화권을 바꾸면 다시 재서 크기가 바뀜
    htext::clearTables();
    htext::loadPo("msgctxt \"T.Slate,Hello\"\nmsgid \"Hi\"\nmsgstr \"\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94\"\n",   // 안녕하세요
                  "ko", htext::Layer::Engine);
    htext::setCulture("en");
    auto tb = HNew(STextBlock)->text(HTEXT("T.Slate", "Hello", "Hi"))->fontSize(14.f);
    const float wEn = tb->computeDesiredSize().x;
    htext::setCulture("ko");
    const float wKo = tb->computeDesiredSize().x;
    check(wKo > wEn * 2.f, "STextBlock(HText): 문화권을 바꾸면 다시 잼 (en " + std::to_string(wEn) + " → ko " + std::to_string(wKo) + ")");
    auto tipW = HNew(STextBlock)->text("x");
    tipW->setTooltip(HTEXT("T.Slate", "Hello", "Hi"));
    check(tipW->tooltip.get() == "\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94", "setTooltip(HText): 지금 문화권 글");
    htext::setCulture("en");
    check(tipW->tooltip.get() == "Hi", "setTooltip(HText): 문화권을 따라 바뀜");
    htext::clearTables();
    atlas.unloadFont();
    hvfs::reset();
}

// 글자 체계 T4(2026-11-19): 대체 글꼴 사슬 · 없는 글자 · 아틀라스 페이지.
static void testFontFallbackAndPages() {
    hvfs::reset();
    hvfs::mountDirectoryAs(HARUKA_ENGINE_CONTENT_DIR, "/Engine/", 10);
    {
        HFontAtlas atlas;
        atlas.setAtlasHeight(1024);
        if (!atlas.initCpuOnly("/Engine/Fonts/Pretendard-Regular.ttf")) {
            check(false, "대체 글꼴: Pretendard 를 읽음");
        } else {
            const std::string kanji = "\xE6\xBC\xA2\xE5\xAD\x97";        // 漢字
            const std::string kana  = "\xE3\x81\x8B\xE3\x81\xAA";        // かな
            const std::string zh    = "\xE8\xBF\x99\xE4\xB8\xAA";        // 这个(간체)
            const size_t fb = atlas.fallbackCount();
            std::printf("    (fallback fonts on this machine: %zu)\n", fb);
            if (fb > 0) {
                const HGlyphInfo gk = atlas.getGlyph(0x6F22, 16.f);   // 漢
                const HGlyphInfo ga = atlas.getGlyph(0x304B, 16.f);   // か
                check(gk.pixelSize.x > 4.f && ga.pixelSize.x > 4.f, "대체 글꼴: 한자 · 가나 글리프를 그림");
                check(atlas.measureText(kanji + kana + zh, 16.f) > 16.f * 4.f, "대체 글꼴: 폭을 잼");
                check(atlas.missingGlyphCount() == 0, "대체 글꼴: 없는 글자 없음");
                // 폭 재기와 그리기가 같은 글꼴: advance 가 같음
                check(atlas.glyphAdvance(0x6F22, 16.f) == gk.advance, "대체 글꼴: 폭 재기 · 그리기가 같은 글꼴");
                check(atlas.getGlyph('A', 16.f).advance == atlas.glyphAdvance('A', 16.f), "주 글꼴 글자는 그대로");
            } else {
                check(true, "대체 글꼴 없는 기계(시험 일부 건너뜀)");
            }
        }
        atlas.unloadFont();
    }
    {
        // 대체 글꼴이 하나도 없으면: 멈추지 않고 .notdef 를 그리며 경고는 글자마다 한 번
        HFontAtlas atlas;
        atlas.setFallbackChain({});
        if (atlas.initCpuOnly("/Engine/Fonts/Pretendard-Regular.ttf")) {
            check(atlas.fallbackCount() == 0, "빈 사슬: 대체 글꼴 0");
            const HGlyphInfo g = atlas.getGlyph(0x6F22, 16.f);
            check(g.advance > 0.f && atlas.missingGlyphCount() == 1, "빈 사슬: 없는 글자는 자리를 차지하고 한 번 셈");
            atlas.getGlyph(0x6F22, 20.f);
            atlas.measureText("\xE6\xBC\xA2", 16.f);
            check(atlas.missingGlyphCount() == 1, "같은 글자는 다시 세지 않음");
            atlas.setFallbackChain({ "/Engine/Fonts/none.ttf", "/Engine/Fonts/Pretendard-SemiBold.ttf" });
            check(atlas.fallbackCount() == 1, "사슬: 없는 파일은 건너뜀");
        }
        atlas.unloadFont();
    }
    {
        // 페이지: 작은 아틀라스(2048 x 64)에 글자를 많이 넣으면 페이지를 더 엶
        HFontAtlas atlas;
        atlas.setAtlasHeight(64);
        if (atlas.initCpuOnly("/Engine/Fonts/Pretendard-Regular.ttf")) {
            int maxPage = 0;
            bool allDrawn = true;
            for (int c = 0xAC00; c < 0xAC00 + 300; ++c) {   // 한글 300자, 40 px
                const HGlyphInfo g = atlas.getGlyph((uint32_t)c, 40.f);
                maxPage = std::max(maxPage, g.page);
                allDrawn = allDrawn && g.pixelSize.x > 0.f;
            }
            check(atlas.pageCount() > 1 && maxPage == atlas.pageCount() - 1, "페이지: 차면 새 페이지(" + std::to_string(atlas.pageCount()) + ")");
            check(allDrawn && atlas.droppedGlyphs() == 0, "페이지: 넘친 글자 없이 모두 그림");
            check(atlas.getGlyph(0xAC00, 40.f).page == 0, "페이지: 먼저 넣은 글자는 첫 페이지 그대로");
            for (int c = 0xB000; c < 0xB000 + 2000 && atlas.droppedGlyphs() == 0; ++c) atlas.getGlyph((uint32_t)c, 40.f);
            check(atlas.pageCount() == HFontAtlas::kMaxPages && atlas.droppedGlyphs() > 0, "페이지: 최대 수를 넘으면 그때 넘침");
        }
        atlas.unloadFont();
    }
    {
        // T4-5: 패널 전용 렌더러와 같은 설정(기본 높이 512, /Engine 의 Pretendard)으로 한글 패널 글을 그림
        HFontAtlas panel;
        if (panel.initCpuOnly("/Engine/Fonts/Pretendard-Regular.ttf")) {
            check(panel.fontPath() == "/Engine/Fonts/Pretendard-Regular.ttf", "패널 글꼴: /Engine 의 Pretendard 를 읽음");
            const std::string title = "\xED\x95\x9C\xEA\xB8\x80 \xED\x8C\xA8\xEB\x84\x90";   // 한글 패널
            HSlateContext pctx;
            pctx.setRootWidget(HNew(STextBlock)->text(title)->fontSize(56.f));
            HSlateDrawList dl;
            const bool drew = pctx.buildDrawList({ 1024.f, 256.f }, panel, VK_NULL_HANDLE, 4096, dl);
            int glyphs = 0;
            for (size_t i = 0; i < title.size();) {
                const uint32_t cp = HFontAtlas::nextCodepoint(title, i);
                if (cp == ' ') continue;
                if (panel.getGlyph(cp, 56.f).pixelSize.x > 8.f) ++glyphs;
            }
            check(drew && glyphs == 4 && panel.missingGlyphCount() == 0, "패널: 한글 네 글자를 글리프로 그림");
        }
        panel.unloadFont();
    }
    hvfs::reset();
}

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
    testDrawListFixed();
    testMultiContextCapture();
    testSlotVertexOffset();
    testLayerSortStability();
    testUIPanelComponent();
    testPanelAutoRedraw();
    testPanelPointer();
    testPanelPointerXr();
    testPanelFollow();
    testRoundedAndShadowPaint();
    testEditorIcons();
    testUtf8AndBoldFont();
    testCheckBoxAndScrollStyle();
    testTooltipAndDisabled();
    testEditableText();
    testFontFallbackAndPages();

    if (g_failed) {
        std::printf("\n%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
