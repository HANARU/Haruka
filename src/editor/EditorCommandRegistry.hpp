#pragma once
// ── 에디터 명령 레지스트리 (H2b, 2026-11-22, Docs/Editor_MultiWindow_Design.md 3절 H2) ──────────────────────────────
//
// UE 의 FUICommandInfo(명령 정의: 이름 · 글 · 아이콘 · 단축키) + FUICommandList(실행 · 실행 가능 · 체크 상태)를 줄인 것입니다.
// 메뉴 항목 · 툴바 단추 · 단축키가 같은 명령 id 를 부르므로, 메뉴에 보이는 단축키 글도 명령의 chord 에서 만듭니다.
//
// 규칙
//   - 레지스트리는 EditorApp 이 소유합니다(전역 싱글턴 아님). 명령 정의(id · 글 · 아이콘 · 단축키 · 단축키 조건)는 아래
//     heditorcmd::defaultSpecs() 하나에 있고, EditorApp 이 동작(execute · canExecute · isChecked)을 id 로 붙입니다. 시험은 같은 표로
//     단축키 결정이 예전(H2a 까지의 tickShortcuts)과 같은지 모든 입력 조합으로 확인합니다(tests/EditorCommandTest.cpp).
//   - execute 는 canExecute 가 거짓이면 아무것도 하지 않습니다. canExecute 는 "그 동작이 아무 일도 안 하는 경우"와 같게 둡니다
//     (단축키 동작이 예전과 같도록). Play 중 막힘 · 경고 로그처럼 알림이 있는 경우는 참으로 둡니다.
//   - 단축키 결정(resolveShortcut): 명령을 등록한 순서 · chord 순서가 우선순위입니다. 첫 번째로 맞는 것 하나만 실행합니다.
//       대화상자가 떠 있거나 카메라 비행 중이면 아무것도 안 함 → 키 엣지 · Ctrl · Shift 가 맞는 chord 를 차례로 보고 →
//       Ctrl chord 는 기즈모 · 숫자 칸을 끄는 동안 실행하지 않음(dragNotice 명령이면 알림만) → needsNoTextFocus 명령은 글 칸
//       포커스 중 건너뜀 → 처음 남은 것을 실행.
//   - polled = false 인 chord 는 표시 전용입니다(Delete · Ctrl+C · Ctrl+V 는 글 칸 처리기 TextInputController 가 포커스 없음일 때
//     같은 명령 id 를 부름. 처리 자리를 옮기지 않은 것은 글 칸 편집 키와 같은 엣지를 쓰기 때문).
//
// editor 모듈 파일이지만 창 · 렌더러를 모릅니다(키 값은 GLFW 와 같은 HInput 상수, platform/IWindow.hpp 의 값을 그대로 씀).

#include "../core/HText.hpp"
#include "../platform/InputKeys.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct EditorChord {
    enum class Shift : uint8_t { Off, On, Any };
    int   key   = 0;       // HInput::KEY_* (0 = 없음)
    bool  ctrl  = false;
    Shift shift = Shift::Off;
    bool  polled = true;   // tickShortcuts 가 이 chord 를 처리하는가(거짓 = 표시 전용)

    bool valid() const { return key != 0; }
    bool matches(int k, bool ctrlDown, bool shiftDown) const {
        if (k != key || ctrl != ctrlDown) return false;
        if (shift == Shift::Any) return true;
        return (shift == Shift::On) == shiftDown;
    }
};

// 메뉴 오른쪽 열의 단축키 글(번역하지 않음, 예전 글과 같은 꼴: "Ctrl+Shift+S" · "Del" · "F").
inline std::string chordText(const EditorChord& c) {
    if (!c.valid()) return {};
    std::string s;
    if (c.ctrl) s += "Ctrl+";
    if (c.shift == EditorChord::Shift::On) s += "Shift+";
    const int k = c.key;
    if (k >= 'A' && k <= 'Z')              s += (char)k;
    else if (k >= '0' && k <= '9')         s += (char)k;
    else if (k == HInput::KEY_DELETE)      s += "Del";
    else if (k == HInput::KEY_F2)          s += "F2";
    else if (k == HInput::KEY_ENTER)       s += "Enter";
    else if (k == HInput::KEY_TAB)         s += "Tab";
    else if (k == HInput::KEY_GRAVE_ACCENT) s += "`";
    else if (k == HInput::KEY_ESCAPE)      s += "Esc";
    else if (k == HInput::KEY_BACKSPACE)   s += "Backspace";
    else                                   s += "?";
    return s;
}

