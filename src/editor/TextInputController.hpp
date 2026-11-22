#pragma once
#include "../ui/slate/HSlate.hpp"
#include "../ui/slate/HTextEdit.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

class IWindow;

// Picker: 디테일의 Add Component 팝업 검색 칸 · ContentSearch: 콘텐츠 브라우저 검색 칸(2026-11-21).
// 글 칸 대상(Rename · Search · LogSearch · Console · Picker · ContentSearch · TextField)은 H1a-2 부터 "어느 칸인가"를
// HSlateContext 의 포커스 위젯으로 정합니다. 이 값은 그 위젯에 붙인 이름표일 뿐이고, 이름표로 부르는 곳(상태 바 ·
// 패널 감춤 · F2)이 있는 칸만 따로 이름을 가집니다. 새 글 칸은 TextField 로 등록하면 열거값을 더하지 않아도 됩니다.
// NumberField(SNumberField 의 정적 포커스)와 Dialog(모달)는 예전 방식 그대로입니다.
enum class EFocusTarget { None, Rename, Search, LogSearch, NumberField, Dialog, Console, Picker, ContentSearch, TextField };

// 텍스트 입력 칸의 공용 바인딩(S6d).
// 아웃라이너 리네임 · 아웃라이너 검색 · 로그 검색이 같은 구조로 TextInputController 에 등록됩니다.
// 글 · 커서 · 선택 · IME 조합 글은 HTextEditState 하나에 있습니다(글자 체계 T1, 2026-11-19). 예전의 버퍼 + "전체 선택" 깃발을 대신합니다.
struct TextInputField {
    HTextEditState&                edit;
    bool&                          focused;
    std::function<bool(glm::vec2)> contains;
    std::function<void()>          onChanged;
    std::function<bool()>          onBegin  = {};   // false 이면 진입을 취소합니다
    std::function<void()>          onCommit = {};   // Enter 확정 시 호출
    bool                           keepFocusOnCommit = false;   // 콘솔: Enter 뒤에도 입력을 이어 감
    std::function<void(int)>       onHistory = {};   // 위(-1) · 아래(+1) 화살표(콘솔 기록 · 팝업 목록 고르기)
    std::function<void()>          onCancel  = {};   // Esc 로 빠져나올 때(팝업 닫기). 포커스를 푼 뒤 부릅니다
    std::function<void()>          onBackspaceEmpty = {};   // 빈 칸에서 Backspace(팝업: 분류에서 위로)
    // Esc 로 빠져나오기 직전(포커스를 풀기 전, H3b). 포커스를 잃으면 확정하는 칸(디테일 글 칸 · 색 16진수 칸)이 이번 나감은 취소임을
    // 알아 둡니다. 포커스를 푼 뒤에는 onCancel 이 불립니다.
    std::function<void()>          onEscape = {};
    bool                           selectAllOnFocus = true;   // 거짓이면 커서를 끝에 둠(콘솔: 이어 쓰기)
};

// 글 칸 하나의 등록(H1a-2). widget 은 패널이 계속 들고 있는 포커스 위젯(SSearchBox · SEditableText, focusable(true))이고,
// 패널이 다시 만들어도 같은 객체여야 합니다(HReuse). field 는 편집 상태 · 콜백 묶음입니다. kind 는 이름표입니다.
// field().focused 는 그리기 · 패널 판정용 거울입니다. 패널이 이 값을 직접 끄면(액터 삭제 · 행 누름 · 팝업 호스트 재설정)
// 예전처럼 부수 효과 없이 포커스를 놓은 것으로 봅니다.
struct TextFieldBinding {
    EFocusTarget                              kind = EFocusTarget::TextField;
    std::function<std::shared_ptr<HWidget>()> widget;
    std::function<TextInputField()>           field;
};

// 에디터 텍스트 입력 포커스 중재 및 키/문자 입력 처리기(S6d).
// EditorApp* 를 들지 않고 좁은 콜백과 TextInputField 바인딩으로만 동작합니다.
class TextInputController {
public:
    struct Deps {
        IWindow* const* window = nullptr;

        std::function<bool(glm::vec2)> detailsScrollContains;

        std::function<bool()> isModalOpen;
        std::function<bool()> isDialogFocused;
        std::function<void()> beginDialogFocus;
        std::function<void()> endDialogFocus;
        std::function<HTextEditState*()> dialogEdit;
        std::function<void()> onModalDefault;
        std::function<void()> onModalCancel;

