#include "TextInputController.hpp"
#include "../input/HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include <iterator>
#include <vector>

void TextInputController::init(Deps deps) {
    deps_ = std::move(deps);
    // 포커스 변경을 알아야 IME · 글 칸 상태를 맞출 수 있습니다(ui 는 platform 을 모름). 에디터는 누르면 포커스를 끄고
    // 포커스를 이 컨트롤러로만 옮기므로, 훅이 일하는 것은 바깥에서 HSlate 포커스를 직접 옮긴 경우뿐입니다.
    if (deps_.slate)
        deps_.slate->setFocusHook([this](const std::shared_ptr<HWidget>& prev, const std::shared_ptr<HWidget>& next) {
            onSlateFocusChanged(prev, next);
        });
}

int TextInputController::bindingOf(EFocusTarget t) const {
    for (size_t i = 0; i < deps_.textFields.size(); ++i) {
        const TextFieldBinding& b = deps_.textFields[i];
        if (b.kind == t && b.widget && b.field) return (int)i;
    }
    return -1;
}

int TextInputController::bindingOfWidget(const HWidget* w) const {
    if (!w) return -1;
    for (size_t i = 0; i < deps_.textFields.size(); ++i) {
        const TextFieldBinding& b = deps_.textFields[i];
        if (b.widget && b.field && b.widget().get() == w) return (int)i;
    }
    return -1;
}

int TextInputController::focusedBinding() const {
    if (!deps_.slate) return -1;
    const auto w = deps_.slate->focusedWidget();
    const int  i = bindingOfWidget(w.get());
    // 패널이 거울 값을 직접 끈 칸(액터 삭제 · 행 누름 · 팝업 호스트 재설정)은 포커스를 놓은 것으로 봅니다. 예전에도 그 값만
    // 끄면 곧바로 "포커스 없음"이었고 부수 효과(IME · 문자 큐)는 없었습니다. 남은 HSlate 포커스는 다음 전환이 덮어씁니다.
    if (i < 0 || !fieldAt(i).focused) return -1;
    return i;
}

EFocusTarget TextInputController::currentFocus() const {
    if (deps_.isDialogFocused && deps_.isDialogFocused()) return EFocusTarget::Dialog;
    if (SNumberField::focusedField())                     return EFocusTarget::NumberField;
    const int i = focusedBinding();
    return i < 0 ? EFocusTarget::None : deps_.textFields[(size_t)i].kind;
}

HTextEditState* TextInputController::focusedTextEdit() const {
    if (!isTextField(currentFocus())) return nullptr;
    const int i = focusedBinding();
    return i < 0 ? nullptr : &fieldAt(i).edit;
}

void TextInputController::slateFocus(int ti) {
    if (!deps_.slate) return;
    ++silent_;
    if (auto w = deps_.textFields[(size_t)ti].widget()) deps_.slate->setFocus(w);
    --silent_;
}

void TextInputController::slateClear() {
    if (!deps_.slate) return;
    ++silent_;
    deps_.slate->clearFocus();
    --silent_;
}

void TextInputController::setFocus(EFocusTarget t) {
    const int ti = isTextField(t) ? bindingOf(t) : -1;
    transition(currentFocus(), focusedBinding(), t, ti, true);
}

bool TextInputController::setFocusWidget(const std::shared_ptr<HWidget>& w) {
    const int ti = bindingOfWidget(w.get());
    if (ti < 0) return false;
    transition(currentFocus(), focusedBinding(), deps_.textFields[(size_t)ti].kind, ti, true);
    return true;
}

void TextInputController::releaseQuiet(EFocusTarget t) {
    const int i = bindingOf(t);
    if (i < 0) return;
    fieldAt(i).focused = false;   // 예전에 패널이 직접 하던 일(거울 값만 끔)
    // 정본(HSlate 포커스)도 맞춥니다. 예전에는 남은 HSlate 포커스를 다음 전환이 덮어썼는데, 그 사이에는 거울 값이 거부권을 가졌습니다.
    if (deps_.slate) {
        const auto w = deps_.textFields[(size_t)i].widget();
        if (w && deps_.slate->focusedWidget() == w) slateClear();
    }
}

