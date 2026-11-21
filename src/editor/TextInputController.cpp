#include "TextInputController.hpp"
#include "../input/HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include <iterator>
#include <vector>

void TextInputController::init(Deps deps) {
    deps_ = std::move(deps);
}

const std::function<TextInputField()>* TextInputController::fieldFn(EFocusTarget t) const {
    const std::function<TextInputField()>* fn = nullptr;
    switch (t) {
        case EFocusTarget::Rename:        fn = &deps_.renameField;        break;
        case EFocusTarget::Search:        fn = &deps_.searchField;        break;
        case EFocusTarget::LogSearch:     fn = &deps_.logSearchField;     break;
        case EFocusTarget::Console:       fn = &deps_.consoleField;       break;
        case EFocusTarget::Picker:        fn = &deps_.pickerField;        break;
        case EFocusTarget::ContentSearch: fn = &deps_.contentSearchField; break;
        default: break;
    }
    return fn && *fn ? fn : nullptr;
}

EFocusTarget TextInputController::currentFocus() const {
    if (deps_.isDialogFocused && deps_.isDialogFocused()) return EFocusTarget::Dialog;
    if (SNumberField::focusedField())                     return EFocusTarget::NumberField;
    for (EFocusTarget t : { EFocusTarget::Rename, EFocusTarget::Search, EFocusTarget::LogSearch, EFocusTarget::Console,
                            EFocusTarget::Picker, EFocusTarget::ContentSearch })
        if (const auto* fn = fieldFn(t); fn && (*fn)().focused) return t;
    return EFocusTarget::None;
}

void TextInputController::setFocus(EFocusTarget t) {
    const EFocusTarget prev = currentFocus();
    if (prev == t) return;

    // 이전 포커스를 정리합니다. 숫자 필드는 편집 중이던 값을 확정해야, 나중에 다른 곳에서
    // 누른 Enter 가 낡은 값으로 되살아나지 않습니다.
    if (prev == EFocusTarget::NumberField) {
        if (auto* nf = SNumberField::focusedField()) nf->commitEdit();
    } else if (const auto* fn = fieldFn(prev)) {
        auto f = (*fn)();
        f.focused = false;
        f.edit.clearSelection();
        f.edit.clearComposition();
        if (f.onChanged) f.onChanged();
    } else if (prev == EFocusTarget::Dialog) {
        if (deps_.endDialogFocus) deps_.endDialogFocus();
    }

    if (t == EFocusTarget::Dialog) {
        if (deps_.beginDialogFocus) deps_.beginDialogFocus();
    } else if (const auto* fn = fieldFn(t)) {
        auto f = (*fn)();
        if (t == EFocusTarget::Rename) {
            if (!f.onBegin || f.onBegin()) {
                f.focused = true;
                f.edit.selectAll();   // 윈도우 표준: 이름 변경 시작 시 전체 선택
            }
        } else if (t == EFocusTarget::Console) {
            f.focused = true;
            f.edit.end(false);   // 콘솔은 이어 쓰기(전체 선택 없음)
        } else {
            f.focused = true;
            f.edit.selectAll();   // 키보드로 들어오면 전체 선택(마우스로 누르면 위젯이 곧바로 그 자리에 커서를 놓음)
        }
        if (f.onChanged) f.onChanged();
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
        prevXDown_      = w.isKeyDown(HInput::KEY_X);
        // 글 입력 칸에서만 IME 를 켭니다(T2). 숫자 칸 · 포커스 없음은 끔.
        const bool textFocus = isTextField(t) || t == EFocusTarget::Dialog;
        w.setTextInputEnabled(textFocus);
    }

    // 텍스트 입력을 시작하면 열려 있던 메뉴는 닫습니다.
    if (t != EFocusTarget::None && deps_.onCloseMenus)
        deps_.onCloseMenus();
}

bool TextInputController::focusWidgetContains(float x, float y) const {
    const glm::vec2 p{ x, y };
    const EFocusTarget focus = currentFocus();
    if (focus == EFocusTarget::NumberField) {
        // 숫자 칸은 디테일 스크롤 상자 안에만 있습니다. 스크롤로 가려진 칸은 그 자리가 다른 패널이므로,
        // 상자 영역 밖의 누름은 칸 밖으로 칩니다. 아니면 가려진 칸 위를 눌러도 편집이 끝나지 않습니다.
        if (auto* nf = SNumberField::focusedField())
            return nf->cachedGeometry().contains(p) &&
                   (!deps_.detailsScrollContains || deps_.detailsScrollContains(p));
        return false;
    }
    if (const auto* fn = fieldFn(focus)) return (*fn)().contains(p);
    return false;   // 대화상자 · 없음
}

