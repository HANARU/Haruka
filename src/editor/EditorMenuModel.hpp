#pragma once
// ── 에디터 메뉴 모델 (H2b, 2026-11-22) ─────────────────────────────────────────────────────────────────────────
//
// UE FMultiBox · FMenuBuilder 를 줄인 것입니다. 상단 메뉴 드롭다운과 오른쪽 누름 메뉴가 같은 항목 모델 · 같은 위젯
// (EditorMenuView)을 씁니다.
//
//   MenuEntry = Command(명령 id) | Heading(소제목) | Separator(구분선) | SubMenu(하위 메뉴)
//   - Command 는 레지스트리 명령 id 를 가리키는 것이 기본입니다(글 · 아이콘 · 단축키 · 실행 가능 · 체크를 명령에서 읽음).
//     오른쪽 누름 메뉴처럼 그 자리의 값(경로)에 묶인 동작은 id 없이 글 · 동작 · 켜짐을 직접 줍니다(UE 의 FUIAction 항목).
//   - 열 때 한 번 rows 로 풉니다(resolve): 글은 지금 문화권, 켜짐 · 체크는 그 순간 값. 두 번째부터의 소제목 앞에는 구분선을 넣습니다.
//
// 키보드 이동 규칙(hmenu::decide, 시험: tests/EditorCommandTest.cpp)
//   ↑ ↓   : 고를 수 있는 행(켜진 명령 · 켜진 하위 메뉴)만 옮겨 다님(소제목 · 구분선 · 꺼진 항목은 건너뜀, 끝에서 처음으로 돎)
//   Enter : 명령이면 실행(메뉴를 모두 닫은 뒤), 하위 메뉴면 엶
//   →     : 하위 메뉴면 엶, 아니면 상단 메뉴 줄의 다음 메뉴(오른쪽 누름 메뉴는 무동작)
//   ←     : 하위 메뉴 안이면 그 하위 메뉴를 닫음, 아니면 상단 메뉴 줄의 이전 메뉴
//   Esc   : 한 단계 닫음(하위 메뉴 → 그 부모, 맨 위면 메뉴 전체)

#include "EditorCommandRegistry.hpp"

#include <functional>
#include <string>
#include <vector>

struct EditorMenuEntry {
    enum class Kind : uint8_t { Command, Heading, Separator, SubMenu };
    Kind                         kind = Kind::Command;
    std::string                  command;    // Command: 레지스트리 명령 id(비면 아래 값으로 그 자리 동작)
    std::string                  label;      // 소제목 · 하위 메뉴 · 그 자리 동작의 글(지금 문화권으로 푼 글)
    std::string                  icon;       // 그 자리 동작 · 하위 메뉴의 아이콘(명령은 명령 아이콘)
    std::string                  shortcut;   // 그 자리 동작의 단축키 글(표시만)
    std::function<void()>        action;     // 그 자리 동작
    std::function<bool()>        enabled;    // 그 자리 동작 · 하위 메뉴(비면 켜짐)
    std::function<bool()>        checked;    // 그 자리 동작(비면 체크 없음)
    std::vector<EditorMenuEntry> children;   // SubMenu

    static EditorMenuEntry cmd(std::string id) {
        EditorMenuEntry e;
        e.kind = Kind::Command;
        e.command = std::move(id);
        return e;
    }
    static EditorMenuEntry act(std::string label, std::function<void()> fn, bool on = true) {
        EditorMenuEntry e;
        e.kind = Kind::Command;
        e.label = std::move(label);
        e.action = std::move(fn);
        if (!on) e.enabled = [] { return false; };
        return e;
    }
    static EditorMenuEntry heading(std::string label) {
        EditorMenuEntry e;
        e.kind = Kind::Heading;
        e.label = std::move(label);
        return e;
    }
    static EditorMenuEntry separator() {
        EditorMenuEntry e;
        e.kind = Kind::Separator;
        return e;
    }
    static EditorMenuEntry submenu(std::string label, std::vector<EditorMenuEntry> children, std::string icon = {}) {
        EditorMenuEntry e;
        e.kind = Kind::SubMenu;
        e.label = std::move(label);
        e.children = std::move(children);
        e.icon = std::move(icon);
        return e;
    }
};

// 연 순간에 푼 한 행(그리기 · 키보드 이동 · 시험이 씀).
struct EditorMenuRow {
    EditorMenuEntry::Kind kind = EditorMenuEntry::Kind::Command;
    std::string label;
    std::string shortcut;
    std::string icon;
    bool        enabled  = true;
    bool        hasCheck = false;
    bool        checked  = false;
    int         entry    = -1;   // entries 안 자리(자동 구분선은 -1)
};

