#pragma once
// Add Component 팝업(2026-11-21). Unity 의 Add Component 창처럼 단추 아래에 열리고, 곧바로 검색 칸에 글을 칠 수
// 있습니다. 목록 계산은 ComponentPickerModel.hpp(화면 없이 시험), 이 클래스는 팝업 위젯 · 키 · 마우스를 맡습니다.
//  - ↑ · ↓ 로 고르고 Enter 로 붙이거나 분류에 들어갑니다. 빈 칸에서 Backspace 는 분류에서 위로, Esc · 바깥 누름은 닫기.
//  - 붙이기는 onAdd 콜백(DetailsPanel 이 Undo 와 함께)이 합니다.
// 팝업 자리는 EditorApp 루트 오버레이의 호스트이고, 다시 만들기는 프레임 끝(flushRebuild)으로 미룹니다(위젯 콜백이 자기
// 트리를 바로 비우지 않게).

#include "ComponentPickerModel.hpp"
#include "EditorContext.hpp"
#include "EditorUiKit.hpp"
#include "TextInputController.hpp"
#include "../ui/slate/HSlate.hpp"
#include "../ui/slate/HTextEdit.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Actor;

class ComponentPicker {
public:
    struct Deps {
        const EditorUiKit*                      ui  = nullptr;
        EditorContext*                          ctx = nullptr;
        std::function<void(EFocusTarget)>       setFocus;   // 검색 칸에 키보드 포커스를 주고 뺌
        std::function<void(Actor*, int)>        onAdd;      // 액터 · ComponentRegistry::entries() 자리
        std::function<std::shared_ptr<HWidget>()> anchorNow; // 지금의 Add Component 단추(디테일을 다시 만들어 단추가 바뀌었을 때)
    };

    void init(Deps deps) { deps_ = std::move(deps); }
    std::shared_ptr<SBorder> resetPopupHost();

    // anchor 단추 아래(자리가 없으면 위)에 엽니다. 같은 단추로 이미 열려 있으면 닫습니다(토글).
    void toggle(Actor* target, const std::shared_ptr<HWidget>& anchor);
    void open(Actor* target, const std::shared_ptr<HWidget>& anchor);
    void close();
    bool isOpen() const { return open_; }

    bool popupContains(glm::vec2 p) const;
    void handleOutsideClick(glm::vec2 p);   // 팝업 · 단추 밖이면 닫음(클릭은 흘려보냄)
    void flushRebuild();
    // 대상 액터가 사라졌거나(씬 전환 · 지움) 선택이 바뀌면 닫습니다. 매 프레임 부릅니다.
    void tick();

    TextInputField field();
    HTextEditState& edit() { return edit_; }

    // 덤프 · 시험용
    const std::vector<hcpick::Item>& items() const { return items_; }
    int highlighted() const { return hi_; }
    const std::string& category() const { return category_; }
    void moveHighlight(int dir);
    void activateHighlighted();
    void enterCategory(const std::string& c);
    void setQuery(const std::string& q) { edit_.setText(q); refresh(true); }

private:
    float s(float x) const { return deps_.ui->s(x); }
    Actor* target() const;
    void refresh(bool resetHighlight);   // 목록 다시 계산 → 목록 위젯 다시 만들기 예약
    void activate(int idx);
    void back();
    void rebuildPanel();
    void rebuildList();

    Deps deps_;
    HTextEditState edit_;
    bool focused_   = false;
    bool open_      = false;
    bool panelDirty_ = false;
    bool listDirty_  = false;
    uint64_t    targetId_ = 0;
    std::weak_ptr<HWidget> anchor_;
    std::string category_;
    std::string lastQuery_;   // 마지막으로 목록을 계산한 검색어
    std::vector<hcpick::Item> items_;
    int hi_ = -1;
    float scrollY_ = 0.f;
    bool  scrollReset_ = false;

    std::shared_ptr<SBorder>      host_;
    std::shared_ptr<HWidget>      panel_;
    std::shared_ptr<SSearchBox>   search_;
    std::shared_ptr<SScrollBox>   scroll_;
    std::shared_ptr<SVerticalBox> list_;
    std::shared_ptr<SBorder>      title_;
};
