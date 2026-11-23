#pragma once
// ── 메뉴 위젯 (H2b, 2026-11-22) ───────────────────────────────────────────────────────────────────────────────
//
// 메뉴 항목 모델(EditorMenuModel.hpp)을 한 문맥의 팝업 층(HPopupLayer)에 띄우는 위젯 묶음입니다. 상단 메뉴 드롭다운(EditorMenus)과
// 오른쪽 누름 메뉴(EditorContextMenu)가 함께 씁니다(UE 의 SMultiBoxWidget + FMenuStack 을 줄인 것).
//
//   - 단계(level): 0 = 루트 메뉴, 1.. = 하위 메뉴. 하위 메뉴는 층의 push(parent = 위 단계)와 Right 자리(넘치면 왼쪽, 창 안으로 끼움)로
//     띄웁니다. 부모 단계를 닫으면 층이 하위도 함께 닫습니다.
//   - 행 모양: [체크 · 아이콘 칸][글][단축키 열][하위 메뉴 화살표]. 꺼진 항목은 흐리게 그리고 누를 수 없습니다(누름은 메뉴가 먹음).
//   - 포인터: hover 로 하위 메뉴를 열고, 같은 메뉴의 다른 행 hover 로 닫습니다. 포인터가 움직였을 때만 강조를 옮깁니다(키보드로 옮긴
//     강조가 가만히 있는 포인터에 되돌려지지 않게). 하위 메뉴 행을 눌러도 엽니다.
//   - 키보드: hmenu::decide 규칙. 메뉴 줄의 다음 · 이전 메뉴와 맨 위 Esc 는 소유자가 처리하도록 돌려줍니다.
//   - 실행: 명령을 고르면 소유자의 닫기(requestClose)를 먼저 부르고, 그 뒤 명령(레지스트리 execute) · 그 자리 동작을 부릅니다.
//   - 소유자가 닫을 때(close)는 알림이 없고, 층이 닫을 때(배경 누름 · 한 번에 하나 규칙 · 다른 창 팝업 닫기)는 onRootDismissed 를 부릅니다.

#include "EditorMenuModel.hpp"
#include "EditorUiKit.hpp"
#include "../ui/slate/HSlate.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class EditorMenuView {
public:
    struct RootSpec {
        HPopupRect                anchor;
        HPopupPlacement           placement;
        HPopupLayer::EOutside     outside          = HPopupLayer::EOutside::PassThrough;
        bool                      closeOnLeft      = true;
        bool                      closeOnRight     = false;
        float                     backdropInsetTop = 0.f;   // Consume 배경이 시작하는 높이(메뉴 줄 아래)
        float                     subInsetTop      = 0.f;   // 하위 메뉴를 끼울 때 위쪽 여백(메뉴 줄 아래로)
        float                     minWidth         = 180.f; // 1x 값
        std::weak_ptr<HWidget>    anchorWidget;             // 이 위젯 위 누름은 바깥이 아님(콤보 단추: 다시 누르면 단추가 닫음, H3a)
    };

    void init(const EditorUiKit* ui, EditorCommandRegistry* reg, std::function<void()> requestClose,
              std::function<void()> onRootDismissed) {
        ui_ = ui;
        reg_ = reg;
        requestClose_ = std::move(requestClose);
        onRootDismissed_ = std::move(onRootDismissed);
    }

    // 루트 메뉴를 띄우거나(같은 문맥에 떠 있으면 내용만 바꿈, 하위 메뉴는 닫음) 바꿉니다. initialHighlight: -1 = 강조 없음,
    // 0 이상 = 그 행(고를 수 없는 행이면 처음 고를 수 있는 행. 0 은 예전처럼 "처음 고를 수 있는 행"과 같음). 실패(문맥 없음 · 행 없음)면 거짓.
    bool show(HSlateContext* ctx, std::vector<EditorMenuEntry> entries, const RootSpec& spec, int initialHighlight = -1);
    // 층에서 뺍니다(알림 없음).
    void close();
    bool shown() const;
    HSlateContext* context() const { return shown() ? ctx_ : nullptr; }
    int depth() const { return (int)levels_.size(); }

    // 포인터 hover(그 창 문맥 좌표). 다른 문맥이면 무시합니다.
    void hover(const HSlateContext* ctx, glm::vec2 p);
    // 키 하나. NextMenu · PrevMenu · Close 는 소유자가 처리합니다(나머지는 여기서 처리하고 None 이 아닌 값을 돌려줌).
    hmenu::Act key(hmenu::Key k, bool inBar);
    // 드롭다운 글자 키(C1): 가장 깊은 단계에서 asciiKey 로 시작하는 다음 켜진 항목으로 강조를 옮기거나(여럿), 하나뿐이면 바로 실행합니다.
    bool activateByMnemonic(char asciiKey);

    // ── 덤프 · 시험 ──
    int  highlight(int level) const { return level < (int)levels_.size() ? levels_[(size_t)level].highlight : -1; }
    const std::vector<EditorMenuRow>* rows(int level) const {
        return level < (int)levels_.size() ? &levels_[(size_t)level].rows : nullptr;
    }
    // 가장 깊은 단계에서 글이 같은 하위 메뉴를 엶(덤프 --ui-open-menu=Edit/Language).
    bool openSubmenuByLabel(const std::string& label);
    // 루트에서 글이 같은 켜진 항목을 누른 것과 같음(오른쪽 메뉴 invoke). 없거나 꺼졌으면 거짓.
    bool activateByLabel(const std::string& label);
    HPopupLayer::Handle rootHandle() const { return levels_.empty() ? 0 : levels_.front().handle; }

private:
    struct Level {
        uint64_t                              token  = 0;
        HPopupLayer::Handle                   handle = 0;
        std::vector<EditorMenuEntry>          entries;
        std::vector<EditorMenuRow>            rows;
        std::vector<std::shared_ptr<SButton>> buttons;   // rows 와 1:1(소제목 · 구분선은 null)
        std::vector<float>                    rowTop;    // 패널 안 행 윗변(배율 픽셀)
        std::shared_ptr<HWidget>              panel;
        glm::vec2                             size { 0.f };
        int                                   highlight = -1;
        int                                   parentRow = -1;   // 이 단계를 연 위 단계의 행
    };

    float s(float x) const { return ui_ ? ui_->s(x) : x; }
    std::shared_ptr<HWidget> buildPanel(Level& lv, float minWidth);
    bool openSub(int level, int row, bool highlightFirst);
    void closeFrom(int level);              // level 과 그 아래 단계를 닫음(알림 없음)
    void setHighlight(int level, int row);
    void activate(int level, int row);
    int  levelOf(uint64_t token) const;
    float rowHeight(const EditorMenuRow& r) const;
    HPopupLayer::Spec levelSpec(Level& lv, uint64_t token);

    const EditorUiKit*     ui_  = nullptr;
    EditorCommandRegistry* reg_ = nullptr;
    std::function<void()>  requestClose_;
    std::function<void()>  onRootDismissed_;
    HSlateContext*         ctx_ = nullptr;
    RootSpec               spec_;
    std::vector<Level>     levels_;
    uint64_t               nextToken_ = 0;
    glm::vec2              lastHover_ { -1e9f, -1e9f };
};
