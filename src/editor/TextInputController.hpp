#pragma once
#include "../ui/slate/HSlate.hpp"
#include "../ui/slate/HTextEdit.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

class IWindow;

// Picker: 디테일의 Add Component 팝업 검색 칸 · ContentSearch: 콘텐츠 브라우저 검색 칸(2026-11-21).
enum class EFocusTarget { None, Rename, Search, LogSearch, NumberField, Dialog, Console, Picker, ContentSearch };

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
};

// 에디터 텍스트 입력 포커스 중재 및 키/문자 입력 처리기(S6d).
// EditorApp* 를 들지 않고 좁은 콜백과 TextInputField 바인딩으로만 동작합니다.
class TextInputController {
public:
    struct Deps {
        IWindow* const* window = nullptr;
        std::function<TextInputField()> renameField;
        std::function<TextInputField()> searchField;
        std::function<TextInputField()> logSearchField;

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
        std::function<TextInputField()> consoleField;   // Output Log 의 콘솔 줄(2026-11-20, Docs/Gameplay_Tools.md D9)
        std::function<TextInputField()> pickerField;          // Add Component 팝업(2026-11-21)
        std::function<TextInputField()> contentSearchField;   // 콘텐츠 브라우저 검색(2026-11-21)
    };

    // 글 입력 칸 대상인지(숫자 칸 · 대화상자 제외).
    static bool isTextField(EFocusTarget t) {
        return t == EFocusTarget::Rename || t == EFocusTarget::Search || t == EFocusTarget::LogSearch ||
               t == EFocusTarget::Console || t == EFocusTarget::Picker || t == EFocusTarget::ContentSearch;
    }

    void init(Deps deps);

    EFocusTarget currentFocus() const;
    void setFocus(EFocusTarget t);
    bool focusWidgetContains(float x, float y) const;
    void tickInput();

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
    // 대상의 바인딩. 글 입력 칸이 아니거나 등록되지 않았으면 null 입니다. 대상별 분기를 여기 한 곳에 둡니다.
    const std::function<TextInputField()>* fieldFn(EFocusTarget t) const;
    KeyEdges sampleEdges(IWindow& win);
    bool editField(IWindow& win, HTextEditState& st, const KeyEdges& k);
    void syncIme(IWindow& win);
    void handleModalInput(IWindow& win, EFocusTarget focus, const KeyEdges& k);
    void handleNumberFieldInput(IWindow& win, const KeyEdges& k);
    void handleTextFieldInput(IWindow& win, EFocusTarget focus, const KeyEdges& k);

    Deps deps_;
    EFocusTarget backspaceFocus_   = EFocusTarget::None;
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
};
