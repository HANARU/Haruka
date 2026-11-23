// 에디터 편집 기본기(2026-10-15)를 화면 없이 검산합니다.
//
// 이 테스트가 존재하는 이유: 커맨드가 액터를 포인터 대신 ID 로 기억하게 바꾸고(액터 삭제를 되돌려도 앞선 편집이
// 이어짐), 되돌릴 수 없던 편집(리네임 · 켜기/끄기 · 부모 바꾸기 · 컴포넌트 추가/제거 · 토글 · 열거 속성)을 커맨드로
// 만들었습니다. 부모를 바꿔도 월드 자세가 그대로여야 하고, 여러 액터를 기즈모로 함께 움직이면 주 선택의 피벗을
// 중심으로 돌아야 하며, 복사한 글은 다른 프로그램의 글과 구별되어야 합니다.
// 실행: build/Release/HarukaEditCmdTest.exe
#include "../src/editor/EditorCommands.hpp"
#include "../src/editor/EditorCommandRegistry.hpp"
#include "../src/editor/EditorIcons.hpp"
#include "../src/editor/EditorMenuModel.hpp"
#include "../src/editor/OutputLogModel.hpp"
#include "../src/editor/ConsoleHistoryModel.hpp"
#include "../src/vfs/Vfs.hpp"
#include "../src/editor/EditorTheme.hpp"
#include "../src/editor/EditTools.hpp"
#include "../src/editor/AssetPickerModel.hpp"
#include "../src/editor/ColorPickerModel.hpp"
#include "../src/sceneio/PrefabRules.hpp"
#include "../src/scene/World.hpp"
#include "../src/scene/ComponentRegistry.hpp"

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

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }
bool near4(const glm::vec4& a, const glm::vec4& b, float eps = 1e-3f) {
    return std::abs(a.r - b.r) < eps && std::abs(a.g - b.g) < eps &&
           std::abs(a.b - b.b) < eps && std::abs(a.a - b.a) < eps;
}
bool nearQ(const glm::quat& a, const glm::quat& b, float eps = 1e-4f) { return std::abs(std::abs(glm::dot(a, b)) - 1.f) < eps; }

// 숫자 · 토글 · 열거 속성과 직렬화 값을 가진 시험용 컴포넌트
class TestComp : public Component {
public:
    float speed = 1.f;
    bool  loud  = false;
    int   mode  = 0;
    int   committed = 0;
    const char* typeName() const override { return "TestComp"; }
    std::vector<PropDesc> buildProps() override {
        auto* p = this;
        return { makePropFloat("Speed", PropColor::Neutral, 0.1f, [p] { return p->speed; }, [p](float v) { p->speed = v; }),
                 makePropToggle("Loud", [p] { return p->loud; }, [p](bool v) { p->loud = v; }),
                 makePropEnum("Mode", { "A", "B", "C" }, [p] { return p->mode; }, [p](int v) { p->mode = v; }) };
    }
    void onPropsCommitted() override { ++committed; }
    void writeFields(ComponentFieldWriter& w) const override { w.f("speed", speed); w.b("loud", loud); w.i("mode", mode); }
    bool readField(const std::string& k, const std::string& v) override {
        if (k == "speed") return fieldparse::toFloat(v, speed);
        if (k == "loud")  return fieldparse::toBool(v, loud);
        if (k == "mode")  return fieldparse::toInt(v, mode);
        return false;
    }
};
class OtherComp : public Component {
public:
    const char* typeName() const override { return "OtherComp"; }
};

// 토글 · 열거에 따라 칸이 나타나고 사라지는 시험용 컴포넌트(라이트의 Use Temperature · Type 과 같은 모양, DEF-131).
// 순서: Kind, Base, [Use Warm 이 켜졌을 때 Warm], [Kind == 1 일 때 Reach], [Kind == 2 일 때 Cone, Lit 토글]
class CondComp : public Component {
public:
    int   kind = 0;
    float base = 1.f;
    bool  useWarm = false;
    float warm = 6500.f;
    float reach = 10.f;
    float cone = 30.f;
    bool  lit = true;
    const char* typeName() const override { return "CondComp"; }
    std::vector<PropDesc> buildProps() override {
        auto* p = this;
        std::vector<PropDesc> out;
        out.push_back(makePropEnum("Kind", { "A", "B", "C" }, [p] { return p->kind; }, [p](int v) { p->kind = v; }));
        out.push_back(makePropFloat("Base", PropColor::Neutral, 0.1f, [p] { return p->base; }, [p](float v) { p->base = v; }));
        out.push_back(makePropToggle("Use Warm", [p] { return p->useWarm; }, [p](bool v) { p->useWarm = v; }));
        if (useWarm)
            out.push_back(makePropFloat("Warm", PropColor::Neutral, 50.f, [p] { return p->warm; }, [p](float v) { p->warm = v; }));
        if (kind == 1)
            out.push_back(makePropFloat("Reach", PropColor::Neutral, 0.1f, [p] { return p->reach; }, [p](float v) { p->reach = v; }));
        if (kind == 2) {
            out.push_back(makePropFloat("Cone", PropColor::Neutral, 0.5f, [p] { return p->cone; }, [p](float v) { p->cone = v; }));
            out.push_back(makePropToggle("Lit", [p] { return p->lit; }, [p](bool v) { p->lit = v; }));
        }
        return out;
    }
};


// ── H2b: 명령 레지스트리 · 단축키 표 · 메뉴 모델 ─────────────────────────────────────────────────────────────────────
// 예전(H2a 까지) EditorApp::tickShortcuts 의 if 사다리를 그대로 옮긴 기준입니다. 결과 = 실행할 동작 이름 · 끌기 알림.
// 새 레지스트리(heditorcmd::defaultSpecs + resolveShortcut)가 모든 입력 조합에서 이것과 같아야 합니다.
struct OldShortcutIn {
    bool z, y, s, f, o, n, d;   // 이번 프레임 엣지
    bool ctrl, shift, modal, cam, text, drag;
};
struct OldShortcutOut {
    std::string id;
    bool notice = false;
};
OldShortcutOut oldTickShortcuts(const OldShortcutIn& k) {
    OldShortcutOut r;
    if (k.modal) return r;
    if (k.cam) return r;
    if (k.f && !k.ctrl && !k.text) { r.id = "View.FrameSelected"; return r; }
    if (!k.ctrl) return r;
    if (k.drag) {
        if (k.s || k.d || k.o || k.n) r.notice = true;
        return r;
    }
    if (k.d && !k.text) { r.id = "Edit.Duplicate"; return r; }
    if (k.s) { r.id = k.shift ? "File.SaveSceneAs" : "File.SaveScene"; return r; }
    if (k.o) { r.id = "File.OpenScene"; return r; }
    if (k.n) { r.id = "File.NewScene"; return r; }
    if (k.f) { r.id = "Outliner.FocusSearch"; return r; }
    if (k.text) return r;
    if (k.z && !k.shift)            r.id = "Edit.Undo";
    else if (k.y || (k.z && k.shift)) r.id = "Edit.Redo";
    return r;
}

void testCommandRegistry() {
    std::printf("\n== H2b-1. 명령 레지스트리: 정의 · 실행 가능 · 체크 · 단축키 글 ==\n");
    EditorCommandRegistry reg;
    heditorcmd::addDefaultSpecs(reg);
    check(reg.size() == heditorcmd::defaultSpecs().size(), "정의 표를 모두 등록(" + std::to_string(reg.size()) + ")");
    // 메뉴에 보이던 단축키 글(예전 EditorApp::buildMenus 의 문자열)과 chord 에서 만든 글이 같음
    const std::pair<const char*, const char*> shown[] = {
        { "File.NewScene", "Ctrl+N" }, { "File.OpenScene", "Ctrl+O" }, { "File.SaveScene", "Ctrl+S" },
        { "File.SaveSceneAs", "Ctrl+Shift+S" }, { "Edit.Undo", "Ctrl+Z" }, { "Edit.Redo", "Ctrl+Y" },
        { "Edit.Copy", "Ctrl+C" }, { "Edit.Paste", "Ctrl+V" }, { "Edit.Duplicate", "Ctrl+D" },
        { "Edit.Delete", "Del" }, { "View.FrameSelected", "F" }, { "View.GizmoSize", "" }, { "Actor.AddEmpty", "" },
    };
    int textOk = 0;
    for (const auto& [id, sc] : shown) textOk += reg.shortcutText(id) == sc ? 1 : 0;
    check(textOk == (int)std::size(shown), "메뉴 단축키 글 = 예전 글 13개(chord 에서 만듦)");
    // 동작이 없으면 실행 불가
    check(!reg.canExecute("File.SaveScene") && !reg.execute("File.SaveScene"), "동작을 붙이기 전에는 실행 안 함");
    check(!reg.bind("No.Such", [] {}), "없는 id 에는 붙지 않음");
    int saved = 0, undone = 0;
    bool canUndo = false;
    reg.bind("File.SaveScene", [&] { ++saved; });
    reg.bind("Edit.Undo", [&] { ++undone; }, [&] { return canUndo; });
    check(reg.execute("File.SaveScene") && saved == 1, "실행");
    check(!reg.canExecute("Edit.Undo") && !reg.execute("Edit.Undo") && undone == 0, "canExecute 거짓이면 실행 안 함");
    canUndo = true;
    check(reg.execute("Edit.Undo") && undone == 1, "canExecute 참이 되면 실행");
    bool on = false;
    EditorCommand lang;
    lang.id = "View.Language.ko";
    lang.label = HText::fromString("ko");
    lang.execute = [] {};
    lang.isChecked = [&] { return on; };
    reg.add(lang);
    check(reg.hasCheck("View.Language.ko") && !reg.isChecked("View.Language.ko"), "체크 명령 · 꺼짐");
    on = true;
    check(reg.isChecked("View.Language.ko") && !reg.hasCheck("File.SaveScene"), "체크 켜짐 · 체크 없는 명령");
    const size_t before = reg.size();
    lang.label = HText::fromString("Korean");
    reg.add(lang);
    check(reg.size() == before && reg.label("View.Language.ko") == "Korean", "같은 id 는 그 자리에서 바뀜(우선순위 유지)");
    // polled 키 = 예전 tickShortcuts 가 엣지를 세던 7개(Z Y S F O N D), 표시 전용(Delete · C · V)은 빠짐
    std::vector<int> keys = reg.polledKeys();
    std::sort(keys.begin(), keys.end());
    std::vector<int> want = { HInput::KEY_Z, HInput::KEY_Y, HInput::KEY_S, HInput::KEY_F, HInput::KEY_O, HInput::KEY_N, HInput::KEY_D };
    std::sort(want.begin(), want.end());
    check(keys == want, "단축키 엣지를 세는 키 = 예전 7개(Delete · Ctrl+C · Ctrl+V 는 글 칸 처리기 몫)");
}

void testShortcutTable() {
    std::printf("\n== H2b-2. 단축키 표가 예전과 같음(모든 입력 조합) ==\n");
    EditorCommandRegistry reg;
    heditorcmd::addDefaultSpecs(reg);
    int mismatch = 0, total = 0;
    std::vector<std::string> reached;
    for (uint32_t m = 0; m < (1u << 13); ++m) {
        OldShortcutIn k{};
        k.z = m & 1; k.y = m & 2; k.s = m & 4; k.f = m & 8; k.o = m & 16; k.n = m & 32; k.d = m & 64;
        k.ctrl = m & 128; k.shift = m & 256; k.modal = m & 512; k.cam = m & 1024; k.text = m & 2048; k.drag = m & 4096;
        const OldShortcutOut want = oldTickShortcuts(k);
        EditorShortcutInput in;
        in.keyEdge = [&k](int key) {
            switch (key) {
                case HInput::KEY_Z: return k.z;
                case HInput::KEY_Y: return k.y;
                case HInput::KEY_S: return k.s;
                case HInput::KEY_F: return k.f;
                case HInput::KEY_O: return k.o;
                case HInput::KEY_N: return k.n;
                case HInput::KEY_D: return k.d;
                default: return false;
            }
        };
        in.ctrl = k.ctrl; in.shift = k.shift; in.modal = k.modal; in.cameraMode = k.cam; in.textFocus = k.text; in.editDrag = k.drag;
        const EditorShortcutResult got = reg.resolveShortcut(in);
        const std::string gotId = got.command ? got.command->id : std::string{};
        ++total;
        if (gotId != want.id || got.dragNotice != want.notice) {
            if (mismatch < 5)
                std::printf("    차이: 입력 %u 예전 '%s'(%d) 지금 '%s'(%d)\n", m, want.id.c_str(), (int)want.notice, gotId.c_str(),
                            (int)got.dragNotice);
            ++mismatch;
        }
        if (!gotId.empty() && std::find(reached.begin(), reached.end(), gotId) == reached.end()) reached.push_back(gotId);
    }
    check(mismatch == 0, "8,192 조합(키 엣지 7 · Ctrl · Shift · 대화상자 · 카메라 비행 · 글 칸 · 끄는 중) 모두 같음(차이 " +
                             std::to_string(mismatch) + ")");
    check(reached.size() == 9, "닿는 명령 9개(F · Ctrl+D · S · Shift+S · O · N · F · Z · Y)");
    // 대표 줄(표의 몇 줄을 사람이 읽을 수 있게)
    auto one = [&](int key, bool ctrl, bool shift, bool text, bool drag) {
        EditorShortcutInput in;
        in.keyEdge = [key](int k2) { return k2 == key; };
        in.ctrl = ctrl; in.shift = shift; in.textFocus = text; in.editDrag = drag;
        const EditorShortcutResult r = reg.resolveShortcut(in);
        return (r.command ? r.command->id : std::string("-")) + (r.dragNotice ? "!" : "");
    };
    check(one(HInput::KEY_Z, true, true, false, false) == "Edit.Redo", "Ctrl+Shift+Z = Redo");
    check(one(HInput::KEY_F, false, false, true, false) == "-", "글 칸 포커스 중 F 는 맞추기 안 함");
    check(one(HInput::KEY_F, true, false, true, false) == "Outliner.FocusSearch", "Ctrl+F 는 글 칸 중에도 검색 칸으로");
    check(one(HInput::KEY_S, true, false, true, false) == "File.SaveScene", "Ctrl+S 는 글 칸 중에도 저장(예전과 같음)");
    check(one(HInput::KEY_D, true, false, false, true) == "-!", "끄는 중 Ctrl+D 는 막고 알림");
    check(one(HInput::KEY_Z, true, false, false, true) == "-", "끄는 중 Ctrl+Z 는 알림 없이 막음");
    check(one(HInput::KEY_F, false, false, false, true) == "View.FrameSelected", "끄는 중에도 F(Ctrl 없음)는 맞추기(예전과 같음)");
}

