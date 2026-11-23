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
    std::string                  command;      // Command: 레지스트리 명령 id(비면 아래 값으로 그 자리 동작)
    std::string                  label;        // 소제목 · 하위 메뉴 · 그 자리 동작의 글(지금 문화권으로 푼 글)
    std::string                  englishLabel; // 영어 원문 라벨(가속 글자 · 드롭다운 글자 키 대조용, 비면 label 사용)
    std::string                  icon;         // 그 자리 동작 · 하위 메뉴의 아이콘(명령은 명령 아이콘)
    std::string                  shortcut;     // 그 자리 동작의 단축키 글(표시만)
    std::function<void()>        action;       // 그 자리 동작
    std::function<bool()>        enabled;      // 그 자리 동작 · 하위 메뉴(비면 켜짐)
    std::function<bool()>        checked;      // 그 자리 동작(비면 체크 없음)
    std::vector<EditorMenuEntry> children;     // SubMenu

    static EditorMenuEntry cmd(std::string id) {
        EditorMenuEntry e;
        e.kind = Kind::Command;
        e.command = std::move(id);
        return e;
    }
    static EditorMenuEntry act(std::string label, std::function<void()> fn, bool on = true,
                               std::string icon = {}, std::string englishLabel = {}) {
        EditorMenuEntry e;
        e.kind = Kind::Command;
        e.label = std::move(label);
        e.englishLabel = std::move(englishLabel);
        e.icon = std::move(icon);
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
    static EditorMenuEntry submenu(std::string label, std::vector<EditorMenuEntry> children, std::string icon = {},
                                   std::string englishLabel = {}) {
        EditorMenuEntry e;
        e.kind = Kind::SubMenu;
        e.label = std::move(label);
        e.englishLabel = std::move(englishLabel);
        e.children = std::move(children);
        e.icon = std::move(icon);
        return e;
    }
};