void TextInputController::releaseQuietWidget(const std::shared_ptr<HWidget>& w) {
    const int i = bindingOfWidget(w.get());
    if (i < 0) return;
    fieldAt(i).focused = false;
    if (deps_.slate && w && deps_.slate->focusedWidget() == w) slateClear();
}

void TextInputController::preserveFocusAcross(const std::function<void()>& swap) {
    const int keep = focusedBinding();
    ++silent_;
    if (swap) swap();
    // 같은 위젯을 다시 포커스로 둡니다(패널이 HReuse 로 같은 객체를 씀). 글 칸 상태 · IME · 문자 큐는 건드리지 않습니다.
    if (keep >= 0 && deps_.slate)
        if (auto w = deps_.textFields[(size_t)keep].widget()) deps_.slate->setFocus(w);
    --silent_;
}

void TextInputController::onSlateFocusChanged(const std::shared_ptr<HWidget>& prevW, const std::shared_ptr<HWidget>& nextW) {
    if (silent_ > 0) return;
    // HSlate 포커스는 이미 바뀌었습니다. 바뀌기 전의 대상은 이전 위젯과 대화상자 · 숫자 칸 상태로 다시 구합니다.
    int pi = bindingOfWidget(prevW.get());
    if (pi >= 0 && !fieldAt(pi).focused) pi = -1;
    EFocusTarget prev = pi >= 0 ? deps_.textFields[(size_t)pi].kind : EFocusTarget::None;
    if (deps_.isDialogFocused && deps_.isDialogFocused()) { prev = EFocusTarget::Dialog; pi = -1; }
    else if (SNumberField::focusedField())                { prev = EFocusTarget::NumberField; pi = -1; }
    const int          ti = bindingOfWidget(nextW.get());
    const EFocusTarget t  = ti >= 0 ? deps_.textFields[(size_t)ti].kind : EFocusTarget::None;
    transition(prev, pi, t, ti, false);
}

void TextInputController::transition(EFocusTarget prev, int pi, EFocusTarget t, int ti, bool driveSlate) {
    if (prev == t && pi == ti) return;

    // 이전 포커스를 정리합니다. 숫자 필드는 편집 중이던 값을 확정해야, 나중에 다른 곳에서
    // 누른 Enter 가 낡은 값으로 되살아나지 않습니다.
    if (prev == EFocusTarget::NumberField) {
        if (auto* nf = SNumberField::focusedField()) nf->commitEdit();
    } else if (pi >= 0) {
        auto f = fieldAt(pi);
        f.focused = false;
        f.edit.clearSelection();
        f.edit.clearComposition();
        if (f.onChanged) f.onChanged();
    } else if (prev == EFocusTarget::Dialog) {
        if (deps_.endDialogFocus) deps_.endDialogFocus();
    }

    bool entered = false;
    if (t == EFocusTarget::Dialog) {
        if (deps_.beginDialogFocus) deps_.beginDialogFocus();
    } else if (ti >= 0) {
        auto f = fieldAt(ti);
        // onBegin 이 거절하면 들어가지 않습니다(이름 바꾸기: 선택이 없거나 에디터 카메라).
        if (!f.onBegin || f.onBegin()) {
            f.focused = true;
            if (f.selectAllOnFocus) f.edit.selectAll();   // 키보드로 들어오면 전체 선택(마우스로 누르면 위젯이 곧바로 그 자리에 커서를 놓음)
            else                    f.edit.end(false);    // 콘솔은 이어 쓰기(전체 선택 없음)
            entered = true;
        }
        // 정본(HSlate 포커스)을 onChanged 보다 먼저 맞춥니다. 예전에는 거울 값이 곧 정본이라 onChanged 때 이미 새 칸이었습니다.
        if (entered && driveSlate) slateFocus(ti);
        if (!entered) slateClear();   // 거절: 바깥에서 옮긴 포커스도 되돌림
        if (f.onChanged) f.onChanged();
    }
    if (!entered && driveSlate) slateClear();   // None · Dialog · 거절: 글 칸 포커스를 놓음

    if (IWindow* kw = windowAt(keyboardWindowId())) {
        IWindow& w = *kw;
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
    }
    // 글 입력 칸에서만 IME 를 켭니다(T2). 숫자 칸 · 포커스 없음은 끔. 여러 창이면 그 칸이 있는 창만 켭니다(M3b).
    {
        const bool textFocus = isTextField(t) || t == EFocusTarget::Dialog;
        const int  fieldWin  = textFocus ? windowOfTarget(t) : -1;
        if (deps_.windowIds) {
            for (int id : deps_.windowIds())
                if (IWindow* w = windowAt(id)) w->setTextInputEnabled(textFocus && id == fieldWin);
        } else if (IWindow* w = windowAt(0)) {
            w->setTextInputEnabled(textFocus);
        }
    }

    // 텍스트 입력을 시작하면 열려 있던 메뉴는 닫습니다.
    if (t != EFocusTarget::None && deps_.onCloseMenus)
        deps_.onCloseMenus();
}

