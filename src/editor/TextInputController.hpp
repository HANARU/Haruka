#pragma once
#include "../ui/slate/HSlate.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <string>

class IWindow;

enum class EFocusTarget { None, Rename, Search, LogSearch, NumberField, Dialog };

// 텍스트 입력 칸의 공용 바인딩(S6d).
// 아웃라이너 리네임 · 아웃라이너 검색 · 로그 검색이 같은 구조로 TextInputController 에 등록됩니다.
struct TextInputField {
    std::string&                   buffer;
    bool&                          focused;
    bool&                          selectedAll;
    std::function<bool(glm::vec2)> contains;
    std::function<void()>          onChanged;
    std::function<bool()>          onBegin  = {};   // false 이면 진입을 취소합니다
    std::function<void()>          onCommit = {};   // Enter 확정 시 호출
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
        std::function<void(std::string*&, bool*&)> dialogBuffer;
        std::function<void()> onModalDefault;
        std::function<void()> onModalCancel;

        std::function<void()> onCloseMenus;
        std::function<void()> onShowOutliner;
        std::function<bool()> isEditDragActive;
        std::function<void()> onDeleteSelection;
        std::function<void()> onCopySelection;
        std::function<void()> onPasteClipboard;
    };

    void init(Deps deps);

    EFocusTarget currentFocus() const;
    void setFocus(EFocusTarget t);
    bool focusWidgetContains(float x, float y) const;
    void tickInput();

private:
    struct KeyEdges {
        bool ctrlHeld   = false;
        bool pasteEdge  = false;
        bool copyEdge   = false;
        bool selAllEdge = false;
        bool f2Edge     = false;
        bool delEdge    = false;
        bool enterEdge  = false;
        bool escEdge    = false;
        int  bsCount    = 0;
    };

    KeyEdges sampleEdges(IWindow& win);
    bool editBuffer(IWindow& win, std::string& buf, bool& selAll, const KeyEdges& k);
    void handleModalInput(IWindow& win, EFocusTarget focus, const KeyEdges& k);
    void handleNumberFieldInput(IWindow& win, const KeyEdges& k);
    void handleTextFieldInput(IWindow& win, EFocusTarget focus, const KeyEdges& k);

    Deps deps_;
    EFocusTarget backspaceFocus_   = EFocusTarget::None;
    bool         backspaceBlocked_ = false;
    bool         prevEnterDown_    = false;
    bool         prevF2Down_       = false;
    bool         prevDeleteDown_   = false;
    bool         prevVDown_        = false;
    bool         prevCDown_        = false;
    bool         prevADown_        = false;
    bool         prevEscapeDown_   = false;
};