void testShortcutAndMenuSameCommand() {
    std::printf("\n== H2b-3. 단축키 · 메뉴 · 툴바가 같은 명령 ==\n");
    EditorCommandRegistry reg;
    heditorcmd::addDefaultSpecs(reg);
    std::vector<std::string> ran;
    bool hasSel = false;
    for (const EditorCommand& c : heditorcmd::defaultSpecs()) {
        const std::string id = c.id;
        reg.bind(id, [&ran, id] { ran.push_back(id); },
                 (id == "Edit.Duplicate" || id == "Edit.Copy") ? std::function<bool()>([&hasSel] { return hasSel; })
                                                                : std::function<bool()>());
    }
    // 단축키 Ctrl+S → 실행
    EditorShortcutInput in;
    in.keyEdge = [](int k) { return k == HInput::KEY_S; };
    in.ctrl = true;
    const EditorShortcutResult r = reg.resolveShortcut(in);
    if (r.command) reg.execute(r.command->id);
    // 메뉴 항목(명령 id) → 행 → 실행
    const std::vector<EditorMenuEntry> file = { EditorMenuEntry::heading("Save"), EditorMenuEntry::cmd("File.SaveScene") };
    const std::vector<EditorMenuRow> rows = hmenu::resolve(file, &reg);
    check(rows.size() == 2 && rows[1].label == "Save Scene" && rows[1].shortcut == "Ctrl+S" && rows[1].icon == "save",
          "메뉴 행의 글 · 단축키 · 아이콘은 명령에서");
    reg.execute(file[(size_t)rows[1].entry].command);
    check(ran.size() == 2 && ran[0] == "File.SaveScene" && ran[1] == "File.SaveScene", "단축키 Ctrl+S 와 메뉴 Save Scene 이 같은 명령을 부름");
    // 비활성: canExecute 거짓이면 메뉴 행이 꺼지고 단축키도 실행하지 않음
    const std::vector<EditorMenuRow> dup = hmenu::resolve({ EditorMenuEntry::cmd("Edit.Duplicate") }, &reg);
    check(dup.size() == 1 && !dup[0].enabled && !hmenu::selectable(dup[0]), "선택이 없으면 Duplicate 행은 꺼짐(흐리게)");
    EditorShortcutInput d;
    d.keyEdge = [](int k) { return k == HInput::KEY_D; };
    d.ctrl = true;
    const EditorShortcutResult rd = reg.resolveShortcut(d);
    check(rd.command && rd.command->id == "Edit.Duplicate" && !reg.execute(rd.command->id) && ran.size() == 2,
          "Ctrl+D 는 같은 명령을 고르지만 canExecute 거짓이라 아무것도 안 함(예전 duplicateSelection 의 빈 선택과 같음)");
    hasSel = true;
    check(hmenu::resolve({ EditorMenuEntry::cmd("Edit.Duplicate") }, &reg)[0].enabled && reg.execute("Edit.Duplicate"),
          "선택이 생기면 켜지고 실행");
    check(reg.execute("Play.Play") && ran.back() == "Play.Play", "툴바 Play 도 레지스트리 명령");
    // 정의되지 않은 명령 id 는 메뉴에 나오지 않음
    check(hmenu::resolve({ EditorMenuEntry::cmd("Nope") }, &reg).empty(), "없는 명령은 행이 생기지 않음");
}

void testMenuModel() {
    std::printf("\n== H2b-4. 메뉴 모델: 키보드 이동 · 하위 메뉴 · 비활성 ==\n");
    using E = EditorMenuEntry;
    using K = hmenu::Key;
    using A = hmenu::Act;
    int hits = 0;
    const std::vector<E> entries = {
        E::heading("A"),
        E::act("one", [&] { ++hits; }),
        E::act("off", [&] { ++hits; }, false),
        E::separator(),
        E::heading("B"),
        E::submenu("sub", { E::act("x", [] {}) }),
        E::submenu("empty", {}),
        E::act("two", [&] { ++hits; }),
    };
    const std::vector<EditorMenuRow> rows = hmenu::resolve(entries, nullptr);
    // 두 번째 소제목 앞 구분선은 이미 있으므로 더하지 않음 → 8행
    check(rows.size() == 8, "행 8개(이미 구분선이 있으면 소제목 앞 자동 구분선 없음)");
    const std::vector<EditorMenuRow> rows2 = hmenu::resolve({ E::heading("A"), E::act("a", [] {}), E::heading("B"), E::act("b", [] {}) }, nullptr);
    check(rows2.size() == 5 && rows2[2].kind == hmenu::Kind::Separator && rows2[2].entry == -1, "두 번째 소제목 앞에 구분선 자동");
    check(!hmenu::selectable(rows[0]) && hmenu::selectable(rows[1]) && !hmenu::selectable(rows[2]) && !hmenu::selectable(rows[3]),
          "소제목 · 꺼진 항목 · 구분선은 고를 수 없음");
    check(!hmenu::selectable(rows[6]), "빈 하위 메뉴는 꺼짐");
    check(hmenu::firstSelectable(rows) == 1, "처음 고를 수 있는 행 = one");
    check(hmenu::step(rows, 1, +1) == 5, "↓: one → sub(꺼진 항목 · 구분선 · 소제목 건너뜀)");
    check(hmenu::step(rows, 5, +1) == 7, "↓: sub → two(빈 하위 메뉴 건너뜀)");
    check(hmenu::step(rows, 7, +1) == 1, "↓: 끝에서 처음으로 돎");
    check(hmenu::step(rows, 1, -1) == 7, "↑: 처음에서 끝으로 돎");
    check(hmenu::step(rows, -1, -1) == 7, "강조가 없을 때 ↑ = 마지막");
    check(hmenu::step(hmenu::resolve({ E::heading("H"), E::act("x", [] {}, false) }, nullptr), -1, +1) == -1,
          "고를 행이 없으면 -1");
    // decide
    check(hmenu::decide(K::Enter, 1, &rows[1], true) == A::Activate, "Enter: 명령 실행");
    check(hmenu::decide(K::Enter, 1, &rows[5], true) == A::OpenSub, "Enter: 하위 메뉴 열기");
    check(hmenu::decide(K::Enter, 1, &rows[2], true) == A::None, "Enter: 꺼진 항목은 무동작");
    check(hmenu::decide(K::Enter, 1, nullptr, true) == A::None, "Enter: 강조 없음 = 무동작");
    check(hmenu::decide(K::Right, 1, &rows[5], true) == A::OpenSub, "→: 하위 메뉴 열기");
    check(hmenu::decide(K::Right, 1, &rows[1], true) == A::NextMenu, "→: 메뉴 줄의 다음 메뉴");
    check(hmenu::decide(K::Right, 1, &rows[1], false) == A::None, "→: 오른쪽 누름 메뉴는 무동작");
    check(hmenu::decide(K::Right, 1, &rows[6], true) == A::NextMenu && hmenu::decide(K::Enter, 1, &rows[6], true) == A::None,
          "꺼진(빈) 하위 메뉴는 → · Enter 로 열지 않음");
    check(hmenu::decide(K::Left, 2, &rows[1], true) == A::CloseSub, "←: 하위 메뉴 안이면 닫기");
    check(hmenu::decide(K::Left, 1, &rows[1], true) == A::PrevMenu, "←: 맨 위면 이전 메뉴");
    check(hmenu::decide(K::Escape, 2, nullptr, true) == A::CloseSub && hmenu::decide(K::Escape, 1, nullptr, true) == A::Close,
          "Esc: 한 단계씩(하위 → 부모, 맨 위 → 전체)");
    check(hmenu::decide(K::Up, 1, nullptr, false) == A::MoveUp && hmenu::decide(K::Down, 3, &rows[5], true) == A::MoveDown, "↑ ↓ 이동");
    hmenu::Key pk;
    check(hmenu::parseKey("esc", pk) && pk == K::Escape && !hmenu::parseKey("tab", pk), "키 이름 읽기");
    // 체크 · 단축키(그 자리 동작)
    E chk = E::act("c", [] {});
    chk.checked = [] { return true; };
    chk.shortcut = "Ctrl+Q";
    const std::vector<EditorMenuRow> r3 = hmenu::resolve({ chk }, nullptr);
    check(r3.size() == 1 && r3[0].hasCheck && r3[0].checked && r3[0].shortcut == "Ctrl+Q", "그 자리 동작의 체크 · 단축키 글");
    check(hits == 0, "풀기(resolve)는 동작을 부르지 않음");
}

} // namespace

// ── H3a: 속성 기본값 · 되돌리기 · 글 바꾸기 · 새 종류(String · Vec3 · Color) ─────────────────────────────────────────
class H3Comp : public Component {
public:
    float       speed = 1.f;
    bool        loud  = false;
    int         mode  = 0;
    std::string tag;
    glm::vec3   offset{ 0.f };
    glm::vec4   tint{ 1.f, 1.f, 1.f, 1.f };
    int         committed = 0;
    const char* typeName() const override { return "H3Comp"; }
    std::vector<PropDesc> buildProps() override {
        auto* p = this;
        return { makePropFloat("Speed", PropColor::Neutral, 0.1f, [p] { return p->speed; }, [p](float v) { p->speed = std::max(0.f, v); })
                     .withDefault(1.f),
                 makePropToggle("Loud", [p] { return p->loud; }, [p](bool v) { p->loud = v; }).withDefault(false),
                 makePropEnum("Mode", { "A", "B", "C" }, [p] { return p->mode; }, [p](int v) { p->mode = v; }).withDefault(0.f),
                 XPROP_String(p, "Tag", tag).withDefault(""),
                 makePropVec3("Offset", 0.01f, [p] { return p->offset; }, [p](const glm::vec3& v) { p->offset = v; }, PropUnit::Offset)
                     .withDefault(glm::vec3(0.f)),
                 makePropColor("Tint", [p] { return p->tint; }, [p](const glm::vec4& v) { p->tint = glm::clamp(v, 0.f, 1.f); }, true)
                     .withDefault(glm::vec4(1.f)),
                 makePropFloat("NoDefault", PropColor::Neutral, 0.1f, [p] { return p->speed * 2.f; }, [](float) {}) };
    }
    void onPropsCommitted() override { ++committed; }
};

// ── H3b: 에셋 경로 칸 · 색 선택기 ───────────────────────────────────────────────────────────────────────────────
class H3bComp : public Component {
public:
    std::string model;
    std::string sound;
    glm::vec3   color{ 1.f, 0.5f, 0.25f };
    int         reloads = 0;
    int         committed = 0;
    const char* typeName() const override { return "H3bComp"; }
    std::vector<PropDesc> buildProps() override {
        auto* p = this;
        return { makePropAssetPath("Model", "Model", [p] { return p->model; },
                                   [p](const std::string& v) { if (v != p->model) { p->model = v; ++p->reloads; } }),
                 makePropAssetPath("File", "Sound,Sound Cue", [p] { return p->sound; }, [p](const std::string& v) { p->sound = v; }),
                 makePropColor("Color", [p] { return glm::vec4(p->color, 1.f); },
                               [p](const glm::vec4& v) { p->color = glm::clamp(glm::vec3(v), glm::vec3(0.f), glm::vec3(1.f)); }) };
    }
    void onPropsCommitted() override { ++committed; }
};

const PropDesc* findPd(const std::vector<PropDesc>& v, const char* label);

