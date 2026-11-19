#include "TextInputController.hpp"
#include "../input/HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include <vector>

void TextInputController::init(Deps deps) {
    deps_ = std::move(deps);
}

EFocusTarget TextInputController::currentFocus() const {
    if (deps_.isDialogFocused && deps_.isDialogFocused()) return EFocusTarget::Dialog;
    if (SNumberField::focusedField())                     return EFocusTarget::NumberField;
    if (deps_.renameField    && deps_.renameField().focused)    return EFocusTarget::Rename;
    if (deps_.searchField    && deps_.searchField().focused)    return EFocusTarget::Search;
    if (deps_.logSearchField && deps_.logSearchField().focused) return EFocusTarget::LogSearch;
    return EFocusTarget::None;
}

void TextInputController::setFocus(EFocusTarget t) {
    const EFocusTarget prev = currentFocus();
    if (prev == t) return;

    auto getField = [this](EFocusTarget target) -> TextInputField {
        if (target == EFocusTarget::Rename)    return deps_.renameField();
        if (target == EFocusTarget::Search)    return deps_.searchField();
        return deps_.logSearchField();
    };

    // 이전 포커스를 정리합니다. 숫자 필드는 편집 중이던 값을 확정해야, 나중에 다른 곳에서
    // 누른 Enter 가 낡은 값으로 되살아나지 않습니다.
    if (prev == EFocusTarget::NumberField) {
        if (auto* nf = SNumberField::focusedField()) nf->commitEdit();
    } else if (prev == EFocusTarget::Rename || prev == EFocusTarget::Search || prev == EFocusTarget::LogSearch) {
        auto f = getField(prev);
        f.focused     = false;
        f.selectedAll = false;
        if (f.onChanged) f.onChanged();
    } else if (prev == EFocusTarget::Dialog) {
        if (deps_.endDialogFocus) deps_.endDialogFocus();
    }

    if (t == EFocusTarget::Rename) {
        auto f = getField(t);
        if (!f.onBegin || f.onBegin()) {
            f.focused     = true;
            f.selectedAll = true;   // 윈도우 표준: 이름 변경 시작 시 전체 선택
        }
        if (f.onChanged) f.onChanged();
    } else if (t == EFocusTarget::Search || t == EFocusTarget::LogSearch) {
        auto f = getField(t);
        f.focused     = true;
        f.selectedAll = !f.buffer.empty();
        if (f.onChanged) f.onChanged();
    } else if (t == EFocusTarget::Dialog) {
        if (deps_.beginDialogFocus) deps_.beginDialogFocus();
    }

    if (deps_.window && *deps_.window) {
        IWindow& w = **deps_.window;
        // 포커스가 없던 동안 쌓인 문자를 버립니다. 그러지 않으면 그동안 누른 글자가
        // 입력창을 여는 순간 한꺼번에 쏟아집니다.
        std::vector<unsigned int> junk;
        w.drainCharQueue(junk);
        // 키 엣지를 현재 물리 상태로 다시 심습니다. 누른 채로 진입하면 그 키가 곧바로
        // 엣지로 잡혀(예: Enter 를 누른 채 리네임 진입) 편집이 즉시 끝나 버립니다.
        prevEnterDown_  = w.isKeyDown(HInput::KEY_ENTER);
        prevF2Down_     = w.isKeyDown(HInput::KEY_F2);
        prevDeleteDown_ = w.isKeyDown(HInput::KEY_DELETE);
        prevVDown_      = w.isKeyDown(HInput::KEY_V);
        prevCDown_      = w.isKeyDown(HInput::KEY_C);
        prevADown_      = w.isKeyDown(HInput::KEY_A);
    }

    // 텍스트 입력을 시작하면 열려 있던 메뉴는 닫습니다.
    if (t != EFocusTarget::None && deps_.onCloseMenus)
        deps_.onCloseMenus();
}

