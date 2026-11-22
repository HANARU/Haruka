#pragma once
#include "EditorUiKit.hpp"
#include "EditorContext.hpp"
#include "EditorMenuModel.hpp"
#include "TextInputController.hpp"
#include "PropertyTextEdit.hpp"
#include "ColorPickerPopup.hpp"
#include "HCommandStack.hpp"
#include "../core/HLog.hpp"
#include "../core/Transform.hpp"
#include "../scene/Reflect.hpp"
#include "../ui/slate/HSlate.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Actor;
class Component;
struct PropDesc;

// 디테일(Inspector) 패널(S6b). 선택 액터의 트랜스폼 · 컴포넌트 속성 · 컴포넌트 추가 · 프리팹 저장 UI 를 그립니다.
// 여러 패널이 함께 쓰는 상태(ctx_, cmdStack_, cachedEuler_, undoSnapshot_, prefabFiles_)는 EditorApp 이 소유하고
// 참조/포인터와 콜백으로 받습니다. EditorApp* 는 들고 있지 않습니다.
class DetailsPanel {
public:
    struct Deps {
        EditorUiKit*          ui           = nullptr;
        EditorContext*        ctx          = nullptr;
        HCommandStack*        cmdStack     = nullptr;
        glm::vec3*            cachedEuler  = nullptr;
        Transform*            undoSnapshot = nullptr;
        HLogCategory*         logCat       = nullptr;
        // 프리팹 파일을 썼음(Save as Prefab · 원본에 적용): 경로 · 쓴 글. EditorApp 이 인스턴스를 다시 펼치고 목록을 갱신합니다.
        std::function<void(const std::string&, const std::string&)> onPrefabWritten;
        // 액터 참조 칸(참조 체계 R-7)을 누르면 부릅니다: 제목 · 지금 값(GUID) · 고른 값을 받을 함수. EditorApp 이 목록 대화상자를 엽니다.
        std::function<void(const std::string&, uint64_t, std::function<void(uint64_t)>)> pickActor;
        // 프리팹 링크가 바뀜(링크 끊기 · 되돌리기): 아웃라이너 표시를 다시 만듭니다.
        std::function<void()> onPrefabLinksChanged;
        // Add Component 단추를 누름(2026-11-21): 대상 액터 · 단추 위젯. EditorApp 이 그 아래에 검색 팝업을 엽니다.
        std::function<void(Actor*, const std::shared_ptr<HWidget>&)> onOpenComponentPicker;
        // ── H3a: 속성 행 메뉴 · 열거 콤보 · 글 칸 ──
        // 행 오른쪽 메뉴를 누른 자리(디테일이 있는 창 좌표)에 엽니다(EditorContextMenu, 연 패널 = Details).
        std::function<void(glm::vec2, std::vector<EditorMenuEntry>)> openMenu;
        // 열거 콤보 드롭다운을 단추 아래에 엽니다(강조할 행 = 지금 값). 같은 메뉴(EditorContextMenu)입니다.
        std::function<void(const std::shared_ptr<HWidget>&, std::vector<EditorMenuEntry>, int)> openCombo;
        std::function<bool(const HWidget*)> isComboOpen;   // 그 단추 아래에 콤보가 열려 있는가(단추 토글)
        std::function<void()>               closeMenu;     // 디테일이 연 메뉴 · 콤보를 닫음(다시 만들 때)
        // 글 칸(String)에 키보드 포커스를 줍니다(TextInputController::setFocusWidget) · 부수 효과 없이 놓습니다(releaseQuiet).
        std::function<void(const std::shared_ptr<HWidget>&)> focusText;
        std::function<void()>                                releaseTextQuiet;
        // ── H3b: 에셋 경로 칸 · 색 선택기 · 속성 검색 ──
        // 에셋 고르기 팝업을 칸 아래에 엽니다(같은 칸이면 닫음): 칸 · 종류(PropDesc::assetType) · 지금 값 · 고른 경로를 받을 함수.
        std::function<void(const std::shared_ptr<HWidget>&, const std::string&, const std::string&,
                           std::function<void(const std::string&)>)> openAssetPicker;
        std::function<void()>                                closeAssetPicker;   // 다시 만들 때
        // 콘텐츠 브라우저에서 그 에셋을 찾아 보입니다(폴더로 가서 고름).
        std::function<void(const std::string&)>              browseToAsset;
        // 색 선택기를 견본 아래에 엽니다(같은 견본이면 확정하고 닫음). 고른 액터가 바뀌어 다시 만들 때 확정하고 닫습니다.
        std::function<void(const std::shared_ptr<HWidget>&, ColorPickerPopup::Target)> openColorPicker;
        std::function<void()>                                confirmColorPicker;
        // 검색 칸 포커스를 부수 효과 없이 놓습니다(트리를 다시 만들 때).
        std::function<void()>                                releaseSearchQuiet;
    };

