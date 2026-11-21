#pragma once
#include "ContentBrowserModel.hpp"
#include "EditorContextMenu.hpp"
#include "EditorUiKit.hpp"
#include "EditorContext.hpp"
#include "HCommandStack.hpp"
#include "TextInputController.hpp"
#include "../core/HLog.hpp"
#include "../ui/slate/HSlate.hpp"
#include "../ui/slate/HTextEdit.hpp"
#include <chrono>
#include <functional>
#include <set>
#include <string>
#include <vector>

class Actor;

// 하단 Content Browser 탭(S6c → 2026-11-21 UE 방식으로 다시 짬).
//  - 왼쪽: Content 폴더를 디스크 그대로 보이는 폴더 트리. 누르면 그 폴더로 갑니다.
//  - 위: 뒤로 · 앞으로, 경로(Content > models > hands, 마디를 누르면 그리로), 검색, 보기 바꾸기(타일 · 목록), 종류 거르기.
//  - 가운데: 지금 폴더의 하위 폴더와 에셋. 검색 · 거르기가 있으면 하위 폴더까지 찾습니다.
//  - 아래: 항목 수 · 고른 것.
// 누르는 규칙(UE 와 같음): 한 번 누름 = 고르기, 두 번 누름 = 실행(폴더 들어가기 · 씬 열기 · 모델/프리팹을 뷰포트 앞에 놓기 ·
// 소리 미리 듣기), 오른쪽 누름 = 메뉴. 예전에는 종류별 고정 폴더 넷의 목록을 늘어놓고 한 번 누르면 바로 실행했습니다.
// 위젯 콜백은 플래그만 세우고 다시 만들기는 프레임 끝(flushRebuild)에서 합니다.
class ContentBrowserPanel {
public:
    struct Deps {
        EditorUiKit*   ui       = nullptr;
        EditorContext* ctx      = nullptr;
        HCommandStack* cmdStack = nullptr;
        HLogCategory*  logCat   = nullptr;
        std::function<std::string()>                 currentScenePath;
        std::function<void(const std::string&)>      onOpenScene;
        std::function<void(Actor*)>                  onSelectOnly;
        std::function<void(const std::vector<Actor*>&)> onRecordCreation;
        std::function<bool(const Actor*)>            isEditorCam;
        std::function<void()>                        onDetailsDirty;
        // 참조 · 이름 바꾸기 · 옮기기 대화상자(참조 체계 A-5).
        std::function<void(const std::string&)>      onAssetActions;
        // 2026-11-21
        std::function<glm::vec3()>                   placePosition;     // 두 번 눌러 놓을 자리(뷰포트 카메라 앞)
        std::function<void(const std::string&)>      onPreviewSound;    // 소리 · 큐 미리 듣기
        std::function<void(EFocusTarget)>            setFocus;          // 검색 칸 포커스
        EditorContextMenu*                           contextMenu = nullptr;
        bool                                         dumpMode    = false;   // 탐색기 열기 같은 바깥 동작을 하지 않음
    };

    void init(Deps deps);
    void scanAll();
    void scanScenes() { scanAll(); }
    void refreshPrefabs() { scanAll(); }

    const std::vector<std::string>& sceneFiles() const { return sceneFiles_; }
    void buildTab(SVerticalBox& content);
    // 위젯 참조를 놓습니다. 다른 탭으로 가면(stayOnTab 거짓) 검색 포커스도 풉니다(보이지 않는 칸으로 글이 들어가지 않게).
    void releaseTab(bool stayOnTab);
    void tick(bool tabActive);          // 폭이 바뀌면 타일 줄 나눔을 다시 함
    void flushRebuild();

    TextInputField searchField();
    HTextEditState& searchEdit() { return searchEdit_; }
    bool contains(glm::vec2 p) const;           // 트리 · 에셋 보기 위
    bool openContextMenuAt(glm::vec2 p);        // 오른쪽 누름. 처리했으면 참

    // 탐색 · 고르기 · 실행(덤프 · 시험도 부름)
    void navigate(const std::string& folder, bool pushHistory = true);
    void goBack();
    void goForward();
    void select(const std::string& path);
    void activate(const std::string& path);    // 두 번 누름과 같음
    void setTileView(bool tiles);
    void setSearch(const std::string& text);
    void toggleFilter(hcb::AssetKind k);
    std::vector<EditorContextMenu::Item> contextItems(const std::string& path);   // 빈 경로 = 빈 자리

