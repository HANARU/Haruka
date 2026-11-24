#pragma once
#include "EditorUiKit.hpp"
#include "EditorMenuModel.hpp"
#include "PropertyRowKit.hpp"
#include "TextInputController.hpp"
#include "DetailsSearch.hpp"
#include "../core/HText.hpp"
#include "../scene/Reflect.hpp"
#include "../ui/slate/HSlate.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ── 범용 설정 창 틀 (PS1b · PS-D2, Project Settings · Editor Preferences 공용) ─────────────────────────────
// 왼쪽 분류 목록 · 위 검색 칸 · 다시 시작 띠(PS1c 단추 자리) · 오른쪽 머리 · 접히는 절 + PropertyRowKit 행을
// 설정 원본(SettingsSource)으로부터 그립니다.
struct SettingsCategoryDef {
    std::string id;
    std::string group;
    HText       title;
    HText       description;
    std::function<std::string()> storagePath;     // 프로젝트 루트 기준 상대 경로
    std::function<std::string()> statusLine;      // 선택적 상태 줄(예: XR 이번 실행 상태)
    std::function<void(SVerticalBox&, float scale)> customBody;   // Packaging 등 전용 본문
};

struct SettingsRowItem {
    std::string categoryId;
    std::string section;
    std::string englishName;
    std::string tooltip;
    PropDesc    prop;
    bool        readOnly = false;
    std::function<void()> onDragEnd;
    // 플랫폼 덮어쓰기(PS-D6)
    bool        allowPlatformOverride = false;
    bool        platformOverrideActive = false;
    PropDesc    platformOverrideProp;
    std::function<void()> onPlatformDragEnd;
    std::function<void()> onAddPlatformOverride;
    std::function<void()> onRemovePlatformOverride;
};

struct SettingsSource {
    std::vector<std::pair<std::string, HText>> groups;   // (group id, 표시 이름)
    std::vector<SettingsCategoryDef>           categories;
    std::function<std::vector<SettingsRowItem>()> buildItems;
    std::function<void()>                         onReload;
    std::function<bool()>                         hasRestartChanges;
    std::function<std::shared_ptr<HWidget>()>     restartBannerAction;   // PS1c: "Restart Now" 단추 자리
};

namespace hsettings_ui {

// 검색어(query)에 맞는 행이 있는 분류 ID 목록을 돌려줍니다. 검색어가 비면 모든 분류를 돌려줍니다.
inline std::vector<std::string> filterCategories(const std::vector<SettingsCategoryDef>& cats,
                                                 const std::vector<SettingsRowItem>& items,
                                                 const std::string& query) {
    std::vector<std::string> out;
    if (!hdsearch::active(query)) {
        for (const auto& c : cats) out.push_back(c.id);
        return out;
    }
    for (const auto& c : cats) {
        bool any = false;
        for (const auto& it : items) {
            if (it.categoryId != c.id) continue;
            const std::string shown = it.prop.label ? it.prop.label : "";
            if (hdsearch::matches(query, shown, it.englishName)) { any = true; break; }
        }
        if (!any && c.customBody) {
            if (hdsearch::matches(query, c.title.toString(), c.title.source())) any = true;
        }
        if (any) out.push_back(c.id);
    }
    return out;
}

} // namespace hsettings_ui

class SettingsPanel {
public:
    struct Deps {
        const EditorUiKit* ui = nullptr;
        std::function<void(glm::vec2, std::vector<EditorMenuEntry>)> openMenu;
        std::function<void(const std::shared_ptr<HWidget>&, std::vector<EditorMenuEntry>, int)> openCombo;
        std::function<bool(const HWidget*)> isComboOpen;
        std::function<void()>               closeMenu;
        std::function<void(const std::shared_ptr<HWidget>&)> focusText;
        std::function<void(const std::shared_ptr<HWidget>&, const std::string&, const std::string&,
                           std::function<void(const std::string&)>)> openAssetPicker;
        std::function<void(const std::string&)> browseToAsset;
    };

    void init(Deps deps, SettingsSource source);
    std::shared_ptr<HWidget> build();

    void reloadAndRebuild();
    void rebuild();
    void flushRebuild();
    void markDirty() { dirty_ = true; }

    bool selectCategory(const std::string& id);
    const std::string& selectedCategory() const { return selectedCategory_; }

    // 검색 칸 · 글 칸 포커스 연동
    std::shared_ptr<HWidget> searchWidget() const { return searchKeep_; }
    TextInputField searchField();
    void setSearch(const std::string& text) { searchEdit_.setText(text); dirty_ = true; }
    std::shared_ptr<HWidget> stringWidget() const { return rowKit_.stringWidget(); }
    TextInputField stringField() { return rowKit_.stringField(); }

    // 행 오른쪽 메뉴(Reset to Default · Copy · Paste)
    bool openRowMenuAt(glm::vec2 p);
    bool menuRowCanReset() const { return menuRow_ && menuRow_->canReset && menuRow_->canReset(); }
    bool menuRowCanPaste() const { return menuRow_ && (bool)menuRow_->paste; }
    bool menuRowCanCopy()  const { return menuRow_ && (bool)menuRow_->copyText; }
    void resetMenuRow()          { if (menuRowCanReset()) menuRow_->reset(); }
    std::string menuRowText() const { return menuRowCanCopy() ? menuRow_->copyText() : std::string(); }
    bool pasteMenuRow(const std::string& text) { return menuRowCanPaste() && menuRow_->paste(text); }

    bool scrollContains(const glm::vec2& p) const;

private:
    float s(float x) const { return deps_.ui ? deps_.ui->s(x) : x; }
    const SettingsCategoryDef* findCategory(const std::string& id) const;

    Deps           deps_;
    SettingsSource source_;
    PropertyRowKit rowKit_;
    std::shared_ptr<HPropertyColumns> cols_ = std::make_shared<HPropertyColumns>();
    std::shared_ptr<PropertyRowKit::RowOps> menuRow_;

    std::shared_ptr<SVerticalBox> rootBox_;
    std::shared_ptr<SVerticalBox> catList_;
    std::shared_ptr<SVerticalBox> bodyHost_;
    std::shared_ptr<SScrollBox>   scroll_;

    std::shared_ptr<SSearchBox> searchKeep_ = HNew(SSearchBox)->focusable(true);
    HTextEditState              searchEdit_;
    bool                        searchFocused_ = false;
    std::string                 shownSearch_;

    std::string selectedCategory_;
    std::map<std::string, bool> sectionExpanded_;
    bool dirty_ = false;
};