    // 행 하나의 동작(H3a): 되돌리기 화살표 · 오른쪽 메뉴(Reset to Default · Copy · Paste) · 덤프가 씁니다.
    struct RowOps {
        std::string                              label;      // 행 이름(덤프 · 시험이 찾는 이름)
        PropDesc::Kind                           kind = PropDesc::Float;
        std::function<bool()>                    canReset;   // 기본값과 다름(화살표 보임 · 메뉴 켜짐)
        std::function<void()>                    reset;      // 기본값으로(Undo 한 단계)
        std::function<std::string()>             copyText;   // 값을 글로
        std::function<bool(const std::string&)> paste;      // 글을 값으로(Undo 한 단계), 모양이 틀리면 거짓
        std::function<void()>                    openCombo;  // 열거 행: 콤보 열기(덤프)
        std::function<void()>                    beginEdit;  // 글 행: 편집 시작(덤프)
        // H3b
        std::string                              english;    // 영어 원문 이름(속성 검색: 지금 문화권 이름 label 과 함께 봄)
        std::function<bool(const std::string&)> acceptsAsset;   // 에셋 행: 그 경로의 종류를 받는가(끌어 놓기)
        std::function<void(const std::string&)> setAsset;       // 에셋 행: 값 넣기(Undo 한 단계)
        std::function<void()>                    openPicker;     // 에셋 행: 고르기 팝업 열기(덤프)
        std::function<void(bool)>                openColor;      // 색 행: 색 선택기 열기(덤프, 참 = 네 성분 시험 값)
    };

    void init(Deps deps);
    std::shared_ptr<HWidget> build();

    void rebuild();
    void flushRebuild();
    void markDirty() { dirty_ = true; }

    void onActorDestroyed(Actor* a);
    // ComponentRegistry::entries()[entryIndex] 타입을 붙입니다(Undo 가능). 이미 있으면 아무것도 하지 않습니다.
    void addComponentUndoable(Actor* a, int entryIndex);
    // 마지막으로 만든 Add Component 단추(덤프가 팝업 자리를 정할 때). 없으면 null.
    std::shared_ptr<HWidget> addComponentButton() const { return addCompBtn_.lock(); }
    bool scrollContains(const glm::vec2& p) const;

    // ── 속성 행 메뉴(H3a) ── p 가 속성 행 위면 그 행의 오른쪽 메뉴를 엽니다(디테일이 있는 창 좌표). 열었으면 참.
    bool openRowMenuAt(glm::vec2 p);
    // 메뉴를 연 행(명령 Details.ResetToDefault · CopyValue · PasteValue 가 씀). 다음에 메뉴를 열거나 패널을 다시 만들 때까지 남습니다.
    bool menuRowCanReset() const { return menuRow_ && menuRow_->canReset && menuRow_->canReset(); }
    bool menuRowCanPaste() const { return menuRow_ && (bool)menuRow_->paste; }
    bool menuRowCanCopy() const  { return menuRow_ && (bool)menuRow_->copyText; }
    void resetMenuRow()          { if (menuRowCanReset()) menuRow_->reset(); }
    std::string menuRowText() const { return menuRowCanCopy() ? menuRow_->copyText() : std::string(); }
    bool pasteMenuRow(const std::string& text) { return menuRowCanPaste() && menuRow_->paste(text); }