    // 끌어 놓기(2026-11-21, UE 와 같음): 에셋 보기의 항목을 누른 채 6 px 넘게 움직이면 끌기가 시작됩니다. EditorApp 이 마우스로
    // 부르고, 뷰포트 위에서 놓으면 dropInViewport 를 부릅니다. Esc 는 cancelDrag.
    void pointerPress(glm::vec2 p);
    void pointerMove(glm::vec2 p, bool leftDown);
    bool dragging() const { return dragging_; }
    const std::string& dragPath() const { return dragPath_; }
    void cancelDrag();
    void forceDrag(const std::string& path);   // 덤프: 그 항목을 끌고 있는 상태로
    bool canDropInViewport(const std::string& path) const;   // 모델 · 프리팹 · 소리 · 큐
    // 모델 · 프리팹 = 그 자리에 놓기, 소리 · 큐 = 닿은 소리 액터의 소리 바꾸기(그 밖은 그 자리에 소리 액터). 놓지 못하면 false.
    bool dropInViewport(const std::string& path, const glm::vec3& point, Actor* hitActor);
    std::shared_ptr<SBorder> resetDragHost();
    void updateDragVisual(glm::vec2 p, bool canDrop);   // 커서 옆 이름표(끌기가 끝나면 지움)

    const std::string& currentFolder() const { return folder_; }
    const std::string& selected() const { return selected_; }
    const hcb::ContentIndex& index() const { return index_; }

private:
    float s(float x) const { return deps_.ui->s(x); }
    void markAll() { toolbarDirty_ = treeDirty_ = viewDirty_ = filterDirty_ = true; }
    void onItemClick(const std::string& path);
    void placeModel(const std::string& path, const glm::vec3* at = nullptr);
    void placePrefab(const std::string& path, const glm::vec3* at = nullptr);
    void assignSound(const std::string& path, Actor* target = nullptr);   // target 이 없으면 고른 액터
    void spawnSoundActor(const std::string& path, const glm::vec3& at);
    void showInExplorer(const std::string& path);
    void copyPath(const std::string& path);

    void rebuildToolbar();
    void rebuildFilterBar();
    void rebuildTree();
    void addTreeRows(const std::string& folder, int depth);
    void rebuildView();
    std::shared_ptr<HWidget> makeTile(const hcb::Item& it, float w, float h);
    std::shared_ptr<HWidget> makeRow(const hcb::Item& it, bool showFolder);

    Deps deps_;
    hcb::ContentIndex        index_;
    std::vector<std::string> sceneFiles_;
    std::string              folder_ = hcb::kRoot;
    std::vector<std::string> back_, forward_;
    std::string              selected_;
    std::set<std::string>    expanded_ = { hcb::kRoot };
    uint32_t                 filterMask_ = 0;
    bool                     tiles_ = true;
    HTextEditState           searchEdit_;
    std::string              shownSearch_;   // 지금 보기를 만든 검색어
    bool                     searchFocused_ = false;
    std::string              lastClick_;
    std::chrono::steady_clock::time_point lastClickAt_{};
    int                      viewWidthSig_ = -1;
    size_t                   viewCount_ = 0;

    bool toolbarDirty_ = false, treeDirty_ = false, viewDirty_ = false, filterDirty_ = false;
    std::shared_ptr<SBorder>      toolbarHost_, filterHost_;
    std::shared_ptr<SSearchBox>   searchBox_;
    std::shared_ptr<SVerticalBox> treeBox_, viewBox_;
    std::shared_ptr<SScrollBox>   treeScroll_, viewScroll_;
    // 오른쪽 누름 판정용: 지금 그린 항목 위젯과 그 경로
    std::vector<std::pair<std::string, std::weak_ptr<HWidget>>> hitItems_;
    std::string hitItemAt(glm::vec2 p, bool viewOnly) const;
    // 끌기
    std::string               pressPath_;     // 누른 항목(끌기 후보)
    glm::vec2                 pressPos_{ 0.f };
    bool                      dragging_ = false;
    std::string               dragPath_;
    std::shared_ptr<SBorder>  dragHost_;
    bool                      dragShown_ = false;
};
