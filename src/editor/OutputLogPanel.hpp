#pragma once
#include "EditorUiKit.hpp"
#include "TextInputController.hpp"
#include "../core/HLog.hpp"
#include "../ui/slate/widgets/SBorder.hpp"
#include "../ui/slate/widgets/SSearchBox.hpp"
#include "../ui/slate/widgets/SVerticalBox.hpp"
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

// 출력 로그 패널(S6a). 위젯 트리 · 사본 링 · 표시/기록 필터 · Categories 팝업을 소유합니다.
// EditorApp* 을 들지 않고 EditorUiKit 과 좁은 콜백만 받습니다.
class OutputLogPanel {
public:
    using SearchField = TextInputField;

    void init(const EditorUiKit* ui, HLogCategory* logCat, float menuBarH, bool dumpMode,
              std::function<void()> onSearchClicked);

    // 부팅 시 첫 syncLogCategories 전에 저장된 표시 필터를 읽습니다.
    void loadFilter();

    // 매 프레임 tick: 새 seq(바뀌었으면 true), 행 영역 높이 변화, 팝업 카테고리 동기화.
    bool tick(bool tabActive);

    // 하단 패널이 로그 탭을 만들 때 · 버릴 때 호출합니다.
    void buildTab(SVerticalBox& bottomContent);
    void releaseTab(bool tabActive);

    // 지연 리빌드 소진(팝업 · 행 문자열).
    void flushRebuilds(bool tabActive);

    // Categories 팝업(H2a): Categories 단추를 감싼 SMenuAnchor 가 로그 패널이 있는 창 문맥의 팝업 층에 띄웁니다. fn 은 그 문맥을
    // 돌려줍니다(null 이면 열지 않음). 바깥 누름 · 위치는 팝업 층이 맡습니다.
    void setPopupContext(std::function<HSlateContext*()> fn) { popupContextFn_ = std::move(fn); }
    // 위젯 트리를 다시 만들 때(buildWidgetTree): 팝업을 닫습니다(예전 resetPopupHost).
    void resetPopup();
    void openPopup();
    void closePopup();
    bool isPopupOpen() const { return popupOpen_; }
    // 지금 팝업이 떠 있는 문맥(층에 들어가 있을 때만).
    HSlateContext* popupContext() const { return catAnchor_ ? catAnchor_->openContext() : nullptr; }

    // 텍스트 포커스 중재용 검색 칸 상태.
    SearchField searchField();
    // 글 칸 포커스 위젯(H1a-2, TextFieldBinding::widget). 탭을 놓아도 같은 객체입니다.
    std::shared_ptr<HWidget> searchWidget() const  { return searchBoxKeep_; }
    std::shared_ptr<HWidget> consoleWidget() const { return consoleBoxKeep_; }
    // 콘솔 줄(2026-11-20, Docs/Gameplay_Tools.md D9). run 은 명령 한 줄을 실행해 결과 글을 돌려주는 쪽(EditorApp 이 CVar 레지스트리로 연결),
    // clicked 는 칸을 눌렀을 때(포커스 요청).
    void        setConsole(std::function<std::string(const std::string&)> run, std::function<void()> clicked);
    TextInputField consoleField();
    bool        isConsoleFocused() const { return consoleFocused_; }
    HTextEditState& consoleEdit() { return consoleEdit_; }
    // 칸의 글을 실행하고 기록에 넣은 뒤 비웁니다(Enter, --ui-console).
    void        submitConsole();
    const std::vector<std::string>& consoleHistory() const { return history_; }
    bool        isSearchFocused() const { return searchFocused_; }
    bool        searchContains(glm::vec2 p) const;

private:
    static constexpr float kRowH  = 16.f;
    static constexpr float kTimeW = 52.f;
    static constexpr float kCatW  = 84.f;

    struct ViewRow { std::string time, category, text; glm::vec4 color = EditorTheme::textPrimary; };

