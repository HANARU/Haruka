#pragma once
// 오른쪽 누름 메뉴(2026-11-21, 콘텐츠 브라우저가 처음 씀). 누른 자리에 항목 목록을 띄우고, 항목을 누르면 닫은 뒤 그 동작을
// 부릅니다. 바깥을 누르거나 Esc 면 닫습니다. 팝업 자리는 EditorApp 루트 오버레이의 호스트이고, 다시 만들기는 프레임 끝으로
// 미룹니다(로그 Categories 팝업 · Add Component 팝업과 같은 규칙).

#include "EditorUiKit.hpp"
#include "../ui/slate/HSlate.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class EditorContextMenu {
public:
    struct Item {
        std::string           label;
        std::function<void()> action;
        bool                  enabled   = true;
        bool                  separator = false;   // 참이면 이 항목 위에 구분선
    };

    void init(const EditorUiKit* ui) { ui_ = ui; }
    std::shared_ptr<SBorder> resetPopupHost();

    void open(glm::vec2 at, std::vector<Item> items);
    void close();
    bool isOpen() const { return open_; }
    bool popupContains(glm::vec2 p) const;
    void handleOutsideClick(glm::vec2 p);
    void flushRebuild();

    const std::vector<Item>& items() const { return items_; }
    // 덤프 · 시험: 이름이 같은 항목을 누른 것과 같습니다. 없거나 꺼져 있으면 false.
    bool invoke(const std::string& label);

private:
    void rebuild();

    const EditorUiKit*       ui_ = nullptr;
    std::shared_ptr<SBorder> host_;
    std::shared_ptr<HWidget> panel_;
    std::vector<Item>        items_;
    glm::vec2                at_{ 0.f };
    bool                     open_  = false;
    bool                     dirty_ = false;
};
