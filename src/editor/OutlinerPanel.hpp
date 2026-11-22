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
        // 이름 바꾸기 칸의 포커스를 부수 효과 없이 놓습니다(TextInputController::releaseQuiet, M3a). 비어 있으면 거울 값만 끕니다.
        std::function<void()>                           onReleaseRename;
    };

    void init(Deps deps);
    std::shared_ptr<HWidget> build();
    std::shared_ptr<SDockOverlay> resetDropOverlay();
    // 끌어 놓기 미리보기를 그릴 위젯(M3b). 아웃라이너가 떠 있는 창에 있으면 그 창 루트의 미리보기를 씁니다(위젯 좌표가 그 창 기준).
    // null 이면 resetDropOverlay 가 만든 메인 루트의 것으로 돌아갑니다. 바꿀 때 옛 미리보기는 끕니다.
    void useDropOverlay(const std::shared_ptr<SDockOverlay>& o);
    // 수정자 키(Ctrl · Shift 누름) · 좌버튼 상태를 읽을 창(M3b: 아웃라이너가 있는 창). null 이면 메인(ctx->window)입니다.
    void setPointerWindow(IWindow* w) { pointerWindow_ = w; }

    void rebuild();
    bool flushRebuild();
    void markDirty()      { dirty_ = true; }
    void revealSelected() { revealSel_ = true; }
    void revealActor(Actor* a);
    void collapseActor(uint64_t id)         { collapsed_.insert(id); dirty_ = true; }
    void uncollapseActor(uint64_t id)       { collapsed_.erase(id); }
    void setSearchText(const std::string& t){ searchEdit_.setText(t); dirty_ = true; }
    // 덤프(--ui-type · --ui-ime-compose): 포커스된 칸의 편집 상태.
    HTextEditState& searchEdit()            { return searchEdit_; }
    HTextEditState& renameEdit()            { return renameEdit_; }

    void checkLayoutSize();
    bool renamingChanged() const            { return renaming_ != prevRenaming_; }
    void syncPrevRenaming()                 { prevRenaming_ = renaming_; }
    bool isRenaming() const                 { return renaming_; }
    void cancelRename()                     { if (deps_.onReleaseRename) deps_.onReleaseRename(); else renaming_ = false; }
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
    // 글 칸 포커스 위젯(H1a-2, TextFieldBinding::widget). 패널이 사는 동안 같은 객체입니다.
    std::shared_ptr<HWidget> searchWidget() const { return searchBox_; }
    std::shared_ptr<HWidget> renameWidget() const { return renameText_; }

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
    // 글 칸 포커스 위젯(H1a-2). HSlate 포커스는 위젯을 약한 참조로 잡으므로, 다시 지을 때 새로 만들지 않고 HReuse 로
    // 같은 객체를 처음 상태로 되돌려 씁니다. 처음 짓기 전에도(팝업 · 행이 아직 없음) 포커스를 받도록 만들 때부터 focusable 입니다.
    // 이름 바꾸기 칸은 행을 다시 지을 때마다 그 행에 다시 붙입니다(포커스는 행이 없을 때 시작될 수도 있음).
    std::shared_ptr<SSearchBox>    searchBox_  = HNew(SSearchBox)->focusable(true);
    std::shared_ptr<SEditableText> renameText_ = HNew(SEditableText)->focusable(true);
    std::shared_ptr<SDockOverlay> dropOverlay_;
    std::shared_ptr<SDockOverlay> mainDropOverlay_;   // resetDropOverlay 가 만든 것(메인 루트)
    IWindow*                      pointerWindow_ = nullptr;
    IWindow* inputWindow() const { return pointerWindow_ ? pointerWindow_ : deps_.ctx->window; }

    // 글 · 커서 · 선택은 HTextEditState(글자 체계 T1). 검색어는 searchEdit_.text 입니다.
    HTextEditState searchEdit_;
    bool        searchFocused_ = false;

    bool        renaming_     = false;
    bool        prevRenaming_ = false;
    HTextEditState renameEdit_;

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