void testAssetField() {
    std::printf("\n== H3b-1. 에셋 경로 칸: 종류 거르기 · 검색 · 선택 · 비우기 · Undo 한 단계 ==\n");
    hcb::ContentIndex idx;
    idx.build({ "models/model.glb", "models/hands/TestGlove_L.glb", "models/Crate.gltf", "audio/Test_Chime.wav",
                "audio/Music.ogg", "audio/Alarm.hcue", "scenes/A.hscene", "prefabs/Lamp.hprefab", "notes.txt" },
              {});
    const auto kModel = hasset::kindsOf("Model");
    const auto kSound = hasset::kindsOf("Sound, Sound Cue");
    check(kModel.size() == 1 && kModel[0] == hcb::AssetKind::Model, "종류 이름 하나(Model)");
    check(kSound.size() == 2 && kSound[1] == hcb::AssetKind::Cue, "쉼표 · 공백으로 둘(Sound · Sound Cue)");
    check(hasset::kindsOf("Nope").empty() && hasset::kindsOf("").empty(), "모르는 이름 · 빈 글 = 모든 종류");
    auto models = hasset::buildList(idx, kModel, "");
    check(models.size() == 3 && models[0].name == "Crate" && models[1].name == "model" && models[2].name == "TestGlove_L",
          "Model 만 3개 · 이름 순(대소문자 무시)");
    auto sounds = hasset::buildList(idx, kSound, "");
    check(sounds.size() == 3 && sounds[0].name == "Alarm", "Sound + Sound Cue 3개(큐 포함)");
    auto all = hasset::buildList(idx, {}, "");
    check(all.size() == 8, "빈 종류 = 에셋 모두(기타 · 폴더 제외, " + std::to_string(all.size()) + ")");
    check(hasset::buildList(idx, kModel, "GLOVE").size() == 1, "검색: 대소문자 무시");
    check(hasset::buildList(idx, kModel, "test l").size() == 1 && hasset::buildList(idx, kModel, "test x").empty(), "검색: 낱말 모두 맞아야 함");
    check(hasset::buildList(idx, kModel, "hands").empty(), "검색: 폴더 경로는 보지 않음(UE 와 같음)");
    check(hasset::indexOf(models, "/Game/models/model.glb") == 1 && hasset::indexOf(models, "/Game/x.glb") == -1, "지금 값 자리");
    check(hasset::step(3, -1, +1) == 0 && hasset::step(3, -1, -1) == 2 && hasset::step(3, 2, +1) == 2 && hasset::step(3, 0, -1) == 0 &&
              hasset::step(0, 0, 1) == -1,
          "↑ ↓: 끝에서 멈춤 · 고른 것 없으면 처음/끝");
    check(hasset::acceptsPath(kModel, "/Game/models/Crate.gltf") && !hasset::acceptsPath(kModel, "/Game/audio/Test_Chime.wav") &&
              !hasset::acceptsPath(kModel, "") && hasset::acceptsPath(kSound, "/Game/audio/Alarm.hcue"),
          "끌어 놓기: 종류가 맞을 때만 받음");
    check(hasset::displayName("/Game/models/hands/TestGlove_L.glb") == "TestGlove_L" && hasset::displayName("").empty(), "칸에 보일 이름");

    World w;
    HCommandStack st;
    Actor* a = w.spawnActor("a");
    auto* comp = a->addComponent<H3bComp>();
    Actor* b = w.spawnActor("b");
    auto* cb = b->addComponent<H3bComp>();
    auto setPath = [&](const std::string& v) {
        return hcmd::commitPropChange(w, a, comp, { cb }, [&] {
            hcmd::withProp(*comp, "Model", PropDesc::AssetPath, [&](const PropDesc& pd) { pd.setS(v); });
            hcmd::PropValue e; e.label = "Model"; e.kind = PropDesc::AssetPath; e.s = v;
            hcmd::setProp(*cb, e);
        }, st);
    };
    check(setPath("/Game/models/model.glb") && comp->model == "/Game/models/model.glb" && cb->model == comp->model && comp->reloads == 1 &&
              comp->committed == 1,
          "고르기 = 값 · 함께 고른 액터 · 다시 읽기 한 번 · 확정 훅");
    check(st.canUndo(), "Undo 기록");
    st.undo();
    check(comp->model.empty() && cb->model.empty() && !st.canUndo(), "Undo 한 번에 두 액터 모두 이전 값(한 단계)");
    st.redo();
    check(comp->model == "/Game/models/model.glb" && cb->model == comp->model, "Redo");
    const uint64_t tok = st.stateToken();
    check(!setPath("/Game/models/model.glb") && st.stateToken() == tok, "같은 값은 기록 없음");
    check(setPath("") && comp->model.empty(), "비우기 = 빈 경로");
    st.undo();
    check(comp->model == "/Game/models/model.glb", "비우기 Undo 한 단계");
    auto props = comp->buildProps();
    const PropDesc& pm = *findPd(props, "Model");
    check(pm.kind == PropDesc::AssetPath && std::string(pm.assetType) == "Model" && !pm.hasDefault, "AssetPath 종류 · 기본값 없음");
    check(hprop::valueText(pm) == "/Game/models/model.glb" && hprop::setFromText(pm, "/Game/models/Crate.gltf") &&
              comp->model == "/Game/models/Crate.gltf",
          "복사 · 붙여넣기 글 = 경로");
}

void testColorMath() {
    std::printf("\n== H3b-2. 색 변환: RGB ↔ HSV · 선형 ↔ sRGB · 16진수 ==\n");
    float worstHsv = 0.f, worstSrgb = 0.f;
    for (int r = 0; r <= 8; ++r)
        for (int g = 0; g <= 8; ++g)
            for (int b = 0; b <= 8; ++b) {
                const glm::vec3 c{ r / 8.f, g / 8.f, b / 8.f };
                const glm::vec3 back = hcolor::hsvToRgb(hcolor::rgbToHsv(c));
                worstHsv = std::max(worstHsv, glm::length(back - c));
                const glm::vec3 lin = hcolor::srgbToLinear(c);
                worstSrgb = std::max(worstSrgb, glm::length(hcolor::linearToSrgb(lin) - c));
            }
    check(worstHsv < 1e-5f, "RGB → HSV → RGB 왕복 729색 최대 오차 " + std::to_string(worstHsv));
    check(worstSrgb < 1e-5f, "sRGB → 선형 → sRGB 왕복 729색 최대 오차 " + std::to_string(worstSrgb));
    const glm::vec3 red = hcolor::rgbToHsv({ 1.f, 0.f, 0.f }), green = hcolor::rgbToHsv({ 0.f, 1.f, 0.f });
    const glm::vec3 blue = hcolor::rgbToHsv({ 0.f, 0.f, 1.f }), mag = hcolor::rgbToHsv({ 1.f, 0.f, 1.f });
    check(red.x == 0.f && std::abs(green.x - 120.f) < 1e-4f && std::abs(blue.x - 240.f) < 1e-4f && std::abs(mag.x - 300.f) < 1e-4f,
          "색상 0 · 120 · 240 · 300 도");
    check(hcolor::rgbToHsv({ 0.5f, 0.5f, 0.5f }, 77.f).x == 77.f && hcolor::rgbToHsv({ 0.f, 0.f, 0.f }, 33.f).x == 33.f,
          "회색 · 검정은 색상 힌트를 그대로");
    const glm::vec3 hdr = hcolor::rgbToHsv({ 4.f, 2.f, 0.f });
    check(std::abs(hdr.z - 4.f) < 1e-5f && std::abs(hdr.y - 1.f) < 1e-5f && glm::length(hcolor::hsvToRgb(hdr) - glm::vec3(4.f, 2.f, 0.f)) < 1e-4f,
          "HDR(V > 1) 왕복");
    check(std::abs(hcolor::srgbToLinear(0.5f) - 0.214041f) < 1e-5f && std::abs(hcolor::linearToSrgb(0.214041f) - 0.5f) < 1e-5f &&
              std::abs(hcolor::srgbToLinear(0.04f) - 0.04f / 12.92f) < 1e-7f,
          "sRGB 0.5 = 선형 0.2140 · 선형 구간");
    glm::vec4 v;
    bool alpha = true;
    check(hcolor::parseHex("#FF8000", v, &alpha) && !alpha && std::abs(v.r - 1.f) < 1e-6f && std::abs(v.g - 128.f / 255.f) < 1e-6f &&
              v.b == 0.f && v.a == 1.f,
          "#RRGGBB");
    check(hcolor::parseHex("ff800080", v, &alpha) && alpha && std::abs(v.a - 128.f / 255.f) < 1e-6f, "RRGGBBAA(소문자)");
    check(hcolor::parseHex("F80", v, &alpha) && !alpha && std::abs(v.g - 136.f / 255.f) < 1e-6f, "짧은 RGB(F → FF)");
    check(hcolor::parseHex(" 0x00FF00 ", v) && v.g == 1.f, "0x · 앞뒤 공백");
    const glm::vec4 keep = v;
    check(!hcolor::parseHex("GG0000", v) && !hcolor::parseHex("12345", v) && !hcolor::parseHex("", v) && v == keep, "틀린 모양 거절(값 그대로)");
    int worstHex = 0;
    for (int i = 0; i < 256; ++i) {
        glm::vec4 c;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02X%02X%02X", i, 255 - i, (i * 7) & 255);
        hcolor::parseHex(buf, c);
        if (hcolor::toHex(c, false) != buf) ++worstHex;
    }
    check(worstHex == 0, "16진수 → 값 → 16진수 256가지 같음");
}

void testColorPicker() {
    std::printf("\n== H3b-3. 색 선택기: 색 공간 · 확정 = Undo 한 단계 · 취소 = 원래 값 ==\n");
    hcolorpick::State lin;
    lin.open({ 0.215861f, 0.215861f, 0.215861f, 1.f }, 3, false);
    check(lin.hex() == "808080", "선형 값의 16진수는 sRGB(선형 0.2159 = #808080)");
    check(lin.setHex("FF0000") && std::abs(lin.value.r - 1.f) < 1e-6f && lin.value.g == 0.f, "16진수 넣기(선형 칸)");
    check(lin.setHex("808080") && std::abs(lin.value.r - 0.215861f) < 1e-4f, "16진수 #80 → 선형 0.2159(sRGB 128/255)");
    hcolorpick::State sr;
    sr.open({ 0.5f, 0.5f, 0.5f, 1.f }, 3, true);
    check(sr.hex() == "808080" && std::abs(sr.displayValue().r - 0.214041f) < 1e-5f, "sRGB 칸: 16진수는 그대로 · 그리는 색은 선형");
    check(sr.setHex("FF8000") && std::abs(sr.value.g - 128.f / 255.f) < 1e-6f, "sRGB 칸 16진수 넣기는 바꾸지 않음");
    hcolorpick::State st;
    st.open({ 1.f, 0.f, 0.f, 1.f }, 3, false);
    st.setHue(120.f);
    check(glm::length(glm::vec3(st.value) - glm::vec3(0.f, 1.f, 0.f)) < 1e-5f, "색상 막대 120 도 = 초록");
    st.setPlane(0.f, 0.f);
    check(glm::length(glm::vec3(st.value) - glm::vec3(1.f)) < 1e-5f && std::abs(st.hsv.x - 120.f) < 1e-4f, "채도 0 = 흰색, 색상 120 기억");
    st.setPlane(1.f, 0.f);
    check(glm::length(glm::vec3(st.value) - glm::vec3(0.f, 1.f, 0.f)) < 1e-5f, "채도를 다시 올리면 같은 색상");
    st.setPlane(1.f, 1.f);
    check(glm::length(glm::vec3(st.value)) < 1e-6f && std::abs(st.hsv.x - 120.f) < 1e-4f, "명도 0 = 검정, 색상 기억");
    st.setValue({ 0.f, 0.f, 0.f, 1.f });
    st.setPlane(1.f, 0.f);
    check(glm::length(glm::vec3(st.value) - glm::vec3(0.f, 1.f, 0.f)) < 1e-5f, "검정에서 명도를 올려도 같은 색상");
    hcolorpick::State hd;
    hd.open({ 4.f, 2.f, 0.f, 1.f }, 3, false);
    check(std::abs(hd.vScale - 4.f) < 1e-5f && hd.planeMarker().y < 1e-5f && hd.hex() == "FFFF00" &&
              hd.displayValue() == glm::vec4(1.f, 1.f, 0.f, 1.f),
          "HDR: 판 세로 범위 = V(4) · 16진수 · 그리는 색은 0 ~ 1 로 자름");
    hd.setPlane(1.f, 0.5f);
    check(std::abs(hd.hsv.z - 2.f) < 1e-5f, "HDR 판 가운데 = V 2");
    hcolorpick::State al;
    al.open({ 1.f, 1.f, 1.f, 0.5f }, 4, false);
    check(al.hex() == "FFFFFF80" && al.setHex("00000040") && std::abs(al.value.a - 64.f / 255.f) < 1e-6f, "네 성분: 16진수 8자리 · 알파");
    al.setAlpha(1.2f);
    check(al.value.a == 1.f && std::abs(al.alphaMarker()) < 1e-6f, "알파 0 ~ 1 로 자름 · 막대 위 = 불투명");
    hcolorpick::State three;
    three.open({ 1.f, 1.f, 1.f, 0.3f }, 3, false);
    three.setAlpha(0.1f);
    check(three.value.a == 1.f && three.hex().size() == 6, "세 성분: 알파 없음(6자리)");

    // 세션: 연 동안 여러 번 바꿔도 확정은 Undo 한 단계, 취소는 원래 값(기록 없음)
    World w;
    HCommandStack stk;
    Actor* a = w.spawnActor("a");
    auto* comp = a->addComponent<H3bComp>();
    Actor* b = w.spawnActor("b");
    auto* cb = b->addComponent<H3bComp>();
    cb->color = { 0.f, 0.f, 1.f };
    auto setAll = [&](const glm::vec4& v) {
        hcmd::withProp(*comp, "Color", PropDesc::Color, [&](const PropDesc& pd) { pd.setV(v); });
        hcmd::withProp(*cb, "Color", PropDesc::Color, [&](const PropDesc& pd) { pd.setV(v); });
    };
    {
        hcmd::PropSession s = hcmd::beginPropSession(*comp, { cb });
        for (int i = 1; i <= 5; ++i) setAll({ i * 0.1f, 0.2f, 0.3f, 1.f });
        const int c0 = comp->committed;
        check(hcmd::endPropSession(w, a, comp, { cb }, s, stk) && comp->committed == c0 + 1, "확정 = 커맨드 · 확정 훅 한 번");
        stk.undo();
        check(glm::length(comp->color - glm::vec3(1.f, 0.5f, 0.25f)) < 1e-6f && glm::length(cb->color - glm::vec3(0.f, 0.f, 1.f)) < 1e-6f &&
                  !stk.canUndo(),
              "Undo 한 번에 두 액터 모두 연 때의 값(끈 다섯 번이 한 단계)");
        stk.redo();
        check(std::abs(comp->color.r - 0.5f) < 1e-6f && std::abs(cb->color.r - 0.5f) < 1e-6f, "Redo = 마지막 값");
    }
    {
        const uint64_t tok = stk.stateToken();
        const glm::vec3 ca = comp->color, cbb = cb->color;
        hcmd::PropSession s = hcmd::beginPropSession(*comp, { cb });
        setAll({ 0.9f, 0.9f, 0.9f, 1.f });
        hcmd::cancelPropSession(comp, { cb }, s);
        check(comp->color == ca && cb->color == cbb && stk.stateToken() == tok, "취소 = 두 액터 모두 원래 값 · 기록 없음");
        hcmd::PropSession s2 = hcmd::beginPropSession(*comp, {});
        check(!hcmd::endPropSession(w, a, comp, {}, s2, stk) && stk.stateToken() == tok, "바꾸지 않고 확정 = 기록 없음");
    }
}