    // ── 열 나눔(H3a) ── 이름 열 폭은 패널 전체가 함께 씁니다(다시 만들어도 그대로).
    const std::shared_ptr<HPropertyColumns>& columns() const { return cols_; }

    // ── 글 칸(H3a) ── TextInputController 에 등록하는 글 칸 하나(편집 중인 행이 이 칸을 씀).
    std::shared_ptr<HWidget> stringWidget() const { return strText_; }
    TextInputField stringField();

    // ── 덤프 · 시험(H3a) ── 행 이름으로 찾습니다(같은 이름이 여럿이면 처음 행).
    const RowOps* findRow(const std::string& label) const;
    bool dumpSetValue(const std::string& label, const std::string& text);   // 붙여넣기와 같음
    bool dumpOpenCombo(const std::string& label);
    bool dumpOpenRowMenu(const std::string& label);
    bool dumpBeginEdit(const std::string& label);
    bool dumpOpenAssetPicker(const std::string& label);
    bool dumpOpenColorPicker(const std::string& label, bool rgbaDemo);
    bool dumpDropAsset(const std::string& label, const std::string& path);   // 그 행 가운데에 놓기(dropAsset 과 같은 길)

    // ── 속성 검색(H3b) ── 패널 위 검색 칸. 검색어는 선택이 바뀌어도 남습니다(UE 와 같음). Esc = 지우고 나감.
    std::shared_ptr<HWidget> searchWidget() const { return searchKeep_; }
    TextInputField searchField();
    HTextEditState& searchEdit() { return searchEdit_; }
    void setSearch(const std::string& text) { searchEdit_.setText(text); dirty_ = true; }
    const std::string& shownSearch() const { return shownSearch_; }
    int  searchMatches() const { return searchMatches_; }   // 마지막으로 만든 목록에서 맞은 행 수(검색 중일 때)

    // ── 에셋 끌어 놓기(H3b) ── p = 디테일이 있는 창 좌표. 에셋 행 위인가 · 그 경로를 받는가 · 놓기(Undo 한 단계).
    bool overAssetRow(glm::vec2 p) const { return assetRowAt(p) != nullptr; }
    bool canDropAsset(glm::vec2 p, const std::string& path) const;
    bool dropAsset(glm::vec2 p, const std::string& path);
    // 끄는 동안 칸 테두리 강조(받음 = 강조색, 안 받음 = 빨강). 바뀌면 다시 만듭니다. null = 강조 없음.
    void setDropHover(glm::vec2 p, const std::string& path, bool active);

private:
    float s(float x) const { return deps_.ui->s(x); }
    void buildTransformSection(Actor& sel);
    void buildComponentsSection(Actor& sel);
    void buildComponentHeader(Actor& sel, Component& comp);
    void buildComponentProp(Actor& sel, Component& comp, const PropDesc& pd,
                            const std::function<void()>& compBegin,
                            const std::function<void()>& compEnd,
                            const std::function<void(const std::function<void()>&)>& commitClick,
                            const std::vector<Component*>& peers);
    void buildAddComponentSection(Actor& sel);
    void buildSavePrefabRow(Actor& sel);
    // 링크된 프리팹(Docs/Linked_Prefabs.md D9): 원본 · 덮어쓰기 수 · 되돌리기 · 원본에 적용 · 링크 끊기.
    void buildPrefabSection(Actor& sel);
    // 지금 고른 소속 액터에서 path 로 시작하는 덮어쓰기가 있는지(build 때 한 번 구함).
    bool overridden(const std::string& prefix) const;