        std::function<void()> onCloseMenus;
        std::function<void()> onShowOutliner;
        std::function<bool()> isEditDragActive;
        std::function<void()> onDeleteSelection;
        std::function<void()> onCopySelection;
        std::function<void()> onPasteClipboard;
        // 글 칸 포커스의 정본(H1a-2). 이 문맥의 포커스 위젯이 어느 칸인지를 정합니다. null 이면 글 칸 포커스를 쓰지 않습니다.
        HSlateContext* slate = nullptr;
        // 글 칸 목록(아웃라이너 이름 바꾸기 · 검색, 로그 검색 · 콘솔, Add Component 팝업, 콘텐츠 검색 …).
        std::vector<TextFieldBinding> textFields;

        // ── 여러 OS 창(M3b) ── 비어 있으면 창 하나(window)로 예전과 같이 동작합니다. 창 번호는 0 = 메인, 1.. = 떠 있는 창입니다.
        // 키 · 글자 · 편집 키는 키보드 창(OS 포커스가 있는 창, 없으면 메인)에서 읽고, 포커스인 칸이 다른 창에 있으면 받지 않습니다.
        // IME 는 포커스인 칸이 있는 창에서만 켜고, 후보 창 자리도 그 창 기준입니다.
        std::function<int()>              keyboardWindow;   // 지금 키보드 창
        std::function<int(EFocusTarget)>  windowOfFocus;    // 그 대상이 있는 창(대화상자 · 없음은 0)
        std::function<IWindow*(int)>      windowById;       // 없는 번호면 null
        std::function<std::vector<int>()> windowIds;        // IME 를 맞출 창 목록(메인 포함)
        // 열린 메뉴가 키보드를 쥐고 있는가(H2b). 참이면 키 엣지만 갱신하고 글자는 버립니다(Delete · Ctrl+C · Ctrl+V · F2 · Esc 가 새지 않게).
        std::function<bool()>             isKeyboardCaptured;
    };

    // 글 입력 칸 대상인지(숫자 칸 · 대화상자 제외).
    static bool isTextField(EFocusTarget t) {
        return t == EFocusTarget::Rename || t == EFocusTarget::Search || t == EFocusTarget::LogSearch ||
               t == EFocusTarget::Console || t == EFocusTarget::Picker || t == EFocusTarget::ContentSearch ||
               t == EFocusTarget::TextField;
    }

    // deps.slate 에 포커스 훅을 겁니다. 문맥이 포커스를 바꿀 수 있는 동안 컨트롤러가 살아 있어야 합니다(EditorApp 은
    // 컨트롤러를 HSlateRenderer 보다 먼저 선언해, 문맥이 먼저 파괴됩니다. 문맥의 소멸자는 훅을 부르지 않습니다).
    void init(Deps deps);