struct EditorCommand {
    std::string              id;
    HText                    label;
    std::string              icon;             // EditorIcons 이름(없으면 빈 글)
    std::vector<EditorChord> chords;           // 첫 번째 = 메뉴에 보이는 단축키
    bool                     needsNoTextFocus = false;   // 글 칸 포커스 중에는 단축키로 부르지 않음
    bool                     dragNotice       = false;   // 끄는 중 단축키를 누르면 "끌기를 먼저 끝내세요" 알림
    std::function<void()>    execute;
    std::function<bool()>    canExecute;       // 비면 참
    std::function<bool()>    isChecked;        // 비면 체크 없는 명령
};

// 단축키 결정의 입력(한 프레임). keyEdge 는 그 키가 이번 프레임에 새로 눌렸는가.
struct EditorShortcutInput {
    std::function<bool(int)> keyEdge;
    bool ctrl       = false;
    bool shift      = false;
    bool modal      = false;   // 대화상자가 떠 있음
    bool cameraMode = false;   // 메인 창 RMB 비행 중(WASD)
    bool textFocus  = false;   // 글 칸 · 숫자 칸 · 대화상자 포커스(currentFocus() != None)
    bool editDrag   = false;   // 기즈모 · 숫자 칸을 끄는 중
};
struct EditorShortcutResult {
    const EditorCommand* command    = nullptr;   // 실행할 명령(없으면 null)
    bool                 dragNotice = false;     // 끄는 중이라 막은 단축키가 있음(알림)
};

class EditorCommandRegistry {
public:
    // 같은 id 가 있으면 그 자리에서 바꿉니다(우선순위 = 처음 등록한 자리).
    EditorCommand& add(EditorCommand c) {
        for (EditorCommand& e : cmds_)
            if (e.id == c.id) { e = std::move(c); return e; }
        cmds_.push_back(std::move(c));
        return cmds_.back();
    }
    // 정의된 명령에 동작을 붙입니다. 없는 id 면 거짓.
    bool bind(std::string_view id, std::function<void()> execute, std::function<bool()> canExecute = {},
              std::function<bool()> isChecked = {}) {
        EditorCommand* c = findMut(id);
        if (!c) return false;
        c->execute    = std::move(execute);
        c->canExecute = std::move(canExecute);
        c->isChecked  = std::move(isChecked);
        return true;
    }
    const EditorCommand* find(std::string_view id) const {
        for (const EditorCommand& e : cmds_)
            if (e.id == id) return &e;
        return nullptr;
    }
    bool has(std::string_view id) const { return find(id) != nullptr; }
    bool canExecute(std::string_view id) const {
        const EditorCommand* c = find(id);
        return c && c->execute && (!c->canExecute || c->canExecute());
    }
    bool hasCheck(std::string_view id) const {
        const EditorCommand* c = find(id);
        return c && (bool)c->isChecked;
    }
    bool isChecked(std::string_view id) const {
        const EditorCommand* c = find(id);
        return c && c->isChecked && c->isChecked();
    }
    // canExecute 면 실행하고 참. 동작이 명령을 다시 등록해도 안전하도록 함수를 복사해 부릅니다.
    bool execute(std::string_view id) {
        if (!canExecute(id)) return false;
        std::function<void()> fn = find(id)->execute;
        fn();
        ++executed_;
        return true;
    }
    std::string label(std::string_view id) const {
        const EditorCommand* c = find(id);
        return c ? c->label.toString() : std::string{};
    }
    std::string shortcutText(std::string_view id) const {
        const EditorCommand* c = find(id);
        return (c && !c->chords.empty()) ? chordText(c->chords.front()) : std::string{};
    }
    // tickShortcuts 가 엣지를 셀 키(polled chord 의 키, 처음 나온 순서, 중복 없음).
    std::vector<int> polledKeys() const {
        std::vector<int> keys;
        for (const EditorCommand& c : cmds_)
            for (const EditorChord& ch : c.chords) {
                if (!ch.polled || !ch.valid()) continue;
                bool seen = false;
                for (int k : keys) seen = seen || k == ch.key;
                if (!seen) keys.push_back(ch.key);
            }
        return keys;
    }
    // 이번 프레임에 실행할 단축키 명령(위 규칙). 실행은 부르는 쪽이 execute(id) 로.
    EditorShortcutResult resolveShortcut(const EditorShortcutInput& in) const {
        EditorShortcutResult r;
        if (in.modal || in.cameraMode || !in.keyEdge) return r;
        for (const EditorCommand& c : cmds_) {
            for (const EditorChord& ch : c.chords) {
                if (!ch.polled || !ch.valid() || !in.keyEdge(ch.key)) continue;
                if (!ch.matches(ch.key, in.ctrl, in.shift)) continue;
                if (ch.ctrl && in.editDrag) { r.dragNotice = r.dragNotice || c.dragNotice; continue; }
                if (c.needsNoTextFocus && in.textFocus) continue;
                r.command = &c;
                return r;
            }
        }
        return r;
    }
    size_t size() const { return cmds_.size(); }
    const std::vector<EditorCommand>& commands() const { return cmds_; }
    uint64_t executedCount() const { return executed_; }

private:
    EditorCommand* findMut(std::string_view id) {
        for (EditorCommand& e : cmds_)
            if (e.id == id) return &e;
        return nullptr;
    }
    std::vector<EditorCommand> cmds_;
    uint64_t                   executed_ = 0;
};

