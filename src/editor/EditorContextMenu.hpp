#pragma once
// 오른쪽 누름 메뉴(2026-11-21, 콘텐츠 브라우저가 처음 씀). 누른 자리에 항목 목록을 띄우고, 항목을 누르면 닫은 뒤 그 동작을
// 부릅니다. 바깥을 누르거나(왼쪽 · 오른쪽) Esc 면 닫습니다. 팝업은 연 패널이 있는 창 문맥의 팝업 층(HPopupLayer, H2a)에 띄우고,
// 다시 만들기는 프레임 끝으로 미룹니다(로그 Categories 팝업 · Add Component 팝업과 같은 규칙). 닫기는 곧바로 층에서 뺍니다.
// H2b: 항목은 상단 메뉴와 같은 메뉴 모델(EditorMenuEntry: 명령 · 소제목 · 구분선 · 하위 메뉴)이고, 그리기 · 하위 메뉴 · 키보드 이동은
// 같은 위젯(EditorMenuView)이 맡습니다. 그 자리의 경로에 묶인 동작은 EditorMenuEntry::act 로 줍니다.

#include "EditorUiKit.hpp"
#include "EditorMenuView.hpp"
#include "../ui/slate/HSlate.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

//
// H3a: 연 패널을 열 때 받습니다(예전에는 "Bottom" 고정). 디테일 속성 행의 오른쪽 메뉴와 열거 콤보 드롭다운도 이 메뉴를 씁니다.
// 콤보는 openBelow(단추 아래, 단추 폭 이상, 지금 값 행 강조 · 체크)로 엽니다. 창 안 팝업 하나 · 키(↑ ↓ Enter Esc) · 바깥 누름 닫기 ·
// 떠 있는 창 문맥은 오른쪽 메뉴와 같습니다.
class EditorContextMenu {
public:
    // popupContext: 패널 이름 → 메뉴를 띄울 문맥(그 패널이 있는 창). null 이면 열지 않습니다. reg: 명령 id 항목을 풀 레지스트리(없어도 됨).
    void init(const EditorUiKit* ui, std::function<HSlateContext*(const std::string&)> popupContext, EditorCommandRegistry* reg = nullptr) {
        ui_ = ui;
        popupContext_ = std::move(popupContext);
        view_.init(ui, reg, [this] { close(); }, [this] { open_ = false; });
    }
    // 위젯 트리를 다시 만들 때(buildWidgetTree): 열린 메뉴를 닫고 항목을 비웁니다.
    void resetPopup();

    // 누른 자리(그 패널이 있는 창 좌표)의 오른쪽 아래에 엽니다. panel = 연 패널(PanelDef id).
    void open(glm::vec2 at, std::vector<EditorMenuEntry> entries, const std::string& panel = "Bottom");
    // 위젯 아래에 엽니다(콤보, H3a). 폭은 위젯 폭 이상, highlightRow 행을 강조(-1 = 없음). 단추 위 누름은 바깥이 아닙니다(단추가 닫음).
    void openBelow(const std::shared_ptr<HWidget>& anchor, std::vector<EditorMenuEntry> entries, const std::string& panel,
                   int highlightRow = -1);
    void close();
    bool isOpen() const { return open_; }
    // 연 패널(열려 있지 않으면 마지막으로 연 패널).
    const std::string& panel() const { return panel_; }
    // 지금 이 위젯 아래에 열린 콤보인가(콤보 단추 토글).
    bool isOpenBelow(const HWidget* anchor) const { return open_ && anchor && anchor_.lock().get() == anchor; }
    // 지금 메뉴가 떠 있는 문맥(층에 들어가 있을 때만).
    HSlateContext* popupContext() const { return view_.context(); }
    // 열려 있으면 그 메뉴가 뜰(또는 떠 있는) 문맥. 키보드 창 판정용.
    HSlateContext* targetContext() const;
    void flushRebuild();

    const std::vector<EditorMenuEntry>& entries() const { return entries_; }
    // 덤프 · 시험: 맨 위 단계에서 글이 같은 항목을 누른 것과 같습니다. 없거나 꺼져 있으면 false.
    bool invoke(const std::string& label);
    // 포인터 hover(그 창 문맥 좌표) · 메뉴 키.
    void hover(const HSlateContext* ctx, glm::vec2 p) { view_.hover(ctx, p); }
    void handleKey(hmenu::Key k);
    const EditorMenuView& view() const { return view_; }

private:
    void rebuild();

    const EditorUiKit*              ui_ = nullptr;
    std::function<HSlateContext*(const std::string&)> popupContext_;
    EditorMenuView                  view_;
    std::vector<EditorMenuEntry>    entries_;
    glm::vec2                       at_{ 0.f };
    std::string                     panel_ = "Bottom";
    std::weak_ptr<HWidget>          anchor_;          // openBelow 의 단추
    bool                            anchored_ = false; // openBelow 로 열었는가(아니면 누른 자리 메뉴)
    int                             highlight_ = -1;
    bool                            open_  = false;
    bool                            dirty_ = false;
};
