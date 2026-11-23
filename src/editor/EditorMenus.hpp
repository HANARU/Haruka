#pragma once
#include "EditorUiKit.hpp"
#include "EditorMenuView.hpp"
#include "../ui/slate/HSlate.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// 상단 메뉴 바 · 드롭다운 팝업(S6e, H2b 에서 메뉴 모델 · 명령 레지스트리로).
// 메뉴 정의(이름 + EditorMenuEntry 목록)는 EditorApp::buildMenus() 가 채워 setDefs() 로 넘깁니다. 항목은 대개 명령 id 이고,
// 글 · 단축키 · 켜짐 · 체크는 레지스트리 명령에서 읽습니다.
// 드롭다운은 메인 문맥의 팝업 층(HPopupLayer, H2a)에 Consume 팝업으로 띄웁니다: 메뉴 줄 아래 배경의 누름 · 휠은 먹고, 배경에서
// 누르고 떼면 닫힙니다(예전 전면 캐처 단추). 메뉴 줄은 덮지 않아 다른 메뉴 단추로 hover 전환 · 토글이 됩니다. 창 오른쪽 끝에 닿으면
// 창 안으로 끼웁니다(H2b). 그리기 · 하위 메뉴 · 키보드 이동은 EditorMenuView 가 맡습니다.
class EditorMenus {
public:
    struct Def {
        std::string                  name;
        std::vector<EditorMenuEntry> entries;
        float                        x = 0.f;
        std::string                  englishName;   // 가속 글자 계산용 영어 원문(비면 name 사용)
    };

    void init(const EditorUiKit* ui, float menuBarH, HSlateContext* ctx, EditorCommandRegistry* reg);
    std::shared_ptr<SHorizontalBox> buildBar();

    void setDefs(std::vector<Def> defs);
    const std::vector<Def>& defs() const { return defs_; }
    void rebuildBar();
    void rebuildPopup();
    void flushRebuilds();

    bool isOpen() const { return openMenu_ >= 0; }
    bool isBarActive() const { return barState_.barActive; }
    bool showMnemonics() const { return barState_.showMnemonics(); }
    int  openIndex() const { return openMenu_; }
    // 드롭다운이 팝업 층에 들어가 있는가(열었지만 아직 소진 전이면 거짓).
    bool popupShown() const { return view_.shown(); }
    void close();
    // "Edit" 또는 "Edit/Language"(하위 메뉴까지, 덤프용). 메뉴 이름이 없으면 거짓.
    bool openByName(const std::string& path);
    void handleHoverSwitch(glm::vec2 mousePos);
    // 드롭다운 행 hover(하위 메뉴 열기 · 닫기). 메인 문맥 좌표.
    void hoverRows(glm::vec2 mousePos) { view_.hover(ctx_, mousePos); }
    // 메뉴 키(드롭다운이 열려 있을 때). ← → 로 이웃 메뉴, Esc 로 한 단계 닫기.
    void handleKey(hmenu::Key k);
    // Alt 메뉴 바 상태 기계(C1).
    hmenu::BarAct handleBarEvent(const hmenu::BarEvent& ev);
    // 열린 드롭다운 글자 키(C1).
    bool handleDropdownMnemonic(char asciiKey) { return view_.activateByMnemonic(asciiKey); }

    const EditorMenuView& view() const { return view_; }

private:
    float s(float x) const { return ui_->s(x); }
    void setOpen(int mi);      // openMenu_ 를 바꾸고 다시 만들기 예약. 닫으면(-1) 곧바로 층에서 뺌
    void switchTo(int mi, bool highlightFirst);

    const EditorUiKit*     ui_       = nullptr;
    EditorCommandRegistry* reg_      = nullptr;
    float                  menuBarH_ = 26.f;

    std::vector<Def>                   defs_;
    std::vector<char>                  barAccels_;
    hmenu::BarState                    barState_;
    int                                openMenu_   = -1;
    bool                               barDirty_   = false;
    bool                               popupDirty_ = false;
    bool                               highlightFirst_ = false;   // 다음 rebuildPopup 에서 첫 행을 강조(키보드로 연 메뉴)
    std::string                        pendingSub_;               // 다음 rebuildPopup 뒤 열 하위 메뉴(덤프)
    std::shared_ptr<SHorizontalBox>    barBox_;
    HSlateContext*                     ctx_    = nullptr;
    EditorMenuView                     view_;
    std::vector<std::shared_ptr<SButton>> buttons_;
    glm::vec2                          lastBarHover_ { -1e9f, -1e9f };   // hover 전환은 포인터가 움직였을 때만(H2b)
};