// 연 순간에 푼 한 행(그리기 · 키보드 이동 · 시험이 씀).
struct EditorMenuRow {
    EditorMenuEntry::Kind kind = EditorMenuEntry::Kind::Command;
    std::string label;
    std::string englishLabel;
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
                r.label        = e.label;
                r.englishLabel = e.englishLabel.empty() ? e.label : e.englishLabel;
                r.icon         = e.icon;
                r.enabled      = (!e.enabled || e.enabled()) && !e.children.empty();
                break;
            case Kind::Command:
                if (!e.command.empty()) {
                    const EditorCommand* c = reg ? reg->find(e.command) : nullptr;
                    if (!c) continue;   // 정의되지 않은 명령은 메뉴에 넣지 않습니다
                    r.label        = c->label.toString();
                    r.englishLabel = c->label.source();
                    r.icon         = c->icon;
                    r.shortcut     = reg->shortcutText(e.command);
                    r.enabled      = reg->canExecute(e.command);
                    r.hasCheck     = (bool)c->isChecked;
                    r.checked      = r.hasCheck && c->isChecked();
                } else {
                    r.label        = e.label;
                    r.englishLabel = e.englishLabel.empty() ? e.label : e.englishLabel;
                    r.icon         = e.icon;
                    r.shortcut     = e.shortcut;
                    r.enabled      = (bool)e.action && (!e.enabled || e.enabled());
                    r.hasCheck     = (bool)e.checked;
                    r.checked      = r.hasCheck && e.checked();
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

// ── Alt 메뉴 바 · 가속 글자 · 드롭다운 글자 키 (C1) ──────────────────────────────────────────────────────────
inline char toUpperAscii(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}
inline bool isAsciiAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
inline bool isAsciiAlnum(char c) {
    return isAsciiAlpha(c) || (c >= '0' && c <= '9');
}

// 영어 원문 라벨 목록에서 메뉴 바 안에서 겹치지 않는 ASCII 대문자 가속 글자('A'..'Z')를 정합니다.
// 기본 첫 글자, 겹치면 다음 글자 순으로 고릅니다. 없으면 '\0'.
inline std::vector<char> assignBarAccelerators(const std::vector<std::string>& englishLabels) {
    std::vector<char> out(englishLabels.size(), '\0');
    bool used[26] = {};
    for (size_t i = 0; i < englishLabels.size(); ++i) {
        for (char c : englishLabels[i]) {
            if (!isAsciiAlpha(c)) continue;
            const char u = toUpperAscii(c);
            if (!used[u - 'A']) {
                used[u - 'A'] = true;
                out[i] = u;
                break;
            }
        }
    }
    return out;
}

// label 안에서 가속 글자 accel(대소문자 무시)이 처음 나타나는 바이트 인덱스. 없으면 -1.
inline int findAcceleratorIndex(std::string_view label, char accel) {
    if (!isAsciiAlpha(accel)) return -1;
    const char target = toUpperAscii(accel);
    for (size_t i = 0; i < label.size(); ++i)
        if (toUpperAscii(label[i]) == target) return (int)i;
    return -1;
}

// 열린 드롭다운에서 ASCII 글자 키로 다음 켜진 항목을 찾습니다.
// 지금 문화권 라벨 첫 글자(ASCII) 또는 영어 원문 라벨 첫 글자(한국어 등 비-ASCII 라벨일 때 포함)로 비교합니다.
// 소제목 · 구분선 · 꺼진 항목은 건너뛰고, 맞는 항목이 하나뿐이면 uniqueMatch = true.
inline int findMnemonicRow(const std::vector<EditorMenuRow>& rows, int from, char asciiKey, bool& uniqueMatch) {
    uniqueMatch = false;
    if (!isAsciiAlnum(asciiKey) || rows.empty()) return -1;
    const char target = toUpperAscii(asciiKey);
    auto matches = [target](const EditorMenuRow& r) {
        if (!selectable(r)) return false;
        if (!r.label.empty() && (unsigned char)r.label[0] < 0x80 && toUpperAscii(r.label[0]) == target) return true;
        if (!r.englishLabel.empty() && (unsigned char)r.englishLabel[0] < 0x80 && toUpperAscii(r.englishLabel[0]) == target)
            return true;
        return false;
    };
    int matchCount = 0;
    for (const EditorMenuRow& r : rows)
        if (matches(r)) ++matchCount;
    if (matchCount == 0) return -1;
    uniqueMatch = (matchCount == 1);
    const int n = (int)rows.size();
    int i = from;
    for (int k = 0; k < n; ++k) {
        if (i < 0 || i >= n) i = 0;
        else                 i = (i + 1) % n;
        if (matches(rows[(size_t)i])) return i;
    }
    return -1;
}

struct BarState {
    bool altDown     = false;
    bool altIsLeft   = false;
    bool altDirty    = false;
    bool barActive   = false;
    int  focusedMenu = -1;
    int  openMenu    = -1;

    bool showMnemonics() const { return barActive || altDown; }
};

enum class BarEventKind : uint8_t {
    AltPress,
    AltRelease,
    OtherKey,
    AccelKey,
    NavLeft,
    NavRight,
    NavDown,
    NavEnter,
    NavSpace,
    NavEscape,
    MouseOrWheel,
    FocusLost,
    Blocked,
};

struct BarEvent {
    BarEventKind kind   = BarEventKind::OtherKey;
    char         ch     = '\0';
    bool         isLeft = true;
};

enum class BarAct : uint8_t {
    None,
    BarChanged,
    OpenMenu,
    CloseMenu,
};

inline BarAct stepBar(BarState& st, const BarEvent& ev, const std::vector<char>& barAccels) {
    const int n = (int)barAccels.size();
    switch (ev.kind) {
        case BarEventKind::Blocked: {
            const bool changed = st.altDown || st.altDirty || st.barActive || st.focusedMenu >= 0;
            st.altDown = false;
            st.altIsLeft = false;
            st.altDirty = false;
            st.barActive = false;
            st.focusedMenu = -1;
            return changed ? BarAct::BarChanged : BarAct::None;
        }
        case BarEventKind::FocusLost: {
            const bool changed = st.altDown || st.barActive || st.focusedMenu >= 0;
            st.altDown = false;
            st.altIsLeft = false;
            st.altDirty = false;
            st.barActive = false;
            st.focusedMenu = -1;
            if (st.openMenu >= 0) {
                st.openMenu = -1;
                return BarAct::CloseMenu;
            }
            return changed ? BarAct::BarChanged : BarAct::None;
        }
        case BarEventKind::MouseOrWheel: {
            if (st.altDown) st.altDirty = true;
            if (st.barActive) {
                st.barActive = false;
                st.focusedMenu = -1;
                return BarAct::BarChanged;
            }
            return BarAct::None;
        }
        case BarEventKind::AltPress:
            st.altDown = true;
            st.altIsLeft = ev.isLeft;
            st.altDirty = false;
            return BarAct::BarChanged;
        case BarEventKind::AltRelease: {
            if (!st.altDown) return BarAct::None;
            const bool dirty = st.altDirty;
            const bool left  = st.altIsLeft;
            st.altDown = false;
            st.altIsLeft = false;
            st.altDirty = false;
            if (dirty || !left) return BarAct::BarChanged;
            if (st.openMenu >= 0) {
                st.openMenu = -1;
                st.barActive = false;
                st.focusedMenu = -1;
                return BarAct::CloseMenu;
            }
            if (st.barActive) {
                st.barActive = false;
                st.focusedMenu = -1;
                return BarAct::BarChanged;
            }
            if (n > 0) {
                st.barActive = true;
                st.focusedMenu = 0;
            }
            return BarAct::BarChanged;
        }
        case BarEventKind::OtherKey:
            if (st.altDown) st.altDirty = true;
            return BarAct::None;
        case BarEventKind::AccelKey: {
            if (!st.altDown && !st.barActive) return BarAct::None;
            if (st.altDown) st.altDirty = true;
            if (!isAsciiAlpha(ev.ch)) return BarAct::None;
            const char target = toUpperAscii(ev.ch);
            int hit = -1;
            for (int i = 0; i < n; ++i) {
                if (toUpperAscii(barAccels[(size_t)i]) == target) { hit = i; break; }
            }
            if (hit < 0) return BarAct::None;
            st.barActive = false;
            st.focusedMenu = hit;
            st.openMenu = hit;
            return BarAct::OpenMenu;
        }
        case BarEventKind::NavLeft:
            if (!st.barActive || n <= 0) return BarAct::None;
            st.focusedMenu = (st.focusedMenu - 1 + n) % n;
            return BarAct::BarChanged;
        case BarEventKind::NavRight:
            if (!st.barActive || n <= 0) return BarAct::None;
            st.focusedMenu = (st.focusedMenu + 1) % n;
            return BarAct::BarChanged;
        case BarEventKind::NavDown:
        case BarEventKind::NavEnter:
        case BarEventKind::NavSpace:
            if (!st.barActive || st.focusedMenu < 0 || st.focusedMenu >= n) return BarAct::None;
            st.barActive = false;
            st.openMenu = st.focusedMenu;
            return BarAct::OpenMenu;
        case BarEventKind::NavEscape:
            if (!st.barActive) return BarAct::None;
            st.barActive = false;
            st.focusedMenu = -1;
            return BarAct::BarChanged;
    }
    return BarAct::None;
}

}  // namespace hmenu