bool TextInputController::focusWidgetContains(float x, float y) const {
    const glm::vec2 p{ x, y };
    switch (currentFocus()) {
        case EFocusTarget::NumberField:
            // 숫자 칸은 디테일 스크롤 상자 안에만 있습니다. 스크롤로 가려진 칸은 그 자리가 다른 패널이므로,
            // 상자 영역 밖의 누름은 칸 밖으로 칩니다. 아니면 가려진 칸 위를 눌러도 편집이 끝나지 않습니다.
            if (auto* nf = SNumberField::focusedField())
                return nf->cachedGeometry().contains(p) &&
                       (!deps_.detailsScrollContains || deps_.detailsScrollContains(p));
            return false;
        case EFocusTarget::Search:
            return deps_.searchField && deps_.searchField().contains(p);
        case EFocusTarget::LogSearch:
            return deps_.logSearchField && deps_.logSearchField().contains(p);
        case EFocusTarget::Rename:
            return deps_.renameField && deps_.renameField().contains(p);
        case EFocusTarget::Dialog:
            return false;
        case EFocusTarget::None:
        default:
            return false;
    }
}

TextInputController::KeyEdges TextInputController::sampleEdges(IWindow& win) {
    // 키 엣지를 여기서 **한 번에** 계산하고 prev 상태를 조건 없이 갱신합니다. 예전에는
    // 숫자 필드 분기가 중간에서 return 해 F2·Delete 엣지가 갱신되지 않았고, 그 탓에 값을
    // 확정한 직후 눌린 채였던 Delete 가 선택 액터를 지웠습니다(undo 없음).
    const bool ctrlHeld = win.isCtrlDown();
    const bool vNow     = win.isKeyDown(HInput::KEY_V);
    const bool cNow     = win.isKeyDown(HInput::KEY_C);
    const bool aNow     = win.isKeyDown(HInput::KEY_A);
    const bool f2Now    = win.isKeyDown(HInput::KEY_F2);
    const bool delNow   = win.isKeyDown(HInput::KEY_DELETE);
    const bool enNow    = win.isKeyDown(HInput::KEY_ENTER);
    const bool bsNow    = win.isKeyDown(HInput::KEY_BACKSPACE);
    // 백스페이스는 누르고 있으면 OS 자동 반복 간격으로 거듭 지웁니다(DEF-58).
    if (const EFocusTarget f = currentFocus(); f != backspaceFocus_) {
        backspaceFocus_   = f;
        backspaceBlocked_ = bsNow;
    }
    if (backspaceBlocked_ && !bsNow) backspaceBlocked_ = false;
    const int bsCount = backspaceBlocked_ ? 0 : win.keyPressCount(HInput::KEY_BACKSPACE);
    const bool escNow = win.isKeyDown(HInput::KEY_ESCAPE);

    KeyEdges k;
    k.ctrlHeld   = ctrlHeld;
    k.pasteEdge  = ctrlHeld && vNow && !prevVDown_;
    k.copyEdge   = ctrlHeld && cNow && !prevCDown_;
    k.selAllEdge = ctrlHeld && aNow && !prevADown_;
    k.f2Edge     = f2Now  && !prevF2Down_;
    k.delEdge    = delNow && !prevDeleteDown_;
    k.enterEdge  = enNow  && !prevEnterDown_;
    k.escEdge    = escNow && !prevEscapeDown_;
    k.bsCount    = bsCount;

    prevVDown_      = vNow;
    prevCDown_      = cNow;
    prevADown_      = aNow;
    prevF2Down_     = f2Now;
    prevDeleteDown_ = delNow;
    prevEnterDown_  = enNow;
    prevEscapeDown_ = escNow;
    return k;
}

bool TextInputController::editBuffer(IWindow& win, std::string& buf, bool& selAll, const KeyEdges& k) {
    bool dirty = false;
    auto clearIfSelAll = [&] {
        if (selAll) { buf.clear(); selAll = false; dirty = true; }
    };

    if (k.selAllEdge && !buf.empty()) { selAll = true; dirty = true; }

    std::vector<unsigned int> chars;
    win.drainCharQueue(chars);
    for (unsigned int cp : chars) {
        // 폰트 아틀라스가 ASCII 전용이라, 그릴 수 없는 글자는 애초에 받지 않습니다.
        if (cp >= 32 && cp < 127) {
            clearIfSelAll();
            buf += (char)cp;
            dirty = true;
        }
    }

    if (k.pasteEdge) {
        clearIfSelAll();
        for (char c : win.getClipboardText())
            if (c >= 32 && c < 127) { buf += c; dirty = true; }
    }
    if (k.copyEdge) win.setClipboardText(buf);

    for (int i = 0; i < k.bsCount && !buf.empty(); ++i) {
        if (selAll) clearIfSelAll();
        else        buf.pop_back();
        dirty = true;
    }
    return dirty;
}