// ── 에디터 명령 정의 표 ────────────────────────────────────────────────────────────────────────────────────
// 등록 순서가 단축키 우선순위입니다. 아래 순서는 H2a 까지의 tickShortcuts 가 키를 본 순서와 같습니다:
// F(맞추기) → Ctrl+D → Ctrl+S · Ctrl+Shift+S → Ctrl+O → Ctrl+N → Ctrl+F → Ctrl+Z → Ctrl+Y · Ctrl+Shift+Z.
// 동적 명령(언어 · Window 패널 목록)은 EditorApp 이 따로 더합니다(단축키 없음).
namespace heditorcmd {

inline std::vector<EditorCommand> defaultSpecs() {
    using S = EditorChord::Shift;
    auto chord = [](int key, bool ctrl, S shift, bool polled = true) {
        EditorChord c;
        c.key = key; c.ctrl = ctrl; c.shift = shift; c.polled = polled;
        return c;
    };
    auto cmd = [](const char* id, HText label, const char* icon, std::vector<EditorChord> chords,
                  bool needsNoText = false, bool dragNotice = false) {
        EditorCommand c;
        c.id = id; c.label = std::move(label); c.icon = icon; c.chords = std::move(chords);
        c.needsNoTextFocus = needsNoText; c.dragNotice = dragNotice;
        return c;
    };
    std::vector<EditorCommand> v;
    // ── 단축키가 있는 명령(우선순위 순) ──
    v.push_back(cmd("View.FrameSelected", HTEXT("Editor.Menu", "Edit.FrameSelected", "Frame Selected"), "focus",
                    { chord(HInput::KEY_F, false, S::Any) }, true));
    v.push_back(cmd("Edit.Duplicate", HTEXT("Editor.Menu", "Edit.Duplicate", "Duplicate"), "copy-plus",
                    { chord(HInput::KEY_D, true, S::Any) }, true, true));
    v.push_back(cmd("File.SaveScene", HTEXT("Editor.Menu", "File.SaveScene", "Save Scene"), "save",
                    { chord(HInput::KEY_S, true, S::Off) }, false, true));
    v.push_back(cmd("File.SaveSceneAs", HTEXT("Editor.Menu", "File.SaveSceneAs", "Save Scene As..."), "file-pen",
                    { chord(HInput::KEY_S, true, S::On) }, false, true));
    v.push_back(cmd("File.OpenScene", HTEXT("Editor.Menu", "File.OpenScene", "Open Scene..."), "folder-open",
                    { chord(HInput::KEY_O, true, S::Any) }, false, true));
    v.push_back(cmd("File.NewScene", HTEXT("Editor.Menu", "File.NewScene", "New Scene"), "file",
                    { chord(HInput::KEY_N, true, S::Any) }, false, true));
    v.push_back(cmd("Outliner.FocusSearch", HTEXT("Editor.Command", "Outliner.FocusSearch", "Search Outliner"), "search",
                    { chord(HInput::KEY_F, true, S::Any) }));
    v.push_back(cmd("Edit.Undo", HTEXT("Editor.Menu", "Edit.Undo", "Undo"), "undo-2",
                    { chord(HInput::KEY_Z, true, S::Off) }, true));
    v.push_back(cmd("Edit.Redo", HTEXT("Editor.Menu", "Edit.Redo", "Redo"), "redo-2",
                    { chord(HInput::KEY_Y, true, S::Any), chord(HInput::KEY_Z, true, S::On) }, true));
    // ── 단축키 표시만(처리는 TextInputController 의 포커스 없음 분기, 같은 명령 id) ──
    v.push_back(cmd("Edit.Copy", HTEXT("Editor.Menu", "Edit.Copy", "Copy"), "copy",
                    { chord(HInput::KEY_C, true, S::Any, false) }));
    v.push_back(cmd("Edit.Paste", HTEXT("Editor.Menu", "Edit.Paste", "Paste"), "clipboard-paste",
                    { chord(HInput::KEY_V, true, S::Any, false) }));
    v.push_back(cmd("Edit.Delete", HTEXT("Editor.Menu", "Edit.Delete", "Delete"), "trash-2",
                    { chord(HInput::KEY_DELETE, false, S::Any, false) }));
    // ── 단축키 없는 명령 ──
    v.push_back(cmd("View.GizmoSize", HTEXT("Editor.Menu", "Edit.GizmoSize", "Gizmo Size (S/M/L)"), "scaling", {}));
    v.push_back(cmd("Edit.AudioOutput", HTEXT("Editor.Menu", "Edit.AudioOutput", "Audio Output..."), "volume-2", {}));
    v.push_back(cmd("Edit.ProjectSettings", HTEXT("Editor.Menu", "Edit.ProjectSettings", "Project Settings..."), "sliders-horizontal", {}));
    v.push_back(cmd("Actor.AddEmpty", HTEXT("Editor.Menu", "Actor.AddEmpty", "Add Empty"), "plus", {}));
    v.push_back(cmd("Actor.AttachToPrev", HTEXT("Editor.Menu", "Actor.AttachToPrev", "Attach To Prev Selected"), "link", {}));
    v.push_back(cmd("Actor.Detach", HTEXT("Editor.Menu", "Actor.Detach", "Detach Selected"), "unlink", {}));
    v.push_back(cmd("Window.ResetLayout", HTEXT("Editor.Menu", "View.ResetLayout", "Reset Layout"), "layout-grid", {}));
    // 툴바(모양은 그대로, 같은 명령 id 로 부름)
    v.push_back(cmd("Play.Play", HTEXT("Editor.Command", "Play.Play", "Play"), "play", {}));
    v.push_back(cmd("Play.PlayDesktop", HTEXT("Editor.Command", "Play.PlayDesktop", "Play on Desktop"), "monitor", {}));
    v.push_back(cmd("Play.Pause", HTEXT("Editor.Command", "Play.Pause", "Pause"), "pause", {}));
    v.push_back(cmd("Play.Stop", HTEXT("Editor.Command", "Play.Stop", "Stop"), "square", {}));
    v.push_back(cmd("Content.Refresh", HTEXT("Editor.Command", "Content.Refresh", "Refresh Content"), "refresh-cw", {}));
    // 디테일 속성 행 오른쪽 메뉴(H3a): 메뉴를 연 행에 대해 동작
    v.push_back(cmd("Details.ResetToDefault", HTEXT("Editor.Command", "Details.ResetToDefault", "Reset to Default"), "rotate-ccw", {}));
    v.push_back(cmd("Details.CopyValue", HTEXT("Editor.Command", "Details.CopyValue", "Copy"), "copy", {}));
    v.push_back(cmd("Details.PasteValue", HTEXT("Editor.Command", "Details.PasteValue", "Paste"), "clipboard-paste", {}));
    // Output Log 표시 옵션(C2)
    v.push_back(cmd("OutputLog.TimeNone",    HTEXT("Editor.OutputLog", "Opt.TimeNone",    "None"),                "eye-off",       {}));
    v.push_back(cmd("OutputLog.TimeUptime",  HTEXT("Editor.OutputLog", "Opt.TimeUptime",  "Seconds Since Launch"), "timer",         {}));
    v.push_back(cmd("OutputLog.TimeClock",   HTEXT("Editor.OutputLog", "Opt.TimeClock",   "Local Time"),          "clock",         {}));
    v.push_back(cmd("OutputLog.Collapse",    HTEXT("Editor.OutputLog", "Opt.Collapse",    "Collapse"),            "list-collapse", {}));
    v.push_back(cmd("OutputLog.WordWrap",    HTEXT("Editor.OutputLog", "Opt.WordWrap",    "Word Wrap"),           "wrap-text",     {}));
    v.push_back(cmd("OutputLog.ClearOnPlay", HTEXT("Editor.OutputLog", "Opt.ClearOnPlay", "Clear on Play"),       "eraser",        {}));
    // 콘솔 포커스(C3): 표시 전용 chord (` 키 처리는 TextInputController 가 콘솔 포커스/비포커스 분기로 맡음)
    v.push_back(cmd("Console.Focus", HTEXT("Editor.Command", "Console.Focus", "Console"), "terminal",
                    { chord(HInput::KEY_GRAVE_ACCENT, false, S::Any, false) }));
    return v;
}

inline void addDefaultSpecs(EditorCommandRegistry& r) {
    for (EditorCommand& c : defaultSpecs()) r.add(std::move(c));
}

// ── 콘솔 명령 처리 (B1, 순수 함수) ───────────────────────────────────────────────────────────────────────────
inline std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

// 없는 id 를 쳤을 때 비슷한 id 를 앞부분 일치 우선 · 포함 순으로 최대 maxCount 개 고릅니다(id 사전순).
inline std::vector<std::string> suggestIds(const EditorCommandRegistry& reg, std::string_view query, size_t maxCount = 5) {
    std::vector<std::string> out;
    if (query.empty() || maxCount == 0) return out;
    std::vector<std::string> ids;
    ids.reserve(reg.commands().size());
    for (const EditorCommand& c : reg.commands()) ids.push_back(c.id);
    std::sort(ids.begin(), ids.end());
    const std::string q = toLowerAscii(query);
    for (const std::string& id : ids) {
        if (out.size() >= maxCount) break;
        if (toLowerAscii(id).rfind(q, 0) == 0) out.push_back(id);
    }
    for (const std::string& id : ids) {
        if (out.size() >= maxCount) break;
        const std::string low = toLowerAscii(id);
        if (low.rfind(q, 0) != 0 && low.find(q) != std::string::npos) out.push_back(id);
    }
    return out;
}

struct ConsoleCmdResult {
    bool        executed = false;
    bool        warning  = false;
    std::string text;
};

// `cmd <id>`: 메뉴와 같은 경로(execute)로 실행합니다. 단축키 전용 조건(needsNoTextFocus 등)은 콘솔 입력 칸에 포커스가 있는
// 상태에서 실행하므로 적용하지 않습니다.
inline ConsoleCmdResult runConsoleCmd(EditorCommandRegistry& reg, const std::vector<std::string>& args) {
    if (args.size() != 1) return { false, false, "usage: cmd <id>" };
    const std::string& id = args[0];
    if (!reg.has(id)) {
        const std::vector<std::string> sim = suggestIds(reg, id, 5);
        std::string msg = "unknown editor command '" + id + "'";
        if (!sim.empty()) {
            msg += " (did you mean: ";
            for (size_t i = 0; i < sim.size(); ++i) {
                if (i) msg += ", ";
                msg += sim[i];
            }
            msg += ")";
        }
        return { false, true, std::move(msg) };
    }
    if (!reg.canExecute(id)) return { false, false, "cannot execute '" + id + "' right now" };
    reg.execute(id);
    return { true, false, "executed: " + id };
}

// `cmd.list [filter]`: 등록된 명령을 id 순으로 보입니다(한 줄: id · 지금 문화권 라벨 · 단축키 글 · 실행 가능 여부).
inline std::string formatCommandList(const EditorCommandRegistry& reg, std::string_view filter = {}) {
    std::vector<const EditorCommand*> sorted;
    sorted.reserve(reg.commands().size());
    for (const EditorCommand& c : reg.commands()) sorted.push_back(&c);
    std::sort(sorted.begin(), sorted.end(), [](const EditorCommand* a, const EditorCommand* b) { return a->id < b->id; });

    const std::string f = toLowerAscii(filter);
    std::string out;
    int count = 0;
    for (const EditorCommand* c : sorted) {
        const std::string lbl = c->label.toString();
        if (!f.empty()) {
            if (toLowerAscii(c->id).find(f) == std::string::npos &&
                toLowerAscii(lbl).find(f) == std::string::npos)
                continue;
        }
        const std::string sc = reg.shortcutText(c->id);
        const bool can = reg.canExecute(c->id);
        if (count++) out += '\n';
        out += c->id + "  \"" + lbl + "\"  [" + (sc.empty() ? std::string("-") : sc) + "]  (" + (can ? "enabled" : "disabled") + ")";
    }
    if (count == 0)
        return filter.empty() ? std::string("no editor commands") : "no editor commands matching '" + std::string(filter) + "'";
    return out;
}

}  // namespace heditorcmd