namespace {
// 반복 편집 키(막기 비트 번호 = 배열 자리).
constexpr int kRepeatKeys[] = { HInput::KEY_BACKSPACE, HInput::KEY_DELETE, HInput::KEY_LEFT,
                                HInput::KEY_RIGHT, HInput::KEY_HOME, HInput::KEY_END };
constexpr size_t kRepeatKeyCount = std::size(kRepeatKeys);
}

TextInputController::KeyEdges TextInputController::sampleEdges(IWindow& win) {
    // 키 엣지를 여기서 **한 번에** 계산하고 prev 상태를 조건 없이 갱신합니다. 예전에는
    // 숫자 필드 분기가 중간에서 return 해 F2·Delete 엣지가 갱신되지 않았고, 그 탓에 값을
    // 확정한 직후 눌린 채였던 Delete 가 선택 액터를 지웠습니다(undo 없음).
    const bool ctrlHeld = win.isCtrlDown();
    const bool vNow     = win.isKeyDown(HInput::KEY_V);
    const bool cNow     = win.isKeyDown(HInput::KEY_C);
    const bool xNow     = win.isKeyDown(HInput::KEY_X);
    const bool aNow     = win.isKeyDown(HInput::KEY_A);
    const bool f2Now    = win.isKeyDown(HInput::KEY_F2);
    const bool delNow   = win.isKeyDown(HInput::KEY_DELETE);
    const bool enNow    = win.isKeyDown(HInput::KEY_ENTER);
    // 반복 편집 키는 누르고 있으면 OS 자동 반복 간격으로 거듭 동작합니다(DEF-58, 글자 체계 T1 에서 화살표 · Home · End · Delete 로
    // 넓힘). 포커스가 바뀔 때 눌려 있던 키는 뗄 때까지 무시합니다(편집 칸에 들어오는 순간 남은 반복이 글을 지우지 않게).
    if (const EFocusTarget f = currentFocus(); f != backspaceFocus_) {
        backspaceFocus_ = f;
        blockedKeys_    = 0;
        for (size_t i = 0; i < kRepeatKeyCount; ++i)
            if (win.isKeyDown(kRepeatKeys[i])) blockedKeys_ |= 1u << i;
    }
    int counts[kRepeatKeyCount] = {};
    for (size_t i = 0; i < kRepeatKeyCount; ++i) {
        const uint32_t bit = 1u << i;
        if ((blockedKeys_ & bit) && !win.isKeyDown(kRepeatKeys[i])) blockedKeys_ &= ~bit;
        counts[i] = (blockedKeys_ & bit) ? 0 : win.keyPressCount(kRepeatKeys[i]);
    }
    const bool escNow = win.isKeyDown(HInput::KEY_ESCAPE);
    const bool upNow   = win.isKeyDown(HInput::KEY_UP);
    const bool downNow = win.isKeyDown(HInput::KEY_DOWN);

    KeyEdges k;
    k.ctrlHeld   = ctrlHeld;
    k.shiftHeld  = win.isShiftDown();
    k.pasteEdge  = ctrlHeld && vNow && !prevVDown_;
    k.copyEdge   = ctrlHeld && cNow && !prevCDown_;
    k.cutEdge    = ctrlHeld && xNow && !prevXDown_;
    k.selAllEdge = ctrlHeld && aNow && !prevADown_;
    k.f2Edge     = f2Now  && !prevF2Down_;
    k.delEdge    = delNow && !prevDeleteDown_;
    k.enterEdge  = enNow  && !prevEnterDown_;
    k.escEdge    = escNow && !prevEscapeDown_;
    k.bsCount    = counts[0];
    k.delCount   = counts[1];
    k.leftCount  = counts[2];
    k.rightCount = counts[3];
    k.homeCount  = counts[4];
    k.endCount   = counts[5];
    k.upEdge     = upNow && !prevUpDown_;
    k.downEdge   = downNow && !prevDownDown_;
    prevUpDown_   = upNow;
    prevDownDown_ = downNow;

    prevVDown_      = vNow;
    prevCDown_      = cNow;
    prevXDown_      = xNow;
    prevADown_      = aNow;
    prevF2Down_     = f2Now;
    prevDeleteDown_ = delNow;
    prevEnterDown_  = enNow;
    prevEscapeDown_ = escNow;
    return k;
}

bool TextInputController::applyEdit(HTextEditState& st, const std::vector<unsigned int>& chars, const KeyEdges& k,
                                    IWindow* clip) {
    bool dirty = false;
    const size_t c0 = st.caret, a0 = st.anchor;
    for (unsigned int cp : chars)
        if (st.insertCodepoint(cp)) dirty = true;   // 인쇄할 수 없는 글자(제어 문자)는 insertCodepoint 가 거름

    if (k.selAllEdge && !st.text.empty()) st.selectAll();
    if (k.copyEdge && clip && st.hasSelection()) clip->setClipboardText(st.selectedText());
    if (k.cutEdge && clip && st.hasSelection()) { clip->setClipboardText(st.cut()); dirty = true; }
    if (k.pasteEdge && clip && st.insert(clip->getClipboardText())) dirty = true;

    for (int i = 0; i < k.bsCount; ++i)  if (st.backspace(k.ctrlHeld)) dirty = true;
    for (int i = 0; i < k.delCount; ++i) if (st.del(k.ctrlHeld))       dirty = true;
    for (int i = 0; i < k.leftCount; ++i)  st.moveLeft(k.ctrlHeld, k.shiftHeld);
    for (int i = 0; i < k.rightCount; ++i) st.moveRight(k.ctrlHeld, k.shiftHeld);
    if (k.homeCount) st.home(k.shiftHeld);
    if (k.endCount)  st.end(k.shiftHeld);
    return dirty || st.caret != c0 || st.anchor != a0;
}

