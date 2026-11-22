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
#include "../src/editor/dock/SDockOverlay.hpp"
#include "../src/vfs/Vfs.hpp"
#include "../src/vfs/LogicalPath.hpp"
#include "../src/renderer/WindowTargetLogic.hpp"
#include "../src/editor/EditorDock.hpp"
#include "../src/editor/EditorFloatWindows.hpp"
#include "../src/editor/EditorWindowInput.hpp"
#include "../src/editor/EditorContextMenu.hpp"
#include "../src/ui/slate/widgets/SColorPicker.hpp"
#include "../src/platform/IWindow.hpp"
#include <fstream>
#include <sstream>
#include <map>

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

// 보조 OS 창 출력의 순수 판정(다중 OS 창 M2, src/renderer/WindowTargetLogic.hpp)과 슬롯 정점 예산.
void testWindowTargetLogic() {
    std::printf("\n-- M2: 보조 창 판정 · 슬롯 정점 예산 --\n");
    using hwin::FramePlan;
    check(hwin::planFrame(false, true, true, 1, 2) == FramePlan::SkipNoArea, "최소화면 해시 · 강제와 상관없이 건너뜀");
    check(hwin::planFrame(true, true, false, 7, 7) == FramePlan::SkipUnchanged, "해시가 같고 그린 적 있으면 건너뜀");
    check(hwin::planFrame(true, true, false, 7, 8) == FramePlan::Draw, "해시가 다르면 그림");
    check(hwin::planFrame(true, false, false, 7, 7) == FramePlan::Draw, "처음(새 스왑체인)은 해시가 같아도 그림");
    check(hwin::planFrame(true, true, true, 7, 7) == FramePlan::Draw, "강제(다시 만듦 · 표시 실패)면 해시가 같아도 그림");

    // 메인 창 최소화 프레임(M3a): 보조 창만 그리는 프레임은 메인 넓이 없음 · XR 아님 · 그릴 수 있는 보조 창이 있을 때만.
    using hwin::DesktopFrame;
    check(hwin::chooseDesktopFrame(true, false, false) == DesktopFrame::Full &&
          hwin::chooseDesktopFrame(true, false, true) == DesktopFrame::Full, "메인이 보이면 예전 경로(보조 창은 함께)");
    check(hwin::chooseDesktopFrame(false, true, true) == DesktopFrame::Full &&
          hwin::chooseDesktopFrame(false, true, false) == DesktopFrame::Full, "메인 최소화 + XR 프레임은 예전 경로(눈 + 보조 창)");
    check(hwin::chooseDesktopFrame(false, false, true) == DesktopFrame::AuxOnly, "메인 최소화 · XR 아님 · 보조 창 있음 → 보조 창만");
    check(hwin::chooseDesktopFrame(false, false, false) == DesktopFrame::Nothing,
          "보조 창이 없거나 모두 최소화면 예전과 같이 아무것도 안 함(DEF-106)");

    const hwin::AcquireOutcome ok  = hwin::classifyAcquire(VK_SUCCESS);
    const hwin::AcquireOutcome sub = hwin::classifyAcquire(VK_SUBOPTIMAL_KHR);
    const hwin::AcquireOutcome nr  = hwin::classifyAcquire(VK_NOT_READY);
    const hwin::AcquireOutcome to  = hwin::classifyAcquire(VK_TIMEOUT);
    const hwin::AcquireOutcome ood = hwin::classifyAcquire(VK_ERROR_OUT_OF_DATE_KHR);
    const hwin::AcquireOutcome lost = hwin::classifyAcquire(VK_ERROR_SURFACE_LOST_KHR);
    check(ok.acquired && !ok.recreate && !ok.notReady && !ok.failed, "acquire 성공: 받음");
    check(sub.acquired && sub.recreate, "SUBOPTIMAL: 받고 그린 뒤 다시 만듦");
    check(!nr.acquired && nr.notReady && !to.acquired && to.notReady, "타임아웃 0: NOT_READY · TIMEOUT 은 받지 않음(세마포어 신호 없음)");
    check(!ood.acquired && ood.recreate && !ood.failed, "OUT_OF_DATE: 받지 않고 그 창만 다시 만듦");
    check(!lost.acquired && lost.failed, "표면 잃음: 그 창만 멈춤");

    const hwin::PresentOutcome pOk  = hwin::classifyPresent(VK_SUCCESS);
    const hwin::PresentOutcome pSub = hwin::classifyPresent(VK_SUBOPTIMAL_KHR);
    const hwin::PresentOutcome pOod = hwin::classifyPresent(VK_ERROR_OUT_OF_DATE_KHR);
    check(pOk.shown && !pOk.recreate && pSub.shown && pSub.recreate && !pOod.shown && pOod.recreate && !pOod.failed,
          "표시: SUBOPTIMAL 은 보이고 다시 만듦, OUT_OF_DATE 는 다시 만들기만");

    // 대기 · 신호 목록: 받은 창만 들어가고 순서는 메인 → 보조.
    VkSemaphore fake[6];
    for (int i = 0; i < 6; ++i) fake[i] = reinterpret_cast<VkSemaphore>((uintptr_t)(0x100 + i));
    hwin::SubmitSemaphores sem;
    sem.add(true,  fake[0], fake[1]);   // 메인
    sem.add(false, fake[2], fake[3]);   // 받지 못한 보조 창
    sem.add(true,  fake[4], fake[5]);   // 받은 보조 창
    check(sem.wait.size() == 2 && sem.stages.size() == 2 && sem.signal.size() == 2, "받지 못한 창은 대기 · 신호 목록에 없음");
    check(sem.wait[0] == fake[0] && sem.wait[1] == fake[4] && sem.signal[0] == fake[1] && sem.signal[1] == fake[5],
          "목록 순서는 메인 → 받은 보조 창");
    check(sem.stages[0] == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, "대기 단계는 COLOR_ATTACHMENT_OUTPUT");
    sem.clear();
    sem.add(false, fake[0], fake[1]);
    check(sem.wait.empty() && sem.signal.empty(), "메인 창이 최소화된 XR 프레임에 보조 창도 없으면 빈 목록(예전과 같음)");

    // 슬롯 정점 예산: 목록 하나의 상한과 슬롯 버퍼 크기는 따로입니다.
    const uint32_t L = HSlateRenderer::maxVerts();
    check(HSlateRenderer::fitsSlot(0, L, L) && !HSlateRenderer::fitsSlot(1, L, L), "슬롯이 목록 상한과 같으면 꽉 찬 목록 하나만");
    check(HSlateRenderer::fitsSlot(L, L, 2 * L) && !HSlateRenderer::fitsSlot(L + 1, L, 2 * L),
          "슬롯을 두 배로 두면 꽉 찬 목록 둘까지(메인 + 보조 창)");
    check(!HSlateRenderer::fitsSlot(0xFFFFFFF0u, 0x20u, 0xFFFFFFFFu), "더하기가 넘쳐도 들어간다고 하지 않음");
    HSlateRenderer ui;
    check(ui.slotVertexCapacity() == L, "기본 슬롯 크기는 목록 상한(게임 실행 파일의 패널 UI 는 그대로)");
    ui.setSlotVertexCapacity(3 * L);
    check(ui.slotVertexCapacity() == 3 * L, "init 전에는 슬롯 크기를 바꿀 수 있음");
    ui.setSlotVertexCapacity(16);
    check(ui.slotVertexCapacity() == L, "목록 상한보다 작게는 두지 않음(꽉 찬 목록 하나는 언제나 들어감)");
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

// ── H1a-1: 히트 경로 · 키보드 이벤트 · 포커스 ─────────────────────────────────
// 여러 창을 지원하려면 "어느 위젯이 이 입력을 받는가" 를 위젯 트리가 정해야 합니다. 이 묶음은
// 그 기반(forEachChild · 히트 경로 · 키 버블 · 포커스 위젯)을 화면 없이 검산합니다.
std::vector<std::string> g_focusLog;

// 자식 하나를 가질 수 있는 탐침입니다. 받은 키 · 글자 · 포커스 알림을 이름과 함께 g_focusLog 에 적습니다.
struct FocusProbe : HWidget {
    std::string              name;
    bool                     focusable  = false;
    bool                     handleKeys = false;
    bool                     selfHit    = true;
    std::shared_ptr<HWidget> child;

    explicit FocusProbe(std::string n, bool f = false) : name(std::move(n)), focusable(f) {}

    glm::vec2 computeDesiredSize() override { return child ? child->computeDesiredSize() : glm::vec2(10.f); }
    void onArrangeChildren(const HGeometry& g) override {
        cachedGeo_ = g;
        if (child) child->onArrangeChildren(g);
    }
    void paint(HPaintContext&, const HGeometry&) override {}
    void forEachChild(const std::function<void(const std::shared_ptr<HWidget>&)>& fn) override {
        if (child) fn(child);
    }
    bool hitTestVisible() const override { return selfHit; }
    bool supportsKeyboardFocus() const override { return focusable; }
    HReply onKeyDown(const HKeyEvent&) override {
        g_focusLog.push_back(name + ":down");
        return handleKeys ? HReply::Handled() : HReply::Unhandled();
    }
    HReply onKeyUp(const HKeyEvent&) override {
        g_focusLog.push_back(name + ":up");
        return handleKeys ? HReply::Handled() : HReply::Unhandled();
    }
    HReply onChar(const HCharEvent& e) override {
        g_focusLog.push_back(name + ":char" + std::to_string(e.codepoint));
        return HReply::Unhandled();
    }
    void onFocusReceived() override { g_focusLog.push_back(name + ":recv"); }
    void onFocusLost()     override { g_focusLog.push_back(name + ":lost"); }
};

std::shared_ptr<FocusProbe> mkProbe(const std::string& n, bool focusable = false) {
    return std::make_shared<FocusProbe>(n, focusable);
}

std::string joinLog() {
    std::string s;
    for (const auto& l : g_focusLog) s += (s.empty() ? "" : " ") + l;
    return s;
}

HKeyEvent keyDown(int k) { HKeyEvent e; e.key = k; e.down = true;  return e; }
HKeyEvent keyUp(int k)   { HKeyEvent e; e.key = k; e.down = false; return e; }

void testHitPath() {
    std::printf("\n-- H1a-1: 히트 경로 --\n");
    // 루트 SOverlay → SBorder(A 자리) → SBox → 탐침
    auto leaf   = mkProbe("leaf");
    auto box    = HNew(SBox)->content(leaf);
    auto border = HNew(SBorder)->content(box);
    auto root   = HNew(SOverlay);
    root->addSlot(border, EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(root);
    HSlateContext ctx;
    ctx.setRootWidget(root);

    HWidgetPath p = ctx.hitTest(kInA);
    check(p.size() == 4 && p.root() == root, "경로가 루트에서 시작하고 길이 4(루트 · 테두리 · 상자 · 탐침)");
    check(p.lastWidget() == leaf && p.widgetAt(1) == border && p.widgetAt(2) == box, "가장 깊은 위젯이 경로 끝, 중간은 부모 순서");
    check(p.entries.back().geometry.position == glm::vec2(10.f, 10.f) &&
          p.entries.back().geometry.size == glm::vec2(100.f, 20.f), "경로의 geometry = 직전 arrange 의 cachedGeometry");
    check(p.contains(box.get()) && !p.contains(nullptr), "contains 가 경로 안의 위젯을 찾음");

    p = ctx.hitTest(kEmpty);
    check(p.size() == 1 && p.lastWidget() == root, "빈 곳은 루트 하나");
    p = ctx.hitTest({ -5.f, -5.f });
    check(p.empty(), "루트 밖은 빈 경로");

    root->visibility = EVisibility::Hidden;
    check(ctx.hitTest(kInA).empty(), "루트가 보이지 않으면 빈 경로");
    root->visibility = EVisibility::Visible;

    // 경로는 약한 참조라 트리가 사라지면 위젯을 붙잡지 않음
    p = ctx.hitTest(kInA);
    std::weak_ptr<HWidget> leafWeak = leaf;
    ctx.setRootWidget(nullptr);
    root.reset(); border.reset(); box.reset(); leaf.reset();
    check(leafWeak.expired() && p.lastWidget() == nullptr, "경로는 위젯을 붙잡지 않음(약한 참조)");
}

void testHitOrderAndVisibility() {
    std::printf("\n-- H1a-1: 겹침 · 보이기 · 통과 --\n");
    auto bottom = mkProbe("bottom");
    auto top    = mkProbe("top");
    auto root   = HNew(SOverlay);
    root->addSlot(bottom, EHAlign::Fill, EVAlign::Fill, kPadA)
        ->addSlot(top,    EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(root);
    HSlateContext ctx;
    ctx.setRootWidget(root);

    check(ctx.hitTest(kInA).lastWidget() == top, "SOverlay 는 마지막(위) 슬롯이 이김");
    top->visibility = EVisibility::Hidden;
    check(ctx.hitTest(kInA).lastWidget() == bottom, "Hidden 위젯은 걸리지 않고 아래 슬롯이 걸림");
    top->visibility = EVisibility::Collapsed;
    check(ctx.hitTest(kInA).lastWidget() == bottom, "Collapsed 위젯도 걸리지 않음");
    top->visibility = EVisibility::Visible;
    top->selfHit = false;
    check(ctx.hitTest(kInA).lastWidget() == bottom, "hitTestVisible() 거짓이면 아래 위젯이 걸림");

    // 통과하는 위젯의 자식은 그대로 걸림
    auto inner = mkProbe("inner");
    top->child = inner;
    arrange(root);
    HWidgetPath p = ctx.hitTest(kInA);
    check(p.lastWidget() == inner && p.contains(top.get()), "통과하는 위젯의 자식은 걸리고 경로에 부모가 남음");

    // 보이지 않는 부모 아래의 자식은 걸리지 않음
    auto hiddenParent = HNew(SBorder)->content(mkProbe("under"));
    hiddenParent->visibility = EVisibility::Hidden;
    auto root2 = HNew(SOverlay);
    root2->addSlot(bottom, EHAlign::Fill, EVAlign::Fill, kPadA)
         ->addSlot(hiddenParent, EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(root2);
    ctx.setRootWidget(root2);
    check(ctx.hitTest(kInA).lastWidget() == bottom, "Hidden 부모 아래의 자식도 건너뜀");

    // 접힌 섹션의 body 는 순회 · 판정에서 빠짐
    auto bodyLeaf = mkProbe("body");
    auto section  = HNew(SCollapseSection)->body(bodyLeaf)->headerHeight(10.f);
    auto root3    = HNew(SOverlay);
    root3->addSlot(section, EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(root3);
    ctx.setRootWidget(root3);
    check(ctx.hitTest({ 50.f, 25.f }).lastWidget() == bodyLeaf, "펼친 섹션의 body 가 걸림");
    section->expanded(false);
    arrange(root3);
    check(ctx.hitTest({ 50.f, 25.f }).lastWidget() == section, "접힌 섹션의 body 는 걸리지 않음");

    // 드롭 존 오버레이는 입력을 통과시킴
    auto dock = std::make_shared<SDockOverlay>();
    check(!dock->hitTestVisible(), "SDockOverlay 는 히트 테스트에서 빠짐");
}

void testHitClip() {
    std::printf("\n-- H1a-1: 잘린 자식은 걸리지 않음 --\n");
    ScrollRig rig(10);   // 스크롤 상자 영역 = [0,400) x [50,150), 행 30 px
    HSlateContext ctx;
    ctx.setRootWidget(rig.root);

    check(ctx.hitTest({ 60.f, 145.f }).lastWidget() == rig.rows[3], "영역 안의 행은 걸림(넷째 행)");
    check(rig.rows[4]->cachedGeometry().contains({ 60.f, 175.f }) &&
          !ctx.hitTest({ 60.f, 175.f }).contains(rig.rows[4].get()), "영역 아래로 잘린 다섯째 행은 걸리지 않음");
    check(!ctx.hitTest({ 60.f, 175.f }).contains(rig.scroll.get()), "영역 밖에서는 스크롤 상자도 경로에 없음");

    rig.scroll->setScrollOffset(30.f);
    arrange(rig.root);
    check(rig.rows[0]->cachedGeometry().contains({ 60.f, 40.f }) &&
          ctx.hitTest({ 60.f, 40.f }).lastWidget() == rig.header, "위로 밀려 잘린 첫 행 자리에서는 머리가 걸림");
    check(ctx.hitTest({ 60.f, 55.f }).lastWidget() == rig.rows[1], "스크롤 뒤 영역 맨 위에 보이는 행이 걸림");

    // clipChildren 을 켠 SBorder 도 같은 기준
    auto leaf = mkProbe("leaf");
    auto inner = HNew(SBox)->content(leaf);
    auto clipper = HNew(SBorder)->clipChildren(true)->content(inner);
    auto root = HNew(SOverlay);
    root->addSlot(clipper, EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(root);
    // 자식을 일부러 영역 밖까지 넓혀 배치합니다(넘친 내용).
    HGeometry wide = clipper->cachedGeometry();
    wide.size.x += 200.f;
    inner->onArrangeChildren(wide);
    ctx.setRootWidget(root);
    check(ctx.hitTest({ 150.f, 20.f }).empty() == false &&
          !ctx.hitTest({ 150.f, 20.f }).contains(leaf.get()), "SBorder clipChildren: 영역 밖으로 넘친 자식은 걸리지 않음");
    clipper->clipChildren(false);
    check(ctx.hitTest({ 150.f, 20.f }).lastWidget() == leaf, "잘라 내지 않으면 넘친 자식이 그려진 자리에서 걸림");
}

void testKeyBubbleAndChar() {
    std::printf("\n-- H1a-1: 키 버블 · 글자 --\n");
    auto leaf  = mkProbe("leaf", true);
    auto inner = mkProbe("inner", true);
    auto outer = mkProbe("outer");
    inner->child = leaf;
    outer->child = inner;
    outer->handleKeys = true;
    auto root = HNew(SOverlay);
    root->addSlot(outer, EHAlign::Fill, EVAlign::Fill, kPadA);
    arrange(root);
    HSlateContext ctx;
    ctx.setRootWidget(root);

    g_focusLog.clear();
    HReply r = ctx.handleKeyEvent(keyDown(257));
    check(!r.handled && g_focusLog.empty(), "포커스가 없으면 키는 아무 데도 가지 않고 Unhandled");
    check(!ctx.setFocus(outer) && ctx.focusedWidget() == nullptr, "포커스를 받지 않는 위젯은 거절");
    check(!ctx.setFocus(nullptr), "null 도 거절");

    check(ctx.setFocus(leaf) && ctx.focusedWidget() == leaf, "포커스를 받는 위젯에 포커스");
    g_focusLog.clear();
    r = ctx.handleKeyEvent(keyDown(257));
    check(r.handled && joinLog() == "leaf:down inner:down outer:down", "키가 포커스 위젯에서 부모로 버블되고 처리한 곳에서 멈춤: " + joinLog());

    inner->handleKeys = true;
    g_focusLog.clear();
    ctx.handleKeyEvent(keyUp(257));
    check(joinLog() == "leaf:up inner:up", "중간에서 처리하면 그 위로는 가지 않음(뗌은 onKeyUp): " + joinLog());

    g_focusLog.clear();
    r = ctx.handleCharEvent(HCharEvent{ 0xAC00 });
    check(!r.handled && joinLog() == "leaf:char44032", "글자는 포커스 위젯에만 감(처리 안 해도 버블하지 않음): " + joinLog());

    g_focusLog.clear();
    ctx.setFocus(inner);
    ctx.handleCharEvent(HCharEvent{ 'a' });
    check(joinLog() == "leaf:lost inner:recv inner:char97", "포커스를 옮기면 새 위젯이 글자를 받음: " + joinLog());
}

void testFocusLifetime() {
    std::printf("\n-- H1a-1: 포커스 수명 --\n");
    auto a = mkProbe("a", true);
    auto b = mkProbe("b", true);
    auto holder = mkProbe("holder");
    holder->child = b;
    auto root = HNew(SOverlay);
    root->addSlot(a, EHAlign::Fill, EVAlign::Fill, kPadA)
        ->addSlot(holder, EHAlign::Fill, EVAlign::Fill, kPadB);
    arrange(root);
    HSlateContext ctx;
    ctx.setRootWidget(root);

    g_focusLog.clear();
    ctx.setFocus(a);
    ctx.setFocus(b);
    check(joinLog() == "a:recv a:lost b:recv", "포커스가 바뀌면 이전 Lost → 새 Received 순서: " + joinLog());
    g_focusLog.clear();
    ctx.setFocus(b);
    check(g_focusLog.empty(), "같은 위젯에 다시 주면 알림 없음");

    // 트리에서 빠짐: 다음 키 이벤트에서 판정해 풂
    holder->child.reset();
    g_focusLog.clear();
    HReply r = ctx.handleKeyEvent(keyDown(65));
    check(!r.handled && ctx.focusedWidget() == nullptr, "트리에서 빠진 위젯의 포커스는 다음 키에서 풀림");
    check(joinLog() == "b:lost", "빠진 위젯은 키를 받지 않고 Lost 만 받음: " + joinLog());

    // 보이지 않는 조상: 키를 보내지 않고 풂
    holder->child = b;
    ctx.setFocus(b);
    holder->visibility = EVisibility::Collapsed;
    g_focusLog.clear();
    ctx.handleCharEvent(HCharEvent{ 'x' });
    check(ctx.focusedWidget() == nullptr && joinLog() == "b:lost", "경로에 보이지 않는 위젯이 있으면 글자를 보내지 않고 풂: " + joinLog());
    holder->visibility = EVisibility::Visible;

    // 파괴: 약한 참조라 저절로 비고, 다음 키에서도 아무 데도 가지 않음
    ctx.setFocus(b);
    holder->child.reset();
    std::weak_ptr<HWidget> bWeak = b;
    b.reset();
    check(bWeak.expired() && ctx.focusedWidget() == nullptr, "포커스 위젯이 파괴되면 focusedWidget() 은 null");
    g_focusLog.clear();
    check(!ctx.handleKeyEvent(keyDown(65)).handled && g_focusLog.empty(), "파괴된 포커스로 키가 가지 않음");

    // 루트 교체: 이전 위젯에 Lost, 같은 루트를 다시 넣으면 유지
    ctx.setFocus(a);
    ctx.setRootWidget(root);
    check(ctx.focusedWidget() == a, "같은 루트를 다시 넣으면 포커스 유지");
    g_focusLog.clear();
    ctx.setRootWidget(HNew(SOverlay));
    check(ctx.focusedWidget() == nullptr && joinLog() == "a:lost", "루트가 바뀌면 포커스가 풀리고 Lost: " + joinLog());

    // clearFocus
    ctx.setRootWidget(root);
    ctx.setFocus(a);
    g_focusLog.clear();
    ctx.clearFocus();
    check(ctx.focusedWidget() == nullptr && joinLog() == "a:lost", "clearFocus 가 Lost 를 부름");

    // 포커스를 받을 수 있는 위젯을 그리는 순서로 나열
    auto c = mkProbe("c", true);
    auto hidden = mkProbe("h", true);
    hidden->visibility = EVisibility::Hidden;
    auto list = HNew(SVerticalBox);
    list->addSlot(a)->addSlot(hidden)->addSlot(mkProbe("plain"))->addSlot(c);
    std::vector<std::shared_ptr<HWidget>> order;
    hslate::collectFocusable(list, order);
    check(order.size() == 2 && order[0] == a && order[1] == c, "collectFocusable: 보이는 포커스 위젯을 그리는 순서로");
}

// 누르면 응답에 포커스 요청을 싣는 탐침입니다.
struct FocusRequester : HWidget {
    HWidget* target = nullptr;
    bool     clear  = false;
    glm::vec2 computeDesiredSize() override { return { 10.f, 10.f }; }
    void paint(HPaintContext&, const HGeometry&) override {}
    HReply onMouseButton(const HPointerEvent& e) override {
        if (!e.down) return HReply::Handled();
        HReply r = HReply::Handled();
        if (clear) r.clearFocus(); else if (target) r.setFocus(target);
        return r;
    }
};

void testFocusReply() {
    std::printf("\n-- H1a-1: 응답의 포커스 요청 · 누르면 포커스 --\n");
    // 합치기 규칙(DEF-65 의 캡처와 같이 처음 요청이 이김)
    FocusProbe x("x", true), y("y", true);
    HReply m = HReply::Unhandled();
    m.merge(HReply::Unhandled());
    check(!m.hasFocusRequest(), "요청 없는 응답끼리는 요청 없음");
    m.merge(HReply::Unhandled().setFocus(&x));
    m.merge(HReply::Handled().setFocus(&y));
    check(m.focusRequest == &x && !m.focusClear && m.handled, "설정 요청은 처음 것이 이김");
    HReply c = HReply::Unhandled().clearFocus();
    c.merge(HReply::Handled().setFocus(&y));
    check(c.focusClear && c.focusRequest == nullptr, "먼저 온 해제 요청 뒤의 설정 요청은 버림");
    HReply s = HReply::Handled().setFocus(&x);
    s.merge(HReply::Handled().clearFocus());
    check(s.focusRequest == &x && !s.focusClear, "먼저 온 설정 요청 뒤의 해제 요청도 버림");
    HReply last = HReply::Handled().setFocus(&x).clearFocus();
    check(last.focusClear && last.focusRequest == nullptr, "한 응답 안에서는 나중에 부른 것이 덮음");

    // 문맥이 누름 뒤에 요청을 적용
    auto target = mkProbe("target", true);
    auto req    = std::make_shared<FocusRequester>();
    req->target = target.get();
    auto root = HNew(SOverlay);
    root->addSlot(req, EHAlign::Fill, EVAlign::Fill, kPadA)
        ->addSlot(target, EHAlign::Fill, EVAlign::Fill, kPadB);
    arrange(root);
    HSlateContext ctx;
    ctx.setRootWidget(root);
    ctx.handlePointerEvent(press(kInA));
    check(ctx.focusedWidget() == target, "누름 응답의 setFocus 가 적용됨");
    ctx.handlePointerEvent(release(kInA));
    check(ctx.focusedWidget() == target, "뗌은 포커스를 바꾸지 않음");
    req->clear = true;
    ctx.handlePointerEvent(press(kInA));
    ctx.handlePointerEvent(release(kInA));
    check(ctx.focusedWidget() == nullptr, "누름 응답의 clearFocus 가 적용됨");

    // 누르면 포커스: 누른 자리 경로의 가장 깊은 포커스 위젯
    ctx.handlePointerEvent(press(kInB));
    check(ctx.focusedWidget() == target, "누르면 포커스: 포커스 위젯을 누르면 포커스를 받음");
    ctx.handlePointerEvent(release(kInB));
    ctx.handlePointerEvent(press(kEmpty));
    check(ctx.focusedWidget() == target, "포커스 위젯이 없는 자리를 누르면 포커스는 그대로");
    ctx.handlePointerEvent(release(kEmpty));
    ctx.clearFocus();
    ctx.setClickToFocus(false);
    ctx.handlePointerEvent(press(kInB));
    check(!ctx.clickToFocus() && ctx.focusedWidget() == nullptr, "누르면 포커스를 끄면 누름으로 포커스가 생기지 않음");
    ctx.handlePointerEvent(release(kInB));
    ctx.setClickToFocus(true);
    HPointerEvent mv = move(kInB, false);
    ctx.handlePointerEvent(mv);
    ctx.handlePointerEvent(wheel(kInB, 1.f));
    check(ctx.focusedWidget() == nullptr, "이동 · 휠로는 포커스가 생기지 않음");

    // 지금 위젯(버튼 · 숫자 칸)은 포커스를 받지 않으므로 기존 동작 그대로
    resetCounts();
    FieldRig rig(1.f);
    HSlateContext ctx2;
    ctx2.setRootWidget(rig.root);
    ctx2.handlePointerEvent(press(kInA));
    ctx2.handlePointerEvent(release(kInA));
    check(ctx2.focusedWidget() == nullptr, "숫자 칸을 눌러도 새 포커스는 생기지 않음(H1a-2 전)");
    if (SNumberField::focusedField()) rig.field->commitEdit();
}

void testMultiContextFocus() {
    std::printf("\n-- H1a-1: 여러 문맥 포커스 분리 --\n");
    auto a = mkProbe("a", true);
    auto b = mkProbe("b", true);
    a->handleKeys = b->handleKeys = true;
    auto rootA = HNew(SOverlay);
    rootA->addSlot(a, EHAlign::Fill, EVAlign::Fill, kPadA);
    auto rootB = HNew(SOverlay);
    rootB->addSlot(b, EHAlign::Fill, EVAlign::Fill, kPadB);
    arrange(rootA);
    arrange(rootB);
    HSlateContext ctxA, ctxB;
    ctxA.setRootWidget(rootA);
    ctxB.setRootWidget(rootB);

    ctxA.setFocus(a);
    ctxB.setFocus(b);
    check(ctxA.focusedWidget() == a && ctxB.focusedWidget() == b, "문맥마다 포커스를 따로 가짐");
    g_focusLog.clear();
    ctxA.handleKeyEvent(keyDown(66));
    ctxB.handleCharEvent(HCharEvent{ 'q' });
    check(joinLog() == "a:down b:char113", "각 문맥의 키 · 글자는 그 문맥의 포커스로만 감: " + joinLog());
    ctxA.setRootWidget(HNew(SBox));
    check(ctxA.focusedWidget() == nullptr && ctxB.focusedWidget() == b, "한 문맥의 루트 교체는 다른 문맥 포커스를 건드리지 않음");
    // 다른 문맥 트리의 위젯은 그 문맥에서 키를 받지 못함(경로가 없음)
    ctxA.setFocus(b);
    g_focusLog.clear();
    check(!ctxA.handleKeyEvent(keyDown(66)).handled && ctxA.focusedWidget() == nullptr, "다른 문맥 트리의 위젯은 이 문맥에서 키를 받지 못하고 풀림");
    check(ctxB.focusedWidget() == b, "그래도 원래 문맥의 포커스는 유지");
}

// ── H1a-2: 포커스 훅 · 위젯 다시 쓰기 · 글 칸 위젯 포커스 ─────────────────────
void testFocusHookAndReuse() {
    std::printf("\n-- H1a-2: 포커스 훅 · HReuse · 글 칸 focusable --\n");
    auto a = mkProbe("a", true);
    auto b = mkProbe("b", true);
    HSlateContext ctx;
    std::vector<std::string> hook;
    ctx.setFocusHook([&](const std::shared_ptr<HWidget>& p, const std::shared_ptr<HWidget>& n) {
        auto nm = [](const std::shared_ptr<HWidget>& w) {
            auto pr = std::dynamic_pointer_cast<FocusProbe>(w);
            return pr ? pr->name : std::string(w ? "?" : "-");
        };
        hook.push_back(nm(p) + ">" + nm(n));
        g_focusLog.push_back("hook");
    });
    g_focusLog.clear();
    ctx.setFocus(a);
    ctx.setFocus(b);
    ctx.setFocus(b);   // 같은 위젯: 알림 없음
    ctx.clearFocus();
    ctx.clearFocus();  // 포커스 없음: 알림 없음
    std::string hs;
    for (const auto& h : hook) hs += h + " ";
    check(hs == "->a a>b b>- ", "훅: 바뀔 때마다 한 번(이전 > 다음), 같은 위젯 · 빈 해제는 알림 없음: " + hs);
    check(joinLog() == "a:recv hook a:lost b:recv hook b:lost hook", "훅은 위젯의 Lost · Received 뒤에 불림: " + joinLog());

    // Lost 안에서 다시 옮기면 안쪽 변경만 알림(바깥 setFocus 는 최종 결과가 자기 것이 아니므로 알리지 않음)
    auto c = mkProbe("c", true);
    struct Redirect : FocusProbe {
        HSlateContext* ctx = nullptr;
        std::shared_ptr<HWidget> to;
        Redirect() : FocusProbe("r", true) {}
        void onFocusLost() override { FocusProbe::onFocusLost(); if (ctx && to) { auto t = to; to.reset(); ctx->setFocus(t); } }
    };
    auto r = std::make_shared<Redirect>();
    r->ctx = &ctx;
    r->to  = c;
    ctx.setFocus(r);
    hook.clear();
    ctx.setFocus(a);   // r 의 Lost 가 c 로 옮김
    hs.clear();
    for (const auto& h : hook) hs += h + " ";
    // 안쪽 변경의 "이전" 은 바깥 setFocus 가 잠깐 잡아 둔 a 입니다(H1a-1 규칙: 먼저 바꾼 뒤 알림).
    check(ctx.focusedWidget() == c && hs == "a>c ", "Lost 안의 재이동: 최종 포커스 c, 훅은 안쪽 한 번: " + hs);

    // 루트 교체는 훅으로 알림(에디터는 preserveFocusAcross 로 감쌈)
    hook.clear();
    ctx.setRootWidget(HNew(SBox));
    ctx.setRootWidget(HNew(SBox));   // 포커스가 이미 없으면 알림 없음
    check(hook.size() == 1 && hook[0] == "c>-" && !ctx.focusedWidget(), "루트 교체: 포커스 해제를 훅으로 알림");

    // HReuse: 같은 객체를 처음 상태로 되돌려 다시 씀(약한 참조 포커스가 살아남음)
    std::shared_ptr<SSearchBox> keep;
    auto first = HReuse(keep);
    first->focusable(true)->placeholder("x");
    HGeometry g;
    g.size = { 50.f, 20.f };
    first->onArrangeChildren(g);
    HSlateContext ctx2;
    check(ctx2.setFocus(first), "focusable(true) 인 SSearchBox 는 포커스를 받음");
    const HWidget* id = first.get();
    auto again = HReuse(keep);
    check(again.get() == id && ctx2.focusedWidget().get() == id, "HReuse: 같은 객체(포커스 유지)");
    check(!again->supportsKeyboardFocus() && again->cachedGeometry().size.x == 0.f,
          "HReuse: 설정 · 배치 상태는 새 위젯과 같게 지움(focusable 도 다시 켜야 함)");
    check(again->weak_from_this().lock() == again, "HReuse: shared_from_this 소유 정보는 그대로");
    auto plain = HNew(SSearchBox);
    check(!ctx2.setFocus(plain) && ctx2.focusedWidget().get() == id, "focusable 을 켜지 않은 SSearchBox 는 거절(게임 쪽 그대로)");
    auto et = HNew(SEditableText);
    check(!et->supportsKeyboardFocus() && et->focusable(true)->supportsKeyboardFocus(), "SEditableText::focusable");
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

// ── H1b: 배치 캐시 · 배치 건너뛰기 · 지연 변경 큐 · 스플리터 가중치 ────────────────────────
// 원하는 크기를 측정 패스마다 한 번만 재고(HWidget::desiredSize), 같은 입력이면 arrange 를 건너뛰며, 콜백에서 맡긴
// 일은 이벤트 전달이 끝난 뒤 실행되는지 봅니다. "예전 방식" 은 hslate::layoutCounters().cacheEnabled = false 입니다
// (캐시도 건너뛰기도 없이 매번 재고 배치 — H1b 전 코드와 같은 계산).

// computeDesiredSize 를 부른 횟수를 세는 잎 위젯.
struct MeasureProbe : HWidget {
    int       measured = 0;
    glm::vec2 size{ 10.f, 10.f };
    glm::vec2 computeDesiredSize() override { ++measured; return size; }
    void paint(HPaintContext& ctx, const HGeometry& geo) override { ctx.drawRect(geo, glm::vec4(1.f)); }
};

// 위젯 트리의 geometry 를 그리는 순서대로 모읍니다(배치 결과 비교용).
void collectGeometry(const std::shared_ptr<HWidget>& w, std::vector<HGeometry>& out) {
    if (!w) return;
    out.push_back(w->cachedGeometry());
    w->forEachChild([&out](const std::shared_ptr<HWidget>& c) { collectGeometry(c, out); });
}

bool sameGeometry(const std::vector<HGeometry>& a, const std::vector<HGeometry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].position != b[i].position || a[i].size != b[i].size || a[i].scale != b[i].scale) return false;
    return true;
}

bool sameElements(const std::vector<HDrawElement>& a, const std::vector<HDrawElement>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].type != b[i].type || a[i].rect != b[i].rect || a[i].text != b[i].text ||
            a[i].layer != b[i].layer || a[i].clip != b[i].clip || a[i].clipped != b[i].clipped) return false;
    return true;
}

// 예전 방식으로 바꿔 두고 끝나면 되돌립니다.
struct NoCacheScope {
    bool saved;
    NoCacheScope() : saved(hslate::layoutCounters().cacheEnabled) { hslate::layoutCounters().cacheEnabled = false; }
    ~NoCacheScope() { hslate::layoutCounters().cacheEnabled = saved; }
};

// 깊게 겹친 Auto 상자 · 여러 줄 글자 · 접기 섹션(펼침 · 접힘) · 스크롤 상자 · 정렬한 SBox · 오버레이를 섞은 트리.
// 같은 인자로 두 번 만들면 같은 트리입니다. scrollTo 는 스크롤 상자의 위치(배치 뒤 적용, 다시 배치해야 보임).
struct MixedTree {
    std::shared_ptr<SVerticalBox> root;
    std::shared_ptr<SScrollBox>   scroll;
    std::shared_ptr<SCollapseSection> openSec, closedSec;
    std::string* dynText = nullptr;
};
MixedTree buildMixedTree(std::string* dynText) {
    MixedTree t;
    t.dynText = dynText;
    using VR = SVerticalBox; using HR = SHorizontalBox;
    // 깊이 6 의 Auto 중첩(세로 · 가로 번갈아)
    std::shared_ptr<HWidget> nest = HNew(STextBlock)->text(std::string("deep\nmulti line\nx"))->fontSize(13.f);
    for (int d = 0; d < 6; ++d) {
        if (d % 2 == 0) {
            auto b = HNew(VR);
            b->addSlot(nest, VR::ESizeRule::Auto, 0.f, HPadding{ 1.f, 2.f, 3.f, 4.f })
             ->addSlot(HNew(STextBlock)->text(std::string("v") + std::to_string(d)), VR::ESizeRule::Auto);
            nest = b;
        } else {
            auto b = HNew(HR);
            b->addSlot(HNew(STextBlock)->text(std::string("h") + std::to_string(d)), HR::ESizeRule::Auto)
             ->addSlot(nest, HR::ESizeRule::Auto, 0.f, HPadding{ 2.f }, EVAlign::Center);
            nest = b;
        }
    }
    auto dyn = HNew(STextBlock)->text(Attr<std::string>(std::function<std::string()>([dynText] { return *dynText; })));
    auto rowDyn = HNew(HR);
    rowDyn->addSlot(dyn, HR::ESizeRule::Auto)
          ->addSlot(std::make_shared<MeasureProbe>(), HR::ESizeRule::Auto)
          ->addSlot(HNew(SBox)->hAlign(EHAlign::Right)->vAlign(EVAlign::Bottom)->padding(HPadding{ 3.f })
                        ->content(HNew(STextBlock)->text(std::string("right"))), HR::ESizeRule::Fill);
    t.openSec = HNew(SCollapseSection)->title("open")->expanded(true)
        ->body(HNew(VR)->addSlot(HNew(STextBlock)->text(std::string("in open")), VR::ESizeRule::Auto)
                       ->addSlot(std::make_shared<MeasureProbe>(), VR::ESizeRule::Fixed, 12.f));
    t.closedSec = HNew(SCollapseSection)->title("closed")->expanded(false)
        ->body(HNew(VR)->addSlot(HNew(STextBlock)->text(std::string("in closed")), VR::ESizeRule::Auto));
    auto list = HNew(VR);
    for (int i = 0; i < 12; ++i)
        list->addSlot(HNew(SBorder)->padding(HPadding{ 2.f })->content(HNew(STextBlock)->text(std::string("row ") + std::to_string(i))),
                      VR::ESizeRule::Auto);
    t.scroll = HNew(SScrollBox)->content(list);
    auto over = HNew(SOverlay);
    over->addSlot(HNew(SBorder)->background(glm::vec4(0.2f)), EHAlign::Fill, EVAlign::Fill)
        ->addSlot(HNew(STextBlock)->text(std::string("centered")), EHAlign::Center, EVAlign::Center);
    t.root = HNew(VR);
    t.root->addSlot(nest, VR::ESizeRule::Auto)
          ->addSlot(rowDyn, VR::ESizeRule::Auto)
          ->addSlot(t.openSec, VR::ESizeRule::Auto)
          ->addSlot(t.closedSec, VR::ESizeRule::Auto)
          ->addSlot(t.scroll, VR::ESizeRule::Fixed, 80.f)
          ->addSlot(over, VR::ESizeRule::Fill)
          ->addSlot(HNew(SButton)->padding(HPadding{ 4.f })->hAlign(EHAlign::Center)->content(HNew(STextBlock)->text(std::string("btn"))),
                    VR::ESizeRule::Auto);
    return t;
}

void testLayoutCacheMatchesUncached() {
    std::printf("\n-- H1b: 원하는 크기 캐시 = 예전 방식 배치 --\n");
    const glm::vec2 size{ 420.f, 600.f };
    std::string textA = "dynamic text", textB = "dynamic text";
    MixedTree a = buildMixedTree(&textA);
    MixedTree b = buildMixedTree(&textB);
    HSlateContext ca, cb;
    ca.setRootWidget(a.root);
    cb.setRootWidget(b.root);

    std::vector<HGeometry> ga, gb;
    std::vector<HDrawElement> ea, eb;
    ca.arrange(size);
    { NoCacheScope nc; cb.arrange(size); }
    collectGeometry(a.root, ga); collectGeometry(b.root, gb);
    check(ga.size() > 40 && sameGeometry(ga, gb), "섞은 트리: 캐시 배치 geometry 가 예전 방식과 같음 (" + std::to_string(ga.size()) + " 위젯)");
    ca.collectElements(size, nullptr, ea);
    { NoCacheScope nc; cb.collectElements(size, nullptr, eb); }
    check(!ea.empty() && sameElements(ea, eb), "섞은 트리: 그리기 요소가 예전 방식과 같음 (" + std::to_string(ea.size()) + " 요소)");

    // 상태를 바꾼 뒤에도 같아야 합니다: 글자 폭 · 접기 · 스크롤 위치.
    textA = textB = "a much longer dynamic text that pushes siblings";
    a.openSec->expanded(false); b.openSec->expanded(false);
    a.closedSec->expanded(true); b.closedSec->expanded(true);
    ca.arrange(size);
    { NoCacheScope nc; cb.arrange(size); }
    a.scroll->setScrollOffset(37.f); b.scroll->setScrollOffset(37.f);
    ca.arrange(size);
    { NoCacheScope nc; cb.arrange(size); }
    ga.clear(); gb.clear();
    collectGeometry(a.root, ga); collectGeometry(b.root, gb);
    check(a.scroll->scrollOffset() == 37.f && sameGeometry(ga, gb), "글자 폭 · 접기 · 펼치기 · 스크롤 바꾼 뒤에도 같음");
    ea.clear(); eb.clear();
    ca.collectElements(size, nullptr, ea);
    { NoCacheScope nc; cb.collectElements(size, nullptr, eb); }
    check(sameElements(ea, eb), "바꾼 뒤 그리기 요소도 같음");
}

void testMeasureCountLinear() {
    std::printf("\n-- H1b: 측정 횟수가 트리 크기에 비례 --\n");
    auto& lc = hslate::layoutCounters();
    // 깊이 D 의 Auto 세로 상자 사슬 + 잎 하나. 위젯 수 = D + 1.
    auto chain = [](int depth, std::shared_ptr<MeasureProbe>& leaf) {
        leaf = std::make_shared<MeasureProbe>();
        std::shared_ptr<HWidget> w = leaf;
        for (int d = 0; d < depth; ++d) {
            auto b = HNew(SVerticalBox);
            b->addSlot(w, SVerticalBox::ESizeRule::Auto);
            w = b;
        }
        return w;
    };
    for (int depth : { 4, 16 }) {
        std::shared_ptr<MeasureProbe> leaf;
        HSlateContext ctx;
        ctx.setRootWidget(chain(depth, leaf));
        const uint64_t m0 = lc.measures;
        ctx.arrange({ 200.f, 200.f });
        const uint64_t cached = lc.measures - m0;
        check(leaf->measured == 1 && cached == (uint64_t)depth + 1,
              "깊이 " + std::to_string(depth) + ": 캐시 배치는 위젯마다 한 번 (측정 " + std::to_string(cached) + ", 잎 " +
              std::to_string(leaf->measured) + ")");
        leaf->measured = 0;
        ctx.invalidateLayout();
        const uint64_t m1 = lc.measures;
        { NoCacheScope nc; ctx.arrange({ 200.f, 200.f }); }
        const uint64_t uncached = lc.measures - m1;
        check(leaf->measured == 2 * depth + 1 && uncached > cached * (uint64_t)depth / 2,
              "깊이 " + std::to_string(depth) + ": 예전 방식은 잎을 2D+1 번 잼 (측정 " + std::to_string(uncached) + ", 잎 " +
              std::to_string(leaf->measured) + ")");
    }
    // 넓은 트리: 가로 상자 하나에 잎 64개(Auto 32 · Fill 16 · Fixed 16) → 위젯 65개, 측정 65번.
    auto row = HNew(SHorizontalBox);
    std::vector<std::shared_ptr<MeasureProbe>> leaves;
    for (int i = 0; i < 64; ++i) {
        leaves.push_back(std::make_shared<MeasureProbe>());
        const auto rule = i < 32 ? SHorizontalBox::ESizeRule::Auto : i < 48 ? SHorizontalBox::ESizeRule::Fill : SHorizontalBox::ESizeRule::Fixed;
        row->addSlot(leaves.back(), rule, 8.f);
    }
    HSlateContext ctx;
    ctx.setRootWidget(row);
    const uint64_t m0 = lc.measures;
    ctx.arrange({ 900.f, 40.f });
    bool once = true;
    for (auto& l : leaves) once = once && l->measured == 1;
    check(once && lc.measures - m0 == 65, "넓은 트리 65 위젯: 측정 65번, 잎마다 한 번");
    // 패스 밖에서는 캐시를 쓰지 않습니다(에디터가 버튼 폭을 미리 재는 곳).
    leaves[0]->size = { 33.f, 10.f };
    check(row->computeDesiredSize().x > 0.f && leaves[0]->desiredSize().x == 33.f, "패스 밖 desiredSize 는 늘 새로 잼");
}

void testArrangeSkip() {
    std::printf("\n-- H1b: 같은 입력이면 arrange 를 건너뜀 · 바뀌면 다시 배치 --\n");
    std::string text = "abc";
    auto dyn  = HNew(STextBlock)->text(Attr<std::string>(std::function<std::string()>([&text] { return text; })));
    auto tail = std::make_shared<MeasureProbe>();
    auto sec  = HNew(SCollapseSection)->title("sec")->headerHeight(20.f)
        ->body(HNew(SVerticalBox)->addSlot(std::make_shared<MeasureProbe>(), SVerticalBox::ESizeRule::Auto));
    auto row = HNew(SHorizontalBox);
    row->addSlot(dyn, SHorizontalBox::ESizeRule::Auto)->addSlot(tail, SHorizontalBox::ESizeRule::Auto);
    auto root = HNew(SVerticalBox);
    root->addSlot(row, SVerticalBox::ESizeRule::Auto)->addSlot(sec, SVerticalBox::ESizeRule::Auto);
    HSlateContext ctx;
    ctx.setRootWidget(root);
    const glm::vec2 size{ 300.f, 200.f };

    auto runs = [&ctx] { return ctx.layoutStats().arrangeRuns; };
    ctx.arrange(size);
    const uint64_t g1 = ctx.layoutGeneration();
    ctx.arrange(size);
    check(ctx.layoutStats().arrangeCalls == 2 && runs() == 1 && ctx.layoutGeneration() == g1, "같은 프레임 두 번째 arrange 는 건너뜀(세대 그대로)");

    // 이동만 보내면 건너뜀(호버만 바뀜).
    ctx.handlePointerEvent(move({ 5.f, 5.f }, false));
    ctx.arrange(size);
    check(runs() == 1, "이동 이벤트 뒤에는 건너뜀");

    // Attr 글자 폭이 바뀌면 다시 배치하고, 옆 위젯이 밀립니다.
    const float tailX0 = tail->cachedGeometry().position.x;
    text = "abcdefghij";
    ctx.arrange(size);
    check(runs() == 2 && tail->cachedGeometry().position.x > tailX0, "글자 폭이 바뀌면 다시 배치(옆 위젯이 밀림)");

    // 접기 섹션 헤더 클릭(누름 · 뗌) → 다시 배치.
    const glm::vec2 hdr{ 5.f, sec->cachedGeometry().position.y + 5.f };
    ctx.handlePointerEvent(press(hdr));
    ctx.handlePointerEvent(release(hdr));
    ctx.arrange(size);
    check(!sec->isExpanded() && runs() == 3, "접기 클릭 뒤 다시 배치");
    // 코드로 펼침을 바꿔도(이벤트 없이) 다시 배치.
    sec->expanded(true);
    ctx.arrange(size);
    check(runs() == 4, "expanded() 로 바꿔도 다시 배치");

    // 크기 · 슬롯 추가 · 슬롯 가중치 · 루트 교체 · invalidateLayout.
    ctx.arrange({ 301.f, 200.f });
    check(runs() == 5, "크기가 바뀌면 다시 배치");
    row->addSlot(std::make_shared<MeasureProbe>(), SHorizontalBox::ESizeRule::Fixed, 7.f);
    ctx.arrange({ 301.f, 200.f });
    check(runs() == 6, "슬롯을 더하면 다시 배치(원하는 크기가 같은 Fixed 슬롯이어도)");
    root->setSlotValue(1, 2.f);
    ctx.arrange({ 301.f, 200.f });
    check(runs() == 7, "슬롯 값을 바꾸면 다시 배치");
    ctx.invalidateLayout();
    ctx.arrange({ 301.f, 200.f });
    check(runs() == 8, "invalidateLayout 뒤 다시 배치");
    ctx.setRootWidget(root);
    ctx.arrange({ 301.f, 200.f });
    check(runs() == 9, "setRootWidget 뒤 다시 배치");
    ctx.arrange({ 301.f, 200.f });
    check(runs() == 9, "아무것도 안 바뀌면 다시 건너뜀");

    // 휠로 스크롤 위치가 바뀌면 다시 배치하고 내용이 올라갑니다.
    auto list = HNew(SVerticalBox);
    std::vector<std::shared_ptr<MeasureProbe>> rows;
    for (int i = 0; i < 10; ++i) { rows.push_back(std::make_shared<MeasureProbe>()); rows.back()->size = { 10.f, 30.f };
                                   list->addSlot(rows.back(), SVerticalBox::ESizeRule::Auto); }
    auto sb = HNew(SScrollBox)->content(list)->wheelStep(10.f);
    HSlateContext sctx;
    sctx.setRootWidget(sb);
    sctx.arrange({ 100.f, 100.f });
    const uint64_t sr = sctx.layoutStats().arrangeRuns;
    const float y0 = rows[0]->cachedGeometry().position.y;
    sctx.handlePointerEvent(wheel({ 50.f, 50.f }, -1.f));
    sctx.arrange({ 100.f, 100.f });
    check(sctx.layoutStats().arrangeRuns == sr + 1 && rows[0]->cachedGeometry().position.y == y0 - 10.f, "휠 스크롤 뒤 다시 배치(내용이 10px 올라감)");
    sb->setScrollOffset(0.f);
    sctx.arrange({ 100.f, 100.f });
    check(rows[0]->cachedGeometry().position.y == y0, "setScrollOffset 도 다시 배치");

    // 건너뛴 결과가 예전 방식과 같은지(위 상태 그대로 새로 배치해 비교).
    std::vector<HGeometry> g1v, g2v;
    collectGeometry(root, g1v);
    { NoCacheScope nc; ctx.arrange({ 301.f, 200.f }); }
    collectGeometry(root, g2v);
    check(sameGeometry(g1v, g2v), "건너뛴 배치 결과 = 다시 배치한 결과");
}

// 누르면 문맥에 일을 맡기는 탐침. 맡긴 일이 전달 중에 돌면 ranInDispatch 가 참이 됩니다.
struct DeferProbe : HWidget {
    HSlateContext* ctx = nullptr;
    std::vector<std::string>* log = nullptr;
    bool inDispatch = false;
    bool ranInDispatch = false;
    std::function<void()> work;
    glm::vec2 computeDesiredSize() override { return { 10.f, 10.f }; }
    void paint(HPaintContext&, const HGeometry&) override {}
    HReply onMouseButton(const HPointerEvent& e) override {
        if (!e.down) return HReply::Handled();
        inDispatch = true;
        log->push_back("press");
        // 맡긴 일이 이 위젯을 파괴할 수 있으므로 필요한 값은 복사해 둡니다.
        ctx->deferred([log = log, w = work, flag = &ranInDispatch, in = &inDispatch] {
            *flag = *flag || *in;   // 위젯이 살아 있을 때만 읽음(아래 work 전)
            log->push_back("deferred");
            if (w) w();
        });
        ctx->flushDeferred();               // 전달 중에는 아무것도 하지 않아야 함
        log->push_back("press-end");
        inDispatch = false;
        return HReply::Handled();
    }
};

void testDeferredQueue() {
    std::printf("\n-- H1b: 지연 변경 큐 --\n");
    std::vector<std::string> log;
    auto box = HNew(SVerticalBox);
    auto probe = std::make_shared<DeferProbe>();
    HSlateContext ctx;
    probe->ctx = &ctx; probe->log = &log;
    box->addSlot(probe, SVerticalBox::ESizeRule::Fixed, 20.f);
    ctx.setRootWidget(box);
    ctx.arrange({ 100.f, 100.f });

    // 맡긴 일이 자기를 품은 상자를 비워도(예전에 크래시 나던 경로) 전달이 끝난 뒤라 안전합니다.
    std::weak_ptr<DeferProbe> weak = probe;
    probe->work = [box] { box->clearSlots(); box->addSlot(HNew(SBox), SVerticalBox::ESizeRule::Fixed, 20.f); };
    probe.reset();
    ctx.handlePointerEvent(press({ 5.f, 5.f }));
    check(log.size() == 2 && log[0] == "press" && log[1] == "press-end" && ctx.deferredCount() == 1,
          "전달 중 flushDeferred 는 아무것도 안 함, 일은 큐에 남음");
    check(!weak.expired(), "전달이 끝날 때까지 위젯이 살아 있음");
    ctx.arrange({ 100.f, 100.f });
    check(log.size() == 3 && log[2] == "deferred" && ctx.deferredCount() == 0, "다음 arrange 앞에서 실행됨(전달 중 맡긴 일)");
    check(weak.expired(), "맡긴 일이 트리를 다시 만들어 탐침이 파괴됨(실행 중이 아니라 안전)");

    // 전달 밖에서 맡긴 일은 arrange 가 실행하지 않고 소유자의 flushDeferred 를 기다립니다(예전 지연 플래그 자리).
    int n = 0;
    ctx.deferred([&n, &ctx] { ++n; ctx.deferred([&n] { n += 10; }); });
    ctx.arrange({ 100.f, 100.f });
    check(n == 0 && ctx.deferredCount() == 1, "전달 밖에서 맡긴 일은 arrange 가 실행하지 않음");
    ctx.flushDeferred();
    check(n == 1 && ctx.deferredCount() == 1, "flush 는 시작 때 쌓인 것만 실행, 실행 중 맡긴 것은 다음으로");
    ctx.flushDeferred();
    check(n == 11 && ctx.deferredCount() == 0, "다음 flush 에서 나머지 실행");
}

void testSplitterWeights() {
    std::printf("\n-- H1b: 스플리터 가중치만 고친 배치 = 다시 만든 배치 --\n");
    // 도크 분할과 같은 모양: 자식 · 손잡이(Fixed 5) · 자식 · 손잡이 · 자식.
    auto buildH = [](float r0, float r1, float r2) {
        auto box = HNew(SHorizontalBox);
        const float r[3] = { r0, r1, r2 };
        for (int i = 0; i < 3; ++i) {
            box->addSlot(HNew(SBorder)->padding(HPadding{ 1.f })->content(HNew(STextBlock)->text(std::string("p") + std::to_string(i))),
                         SHorizontalBox::ESizeRule::Fill, r[i]);
            if (i < 2) box->addSlot(HNew(SBorder), SHorizontalBox::ESizeRule::Fixed, 5.f);
        }
        return box;
    };
    auto buildV = [](float r0, float r1) {
        auto box = HNew(SVerticalBox);
        box->addSlot(HNew(SBox), SVerticalBox::ESizeRule::Fill, r0)
           ->addSlot(HNew(SBorder), SVerticalBox::ESizeRule::Fixed, 5.f)
           ->addSlot(HNew(SBox), SVerticalBox::ESizeRule::Fill, r1);
        return box;
    };
    const glm::vec2 size{ 1013.f, 577.f };
    auto live = buildH(0.20f, 0.60f, 0.20f);
    HSlateContext ctx;
    ctx.setRootWidget(live);
    ctx.arrange(size);
    const uint64_t gen = ctx.layoutGeneration();
    // 끄는 동안 몇 번 바꿈(마지막 값만 남음).
    for (float l : { 0.25f, 0.31f, 0.337f }) {
        const float sum = 0.80f;
        check(live->setSlotValue(0, l) && live->setSlotValue(2, sum - l), "가중치 바꾸기 " + std::to_string(l));
        ctx.arrange(size);
    }
    check(ctx.layoutGeneration() == gen + 3, "가중치를 바꿀 때마다 다시 배치");
    check(!live->setSlotValue(5, 1.f) && live->slotValue(0) == 0.337f, "범위 밖 슬롯은 거절, 값은 그대로");
    auto fresh = buildH(0.337f, 0.80f - 0.337f, 0.20f);
    HSlateContext fctx;
    fctx.setRootWidget(fresh);
    fctx.arrange(size);
    std::vector<HGeometry> ga, gb;
    collectGeometry(live, ga); collectGeometry(fresh, gb);
    check(sameGeometry(ga, gb), "가로 분할: 가중치만 고친 배치 = 같은 비율로 다시 만든 배치");

    auto vlive = buildV(0.72f, 0.28f);
    HSlateContext vctx;
    vctx.setRootWidget(vlive);
    vctx.arrange(size);
    vlive->setSlotValue(0, 0.5f); vlive->setSlotValue(2, 0.5f);
    vctx.arrange(size);
    auto vfresh = buildV(0.5f, 0.5f);
    HSlateContext vfctx;
    vfctx.setRootWidget(vfresh);
    vfctx.arrange(size);
    ga.clear(); gb.clear();
    collectGeometry(vlive, ga); collectGeometry(vfresh, gb);
    check(sameGeometry(ga, gb), "세로 분할: 가중치만 고친 배치 = 다시 만든 배치");
}


// ── M3a: 떠 있는 도크 창(EditorDock 창 단위 · EditorFloatWindows 수명) ───────────────────────────────────────────
namespace m3a {

// 렌더러 · 플랫폼 호출 순서를 기록하는 가짜 창.
struct Log { std::vector<std::string> lines; };

class FakeWin : public IWindow {
public:
    FakeWin(Log* log, int id, int w, int h) : log_(log), id_(id), w_(w), h_(h) {}
    void init() override {}
    void cleanup() override { if (!cleaned_) { cleaned_ = true; log_->lines.push_back("destroyWindow:" + std::to_string(id_)); } }
    bool shouldClose() const override { return close_; }
    void setShouldClose(bool c) override { close_ = c; }
    void pollEvents() override {}
    void waitEvents() override {}
    void getFramebufferSize(int& w, int& h) const override { w = w_; h = h_; }
    void getWindowSize(int& w, int& h) const override { w = w_; h = h_; }
    void setTitle(const std::string& t) override { title = t; }
    bool wasResized() const override { return false; }
    void resetResizedFlag() override {}
    bool getWindowPos(int& x, int& y) const override { x = x_; y = y_; return true; }
    void setWindowPos(int x, int y) override { x_ = x; y_ = y; }
    void focus() override { ++focusCalls; }
    bool providesSurface() const override { return false; }
    VkSurfaceKHR createSurface(VkInstance) override { return VK_NULL_HANDLE; }
    std::vector<const char*> getRequiredInstanceExtensions() const override { return {}; }
    void  getMouseDelta(float& dx, float& dy) override { dx = dy = 0.f; }
    void  getRightMouseDelta(float& dx, float& dy) override { dx = dy = 0.f; }
    float getScrollDelta() override { return 0.f; }
    bool  isKeyDown(int) const override { return false; }
    int   keyPressCount(int) const override { return 0; }
    bool  isMouseButtonDown(int) const override { return false; }
    void  getMousePosition(float& x, float& y) const override { x = y = -1.f; }
    void  activateCameraMode() override {}
    void  deactivateCameraMode() override {}
    bool  isCameraModeActive() const override { return false; }
    void  drainCharQueue(std::vector<unsigned int>& out) override { out.clear(); }
    std::string getClipboardText() const override { return {}; }
    void  setClipboardText(const std::string&) override {}
    void  setCursorShape(int) override {}

    std::string title;
    int focusCalls = 0;
    int x_ = 0, y_ = 0;

private:
    Log* log_;
    int  id_;
    int  w_, h_;
    bool close_   = false;
    bool cleaned_ = false;
};

// 패널 위젯 출현 횟수(루트들을 forEachChild 로 돌며 셈).
static void countIn(const std::shared_ptr<HWidget>& w, const HWidget* target, int& n) {
    if (!w) return;
    if (w.get() == target) ++n;
    w->forEachChild([&](const std::shared_ptr<HWidget>& c) { countIn(c, target, n); });
}

}  // namespace m3a

// 떠 있는 창 수명: 만들기(작업 영역 끼우기) · OS 닫기 알림 한 번 · 프레임 끝 정리 순서(렌더러 대상 → OS 창) · 실패 처리.
void testFloatWindowLifecycle() {
    std::printf("\n-- M3a: 떠 있는 창 수명 --\n");
    using namespace m3a;
    Log log;
    std::vector<FakeWin*> made;
    bool failCreate = false, refuseTarget = false;
    uint32_t nextTarget = 10;
    std::vector<std::pair<uint32_t, HSlateContext*>> uis;

    EditorFloatWindows fw;
    EditorFloatWindows::Ops ops;
    ops.createWindow = [&](int w, int h, const std::string& title, bool hidden) -> std::unique_ptr<IWindow> {
        log.lines.push_back(std::string("createWindow:") + title + (hidden ? ":hidden" : ""));
        if (failCreate) return nullptr;
        auto p = std::make_unique<FakeWin>(&log, (int)made.size() + 1, w, h);
        made.push_back(p.get());
        return p;
    };
    ops.addTarget    = [&](IWindow*) -> uint32_t {
        if (refuseTarget) { log.lines.push_back("addTarget:refused"); return 0u; }
        log.lines.push_back("addTarget:" + std::to_string(nextTarget));
        return nextTarget++;
    };
    ops.setUi        = [&](uint32_t id, HSlateContext* c) { uis.push_back({ id, c }); log.lines.push_back("setUi:" + std::to_string(id)); };
    ops.removeTarget = [&](uint32_t id) { log.lines.push_back("removeTarget:" + std::to_string(id)); };
    ops.workAreas    = [] { return std::vector<hwinplace::Rect>{ { 0, 0, 1920, 1040 } }; };
    fw.init(std::move(ops), /*hidden=*/true);

    auto rootA = HNew(SBorder), rootB = HNew(SBorder);
    std::vector<EditorFloatWindows::Desired> want = {
        { 1, { 120, 80, 400, 300 }, "Details", rootA },
        { 4, { -5000, -3000, 400, 700 }, "Outliner", rootB },
    };
    fw.sync(want);
    check(fw.windows().size() == 2 && made.size() == 2, "원하는 창 둘 → OS 창 둘");
    check(log.lines.size() >= 3 && log.lines[0] == "createWindow:Details:hidden" && log.lines[1] == "addTarget:10" &&
          log.lines[2] == "setUi:10", "만들기 순서: OS 창 → 렌더러 대상 → 문맥 전달(숨은 실행이면 숨은 창)");
    check(made[0]->x_ == 120 && made[0]->y_ == 80, "화면 안의 자리는 그대로");
    check(made[1]->x_ == 0 && made[1]->y_ == hwinplace::kTitleBarPx, "화면 밖 자리는 작업 영역 안으로 끼움");
    const EditorWindow* w1 = fw.find(1);
    check(w1 && w1->ctx && w1->ctx->rootWidget() == rootA && uis.size() == 2 && uis[0].second == w1->ctx.get(),
          "문맥 루트 = 도크 호스트, 렌더러에 그 문맥을 넘김");
    hwinplace::Rect rr;
    check(fw.readRect(4, rr) && rr.x == 0 && rr.y == hwinplace::kTitleBarPx && rr.w == 400 && rr.h == 700,
          "readRect: OS 창의 자리 · 크기(저장용)");

    // 제목 · 루트가 바뀌면 고침, 다시 만들지 않음.
    log.lines.clear();
    auto rootA2 = HNew(SBorder);
    want[0].title = "Outliner";
    want[0].root  = rootA2;
    fw.sync(want);
    check(log.lines.empty() && made[0]->title == "Outliner" && fw.find(1)->ctx->rootWidget() == rootA2,
          "같은 번호: 제목 · 루트만 고치고 창은 그대로");

    // OS 닫기 단추: 창마다 한 번만 알림.
    made[0]->setShouldClose(true);
    std::vector<int> req = fw.takeCloseRequests();
    check(req.size() == 1 && req[0] == 1, "OS 닫기 단추 → 그 창 번호를 알림");
    check(fw.takeCloseRequests().empty(), "같은 닫기는 한 번만 알림");
    fw.keepOpen(1);
    check(!made[0]->shouldClose() && fw.takeCloseRequests().empty(), "keepOpen: 닫기 요청을 되돌림");
    made[0]->setShouldClose(true);
    check(fw.takeCloseRequests().size() == 1, "되돌린 뒤 다시 누르면 다시 알림");

    // 프레임 끝 정리: 도크가 그 창을 뺀 목록을 주면 렌더러 대상 → OS 창 순으로 없앰.
    log.lines.clear();
    HSlateContext* ctx1 = fw.find(1)->ctx.get();
    (void)ctx1;
    want.erase(want.begin());
    fw.sync(want);
    check(log.lines.size() == 2 && log.lines[0] == "removeTarget:10" && log.lines[1] == "destroyWindow:1",
          "정리 순서: 렌더러 대상 제거 → OS 창 파괴");
    check(fw.windows().size() == 1 && !fw.find(1) && fw.find(4), "빠진 창만 없어짐");

    // 만들기 실패: 다시 시도하지 않음. 렌더러가 거부하면 OS 창을 남기지 않음.
    log.lines.clear();
    failCreate = true;
    want.push_back({ 7, { 10, 50, 300, 200 }, "Details", HNew(SBorder) });
    fw.sync(want);
    fw.sync(want);
    check(!fw.find(7) && std::count(log.lines.begin(), log.lines.end(), std::string("createWindow:Details:hidden")) == 1,
          "OS 창을 못 만든 번호는 이번 실행 동안 다시 시도하지 않음");
    failCreate = false;
    refuseTarget = true;
    log.lines.clear();
    want.push_back({ 8, { 10, 50, 300, 200 }, "Log", HNew(SBorder) });
    fw.sync(want);
    check(!fw.find(8) && log.lines.size() == 3 && log.lines[1] == "addTarget:refused" &&
          log.lines[2] == "destroyWindow:" + std::to_string(made.size()),
          "렌더러가 거부하면 만든 OS 창을 바로 없앰(대상 없음 → removeTarget 없음)");
    refuseTarget = false;

    // 모두 없애기(에디터 정리): 남은 창도 같은 순서.
    log.lines.clear();
    fw.destroyAll();
    check(fw.windows().empty() && log.lines.size() == 2 && log.lines[0] == "removeTarget:11" && log.lines[1] == "destroyWindow:2",
          "destroyAll: 렌더러 대상 → OS 창");
}

// 도크 창 단위: 패널 위젯 하나는 언제나 한 루트에만(메인 호스트 · 떠 있는 창 호스트들). 창 닫기(D4) · 다시 열기 · 배치 다시 읽기 뒤에도.
void testDockPanelOnePlace() {
    std::printf("\n-- M3a: 도크 패널은 한 루트에만 --\n");
    using namespace m3a;
    const char* path = "m3a_dock_test_layout.txt";
    {
        std::ofstream f(path, std::ios::binary);
        if (!f) { check(false, "시험 배치 파일을 쓸 수 없음"); return; }
        f << "# Haruka Dock Layout v1\nS V 2 0.72 0.28\nL 0 1 Viewport\nL 0 1 Bottom\n"
             "# floating windows\nF 1 100 100 360 600 0\nL 1 2 Details Outliner\nF 4 50 50 300 300 0\nL 0 1 Stats\n";
    }
    EditorUiKit   ui;
    PanelRegistry reg;
    std::map<std::string, std::shared_ptr<HWidget>> panels;
    for (const char* id : { "Viewport", "Outliner", "Details", "Bottom", "Stats" }) {
        panels[id] = HNew(SBorder);
        reg.add(id, id, panels[id], std::string(id) != "Viewport", std::string(id) != "Viewport");
    }
    const std::string layoutPath = path;
    int raised = 0, raisedId = 0;
    bool outlinerVis = false, detailsVis = false;
    EditorDock dock;
    EditorDock::Deps d;
    d.ui = &ui;
    d.panelReg = &reg;
    d.uiLayoutOverride = &layoutPath;
    d.onVisibilityChanged = [&](bool o, bool, bool det) { outlinerVis = o; detailsVis = det; };
    d.onRaiseFloat = [&](int id) { ++raised; raisedId = id; };
    dock.init(d);
    auto mainHost = dock.buildHost();
    check(dock.loadLayout() && dock.floats().size() == 2, "F 블록 두 개 읽음");
    dock.rebuildArea();

    auto roots = [&] {
        std::vector<std::shared_ptr<HWidget>> r{ mainHost };
        for (const auto& f : dock.floats()) r.push_back(dock.floatHost(f.id));
        return r;
    };
    // 시험이 OS 창처럼 떠 있는 창 호스트를 들고 있습니다(창은 프레임 끝까지 루트를 들고 있음).
    std::vector<std::shared_ptr<HWidget>> held = roots();
    auto countAll = [&](const std::string& id) {
        int n = 0;
        for (const auto& r : held) countIn(r, panels[id].get(), n);
        for (const auto& r : roots()) {
            if (std::find(held.begin(), held.end(), r) == held.end()) countIn(r, panels[id].get(), n);
        }
        return n;
    };
    auto allOnce = [&](std::initializer_list<const char*> shown, std::initializer_list<const char*> hidden) {
        bool ok = true;
        for (const char* id : shown)  ok = ok && countAll(id) == 1;
        for (const char* id : hidden) ok = ok && countAll(id) == 0;
        return ok;
    };
    check(dock.floatHost(1) && dock.floatHost(4) && dock.floatHost(1) != dock.floatHost(4), "떠 있는 창마다 호스트 위젯");
    check(dock.windowStateCount() == 3, "창 상태 = 메인 + 떠 있는 창 둘");
    check(allOnce({ "Viewport", "Bottom", "Outliner", "Stats" }, { "Details" }),
          "읽은 직후: 활성 탭 패널은 한 번씩, 가려진 탭(Details)은 없음");
    check(outlinerVis && !detailsVis, "보임 여부는 떠 있는 창의 활성 탭도 셈");
    check(dock.floatTitle(1) == "Outliner" && dock.floatTitle(4) == "Stats", "창 제목 = 활성 탭 제목");
    check(dock.panelActiveInMain("Bottom") && !dock.panelActiveInMain("Outliner"), "panelActiveInMain 은 메인만");
    check(dock.floatOfPanel("Details") == 1 && dock.floatOfPanel("Bottom") == 0, "floatOfPanel");

    // 같은 창의 다른 탭 열기(Window 메뉴): 그 창 안에서 활성 탭을 바꾸고 창을 앞으로.
    std::shared_ptr<HWidget> host1 = dock.floatHost(1);
    dock.showPanel("Details");
    check(raised == 1 && raisedId == 1 && dock.floatHost(1) == host1, "떠 있는 창의 패널 열기 → 그 창을 앞으로(호스트 그대로)");
    check(allOnce({ "Viewport", "Bottom", "Details", "Stats" }, { "Outliner" }) && dock.floatTitle(1) == "Details",
          "탭 바꿈 뒤에도 한 번씩 · 제목 따라감");

    // OS 닫기(D4): 안의 탭을 모두 닫음 → 호스트는 비고, 패널은 어느 루트에도 없음.
    check(dock.closeFloat(1) && dock.floats().size() == 1 && !dock.floatHost(1), "closeFloat: 창이 목록에서 빠짐");
    int inOld = 0;
    countIn(host1, panels["Details"].get(), inOld);
    check(inOld == 0, "닫힌 창의 호스트(OS 창이 아직 들고 있음)에는 패널이 남지 않음");
    check(allOnce({ "Viewport", "Bottom", "Stats" }, { "Details", "Outliner" }) && !detailsVis && !outlinerVis,
          "닫은 창의 패널은 어디에도 없음 · 보임 꺼짐");

    // Window 메뉴로 다시 열면 메인으로(D4: 원래 자리).
    dock.showPanel("Details");
    check(dock.panelActiveInMain("Details") && allOnce({ "Viewport", "Details", "Stats" }, { "Outliner" }),
          "다시 열기 → 메인에 한 번");
    int inOld2 = 0;
    countIn(host1, panels["Details"].get(), inOld2);
    check(inOld2 == 0, "옛 창 호스트에는 여전히 없음");

    // 배치 다시 읽기: Details 가 메인에서 떠 있는 창으로 돌아감 — 메인 트리에는 남지 않아야 함.
    check(dock.loadLayout(), "배치 다시 읽기");
    dock.rebuildArea();
    held = roots();
    held.push_back(host1);
    check(dock.floats().size() == 2 && !dock.panelActiveInMain("Details"), "다시 읽은 뒤 Details 는 떠 있는 창");
    check(allOnce({ "Viewport", "Bottom", "Outliner", "Stats" }, { "Details" }), "다시 읽은 뒤에도 한 번씩(옛 호스트 포함)");
    std::remove(path);
}

// ── M3b: 창별 입력 분배 ──────────────────────────────────────────────────────────────────────────────
// 순수 규칙: 누름 엣지(창마다) · 키보드 창 고르기 · 글 칸 · 패널 포인터 · D7 · D5 · 창 사이 좌표.
void testWindowInputRules() {
    std::printf("\n-- M3b: 창별 입력 규칙 --\n");
    using namespace hwininput;
    PointerSample s;
    s.pos = { 10.f, 20.f };
    s.left = true;
    PointerFrame f = makeFrame(s, false, false);
    check(f.leftPressed && !f.leftReleased && f.left && f.pos == glm::vec2(10.f, 20.f), "누름 엣지: 직전 뗌 → 누름");
    f = makeFrame(s, true, false);
    check(!f.leftPressed && !f.leftReleased, "누른 채: 엣지 없음");
    s.left = false;
    s.right = true;
    f = makeFrame(s, true, false);
    check(f.leftReleased && !f.leftPressed && f.rightPressed, "뗌 엣지 · 오른쪽 누름 엣지");
    f = makeFrame(s, false, true);
    check(!f.rightPressed && !f.leftReleased, "오른쪽 누른 채: 엣지 없음");

    check(keyboardWindow({}) == 0, "키보드 창: 떠 있는 창이 없으면 메인");
    check(keyboardWindow({ { 1, false, false }, { 4, false, false } }) == 0, "키보드 창: 포커스 있는 떠 있는 창이 없으면 메인(숨은 실행)");
    check(keyboardWindow({ { 1, false, false }, { 4, true, false } }) == 4, "키보드 창: 포커스 있는 떠 있는 창");
    check(keyboardWindow({ { 1, true, true }, { 4, false, false } }) == 0, "키보드 창: 최소화된 창은 받지 않음");
    check(keyboardWindow({ { 2, true, false }, { 4, true, false } }) == 2, "키보드 창: 둘이면 목록 순서로 처음 것");
    check(keyboardWindow({ { 0, true, false } }) == 0, "키보드 창: 번호 0(메인 자리)은 떠 있는 창으로 치지 않음");

    check(fieldTakesKeys(0, 0) && fieldTakesKeys(3, 3) && !fieldTakesKeys(3, 0) && !fieldTakesKeys(0, 3),
          "글 칸은 그 칸이 있는 창에 키보드가 있을 때만");
    check(panelGetsPointer(0, 0) && panelGetsPointer(2, 2) && !panelGetsPointer(2, 0) && !panelGetsPointer(0, 2),
          "패널 포인터 판정은 그 패널이 있는 창의 포인터로만");
    check(!blocksGameInput(false, 3) && !blocksGameInput(true, 0) && blocksGameInput(true, 3),
          "D7: Play 중 떠 있는 창에 키보드가 있을 때만 게임 입력을 막음");
    check(floatTakesInput(false, false) && !floatTakesInput(true, false) && !floatTakesInput(false, true),
          "D5: 모달이 떠 있거나 최소화된 떠 있는 창은 입력을 받지 않음");
    check(toWindow({ 10.f, 20.f }, { 100, 100 }, { 0, 0 }) == glm::vec2(110.f, 120.f), "창 사이 좌표: 떠 있는 창 → 메인(원점 0,0)");
    check(toWindow({ 10.f, 20.f }, { 100, 100 }, { 300, 50 }) == glm::vec2(-190.f, 70.f), "창 사이 좌표: 메인이 오른쪽에 있으면 음수 x");
    check(toWindow({ 5.f, 5.f }, { 7, 9 }, { 7, 9 }) == glm::vec2(5.f, 5.f), "창 사이 좌표: 같은 원점이면 그대로");
}

// 시험 입력 대본(--ui-float-input) 읽기 · 적용.
void testFloatScript() {
    std::printf("\n-- M3b: 떠 있는 창 입력 대본 --\n");
    using namespace hwininput;
    using K = ScriptStep::Kind;
    std::vector<ScriptStep> st;
    std::string err;
    check(parseScript("3:click@60,105;wait;3:wheel@300,200=-1;3:drag@0,0>10,20;2:focus;2:rdown@1.5,2;2:rup@1.5,2", st, err) && err.empty(),
          "대본 읽기 성공");
    check(st.size() == 11, "click 2 + wait 1 + wheel 1 + drag 4 + focus 1 + rdown 1 + rup 1 = 11 단계");
    check(st[0].kind == K::Down && st[1].kind == K::Up && st[0].window == 3 && st[0].pos == glm::vec2(60.f, 105.f), "click = down · up(같은 자리)");
    check(st[2].kind == K::Wait, "wait");
    check(st[3].kind == K::Wheel && st[3].value == -1.f && st[3].pos == glm::vec2(300.f, 200.f), "wheel 자리 · 값");
    check(st[4].kind == K::Down && st[5].kind == K::Move && st[5].pos == glm::vec2(5.f, 10.f) && st[6].pos == glm::vec2(10.f, 20.f) &&
          st[7].kind == K::Up, "drag = down · 가운데 move · 끝 move · up");
    check(st[8].kind == K::Focus && st[8].window == 2 && !st[8].hasPos, "focus(자리 없음)");
    check(st[9].kind == K::RDown && st[9].pos == glm::vec2(1.5f, 2.f) && st[10].kind == K::RUp, "rdown · rup(소수 좌표)");

    for (const char* bad : { "click@1,2", "-1:click@1,2", "x:click@1,2", "3:click", "3:click@1", "3:jump@1,2", "3:wheel@1,2",
                             "3:drag@1,2", "3:focus@1,2", "3:click@1,2x" }) {
        std::vector<ScriptStep> v;
        std::string e;
        check(!parseScript(bad, v, e) && e == bad, std::string("틀린 단계 거부: ") + bad);
    }
    // M4: 창 0 = 메인, place(창 자리 옮기기), 창 밖(음수) 좌표.
    {
        std::vector<ScriptStep> v;
        std::string e;
        check(parseScript("0:drag@10,20>-300,40;2:place@-50,60;0:focus", v, e) && v.size() == 6, "M4: 메인(0) 끌기 · place · 메인 focus = 6 단계");
        check(v[0].window == 0 && v[0].kind == K::Down && v[3].kind == K::Up && v[3].pos == glm::vec2(-300.f, 40.f),
              "M4: 메인 끌기, 창 밖(음수) 좌표");
        check(v[4].kind == K::Place && v[4].window == 2 && v[4].hasPos && v[4].pos == glm::vec2(-50.f, 60.f), "M4: place 자리(화면 좌표)");
        check(v[5].kind == K::Focus && v[5].window == 0, "M4: 메인 focus");
        PointerSample ps;
        ps.pos = { 1.f, 2.f };
        bool fo = false;
        applyStep(v[4], ps, fo);
        check(ps.pos == glm::vec2(1.f, 2.f) && !ps.left && !fo, "M4: place 는 포인터 값을 바꾸지 않음");
        std::vector<ScriptStep> v2;
        std::string e2;
        check(!parseScript("2:place@1", v2, e2) && e2 == "2:place@1" && !parseScript("2:place", v2, e2), "M4: 자리 없는 place 거부");
    }
    {
        std::vector<ScriptStep> v;
        std::string e;
        check(!parseScript("3:move@1,1;3:nope@2,2;3:up@1,1", v, e) && v.size() == 1 && e == "3:nope@2,2",
              "틀린 단계에서 멈춤(앞 단계는 남음)");
        check(parseScript(";;3:move@1,1;", v, e) && v.size() == 2, "빈 단계는 건너뜀");
    }

    // 적용: 버튼 · 자리는 남고, 휠은 그 단계에만(부르는 쪽이 프레임마다 0 으로).
    PointerSample s;
    bool focused = false;
    applyStep(st[0], s, focused);
    check(s.left && s.pos == glm::vec2(60.f, 105.f), "down: 누름 · 자리");
    applyStep(st[2], s, focused);
    check(s.left && s.pos == glm::vec2(60.f, 105.f), "wait: 그대로");
    applyStep(st[3], s, focused);
    check(s.left && s.scroll == -1.f && s.pos == glm::vec2(300.f, 200.f), "wheel: 휠 값 · 자리, 버튼은 그대로");
    applyStep(st[1], s, focused);
    check(!s.left, "up: 뗌");
    applyStep(st[8], s, focused);
    check(focused && s.pos == glm::vec2(60.f, 105.f), "focus: 포커스만, 자리 그대로");
    applyStep(st[9], s, focused);
    check(s.right, "rdown");
    applyStep({ 2, K::Unfocus }, s, focused);
    check(!focused, "unfocus");
}

namespace m3b {
// 커서 모양을 기록하는 가짜 창.
class CursorWin : public m3a::FakeWin {
public:
    using m3a::FakeWin::FakeWin;
    void setCursorShape(int s) override { cursor = s; ++cursorCalls; }
    int cursor = -1;
    int cursorCalls = 0;
};
}  // namespace m3b

// 떠 있는 창의 스플리터 · 탭 입력은 그 창 상태로(메인 상태 · 메인 트리 · 메인 미리보기는 그대로).
void testDockFloatInput() {
    std::printf("\n-- M3b: 떠 있는 창의 스플리터 · 탭 --\n");
    using namespace m3a;
    const char* path = "m3b_dock_input_layout.txt";
    {
        std::ofstream f(path, std::ios::binary);
        if (!f) { check(false, "시험 배치 파일을 쓸 수 없음"); return; }
        f << "# Haruka Dock Layout v1\nS V 2 0.6 0.4\nL 0 1 Viewport\nL 0 1 Bottom\n"
             "# floating windows\nF 3 100 100 600 700 0\nS V 2 0.5 0.5\nL 0 1 Outliner\nL 0 2 Details Stats\n";
    }
    EditorUiKit   ui;
    PanelRegistry reg;
    std::map<std::string, std::shared_ptr<HWidget>> panels;
    for (const char* id : { "Viewport", "Outliner", "Details", "Bottom", "Stats" }) {
        panels[id] = HNew(SBorder);
        // 닫기 단추가 없게(탭 줄을 훑어 누를 때 x 를 누르지 않게) 모두 닫을 수 없는 패널로 둡니다.
        reg.add(id, id, panels[id], /*closable=*/false, std::string(id) != "Viewport");
    }
    const std::string layoutPath = path;
    EditorDock dock;
    EditorDock::Deps d;
    d.ui = &ui;
    d.panelReg = &reg;
    d.uiLayoutOverride = &layoutPath;
    int cancels = 0;
    d.onCancelOutlinerDrag = [&] { ++cancels; };
    dock.init(d);
    auto mainHost    = dock.buildHost();
    auto mainOverlay = dock.resetOverlay();
    check(dock.loadLayout() && dock.floats().size() == 1, "F 블록 하나(창 3) 읽음");
    dock.rebuildArea();
    auto froot = dock.floatRoot(3);
    check(froot && froot != dock.floatHost(3), "떠 있는 창 루트 = 호스트 위 미리보기 층(SOverlay)");
    std::shared_ptr<SDockOverlay> fOverlay;
    int kids = 0;
    froot->forEachChild([&](const std::shared_ptr<HWidget>& c) {
        if (kids++ == 1) fOverlay = std::dynamic_pointer_cast<SDockOverlay>(c);
    });
    check(kids == 3 && fOverlay && dock.floatDropOverlay(3) && dock.floatDropOverlay(3) != fOverlay,
          "루트 자식: 호스트 · 탭 끌기 미리보기 · 아웃라이너 미리보기");

    HSlateContext mctx, fctx;
    mctx.setRootWidget(mainHost);
    fctx.setRootWidget(froot);
    auto arrangeAll = [&] { mctx.arrange({ 1600.f, 900.f }); fctx.arrange({ 600.f, 700.f }); };
    arrangeAll();
    Log log;
    m3b::CursorWin mwin(&log, 0, 1600, 900), fwin(&log, 3, 600, 700);
    auto treeText = [&] {
        std::ostringstream o;
        for (const auto& f : dock.floats()) dockSerialize(f.root.get(), o);
        return o.str();
    };

    // 스플리터: 떠 있는 창 손잡이(y 347.5 ~ 352.5) 위 → 그 창 커서만 바뀜.
    check(!dock.handleSplitterInput(3, 300.f, 350.f, false, false, false, &fwin) && fwin.cursor == 2 && mwin.cursorCalls == 0,
          "손잡이 위 호버: 그 창 커서 = 세로 크기, 메인 창은 건드리지 않음");
    check(!dock.handleSplitterInput(0, 300.f, 350.f, true, false, false, &mwin) && !dock.isSplitterDragging(),
          "같은 좌표를 메인에서 누르면 떠 있는 창 손잡이가 아님(메인 손잡이도 아님)");
    const float outlinerH0 = panels["Outliner"]->cachedGeometry().size.y;
    check(dock.handleSplitterInput(3, 300.f, 350.f, true, false, false, &fwin) && dock.isSplitterDragging() && cancels == 1,
          "떠 있는 창 손잡이 누름 → 끌기 시작(아웃라이너 끌기 취소 알림)");
    check(dock.handleSplitterInput(3, 300.f, 250.f, true, true, false, &fwin), "끄는 중 소비");
    const float r0 = dock.floats()[0].root->ratios[0];
    check(std::abs(r0 - (0.5f - 100.f / 700.f)) < 0.002f, "떠 있는 창 비율: 0.5 - 100/700");
    arrangeAll();
    check(panels["Outliner"]->cachedGeometry().size.y < outlinerH0 - 90.f, "끄는 동안 그 창 배치가 따라옴(위젯 트리 그대로, 가중치만)");
    check(dock.handleSplitterInput(3, 300.f, 250.f, false, true, false, &fwin) && !dock.isSplitterDragging(), "놓으면 끌기 끝");

    // 탭: 아래 리프(Details · Stats)의 탭 줄을 훑어 누르면 그 창의 활성 탭이 바뀜.
    arrangeAll();
    const float tabY = panels["Details"]->cachedGeometry().position.y - 13.f;
    const std::string floatsBefore = treeText();
    float statsX = -1.f;
    // 누를 때마다 배치를 다시 합니다(탭 누르기가 위젯 트리를 다시 만들고, 에디터는 프레임마다 이벤트 전에 배치함).
    for (float x = 1.f; x < 600.f && statsX < 0.f; x += 3.f) {
        arrangeAll();
        dock.handleTabInput(3, x, tabY, true, false, false, &fwin);
        dock.handleTabInput(3, x, tabY, false, true, false, &fwin);
        if (dock.panelActiveIn(3, "Stats")) statsX = x;
    }
    check(statsX > 0.f && dock.panelActiveIn(3, "Stats") && !dock.panelActiveIn(3, "Details"), "떠 있는 창 탭 누르기 → 그 탭이 활성");
    check(dock.panelActiveInMain("Viewport") && dock.panelActiveInMain("Bottom") && !dock.panelActiveInMain("Stats"),
          "메인 트리는 그대로");

    // 메인 좌표로 같은 자리를 눌러도 떠 있는 창 탭은 바뀌지 않음.
    arrangeAll();
    for (float x = 1.f; x < statsX + 30.f; x += 3.f) {
        arrangeAll();
        dock.handleTabInput(0, x, tabY, true, false, false, &mwin);
        dock.handleTabInput(0, x, tabY, false, true, false, &mwin);
    }
    check(dock.panelActiveIn(3, "Stats"), "메인 창 입력은 떠 있는 창 탭을 바꾸지 않음");

    // 탭을 창 밖에 놓기(M4 전): 아무 일 없음. 끄는 동안 이 창의 미리보기만 켜지고 메인 미리보기는 꺼진 채.
    arrangeAll();
    const std::string beforeOutside = treeText();
    dock.handleTabInput(3, statsX, tabY, true, false, false, &fwin);
    dock.handleTabInput(3, statsX + 40.f, 150.f, true, true, false, &fwin);
    check(fOverlay->active && !mainOverlay->active, "창 안에서 끄는 동안: 그 창 미리보기만 켜짐");
    dock.handleTabInput(3, 900.f, 150.f, true, true, false, &fwin);
    check(!fOverlay->active && !mainOverlay->active, "창 밖으로 나가면 미리보기 꺼짐");
    dock.handleTabInput(3, 900.f, 150.f, false, true, false, &fwin);
    check(treeText() == beforeOutside && dock.floats().size() == 1, "창 밖에 놓기: 배치 그대로(M4 전, 메인에서와 같음)");

    // 같은 창 안 재도킹: Stats 를 Outliner 리프 오른쪽 가장자리에.
    arrangeAll();
    dock.handleTabInput(3, statsX, tabY, true, false, false, &fwin);
    dock.handleTabInput(3, 400.f, 200.f, true, true, false, &fwin);
    dock.handleTabInput(3, 580.f, 150.f, true, true, false, &fwin);
    check(fOverlay->active && fOverlay->rect.position.x >= 299.f, "오른쪽 가장자리: 미리보기는 오른쪽 반");
    dock.handleTabInput(3, 580.f, 150.f, false, true, false, &fwin);
    const DockNode* fr = dock.floats().empty() ? nullptr : dock.floats()[0].root.get();
    check(fr && !fr->isLeaf() && fr->children.size() == 2 && !fr->children[0]->isLeaf() &&
          fr->children[0]->split == DockNode::Split::Horizontal && dock.floatOfPanel("Stats") == 3 &&
          dock.floatOfPanel("Outliner") == 3 && treeText() != beforeOutside,
          "같은 창 재도킹: Outliner 리프가 가로 분할(Outliner | Stats), 패널은 모두 그 창");
    check(dock.panelActiveInMain("Viewport") && dock.panelActiveInMain("Bottom"), "재도킹 뒤에도 메인 트리 그대로");
    check(!fOverlay->active && !mainOverlay->active, "놓은 뒤 미리보기 꺼짐");
    (void)floatsBefore;
    std::remove(path);
}

// ── M4: 탭 떼기 · 붙이기(창을 넘는 탭 끌기) ──────────────────────────────────────────────────────────────────────
// EditorDock 에 화면 자리(dropWindows)를 주고, 메인 → 떠 있는 창 · 창 밖(새 창) · 거부(Viewport) · 떠 있는 창 → 메인 · 떠 있는 창 →
// 다른 떠 있는 창(마지막 탭 → 창 닫힘) · 창 통째 합치기 · 끄는 중 놓을 창이 닫힘을 차례로 흉내 냅니다.
void testDockCrossWindowDrag() {
    std::printf("\n-- M4: 창을 넘는 탭 끌기 --\n");
    using namespace m3a;
    const char* path = "m4_dock_cross_layout.txt";
    {
        std::ofstream f(path, std::ios::binary);
        if (!f) { check(false, "시험 배치 파일을 쓸 수 없음"); return; }
        f << "# Haruka Dock Layout v1\nS V 2 0.6 0.4\nL 0 1 Viewport\nL 0 1 Bottom\n"
             "# floating windows\nF 3 100 100 600 700 0\nS V 2 0.5 0.5\nL 0 1 Outliner\nL 0 2 Details Stats\n"
             "F 5 1700 100 400 300 0\nL 0 1 Log\n";
    }
    EditorUiKit   ui;
    PanelRegistry reg;
    std::map<std::string, std::shared_ptr<HWidget>> panels;
    for (const char* id : { "Viewport", "Outliner", "Details", "Bottom", "Stats", "Log" }) {
        panels[id] = HNew(SBorder);
        const bool vp = std::string(id) == "Viewport";
        reg.add(id, id, panels[id], /*closable=*/!vp, /*floatable=*/!vp);
    }
    const std::string layoutPath = path;
    std::map<int, uint64_t> front{ { 0, 1 }, { 3, 2 }, { 5, 3 } };
    uint64_t frontNext = 10;
    std::vector<int> raised;
    EditorDock dock;
    EditorDock::Deps d;
    d.ui = &ui;
    d.panelReg = &reg;
    d.uiLayoutOverride = &layoutPath;
    d.onRaiseFloat = [&](int id) { raised.push_back(id); front[id] = ++frontNext; };
    // 화면 자리: 메인 (0,0) 1600x900, 떠 있는 창은 배치 값 그대로(실제로는 OS 창에서 읽음). 처음 보는 창은 맨 앞.
    d.dropWindows = [&] {
        std::vector<hdockdrag::DropWindow> v;
        v.push_back({ 0, { 0, 0, 1600, 900 }, front[0], true });
        for (const DockFloat& f : dock.floats()) {
            if (!front.count(f.id)) front[f.id] = ++frontNext;
            v.push_back({ f.id, { f.x, f.y, f.w, f.h }, front[f.id], true });
        }
        return v;
    };
    d.clampFloatRect = [](hdockdrag::ScreenRect& r) {
        const hwinplace::Rect c = hwinplace::clampToWorkAreas({ r.x, r.y, r.w, r.h }, { { 0, 0, 1920, 1040 } });
        r = { c.x, c.y, c.w, c.h };
    };
    dock.init(d);
    auto mainHost    = dock.buildHost();
    auto mainOverlay = dock.resetOverlay();
    check(dock.loadLayout() && dock.floats().size() == 2, "F 블록 둘(창 3 · 5) 읽음");
    dock.rebuildArea();

    HSlateContext mctx;
    mctx.setRootWidget(mainHost);
    std::map<int, std::unique_ptr<HSlateContext>> fctx;
    Log log;
    std::map<int, std::unique_ptr<m3b::CursorWin>> wins;
    wins[0] = std::make_unique<m3b::CursorWin>(&log, 0, 1600, 900);
    auto arrangeAll = [&] {
        mctx.arrange({ 1600.f, 900.f });
        for (const DockFloat& f : dock.floats()) {
            auto& c = fctx[f.id];
            if (!c) c = std::make_unique<HSlateContext>();
            if (c->rootWidget() != dock.floatRoot(f.id)) c->setRootWidget(dock.floatRoot(f.id));
            c->arrange({ (float)f.w, (float)f.h });
            if (!wins[f.id]) wins[f.id] = std::make_unique<m3b::CursorWin>(&log, f.id, f.w, f.h);
        }
    };
    auto step = [&](int win, glm::vec2 p, bool down, bool prev) {
        arrangeAll();
        return dock.handleTabInput(win, p.x, p.y, down, prev, false, wins[win].get());
    };
    auto overlayOn = [&](int id) {
        if (id == 0) return mainOverlay->active;
        auto r = dock.floatRoot(id);
        bool on = false;
        int k = 0;
        if (r) r->forEachChild([&](const std::shared_ptr<HWidget>& c) {
            if (k++ == 1) if (auto o = std::dynamic_pointer_cast<SDockOverlay>(c)) on = o->active;
        });
        return on;
    };
    auto anyOverlay = [&] {
        bool on = overlayOn(0);
        for (const DockFloat& f : dock.floats()) on = on || overlayOn(f.id);
        return on;
    };
    auto tabAt = [&](const char* id) {   // 활성 탭인 패널의 탭 줄 왼쪽(탭 글자 위)
        arrangeAll();
        const HGeometry& g = panels[id]->cachedGeometry();
        return glm::vec2{ g.position.x + 20.f, g.position.y - 13.f };
    };

    // A. 메인 → 떠 있는 창: Bottom 탭을 창 5(메인 오른쪽 밖, 1700,100) 가운데에.
    {
        const glm::vec2 p = tabAt("Bottom");
        check(step(0, p, true, false), "A: 메인 Bottom 탭 누름");
        check(step(0, { 1900.f, 250.f }, true, true) && overlayOn(5) && !overlayOn(0) && !overlayOn(3),
              "A: 창 5 위 → 미리보기는 창 5 에만(메인 좌표 1900,250 = 화면 좌표)");
        check(wins[0]->cursor == 0, "A: 리프 위 커서 = 화살표");
        const int splitterCalls = wins[5]->cursorCalls;
        dock.handleSplitterInput(5, 10.f, 10.f, false, false, false, wins[5].get());
        check(wins[5]->cursorCalls == splitterCalls, "A: 끄는 동안 다른 창은 커서 모양을 바꾸지 않음");
        check(step(0, { 1900.f, 250.f }, false, true), "A: 놓음");
        check(dock.floatOfPanel("Bottom") == 5 && dock.panelActiveIn(5, "Bottom") && !dock.contains("Nope") &&
              dock.panelActiveInMain("Viewport") && !dock.panelActiveInMain("Bottom"), "A: Bottom 은 창 5 의 탭(가운데), 메인에서 빠짐");
        check(!raised.empty() && raised.back() == 5, "A: 놓은 창(5)을 앞으로");
        check(!anyOverlay(), "A: 놓은 뒤 미리보기 모두 꺼짐");
    }
    // B. 떠 있는 창 → 창 밖(새 창): 창 3 의 Outliner 탭을 화면 (-100,400) 에.
    {
        const glm::vec2 p = tabAt("Outliner");
        check(step(3, p, true, false), "B: 창 3 Outliner 탭 누름");
        step(3, { -200.f, 300.f }, true, true);
        check(!anyOverlay() && wins[3]->cursor == 3, "B: 창 밖 → 미리보기 없음 · 커서 = 이동(새 창)");
        step(3, { -200.f, 300.f }, false, true);
        check(wins[3]->cursor == 0, "B: 놓으면 커서 화살표");
        const DockFloat* nf = nullptr;
        for (const DockFloat& f : dock.floats())
            if (f.id == 6) nf = &f;
        check(dock.floats().size() == 3 && nf && dock.floatOfPanel("Outliner") == 6, "B: 새 떠 있는 창 6(번호 = 가장 큰 번호 + 1)에 Outliner");
        check(nf && nf->x == -120 && nf->y == 387 && nf->w == 600 && nf->h >= 330 && nf->h <= 350,
              "B: 새 창 자리 = 커서 - 잡은 자리(20,13), 크기 = 원래 리프(600 x 약 347), 제목 줄이 보여 끼우지 않음");
        check(dock.floatOfPanel("Details") == 3 && dock.floatRoot(6) != nullptr, "B: 원래 창 3 은 남은 탭으로 그대로, 새 창 루트 있음");
    }
    // C. 떼기 거부(D8): Viewport 를 창 밖 · 떠 있는 창에.
    {
        const glm::vec2 p = tabAt("Viewport");
        const size_t n = dock.floats().size();
        check(step(0, p, true, false), "C: Viewport 탭 누름");
        step(0, { 1650.f, 950.f }, true, true);
        check(wins[0]->cursor == 4 && !anyOverlay(), "C: 창 밖 → 커서 = 금지, 미리보기 없음");
        step(0, { 1900.f, 250.f }, true, true);
        check(wins[0]->cursor == 4 && !anyOverlay(), "C: 떠 있는 창 위 → 커서 = 금지, 미리보기 없음");
        step(0, { 1650.f, 950.f }, false, true);
        check(dock.floats().size() == n && dock.panelActiveInMain("Viewport") && dock.floatOfPanel("Viewport") == 0,
              "C: 놓아도 Viewport 는 메인, 떠 있는 창 수 그대로");
    }
    // D. 떠 있는 창 → 메인: 창 3 의 Details 를 메인 오른쪽 가장자리(화면 1500,400)에.
    {
        const glm::vec2 p = tabAt("Details");
        step(3, p, true, false);
        step(3, { 1400.f, 300.f }, true, true);
        check(overlayOn(0) && !overlayOn(3) && mainOverlay->rect.position.x > 700.f, "D: 메인 위 → 메인 미리보기(오른쪽 반)");
        step(3, { 1400.f, 300.f }, false, true);
        check(dock.panelActiveInMain("Details") && dock.floatOfPanel("Details") == 0 && dock.floatOfPanel("Stats") == 3,
              "D: Details 는 메인(오른쪽 분할), Stats 는 창 3 에 남음");
        check(raised.back() == 0, "D: 메인을 앞으로");
    }
    // E. 떠 있는 창 → 다른 떠 있는 창, 마지막 탭: 창 3 의 Stats 를 창 6(메인 밖 부분, 화면 -60,560)에 → 창 3 닫힘.
    {
        const glm::vec2 p = tabAt("Stats");
        step(3, p, true, false);
        step(3, { -160.f, 460.f }, true, true);
        check(overlayOn(6) && !overlayOn(0), "E: 창 6 위(메인과 겹치지 않는 곳) → 창 6 미리보기");
        step(3, { -160.f, 460.f }, false, true);
        check(dock.floatOfPanel("Stats") == 6 && dock.floats().size() == 2 && dock.floatRoot(3) == nullptr,
              "E: Stats 는 창 6, 빈 창 3 은 목록에서 빠지고 창 상태도 없어짐");
        check(raised.back() == 6 && !anyOverlay(), "E: 창 6 을 앞으로, 미리보기 꺼짐");
    }
    // F. 창 통째 합치기: 창 6 의 탭 줄 빈 곳을 끌어 메인 Viewport 에.
    {
        arrangeAll();
        const HGeometry& sg = panels["Stats"]->cachedGeometry();
        const glm::vec2 empty{ sg.position.x + sg.size.x - 20.f, sg.position.y - 13.f };
        check(step(6, empty, true, false), "F: 창 6 탭 줄 빈 곳 누름 = 통째 끌기 시작(소비)");
        step(6, empty + glm::vec2(2.f, 0.f), false, true);
        check(dock.floats().size() == 2 && dock.floatOfPanel("Stats") == 6, "F: 끌지 않고 떼면 아무 일 없음");
        step(6, empty, true, false);
        step(6, { 420.f, -187.f }, true, true);   // 화면 300,200 = 메인 Viewport 위
        check(overlayOn(0) && !overlayOn(6), "F: 메인 위 → 메인 미리보기");
        step(6, { 420.f, -187.f }, false, true);
        check(dock.floats().size() == 1 && dock.floatOfPanel("Stats") == 0 && dock.floatOfPanel("Outliner") == 0 &&
              dock.contains("Stats") && dock.contains("Outliner") && dock.floatRoot(6) == nullptr,
              "F: 창 6 의 탭이 모두 메인으로, 창 6 은 빠짐");
    }
    // G. 끄는 중 놓을 창이 닫힘: 메인 Details 를 창 5 위로 끄는 중 창 5 를 OS 닫기(D4) → 놓아도 아무 일 없음(새 창도 안 생김).
    {
        const glm::vec2 p = tabAt("Details");
        step(0, p, true, false);
        step(0, { 1900.f, 250.f }, true, true);
        check(overlayOn(5), "G: 창 5 위 미리보기");
        check(dock.closeFloat(5) && dock.floats().empty(), "G: 끄는 중 창 5 닫기(안의 탭 닫음)");
        check(!mainOverlay->active, "G: 미리보기 꺼짐");
        step(0, { 1900.f, 250.f }, false, true);
        check(dock.floats().empty() && dock.panelActiveInMain("Details"), "G: 움직이지 않고 놓으면 아무 일 없음(사라진 창 자리는 새 창이 아님)");
    }
    std::remove(path);
}

// 떠 있는 창 문맥의 팝업 층(H2a): 만들 때 setupContext(팝업 훅), 창이 없어질 때 그 창 팝업을 닫아 소유자가 상태를 비움.
void testFloatWindowPopups() {
    std::printf("\n-- H2a: 떠 있는 창 팝업 층 수명 --\n");
    using namespace m3a;
    Log log;
    EditorFloatWindows fw;
    EditorFloatWindows::Ops ops;
    ops.createWindow = [&](int w, int h, const std::string&, bool) -> std::unique_ptr<IWindow> {
        return std::make_unique<FakeWin>(&log, 1, w, h);
    };
    ops.addTarget    = [](IWindow*) -> uint32_t { return 7u; };
    ops.removeTarget = [&](uint32_t) { log.lines.push_back("removeTarget"); };
    std::vector<int> setups;
    ops.setupContext = [&](int id, HSlateContext& c) {
        setups.push_back(id);
        c.popups().setPushHook([&log](HPopupLayer::Handle) { log.lines.push_back("pushHook"); });
    };
    fw.init(std::move(ops), true);
    auto root = HNew(SBorder);
    fw.sync({ { 2, { 0, 0, 300, 200 }, "Details", root } });
    EditorWindow* w = fw.find(2);
    check(w && w->ctx && setups == std::vector<int>{ 2 }, "새 창 문맥을 만든 직후 setupContext 를 한 번 부름");
    int dismissed = 0;
    HPopupLayer::Spec s;
    s.content = HNew(SBox)->widthOverride(20.f)->heightOverride(20.f);
    s.onDismissed = [&] { ++dismissed; log.lines.push_back("dismissed"); };
    const auto h = w ? w->ctx->pushPopup(std::move(s)) : 0;
    check(h != 0 && !log.lines.empty() && log.lines.back() == "pushHook", "그 창 문맥에 팝업을 넣으면 훅이 불림(다른 창 팝업 닫기 자리)");
    fw.sync({ { 2, { 0, 0, 300, 200 }, "Details", root } });
    check(dismissed == 0 && setups.size() == 1, "남은 창은 다시 만들지 않고 팝업도 그대로");
    log.lines.clear();
    fw.sync({});
    const auto at = [&](const char* k) { return std::find(log.lines.begin(), log.lines.end(), k) - log.lines.begin(); };
    check(dismissed == 1 && at("dismissed") > at("removeTarget"), "창이 없어지면 그 창 팝업을 닫음(렌더러 대상을 뺀 뒤, 문맥을 버리기 전)");
}

// ── H2a: 문맥별 팝업 층 · SMenuAnchor ───────────────────────────────────────────────────────────────────
// 팝업 네 종류(메뉴 드롭다운 · 오른쪽 메뉴 · Add Component · 로그 Categories)가 각자 하던 호스트 · 바깥 누름 · 위치 계산을
// HSlateContext 의 팝업 층(HPopupLayer)으로 옮겼습니다. 위치 식은 예전 식과 1px 도 달라지면 안 되므로(덤프 기준) 예전 식을 여기에
// 그대로 적어 두고 비교합니다.
namespace h2a {

// 고정 크기 팝업 내용: 누름 · 뗌 · 휠 · 이동을 세고, 색 사각형 하나를 그립니다(그리기 순서 확인).
struct PopupBox : HWidget {
    glm::vec2 sz;
    glm::vec4 color;
    int down = 0, up = 0, wheel = 0, move = 0;
    bool handles = true;
    std::function<void()> onPress;   // 누를 때(전달 중) 부름: 자기 팝업 닫기 시험
    PopupBox(glm::vec2 s, glm::vec4 c) : sz(s), color(c) {}
    glm::vec2 computeDesiredSize() override { return sz; }
    void paint(HPaintContext& ctx, const HGeometry& g) override { ctx.drawRect(g, color); }
    HReply onMouseButton(const HPointerEvent& e) override {
        if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
        if (e.down) ++down; else ++up;
        if (e.down && onPress) onPress();
        return handles ? HReply::Handled() : HReply::Unhandled();
    }
    HReply onScroll(const HPointerEvent&) override { ++wheel; return HReply::Handled(); }
    HReply onMouseMove(const HPointerEvent&) override { ++move; return HReply::Unhandled(); }
};

// 루트: 화면 전체를 덮는 탐침(누름 · 휠이 루트까지 왔는지).
struct RootBox : HWidget {
    int down = 0, up = 0, wheel = 0, move = 0;
    glm::vec2 computeDesiredSize() override { return { 0.f, 0.f }; }
    void paint(HPaintContext& ctx, const HGeometry& g) override { ctx.drawRect(g, glm::vec4(0.1f, 0.1f, 0.1f, 1.f)); }
    HReply onMouseButton(const HPointerEvent& e) override { if (e.down) ++down; else ++up; return HReply::Handled(); }
    HReply onScroll(const HPointerEvent&) override { ++wheel; return HReply::Handled(); }
    HReply onMouseMove(const HPointerEvent&) override { ++move; return HReply::Unhandled(); }
};

HPointerEvent btn(glm::vec2 p, int b, bool down) { HPointerEvent e; e.position = p; e.button = b; e.down = down; return e; }
HPointerEvent wheelAt(glm::vec2 p) { HPointerEvent e; e.position = p; e.wheelDelta = 1.f; return e; }
HPointerEvent moveAt(glm::vec2 p, bool down) { HPointerEvent e; e.position = p; e.button = -1; e.down = down; return e; }

// 예전 식(2026-11-21 의 ComponentPicker::rebuildPanel · OutputLogPanel::positionPopupPanel · EditorContextMenu::rebuild).
glm::vec2 oldPicker(const HGeometry& host, const HGeometry& btn, float panelW, float panelH, float gap) {
    float top = btn.position.y + btn.size.y + gap;
    if (top + panelH > host.position.y + host.size.y) top = btn.position.y - panelH - gap;
    top = std::clamp(top, host.position.y, std::max(host.position.y, host.position.y + host.size.y - panelH));
    const float left = std::clamp(btn.position.x + (btn.size.x - panelW) * 0.5f, host.position.x,
                                  std::max(host.position.x, host.position.x + host.size.x - panelW));
    return { left, top };
}
glm::vec2 oldCategories(const HGeometry& host, const HGeometry& btn, float panelW, float panelH, float gap, float menuBarH) {
    const bool  up   = (btn.position.y + btn.size.y * 0.5f) > (host.position.y + host.size.y * 0.5f);
    float       top  = up ? btn.position.y - panelH - gap : btn.position.y + btn.size.y + gap;
    const float minT = host.position.y + menuBarH;
    top = std::clamp(top, minT, std::max(minT, host.position.y + host.size.y - panelH));
    const float left = std::clamp(btn.position.x, host.position.x,
                                  std::max(host.position.x, host.position.x + host.size.x - panelW));
    return { left, top };
}
glm::vec2 oldContext(const HGeometry& host, glm::vec2 at, float panelW, float panelH) {
    float left = at.x, top = at.y;
    if (left + panelW > host.position.x + host.size.x) left = at.x - panelW;
    if (top + panelH > host.position.y + host.size.y) top = at.y - panelH;
    left = std::clamp(left, host.position.x, std::max(host.position.x, host.position.x + host.size.x - panelW));
    top  = std::clamp(top,  host.position.y, std::max(host.position.y, host.position.y + host.size.y - panelH));
    return { left, top };
}

HPopupPlacement pickerPl(float gap) {
    HPopupPlacement p; p.side = HPopupPlacement::Side::Below; p.align = HPopupPlacement::Align::Center;
    p.flip = HPopupPlacement::Flip::Overflow; p.clamp = true; p.gap = gap; return p;
}
HPopupPlacement categoriesPl(float gap, float inset) {
    HPopupPlacement p; p.side = HPopupPlacement::Side::Below; p.align = HPopupPlacement::Align::Start;
    p.flip = HPopupPlacement::Flip::Half; p.clamp = true; p.gap = gap; p.insetTop = inset; return p;
}
HPopupPlacement contextPl() {
    HPopupPlacement p; p.side = HPopupPlacement::Side::Right; p.flip = HPopupPlacement::Flip::Overflow; p.clamp = true; return p;
}
HPopupPlacement menuPl() {
    HPopupPlacement p; p.side = HPopupPlacement::Side::Below; p.align = HPopupPlacement::Align::Start;
    p.flip = HPopupPlacement::Flip::None; p.clamp = false; return p;
}

HGeometry geo(float x, float y, float w, float h) { HGeometry g; g.position = { x, y }; g.size = { w, h }; return g; }

} // namespace h2a

void testPopupPlacement() {
    std::printf("\n-- H2a: 팝업 자리(예전 식과 같음 · 뒤집기 · 끼우기) --\n");
    using namespace h2a;
    // 앵커 · 창 크기 · 배율(1, 1.25, 1.5)을 여러 개 돌려 예전 식과 비트 단위로 같은지 봅니다.
    int same = 0, total = 0;
    const float scales[] = { 1.f, 1.25f, 1.5f, 1.75f };
    const glm::vec2 hosts[] = { { 1600.f, 900.f }, { 900.f, 900.f }, { 600.f, 700.f }, { 1600.f, 1400.f }, { 0.f, 0.f } };
    for (float sc : scales)
        for (glm::vec2 hs : hosts)
            for (float bx = -30.f; bx < 1700.f; bx += 137.3f)
                for (float by = -10.f; by < 1450.f; by += 91.7f) {
                    const HGeometry host = geo(0.f, 0.f, hs.x, hs.y);
                    const HGeometry b    = geo(bx, by, 96.f * sc, 24.f * sc);
                    const HPopupRect bounds = HPopupRect::of(host), anchor = HPopupRect::of(b);
                    const float pw = 280.f * sc, ph = (30.f + 24.f + 22.f * 12.f + 6.f) * sc;
                    const glm::vec2 a1 = oldPicker(host, b, pw, ph, 2.f * sc);
                    const glm::vec2 n1 = hslate::placePopup(pickerPl(2.f * sc), anchor, { pw, ph }, bounds);
                    const glm::vec2 a2 = oldCategories(host, b, 330.f * sc, 300.f * sc, 2.f * sc, 26.f * sc);
                    const glm::vec2 n2 = hslate::placePopup(categoriesPl(2.f * sc, 26.f * sc), anchor, { 330.f * sc, 300.f * sc }, bounds);
                    const glm::vec2 at{ bx, by };
                    const glm::vec2 a3 = oldContext(host, at, 180.f * sc, 140.f * sc);
                    const glm::vec2 n3 = hslate::placePopup(contextPl(), { at, { 0.f, 0.f } }, { 180.f * sc, 140.f * sc }, bounds);
                    const glm::vec2 a4{ bx, 26.f * sc };
                    const glm::vec2 n4 = hslate::placePopup(menuPl(), { { bx, 0.f }, { 0.f, 26.f * sc } }, { 200.f, 400.f }, bounds);
                    total += 4;
                    same += (std::memcmp(&a1, &n1, sizeof a1) == 0) + (std::memcmp(&a2, &n2, sizeof a2) == 0) +
                            (std::memcmp(&a3, &n3, sizeof a3) == 0) + (std::memcmp(&a4, &n4, sizeof a4) == 0);
                }
    check(total > 1000 && same == total, "Add Component · Categories · 오른쪽 메뉴 · 메뉴 드롭다운 자리가 예전 식과 비트 단위로 같음: " +
                                         std::to_string(same) + "/" + std::to_string(total));

    const HPopupRect b{ { 0.f, 0.f }, { 400.f, 300.f } };
    glm::vec2 p = hslate::placePopup(pickerPl(2.f), { { 100.f, 50.f }, { 40.f, 20.f } }, { 80.f, 60.f }, b);
    check(p == glm::vec2(80.f, 72.f), "Below · Center: 앵커 아래 2px · 가운데 맞춤");
    p = hslate::placePopup(pickerPl(2.f), { { 100.f, 250.f }, { 40.f, 20.f } }, { 80.f, 60.f }, b);
    check(p == glm::vec2(80.f, 188.f), "Below · Overflow: 아래 자리가 없으면 앵커 위로 뒤집음");
    p = hslate::placePopup(pickerPl(2.f), { { 380.f, 50.f }, { 40.f, 20.f } }, { 80.f, 60.f }, b);
    check(p == glm::vec2(320.f, 72.f), "끼우기: 오른쪽 경계를 넘으면 창 안으로");
    p = hslate::placePopup(categoriesPl(2.f, 26.f), { { 10.f, 200.f }, { 40.f, 20.f } }, { 80.f, 250.f }, b);
    check(p == glm::vec2(10.f, 26.f), "Below · Half: 앵커가 아래 절반이면 위로 열고 메뉴 줄 아래(insetTop)로 끼움");
    p = hslate::placePopup(contextPl(), { { 350.f, 280.f }, { 0.f, 0.f } }, { 100.f, 50.f }, b);
    check(p == glm::vec2(250.f, 230.f), "Right(누른 점): 오른쪽 · 아래가 넘치면 왼쪽 · 위로 뒤집음");
    p = hslate::placePopup(contextPl(), { { 100.f, 40.f }, { 60.f, 20.f } }, { 100.f, 50.f }, b);
    check(p == glm::vec2(160.f, 40.f), "Right(하위 메뉴): 앵커 오른쪽 · 위 맞춤");
    p = hslate::placePopup(menuPl(), { { 390.f, 0.f }, { 0.f, 26.f } }, { 200.f, 400.f }, b);
    check(p == glm::vec2(390.f, 26.f), "None · clamp 없음(메뉴 드롭다운): 경계를 넘어도 그대로");
}

void testPopupLayerBasics() {
    std::printf("\n-- H2a: 팝업 층(그리기 순서 · 소비 · 바깥 누름 · Esc · overPopup) --\n");
    using namespace h2a;
    auto root = std::make_shared<RootBox>();
    HSlateContext ctx;
    ctx.setRootWidget(root);
    ctx.arrange({ kW, kH });
    check(ctx.popups().bounds().size == glm::vec2(kW, kH), "경계 = 문맥 배치 크기");

    auto pop = std::make_shared<PopupBox>(glm::vec2(100.f, 80.f), glm::vec4(1.f, 0.f, 0.f, 1.f));
    auto anchorW = std::make_shared<PlainProbe>();
    anchorW->onArrangeChildren(geo(20.f, 20.f, 40.f, 20.f));
    HPopupLayer::Spec s;
    s.content      = pop;
    s.anchor       = HPopupRect::of(anchorW->cachedGeometry());
    s.placement    = h2a::pickerPl(2.f);
    s.anchorWidget = anchorW;
    int dismissed = 0;
    s.onDismissed  = [&] { ++dismissed; };
    const HPopupLayer::Handle h = ctx.pushPopup(std::move(s));
    check(h != 0 && ctx.popups().isOpen(h) && ctx.popups().openCount() == 1, "넣으면 열림");
    HPopupRect placed;
    check(ctx.popups().placedRect(h, placed) && placed.pos == glm::vec2(0.f, 42.f) && placed.size == glm::vec2(100.f, 80.f),
          "넣을 때 자리 · 크기(desiredSize) 정함");
    check(!ctx.popups().contains({ 10.f, 50.f }), "배치 전에는 영역이 비어 있음(예전 팝업 호스트와 같음)");
    ctx.arrange({ kW, kH });
    check(pop->cachedGeometry().position == glm::vec2(0.f, 42.f) && ctx.popups().contains({ 10.f, 50.f }), "배치 뒤 그 자리에 놓임");

    std::vector<HDrawElement> els;
    ctx.collectElements({ kW, kH }, nullptr, els);
    check(els.size() == 2 && els[0].color.r < 0.5f && els[1].color == glm::vec4(1.f, 0.f, 0.f, 1.f), "그리기: 루트 다음 팝업");

    // 팝업 위 누름 · 휠은 소비, 바깥은 루트로.
    ctx.handlePointerEvent(btn({ 10.f, 50.f }, 0, true));
    ctx.handlePointerEvent(btn({ 10.f, 50.f }, 0, false));
    check(pop->down == 1 && pop->up == 1 && root->down == 0 && root->up == 0, "팝업 위 누름 · 뗌은 팝업이 받고 루트로 새지 않음");
    pop->handles = false;
    ctx.handlePointerEvent(btn({ 10.f, 50.f }, 0, true));
    check(root->down == 0, "팝업 내용이 처리하지 않아도 영역 안 누름은 소비(예전 같은 크기의 빈 단추)");
    pop->handles = true;
    ctx.handlePointerEvent(wheelAt({ 10.f, 50.f }));
    check(pop->wheel == 1 && root->wheel == 0, "팝업 위 휠도 소비");
    ctx.handlePointerEvent(btn({ 300.f, 200.f }, 0, true));
    check(root->down == 1 && ctx.popups().isOpen(h), "바깥 누름은 루트로 흘러감(층이 스스로 닫지 않음, DEF-64)");
    const int moves = root->move;
    ctx.handlePointerEvent(moveAt({ 10.f, 50.f }, false));
    check(pop->move >= 1 && root->move == moves + 1, "이동은 팝업과 루트 모두 받음(호버 풀림)");

    check(ctx.popups().overPopup({ 10.f, 50.f }) && !ctx.popups().overPopup({ 300.f, 200.f }), "overPopup = 팝업 영역 안");
    check(!ctx.popups().dismissOutside({ 30.f, 25.f }, 0) && ctx.popups().isOpen(h), "앵커 위 누름은 바깥이 아님(단추가 토글)");
    check(!ctx.popups().dismissOutside({ 10.f, 50.f }, 0), "팝업 안은 바깥이 아님");
    check(!ctx.popups().dismissOutside({ 300.f, 200.f }, 1) && ctx.popups().isOpen(h), "오른쪽 누름으로 닫히지 않는 팝업");
    check(!ctx.popups().dismissOnEscape() && ctx.popups().isOpen(h), "closeOnEscape 가 아니면 Esc 로 닫히지 않음");
    check(ctx.popups().dismissOutside({ 300.f, 200.f }, 0) && !ctx.popups().isOpen(h) && dismissed == 1, "왼쪽 바깥 누름으로 닫힘 · 알림 한 번");
    ctx.handlePointerEvent(btn({ 10.f, 50.f }, 0, true));
    check(root->down == 2 && pop->down == 2, "닫힌 팝업은 곧바로 판정에서 빠짐");
    ctx.popups().dismiss(h);
    check(dismissed == 1, "이미 닫힌 팝업을 다시 닫아도 알림 없음");

    // 오른쪽 메뉴 규칙: 왼쪽 · 오른쪽 바깥 누름, Esc.
    auto ctxMenu = std::make_shared<PopupBox>(glm::vec2(60.f, 40.f), glm::vec4(0.f, 1.f, 0.f, 1.f));
    HPopupLayer::Spec cs;
    cs.content = ctxMenu;
    cs.anchor  = { { 390.f, 290.f }, { 0.f, 0.f } };
    cs.placement = h2a::contextPl();
    cs.closeOnRightOutside = true;
    cs.closeOnEscape = true;
    const HPopupLayer::Handle hc = ctx.pushPopup(std::move(cs));
    HPopupRect cr;
    ctx.popups().placedRect(hc, cr);
    check(cr.pos == glm::vec2(330.f, 250.f), "오른쪽 메뉴: 창 오른쪽 아래 구석이면 왼쪽 위로 열림");
    check(ctx.popups().dismissOutside({ 10.f, 10.f }, 1) && !ctx.popups().isOpen(hc), "오른쪽 메뉴는 오른쪽 바깥 누름으로도 닫힘");
    cs = {};
    cs.content = ctxMenu; cs.anchor = { { 100.f, 100.f }, { 0.f, 0.f } }; cs.placement = h2a::contextPl(); cs.closeOnEscape = true;
    const HPopupLayer::Handle hc2 = ctx.pushPopup(std::move(cs));
    HPopupLayer::Spec ps;
    ps.content = std::make_shared<PopupBox>(glm::vec2(20.f, 20.f), glm::vec4(1.f));
    ps.anchor  = { { 5.f, 5.f }, { 0.f, 0.f } };
    const HPopupLayer::Handle hp = ctx.pushPopup(std::move(ps));
    check(ctx.popups().dismissOnEscape() && !ctx.popups().isOpen(hc2) && ctx.popups().isOpen(hp), "Esc 는 closeOnEscape 인 가장 위 팝업만 닫음");
    ctx.popups().dismissAll();
    check(ctx.popups().empty(), "dismissAll");
}

// H2b: 같은 창 안 루트 팝업은 한 번에 하나(UE FMenuStack) · 팝업 밑 위젯 툴팁 막기 · 대본 메뉴 키.
void testPopupSingleRootAndTooltip() {
    std::printf("\n-- H2b: 창 안 팝업 하나 · 팝업 밑 툴팁 막기 · 대본 메뉴 키 --\n");
    using namespace h2a;
    auto root = std::make_shared<RootBox>();
    root->tooltip.set("root tip");
    HSlateContext ctx;
    ctx.setRootWidget(root);
    ctx.arrange({ kW, kH });
    check(!ctx.popups().singleRoot(), "기본은 꺼짐(H2a 와 같음: 여러 루트가 함께 뜰 수 있음)");
    ctx.popups().setSingleRoot(true);

    auto mk = [](glm::vec2 at, glm::vec2 sz, std::function<void()> onDis, HPopupLayer::Handle parent = 0,
                 HPopupLayer::EOutside out = HPopupLayer::EOutside::PassThrough) {
        HPopupLayer::Spec s;
        s.content = std::make_shared<PopupBox>(sz, glm::vec4(1.f));
        s.anchor  = { at, { 0.f, 0.f } };
        s.placement = contextPl();
        s.parent  = parent;
        s.outside = out;
        s.onDismissed = std::move(onDis);
        return s;
    };
    std::vector<std::string> closed;
    const auto a  = ctx.pushPopup(mk({ 10.f, 10.f }, { 60.f, 40.f }, [&] { closed.push_back("a"); }));
    const auto a1 = ctx.pushPopup(mk({ 70.f, 10.f }, { 60.f, 40.f }, [&] { closed.push_back("a1"); }, a));
    check(ctx.popups().isOpen(a) && ctx.popups().isOpen(a1) && closed.empty(), "하위 팝업은 루트를 닫지 않음");
    int hooked = 0;
    ctx.popups().setPushHook([&](HPopupLayer::Handle) { ++hooked; });
    const auto b = ctx.pushPopup(mk({ 200.f, 100.f }, { 60.f, 40.f }, [&] { closed.push_back("b"); }));
    check(!ctx.popups().isOpen(a) && !ctx.popups().isOpen(a1) && ctx.popups().isOpen(b) && ctx.popups().openCount() == 1,
          "새 루트를 넣으면 다른 루트(와 그 하위)가 닫힘");
    check(closed == std::vector<std::string>{ "a1", "a" } && hooked == 1, "닫힌 순서 = 위에서부터, 알림은 넣기 전 · 훅은 넣은 뒤 한 번");
    // Consume(메뉴 드롭다운)도 같은 규칙: 오른쪽 메뉴를 닫고 뜸
    const auto c = ctx.pushPopup(mk({ 0.f, 0.f }, { 80.f, 60.f }, [&] { closed.push_back("c"); }, 0, HPopupLayer::EOutside::Consume));
    check(!ctx.popups().isOpen(b) && ctx.popups().isOpen(c) && closed.back() == "b", "메뉴 드롭다운이 뜨면 오른쪽 메뉴가 닫힘");
    // 닫힘 알림 안에서 다른 팝업을 넣어도 됨(알림은 넣기 전에 끝남)
    ctx.popups().dismissAll();
    ctx.popups().setPushHook(nullptr);
    ctx.popups().collect();

    // 툴팁: 팝업 영역 안에서는 그 밑 루트 위젯의 툴팁이 뜨지 않음(H2a 판단 4), 영역 밖은 예전처럼 뜸
    const auto t = ctx.pushPopup(mk({ 100.f, 100.f }, { 100.f, 80.f }, {}));
    ctx.arrange({ kW, kH });
    HPopupRect r;
    ctx.popups().placedRect(t, r);
    ctx.handlePointerEvent(moveAt(r.pos + glm::vec2(10.f, 10.f), false));
    ctx.tickTooltip(1.f);
    check(!ctx.isTooltipVisible(), "팝업 위: 밑 위젯 툴팁 안 뜸");
    ctx.handlePointerEvent(moveAt({ 350.f, 280.f }, false));
    ctx.tickTooltip(1.f);
    check(ctx.isTooltipVisible(), "팝업 밖: 루트 툴팁 뜸(예전과 같음)");
    ctx.popups().dismissAll();
    ctx.popups().collect();
    const auto cons = ctx.pushPopup(mk({ 0.f, 0.f }, { 50.f, 30.f }, {}, 0, HPopupLayer::EOutside::Consume));
    ctx.arrange({ kW, kH });
    ctx.handlePointerEvent(moveAt({ 351.f, 281.f }, false));
    ctx.tickTooltip(1.f);
    check(ctx.popups().isOpen(cons) && !ctx.isTooltipVisible(), "Consume 팝업(메뉴)이 열려 있으면 배경 위 툴팁도 안 뜸");
    ctx.popups().dismissAll();
    ctx.popups().collect();
    ctx.handlePointerEvent(moveAt({ 352.f, 282.f }, false));
    ctx.tickTooltip(1.f);
    check(ctx.isTooltipVisible(), "팝업이 닫히면 다시 뜸");

    // 대본 메뉴 키(<창>:key@이름)
    using namespace hwininput;
    std::vector<ScriptStep> st;
    std::string err;
    check(parseScript("0:click@26,13;0:key@down;3:key@esc", st, err) && st.size() == 4 && st[2].kind == ScriptStep::Kind::Key &&
              st[2].key == "down" && st[2].window == 0 && st[3].window == 3 && st[3].key == "esc",
          "대본: key@down · key@esc");
    st.clear();
    check(!parseScript("0:key@tab", st, err) && err == "0:key@tab", "모르는 키 이름은 거절");
}

void testPopupLayerConsume() {
    std::printf("\n-- H2a: Consume 팝업(메뉴 드롭다운의 배경) --\n");
    using namespace h2a;
    auto root = std::make_shared<RootBox>();
    HSlateContext ctx;
    ctx.setRootWidget(root);
    ctx.arrange({ kW, kH });
    auto drop = std::make_shared<PopupBox>(glm::vec2(120.f, 90.f), glm::vec4(0.f, 0.f, 1.f, 1.f));
    int dismissed = 0;
    HPopupLayer::Spec s;
    s.content = drop;
    s.anchor  = { { 50.f, 0.f }, { 0.f, 26.f } };
    s.placement = h2a::menuPl();
    s.outside = HPopupLayer::EOutside::Consume;
    s.backdropInsetTop = 26.f;
    s.closeOnLeftOutside = false;
    s.onDismissed = [&] { ++dismissed; };
    const HPopupLayer::Handle h = ctx.pushPopup(std::move(s));
    ctx.arrange({ kW, kH });
    check(drop->cachedGeometry().position == glm::vec2(50.f, 26.f), "메뉴 단추 아래 · 왼쪽 맞춤");
    check(ctx.popups().overPopup({ 390.f, 10.f }) && ctx.popups().overPopup({ 300.f, 250.f }), "Consume 이 열려 있으면 창 전체가 overPopup(예전 menus_.isOpen())");
    check(!ctx.popups().dismissOutside({ 300.f, 250.f }, 0) && ctx.popups().isOpen(h), "Consume 은 dismissOutside 로 닫히지 않음(배경이 닫음)");

    ctx.handlePointerEvent(btn({ 10.f, 10.f }, 0, true));
    ctx.handlePointerEvent(btn({ 10.f, 10.f }, 0, false));
    check(root->down == 1 && root->up == 1 && ctx.popups().isOpen(h), "메뉴 줄 행(insetTop 위)은 배경이 아님: 루트(메뉴 단추)가 받음");
    ctx.handlePointerEvent(wheelAt({ 300.f, 250.f }));
    check(root->wheel == 0, "배경 위 휠은 먹음");
    ctx.handlePointerEvent(btn({ 300.f, 250.f }, 0, true));
    check(root->down == 1 && ctx.popups().isOpen(h), "배경 누름은 먹고 아직 닫지 않음(예전 캐처 단추: 뗄 때 클릭)");
    ctx.handlePointerEvent(btn({ 10.f, 10.f }, 0, false));
    check(ctx.popups().isOpen(h), "누름이 배경 밖(메뉴 줄)에서 끝나면 닫지 않음");
    ctx.handlePointerEvent(moveAt({ 300.f, 250.f }, false));
    ctx.handlePointerEvent(btn({ 300.f, 250.f }, 0, false));
    check(ctx.popups().isOpen(h), "버튼이 떨어진 이동 뒤에는 눌림이 풀려 뗌만으로 닫지 않음");
    ctx.handlePointerEvent(btn({ 300.f, 250.f }, 0, true));
    ctx.handlePointerEvent(btn({ 310.f, 260.f }, 0, false));
    check(!ctx.popups().isOpen(h) && dismissed == 1 && root->down == 1, "배경에서 누르고 떼면 닫힘(누름은 루트로 가지 않음)");
}

void testPopupLayerCascadeAndDispatch() {
    std::printf("\n-- H2a: 하위 팝업 연쇄 · 전달 중 닫기 · 포커스 · 툴팁 --\n");
    using namespace h2a;
    auto root = std::make_shared<RootBox>();
    HSlateContext ctx;
    ctx.setRootWidget(root);
    ctx.arrange({ kW, kH });
    std::vector<std::string> order;
    auto mk = [&](const char* name, glm::vec2 at, HPopupLayer::Handle parent) {
        HPopupLayer::Spec s;
        s.content = std::make_shared<PopupBox>(glm::vec2(60.f, 60.f), glm::vec4(1.f));
        s.anchor  = { at, { 0.f, 0.f } };
        s.placement = h2a::contextPl();
        s.parent  = parent;
        std::string n = name;
        s.onDismissed = [&order, n] { order.push_back(n); };
        return ctx.pushPopup(std::move(s));
    };
    const auto a  = mk("a", { 10.f, 10.f }, 0);
    const auto b  = mk("b", { 80.f, 10.f }, a);
    const auto c  = mk("c", { 150.f, 10.f }, b);
    ctx.arrange({ kW, kH });
    check(ctx.popups().openCount() == 3, "부모 → 하위 → 하위의 하위");
    check(!ctx.popups().dismissOutside({ 160.f, 20.f }, 0) && ctx.popups().openCount() == 3, "하위 팝업 안 누름은 부모의 바깥이 아님");
    const auto b2 = mk("b2", { 80.f, 100.f }, a);
    check(!ctx.popups().isOpen(b) && !ctx.popups().isOpen(c) && ctx.popups().isOpen(b2) && order == std::vector<std::string>{ "c", "b" },
          "같은 부모의 다른 하위를 열면 앞 하위(와 그 하위)가 위에서부터 닫힘");
    order.clear();
    ctx.popups().dismiss(a);
    check(ctx.popups().empty() && order == std::vector<std::string>{ "b2", "a" }, "부모를 닫으면 하위도 함께(위에서부터)");
    check(mk("x", { 0.f, 0.f }, 999) == 0, "닫혔거나 없는 부모로는 넣지 않음");

    // 팝업 안 단추가 전달 중에 자기 팝업을 닫아도 위젯은 전달이 끝날 때까지 살아 있습니다.
    auto self = std::make_shared<PopupBox>(glm::vec2(50.f, 50.f), glm::vec4(1.f));
    std::weak_ptr<HWidget> weak = self;
    HPopupLayer::Handle hs = 0;
    bool aliveInside = false;
    self->onPress = [&] { ctx.dismissPopup(hs); aliveInside = !weak.expired(); };
    HPopupLayer::Spec ss;
    ss.content = self;
    ss.anchor  = { { 200.f, 100.f }, { 0.f, 0.f } };
    ss.placement = h2a::contextPl();
    hs = ctx.pushPopup(std::move(ss));
    self.reset();
    ctx.arrange({ kW, kH });
    ctx.handlePointerEvent(btn({ 210.f, 110.f }, 0, true));
    check(aliveInside && !ctx.popups().isOpen(hs), "전달 중 자기 팝업 닫기: 닫히지만 실행 중인 위젯은 살아 있음");
    check(weak.expired(), "전달이 끝나면 닫힌 팝업의 위젯을 놓음(collect)");

    // 포커스 경로 · 키 · 툴팁이 팝업 안 위젯까지 갑니다(Add Component 검색 칸).
    auto field = mkProbe("field", true);
    field->handleKeys = true;
    auto holder = mkProbe("holder");
    holder->child = field;
    HPopupLayer::Spec fs;
    fs.content = holder;
    fs.anchor  = { { 100.f, 100.f }, { 0.f, 0.f } };
    fs.placement = h2a::contextPl();
    fs.size    = { 10.f, 10.f };
    const auto hf = ctx.pushPopup(std::move(fs));
    ctx.arrange({ kW, kH });
    g_focusLog.clear();
    check(ctx.setFocus(field), "팝업 안 글 칸에 포커스");
    const HReply kr = ctx.handleKeyEvent(keyDown(257));
    check(kr.handled && ctx.focusedWidget() == field, "키가 팝업 안 포커스 위젯에 감(포커스 경로가 팝업 층에서 찾아짐)");
    const HWidgetPath hp = ctx.hitTest({ 105.f, 105.f });
    check(hp.lastWidget() == field, "히트 경로: 팝업이 루트보다 먼저");
    holder->tooltip.set("tip");
    ctx.handlePointerEvent(moveAt({ 105.f, 105.f }, false));
    ctx.tickTooltip(1.f);
    check(ctx.isTooltipVisible(), "팝업 안 위젯의 툴팁");
    ctx.dismissPopup(hf);
    ctx.arrange({ kW, kH });
    ctx.handleKeyEvent(keyDown(257));
    check(ctx.focusedWidget() == nullptr, "팝업이 닫혀 트리에서 빠진 포커스 위젯은 다음 키 때 풀림");
}

void testPopupLayerPerContext() {
    std::printf("\n-- H2a: 문맥별 팝업 층(떠 있는 창) --\n");
    using namespace h2a;
    auto rootA = std::make_shared<RootBox>();
    auto rootB = std::make_shared<RootBox>();
    HSlateContext a, b;
    a.setRootWidget(rootA);
    b.setRootWidget(rootB);
    a.arrange({ kW, kH });
    b.arrange({ 200.f, 150.f });
    // 에디터와 같은 규칙: 한 문맥에 팝업이 열리면 다른 문맥의 팝업을 닫음(훅).
    a.popups().setPushHook([&](HPopupLayer::Handle) { b.dismissAllPopups(); });
    b.popups().setPushHook([&](HPopupLayer::Handle) { a.dismissAllPopups(); });
    auto pb = std::make_shared<PopupBox>(glm::vec2(100.f, 100.f), glm::vec4(1.f));
    HPopupLayer::Spec s;
    s.content = pb;
    s.anchor  = { { 180.f, 140.f }, { 0.f, 0.f } };
    s.placement = h2a::contextPl();
    int bDismissed = 0;
    s.onDismissed = [&] { ++bDismissed; };
    const auto hb = b.pushPopup(std::move(s));
    HPopupRect r;
    b.popups().placedRect(hb, r);
    check(r.pos == glm::vec2(80.f, 40.f), "자리는 그 문맥(떠 있는 창) 경계로 정함");
    b.arrange({ 200.f, 150.f });
    check(b.popups().contains({ 90.f, 50.f }) && !a.popups().contains({ 90.f, 50.f }) && a.popups().empty(), "다른 문맥 팝업 층에는 없음");
    a.handlePointerEvent(btn({ 90.f, 50.f }, 0, true));
    check(rootA->down == 1 && pb->down == 0, "메인 누름은 떠 있는 창 팝업에 가지 않음");
    std::vector<HDrawElement> els;
    a.collectElements({ kW, kH }, nullptr, els);
    check(els.size() == 1, "메인 그리기 목록에 떠 있는 창 팝업이 없음");
    b.collectElements({ 200.f, 150.f }, nullptr, els);
    check(els.size() == 2, "떠 있는 창 그리기 목록에 그 창 팝업");
    HPopupLayer::Spec s2;
    s2.content = std::make_shared<PopupBox>(glm::vec2(10.f, 10.f), glm::vec4(1.f));
    s2.anchor  = { { 10.f, 10.f }, { 0.f, 0.f } };
    a.pushPopup(std::move(s2));
    check(!b.popups().isOpen(hb) && bDismissed == 1 && a.popups().openCount() == 1, "한 창에서 열면 다른 창 팝업이 닫힘(훅)");
}

void testMenuAnchor() {
    std::printf("\n-- H2a: SMenuAnchor --\n");
    using namespace h2a;
    HSlateContext ctx;
    auto inner = std::make_shared<PopupBox>(glm::vec2(40.f, 20.f), glm::vec4(0.3f, 0.3f, 0.3f, 1.f));
    int built = 0;
    std::vector<bool> changes;
    auto anchor = HNew(SMenuAnchor)
        ->content(inner)
        ->menuContent([&]() -> std::shared_ptr<HWidget> {
            ++built;
            return std::make_shared<PopupBox>(glm::vec2(80.f, 60.f), glm::vec4(1.f, 1.f, 0.f, 1.f));
        })
        ->placement(h2a::pickerPl(2.f))
        ->popupContext(&ctx)
        ->onOpenChanged([&](bool o) { changes.push_back(o); });
    auto root = HNew(SOverlay);
    root->addSlot(anchor, EHAlign::Left, EVAlign::Top, HPadding{ 100.f, 50.f, 0.f, 0.f });
    ctx.setRootWidget(root);
    ctx.arrange({ kW, kH });
    check(anchor->cachedGeometry().position == glm::vec2(100.f, 50.f) && inner->cachedGeometry().position == glm::vec2(100.f, 50.f) &&
          inner->cachedGeometry().size == glm::vec2(40.f, 20.f), "자식을 받은 자리 그대로 배치(감싸도 화면 그대로)");
    std::vector<HDrawElement> els;
    ctx.collectElements({ kW, kH }, nullptr, els);
    check(els.size() == 1, "앵커 자신은 그리지 않음");
    check(!anchor->isOpen() && anchor->open() && anchor->isOpen() && built == 1 && changes == std::vector<bool>{ true }, "열면 내용을 만들어 팝업 층에");
    HPopupRect r;
    ctx.popups().placedRect(anchor->handle(), r);
    check(r.pos == glm::vec2(80.f, 72.f), "자기 배치 자리를 앵커로(아래 2px · 가운데)");
    ctx.arrange({ kW, kH });
    check(!ctx.popups().dismissOutside({ 110.f, 55.f }, 0) && anchor->isOpen(), "앵커 위 누름은 바깥이 아님");
    ctx.handlePointerEvent(btn({ 110.f, 55.f }, 0, true));
    check(inner->down == 1, "앵커 위 누름은 자식(단추)이 받음");
    root->addSlot(HNew(SBox));   // 배치 바꾸기 흉내
    check(anchor->refresh() && built == 2 && anchor->isOpen(), "refresh: 열린 채 내용을 다시 만듦(같은 팝업)");
    check(ctx.popups().dismissOutside({ 390.f, 290.f }, 0) && !anchor->isOpen() && changes == std::vector<bool>{ true, false },
          "층이 닫으면(바깥 누름) onOpenChanged(false)");
    check(anchor->toggle() && anchor->isOpen() && !anchor->toggle() && !anchor->isOpen(), "toggle");
    auto noCtx = HNew(SMenuAnchor)->menuContent([] { return std::static_pointer_cast<HWidget>(HNew(SBox)); });
    check(!noCtx->open() && !noCtx->isOpen(), "문맥이 없으면 열지 않음");
}

// ── H3a: 속성 행(열 나눔 공유) · 성분 칸 · 콤보(메뉴 위젯) ──────────────────────────────────────────────────────────
namespace h3a {
struct ValueProbe : HWidget {
    int down = 0, up = 0;
    glm::vec2 computeDesiredSize() override { return { 10.f, 10.f }; }
    void paint(HPaintContext&, const HGeometry&) override {}
    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.down) ++down; else ++up;
        return HReply::Handled();
    }
};
std::shared_ptr<SPropertyRow> makeRow(const std::shared_ptr<HPropertyColumns>& cols, const std::shared_ptr<HWidget>& value, float indent,
                                      const std::shared_ptr<HWidget>& reset = nullptr) {
    return HNew(SPropertyRow)->columns(cols)->name(HNew(STextBlock)->text("Name")->fontSize(12.f))->value(value)->reset(reset)
        ->indent(indent)->resetWidth(18.f)->rightPad(4.f)->height(24.f);
}
}  // namespace h3a

void testPropertyRowColumns() {
    std::printf("\n-- H3a: 속성 행 · 열 나눔선(패널 전체 공유) --\n");
    using namespace h3a;
    HSlateContext ctx;
    auto cols = std::make_shared<HPropertyColumns>();
    auto v1 = std::make_shared<ValueProbe>();
    auto v2 = std::make_shared<ValueProbe>();
    auto rp = std::make_shared<ValueProbe>();
    auto r1 = makeRow(cols, v1, 12.f, rp);
    auto r2 = makeRow(cols, v2, 30.f);   // 들여쓰기가 달라도 나눔선은 같은 자리
    auto list = HNew(SVerticalBox);
    list->addSlot(r1, SVerticalBox::ESizeRule::Fixed, 24.f);
    list->addSlot(r2, SVerticalBox::ESizeRule::Fixed, 24.f);
    ctx.setRootWidget(list);
    ctx.arrange({ kW, kH });
    const float W = kW - 18.f - 4.f;
    check(nearF(r1->dividerX(), 0.4f * W) && nearF(r2->dividerX(), r1->dividerX()), "처음 나눔선 = 비율 0.4, 들여쓰기와 상관없이 같은 자리");
    check(nearF(v1->cachedGeometry().position.x, r1->dividerX() + 3.f) && nearF(v1->cachedGeometry().size.x, W - r1->dividerX() - 3.f),
          "값 칸 = 나눔선 + 틈 절반 ~ 되돌리기 자리 앞");
    check(nearF(rp->cachedGeometry().position.x, W) && nearF(rp->cachedGeometry().size.x, 18.f), "되돌리기 자리는 늘 오른쪽 끝(18px)");
    check(nearF(r2->nameWidget()->cachedGeometry().position.x, 30.f) && r2->nameWidget()->cachedGeometry().size.x < r1->nameWidget()->cachedGeometry().size.x,
          "이름은 들여쓰기만큼 밀리고 나눔선 앞에서 끝남");

    // 첫 행의 나눔선을 끌면 둘째 행도 함께
    const float x0 = r1->dividerX();
    ctx.handlePointerEvent(press({ x0 + 2.f, 12.f }));
    check(r1->draggingDivider() && v1->down == 0, "나눔선 ±4px 누름은 끌기(값 칸으로 가지 않음)");
    ctx.handlePointerEvent(move({ 200.f, 12.f }, true));
    ctx.handlePointerEvent(release({ 200.f, 12.f }));
    ctx.arrange({ kW, kH });
    check(!r1->draggingDivider() && nearF(r1->dividerX(), 200.f) && nearF(r2->dividerX(), 200.f) && cols->changes > 0,
          "끈 자리로 두 행의 나눔선이 같이 옮겨짐");
    check(nearF(v2->cachedGeometry().position.x, 203.f), "둘째 행 값 칸도 따라 옮겨짐");
    ctx.handlePointerEvent(press({ 200.f, 12.f }));
    ctx.handlePointerEvent(move({ 2.f, 12.f }, true));
    ctx.arrange({ kW, kH });
    check(nearF(r1->dividerX(), cols->minNamePx), "왼쪽 끝은 이름 열 최소 폭에서 멈춤");
    ctx.handlePointerEvent(move({ kW, 12.f }, true));
    ctx.handlePointerEvent(release({ kW, 12.f }));
    ctx.arrange({ kW, kH });
    check(nearF(r1->dividerX(), W - cols->minValuePx), "오른쪽 끝은 값 칸 최소 폭에서 멈춤");
    // 비율이라 패널 폭이 바뀌어도 같은 비율
    const float frac = cols->nameFraction;
    ctx.arrange({ kW * 2.f, kH });
    check(nearF(r1->dividerX(), frac * (kW * 2.f - 22.f), 1e-3f), "패널 폭이 바뀌어도 같은 비율");
    ctx.arrange({ kW, kH });

    // 값 칸 누름 · hover · 되돌리기 칸
    ctx.handlePointerEvent(press({ kW - 40.f, 12.f }));
    ctx.handlePointerEvent(release({ kW - 40.f, 12.f }));
    check(v1->down == 1 && v1->up == 1 && !r1->draggingDivider(), "나눔선 밖 누름은 값 칸이 받음");
    ctx.handlePointerEvent(move({ 100.f, 36.f }, false));
    check(r2->hovered() && !r1->hovered(), "포인터가 있는 행만 hover");
    rp->visibility.set(EVisibility::Hidden);
    ctx.handlePointerEvent(press({ W + 9.f, 12.f }));
    ctx.handlePointerEvent(release({ W + 9.f, 12.f }));
    check(rp->down == 0, "숨은 되돌리기 칸은 누름을 받지 않음(자리는 그대로)");
    rp->visibility.set(EVisibility::Visible);
    ctx.handlePointerEvent(press({ W + 9.f, 12.f }));
    check(rp->down == 1, "보이면 받음");
    ctx.handlePointerEvent(release({ W + 9.f, 12.f }));
}

void testVectorField() {
    std::printf("\n-- H3a: 성분 칸(SVectorField) --\n");
    HSlateContext ctx;
    glm::vec3 v{ 1.f, 2.f, 3.f };
    std::vector<int> begins, ends, sets;
    auto f = HNew(SVectorField)->components(3)->step(0.01f)->unit(ENumberUnit::Offset)->gap(2.f)->barWidth(3.f)
        ->colors({ glm::vec4(1, 0, 0, 1), glm::vec4(0, 1, 0, 1), glm::vec4(0, 0, 1, 1) })
        ->get([&v](int i) { return v[i]; })
        ->set([&v, &sets](int i, float x) { v[i] = x; sets.push_back(i); })
        ->onDragBegin([&begins](int i) { begins.push_back(i); })
        ->onDragEnd([&ends](int i) { ends.push_back(i); });
    auto list = HNew(SVerticalBox);
    list->addSlot(f, SVerticalBox::ESizeRule::Fixed, 24.f);
    ctx.setRootWidget(list);
    ctx.arrange({ kW, kH });
    const float fw = (kW - 4.f) / 3.f;
    check(f->count() == 3 && f->field(0) && f->field(2) && !f->field(3), "성분 칸 셋");
    check(nearF(f->field(1)->cachedGeometry().position.x, fw + 2.f) && nearF(f->field(1)->cachedGeometry().size.x, fw), "같은 폭 · 틈 2px");
    // 가운데 칸을 끌면 Y 만(Offset: 1cm/px, 데드존 3px 뒤부터)
    const float cx = fw + 2.f + fw * 0.5f;
    ctx.handlePointerEvent(press({ cx, 12.f }));
    ctx.handlePointerEvent(move({ cx + 10.f, 12.f }, true));
    ctx.handlePointerEvent(release({ cx + 10.f, 12.f }));
    check(nearF(v.y, 2.07f) && v.x == 1.f && v.z == 3.f, "Y 성분만 바뀜(7px x 0.01)");
    check(begins == std::vector<int>{ 1 } && ends == std::vector<int>{ 1 }, "끌기 시작 · 끝 알림에 성분 번호");
    // Shift = 10배(숫자 칸 규칙 그대로, DEF-46)
    const float zx = 2.f * (fw + 2.f) + fw * 0.5f;
    ctx.handlePointerEvent(press({ zx, 12.f }));
    ctx.handlePointerEvent(moveMod({ zx + 10.f, 12.f }, true, false));
    ctx.handlePointerEvent(release({ zx + 10.f, 12.f }));
    check(nearF(v.z, 3.7f, 1e-3f) && nearF(v.y, 2.07f), "Shift 끌기는 10배, 다른 성분은 그대로");
    // 누르고 떼기 = 그 성분만 입력 모드, 확정하면 그 성분 세터 · 끝 알림
    sets.clear(); ends.clear();
    ctx.handlePointerEvent(press({ fw * 0.5f, 12.f }));
    ctx.handlePointerEvent(release({ fw * 0.5f, 12.f }));
    check(f->field(0)->isEditing() && !f->field(1)->isEditing() && SNumberField::focusedField() == f->field(0).get(), "누른 성분만 입력 모드");
    f->field(0)->selectAll();
    f->field(0)->onChar((unsigned)'5');
    f->field(0)->commitEdit();
    check(v.x == 5.f && sets == std::vector<int>{ 0 } && ends == std::vector<int>{ 0 } && SNumberField::focusedField() == nullptr,
          "입력 확정: X 만 바뀌고 Undo 알림 한 번");
    // 띠 색은 왼쪽 끝에 그립니다
    std::vector<HDrawElement> els;
    ctx.collectElements({ kW, kH }, nullptr, els);
    int bars = 0;
    for (const HDrawElement& e : els)
        if (e.type == HDrawElement::Type::Rect && nearF(e.rect.z, 3.f) && nearF(e.rect.w, 24.f)) ++bars;
    check(bars == 3, "성분마다 색 띠 하나(3px)");
}

void testComboMenu() {
    std::printf("\n-- H3a: 열거 콤보(메뉴 위젯 · 팝업 층) --\n");
    using namespace h2a;
    HSlateContext ctx;
    ctx.popups().setSingleRoot(true);
    EditorUiKit ui;
    EditorContextMenu menu;
    std::string askedPanel;
    menu.init(&ui, [&ctx, &askedPanel](const std::string& panel) { askedPanel = panel; return &ctx; }, nullptr);
    auto rootBox = std::make_shared<RootBox>();
    auto anchor = HNew(SButton)->padding(HPadding{ 0.f, 0.f })->content(HNew(SBox)->widthOverride(100.f)->heightOverride(22.f));
    auto root = HNew(SOverlay);
    root->addSlot(rootBox);
    root->addSlot(anchor, EHAlign::Left, EVAlign::Top, HPadding{ 50.f, 50.f, 0.f, 0.f });
    ctx.setRootWidget(root);
    ctx.arrange({ kW, kH });
    int current = 1, chosen = -1;
    auto entries = [&] {
        std::vector<EditorMenuEntry> e;
        for (int i = 0; i < 3; ++i) {
            EditorMenuEntry x = EditorMenuEntry::act(std::string(1, (char)('A' + i)), [&chosen, &current, i] { chosen = i; current = i; });
            x.checked = [&current, i] { return current == i; };
            e.push_back(std::move(x));
        }
        return e;
    };
    auto open = [&] {
        menu.openBelow(anchor, entries(), "Details", current);
        menu.flushRebuild();
        ctx.arrange({ kW, kH });
    };
    open();
    const std::vector<EditorMenuRow>* rows = menu.view().rows(0);
    check(menu.isOpen() && menu.popupContext() == &ctx && askedPanel == "Details" && menu.panel() == "Details", "연 패널의 문맥에 엶");
    check(rows && rows->size() == 3 && (*rows)[1].hasCheck && (*rows)[1].checked && !(*rows)[0].checked, "지금 값에 체크");
    check(menu.view().highlight(0) == 1, "지금 값 행을 강조");
    HPopupRect r;
    check(ctx.popups().placedRect(menu.view().rootHandle(), r) && nearF(r.pos.x, 50.f) && nearF(r.pos.y, 73.f) && r.size.x >= 100.f,
          "단추 아래(1px) · 왼쪽 맞춤 · 폭은 단추 폭 이상");
    check(menu.isOpenBelow(anchor.get()) && !menu.isOpenBelow(rootBox.get()), "어느 단추의 콤보인지(단추 토글)");
    menu.handleKey(hmenu::Key::Down);
    check(menu.view().highlight(0) == 2, "↓");
    menu.handleKey(hmenu::Key::Down);
    check(menu.view().highlight(0) == 0, "↓ 끝에서 처음으로");
    menu.handleKey(hmenu::Key::Up);
    menu.handleKey(hmenu::Key::Up);
    check(menu.view().highlight(0) == 1, "↑ ↑");
    menu.handleKey(hmenu::Key::Left);
    menu.handleKey(hmenu::Key::Right);
    check(menu.isOpen() && menu.view().highlight(0) == 1, "← → 는 무동작(콤보는 메뉴 줄이 아님)");
    menu.handleKey(hmenu::Key::Down);
    menu.handleKey(hmenu::Key::Enter);
    check(!menu.isOpen() && chosen == 2 && current == 2 && ctx.popups().empty(), "Enter = 고르고 닫힘");
    open();
    check(menu.view().highlight(0) == 2, "다시 열면 새 값 행을 강조");
    menu.handleKey(hmenu::Key::Escape);
    check(!menu.isOpen() && chosen == 2 && ctx.popups().empty(), "Esc = 고르지 않고 닫힘");
    open();
    check(!ctx.popups().dismissOutside({ 60.f, 60.f }, 0) && menu.isOpen(), "단추 위 누름은 바깥이 아님(단추가 닫음)");
    check(ctx.popups().dismissOutside({ 390.f, 290.f }, 0) && !menu.isOpen(), "바깥 누름 = 닫힘");
    open();
    check(ctx.popups().dismissOutside({ 390.f, 290.f }, 1) && !menu.isOpen(), "바깥 오른쪽 누름도 닫힘");
    // 단추가 배치되기 전에 열면 배치된 뒤에 띄움(디테일을 같은 프레임에 다시 만든 경우)
    auto fresh = HNew(SButton)->padding(HPadding{ 0.f, 0.f })->content(HNew(SBox)->widthOverride(80.f)->heightOverride(20.f));
    menu.openBelow(fresh, entries(), "Details", 0);
    menu.flushRebuild();
    check(menu.isOpen() && !menu.popupContext(), "배치 전 단추: 아직 띄우지 않음");
    root->addSlot(fresh, EHAlign::Left, EVAlign::Top, HPadding{ 200.f, 100.f, 0.f, 0.f });
    ctx.arrange({ kW, kH });
    menu.flushRebuild();
    check(menu.popupContext() == &ctx && ctx.popups().placedRect(menu.view().rootHandle(), r) && nearF(r.pos.y, 121.f),
          "배치된 뒤 그 단추 아래에 띄움");
    // 누른 자리 메뉴로 바꿔 열면 콤보 단추와 상관없음
    menu.open({ 10.f, 10.f }, entries(), "Bottom");
    menu.flushRebuild();
    check(menu.isOpen() && !menu.isOpenBelow(fresh.get()) && menu.panel() == "Bottom" && askedPanel == "Bottom", "누른 자리 메뉴(패널 = Bottom)");
    menu.close();
    check(!menu.isOpen() && ctx.popups().empty(), "닫기");
}

// H3b: 색 선택기 조각(판 · 막대)과 그라데이션 사각형. 판 · 막대는 누르면 포인터를 잡고 끄는 동안 (u, v) · t 를 알리며, 칠하기는
// 칸마다 네 모서리 색을 잇는 그라데이션입니다. 렌더러는 잘린 그라데이션의 정점 색을 잘린 자리 비율로 구합니다.
void testColorPickerWidgets() {
    std::printf("\n-- H3b: 색 선택기 판 · 막대 · 그라데이션 --\n");
    const VkImageView kWhite = reinterpret_cast<VkImageView>(uintptr_t(0x1111));
    // (a) 판: 누름 · 끌기 · 창 밖 끌기(자름) · 뗌
    {
        HSlateRenderer ui;
        float u = -1.f, v = -1.f;
        int begins = 0, ends = 0, changes = 0;
        auto plane = HNew(SColorPlane)
            ->colorAt([](float pu, float pv) { return glm::vec4(pu, pv, 0.f, 1.f); })
            ->marker([&] { return glm::vec2(u, v); })
            ->onBegin([&] { ++begins; })->onEnd([&] { ++ends; })
            ->onChange([&](float pu, float pv) { u = pu; v = pv; ++changes; })
            ->cells(4)->size({ 100.f, 100.f });
        auto root = HNew(SBox)->hAlign(EHAlign::Left)->vAlign(EVAlign::Top)->content(plane);
        ui.setRootWidget(root);
        ui.arrange({ 400.f, 300.f });
        ui.handlePointerEvent(press({ 25.f, 75.f }));
        check(begins == 1 && nearF(u, 0.25f) && nearF(v, 0.75f), "판 누름 = 시작 · (0.25, 0.75)");
        ui.handlePointerEvent(move({ 50.f, 10.f }, true));
        check(nearF(u, 0.5f) && nearF(v, 0.1f) && plane->dragging(), "끄는 동안 알림");
        ui.handlePointerEvent(move({ 300.f, -40.f }, true));
        check(nearF(u, 1.f) && nearF(v, 0.f), "판 밖으로 끌면 0 ~ 1 로 자름(포인터를 잡음)");
        ui.handlePointerEvent(release({ 300.f, -40.f }));
        check(ends == 1 && !plane->dragging(), "뗌 = 끝 한 번");
        const int c0 = changes;
        ui.handlePointerEvent(move({ 30.f, 30.f }, false));
        check(changes == c0, "뗀 뒤 이동은 알리지 않음");
        // 칠하기: 칸 4 x 4 = 그라데이션 16개, 첫 칸 모서리 색
        HSlateDrawList dl;
        check(ui.buildDrawList({ 400.f, 300.f }, kWhite, dl) && dl.vertices.size() >= 16 * 4, "칸 16개(정점 64 이상)");
        const UIVertex* q = dl.vertices.data();
        check(nearF(q[0].color.r, 0.f) && nearF(q[1].color.r, 0.25f) && nearF(q[2].color.g, 0.25f) && nearF(q[3].color.g, 0.25f) &&
                  nearF(q[3].color.r, 0.f),
              "첫 칸: 왼쪽 위 (0,0) · 오른쪽 위 u 0.25 · 아래 v 0.25");
    }
    // (b) 막대: 세로 · 가로
    {
        HSlateRenderer ui;
        float t = -1.f;
        auto bar = HNew(SColorBar)->colorAt([](float x) { return glm::vec4(x, 0.f, 0.f, 1.f); })
            ->onChange([&](float x) { t = x; })->vertical(true)->size({ 16.f, 200.f });
        ui.setRootWidget(HNew(SBox)->hAlign(EHAlign::Left)->vAlign(EVAlign::Top)->content(bar));
        ui.arrange({ 400.f, 300.f });
        ui.handlePointerEvent(press({ 8.f, 50.f }));
        check(nearF(t, 0.25f), "세로 막대: 위 = 0, 50/200 = 0.25");
        ui.handlePointerEvent(move({ 8.f, 400.f }, true));
        check(nearF(t, 1.f), "막대 밖 = 1 로 자름");
        ui.handlePointerEvent(release({ 8.f, 400.f }));
        auto hbar = HNew(SColorBar)->onChange([&](float x) { t = x; })->vertical(false)->size({ 200.f, 16.f });
        ui.setRootWidget(HNew(SBox)->hAlign(EHAlign::Left)->vAlign(EVAlign::Top)->content(hbar));
        ui.arrange({ 400.f, 300.f });
        ui.handlePointerEvent(press({ 150.f, 8.f }));
        check(nearF(t, 0.75f), "가로 막대: 150/200 = 0.75");
        ui.handlePointerEvent(release({ 150.f, 8.f }));
    }
    // (c) 그라데이션 사각형: 잘리면 잘린 자리 색(쌍선형) · 덤프 줄
    {
        struct GradProbe : HWidget {
            glm::vec2 computeDesiredSize() override { return { 100.f, 100.f }; }
            void paint(HPaintContext& ctx, const HGeometry& geo) override {
                HGeometry clip = geo;
                clip.size = { 50.f, 100.f };   // 왼쪽 절반만
                ctx.pushClip(clip);
                ctx.drawGradientRect(geo, { 0.f, 0.f, 0.f, 1.f }, { 1.f, 0.f, 0.f, 1.f }, { 1.f, 1.f, 0.f, 1.f }, { 0.f, 1.f, 0.f, 1.f });
                ctx.popClip();
            }
        };
        HSlateRenderer ui;
        ui.setRootWidget(HNew(SBox)->hAlign(EHAlign::Left)->vAlign(EVAlign::Top)->content(std::make_shared<GradProbe>()));
        HSlateDrawList dl;
        check(ui.buildDrawList({ 400.f, 300.f }, kWhite, dl) && dl.vertices.size() == 4, "그라데이션 사각형 하나");
        const UIVertex* q = dl.vertices.data();
        check(nearF(q[1].pos.x, 50.f) && nearF(q[1].color.r, 0.5f) && nearF(q[2].color.r, 0.5f) && nearF(q[2].color.g, 1.f) &&
                  nearF(q[0].color.r, 0.f) && nearF(q[3].color.g, 1.f),
              "왼쪽 절반으로 잘리면 오른쪽 정점 색 = 가운데 색(r 0.5)");
        check(q[0].color != q[1].color && q[1].color != q[2].color, "정점마다 다른 색(보통 사각형은 하나)");
        std::vector<HDrawElement> els;
        ui.context().collectElements({ 400.f, 300.f }, nullptr, els);
        check(els.size() == 1 && els[0].gradient && els[0].corner[2] == glm::vec4(1.f, 1.f, 0.f, 1.f), "그리기 요소에 모서리 색");
    }
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
    testWindowTargetLogic();
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
    testHitPath();
    testHitOrderAndVisibility();
    testHitClip();
    testKeyBubbleAndChar();
    testFocusLifetime();
    testFocusReply();
    testMultiContextFocus();
    testFocusHookAndReuse();
    testLayoutCacheMatchesUncached();
    testMeasureCountLinear();
    testArrangeSkip();
    testDeferredQueue();
    testSplitterWeights();
    testFloatWindowLifecycle();
    testDockPanelOnePlace();
    testWindowInputRules();
    testFloatScript();
    testDockFloatInput();
    testDockCrossWindowDrag();
    testPopupPlacement();
    testPopupLayerBasics();
    testPopupLayerConsume();
    testPopupLayerCascadeAndDispatch();
    testPopupLayerPerContext();
    testMenuAnchor();
    testFloatWindowPopups();
    testPopupSingleRootAndTooltip();
    testPropertyRowColumns();
    testVectorField();
    testComboMenu();
    testColorPickerWidgets();

    if (g_failed) {
        std::printf("\n%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