IWindow* TextInputController::windowAt(int id) const {
    if (deps_.windowById)
        if (IWindow* w = deps_.windowById(id)) return w;
    // 번호를 모르면(창이 방금 닫힘 등) 메인으로 돌아갑니다.
    return (deps_.window && *deps_.window) ? *deps_.window : nullptr;
}

bool TextInputController::focusWidgetContains(int winId, float x, float y) const {
    const glm::vec2 p{ x, y };
    const EFocusTarget focus = currentFocus();
    // 다른 창의 좌표는 그 칸의 위젯 좌표와 비교할 수 없습니다(창마다 원점이 다름). 다른 창을 누른 것은 칸 밖입니다(M3b).
    if (focus != EFocusTarget::None && windowOfTarget(focus) != winId) return false;
    if (focus == EFocusTarget::NumberField) {
        // 숫자 칸은 디테일 스크롤 상자 안에만 있습니다. 스크롤로 가려진 칸은 그 자리가 다른 패널이므로,
        // 상자 영역 밖의 누름은 칸 밖으로 칩니다. 아니면 가려진 칸 위를 눌러도 편집이 끝나지 않습니다.
        if (auto* nf = SNumberField::focusedField())
            return nf->cachedGeometry().contains(p) &&
                   (!deps_.detailsScrollContains || deps_.detailsScrollContains(p));
        return false;
    }
    if (isTextField(focus))
        if (const int i = focusedBinding(); i >= 0) return fieldAt(i).contains(p);
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
    // 이름표 없는 글 칸끼리 옮길 때도 알아보도록 등록 번호까지 봅니다(H1a-2).
    if (const EFocusTarget f = currentFocus(); f != backspaceFocus_ || focusedBinding() != backspaceBinding_) {
        backspaceFocus_   = f;
        backspaceBinding_ = focusedBinding();
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
    HTextEditState* st = focusedTextEdit();
    if (!st && currentFocus() == EFocusTarget::Dialog && deps_.dialogEdit) st = deps_.dialogEdit();
    if (!st || st->caretRect.w <= 0.f) return;
    // 후보 창 자리는 그 칸이 있는 창 기준입니다(칸의 커서 사각형이 그 창 문맥 좌표, M3b). 창 하나면 win 과 같습니다.
    IWindow* fw = deps_.windowById ? windowAt(windowOfTarget(currentFocus())) : &win;
    if (fw) fw->setTextInputRect(st->caretRect.x, st->caretRect.y, st->caretRect.z, st->caretRect.w);
}

void TextInputController::reseedEdges(IWindow& win) {
    prevEnterDown_  = win.isKeyDown(HInput::KEY_ENTER);
    prevF2Down_     = win.isKeyDown(HInput::KEY_F2);
    prevDeleteDown_ = win.isKeyDown(HInput::KEY_DELETE);
    prevVDown_      = win.isKeyDown(HInput::KEY_V);
    prevCDown_      = win.isKeyDown(HInput::KEY_C);
    prevADown_      = win.isKeyDown(HInput::KEY_A);
    prevXDown_      = win.isKeyDown(HInput::KEY_X);
    prevEscapeDown_ = win.isKeyDown(HInput::KEY_ESCAPE);
    prevUpDown_     = win.isKeyDown(HInput::KEY_UP);
    prevDownDown_   = win.isKeyDown(HInput::KEY_DOWN);
    blockedKeys_    = 0;
    for (size_t i = 0; i < kRepeatKeyCount; ++i)
        if (win.isKeyDown(kRepeatKeys[i])) blockedKeys_ |= 1u << i;
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
    if (!isTextField(focus)) return;
    const int i = focusedBinding();
    if (i < 0) return;
    auto f = fieldAt(i);
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
    // 키보드 창(M3b): OS 포커스가 있는 창. 창 하나면 언제나 메인입니다.
    const int kb = keyboardWindowId();
    IWindow* kw = windowAt(kb);
    if (!kw) return;
    IWindow& win = *kw;
    if (kb != lastKeyboardWin_) {
        // 다른 창의 키 상태와 이어 보면 엣지가 거짓으로 잡힙니다(예: 메인에서 누른 Enter 가 떠 있는 창에서 다시 눌림으로).
        lastKeyboardWin_ = kb;
        reseedEdges(win);
    }
    const KeyEdges k = sampleEdges(win);
    const EFocusTarget focus = currentFocus();
    syncIme(win);

    // 열린 메뉴(상단 드롭다운 · 오른쪽 메뉴)가 키보드를 쥐고 있으면(H2b) 이번 프레임 키는 메뉴 몫입니다. 엣지는 위에서 갱신했으므로
    // 메뉴가 닫힌 다음 프레임에 같은 키가 다시 눌린 것으로 잡히지 않습니다.
    if (deps_.isKeyboardCaptured && deps_.isKeyboardCaptured()) {
        std::vector<unsigned int> junk;
        win.drainCharQueue(junk);
        return;
    }

    if (deps_.isModalOpen && deps_.isModalOpen()) {
        // 대화상자는 메인에 뜨고 떠 있는 창의 입력은 받지 않습니다(D5). 떠 있는 창의 글자는 버립니다.
        if (kb != 0) {
            std::vector<unsigned int> junk;
            win.drainCharQueue(junk);
            return;
        }
        handleModalInput(win, focus, k);
        return;
    }

    // 포커스인 칸이 키보드 창에 없으면 그 칸은 키를 받지 않습니다(M3b). 다른 창에서 친 글자 · 편집 키 · Esc · Enter 는 버립니다.
    if (focus != EFocusTarget::None && windowOfTarget(focus) != kb) {
        std::vector<unsigned int> junk;
        win.drainCharQueue(junk);
        return;
    }

    // Esc — 무엇을 편집하고 있든 취소하고 빠져나옵니다(숫자 필드는 값을 확정).
    std::string compNow;
    size_t      compCaretNow = 0;
    const bool  imeComposing = win.getComposition(compNow, compCaretNow);
    if (k.escEdge && focus != EFocusTarget::None && !imeComposing) {
        std::function<void()> cancel;
        if (const int i = focusedBinding(); i >= 0 && isTextField(focus)) {
            const TextInputField f = fieldAt(i);
            cancel = f.onCancel;
            if (f.onEscape) f.onEscape();   // 포커스를 잃으면 확정하는 칸: 이번 나감은 취소(H3b)
        }
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
