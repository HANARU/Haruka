#pragma once
// ── 에셋 고르기 팝업 (H3b, 2026-11-22) ──────────────────────────────────────────────────────────────────────────
// 디테일 패널 에셋 경로 칸을 누르면 칸 아래에 열립니다(UE 의 에셋 고르기 드롭다운 축소판, ComponentPicker 와 같은 틀).
//   - 위: 검색 칸(열면 곧바로 칠 수 있음). 아래: 그 종류의 에셋 목록(콘텐츠 색인), 지금 값은 체크 · 처음 강조.
//   - ↑ ↓ 로 고르고 Enter 로 넣습니다. 행을 누르면 넣습니다. Esc · 바깥 누름 · 칸 다시 누름 = 닫기(값 그대로).
//   - 넣기는 onPick 콜백(디테일 패널이 Undo 한 단계와 함께)이 합니다. 목록 계산은 AssetPickerModel.hpp.
// 팝업은 디테일 패널이 있는 창 문맥의 팝업 층에 띄웁니다(떠 있는 창 포함). 다시 만들기는 프레임 끝(flushRebuild).

#include "AssetPickerModel.hpp"
#include "EditorUiKit.hpp"
#include "TextInputController.hpp"
#include "../ui/slate/HSlate.hpp"
#include "../ui/slate/HTextEdit.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class AssetPicker {
public:
    struct Deps {
        const EditorUiKit*                         ui = nullptr;
        std::function<const hcb::ContentIndex*()>  index;            // 콘텐츠 색인(콘텐츠 브라우저가 가진 것)
        std::function<void(const std::shared_ptr<HWidget>&)> focus;  // 검색 칸에 키보드 포커스(setFocusWidget)
        std::function<void()>                      releaseFocusQuiet;   // 검색 칸 포커스를 부수 효과 없이 놓음
        std::function<void()>                      clearFocus;       // 포커스 없음으로(닫을 때 검색 칸이 포커스면)
        std::function<HSlateContext*()>            popupContext;     // 디테일이 있는 창 문맥
    };

    void init(Deps deps) { deps_ = std::move(deps); }
    // 위젯 트리를 다시 만들 때: 닫고 포커스를 조용히 놓습니다.
    void resetPopup();

    // anchor(값 칸) 아래에 엽니다. assetType = PropDesc::assetType, current = 지금 값, onPick = 고른 경로(Undo 는 부르는 쪽).
    void open(const std::shared_ptr<HWidget>& anchor, const std::string& assetType, const std::string& current,
              std::function<void(const std::string&)> onPick);
    // 같은 칸으로 이미 열려 있으면 닫습니다(토글).
    void toggle(const std::shared_ptr<HWidget>& anchor, const std::string& assetType, const std::string& current,
                std::function<void(const std::string&)> onPick);
    void close();
    bool isOpen() const { return open_; }
    bool isOpenFor(const HWidget* anchor) const { return open_ && anchor && anchor_.lock().get() == anchor; }
    HSlateContext* popupContext() const;
    void flushRebuild();

    TextInputField field();
    HTextEditState& edit() { return edit_; }
    std::shared_ptr<HWidget> searchWidget() const { return searchKeep_; }

    // 덤프 · 시험
    const std::vector<hcb::Item>& items() const { return items_; }
    int highlighted() const { return hi_; }
    void setQuery(const std::string& q) { edit_.setText(q); refresh(true); }
    void moveHighlight(int dir);
    void activateHighlighted() { activate(hi_); }

private:
    float s(float x) const { return deps_.ui->s(x); }
    void refresh(bool resetHighlight);
    void activate(int idx);
    void rebuildPanel();
    void rebuildList();
    void dismissPopup();

    Deps deps_;
    HTextEditState edit_;
    bool focused_ = false;
    bool open_ = false;
    bool panelDirty_ = false, listDirty_ = false;
    std::weak_ptr<HWidget> anchor_;
    std::vector<hcb::AssetKind> kinds_;
    std::string assetType_;
    std::string current_;
    std::string lastQuery_;
    std::function<void(const std::string&)> onPick_;
    std::vector<hcb::Item> items_;
    int   hi_ = -1;
    float scrollY_ = 0.f;
    bool  scrollReset_ = false;

    HSlateContext*              ctx_ = nullptr;
    HPopupLayer::Handle         handle_ = 0;
    uint64_t                    serial_ = 0;
    std::shared_ptr<SSearchBox> search_;
    std::shared_ptr<SSearchBox> searchKeep_ = HNew(SSearchBox)->focusable(true);
    std::shared_ptr<SScrollBox> scroll_;
    std::shared_ptr<SVerticalBox> list_;
    std::shared_ptr<STextBlock> footer_;
};