    const EditorUiKit*    ui_       = nullptr;
    HLogCategory*         logCat_   = nullptr;
    float                 menuBarH_ = 26.f;
    bool                  dumpMode_ = false;
    std::function<void()> onSearchClicked_;

    uint64_t                 seenSeq_      = 0;
    std::deque<HLogRecord>   mirror_;
    uint64_t                 mirrorSeq_    = 0;
    std::vector<std::string> catNames_;
    std::vector<int>         catCounts_;
    std::vector<char>        catHidden_;
    std::set<std::string>    hiddenNames_;
    bool                     showVerb_[5]  = { false, true, true, true, true };
    HTextEditState           searchEdit_;   // 글자 체계 T1
    HTextEditState           consoleEdit_;
    bool                     consoleFocused_ = false;
    std::vector<std::string> history_;
    int                      historyPos_ = -1;   // -1 = 기록을 고르지 않음
    std::function<std::string(const std::string&)> consoleRun_;
    std::function<void()>    consoleClicked_;
    std::shared_ptr<SSearchBox> consoleBox_;
    // 글 칸 포커스 위젯(H1a-2). HSlate 포커스는 위젯을 약한 참조로 잡으므로, 다시 지을 때 새로 만들지 않고 HReuse 로
    // 같은 객체를 처음 상태로 되돌려 씁니다. 처음 짓기 전에도(팝업 · 행이 아직 없음) 포커스를 받도록 만들 때부터 focusable 입니다.
    // consoleBox_ · searchBox_ 는 탭이 보일 때만 이 객체를 가리킵니다(releaseTab 이 놓음, 판정 코드는 그대로).
    std::shared_ptr<SSearchBox> consoleBoxKeep_ = HNew(SSearchBox)->focusable(true);
    std::shared_ptr<SSearchBox> searchBoxKeep_  = HNew(SSearchBox)->focusable(true);
public:
    // 콘솔 기록에서 위(-1) · 아래(+1)로 고릅니다(화살표 · --ui-console-recall).
    void stepHistory(int dir);
private:
    bool                     searchFocused_ = false;
    uint64_t                 clearedSeq_    = 0;
    std::string              errText_  = "E 0";
    std::string              warnText_ = "W 0";
    bool                     hasErr_   = false;
    bool                     hasWarn_  = false;
    std::vector<ViewRow>     viewRows_;
    bool                     viewDirty_ = true;
    int                      rowCap_    = 0;
    glm::ivec2               rowsSig_ { 0, 0 };

    uint64_t    anchorSeq_ = 0;
    uint64_t    pinSeq_    = 0;
    int         scrollReq_ = 0;
    std::string jumpText_;

    std::shared_ptr<SVerticalBox> rowsBox_;
    std::shared_ptr<SSearchBox>   searchBox_;
    std::shared_ptr<SButton>      catButton_;
    // Categories 단추를 감싸는 앵커(H2a). 탭을 다시 만들어도 같은 객체를 쓰고 자식만 바꿉니다(열린 팝업과의 연결 유지).
    std::shared_ptr<SMenuAnchor>  catAnchor_;
    std::function<HSlateContext*()> popupContextFn_;
    bool                          popupOpen_  = false;
    bool                          popupDirty_ = false;

    void pullRecords();
    void syncCategories();
    void rebuildRows();
    void refreshView();
    void rebuildPopup();
    std::shared_ptr<SVerticalBox> buildPopupList(const std::vector<HLogCategoryState*>& cats);
    void addPopupCategoryRow(SVerticalBox& list, HLogCategoryState* st,
                             float rowH, const glm::vec4& btnN, const glm::vec4& btnH);
    std::shared_ptr<HWidget> buildPopupPanel();
    bool recordVisible(const HLogRecord& r, const std::string& needleLower) const;
    void setCategoryHidden(uint16_t index, bool hidden);
    void saveFilter() const;
    void saveLevels() const;
};
