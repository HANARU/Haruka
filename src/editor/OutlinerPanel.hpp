#pragma once
#include "EditorUiKit.hpp"
#include "EditorContext.hpp"
#include "HCommandStack.hpp"
#include "ComponentVisualizers.hpp"
#include "TextInputController.hpp"
#include "dock/SDockOverlay.hpp"
#include "../core/HLog.hpp"
#include "../ui/slate/HSlate.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class Actor;

// 아웃라이너(Scene Outliner) 패널(S6d).
// 계층 트리 평탄화, 가상 스크롤 행 조립, 검색, 접힘, 인라인 리네임 상태,
// 끌어 놓기 재부모(drag reparent)를 담당합니다. EditorApp* 를 들지 않습니다.
class OutlinerPanel {
public:
    struct Row {
        uint64_t id       = 0;
        Actor*   actor    = nullptr;
        int      depth    = 0;
        bool     hasKids  = false;
        bool     expanded = true;
        bool     dimPath  = false;   // 검색 중 자손 매치 때문에 경로로만 남음
        bool     dimOff   = false;   // 계층에서 비활성
    };

    struct Deps {
        EditorUiKit*                ui          = nullptr;
        EditorContext*              ctx         = nullptr;
        HCommandStack*              cmdStack    = nullptr;
        const ComponentVisualizers* visualizers = nullptr;
        HLogCategory*               logCat      = nullptr;
        float*                      seedWidth   = nullptr;   // &outlinerW_
        Actor* const*               editorCamActor = nullptr;

        std::function<bool(const Actor*)>               isEditorCam;
        std::function<bool(const Actor*)>               isSelected;
        std::function<void(Actor*)>                     onSelectOnly;
        std::function<void(Actor*)>                     onToggleSelect;
        std::function<void(Actor*)>                     onSelectRange;
        std::function<std::vector<Actor*>()>            getSelectionRoots;
        std::function<void(const std::vector<Actor*>&)> onRecordCreation;
        std::function<void(Actor*)>                     onDeleteActor;
        std::function<void()>                           onCancelGizmo;
        std::function<void()>                           onDetailsDirty;
        std::function<void()>                           onSearchClicked;
    };

    void init(Deps deps);
    std::shared_ptr<HWidget> build();
    std::shared_ptr<SDockOverlay> resetDropOverlay();

    void rebuild();
    bool flushRebuild();
    void markDirty()      { dirty_ = true; }
    void revealSelected() { revealSel_ = true; }
    void revealActor(Actor* a);
    void collapseActor(uint64_t id)         { collapsed_.insert(id); dirty_ = true; }
    void uncollapseActor(uint64_t id)       { collapsed_.erase(id); }
    void setSearchText(const std::string& t){ search_ = t; dirty_ = true; }

    void checkLayoutSize();
    bool renamingChanged() const            { return renaming_ != prevRenaming_; }
    void syncPrevRenaming()                 { prevRenaming_ = renaming_; }
    bool isRenaming() const                 { return renaming_; }
    void cancelRename()                     { renaming_ = false; }
    void onActorDestroyed(Actor* a);
    void resetForWorld();

    void releasePressHeld()                 { pressHeld_ = false; }
    void setPressHeldIfContains(glm::vec2 p);
    void tickDrag(float mx, float my, bool leftDown, bool prevLeftDown, bool modalOpen, bool visible);
    void cancelDrag();

    const std::vector<Row>& rows() const { return rows_; }
    std::string nextActorName() { return "Actor_" + std::to_string(actorCounter_++); }

    TextInputField searchField();
    TextInputField renameField();

private:
    static constexpr float kRowH      = EditorTheme::rowH;
    static constexpr float kIndent    = EditorTheme::space12;
    static constexpr float kArrowW    = 16.f;
    static constexpr float kTypeW     = 74.f;
    static constexpr float kEyeW      = 20.f;
    static constexpr float kDelW      = 26.f;
    static constexpr float kMinLabelW = 56.f;
    static constexpr float kHeaderH   = EditorTheme::panelHeaderH;   // "+ Add Empty" 줄
    static constexpr float kFooterH   = 20.f;   // 액터 개수 줄

    struct RowHit { uint64_t id = 0; std::shared_ptr<HWidget> widget; };

    float s(float x) const { return deps_.ui->s(x); }
    void        flatten(const std::string& filterLower, std::vector<Row>& out) const;
    std::string typeLabel(const Actor* a, glm::vec4& outColor) const;
    void        addRow(const Row& r, float listW);

    Deps deps_;
    std::shared_ptr<SVerticalBox> list_;
    std::shared_ptr<SSearchBox>   searchBox_;
    std::shared_ptr<SDockOverlay> dropOverlay_;

    std::string search_;
    bool        searchFocused_ = false;
    bool        searchSelAll_  = false;

    bool        renaming_     = false;
    bool        prevRenaming_ = false;
    std::string renameBuffer_;
    bool        renameSelAll_ = false;

    std::unordered_set<uint64_t> collapsed_;
    std::vector<Row>             rows_;
    std::vector<RowHit>          rowHits_;
    bool        dirty_        = false;
    bool        revealSel_    = false;
    int         scrollTop_    = 0;
    int         rowCap_       = 0;
    glm::ivec2  layoutSig_    { 0, 0 };
    std::string footer_;
    std::string filterPrev_;
    bool        pressHeld_    = false;
    int         actorCounter_ = 0;

    uint64_t  dragId_     = 0;
    bool      dragActive_ = false;
    glm::vec2 dragStart_  { 0.f, 0.f };
};
