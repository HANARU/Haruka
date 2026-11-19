#pragma once
#include "EditorUiKit.hpp"
#include "../ui/slate/HSlate.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// 상단 메뉴 바 · 드롭다운 팝업(S6e).
// 메뉴 항목 정의 목록은 EditorApp::buildMenus() 가 채워 setDefs() 로 넘깁니다.
class EditorMenus {
public:
    struct Item {
        std::string           label;      // 지금 문화권으로 푼 글(글자 체계 T3: HTEXT(...).toString())
        std::function<void()> action;
        std::function<bool()> checked;    // 비어 있지 않으면 앞쪽에 체크 박스를 그립니다
        std::string           shortcut;   // 오른쪽 열의 단축키(번역하지 않음). 예전에는 label 에 공백으로 줄 맞춰 붙였음
    };
    struct Def {
        std::string       name;
        std::vector<Item> items;
        float             x = 0.f;
    };

    void init(const EditorUiKit* ui, float menuBarH);
    std::shared_ptr<SHorizontalBox> buildBar();
    std::shared_ptr<SBorder>        resetPopupHost();

    void setDefs(std::vector<Def> defs);
    void rebuildBar();
    void rebuildPopup();
    void flushRebuilds();

    bool isOpen() const { return openMenu_ >= 0; }
    void close();
    bool openByName(const std::string& name);
    void handleHoverSwitch(glm::vec2 mousePos);

private:
    float s(float x) const { return ui_->s(x); }

    const EditorUiKit* ui_       = nullptr;
    float              menuBarH_ = 26.f;

    std::vector<Def>                   defs_;
    int                                openMenu_   = -1;
    bool                               barDirty_   = false;
    bool                               popupDirty_ = false;
    std::shared_ptr<SHorizontalBox>    barBox_;
    std::shared_ptr<SBorder>           popupHost_;
    std::vector<std::shared_ptr<SButton>> buttons_;
};