namespace hmenu {

using Kind = EditorMenuEntry::Kind;

inline std::vector<EditorMenuRow> resolve(const std::vector<EditorMenuEntry>& entries, const EditorCommandRegistry* reg) {
    std::vector<EditorMenuRow> rows;
    bool seenHeading = false;
    for (int i = 0; i < (int)entries.size(); ++i) {
        const EditorMenuEntry& e = entries[(size_t)i];
        EditorMenuRow r;
        r.kind  = e.kind;
        r.entry = i;
        switch (e.kind) {
            case Kind::Heading:
                // 두 번째부터의 소제목은 위에 구분선(UE 의 섹션 경계). 바로 앞이 구분선이면 더하지 않습니다.
                if (seenHeading && !rows.empty() && rows.back().kind != Kind::Separator) {
                    EditorMenuRow sep;
                    sep.kind = Kind::Separator;
                    rows.push_back(sep);
                }
                seenHeading = true;
                r.label   = e.label;
                r.enabled = false;
                break;
            case Kind::Separator:
                r.enabled = false;
                break;
            case Kind::SubMenu:
                r.label   = e.label;
                r.icon    = e.icon;
                r.enabled = (!e.enabled || e.enabled()) && !e.children.empty();
                break;
            case Kind::Command:
                if (!e.command.empty()) {
                    const EditorCommand* c = reg ? reg->find(e.command) : nullptr;
                    if (!c) continue;   // 정의되지 않은 명령은 메뉴에 넣지 않습니다
                    r.label    = c->label.toString();
                    r.icon     = c->icon;
                    r.shortcut = reg->shortcutText(e.command);
                    r.enabled  = reg->canExecute(e.command);
                    r.hasCheck = (bool)c->isChecked;
                    r.checked  = r.hasCheck && c->isChecked();
                } else {
                    r.label    = e.label;
                    r.icon     = e.icon;
                    r.shortcut = e.shortcut;
                    r.enabled  = (bool)e.action && (!e.enabled || e.enabled());
                    r.hasCheck = (bool)e.checked;
                    r.checked  = r.hasCheck && e.checked();
                }
                break;
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

inline bool selectable(const EditorMenuRow& r) {
    return r.enabled && (r.kind == Kind::Command || r.kind == Kind::SubMenu);
}

// from 에서 dir(+1 아래 · -1 위)로 다음 고를 수 있는 행. from < 0 이면 처음(아래) · 끝(위)부터. 끝에서 돌고, 없으면 -1.
inline int step(const std::vector<EditorMenuRow>& rows, int from, int dir) {
    const int n = (int)rows.size();
    if (n == 0) return -1;
    int i = from;
    for (int k = 0; k < n; ++k) {
        if (i < 0 || i >= n) i = dir > 0 ? 0 : n - 1;
        else                 i = (i + dir + n) % n;
        if (selectable(rows[(size_t)i])) return i;
    }
    return -1;
}
inline int firstSelectable(const std::vector<EditorMenuRow>& rows) { return step(rows, -1, +1); }

enum class Key : uint8_t { Up, Down, Left, Right, Enter, Escape };
enum class Act : uint8_t { None, MoveUp, MoveDown, Activate, OpenSub, CloseSub, PrevMenu, NextMenu, Close };

// depth = 열린 단계 수(1 = 맨 위 메뉴만), hl = 가장 깊은 단계의 강조 행(없으면 null), inBar = 상단 메뉴 줄의 드롭다운인가.
inline Act decide(Key k, int depth, const EditorMenuRow* hl, bool inBar) {
    const bool sub = hl && hl->kind == Kind::SubMenu && selectable(*hl);
    switch (k) {
        case Key::Up:     return Act::MoveUp;
        case Key::Down:   return Act::MoveDown;
        case Key::Enter:
            if (sub) return Act::OpenSub;
            if (hl && hl->kind == Kind::Command && selectable(*hl)) return Act::Activate;
            return Act::None;
        case Key::Right:
            if (sub) return Act::OpenSub;
            return inBar ? Act::NextMenu : Act::None;
        case Key::Left:
            if (depth > 1) return Act::CloseSub;
            return inBar ? Act::PrevMenu : Act::None;
        case Key::Escape:
            return depth > 1 ? Act::CloseSub : Act::Close;
    }
    return Act::None;
}

// 키 이름(덤프 · 시험 대본): up · down · left · right · enter · esc. 모르면 거짓.
inline bool parseKey(const std::string& s, Key& out) {
    if (s == "up")    { out = Key::Up;     return true; }
    if (s == "down")  { out = Key::Down;   return true; }
    if (s == "left")  { out = Key::Left;   return true; }
    if (s == "right") { out = Key::Right;  return true; }
    if (s == "enter") { out = Key::Enter;  return true; }
    if (s == "esc")   { out = Key::Escape; return true; }
    return false;
}

}  // namespace hmenu