void TextInputController::handleModalInput(IWindow& win, EFocusTarget focus, const KeyEdges& k) {
    if (focus == EFocusTarget::Dialog && deps_.dialogBuffer) {
        std::string* buf = nullptr;
        bool* selAll = nullptr;
        deps_.dialogBuffer(buf, selAll);
        if (buf && selAll) editBuffer(win, *buf, *selAll, k);
    } else {
        std::vector<unsigned int> junk;
        win.drainCharQueue(junk);
    }
    if (k.enterEdge)    { if (deps_.onModalDefault) deps_.onModalDefault(); }
    else if (k.escEdge) { if (deps_.onModalCancel)  deps_.onModalCancel();  }
}

void TextInputController::handleNumberFieldInput(IWindow& win, const KeyEdges& k) {
    auto* nf = SNumberField::focusedField();
    std::vector<unsigned int> chars;
    win.drainCharQueue(chars);
    for (unsigned int cp : chars) nf->onChar(cp);

    if (k.selAllEdge) nf->selectAll();
    if (k.pasteEdge)
        for (char c : win.getClipboardText()) nf->onChar((unsigned char)c);
    if (k.copyEdge)  win.setClipboardText(nf->editBuffer());
    for (int i = 0; i < k.bsCount; ++i) nf->onBackspace();
    if (k.enterEdge) nf->commitEdit();
}

void TextInputController::handleTextFieldInput(IWindow& win, EFocusTarget focus, const KeyEdges& k) {
    auto f = (focus == EFocusTarget::Rename) ? deps_.renameField()
           : (focus == EFocusTarget::Search) ? deps_.searchField()
                                             : deps_.logSearchField();
    const bool dirty = editBuffer(win, f.buffer, f.selectedAll, k);
    if (k.enterEdge) {
        if (f.onCommit) f.onCommit();
        setFocus(EFocusTarget::None);
        return;
    }
    if (dirty && f.onChanged) f.onChanged();
}

void TextInputController::tickInput() {
    if (!deps_.window || !*deps_.window) return;
    IWindow& win = **deps_.window;
    const KeyEdges k = sampleEdges(win);
    const EFocusTarget focus = currentFocus();

    if (deps_.isModalOpen && deps_.isModalOpen()) {
        handleModalInput(win, focus, k);
        return;
    }

    // Esc — 무엇을 편집하고 있든 취소하고 빠져나옵니다(숫자 필드는 값을 확정).
    if (k.escEdge && focus != EFocusTarget::None) {
        setFocus(EFocusTarget::None);
        return;
    }

    // F2 — 리네임 진입·종료. 다른 텍스트 편집 중에는 반응하지 않습니다.
    if (k.f2Edge && (focus == EFocusTarget::None || focus == EFocusTarget::Rename)) {
        // 리네임은 아웃라이너 행에서 보이므로, 닫혀 있거나 가려진 탭이면 먼저 꺼냅니다(Ctrl+F 와 같은 이유).
        if (focus != EFocusTarget::Rename && deps_.onShowOutliner) deps_.onShowOutliner();
        setFocus(focus == EFocusTarget::Rename ? EFocusTarget::None : EFocusTarget::Rename);
        return;
    }

    if (focus == EFocusTarget::NumberField) {
        handleNumberFieldInput(win, k);
        return;
    }

    if (focus == EFocusTarget::Rename || focus == EFocusTarget::Search ||
        focus == EFocusTarget::LogSearch) {
        handleTextFieldInput(win, focus, k);
        return;
    }

    // ── 포커스 없음 ───────────────────────────────────────────────────────────
    if (k.delEdge  && deps_.onDeleteSelection) deps_.onDeleteSelection();
    if (k.copyEdge && deps_.onCopySelection)   deps_.onCopySelection();
    if (k.pasteEdge && (!deps_.isEditDragActive || !deps_.isEditDragActive()))
        if (deps_.onPasteClipboard) deps_.onPasteClipboard();   // 붙여넣기도 주 선택을 바꿉니다(DEF-132)

    // 아무도 받지 않는 문자는 버립니다. 쌓아 두면 다음에 입력창을 여는 순간
    // 그동안 누른 글자가 한꺼번에 쏟아집니다.
    std::vector<unsigned int> junk;
    win.drainCharQueue(junk);
}