    // 속성 행 하나(SPropertyRow, H3a): [이름 | 값][되돌리기]. 모든 종류가 이 틀을 씁니다(B10). into 가 비면 list_.
    std::shared_ptr<SPropertyRow> addPropRow(const std::string& name, glm::vec4 nameColor, std::shared_ptr<HWidget> value,
                                             std::shared_ptr<RowOps> ops, SVerticalBox* into = nullptr);
    // 종류별 값 칸
    std::shared_ptr<HWidget> numberField(std::function<float()> getter, std::function<void(float)> setter, float step,
                                         ENumberUnit unit, glm::vec4 barColor, std::function<void()> bFn, std::function<void()> eFn);
    std::shared_ptr<SVectorField> vectorField(const PropDesc& pd, std::function<float(int)> get, std::function<void(int, float)> set,
                                              std::function<void()> bFn, std::function<void()> eFn);
    std::shared_ptr<HWidget> colorField(const PropDesc& pd, std::function<float(int)> get, std::function<void(int, float)> set,
                                        std::function<void()> bFn, std::function<void()> eFn,
                                        std::function<void(const std::shared_ptr<HWidget>&)> openPicker, const std::shared_ptr<RowOps>& ops);
    std::shared_ptr<HWidget> comboField(const PropDesc& pd, std::function<void(int)> choose, const std::shared_ptr<RowOps>& ops);
    std::shared_ptr<HWidget> actorRefField(const PropDesc& pd, std::function<void(uint64_t)> choose);
    std::shared_ptr<HWidget> checkField(std::function<bool()> getB, std::function<void(bool)> setB);
    std::shared_ptr<HWidget> stringValue(const std::string& key, std::function<std::string()> get,
                                         std::function<void(const std::string&)> commit, const std::shared_ptr<RowOps>& ops);
    std::shared_ptr<HWidget> assetPathValue(const PropDesc& pd, std::function<void(const std::string&)> setPath,
                                            const std::shared_ptr<RowOps>& ops);
    const RowOps* assetRowAt(glm::vec2 p) const;
    bool searching() const;
    bool rowMatches(const std::string& shown, const std::string& english) const;
    std::vector<EditorMenuEntry> rowMenuEntries() const;

    Deps deps_;
    std::shared_ptr<SVerticalBox> list_;
    std::shared_ptr<SScrollBox>   scroll_;
    bool   dirty_           = false;
    Actor* actor_           = nullptr;
    std::weak_ptr<HWidget> addCompBtn_;
    bool   txExpanded_      = true;
    std::vector<std::string> overrides_;   // 고른 소속 액터의 덮어쓴 경로(build 때 구함)

    // H3a: 열 나눔(패널 전체 공유) · 행 판정 · 메뉴를 연 행
    std::shared_ptr<HPropertyColumns> cols_ = std::make_shared<HPropertyColumns>();
    std::vector<std::pair<std::weak_ptr<HWidget>, std::shared_ptr<RowOps>>> rowHits_;
    std::shared_ptr<RowOps> menuRow_;
    // H3a: 글 칸. 편집 중인 행 하나만 이 위젯을 씁니다(HReuse 로 같은 객체라 다시 만들어도 포커스가 이어짐).
    std::shared_ptr<SEditableText> strText_ = HNew(SEditableText)->focusable(true);
    PropertyTextEdit               str_;               // 편집 상태(확정 · 취소 규칙, 시험: tests/TextTest.cpp)
    bool                           strShown_ = false;  // 지금 트리에 편집 칸이 있는가

    // H3b: 속성 검색 칸(패널 위, 목록과 따로라 다시 만들어도 같은 객체)
    std::shared_ptr<SSearchBox> searchKeep_ = HNew(SSearchBox)->focusable(true);
    HTextEditState              searchEdit_;
    bool                        searchFocused_ = false;
    std::string                 shownSearch_;      // 지금 목록을 만든 검색어
    int                         searchMatches_ = 0;
    // H3b: 끌어 놓기 강조(행 이름 · 받는가)
    std::string                 dropHoverRow_;
    bool                        dropHoverOk_ = false;
    // H3b: 네 성분 색 선택기 시험 값(덤프 전용, 실제 RGBA 속성이 아직 없음)
    glm::vec4                   demoColor_{ 0.20f, 0.55f, 0.90f, 0.60f };
};
