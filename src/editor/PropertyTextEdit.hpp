#pragma once
// ── 디테일 글 칸 편집 상태 (H3a, 2026-11-22) ─────────────────────────────────────────────────────────────────────
// 디테일 패널의 String 속성 행이 함께 쓰는 편집 칸 하나입니다. 편집 중인 행은 하나뿐이고(UE 도 같음), 키보드 포커스는 H1a-2 글 칸
// 체계(TextFieldBinding · EFocusTarget::TextField · setFocusWidget)로 받습니다. 규칙은 아웃라이너 이름 바꾸기 칸과 같습니다.
//   - Enter = 확정(값이 같으면 아무것도 하지 않음, 다르면 commit 한 번 = Undo 한 단계)
//   - Esc = 취소(값 그대로)
//   - 다른 곳 누름 · 다른 칸으로 포커스가 감 = 확정(H3b, UE · 숫자 칸과 같음. H3a 에서는 이름 바꾸기 칸처럼 취소였음)
//   - 행이 없어져 조용히 놓을 때(releaseQuiet → forget) = 아무것도 하지 않음
// 위젯 · 패널을 모르므로 화면 없이 시험합니다(tests/TextTest.cpp).

#include "TextInputController.hpp"

#include <functional>
#include <string>

class PropertyTextEdit {
public:
    // 행 key 의 편집을 시작할 준비를 합니다(포커스는 부르는 쪽이 줌). get = 지금 값, commit = 확정(Undo 포함).
    void prepare(std::string key, std::function<std::string()> get, std::function<void(const std::string&)> commit) {
        key_ = std::move(key);
        get_ = std::move(get);
        commit_ = std::move(commit);
    }
    // 다시 만들 때 같은 행의 새 함수로 바꿉니다(포커스 · 글은 그대로).
    void rebind(std::function<std::string()> get, std::function<void(const std::string&)> commit) {
        get_ = std::move(get);
        commit_ = std::move(commit);
    }
    bool editing(const std::string& key) const { return focused_ && key_ == key; }
    bool focused() const { return focused_; }
    const std::string& key() const { return key_; }
    HTextEditState& edit() { return edit_; }
    // 행이 없어져 포커스를 조용히 놓은 뒤(releaseQuiet) 부릅니다.
    void forget() { focused_ = false; key_.clear(); escaped_ = false; }

    // TextInputController 에 넘길 칸. changed = 포커스가 바뀜(들어감 · 나감: 행 모양을 다시 만듦), contains = 칸 위인가.
    TextInputField field(std::function<void()> changed, std::function<bool(glm::vec2)> contains) {
        TextInputField f{
            edit_,
            focused_,
            std::move(contains),
            // 포커스를 잃음(다른 곳 누름 · 다른 칸): 아직 끝나지 않은 편집이면 확정합니다(Esc 는 onEscape 가 먼저 표시해 둠).
            [this, changed] {
                if (!focused_) {
                    if (!key_.empty() && !escaped_) commitIfChanged();
                    key_.clear();
                    escaped_ = false;
                }
                if (changed) changed();
            },
            // 들어갈 때 지금 값으로 채웁니다. 편집할 행이 없으면 들어가지 않습니다.
            [this]() -> bool {
                if (key_.empty() || !get_) return false;
                escaped_ = false;
                edit_.setText(get_(), true);
                return true;
            },
            // Enter: 확정(뒤따르는 포커스 해제가 한 번 더 확정하지 않게 행을 비움)
            [this, changed] {
                commitIfChanged();
                key_.clear();
                if (changed) changed();
            }
        };
        f.onEscape = [this] { escaped_ = true; };
        f.onCancel = [this, changed] { key_.clear(); escaped_ = false; if (changed) changed(); };
        return f;
    }

private:
    void commitIfChanged() {
        const std::string text = edit_.text;
        if (commit_ && (!get_ || get_() != text)) commit_(text);
    }

    HTextEditState                          edit_;
    bool                                    escaped_ = false;   // 이번 나감이 Esc(취소)인가
    bool                                    focused_ = false;
    std::string                             key_;
    std::function<std::string()>            get_;
    std::function<void(const std::string&)> commit_;
};