bool TextInputController::editField(IWindow& win, HTextEditState& st, const KeyEdges& k) {
    std::vector<unsigned int> chars;
    win.drainCharQueue(chars);
    // IME 조합 중에는 IME 가 편집 키를 먹습니다(GLFW 는 VK_PROCESSKEY 를 버림). 조합 중이거나 이번 프레임에 조합이 끝났으면
    // 키는 적용하지 않고 확정 글자만 넣어, 같은 키가 IME 와 칸에 두 번 먹히지 않게 합니다.
    std::string comp;
    size_t      compCaret = 0;
    const bool  composing = win.getComposition(comp, compCaret);
    const bool  wasComposing = st.isComposing();
    bool dirty = false;
    if (composing || wasComposing) {
        KeyEdges none;
        dirty = applyEdit(st, chars, none, &win);
    } else {
        dirty = applyEdit(st, chars, k, &win);
    }
    // IME 가 켜진 창만 조합 글을 맞춥니다. 숨은 창(덤프)은 IME 를 켜지 않아, 덤프가 흉내 낸 조합 글이 그대로 남습니다.
    if (!win.isTextInputEnabled()) return dirty;
    const std::string before = st.composition;
    if (composing) st.setComposition(comp, compCaret);
    else           st.clearComposition();
    return dirty || st.composition != before;
}

void TextInputController::syncIme(IWindow& win) {
    HTextEditState* st = nullptr;
    const EFocusTarget focus = currentFocus();
    if (const auto* fn = fieldFn(focus)) st = &(*fn)().edit;
    else if (focus == EFocusTarget::Dialog && deps_.dialogEdit) st = deps_.dialogEdit();
    if (st && st->caretRect.w > 0.f)
        win.setTextInputRect(st->caretRect.x, st->caretRect.y, st->caretRect.z, st->caretRect.w);
}

void TextInputController::handleModalInput(IWindow& win, EFocusTarget focus, const KeyEdges& k) {
    HTextEditState* st = (focus == EFocusTarget::Dialog && deps_.dialogEdit) ? deps_.dialogEdit() : nullptr;
    if (st) {
        editField(win, *st, k);
        if (st->isComposing()) return;   // 조합 중 Enter · Esc 는 IME 몫
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
    const auto* fn = fieldFn(focus);
    if (!fn) return;
    auto f = (*fn)();
    // 빈 칸에서 누른 Backspace(팝업: 분류 안에서 위로). 지울 글이 없을 때만입니다.
    const bool emptyBackspace = k.bsCount > 0 && f.edit.text.empty() && !f.edit.isComposing();
    const bool dirty = editField(win, f.edit, k);
    if (emptyBackspace && f.onBackspaceEmpty) f.onBackspaceEmpty();
    if (k.enterEdge && !f.edit.isComposing()) {
        if (f.onCommit) f.onCommit();
        if (!f.keepFocusOnCommit) setFocus(EFocusTarget::None);
        return;
    }
    if (f.onHistory && !f.edit.isComposing()) {
        if (k.upEdge)   f.onHistory(-1);
        if (k.downEdge) f.onHistory(+1);
    }
    if (dirty && f.onChanged) f.onChanged();
}

void TextInputController::tickInput() {
    if (!deps_.window || !*deps_.window) return;
    IWindow& win = **deps_.window;
    const KeyEdges k = sampleEdges(win);
    const EFocusTarget focus = currentFocus();
    syncIme(win);

    if (deps_.isModalOpen && deps_.isModalOpen()) {
        handleModalInput(win, focus, k);
        return;
    }

    // Esc — 무엇을 편집하고 있든 취소하고 빠져나옵니다(숫자 필드는 값을 확정).
    std::string compNow;
    size_t      compCaretNow = 0;
    const bool  imeComposing = win.getComposition(compNow, compCaretNow);
    if (k.escEdge && focus != EFocusTarget::None && !imeComposing) {
        std::function<void()> cancel;
        if (const auto* fn = fieldFn(focus)) cancel = (*fn)().onCancel;
        setFocus(EFocusTarget::None);
        if (cancel) cancel();
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

    if (isTextField(focus)) {
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
