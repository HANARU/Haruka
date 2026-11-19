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

    // 최상위 오버레이용 팝업 호스트(buildWidgetTree 에서 한 번 붙임).
    std::shared_ptr<SBorder> resetPopupHost();
    void openPopup();
    void closePopup();
    void handleOutsideClick(glm::vec2 mousePos);
    bool popupContains(glm::vec2 pos) const;

    // 텍스트 포커스 중재용 검색 칸 상태.
    SearchField searchField();
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
    std::string              search_;
    bool                     searchFocused_ = false;
    bool                     searchSelAll_  = false;
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
    std::shared_ptr<SBorder>      popupHost_;
    std::shared_ptr<HWidget>      popupPanel_;
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
    void positionPopupPanel(const std::shared_ptr<HWidget>& panel,
                            const std::shared_ptr<HWidget>& absorber,
                            float panelW, float panelH);
    bool recordVisible(const HLogRecord& r, const std::string& needleLower) const;
    void setCategoryHidden(uint16_t index, bool hidden);
    void saveFilter() const;
    void saveLevels() const;
};