    // 대화상자 → 숫자 칸 → (HSlate 포커스 위젯 → 등록된 글 칸의 이름표) 순서입니다. 패널이 거울 값을 끈 칸은 None.
    EFocusTarget currentFocus() const;
    // 이름표로 포커스를 옮깁니다(같은 이름표가 여럿이면 처음 등록한 칸). None · Dialog 도 받습니다.
    void setFocus(EFocusTarget t);
    // 위젯으로 포커스를 옮깁니다(이름표 없는 새 글 칸용). 등록되지 않은 위젯이면 아무것도 하지 않고 거짓.
    bool setFocusWidget(const std::shared_ptr<HWidget>& w);
    // 그 칸의 포커스를 부수 효과 없이 놓습니다(M3a, H1a-2 판단 2). 패널이 거울 값(renaming_ · focused_)을 직접 끄던 자리(액터 삭제 ·
    // 행 누름 · 팝업 호스트 재설정)가 부릅니다. 거울 값을 끄고 HSlate 포커스가 그 칸이면 조용히 비웁니다. setFocus(None) 과 달리 글 칸
    // 상태 · 문자 큐 · 키 엣지 · IME · onChanged 를 건드리지 않습니다 — 예전 동작(거울 값만 끔)과 같게 하려는 것이고, IME 를 끌지는
    // 사용자 결정으로 남아 있습니다(H1a-2 보고).
    void releaseQuiet(EFocusTarget t);
    // 위젯으로 찾는 releaseQuiet(H3b). 이름표 없는 글 칸(TextField)이 여럿이라 이름표로는 처음 칸만 찾습니다.
    void releaseQuietWidget(const std::shared_ptr<HWidget>& w);
    // 지금 포커스인 글 칸의 편집 상태(없으면 null). 대화상자는 포함하지 않습니다.
    HTextEditState* focusedTextEdit() const;
    // 루트 교체(uiScale · 문화권 변경의 buildWidgetTree) 동안 글 칸 포커스를 그대로 둡니다. HSlateContext::setRootWidget 은
    // 루트가 바뀌면 포커스를 풀지만, 예전 에디터는 패널의 거울 값이 남아 포커스가 이어졌으므로 그 동작을 지킵니다
    // (확정 · 취소 · 문자 큐 비우기 · IME 변화 없음). swap 안에서 일어난 포커스 변경 알림은 무시합니다.
    void preserveFocusAcross(const std::function<void()>& swap);
    // (x, y) 가 지금 포커스인 칸 위인지. winId 는 그 좌표의 창이고, 칸이 다른 창에 있으면 거짓입니다(M3b: 다른 창을 누르면 편집 끝).
    bool focusWidgetContains(int winId, float x, float y) const;
    bool focusWidgetContains(float x, float y) const { return focusWidgetContains(0, x, y); }
    void tickInput();
    // 지금 포커스인 대상이 있는 창(없으면 0).
    int  focusWindow() const { return windowOfTarget(currentFocus()); }

private:
public:
    // 편집 키 한 프레임 묶음. 반복 수는 OS 자동 반복을 따릅니다(DEF-58). 시험이 직접 만들어 applyEdit 에 넘기기도 합니다.
    struct KeyEdges {
        bool ctrlHeld   = false;
        bool shiftHeld  = false;
        bool pasteEdge  = false;
        bool copyEdge   = false;
        bool cutEdge    = false;
        bool selAllEdge = false;
        bool f2Edge     = false;
        bool delEdge    = false;
        bool enterEdge  = false;
        bool escEdge    = false;
        int  bsCount    = 0;
        int  delCount   = 0;
        int  leftCount  = 0;
        int  rightCount = 0;
        int  homeCount  = 0;
        int  endCount   = 0;
        bool upEdge     = false;
        bool downEdge   = false;
    };
    // 한 편집 칸에 글자 · 키 · 클립보드를 적용합니다(창 없이 시험 가능). 글이 바뀌었으면 참.
    static bool applyEdit(HTextEditState& st, const std::vector<unsigned int>& chars, const KeyEdges& k,
                          IWindow* clipboard);

private:
    // 등록 목록에서 찾기. 못 찾으면 -1 입니다.
    int bindingOf(EFocusTarget t) const;
    int bindingOfWidget(const HWidget* w) const;
    // HSlate 포커스 위젯이 가리키는 글 칸(패널이 거울 값을 껐으면 -1).
    int focusedBinding() const;
    TextInputField fieldAt(int i) const { return deps_.textFields[(size_t)i].field(); }
    // 포커스 전환의 본체. prev · pi 는 바뀌기 전, t · ti 는 바뀐 뒤입니다. driveSlate 가 참이면 HSlate 포커스도 여기서 옮기고
    // (setFocus 경로), 거짓이면 HSlate 쪽은 이미 바뀌었습니다(포커스 훅 경로).
    void transition(EFocusTarget prev, int pi, EFocusTarget t, int ti, bool driveSlate);
    void onSlateFocusChanged(const std::shared_ptr<HWidget>& prev, const std::shared_ptr<HWidget>& next);
    void slateFocus(int ti);
    void slateClear();
    KeyEdges sampleEdges(IWindow& win);
    bool editField(IWindow& win, HTextEditState& st, const KeyEdges& k);
    void syncIme(IWindow& win);
    void handleModalInput(IWindow& win, EFocusTarget focus, const KeyEdges& k);
    void handleNumberFieldInput(IWindow& win, const KeyEdges& k);
    void handleTextFieldInput(IWindow& win, EFocusTarget focus, const KeyEdges& k);
    int      keyboardWindowId() const { return deps_.keyboardWindow ? deps_.keyboardWindow() : 0; }
    int      windowOfTarget(EFocusTarget t) const {
        return (t == EFocusTarget::None || !deps_.windowOfFocus) ? 0 : deps_.windowOfFocus(t);
    }
    IWindow* windowAt(int id) const;
    // 키보드 창이 바뀌면 새 창의 지금 키 상태로 엣지를 다시 심고, 눌려 있던 반복 키는 뗄 때까지 막습니다.
    void     reseedEdges(IWindow& win);

    Deps deps_;
    // 컨트롤러가 HSlate 포커스를 옮기는 동안(또는 루트 교체 동안) 0 보다 큽니다. 그동안의 포커스 훅은 무시합니다.
    int          silent_           = 0;
    EFocusTarget backspaceFocus_   = EFocusTarget::None;
    int          backspaceBinding_ = -1;
    // 포커스가 바뀔 때 눌려 있던 편집 키는 뗄 때까지 무시합니다(Backspace 로 시작한 DEF-58 규칙을 모든 반복 키로).
    uint32_t     blockedKeys_      = 0;
    bool         prevEnterDown_    = false;
    bool         prevF2Down_       = false;
    bool         prevDeleteDown_   = false;
    bool         prevVDown_        = false;
    bool         prevCDown_        = false;
    bool         prevADown_        = false;
    bool         prevXDown_        = false;
    bool         prevEscapeDown_   = false;
    bool         prevUpDown_       = false;
    bool         prevDownDown_     = false;
    int          lastKeyboardWin_  = 0;
};