const PropDesc* findPd(const std::vector<PropDesc>& v, const char* label) {
    for (const PropDesc& pd : v) if (std::string(pd.label) == label) return &pd;
    return nullptr;
}

void testPropDefaults() {
    std::printf("\n== H3a-1. 속성 기본값 비교 · 되돌리기 ==\n");
    H3Comp c;
    auto props = c.buildProps();
    const PropDesc& sp = *findPd(props, "Speed");
    const PropDesc& lo = *findPd(props, "Loud");
    const PropDesc& md = *findPd(props, "Mode");
    const PropDesc& tg = *findPd(props, "Tag");
    const PropDesc& of = *findPd(props, "Offset");
    const PropDesc& ti = *findPd(props, "Tint");
    const PropDesc& nd = *findPd(props, "NoDefault");
    check(sp.hasDefault && sp.defF == 1.f && lo.hasDefault && !lo.defB && md.hasDefault && md.defI == 0, "Float · Toggle · Enum 기본값");
    check(tg.kind == PropDesc::String && tg.hasDefault && tg.defS.empty(), "String 종류 · 기본값 빈 글");
    check(of.kind == PropDesc::Vec3 && of.components == 3 && of.defV == glm::vec4(0.f), "Vec3 종류 · 기본값 0");
    check(ti.kind == PropDesc::Color && ti.components == 4 && ti.defV == glm::vec4(1.f), "Color(RGBA) 종류 · 기본값 흰색");
    check(!nd.hasDefault && hprop::isDefault(nd) && !hprop::canReset(nd), "기본값 없는 칸은 늘 기본값(되돌릴 것 없음)");
    check(hprop::isDefault(sp) && hprop::isDefault(tg) && hprop::isDefault(of) && hprop::isDefault(ti), "처음엔 모두 기본값");
    c.speed = 1.f + 5e-7f;
    check(hprop::isDefault(sp), "float 잡음(5e-7)은 기본값으로 봄");
    c.speed = 1.001f;
    check(!hprop::isDefault(sp) && hprop::canReset(sp), "1.001 은 기본값과 다름");
    c.tag = "guard"; c.offset = { 0.f, 2.f, 0.f }; c.tint = { 1.f, 1.f, 1.f, 0.5f }; c.loud = true; c.mode = 2;
    check(!hprop::isDefault(tg) && !hprop::isDefault(of) && !hprop::isDefault(ti) && !hprop::isDefault(lo) && !hprop::isDefault(md),
          "글 · 성분(Y 하나) · 알파 · 토글 · 열거가 다르면 다름");
    for (const PropDesc& pd : props) hprop::resetToDefault(pd);
    check(c.speed == 1.f && !c.loud && c.mode == 0 && c.tag.empty() && c.offset == glm::vec3(0.f) && c.tint == glm::vec4(1.f),
          "resetToDefault 가 세터로 기본값을 넣음");
    // Color(RGB) 는 알파를 비교하지 않음
    {
        glm::vec4 rgb{ 0.2f, 0.4f, 0.6f, 1.f };
        PropDesc col = makePropColor("C", [&rgb] { return rgb; }, [&rgb](const glm::vec4& v) { rgb = v; }).withDefault(glm::vec3(0.2f, 0.4f, 0.6f));
        rgb.a = 0.f;
        check(col.components == 3 && hprop::isDefault(col) && col.defV.a == 1.f, "RGB 색 칸은 알파를 보지 않음(기본 알파 1)");
    }
    // withDefault 의 종류별 해석
    {
        bool b = false;
        PropDesc t = makePropToggle("T", [&b] { return b; }, [&b](bool v) { b = v; }).withDefault(true);
        enum class EE { X, Y, Z };
        int e = 0;
        PropDesc en = makePropEnum("E", { "X", "Y", "Z" }, [&e] { return e; }, [&e](int v) { e = v; }).withDefault(EE::Z);
        uint64_t g = 7;
        PropDesc ar = makePropActorRef("R", [&g] { return g; }, [&g](uint64_t v) { g = v; });
        check(t.defB && en.defI == 2 && ar.hasDefault && ar.defG == 0 && hprop::canReset(ar), "토글 true · 열거 열거값 · 액터 참조는 늘 없음(0)");
    }

    std::printf("\n== H3a-2. 값 글(복사 · 붙여넣기) ==\n");
    c.speed = 2.5f; c.offset = { 1.f, -2.f, 0.5f }; c.tint = { 1.f, 0.5f, 0.25f, 1.f }; c.mode = 1; c.loud = true; c.tag = "a b";
    check(hprop::valueText(sp) == "2.5" && hprop::valueText(lo) == "True" && hprop::valueText(md) == "B" && hprop::valueText(tg) == "a b",
          "수 · 토글 · 열거 항목 · 글");
    check(hprop::valueText(of) == "(X=1,Y=-2,Z=0.5)" && hprop::valueText(ti) == "(R=1,G=0.5,B=0.25,A=1)", "성분 글 (X=..) · (R=..)");
    check(hprop::setFromText(of, "(X=3,Y=4,Z=5)") && c.offset == glm::vec3(3.f, 4.f, 5.f), "성분 글 붙여넣기");
    check(hprop::setFromText(of, "Y=9") && c.offset == glm::vec3(3.f, 9.f, 5.f), "이름 있는 성분 하나만(나머지 그대로)");
    check(hprop::setFromText(of, " 1, 2 ,3 ") && c.offset == glm::vec3(1.f, 2.f, 3.f), "괄호 · 이름 없는 수 셋");
    check(!hprop::setFromText(of, "(X=1,Q=2)") && !hprop::setFromText(of, "abc") && !hprop::setFromText(of, "(X=1,Y=2,Z=3,W=4)") &&
          c.offset == glm::vec3(1.f, 2.f, 3.f), "모르는 성분 · 글 · 넘치는 성분은 거절(값 그대로)");
    check(hprop::setFromText(ti, "(R=0,G=0,B=1,A=0.5)") && c.tint == glm::vec4(0.f, 0.f, 1.f, 0.5f), "색 RGBA 붙여넣기");
    check(hprop::setFromText(sp, "4") && c.speed == 4.f && !hprop::setFromText(sp, "4x") && !hprop::setFromText(sp, "") && c.speed == 4.f,
          "수: 꼬리 쓰레기 · 빈 글 거절");
    check(hprop::setFromText(lo, "false") && !c.loud && hprop::setFromText(lo, "TRUE") && c.loud && !hprop::setFromText(lo, "yes"), "토글 글");
    check(hprop::setFromText(md, "C") && c.mode == 2 && hprop::setFromText(md, "0") && c.mode == 0 && !hprop::setFromText(md, "3") &&
          !hprop::setFromText(md, "Q"), "열거: 항목 글 · 번호, 범위 밖 거절");
    check(hprop::setFromText(tg, "  x  ") && c.tag == "  x  ", "글은 그대로(앞뒤 공백 유지)");

    std::printf("\n== H3a-3. 되돌리기 · 붙여넣기는 Undo 한 단계 ==\n");
    {
        World w;
        HCommandStack st;
        Actor* a = w.spawnActor("a");
        auto* comp = a->addComponent<H3Comp>();
        comp->speed = 3.f;
        comp->offset = { 0.f, 1.f, 0.f };
        check(hcmd::resetPropToDefault(w, a, comp, {}, "Speed", PropDesc::Float, st) && comp->speed == 1.f && st.canUndo() &&
                  comp->committed == 1,
              "되돌림 · 커맨드 하나 · 확정 훅");
        st.undo();
        check(comp->speed == 3.f && !st.canUndo() && comp->offset == glm::vec3(0.f, 1.f, 0.f), "Undo 한 번에 이전 값(다른 칸은 그대로)");
        st.redo();
        check(comp->speed == 1.f, "Redo 로 다시 기본값");
        const uint64_t tok = st.stateToken();
        check(!hcmd::resetPropToDefault(w, a, comp, {}, "Speed", PropDesc::Float, st) && st.stateToken() == tok,
              "이미 기본값이면 아무것도 쌓지 않음");
        check(!hcmd::resetPropToDefault(w, a, comp, {}, "NoDefault", PropDesc::Float, st) && st.stateToken() == tok, "기본값 없는 칸은 거절");
        check(!hcmd::resetPropToDefault(w, a, comp, {}, "Speed", PropDesc::Toggle, st) && st.stateToken() == tok, "종류가 다르면 거절");
        check(hcmd::resetPropToDefault(w, a, comp, {}, "Offset", PropDesc::Vec3, st) && comp->offset == glm::vec3(0.f), "성분 칸 되돌리기");
        st.undo();
        check(comp->offset == glm::vec3(0.f, 1.f, 0.f), "성분 칸 Undo");

        // 여러 액터: 주 선택 + 같은 컴포넌트를 한 번의 Undo 로
        Actor* b = w.spawnActor("b");
        auto* cb = b->addComponent<H3Comp>();
        cb->speed = 7.f;
        comp->speed = 5.f;
        check(hcmd::resetPropToDefault(w, a, comp, { cb }, "Speed", PropDesc::Float, st) && comp->speed == 1.f && cb->speed == 1.f,
              "함께 고른 액터도 되돌림");
        st.undo();
        check(comp->speed == 5.f && cb->speed == 7.f, "두 액터가 한 번의 Undo 로 돌아옴");
        comp->speed = 1.f;
        check(hcmd::resetPropToDefault(w, a, comp, { cb }, "Speed", PropDesc::Float, st) && cb->speed == 1.f && comp->speed == 1.f,
              "주 선택이 이미 기본값이어도 다른 액터가 다르면 되돌림");

        // 글 칸 확정(commitPropChange): 한 단계
        const uint64_t t0 = st.stateToken();
        check(hcmd::commitPropChange(w, a, comp, {}, [comp] { comp->tag = "guard"; }, st) && st.stateToken() != t0, "글 확정 = 커맨드 하나");
        st.undo();
        check(comp->tag.empty(), "글 확정 Undo");
        const uint64_t t1 = st.stateToken();
        check(!hcmd::commitPropChange(w, a, comp, {}, [] {}, st) && st.stateToken() == t1, "바뀐 것이 없으면 쌓지 않음");
    }

    std::printf("\n== H3a-4. 새 종류의 값 기억(Undo 스냅샷) · 성분 하나만 다른 액터에 ==\n");
    {
        H3Comp x;
        x.tag = "t"; x.offset = { 1.f, 2.f, 3.f }; x.tint = { 0.1f, 0.2f, 0.3f, 0.4f };
        const hcmd::PropValues v = hcmd::captureProps(x);
        check(v.count(PropDesc::String) == 1 && v.count(PropDesc::Vec3) == 1 && v.count(PropDesc::Color) == 1, "글 · 성분 · 색을 뜸");
        H3Comp y;
        hcmd::applyProps(y, v);
        check(y.tag == "t" && y.offset == x.offset && y.tint == x.tint && y.committed == 1, "다른 컴포넌트에 그대로 넣음(확정 훅 한 번)");
        check(hcmd::captureProps(y) == v, "뜬 값이 같음(비교)");
        y.offset = { 9.f, 9.f, 9.f };
        check(hcmd::setPropComponent(y, "Offset", PropDesc::Vec3, 1, -1.f) && y.offset == glm::vec3(9.f, -1.f, 9.f), "성분 하나만 바꿈");
        check(!hcmd::setPropComponent(y, "Offset", PropDesc::Color, 1, 0.f) && !hcmd::setPropComponent(y, "Nope", PropDesc::Vec3, 0, 0.f),
              "종류 · 이름이 다르면 거절");
        hcmd::PropValue e; e.label = "Tag"; e.kind = PropDesc::String; e.s = "zz";
        check(hcmd::setProp(y, e) && y.tag == "zz", "setProp 이 글 칸에도 넣음");
    }

    std::printf("\n== H3a-5. 디테일 행 메뉴 명령(레지스트리) ==\n");
    {
        EditorCommandRegistry reg;
        heditorcmd::addDefaultSpecs(reg);
        bool canReset = false;
        int resets = 0;
        reg.bind("Details.ResetToDefault", [&resets] { ++resets; }, [&canReset] { return canReset; });
        reg.bind("Details.CopyValue", [] {});
        reg.bind("Details.PasteValue", [] {});
        const std::vector<EditorMenuEntry> entries = { EditorMenuEntry::cmd("Details.ResetToDefault"), EditorMenuEntry::separator(),
                                                       EditorMenuEntry::cmd("Details.CopyValue"), EditorMenuEntry::cmd("Details.PasteValue") };
        std::vector<EditorMenuRow> rows = hmenu::resolve(entries, &reg);
        check(rows.size() == 4 && rows[0].label == "Reset to Default" && !rows[0].enabled && rows[0].icon == "rotate-ccw" && rows[2].enabled,
              "기본값과 같으면 Reset to Default 흐림");
        check(hmenu::firstSelectable(rows) == 2 && !reg.execute("Details.ResetToDefault") && resets == 0, "흐린 항목은 건너뛰고 실행도 안 됨");
        canReset = true;
        rows = hmenu::resolve(entries, &reg);
        check(rows[0].enabled && reg.execute("Details.ResetToDefault") && resets == 1, "다르면 켜지고 실행");
        check(reg.shortcutText("Details.ResetToDefault").empty(), "단축키 없음(단축키 표에 영향 없음)");
    }
}

int main() {
    std::printf("Haruka 편집 기본기 검산\n");
    ComponentRegistry::get().registerType<TestComp>("TestComp", "Test");
    ComponentRegistry::get().registerType<OtherComp>("OtherComp", "Other");

    std::printf("\n== 1. 액터: 자리 지정 붙이기 · 포인터로 컴포넌트 지우기 · 옮기기 ==\n");
    {
        World w;
        Actor* P = w.spawnActor("P");
        Actor* a = w.spawnActor("a");
        Actor* b = w.spawnActor("b");
        Actor* c = w.spawnActor("c");
        a->attachTo(P); b->attachTo(P); c->attachTo(P);
        check(c->siblingIndex() == 2, "끝에 붙음");
        c->attachTo(P, 0);
        check(c->siblingIndex() == 0 && a->siblingIndex() == 1, "같은 부모 안에서 자리만 옮김");
        a->attachTo(nullptr, 0);
        check(!a->getParent() && a->siblingIndex() == -1, "null 부모는 떼기");
        a->attachTo(P, 1);
        check(P->getChildren().size() == 3 && P->getChildren()[1] == a, "다른 부모에서 자리 지정해 붙임");
        P->attachTo(a, 0);
        check(!P->getParent(), "자손 아래로는 거부(사이클)");

        Actor* h = w.spawnActor("h");
        auto* t1 = h->addComponent<TestComp>();
        auto* o  = h->addComponent<OtherComp>();
        auto* t2 = h->addComponent<TestComp>();
        check(h->getComponent<TestComp>() == t1, "같은 타입이 둘이면 먼저 붙은 것을 찾음");
        check(h->removeComponent(t1) && h->getComponent<TestComp>() == t2, "먼저 붙은 것을 지우면 다음 것을 찾음");
        check(h->componentIndex(o) == 0 && h->componentIndex(t2) == 1, "붙은 순서 안의 자리");
        h->moveComponent(t2, 0);
        check(h->componentIndex(t2) == 0 && h->componentIndex(o) == 1, "자리를 옮김");
        check(!h->removeComponent(t1), "이미 지운 포인터는 false");
    }

    std::printf("\n== 2. 이름 · 켜기/끄기 · 부모 바꾸기 커맨드(월드 자세 유지) ==\n");
    {
        World w;
        HCommandStack st;
        Actor* P = w.spawnActor("P");
        P->setLocalPosition({ 10.f, 0.f, 0.f });
        P->setLocalRotation(glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f)));
        P->setLocalScale({ 2.f, 2.f, 2.f });
        Actor* Q = w.spawnActor("Q");
        Actor* x = w.spawnActor("x");
        Actor* y = w.spawnActor("y");
        x->attachTo(Q); y->attachTo(Q);
        x->setLocalPosition({ 1.f, 2.f, 3.f });
        x->setLocalRotation(glm::angleAxis(glm::radians(30.f), glm::vec3(1.f, 0.f, 0.f)));
        const glm::vec3 wp = x->worldPosition();
        const glm::quat wr = x->worldRotation();
        const glm::vec3 ws = x->worldScale();
        const Transform oldLocal = x->transform();

        st.push(std::make_unique<RenameActorCmd>(w, x, "x", "renamed"));
        x->name = "renamed";
        st.push(std::make_unique<SetActiveCmd>(w, x, true, false));
        x->setActive(false);

        auto rp = std::make_unique<ReparentCmd>(w, x->id, Q->id, x->siblingIndex(), oldLocal, P->id, -1, hxform::localUnder(*x, P));
        rp->redo();
        st.push(std::move(rp));
        check(x->getParent() == P, "새 부모 아래");
        check(near(x->worldPosition(), wp) && nearQ(x->worldRotation(), wr) && near(x->worldScale(), ws),
              "부모를 바꿔도 월드 위치 · 회전 · 크기가 그대로");
        st.undo();
        check(x->getParent() == Q && x->siblingIndex() == 0 && near(x->transform().position(), oldLocal.position()),
              "되돌리면 원래 부모 · 원래 형제 자리 · 원래 로컬");
        st.undo();
        check(x->activeSelf(), "켜기/끄기 되돌림");
        st.undo();
        check(x->name == "x", "이름 되돌림");
        st.redo(); st.redo(); st.redo();
        check(x->name == "renamed" && !x->activeSelf() && x->getParent() == P, "다시 하기");

        // 부모가 사라졌으면 부모 바꾸기를 하지 않습니다.
        st.undo();
        const uint64_t qid = Q->id;
        w.destroyActor(Q);
        check(!w.findById(qid), "(준비) 원래 부모를 지움");
        Actor* xx = w.findById(x->id);
        check(!xx, "(준비) 자식도 함께 지워짐 — 커맨드는 없는 액터를 건너뜀");
        st.redo();   // x 가 없으니 아무 일도 없음
        check(true, "없는 액터를 가리키는 커맨드도 실행해도 안전함");
    }

    std::printf("\n== 3. 컴포넌트 켜기/끄기 · 속성(숫자 · 토글 · 열거) ==\n");
    {
        World w;
        HCommandStack st;
        Actor* a = w.spawnActor("a");
        a->addComponent<OtherComp>();
        auto* t = a->addComponent<TestComp>();
        st.push(std::make_unique<ComponentEnabledCmd>(w, a, t, true, false));
        t->setEnabled(false);
        hcmd::PropValues before = hcmd::captureProps(*t);
        t->speed = 4.f; t->loud = true; t->mode = 2;
        hcmd::PropValues after = hcmd::captureProps(*t);
        check(before != after && before.count(PropDesc::Float) == 1 && before.count(PropDesc::Toggle) == 1 &&
                  before.count(PropDesc::Enum) == 1, "종류별로 값을 뜸");
        st.push(std::make_unique<ComponentPropsCmd>(w, a, t, before, after));
        st.undo();
        check(t->speed == 1.f && !t->loud && t->mode == 0 && t->committed == 1, "숫자 · 토글 · 열거를 되돌리고 확정 훅을 부름");
        st.undo();
        check(t->isEnabled(), "컴포넌트 켜기/끄기 되돌림");
        st.redo(); st.redo();
        check(!t->isEnabled() && t->speed == 4.f && t->loud && t->mode == 2, "다시 하기");

        // 같은 타입이 둘이면 순번으로 구별합니다(씬 파일에서는 둘이 붙을 수 있음).
        auto* t2 = a->addComponent<TestComp>();
        hcmd::PropValues b2 = hcmd::captureProps(*t2);
        t2->speed = 8.f;
        st.push(std::make_unique<ComponentPropsCmd>(w, a, t2, b2, hcmd::captureProps(*t2)));
        st.undo();
        check(t2->speed == 1.f && t->speed == 4.f, "두 번째 컴포넌트의 편집은 두 번째에만 되돌림");
    }

    std::printf("\n== 3-0. 여러 액터 동시 편집(같은 타입 · 순번 찾기, 칸 이름으로 넣기, 한 번의 Undo) ==\n");
    {
        World w;
        HCommandStack st;
        Actor* a = w.spawnActor("a");
        Actor* b = w.spawnActor("b");
        Actor* c = w.spawnActor("c");
        auto* ta = a->addComponent<TestComp>();
        b->addComponent<OtherComp>();
        auto* tb = b->addComponent<TestComp>();   // 순번 0(다른 타입이 앞에 있어도)
        c->addComponent<OtherComp>();             // TestComp 없음 → 건너뜀
        const std::vector<Actor*> sel{ a, b, c };
        const auto peers = hcmd::peerComponents(*a, *ta, sel);
        check(peers.size() == 1 && peers[0] == tb, "같은 타입 · 순번의 컴포넌트를 가진 액터만 고름");
        auto* ta2 = a->addComponent<TestComp>();  // a 의 두 번째 TestComp: b 에는 순번 1 이 없음
        check(hcmd::peerComponents(*a, *ta2, sel).empty(), "순번이 다르면 고르지 않음");

        hcmd::PropValue e; e.label = "Speed"; e.kind = PropDesc::Float; e.f = 3.f;
        check(hcmd::setProp(*tb, e) && tb->speed == 3.f && tb->committed == 0, "칸 이름 · 종류로 값을 넣고 확정 훅은 부르지 않음");
        e.kind = PropDesc::Toggle;
        check(!hcmd::setProp(*tb, e), "이름이 같아도 종류가 다르면 넣지 않음");
        e.label = "Mode"; e.kind = PropDesc::Enum; e.i = 2;
        check(hcmd::setProp(*tb, e) && tb->mode == 2, "열거 값");

        // 디테일 패널과 같은 순서: 두 컴포넌트의 이전 값 → 바꿈 → 한 묶음 커맨드
        tb->speed = 1.f; tb->mode = 0;
        const hcmd::PropValues ba = hcmd::captureProps(*ta), bb = hcmd::captureProps(*tb);
        ta->speed = 5.f; tb->speed = 5.f;
        auto group = std::make_unique<CompositeCmd>();
        group->add(std::make_unique<ComponentPropsCmd>(w, a, ta, ba, hcmd::captureProps(*ta)));
        group->add(std::make_unique<ComponentPropsCmd>(w, b, tb, bb, hcmd::captureProps(*tb)));
        st.push(std::move(group));
        st.undo();
        check(ta->speed == 1.f && tb->speed == 1.f, "한 번의 Undo 로 두 액터 모두 되돌림");
        st.redo();
        check(ta->speed == 5.f && tb->speed == 5.f, "다시 하기도 함께");
    }

    std::printf("\n== 3-1. 칸이 나타나고 사라지는 속성의 Undo · Redo (DEF-131) ==\n");
    {
        World w;
        HCommandStack st;
        Actor* a = w.spawnActor("a");
        auto* c = a->addComponent<CondComp>();
        c->kind = 1;   // Reach 칸이 보임
        c->reach = 12.f;

        // Use Warm 을 켜면 Warm 칸이 Reach 앞에 낍니다. 예전에는 Undo 가 Reach 값을 Warm 자리(숫자 순번 1)에 넣었습니다.
        hcmd::PropValues b0 = hcmd::captureProps(*c);
        c->useWarm = true;
        st.push(std::make_unique<ComponentPropsCmd>(w, a, c, b0, hcmd::captureProps(*c)));
        st.undo();
        check(!c->useWarm && c->reach == 12.f && c->warm == 6500.f, "토글을 되돌려도 숨은 칸 · 뒤 칸 값이 그대로");
        st.redo();
        check(c->useWarm && c->reach == 12.f && c->warm == 6500.f, "토글 다시 하기도 값이 제자리");

        // 토글이 켜진 동안 Warm 을 바꾼 편집, 이어서 토글을 끈 편집. 두 번 되돌리면 Warm 이 원래 값으로.
        hcmd::PropValues b1 = hcmd::captureProps(*c);
        c->warm = 3000.f;
        st.push(std::make_unique<ComponentPropsCmd>(w, a, c, b1, hcmd::captureProps(*c)));
        hcmd::PropValues b2 = hcmd::captureProps(*c);
        c->useWarm = false;
        st.push(std::make_unique<ComponentPropsCmd>(w, a, c, b2, hcmd::captureProps(*c)));
        st.undo();
        check(c->useWarm && c->warm == 3000.f && c->reach == 12.f, "토글 끄기를 되돌리면 켜진 칸 값이 그대로");
        st.undo();
        check(c->useWarm && c->warm == 6500.f && c->reach == 12.f, "숨었다 나타난 칸의 편집도 제 칸에서 되돌림");

        // 열거를 바꾸면 칸 묶음이 통째로 바뀝니다. 되돌리면 이전 열거의 칸(Reach)에, 다시 하면 새 칸(Cone · Lit)에.
        hcmd::PropValues b3 = hcmd::captureProps(*c);
        c->kind = 2;
        c->cone = 45.f;
        c->lit  = false;
        st.push(std::make_unique<ComponentPropsCmd>(w, a, c, b3, hcmd::captureProps(*c)));
        c->reach = 99.f;   // 숨은 칸을 바깥에서 바꿔도(이 시험만의 조작) 되돌리면 기억한 값으로
        c->cone  = 1.f; c->lit = true;
        st.undo();
        check(c->kind == 1 && c->reach == 12.f && c->base == 1.f, "열거를 되돌리면 이전 칸 값으로");
        st.redo();
        check(c->kind == 2 && c->cone == 45.f && !c->lit, "열거 다시 하기는 새로 나타난 칸 · 토글에도 값을 넣음");
    }

    std::printf("\n== 4. 컴포넌트 추가 · 제거(값 · 켜짐 · 순서 보존) ==\n");
    {
        World w;
        HCommandStack st;
        Actor* a = w.spawnActor("a");
        a->addComponent<OtherComp>();
        auto* t = a->addComponent<TestComp>();
        a->addComponent<OtherComp>();
        t->speed = 7.5f; t->loud = true; t->mode = 1;
        t->setEnabled(false);
        int changed = 0;
        auto rm = std::make_unique<ComponentPresenceCmd>(ComponentPresenceCmd::Kind::Remove, w, a, t, ComponentLoadContext{},
                                                         [&] { ++changed; });
        rm->redo();
        st.push(std::move(rm));
        check(!a->getComponent<TestComp>() && a->getComponents().size() == 2, "지움");
        st.undo();
        auto* back = a->getComponent<TestComp>();
        check(back && back->speed == 7.5f && back->loud && back->mode == 1 && !back->isEnabled(), "값과 꺼짐 상태로 되살림");
        check(back && a->componentIndex(back) == 1, "원래 자리(가운데)로 되살림");
        st.redo();
        check(!a->getComponent<TestComp>() && changed == 3, "다시 지움(바뀔 때마다 알림)");

        // 붙임 커맨드: 이미 붙인 뒤 쌓고, Undo 는 지우고 Redo 는 그때 값으로 되살림
        auto* n = a->addComponent<TestComp>();
        n->speed = 3.f;
        st.push(std::make_unique<ComponentPresenceCmd>(ComponentPresenceCmd::Kind::Add, w, a, n, ComponentLoadContext{}, nullptr));
        st.undo();
        check(!a->getComponent<TestComp>(), "붙임을 되돌리면 지움");
        st.redo();
        check(a->getComponent<TestComp>() && a->getComponent<TestComp>()->speed == 3.f, "다시 하면 값과 함께 되살림");

        // 속성 커맨드는 컴포넌트를 포인터가 아니라 타입 · 순번으로 찾으므로, 되살린 컴포넌트에도 이어집니다.
        auto* cur = a->getComponent<TestComp>();
        hcmd::PropValues b0 = hcmd::captureProps(*cur);
        cur->speed = 9.f;
        st.push(std::make_unique<ComponentPropsCmd>(w, a, cur, b0, hcmd::captureProps(*cur)));
        auto rm2 = std::make_unique<ComponentPresenceCmd>(ComponentPresenceCmd::Kind::Remove, w, a, cur, ComponentLoadContext{}, nullptr);
        rm2->redo();
        st.push(std::move(rm2));
        st.undo();   // 되살림(새 객체)
        st.undo();   // 속성 되돌림 → 새 객체에 적용
        check(a->getComponent<TestComp>() && a->getComponent<TestComp>()->speed == 3.f, "되살린 컴포넌트에도 앞선 속성 Undo 가 이어짐");
    }

    std::printf("\n== 5. 묶음 커맨드 ==\n");
    {
        World w;
        Actor* a = w.spawnActor("a");
        auto comp = std::make_unique<CompositeCmd>();
        Transform t0 = a->transform(), t1 = t0, t2 = t0;
        t1.setPosition({ 1.f, 0.f, 0.f });
        t2.setPosition({ 2.f, 0.f, 0.f });
        comp->add(std::make_unique<SetTransformCmd>(w, a, t0, t1));
        comp->add(std::make_unique<SetTransformCmd>(w, a, t1, t2));
        comp->redo();
        check(a->transform().position().x == 2.f, "순서대로 다시 하기");
        comp->undo();
        check(a->transform().position().x == 0.f, "거꾸로 되돌리기");
    }

    std::printf("\n== 6. 여러 액터 함께 움직이기(주 선택 피벗) ==\n");
    {
        hxform::WorldPose ps{ { 0.f, 0.f, 0.f }, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f } };
        hxform::WorldPose os{ { 2.f, 0.f, 0.f }, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f } };
        hxform::WorldPose pn = ps;
        pn.pos = { 0.f, 0.f, 5.f };
        check(near(hxform::followPivot(ps, pn, os).pos, { 2.f, 0.f, 5.f }), "이동은 같은 만큼");
        pn = ps;
        pn.rot = glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f));
        const hxform::WorldPose r = hxform::followPivot(ps, pn, os);
        check(near(r.pos, { 0.f, 0.f, -2.f }) && nearQ(r.rot, pn.rot), "회전은 주 선택 위치를 중심으로");
        pn = ps;
        pn.scale = { 2.f, 2.f, 2.f };
        const hxform::WorldPose sc = hxform::followPivot(ps, pn, os);
        check(near(sc.pos, { 4.f, 0.f, 0.f }) && near(sc.scale, { 2.f, 2.f, 2.f }), "크기는 피벗에서 거리와 크기를 함께 늘림");
    }

    std::printf("\n== 7. 클립보드 글 · 복사본 이름 · 화면 맞추기 거리 ==\n");
    {
        const std::string set = "ACTOR:Cube\nPOS:0 0 0\nEND_ACTOR\n";
        const std::string clip = hclip::wrap(set, { 0, 42 });
        std::string out;
        std::vector<uint64_t> parents;
        check(hclip::unwrap(clip, out, parents) && out == set && parents == std::vector<uint64_t>{ 0, 42 }, "감싸고 풀면 그대로");
        std::string crlf = clip;
        for (size_t i = 0; (i = crlf.find('\n', i)) != std::string::npos; i += 2) crlf.insert(i, "\r");
        check(hclip::unwrap(crlf, out, parents) && out == set, "CRLF 로 바뀌어도 읽음");
        check(!hclip::unwrap("hello world", out, parents), "다른 글은 거부");
        check(!hclip::unwrap(std::string(hclip::kHeader) + "\n#ROOT_PARENTS x\nACTOR:a\n", out, parents), "깨진 부모 목록은 거부");
        check(!hclip::unwrap(std::string(hclip::kHeader) + "\n#ROOT_PARENTS 0\n", out, parents), "액터가 없으면 거부");

        // 세션 표지(DEF-136): 부모 ID 는 복사한 실행 안에서만 뜻이 있습니다.
        std::string sess = "x";
        check(hclip::unwrap(clip, out, parents, &sess) && sess == hclip::sessionToken() && sess.size() == 16,
              "기본 클립에는 이 실행의 세션 표지가 들어감");
        check(hclip::unwrap(hclip::wrap(set, { 7 }, "0123456789abcdef"), out, parents, &sess) &&
                  sess == "0123456789abcdef" && sess != hclip::sessionToken() && parents == std::vector<uint64_t>{ 7 },
              "다른 실행의 표지를 그대로 돌려줌(붙여넣기는 루트에 둠)");
        check(hclip::unwrap(std::string(hclip::kHeader) + "\n#ROOT_PARENTS 5\n" + set, out, parents, &sess) && sess.empty() &&
                  parents == std::vector<uint64_t>{ 5 },
              "표지 줄이 없는 예전 클립도 읽되 표지는 비어 있음(다른 실행으로 봄)");
        check(!hclip::unwrap(std::string(hclip::kHeader) + "\n#SESSION abc", out, parents, &sess), "표지 줄에서 끝나면 거부");

        std::set<std::string> names{ "Cube", "Cube (1)", "Lamp (3)" };
        check(hclip::nextCopyName("Cube", names) == "Cube (2)", "이미 있는 번호는 건너뜀");
        check(hclip::nextCopyName("Cube (1)", names) == "Cube (2)", "번호가 붙은 이름은 번호를 바꿈");
        check(hclip::nextCopyName("Lamp (3)", names) == "Lamp (1)", "가장 작은 빈 번호");
        check(hclip::nextCopyName("Odd (x)", names) == "Odd (x) (1)", "숫자가 아닌 괄호는 이름의 일부");

        const float d = hframe::fitDistance(1.f, glm::radians(90.f), 1.f);
        check(std::abs(d - 1.f / std::sin(glm::radians(45.f))) < 1e-4f, "반지름 1, 시야 90° → 1/sin45°");
        check(hframe::fitDistance(1.f, glm::radians(90.f), 0.5f) > d, "세로로 긴 화면은 좁은 가로 시야에 맞춰 더 멀리");
    }

    std::printf("\n== 8. 프리팹 이름 · 머리 줄 · 실패한 블록 정리 (DEF-140 · 141 · 142) ==\n");
    {
        check(hprefab::isValidName("Cube") && hprefab::isValidName("Cube (1)") && hprefab::isValidName("Lamp_2-b.v2"),
              "보통 이름 · 복사본 이름 · 점이 낀 이름은 됨");
        std::string why;
        check(!hprefab::isValidName("a/b", &why) && !why.empty(), "폴더 구분자 '/' 거부(이유를 돌려줌)");
        check(!hprefab::isValidName("a\\b") && !hprefab::isValidName("..") && !hprefab::isValidName("."), "'\\' · '..' · '.' 거부");
        check(!hprefab::isValidName("") && !hprefab::isValidName("a:b") && !hprefab::isValidName("a?") && !hprefab::isValidName("a|b"),
              "빈 이름 · Windows 금지 글자 거부");
        check(!hprefab::isValidName("name.") && !hprefab::isValidName("name ") && !hprefab::isValidName(" name"),
              "끝의 점 · 앞뒤 공백 거부");
        check(!hprefab::isValidName("CON") && !hprefab::isValidName("nul.x") && !hprefab::isValidName("Com1") &&
                  hprefab::isValidName("CONSOLE"),
              "장치 이름은 거부, 장치 이름으로 시작하는 긴 이름은 됨");
        check(!hprefab::isValidName("\xED\x95\x9C") && !hprefab::isValidName(std::string(101, 'a')), "ASCII 밖 글자 · 100자 초과 거부");

        std::string t0 = "# Haruka Prefab v4\nACTOR:A\n";
        check(hprefab::stripBomAndDetectV3(t0) && t0 == "# Haruka Prefab v4\nACTOR:A\n", "머리 줄로 시작하면 v4");
        std::string t1 = "# Haruka Prefab v3\nACTOR:A\n";
        check(hprefab::stripBomAndDetectV3(t1) && t1 == "# Haruka Prefab v3\nACTOR:A\n", "옛 v3 머리 줄도 계속 받음");
        std::string t2 = "\xEF\xBB\xBF# Haruka Prefab v3\r\nACTOR:A\r\n";
        check(hprefab::stripBomAndDetectV3(t2) && t2.rfind("# Haruka", 0) == 0, "BOM 을 떼고 v3 로 판별");
        std::string t3 = "\n  \r\n# hand-written note\n# Haruka Prefab v3\nACTOR:A\n";
        check(hprefab::stripBomAndDetectV3(t3), "머리 줄 앞의 빈 줄 · 주석은 건너뜀");
        std::string t4 = "\xEF\xBB\xBFNAME:Old\nPOS:0 0 0\n";
        check(!hprefab::stripBomAndDetectV3(t4) && t4.rfind("NAME:", 0) == 0, "v1 · v2 는 v3 가 아님(BOM 은 떼어 옛 경로도 읽게)");
        std::string t5 = "NAME:x\n# Haruka Prefab v3\n";
        check(!hprefab::stripBomAndDetectV3(t5), "액터 줄 뒤의 머리 줄은 v3 로 보지 않음");

        // 실패한 블록: 루트 R(0) → 자식 C(1), 실패한 중간 블록(2, null) → 그 자식 G(3) · G 의 자식 H(4)
        World w;
        Actor* keep = w.spawnActor("Existing");
        Actor* R = w.spawnActor("R");
        Actor* C = w.spawnActor("C");
        C->attachTo(R);
        Actor* G = w.spawnActor("G");
        Actor* H = w.spawnActor("H");
        H->attachTo(G);
        const uint64_t idG = G->id, idH = H->id, idR = R->id, idC = C->id;
        check(hprefab::discardOrphans(w, { R, C, nullptr, G, H }) == 1, "루트 밖에 남은 고아 서브트리 하나를 지움");
        check(w.findById(idR) && w.findById(idC) && !w.findById(idG) && !w.findById(idH) && w.findById(keep->id),
              "루트 서브트리 · 원래 있던 액터는 남고 고아와 그 자식은 지워짐");
        Actor* A = w.spawnActor("A");
        Actor* B = w.spawnActor("B");
        B->attachTo(A);
        const uint64_t idA = A->id, idB = B->id;
        check(hprefab::discardOrphans(w, { nullptr, A, B }) == 1 && !w.findById(idA) && !w.findById(idB) && w.findById(keep->id),
              "루트가 실패하면 꺼낸 액터를 모두 지움");
        check(hprefab::discardOrphans(w, { w.findById(idR), w.findById(idC) }) == 0 && w.findById(idR), "실패가 없으면 아무것도 지우지 않음");
    }

    std::printf("\n== 9. 에디터 색 토큰 (EditorTheme sRGB -> 선형) ==\n");
    {
        static_assert(EditorTheme::bgPanel.x > 0.012f && EditorTheme::bgPanel.x < 0.014f,
                      "EditorTheme tokens must be constexpr");
        check(near4(EditorTheme::srgb(0x000000), { 0.f, 0.f, 0.f, 1.f }, 1e-6f), "sRGB #000000 -> 0");
        check(near4(EditorTheme::srgb(0xFFFFFF), { 1.f, 1.f, 1.f, 1.f }, 1e-5f), "sRGB #FFFFFF -> 1");
        check(near4(EditorTheme::srgb(0x010203), { (1.f / 255.f) / 12.92f, (2.f / 255.f) / 12.92f, (3.f / 255.f) / 12.92f, 1.f }, 1e-6f),
              "sRGB 선형 구간 (<= 0.04045)");

        check(near4(EditorTheme::bgWindow,      { 0.0075f, 0.0075f, 0.0080f, 1.f }), "bgWindow (#151516)");
        check(near4(EditorTheme::bgPanel,       { 0.0130f, 0.0130f, 0.0137f, 1.f }), "bgPanel (#1E1E1F)");
        check(near4(EditorTheme::bgHeader,      { 0.0194f, 0.0194f, 0.0212f, 1.f }), "bgHeader (#262628)");
        check(near4(EditorTheme::bgInput,       { 0.0056f, 0.0056f, 0.0060f, 1.f }), "bgInput (#111112)");
        check(near4(EditorTheme::ctlNormal,     { 0.0242f, 0.0242f, 0.0273f, 1.f }), "ctlNormal (#2B2B2E)");
        check(near4(EditorTheme::ctlHover,      { 0.0395f, 0.0395f, 0.0452f, 1.f }), "ctlHover (#38383C)");
        check(near4(EditorTheme::ctlPressed,    { 0.0595f, 0.0595f, 0.0685f, 1.f }), "ctlPressed (#45454A)");
        check(near4(EditorTheme::lineDivider,   { 0.0296f, 0.0296f, 0.0331f, 1.f }), "lineDivider (#303033)");
        check(near4(EditorTheme::textPrimary,   { 0.7605f, 0.7605f, 0.7758f, 1.f }), "textPrimary (#E2E2E4)");
        check(near4(EditorTheme::textSecondary, { 0.3663f, 0.3663f, 0.3916f, 1.f }), "textSecondary (#A3A3A8)");
        check(near4(EditorTheme::textDisabled,  { 0.1441f, 0.1441f, 0.1620f, 1.f }), "textDisabled (#6A6A70)");
        check(near4(EditorTheme::accent,        { 0.0262f, 0.1470f, 0.6724f, 1.f }), "accent (#2D6BD6)");
        check(near4(EditorTheme::accentHover,   { 0.0685f, 0.2307f, 0.8070f, 1.f }), "accentHover (#4A84E8)");
        check(near4(EditorTheme::selectRow,     { 0.0176f, 0.0612f, 0.1559f, 1.f }), "selectRow (#24466E)");
        check(near4(EditorTheme::ok,            { 0.0497f, 0.3712f, 0.1248f, 1.f }), "ok (#3FA463)");
        check(near4(EditorTheme::warn,          { 0.7454f, 0.4508f, 0.0685f, 1.f }), "warn (#E0B34A)");
        check(near4(EditorTheme::error,         { 0.7454f, 0.0844f, 0.1022f, 1.f }), "error (#E0525A)");
        check(near4(EditorTheme::axisX,         { 0.7454f, 0.0844f, 0.1022f, 1.f }), "axisX (#E0525A)");
        check(near4(EditorTheme::axisY,         { 0.1500f, 0.5210f, 0.1022f, 1.f }), "axisY (#6CBF5A)");
        check(near4(EditorTheme::axisZ,         { 0.0742f, 0.2664f, 0.8714f, 1.f }), "axisZ (#4D8DF0)");
    }

    testCommandRegistry();
    testShortcutTable();
    testShortcutAndMenuSameCommand();
    testMenuModel();
    testPropDefaults();
    testAssetField();
    testColorMath();
    testColorPicker();

    std::printf("\n== B1. 콘솔에서 에디터 명령 실행 (cmd / cmd.list) ==\n");
    {
        EditorCommandRegistry reg;
        heditorcmd::addDefaultSpecs(reg);
        int frameRan = 0;
        bool canUndo = false;
        int undoRan = 0;
        reg.bind("View.FrameSelected", [&] { ++frameRan; });
        reg.bind("Edit.Undo", [&] { ++undoRan; }, [&] { return canUndo; });

        // 1. 있는 id 실행 (needsNoTextFocus = true 여도 콘솔에서는 실행됨)
        const auto r1 = heditorcmd::runConsoleCmd(reg, { "View.FrameSelected" });
        check(r1.executed && !r1.warning && frameRan == 1 && r1.text.find("View.FrameSelected") != std::string::npos,
              "cmd: 있는 id 실행 (needsNoTextFocus 무관)");

        // 2. canExecute 거짓이면 실행하지 않고 불가 안내
        const auto r2 = heditorcmd::runConsoleCmd(reg, { "Edit.Undo" });
        check(!r2.executed && !r2.warning && undoRan == 0 && r2.text.find("cannot execute") != std::string::npos,
              "cmd: canExecute 거짓이면 실행 안 함");

        canUndo = true;
        const auto r2b = heditorcmd::runConsoleCmd(reg, { "Edit.Undo" });
        check(r2b.executed && undoRan == 1, "cmd: canExecute 참으로 바뀌면 실행됨");

        // 3. 인자 오류
        const auto rEmpty = heditorcmd::runConsoleCmd(reg, {});
        check(!rEmpty.executed && rEmpty.text == "usage: cmd <id>", "cmd: 인자 없음 -> usage");

        // 4. 없는 id -> 경고 + 비슷한 id 최대 5개(앞부분 일치 우선, 대소문자 무시)
        const auto simPrefix = heditorcmd::suggestIds(reg, "view.", 5);
        check(simPrefix.size() == 2 && simPrefix[0] == "View.FrameSelected" && simPrefix[1] == "View.GizmoSize",
              "suggestIds: 대소문자 무시 앞부분 일치");

        const auto simCap = heditorcmd::suggestIds(reg, "e", 5);
        check(simCap.size() == 5 && simCap[0].rfind("Edit.", 0) == 0,
              "suggestIds: 최대 5개 제한 · 앞부분 일치 우선");

        const auto rMiss = heditorcmd::runConsoleCmd(reg, { "View.Frame" });
        check(!rMiss.executed && rMiss.warning &&
              rMiss.text.find("unknown editor command 'View.Frame'") != std::string::npos &&
              rMiss.text.find("View.FrameSelected") != std::string::npos,
              "cmd: 없는 id 면 경고 + 추천 목록");

        const auto rNone = heditorcmd::runConsoleCmd(reg, { "zzzz_nope" });
        check(!rNone.executed && rNone.warning && rNone.text.find("did you mean") == std::string::npos,
              "cmd: 비슷한 id 가 없으면 추천 생략");

        // 5. cmd.list 정렬 · 한 줄 구성(id · 라벨 · 단축키 · 실행 가능) · 거르기
        const std::string allList = heditorcmd::formatCommandList(reg);
        const size_t posActor = allList.find("Actor.AddEmpty");
        const size_t posView  = allList.find("View.FrameSelected");
        check(posActor != std::string::npos && posView != std::string::npos && posActor < posView,
              "cmd.list: id 사전순 정렬");

        const std::string viewList = heditorcmd::formatCommandList(reg, "view.");
        check(viewList.find("View.FrameSelected") != std::string::npos &&
              viewList.find("Frame Selected") != std::string::npos &&
              viewList.find("[F]") != std::string::npos &&
              viewList.find("(enabled)") != std::string::npos &&
              viewList.find("View.GizmoSize") != std::string::npos &&
              viewList.find("[-]") != std::string::npos &&
              viewList.find("(disabled)") != std::string::npos &&
              viewList.find("File.SaveScene") == std::string::npos,
              "cmd.list: id 필터 · 한 줄 형식(id · 라벨 · 단축키 · 가능 여부)");

        const std::string labelList = heditorcmd::formatCommandList(reg, "s/m/l");
        check(labelList.find("View.GizmoSize") != std::string::npos &&
              labelList.find("View.FrameSelected") == std::string::npos,
              "cmd.list: 라벨 대소문자 무시 거르기");

        const std::string emptyList = heditorcmd::formatCommandList(reg, "no_such_filter_xyz");
        check(emptyList == "no editor commands matching 'no_such_filter_xyz'",
              "cmd.list: 거른 결과 없음 안내");
    }

    // ── C1: 메뉴 아이콘 일관성 · 가속 글자 · 드롭다운 글자 키 · Alt 메뉴 바 상태 기계 ──
    {
        std::printf("\n[C1] 메뉴 아이콘 일관성\n");
        const auto& iconNames = EditorIcons::names();
        auto hasIconName = [&](const std::string& name) {
            for (const char* n : iconNames)
                if (name == n) return true;
            return false;
        };
        const auto specs = heditorcmd::defaultSpecs();
        bool allHaveIcon = true, allInList = true;
        std::string missingInfo;
        for (const EditorCommand& c : specs) {
            if (c.icon.empty()) { allHaveIcon = false; missingInfo += c.id + "(empty) "; }
            else if (!hasIconName(c.icon)) { allInList = false; missingInfo += c.id + "(" + c.icon + ") "; }
        }
        check(allHaveIcon, "defaultSpecs 모든 명령에 아이콘 지정됨 " + missingInfo);
        check(allInList, "defaultSpecs 모든 아이콘이 EditorIcons::names() 에 있음 " + missingInfo);

        hvfs::reset();
        hvfs::mountDirectoryAs(HARUKA_ENGINE_CONTENT_DIR, "/Engine/", 10);
        EditorIcons icons;
        check(icons.load() && icons.count() == iconNames.size(),
              "EditorIcons 모든 SVG 파일이 존재하고 nanosvg 로 읽힘");
        bool allLoaded = true;
        for (const EditorCommand& c : specs)
            if (!icons.has(c.icon)) allLoaded = false;
        check(allLoaded, "모든 명령 아이콘이 실제로 로드됨");
    }

    {
        std::printf("\n[C1] 메뉴 바 가속 글자 · 드롭다운 글자 키\n");
        const auto acc = hmenu::assignBarAccelerators({ "File", "Edit", "Actor", "View", "Window" });
        check(acc.size() == 5 && acc[0] == 'F' && acc[1] == 'E' && acc[2] == 'A' && acc[3] == 'V' && acc[4] == 'W',
              "메뉴 바 기본 가속 글자 = F E A V W");
        const auto col = hmenu::assignBarAccelerators({ "File", "Format", "Filter" });
        check(col.size() == 3 && col[0] == 'F' && col[1] == 'O' && col[2] == 'I',
              "겹치는 메뉴 이름 가속 글자 = 다음 글자(F O I)");
        check(hmenu::findAcceleratorIndex("File", 'F') == 0 &&
              hmenu::findAcceleratorIndex("Format", 'O') == 1 &&
              hmenu::findAcceleratorIndex("Edit", 'X') == -1,
              "findAcceleratorIndex: 밑줄 위치 계산");

        std::vector<EditorMenuRow> rows;
        rows.push_back({ hmenu::Kind::Heading, "Section", "Section", "", "", false, false, false, 0 });
        rows.push_back({ hmenu::Kind::Command, "Save", "Save", "", "", true, false, false, 1 });
        rows.push_back({ hmenu::Kind::Command, "Save As...", "Save As...", "", "", true, false, false, 2 });
        rows.push_back({ hmenu::Kind::Command, "Select Disabled", "Select Disabled", "", "", false, false, false, 3 });
        rows.push_back({ hmenu::Kind::Command, "실행 취소", "Undo", "", "", true, false, false, 4 });

        bool uniq = false;
        const int r1 = hmenu::findMnemonicRow(rows, -1, 'S', uniq);
        check(r1 == 1 && !uniq, "드롭다운 글자 키 S: 첫 켜진 S 항목(Save), 여럿이라 즉시 실행 안 함");
        const int r2 = hmenu::findMnemonicRow(rows, r1, 'S', uniq);
        check(r2 == 2 && !uniq, "드롭다운 글자 키 S 두 번째: 다음 S 항목(Save As...), 꺼진 항목 건너뜀");
        const int r3 = hmenu::findMnemonicRow(rows, r2, 'S', uniq);
        check(r3 == 1 && !uniq, "드롭다운 글자 키 S 세 번째: 처음으로 돎");
        const int ru = hmenu::findMnemonicRow(rows, -1, 'U', uniq);
        check(ru == 4 && uniq, "한국어 라벨도 영어 원문 첫 글자(Undo)로 맞춤 · 하나뿐이면 uniqueMatch");
        check(hmenu::findMnemonicRow(rows, -1, 'Z', uniq) == -1 && !uniq, "없는 글자 키는 -1");
    }

    {
        std::printf("\n[C1] Alt 메뉴 바 상태 기계\n");
        const std::vector<char> acc = { 'F', 'E', 'A', 'V', 'W' };
        hmenu::BarState st;

        // 1. Alt 탭 -> 메뉴 바 모드
        check(hmenu::stepBar(st, { hmenu::BarEventKind::AltPress }, acc) == hmenu::BarAct::BarChanged && st.showMnemonics(),
              "Alt 누름: 가속 글자 표시 켜짐");
        check(hmenu::stepBar(st, { hmenu::BarEventKind::AltRelease }, acc) == hmenu::BarAct::BarChanged &&
              st.barActive && st.focusedMenu == 0 && st.openMenu == -1,
              "Alt 뗌(탭): 메뉴 바 모드 진입 · File 강조 · 드롭다운 미열림");

        // 2. 모드에서 Esc -> 해제
        check(hmenu::stepBar(st, { hmenu::BarEventKind::NavEscape }, acc) == hmenu::BarAct::BarChanged &&
              !st.barActive && st.focusedMenu == -1 && !st.showMnemonics(),
              "모드에서 Esc: 해제");

        // 3. Alt+F4 -> 모드 아님
        hmenu::stepBar(st, { hmenu::BarEventKind::AltPress }, acc);
        hmenu::stepBar(st, { hmenu::BarEventKind::OtherKey }, acc);
        check(hmenu::stepBar(st, { hmenu::BarEventKind::AltRelease }, acc) == hmenu::BarAct::BarChanged &&
              !st.barActive && !st.showMnemonics(),
              "Alt+F4(OtherKey): 떼어도 메뉴 모드 아님");

        // 4. Alt+끌기(MouseOrWheel) -> 모드 아님
        hmenu::stepBar(st, { hmenu::BarEventKind::AltPress }, acc);
        hmenu::stepBar(st, { hmenu::BarEventKind::MouseOrWheel }, acc);
        check(hmenu::stepBar(st, { hmenu::BarEventKind::AltRelease }, acc) == hmenu::BarAct::BarChanged &&
              !st.barActive,
              "Alt+끌기(MouseOrWheel): 떼어도 메뉴 모드 아님");

        // 5. 모드에서 -> -> Enter
        hmenu::stepBar(st, { hmenu::BarEventKind::AltPress }, acc);
        hmenu::stepBar(st, { hmenu::BarEventKind::AltRelease }, acc);
        hmenu::stepBar(st, { hmenu::BarEventKind::NavRight }, acc);
        check(st.focusedMenu == 1, "모드에서 ->: Edit(1) 강조");
        check(hmenu::stepBar(st, { hmenu::BarEventKind::NavEnter }, acc) == hmenu::BarAct::OpenMenu &&
              st.openMenu == 1 && !st.barActive,
              "모드에서 Enter: Edit 드롭다운 열림");

        // 6. Alt+E -> Edit 바로 열기
        st = hmenu::BarState{};
        hmenu::stepBar(st, { hmenu::BarEventKind::AltPress }, acc);
        check(hmenu::stepBar(st, { hmenu::BarEventKind::AccelKey, 'E' }, acc) == hmenu::BarAct::OpenMenu &&
              st.openMenu == 1 && !st.barActive,
              "Alt+E: Edit 드롭다운 바로 열림");
        hmenu::stepBar(st, { hmenu::BarEventKind::AltRelease }, acc);
        check(st.openMenu == 1 && !st.barActive && !st.showMnemonics(),
              "Alt+E 뒤 Alt 뗌: 드롭다운 유지 · 가속 글자 숨김");

        // 7. 모달 · 떠 있는 창 키보드 중(Blocked) 무시
        st = hmenu::BarState{};
        hmenu::stepBar(st, { hmenu::BarEventKind::AltPress }, acc);
        check(hmenu::stepBar(st, { hmenu::BarEventKind::Blocked }, acc) == hmenu::BarAct::BarChanged &&
              !st.altDown && !st.barActive,
              "Blocked(모달 · 떠 있는 창): Alt 상태 초기화");
        check(hmenu::stepBar(st, { hmenu::BarEventKind::AltRelease }, acc) == hmenu::BarAct::None && !st.barActive,
              "Blocked 뒤 AltRelease: 메뉴 모드 진입 안 함");

        // 8. 오른쪽 Alt 탭 -> 메뉴 바 모드 진입 안 함 (한/영 키 오작동 방지)
        st = hmenu::BarState{};
        hmenu::stepBar(st, { hmenu::BarEventKind::AltPress, '\0', false }, acc);
        check(hmenu::stepBar(st, { hmenu::BarEventKind::AltRelease }, acc) == hmenu::BarAct::BarChanged &&
              !st.barActive && !st.showMnemonics(),
              "오른쪽 Alt 탭: 메뉴 바 모드 아님");

        // 9. Alt+화살표/편집키(OtherKey) 뒤 Alt 떼기 -> 메뉴 바 모드 아님
        st = hmenu::BarState{};
        hmenu::stepBar(st, { hmenu::BarEventKind::AltPress, '\0', true }, acc);
        hmenu::stepBar(st, { hmenu::BarEventKind::OtherKey }, acc);
        check(hmenu::stepBar(st, { hmenu::BarEventKind::AltRelease }, acc) == hmenu::BarAct::BarChanged &&
              !st.barActive && !st.showMnemonics(),
              "Alt+화살표 등(OtherKey) 뒤 Alt 뗌: 메뉴 바 모드 아님");
    }

    // ── C2: Output Log 표시 옵션(시각 · 묶기 · 줄 바꿈 · 설정 파일) ──
    {
        std::printf("\n[C2] Output Log 시각 포맷\n");
        check(hlogview::formatLogTime(ELogTimeMode::None, 12.34f, 0, false).empty(),
              "시각 없음: 빈 문자열");
        check(hlogview::formatLogTime(ELogTimeMode::Uptime, 12.345f, 0, false) == "12.35",
              "시각 시작 뒤 초: 소수 둘째 자리");
        check(hlogview::formatLogTime(ELogTimeMode::Uptime, 12.345f, 0, true) == "#.##",
              "시각 덤프 모드(uptime): #.## 고정");
        check(hlogview::formatLogTime(ELogTimeMode::Clock, 12.345f, 0, true) == "##:##:##.###",
              "시각 덤프 모드(clock): ##:##:##.### 고정");
        const std::string clk = hlogview::formatLogTime(ELogTimeMode::Clock, 1.25f, 1700000000000LL, false);
        check(clk.size() == 12 && clk[2] == ':' && clk[5] == ':' && clk[8] == '.' && clk.substr(9) == "250",
              "시각 지역 시각: HH:MM:SS.mmm 형식");
    }

    {
        std::printf("\n[C2] Output Log 같은 메시지 묶기(Collapse)\n");
        hlogview::CollapsedLogList cl;
        cl.push({ 1, 1.0f, 0, ELogVerbosity::Log, "hello" });
        cl.push({ 2, 2.0f, 0, ELogVerbosity::Log, "world" });
        cl.push({ 3, 3.0f, 0, ELogVerbosity::Log, "hello" });
        cl.push({ 4, 4.0f, 1, ELogVerbosity::Log, "hello" });       // 다른 분류
        cl.push({ 5, 5.0f, 0, ELogVerbosity::Warning, "hello" });   // 다른 심각도
        check(cl.size() == 4, "묶기: 같은 것만 묶이고 다른 분류 · 심각도는 분리");
        check(cl.entries()[0].count == 2 && cl.entries()[0].firstSeq == 1 && cl.entries()[0].lastSeq == 3 &&
              std::abs(cl.entries()[0].lastTime - 3.0f) < 1e-5f,
              "묶기: 처음 나온 자리에 두고 count=2 · 시각은 마지막 것");
        check(cl.firstAfter(1) == 1 && cl.firstAfter(4) == 3,
              "묶기: firstAfter(firstSeq) 이분 탐색");
        cl.evict({ 1, 1.0f, 0, ELogVerbosity::Log, "hello" });
        check(cl.size() == 4 && cl.entries()[0].count == 1, "묶기 evict: count 2 -> 1");
        cl.evict({ 2, 2.0f, 0, ELogVerbosity::Log, "world" });
        check(cl.size() == 3 && cl.entries()[1].category == 1, "묶기 evict: count 1 -> 제거");
    }

    {
        std::printf("\n[C2] Output Log 줄 나눔(wrapLogText)\n");
        auto monoWidth = [](std::string_view sv) -> float {
            float w = 0.f;
            for (size_t i = 0; i < sv.size();) {
                const uint32_t cp = utf8::next(sv, i);
                w += (cp >= 0x80) ? 12.f : 6.f;
            }
            return w;
        };
        const auto wEmpty = hlogview::wrapLogText("", 100.f, monoWidth);
        check(wEmpty.size() == 1 && wEmpty[0].empty(), "줄 나눔: 빈 글은 빈 1줄");

        const auto wZero = hlogview::wrapLogText("abc\ndef", 0.f, monoWidth);
        check(wZero.size() == 2 && wZero[0] == "abc" && wZero[1] == "def",
              "줄 나눔: 폭 0은 개행만 나눔");

        const auto wLong = hlogview::wrapLogText("ABCDEFGHIJ", 24.f, monoWidth);
        check(wLong.size() == 3 && wLong[0] == "ABCD" && wLong[1] == "EFGH" && wLong[2] == "IJ",
              "줄 나눔: 공백 없는 긴 낱말 24px(4자)씩 나눔");

        const auto wKo = hlogview::wrapLogText("가나다라마바사", 36.f, monoWidth);
        check(wKo.size() == 3 && wKo[0] == "가나다" && wKo[1] == "라마바" && wKo[2] == "사",
              "줄 나눔: 한국어 UTF-8 경계 깨지지 않고 3자씩 나눔");

        const auto wNl = hlogview::wrapLogText("first line\r\nsecond\nthird", 200.f, monoWidth);
        check(wNl.size() == 3 && wNl[0] == "first line" && wNl[1] == "second" && wNl[2] == "third",
              "줄 나눔: \\r\\n · \\n 문단 나눔");

        bool hadNl = false;
        const std::string one = hlogview::firstLineForSingleRow("first\nsecond", hadNl);
        check(hadNl && one == "first..", "줄 바꿈 끔: 개행이 있으면 첫 줄 + '..'");

        // 줄 바꿈 캐시 상한 정리(C2 보완)
        std::unordered_map<uint64_t, std::vector<std::string>> cache;
        const size_t cap = HLog::kCapacity;
        for (uint64_t s = 1; s <= cap * 3; ++s) {
            cache.emplace(s, std::vector<std::string>{ "line" });
            if (s > cap) {
                const uint64_t minKeep = s - cap + 1;
                hlogview::pruneWrapCache(cache, minKeep, cap * 2, [](uint64_t k) { return k == 42; });
            }
        }
        check(cache.size() <= cap * 2 && cache.count(42) == 1 && cache.count(1) == 0 && cache.count(cap * 3) == 1,
              "줄 바꿈 캐시 정리: 링 크기 초과 입력 뒤 상한 안 · 대표 seq 보존 · 오래된 seq 제거");
    }

    {
        std::printf("\n[C2] Output Log 설정 파일 읽기 · 쓰기\n");
        OutputLogOptions o;
        bool sv[5] = { false, true, true, true, true };
        std::set<std::string> hidden;
        std::istringstream v1in(
            "# Haruka Output Log display filter v1. Search text is not saved.\n"
            "severity Warning off\n"
            "hide Input\n"
            "unknown_future_key foo bar\n");
        hlogview::parseLogOptions(v1in, o, sv, hidden);
        check(!sv[(int)ELogVerbosity::Warning] && sv[(int)ELogVerbosity::Error] &&
              hidden.count("Input") == 1 && o.timeMode == ELogTimeMode::Uptime && !o.collapse,
              "설정 v1 하위 호환 읽기 + 모르는 줄 무시");

        o.timeMode = ELogTimeMode::Clock;
        o.collapse = true;
        o.wordWrap = true;
        o.clearOnPlay = true;
        std::ostringstream v2out;
        hlogview::formatLogOptions(v2out, o, sv, hidden);

        OutputLogOptions o2;
        bool sv2[5] = { false, true, true, true, true };
        std::set<std::string> hidden2;
        std::istringstream v2in(v2out.str());
        hlogview::parseLogOptions(v2in, o2, sv2, hidden2);
        check(o2.timeMode == ELogTimeMode::Clock && o2.collapse && o2.wordWrap && o2.clearOnPlay &&
              !sv2[(int)ELogVerbosity::Warning] && hidden2.count("Input") == 1,
              "설정 v2 왕복 저장 · 복원");
    }

    {
        std::printf("\n[C3] 콘솔 기록 파일 읽기 · 쓰기 · Console.Focus 명령\n");
        {
            std::istringstream emptyIn("");
            check(hconsole::parseHistory(emptyIn).empty(), "빈 기록 파일");
        }
        {
            std::istringstream crlfIn("# Haruka Console History v1\r\n\r\n  \r\nhelp\r\nhelp\r\nlist time.\r\n\r\nhelp\r\n");
            auto h = hconsole::parseHistory(crlfIn);
            check(h == std::vector<std::string>{ "help", "list time.", "help" },
                  "CRLF · 빈 줄 · 연속 중복 제거 · 비연속 유지");
        }
        {
            std::vector<std::string> h;
            for (int i = 1; i <= 80; ++i) hconsole::pushHistory(h, "cmd " + std::to_string(i));
            check(h.size() == hconsole::kMaxHistory && h.front() == "cmd 17" && h.back() == "cmd 80",
                  "64줄 상한(최근 64줄 유지)");
            std::ostringstream out;
            hconsole::formatHistory(out, h);
            check(out.str().rfind(hconsole::kHistoryHeader, 0) == 0, "머리말 주석 한 줄");
            std::istringstream in(out.str());
            auto h2 = hconsole::parseHistory(in);
            check(h2 == h, "기록 파일 왕복 저장 · 복원");
        }
        {
            EditorCommandRegistry reg;
            heditorcmd::addDefaultSpecs(reg);
            const EditorCommand* cf = reg.find("Console.Focus");
            check(cf && cf->icon == "terminal" && reg.shortcutText("Console.Focus") == "`",
                  "Console.Focus 명령(아이콘 terminal, 단축키 `)");
        }
        {
            auto l1 = hconsole::computeCompletionLayout({ 80.f, 120.f }, { 30.f, 0.f }, 620.f, 12.f);
            check(l1.nameW == 120.f && l1.valW == 30.f && l1.gap == 12.f && l1.helpW == 620.f - 120.f - 30.f - 24.f,
                  "후보 열 폭: 가장 긴 이름 · 값에 맞추고 간격 12px 적용");
            auto l2 = hconsole::computeCompletionLayout({ 400.f }, { 200.f }, 500.f, 12.f);
            check(l2.nameW == 500.f * 0.45f && l2.valW == 500.f * 0.20f && l2.helpW == 500.f - l2.nameW - l2.valW - 24.f,
                  "후보 열 폭: 이름 45% · 값 20% 상한 및 도움말 남은 폭");
            auto l3 = hconsole::computeCompletionLayout({ 100.f }, { 0.f }, 500.f, 12.f);
            check(l3.valW == 0.f && l3.helpW == 500.f - 100.f - 12.f,
                  "후보 열 폭: 값이 없으면 값 열 생략 · 간격 1회");
        }
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
