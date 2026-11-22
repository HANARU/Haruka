#include "EditorApp.hpp"
#include "../runtime/EngineOptions.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../math/Geometry.hpp"
#include "../scene/ComponentRegistry.hpp"
#include "../core/FrameTimings.hpp"
#include "../core/AppPaths.hpp"
#include "../core/ProjectPaths.hpp"
#include "EditorCommands.hpp"
#include "../renderer/Renderer.hpp"
#include "../scene/World.hpp"
#include "../scene/Actor.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../renderer/MeshHandle.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/LightComponent.hpp"
#include "../audio/AudioComponent.hpp"
#include "../physics/RigidBodyComponent.hpp"
#include "../assets/AssetManager.hpp"
#include "../sceneio/PrefabManager.hpp"
#include "../sceneio/PrefabRules.hpp"
#include "../sceneio/SceneSerializer.hpp"
#include "../platform/IWindow.hpp"
#include "../input/HInputSystem.hpp"
#include "../components/XrComponents.hpp"
#include "../core/DebugDraw.hpp"
#include "../core/CVar.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// ── Helpers ────────────────────────────────────────────────────────────────────
static glm::vec3 quatToEulerDeg(const glm::quat& q) {
    return glm::degrees(glm::vec3(glm::pitch(q), glm::yaw(q), glm::roll(q)));
}

namespace fs = std::filesystem;

static constinit HLogCategory LogEditor{ "Editor", ELogVerbosity::Log };

// ── init ───────────────────────────────────────────────────────────────────────
void EditorApp::subscribeWorldEvents(World& world) {
    // 액터 파괴를 구조적으로 감시합니다. 어떤 파괴 경로(삭제 버튼·단축키·씬 로드·
    // 향후 재귀 파괴)든 이 구독 하나로, 그 액터를 참조하는 undo/redo 커맨드를
    // 걷어내고 선택 포인터가 그 액터를 가리키고 있었다면 비웁니다. 각 호출부가
    // cmdStack_.clear() 를 기억해야 했던 규율을 이벤트 메커니즘으로 대체합니다.
    actorDestroyedSub_ = world.onActorDestroyed.add([this](Actor* a) {
        cmdStack_.purge(a);
        // 파괴는 erase 앞에서 방송되므로 여기서 a->id 를 읽는 것은 안전합니다.
        // 서브트리 재귀 파괴 시 자손마다 따로 방송되어 접힘 상태가 남지 않습니다.
        if (prevSelected2_ == a) prevSelected2_ = nullptr;
        // 디테일 패널이 이 액터를 보여 주고 있었다면 다시 만들어야 합니다. 지금 여기서
        // 만들지 않는 이유는 이 방송이 위젯 콜백(아웃라이너 x·Stop) 안에서도 오기 때문입니다.
        detailsPanel_.onActorDestroyed(a);
        ctx_.selection.erase(std::remove(ctx_.selection.begin(), ctx_.selection.end(), a), ctx_.selection.end());
        if (ctx_.selected == a)
            ctx_.selected = ctx_.selection.empty() ? nullptr : ctx_.selection.back();
        outlinerPanel_.onActorDestroyed(a);
    });
}

void EditorApp::init(Renderer& renderer, World& world, AssetManager& assets, IWindow& window) {
    ctx_.world    = &world;
    ctx_.assets   = &assets;
    ctx_.renderer = &renderer;
    ctx_.window   = &window;
    world.setService<hprefablink::PrefabSourceCache>(&prefabCache_);

    subscribeWorldEvents(world);

    auto beginCmd = [&renderer]() { return renderer.beginOneTimeCmd(); };
    auto endCmd   = [&renderer](VkCommandBuffer cmd) { renderer.endOneTimeCmd(cmd); };
    // 글꼴(D4): 엔진 콘텐츠의 Pretendard(보통 · 굵게, OFL). 없으면 아틀라스가 시스템 글꼴로 내려갑니다. 한글 글리프가 들어가도록
    // 에디터 아틀라스는 2048 x 1024 입니다(게임 쪽 3D 패널은 기본 크기).
    slateRenderer_.fontAtlas().setAtlasHeight(1024);
    // 슬롯 정점 버퍼: 에디터는 메인 오버레이 · 월드 패널 · 보조 OS 창이 한 슬롯 버퍼에 이어 씁니다(다중 OS 창 M2, 근거는
    // Docs/Editor_MultiWindow_Design.md 6절 M2). 게임 실행 파일의 패널 UI 는 기본 크기 그대로입니다.
    slateRenderer_.setSlotVertexCapacity(kEditorSlotVerts);
    slateRenderer_.init(renderer.getDevice(), renderer.getPhysDevice(),
                        renderer.getSwapFormat(), renderer.pushDescriptorFn(), beginCmd, endCmd,
                        "/Engine/Fonts/Pretendard-Regular.ttf");
    if (slateRenderer_.fontAtlas().loadBoldFont("/Engine/Fonts/Pretendard-SemiBold.ttf"))
        HLOG(LogEditor, Log) << "Editor font: Pretendard (regular, semibold)";
    else
        HLOG(LogEditor, Warning) << "Editor font: Pretendard not found in /Engine/Fonts; using the system font";

    // 아이콘(D3): /Engine/Editor/Icons 의 SVG 를 읽어 글꼴 아틀라스에 붙입니다. 못 읽으면 위젯이 글자로 그립니다.
    icons_.load();
    slateRenderer_.fontAtlas().setIconSource(&icons_);
    ui_.fonts   = &slateRenderer_.fontAtlas();
    ui_.uiScale = uiScale_;

    viewport_.init({
        &ctx_, &cmdStack_, &visualizers_,
        &gizmoMode_, &worldSpace_, &snap_, &camSpeed_,
        [this] { return (bool)modal_; },
        [this] { return menus_.isOpen(); },
        [this] { return dock_.contains("Viewport"); },
        [this] { return currentFocus() == EFocusTarget::None; },
        [this] { return vpToolbar_.fbRect(); },
        // 메인 문맥 팝업 층의 열린 팝업 영역(H2a, 예전 로그 · Add Component · 오른쪽 메뉴 popupContains 셋). 메뉴는 위 isMenusOpen 이 막음.
        [this](glm::vec2 p) { return slateRenderer_.context().popups().contains(p); },
        [this](const Actor* a) { return isSelected(a); },
        [this](Actor* a) { selectOnly(a); },
        [this](Actor* a) { toggleSelect(a); },
        [this] { syncEulerCache(); },
        [this] { beginGroupDrag(); },
        [this] { updateGroupDrag(); },
        [this] { groupDrag_.clear(); },
        [this](const Transform& before, const Transform& after) { commitGizmoDrag(before, after); },
        [this] { return menuKeyCapture_; }   // 열린 메뉴가 키를 받는 동안 Q · W · E · R 막기(H2b)
    });
    viewport_.spawnEditorCamera(world);

    // 명령 레지스트리(H2b): 메뉴 · 툴바 · 단축키가 같은 명령 id 를 부릅니다. 패널 · 메뉴를 만들기 전에 정의 · 동작을 채웁니다.
    registerCommands();
    initPanels();

    contentPanel_.scanAll();
    logPanel_.loadFilter();   // 첫 syncCategories 보다 먼저. 숨긴 카테고리는 등록되는 순간 적용됩니다
    buildWidgetTree();
    outlinerPanel_.rebuild();
    detailsPanel_.rebuild();
    rebuildBottomPanel();

    renderer.setUIRenderer(&slateRenderer_);
    // 떠 있는 도크 창(M3a): 렌더러 대상은 UI 렌더러를 등록한 뒤에만 만들 수 있으므로 여기서 처음 맞춥니다.
    initFloatWindows();
    syncFloatWindows();
    hotReloader_.start(happ::inExeDir("shaders").string(), renderer);
    startAssetWatch();
    HLOG(LogEditor, Log) << "Ready. Press Play to run game simulation.";
}

void EditorApp::initPanels() {
    EditorDock::Deps dockDeps{
        &ui_, &panelReg_, &LogEditor, &uiLayout_,
        [] { return happ::inWriteRoot("Config/editor_layout.txt").string(); },
        [] { std::error_code ec; fs::create_directories(happ::inWriteRoot("Config"), ec); },
        [this](bool outlinerVis, bool bottomVis, bool detailsVis) {
            outlinerVisible_ = outlinerVis;
            const EFocusTarget f = currentFocus();
            if ((!outlinerVis && (f == EFocusTarget::Search || f == EFocusTarget::Rename)) ||
                (!bottomVis   && (f == EFocusTarget::LogSearch || f == EFocusTarget::Console || f == EFocusTarget::ContentSearch)) ||
                (!detailsVis  && (f == EFocusTarget::NumberField || f == EFocusTarget::TextField)))
                setTextFocus(EFocusTarget::None);
            if (!bottomVis) logPanel_.closePopup();
            if (!detailsVis) { componentPicker_.close(); assetPicker_.close(); colorPicker_.confirm(); }
            // 오른쪽 메뉴 · 콤보는 연 패널이 닫히면 닫습니다(H3a: 디테일 행 메뉴 · 열거 콤보도 이 메뉴).
            if ((!bottomVis && contextMenu_.panel() == "Bottom") || (!detailsVis && contextMenu_.panel() == "Details"))
                contextMenu_.close();
            // 팝업은 그 패널이 있는 창 문맥의 팝업 층에 있습니다(H2a). 패널이 다른 창으로 옮겨 가면 옛 창에 남은 팝업을 닫습니다.
            closePopupsLeftBehind();
        },
        [this] { outlinerPanel_.cancelDrag(); },
        [this](int winId) { raiseWindow(winId); },
        [this](int floatId, int& x, int& y, int& w, int& h) {
            hwinplace::Rect r;
            if (!floatWindows_.readRect(floatId, r)) return false;
            x = r.x; y = r.y; w = r.w; h = r.h;
            return true;
        }
    };
    fillDockWindowDeps(dockDeps);   // 창을 넘는 탭 끌기(M4): 화면 자리 · 앞뒤 순서 · 작업 영역 끼우기
    dock_.init(std::move(dockDeps));
    menus_.init(&ui_, kMenuBarH, &slateRenderer_.context(), &commands_);
    // 한 창에서 팝업을 열면 다른 창의 팝업은 닫습니다(H2a, UE 도 메뉴는 한 번에 하나). 떠 있는 창 문맥은 만들 때 같은 훅을 겁니다.
    slateRenderer_.context().popups().setPushHook([this](HPopupLayer::Handle) { dismissPopupsExcept(0); });
    // 같은 창 안에서도 루트 팝업은 한 번에 하나입니다(H2b, UE FMenuStack): 새 팝업이 뜨면 그 창의 다른 팝업(과 하위)은 닫힙니다.
    slateRenderer_.context().popups().setSingleRoot(true);
    toolbar_.init({
        &ui_, &ctx_,
        [this] { return playState_ == EPlayState::Playing; },
        [this] { return playState_ == EPlayState::Paused;  },
        // 툴바 단추도 명령 레지스트리의 같은 명령을 부릅니다(H2b, 모양은 그대로).
        [this] { commands_.execute("Play.Play"); },
        [this] { commands_.execute("Play.Pause"); },
        [this] { commands_.execute("Play.Stop"); },
        [this] { commands_.execute("File.SaveScene"); },
        [this] { commands_.execute("File.OpenScene"); },
        [this] { commands_.execute("Content.Refresh"); }
    });
    vpToolbar_.init({
        &ui_, &ctx_, &gizmoMode_, &worldSpace_, &snap_, &camSpeed_,
        [this] { return viewport_.viewportFbRect().width; },
        [this](int& axis, float& v) { return viewport_.gizmo().dragReadout(axis, v); }
    });
    statusBar_.init({
        &ui_, dumpMode_,
        [this] { return currentFocus(); },
        [this] { return sceneTitle(); }
    });
    outlinerPanel_.init({
        &ui_, &ctx_, &cmdStack_, &visualizers_, &LogEditor, &outlinerW_, viewport_.cameraActorPtr(),
        [this](const Actor* a) { return a == viewport_.cameraActor(); },
        [this](const Actor* a) { return isSelected(a); },
        [this](Actor* a) { selectOnly(a); },
        [this](Actor* a) { toggleSelect(a); },
        [this](Actor* a) { selectRange(a); },
        [this] { return selectionRoots(); },
        [this](const std::vector<Actor*>& made) { recordActorCreation(made); },
        [this](Actor* a) { deleteActorUndoable(a); },
        [this] { viewport_.gizmo().cancel(); },
        [this] { detailsPanel_.markDirty(); },
        [this] {
            setTextFocus(currentFocus() == EFocusTarget::Search ? EFocusTarget::None
                                                                : EFocusTarget::Search);
        },
        [this] { textInput_.releaseQuiet(EFocusTarget::Rename); }
    });
    // 글 칸 포커스(H1a-2): 어느 칸인가는 에디터 문맥의 HSlate 포커스 위젯이 정본입니다. 누르면 포커스는 끕니다. 에디터는
    // 입력 순서(바깥 누름으로 포커스 해제 → 팝업 바깥 → … → UI, Docs/Editor_MultiWindow_Design.md 5절)와 위젯의 onClick 으로
    // 포커스를 정하는데, 히트 경로로 고르는 규칙은 지금 마우스 전달과 다를 수 있기 때문입니다(H1a-1 보고).
    slateRenderer_.context().setClickToFocus(false);
    TextInputController::Deps textDeps{
        &ctx_.window,
        [this](glm::vec2 p) { return detailsPanel_.scrollContains(p); },
        [this] { return (bool)modal_; },
        [this] { return dialogFocused_; },
        [this] {
            if (modal_ && modal_->hasInput) {
                dialogFocused_ = true;
                dialogEdit_.selectAll();
            }
        },
        [this] { dialogFocused_ = false; dialogEdit_.clearSelection(); dialogEdit_.clearComposition(); },
        [this]() -> HTextEditState* { return &dialogEdit_; },
        [this] { triggerModalDefault(); },
        [this] { triggerModalCancel(); },
        [this] { menus_.close(); },
        [this] { bringPanelForward("Outliner", outlinerVisible_); },
        [this] { return editDragActive(); },
        // 포커스 없음일 때의 Delete · Ctrl+C · Ctrl+V 는 메뉴와 같은 명령(H2b).
        [this] { commands_.execute("Edit.Delete"); },
        [this] { commands_.execute("Edit.Copy"); },
        [this] { commands_.execute("Edit.Paste"); },
        &slateRenderer_.context(),
        {
            { EFocusTarget::Rename,        [this] { return outlinerPanel_.renameWidget(); },  [this] { return outlinerPanel_.renameField(); } },
            { EFocusTarget::Search,        [this] { return outlinerPanel_.searchWidget(); },  [this] { return outlinerPanel_.searchField(); } },
            { EFocusTarget::LogSearch,     [this] { return logPanel_.searchWidget(); },       [this] { return logPanel_.searchField(); } },
            { EFocusTarget::Console,       [this] { return logPanel_.consoleWidget(); },      [this] { return logPanel_.consoleField(); } },
            { EFocusTarget::Picker,        [this] { return componentPicker_.searchWidget(); }, [this] { return componentPicker_.field(); } },
            { EFocusTarget::ContentSearch, [this] { return contentPanel_.searchWidget(); },   [this] { return contentPanel_.searchField(); } },
            // 디테일 글 칸(H3a): 이름표 없는 새 글 칸(TextField). 편집 중인 행 하나가 이 칸을 씁니다.
            { EFocusTarget::TextField,     [this] { return detailsPanel_.stringWidget(); },   [this] { return detailsPanel_.stringField(); } },
            // H3b: 디테일 속성 검색 칸 · 에셋 고르기 팝업 검색 칸 · 색 선택기 16진수 칸(모두 디테일이 있는 창).
            { EFocusTarget::TextField,     [this] { return detailsPanel_.searchWidget(); },   [this] { return detailsPanel_.searchField(); } },
            { EFocusTarget::TextField,     [this] { return assetPicker_.searchWidget(); },    [this] { return assetPicker_.field(); } },
            { EFocusTarget::TextField,     [this] { return colorPicker_.hexWidget(); },       [this] { return colorPicker_.hexField(); } },
        }
    };
    // 여러 OS 창(M3b): 키보드는 포커스 창에서 읽고, 칸은 그 칸이 있는 창에 키보드가 있을 때만 받습니다.
    textDeps.keyboardWindow = [this] { return keyboardWindowId(); };
    textDeps.windowOfFocus  = [this](EFocusTarget t) { return windowOfFocusTarget(t); };
    textDeps.windowById     = [this](int id) { return windowById(id); };
    textDeps.windowIds      = [this] { return windowIds(); };
    textDeps.isKeyboardCaptured = [this] { return menuKeyCapture_; };   // 열린 메뉴가 키를 받음(H2b)
    textInput_.init(std::move(textDeps));
    DetailsPanel::Deps detailsDeps{ &ui_, &ctx_, &cmdStack_, &cachedEuler_, &undoSnapshot_,
                         &LogEditor,
                         [this](const std::string& path, const std::string& text) {
                             refreshPrefab(path, text);
                             contentPanel_.refreshPrefabs();
                             rebuildBottomPanel();
                         },
                         [this](const std::string& title, uint64_t current, std::function<void(uint64_t)> pick) {
                             openActorPicker(title, current, std::move(pick));
                         },
                         [this] { outlinerPanel_.markDirty(); },
                         [this](Actor* a, const std::shared_ptr<HWidget>& anchor) {
                             // 팝업은 디테일이 있는 창의 팝업 층에 뜹니다(H2a, 떠 있는 창 포함).
                             componentPicker_.toggle(a, anchor);
                         } };
    // H3a: 속성 행 오른쪽 메뉴 · 열거 콤보는 오른쪽 메뉴와 같은 메뉴(EditorContextMenu, 연 패널 = Details)이고 디테일이 있는 창 문맥에 뜹니다.
    detailsDeps.openMenu = [this](glm::vec2 at, std::vector<EditorMenuEntry> entries) {
        contextMenu_.open(at, std::move(entries), "Details");
    };
    detailsDeps.openCombo = [this](const std::shared_ptr<HWidget>& anchor, std::vector<EditorMenuEntry> entries, int highlight) {
        contextMenu_.openBelow(anchor, std::move(entries), "Details", highlight);
    };
    detailsDeps.isComboOpen = [this](const HWidget* w) { return contextMenu_.isOpenBelow(w); };
    detailsDeps.closeMenu   = [this] { if (contextMenu_.isOpen() && contextMenu_.panel() == "Details") contextMenu_.close(); };
    detailsDeps.focusText   = [this](const std::shared_ptr<HWidget>& w) { textInput_.setFocusWidget(w); };
    detailsDeps.releaseTextQuiet = [this] { textInput_.releaseQuietWidget(detailsPanel_.stringWidget()); };
    // H3b: 에셋 고르기 · 콘텐츠 브라우저에서 찾기 · 색 선택기(디테일이 있는 창 문맥).
    detailsDeps.openAssetPicker = [this](const std::shared_ptr<HWidget>& anchor, const std::string& type, const std::string& current,
                                         std::function<void(const std::string&)> pick) {
        assetPicker_.toggle(anchor, type, current, std::move(pick));
    };
    detailsDeps.closeAssetPicker   = [this] { assetPicker_.close(); };
    detailsDeps.browseToAsset      = [this](const std::string& path) { browseToAsset(path); };
    detailsDeps.openColorPicker    = [this](const std::shared_ptr<HWidget>& anchor, ColorPickerPopup::Target t) {
        colorPicker_.toggle(anchor, std::move(t));
    };
    detailsDeps.confirmColorPicker = [this] { colorPicker_.confirm(); };
    detailsDeps.releaseSearchQuiet = [this] { textInput_.releaseQuietWidget(detailsPanel_.searchWidget()); };
    detailsPanel_.init(std::move(detailsDeps));
    assetPicker_.init({ &ui_,
                        [this]() -> const hcb::ContentIndex* { return &contentPanel_.index(); },
                        [this](const std::shared_ptr<HWidget>& w) { textInput_.setFocusWidget(w); },
                        [this] { textInput_.releaseQuietWidget(assetPicker_.searchWidget()); },
                        [this] { setTextFocus(EFocusTarget::None); },
                        [this] { return panelPopupContext("Details"); } });
    colorPicker_.init({ &ui_,
                        [this] { return panelPopupContext("Details"); },
                        [this](const std::shared_ptr<HWidget>& w) { textInput_.setFocusWidget(w); },
                        [this] { textInput_.releaseQuietWidget(colorPicker_.hexWidget()); },
                        [this] { setTextFocus(EFocusTarget::None); } });
    componentPicker_.init({ &ui_, &ctx_,
                            [this](EFocusTarget t) { setTextFocus(t); },
                            [this](Actor* a, int entry) { detailsPanel_.addComponentUndoable(a, entry); },
                            [this] { return detailsPanel_.addComponentButton(); },
                            [this] { textInput_.releaseQuiet(EFocusTarget::Picker); },
                            [this] { return panelPopupContext("Details"); } });
    contentPanel_.init({
        &ui_, &ctx_, &cmdStack_, &LogEditor,
        [this] { return doc_.path(); },
        [this](const std::string& path) {
            if (hvfs::toCanonical(path) == hvfs::toCanonical(doc_.path()) || blockedDuringPlay("opening a scene")) return;
            guardUnsaved(HTEXT("Editor.Dialog", "Verb.OpenAnother", "opening another scene"), [this, path] { openScene(path); });
        },
        [this](Actor* a) { selectOnly(a); },
        [this](const std::vector<Actor*>& made) { recordActorCreation(made); },
        [this](const Actor* a) { return a == viewport_.cameraActor(); },
        [this] { detailsPanel_.markDirty(); },
        [this](const std::string& path) { openAssetActions(path); },
        // 두 번 눌러 놓는 자리: 에디터 카메라 정면 광선이 닿는 면(없으면 3 m 앞).
        [this] { return viewport_.placementPoint(); },
        [this](const std::string& path) { previewSound(path); },
        [this](EFocusTarget t) { setTextFocus(t); },
        &contextMenu_,
        dumpMode_
    });
    // 콘텐츠 브라우저의 오른쪽 메뉴, 디테일 속성 행의 오른쪽 메뉴 · 열거 콤보(H3a). 연 패널이 있는 창 문맥에 뜹니다.
    contextMenu_.init(&ui_, [this](const std::string& panel) { return panelPopupContext(panel.c_str()); }, &commands_);
    gpuStatsPanel_.init(&ui_, &ctx_, &vpTimings_, dumpMode_);
    logPanel_.init(&ui_, &LogEditor, kMenuBarH, dumpMode_, [this] {
        setTextFocus(currentFocus() == EFocusTarget::LogSearch ? EFocusTarget::None
                                                               : EFocusTarget::LogSearch);
    });
    // Categories 팝업은 로그 패널이 있는 창의 팝업 층에 뜹니다(H2a).
    logPanel_.setPopupContext([this] { return panelPopupContext("Bottom"); });
    // 콘솔 줄(Docs/Gameplay_Tools.md D9): 엔진의 CVar 레지스트리(월드 서비스)로 실행하고 결과는 Console 분류 로그로 남깁니다.
    logPanel_.setConsole(
        [this](const std::string& line) -> std::string {
            CVarRegistry* cv = ctx_.world ? ctx_.world->service<CVarRegistry>() : nullptr;
            if (!cv) {
                HLOG(LogConsole, Warning) << "Console is not available (no CVar registry)";
                return {};
            }
            return cv->executeLogged(line);
        },
        [this] { setTextFocus(EFocusTarget::Console); });
}

void EditorApp::cleanup() {
    dock_.saveLayout();   // 최종 레이아웃(활성 탭 포함)을 종료 시점에 영속화
    // 월드가 에디터보다 오래 살 수 있으므로, this 를 캡처한 리스너를 먼저 뗍니다.
    if (ctx_.world) ctx_.world->onActorDestroyed.remove(actorDestroyedSub_);
    viewport_.cleanup(ctx_.world);
    if (ctx_.world && ctx_.world->service<hprefablink::PrefabSourceCache>() == &prefabCache_)
        ctx_.world->setService<hprefablink::PrefabSourceCache>(nullptr);
    hotReloader_.stop();
    assetWatcher_.stop();
    // 떠 있는 창: 배치는 위에서 저장했고(그때 OS 창 자리를 읽음), 보조 창 프레임버퍼가 UI 렌더러의 창 패스를 쓰므로 그보다 먼저 없앱니다.
    floatWindows_.destroyAll();
    // UI 파이프라인 · 버퍼를 지우기 전에 마지막 프레임 제출이 끝나기를 기다립니다. 예전에는 보조 창 정리(removeWindowTarget 의 장치
    // 대기)가 있을 때만 우연히 기다려, 떠 있는 창 없는 Debug 자동 실행이 종료 중 검증 오류 6개(사용 중인 파이프라인 · 버퍼 · 프레임버퍼
    // 파괴)로 exit 3 을 냈습니다(M3a 확인 중 발견).
    if (ctx_.renderer && ctx_.renderer->getDevice() != VK_NULL_HANDLE) vkDeviceWaitIdle(ctx_.renderer->getDevice());
    slateRenderer_.cleanup();
}

// ── tick ───────────────────────────────────────────────────────────────────────
void EditorApp::tickStats() {
    // FPS 카운터. 클램프된 dt 가 아니라 실시간으로 셉니다.
    ++fpsCount_;
    {
        const auto  nowT  = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(nowT - fpsMark_).count();
        if (elapsed >= 1.f) {
            vpTimings_.fps = (int)(fpsCount_ / elapsed);

            const uint64_t rc = gFrameTimings().recreateCount;
            vpTimings_.recreatePerSec = (rc - prevRecreate_) / elapsed;
            prevRecreate_ = rc;

            fpsCount_ = 0;
            fpsMark_  = nowT;
        }
    }

    // GPU timings from renderer
    if (ctx_.renderer) {
        auto t = ctx_.renderer->getTimings();
        auto s = ctx_.renderer->getFrameStats();
        vpTimings_.shadowMs        = t.shadowMs;
        vpTimings_.mainMs          = t.mainMs;
        vpTimings_.aoMs            = t.aoMs;
        vpTimings_.bloomMs         = t.bloomMs;
        vpTimings_.tonemapMs       = t.tonemapMs;
        vpTimings_.uiMs            = t.uiMs;
        vpTimings_.totalMs         = t.totalMs;
        vpTimings_.drawCalls       = s.drawCalls;
        vpTimings_.instances       = s.instances;
        vpTimings_.lights          = s.lights;
        vpTimings_.lightsTotal     = s.lightsTotal;
        vpTimings_.culled          = s.culled;
        vpTimings_.transparentDraws= s.transparentDraws;
        vpTimings_.dropped         = s.dropped;
    }
}

void EditorApp::tickSelectionSync(int actorCount) {
    bool selChanged = (ctx_.selected != prevSelected_);

    // 주 선택은 그대로인데 선택 집합만 바뀌면(뷰포트 Ctrl+클릭, 아웃라이너 Ctrl · Shift+클릭) 아웃라이너 강조와 디테일의
    // "N actors selected" 배지가 낡습니다. 선택 집합의 서명(개수 + ID)을 앞 프레임과 비교해 다시 만듭니다(DEF-135).
    {
        uint64_t sig = 1469598103934665603ull ^ ctx_.selection.size();
        for (const Actor* s : ctx_.selection) sig = (sig ^ (s ? s->id : 0)) * 1099511628211ull;
        if (sig != prevSelSig_) {
            prevSelSig_    = sig;
            outlinerPanel_.markDirty();
            if (!selChanged) detailsPanel_.markDirty();   // 주 선택이 바뀌면 아래에서 어차피 다시 만듭니다
        }
    }
    // 프리팹 인스턴스 안 액터를 고친 편집(커맨드)이 쌓이면 디테일의 덮어쓰기 표시를 다시 구합니다(Docs/Linked_Prefabs.md D9).
    if (cmdStack_.stateToken() != prevCmdToken_) {
        prevCmdToken_ = cmdStack_.stateToken();
        if (ctx_.selected && hprefablink::instanceRootOf(ctx_.selected)) detailsPanel_.markDirty();
    }

    // 리네임 해제를 리빌드 **앞**으로 옮깁니다. 뒤에 두면 선택이 바뀌는 프레임에 새 행이
    // 직전 액터의 이름 칸(renameEdit_)을 한 프레임 그대로 보여 줍니다.
    if (selChanged && outlinerPanel_.isRenaming()) setTextFocus(EFocusTarget::None);
    // 접힌 부모 아래 자식을 뷰포트에서 픽하면 조상을 펼치고 그 행으로 스크롤합니다.
    if (selChanged && ctx_.selected) { outlinerPanel_.revealActor(ctx_.selected); outlinerPanel_.revealSelected(); }

    bool renamChanged = outlinerPanel_.renamingChanged();
    outlinerPanel_.checkLayoutSize();

    if (actorCount != prevActorCount_ || selChanged || renamChanged)
        outlinerPanel_.markDirty();
    if (outlinerPanel_.flushRebuild())
        prevActorCount_ = actorCount;
    outlinerPanel_.syncPrevRenaming();

    if (selChanged) {
        syncEulerCache();
        detailsPanel_.rebuild();
        prevSelected2_ = prevSelected_;
        prevSelected_  = ctx_.selected;
    }
}

void EditorApp::tick(float dt) {
    if (!ctx_.world) return;
    if (layoutStatsMode_) sampleLayoutStats();

    // 플레이 시작·끝 처리(게임 모듈)에서 표시 대상이 파괴됐다면, 아래 tickRenameInput 의
    // 숫자 칸 확정이 낡은 접근자를 부르기 전에 디테일 패널부터 다시 만듭니다(DEF-92).
    detailsPanel_.flushRebuild();

    if (hotReloader_.reloadPending() && ctx_.renderer)
        ctx_.renderer->reloadPipelines();
    tickAssetReload();

    tickStats();

    // 휠은 프로세스 전체에서 이 한 줄로만 읽습니다(getScrollDelta 는 consume-on-read라
    // 두 곳에서 읽으면 한쪽이 항상 0 을 받습니다).
    float scroll = ctx_.window ? ctx_.window->getScrollDelta() : 0.f;
    // RMB 비행 중 휠은 돌리가 아니라 이동 속도 조절입니다. 카메라 모드를 놓으면
    // 종전처럼 돌리로 돌아갑니다.
    if (scroll != 0.f && ctx_.window && ctx_.window->isCameraModeActive()) {
        vpToolbar_.cycleCamSpeed(scroll > 0.f ? 1 : -1);
        scroll    = 0.f;
    }
    // 나머지 휠은 커서 아래 위젯에 먼저 보냅니다(DEF-57). 맨 위 위젯부터 받으므로, 열린
    // 메뉴·팝업이 아래 패널보다 먼저 받습니다. 스크롤 상자(디테일·콘텐츠 브라우저·GPU Stats)와
    // 스스로 스크롤하는 목록(아웃라이너·Output Log)이 여기서 받습니다.
    if (scroll != 0.f && ctx_.window) {
        float mx, my;
        ctx_.window->getMousePosition(mx, my);
        int fw = 0, fh = 0;
        ctx_.window->getFramebufferSize(fw, fh);
        slateRenderer_.arrange({ (float)fw, (float)fh });
        HPointerEvent we;
        we.position   = { mx, my };
        we.wheelDelta = scroll;
        if (slateRenderer_.handlePointerEvent(we).handled) scroll = 0.f;
    }
    // 위젯이 받지 않은 휠은 커서가 뷰포트 위일 때만 카메라 돌리로 씁니다.
    if (scroll != 0.f && ctx_.window) {
        float mx, my;
        ctx_.window->getMousePosition(mx, my);
        int wW = 1, wH = 1;
        ctx_.window->getWindowSize(wW, wH);
        if (!viewport_.inViewport(mx, my, (float)wW, (float)wH)) scroll = 0.f;
    }
    viewport_.tickCamera(dt, scroll);
    // D7(M3b): Play 중 떠 있는 창에 키보드가 있으면 게임 카메라 시선도 막습니다(키는 엔진이 HInputSystem 에서 막음).
    if (blocksGameInput()) HInputSystem::get().setLookDelta(0.f, 0.f);
    if (!pendingCulture_.empty()) {
        const std::string c = std::move(pendingCulture_);
        pendingCulture_.clear();
        applyCulture(c, true);
    }
    // 열린 메뉴의 키(H2b)를 글 칸 · 단축키보다 먼저 봅니다. 메뉴가 키보드를 쥐면 그 둘은 이번 프레임 키를 받지 않습니다.
    tickMenuKeys();
    // 색 선택기 Esc(H3b): 디테일 창 키보드의 Esc 엣지. 글 칸 · 숫자 칸이 포커스였으면 그 Esc 는 그 칸 몫(칸만 나감)이라 보지 않습니다.
    const bool pickerEscFocus = currentFocus() != EFocusTarget::None || menuKeyCapture_;
    textInput_.tickInput();
    tickShortcuts();
    flushModal();
    viewport_.tickHotkeys();

    int fw = 1280, fh = 720;
    if (ctx_.window) ctx_.window->getFramebufferSize(fw, fh);

    float newScale = dumpMode_ ? 1.0f : std::clamp(fh / 1080.0f, 1.0f, 2.5f);
    if (std::abs(newScale - uiScale_) > 0.04f) {
        uiScale_    = newScale;
        ui_.uiScale = newScale;
        buildWidgetTree();
        outlinerPanel_.rebuild();
        detailsPanel_.rebuild();
        rebuildBottomPanel();
    }

    viewport_.tickOverlay(uiScale_,
                          viewportDumpPath_.empty() ? nullptr : &lastEditorLines_,
                          viewportDumpPath_.empty() ? nullptr : &lastGizmoTris_);

    tickSelectionSync((int)ctx_.world->getActors().size());

    vpToolbar_.tick();
    statusBar_.tick(logPanel_.tick(bottomTab_ == 1));
    componentPicker_.tick();   // 대상 액터가 사라지거나 선택이 바뀌면 닫음
    {
        const int dw = dock_.floatOfPanel("Details");
        IWindow* w = windowById(dw);
        const bool escDown = w && keyboardWindowId() == dw && w->isKeyDown(HInput::KEY_ESCAPE);
        colorPicker_.tick(escDown && !prevPickerEsc_ && !pickerEscFocus && !modal_);
        prevPickerEsc_ = escDown;
    }
    contentPanel_.tick(bottomTab_ == 0);

    if (dumpMode_ && !dumpStateApplied_ && dumpFrameCount_ >= 1) {
        applyDumpState();
        dumpStateApplied_ = true;
    }
    if (dumpMode_ && dumpStateApplied_ && !detailsDumpApplied_ && dumpFrameCount_ >= 3) {
        applyDetailsDumpState();
        detailsDumpApplied_ = true;
    }
    ++dumpFrameCount_;

    // 단축키·메뉴 액션처럼 위젯 이벤트를 거치지 않고 상태만 바뀌는 경로도 여기서 소진합니다.
    flushChromeRebuilds();
    slateRenderer_.context().tickTooltip(dumpMode_ ? (uiHoverActive_ ? 1.0f : 0.f) : dt);
    // 떠 있는 창의 툴팁(M3b: 그 창에서 호버). 덤프는 시간을 흘리지 않습니다(덤프 기준이 툴팁 없음).
    for (const auto& w : floatWindows_.windows())
        if (w->ctx) w->ctx->tickTooltip(dumpMode_ ? 0.f : dt);
    tickFloatCycle();
}

// ── buildWidgetTree ────────────────────────────────────────────────────────────
// 패널을 헤더 없이(도크 탭 바가 제목을 대신) 만들어 PanelRegistry 에 등록하고, 도크
// 트리(dockRoot_)를 dockHost_ 에 조합합니다. root 는 [toolbar / dockHost_] 뿐이라, 이후
// 메뉴바·상태바 같은 고정 크롬은 도크 영역 바깥에 얹으면 됩니다.
void EditorApp::buildWidgetTree() {
    slateRenderer_.context().setTooltipStyle(EditorUiKit::tooltipStyle(uiScale_));
    using VR = SVerticalBox;
    using HR = SHorizontalBox;
    using BR = SBorder;

    // ── Panel: Outliner (search + list) ───────────────────────────────────────
    auto outlinerPanel = outlinerPanel_.build();

    // ── Panel: Details ─────────────────────────────────────────────────────────
    auto detailsPanel = detailsPanel_.build();

    // ── Panel: Viewport (transparent + stats overlay) ─────────────────────────
    auto viewportWidget = viewport_.buildViewportWidget();
    auto statsText = HNew(STextBlock)
        ->text(AttrStr([this]() -> std::string {
            // GPU 줄과 CPU 줄을 함께 보여 줍니다. GPU 합계가 작은데 Loop 가 크면
            // 그 차이가 곧 "어느 계측기에도 잡히지 않는 시간"이고, 그때는
            // Fence / Acquire / Present 중 어디서 막히는지를 아래 줄로 봅니다.
            const auto& f = gFrameTimings();
            char buf[512];
            if (dumpMode_) {
                snprintf(buf, sizeof(buf),
                    "# FPS  |  Draw: %d  Inst: %d  Culled: %d  Dropped: %d  Transp: %d  |  Target recreate/s: #\n"
                    "GPU   Shadow #.##  Main #.##  AO #.##  Bloom #.##  Tonemap #.##  UI #.##  Total #.##\n"
                    "CPU   Fence #.##  Acquire #.##  ImgFence #.##  Extract #.##  Record #.## (UI #.##)  Submit #.##  Present #.##\n"
                    "LOOP  Poll #.##  Tick #.##  Input #.##  DrawFrame #.##  |  Frame #.## ms",
                    vpTimings_.drawCalls, vpTimings_.instances, vpTimings_.culled, vpTimings_.dropped,
                    vpTimings_.transparentDraws);
            } else {
                snprintf(buf, sizeof(buf),
                    "%d FPS  |  Draw: %d  Inst: %d  Culled: %d  Dropped: %d  Transp: %d  |  Target recreate/s: %.0f\n"
                    "GPU   Shadow %.2f  Main %.2f  AO %.2f  Bloom %.2f  Tonemap %.2f  UI %.2f  Total %.2f\n"
                    "CPU   Fence %.2f  Acquire %.2f  ImgFence %.2f  Extract %.2f  Record %.2f (UI %.2f)  Submit %.2f  Present %.2f\n"
                    "LOOP  Poll %.2f  Tick %.2f  Input %.2f  DrawFrame %.2f  |  Frame %.2f ms",
                    vpTimings_.fps, vpTimings_.drawCalls, vpTimings_.instances, vpTimings_.culled, vpTimings_.dropped,
                    vpTimings_.transparentDraws, vpTimings_.recreatePerSec,
                    vpTimings_.shadowMs, vpTimings_.mainMs, vpTimings_.aoMs, vpTimings_.bloomMs,
                    vpTimings_.tonemapMs, vpTimings_.uiMs, vpTimings_.totalMs,
                    f.fenceWait.v, f.acquire.v, f.imgFence.v, f.extract.v,
                    f.record.v, f.uiRecord.v, f.submit.v, f.present.v,
                    f.pollEvents.v, f.tick.v, f.handleInput.v, f.drawFrame.v, f.loopTotal.v);
            }
            return buf;
        }))
        ->fontSize(s(12.f))->color(EditorTheme::textPrimary)
        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Top);
    // 뷰포트 오버레이는 [툴바 / 간격 / 통계 / 남는 공간] 을 한 세로 상자에 쌓습니다.
    // 통계를 상수 패딩으로 밀어내는 방식이면 폰트·uiScale·툴바 축약 단계가 바뀔 때마다
    // 어긋나는데, HSlate 에는 클리핑이 없어 글자끼리 경고 없이 그대로 포개집니다.
    auto vpBar = vpToolbar_.build();
    auto vpLayer = HNew(VR)
        ->addSlot(vpBar, VR::ESizeRule::Auto, 0.f, HPadding{}, EHAlign::Left)
        ->addSlot(spacer(), VR::ESizeRule::Fixed, s(6.f))
        ->addSlot(HNew(BR)->background(EditorTheme::statsBackdrop)->padding(HPadding{s(6.f), s(4.f)})
                      ->content(statsText),
                  VR::ESizeRule::Auto, 0.f, HPadding{}, EHAlign::Left)
        ->addSlot(spacer(), VR::ESizeRule::Fill)
        // 기즈모 드래그 값. 툴바 안에 두면 드래그 중 폭이 늘어 사다리 계산이 흔들리므로
        // 뷰포트 하단에 따로 둡니다. 드래그 중이 아니면 빈 문자열이라 자리를 차지하지 않습니다.
        ->addSlot(HNew(STextBlock)
                      ->text(AttrStr([this] { return vpToolbar_.readoutText(); }))
                      ->fontSize(s(13.f))->color(EditorTheme::warn)
                      ->hAlign(EHAlign::Left)->vAlign(EVAlign::Bottom),
                  VR::ESizeRule::Auto, 0.f, HPadding{}, EHAlign::Left);

    auto viewportPanel = HNew(SOverlay);
    viewportPanel->addSlot(viewportWidget);
    // 레이어 슬롯은 반드시 Fill/Fill 입니다. 툴바 크기에 딱 맞춘 슬롯이면 SOverlay 가
    // 자신을 포함하는 슬롯에만 마우스 이동을 내려보내므로, 커서가 툴바를 벗어나는 순간
    // 이동 이벤트가 끊겨 버튼 하이라이트가 굳습니다. Fill 이면 뷰포트 어디로 움직여도
    // 세로 상자가 모든 자식에게 이동을 전달해 hover 가 풀립니다.
    viewportPanel->addSlot(vpLayer, EHAlign::Fill, EVAlign::Fill,
                           HPadding{s(ViewportToolbar::kInset), s(ViewportToolbar::kInset)});

    // ── Panel: Bottom (internal Content/Log/GPU tabs) ─────────────────────────
    bottomContent_ = HNew(VR);
    // Content Browser 와 GPU Stats 는 스크롤 상자에 담고, Output Log 는 담지 않습니다
    // (rebuildBottomPanel 이 탭마다 bottomHost_ 의 내용을 고릅니다). 로그 행 수는 행 영역의
    // 실측 높이로 정하는데, 스크롤 상자 안에서는 그 높이가 내용을 따라 늘어나 행 수가 함께
    // 불어나기 때문입니다.
    bottomScroll_  = HNew(SScrollBox)->style(EditorUiKit::scrollBoxStyle(s(1.f)))->wheelStep(s(48.f));
    bottomHost_    = HNew(BR)->background(EditorTheme::transparent);
    auto tabBar = HNew(STabBar)
        ->style(EditorUiKit::tabBarStyle(ui_.s(1.f)))
        ->addTab(HTEXT("Editor.Panels", "ContentBrowser", "Content Browser").toString())
        ->addTab(HTEXT("Editor.Panels", "OutputLog", "Output Log").toString())
        ->addTab(HTEXT("Editor.Panels", "GpuStats", "GPU Stats").toString())
        ->tabWidth(s(145.f))->height(s(EditorTheme::tabH))->fontSize(s(13.f))
        ->activeTab([this]() -> int { return bottomTab_; })
        ->onTabChanged([this](int i){ bottomTab_ = i; rebuildBottomPanel(); });
    auto bottomPanel = HNew(BR)
        ->background(kBgPanel)
        ->content(HNew(VR)
            ->addSlot(tabBar,         VR::ESizeRule::Fixed, s(EditorTheme::tabH))
            ->addSlot(bottomHost_,    VR::ESizeRule::Fill));

    // ── Register panels (신규 위젯도 add 한 줄이면 도킹 대상 — 확장성 seam) ──────
    // 제목은 지금 문화권 글(글자 체계 T3). 패널 id 는 레이아웃 파일 · 코드가 쓰는 이름이라 번역하지 않습니다.
    panelReg_.add("Outliner", HTEXT("Editor.Panels", "Outliner", "Outliner").toString(),      outlinerPanel);
    panelReg_.add("Viewport", HTEXT("Editor.Panels", "Viewport", "Viewport").toString(),      viewportPanel, /*closable=*/false, /*floatable=*/false);
    panelReg_.add("Details",  HTEXT("Editor.Panels", "Details", "Details").toString(),       detailsPanel);
    panelReg_.add("Bottom",   HTEXT("Editor.Panels", "ContentDrawer", "Content Drawer").toString(), bottomPanel);

    // 저장된 레이아웃을 복원하고(등록된 패널 기준으로 검증), 없거나 손상되면 기본 배치로 폴백합니다.
    // uiScale 변경 등으로 buildWidgetTree 가 재호출될 때는 dockRoot_ 가 이미 살아 있으므로 재로딩하지
    // 않습니다 — 세션 중 사용자가 바꾼 레이아웃을 디스크 값으로 되돌리지 않기 위함입니다.
    if (!dock_.hasRoot() && !dock_.loadLayout())
        dock_.buildDefaultLayout();

    // ── Menu bar (최상단 크롬) ──────────────────────────────────────────────────
    auto menuBar = HNew(BR)
        ->background(EditorTheme::bgWindow)->borderColor(kBorder)->borderThickness(1.f)
        ->content(menus_.buildBar());
    buildMenus();
    menus_.rebuildBar();

    // ── Toolbar (도크 영역 바깥의 고정 크롬) ────────────────────────────────────
    auto toolbar = toolbar_.build();

    // ── Dock host + root ───────────────────────────────────────────────────────
    auto dockHost      = dock_.buildHost();
    auto dockOverlay   = dock_.resetOverlay();
    // 팝업(로그 Categories · Add Component · 오른쪽 메뉴 · 메뉴 드롭다운)은 루트 오버레이의 호스트가 아니라 문맥의 팝업 층에 뜹니다
    // (H2a). 트리를 다시 만들면 예전 resetPopupHost 처럼 메뉴 드롭다운만 남기고 닫습니다(드롭다운은 아래 rebuildPopup 이 다시 만듦).
    logPanel_.resetPopup();
    componentPicker_.resetPopup();
    assetPicker_.resetPopup();
    colorPicker_.resetPopup();
    contextMenu_.resetPopup();
    auto dragHost     = contentPanel_.resetDragHost();
    modalHost_     = HNew(BR)->background(EditorTheme::transparent);
    auto statusBar = statusBar_.build();

    // 상태바는 반드시 Fixed 입니다. Auto 면 내용에 따라 높이가 흔들려, 그때마다
    // 뷰포트 rect 가 바뀌고 렌더 타깃이 재생성됩니다.
    auto rootStack = HNew(VR)
        ->addSlot(menuBar,   VR::ESizeRule::Fixed, s(kMenuBarH))
        ->addSlot(toolbar,   VR::ESizeRule::Fixed, s(kToolbarH))
        ->addSlot(dockHost,  VR::ESizeRule::Fill,  1.f)
        ->addSlot(statusBar, VR::ESizeRule::Fixed, s(kStatusBarH));
    auto root = HNew(SOverlay);
    root->addSlot(rootStack);
    root->addSlot(dragHost);
    root->addSlot(dockOverlay);
    root->addSlot(outlinerPanel_.resetDropOverlay());
    root->addSlot(modalHost_);   // 맨 위: 대화상자가 떠 있으면 모든 누름을 받습니다
    // 루트를 바꿔도 글 칸 포커스는 이어집니다(예전과 같음: 확정 · 취소 없음, H1a-2). 글 칸 위젯은 패널이 HReuse 로 같은
    // 객체를 쓰므로 그 위젯에 포커스를 되돌립니다. Add Component 팝업은 resetPopup 이 이미 포커스를 놓았습니다.
    textInput_.preserveFocusAcross([&] { slateRenderer_.setRootWidget(root); });

    dock_.rebuildArea();
    menus_.rebuildPopup();
    rebuildModal();
}

// ── Menu bar (Phase 1 · H2b 메뉴 모델 · 명령 레지스트리) ───────────────────────────────────────────────────────
// 구성(H2b, UE5 에디터 메뉴를 따라 소제목으로 묶음. 예전 → 새 대응은 Docs/Editor_MultiWindow_Design.md 6절 H2b):
//   File   : Scene(New · Open) | Save(Save · Save As)
//   Edit   : History(Undo · Redo) | Edit(Copy · Paste · Duplicate · Delete) | Configuration(Language ▸ · Audio Output)
//   Actor  : Create(Add Empty) | Hierarchy(Attach To Prev Selected · Detach Selected)
//   View   : Viewport(Frame Selected · Gizmo Size)
//   Window : Panels(등록된 패널, 열림 체크) | Layout(Reset Layout)
// 메뉴 이름(메뉴 줄 단추)은 예전과 같습니다. 항목 글 · 단축키 · 켜짐 · 체크는 명령에서 읽습니다.
void EditorApp::buildMenus() {
    using E = EditorMenuEntry;
    // 언어(글자 체계 T3-10). 고르면 다음 프레임에 표를 읽고 UI 를 다시 만듭니다(누른 단추가 처리 중에 사라지지 않게).
    std::vector<E> languages;
    for (const htext::CultureInfo& c : htext::knownCultures()) {
        const std::string code = c.code;
        EditorCommand cmd;
        cmd.id        = "View.Language." + code;
        cmd.label     = HText::fromString(c.nativeName);
        cmd.execute   = [this, code] { pendingCulture_ = code; };
        cmd.isChecked = [code] { return htext::culture() == code; };
        commands_.add(std::move(cmd));
        languages.push_back(E::cmd("View.Language." + code));
    }
    // 등록된 패널을 등록 순서로 보입니다(DEF-69). 열린 패널은 체크 표시가 붙고, 누르면 그 탭을
    // 앞으로 가져옵니다. 탭의 x 로 닫은 패널은 여기서 다시 엽니다.
    std::vector<E> panels;
    for (const std::string& id : panelReg_.order()) {
        const PanelDef* def = panelReg_.find(id);
        if (!def) continue;
        EditorCommand cmd;
        cmd.id        = "Window.Panel." + id;
        cmd.label     = HText::fromString(def->title);
        cmd.execute   = [this, id] { dock_.showPanel(id); };
        cmd.isChecked = [this, id] { return dock_.contains(id); };
        commands_.add(std::move(cmd));
        panels.push_back(E::cmd("Window.Panel." + id));
    }

    std::vector<EditorMenus::Def> defs;
    defs.push_back({ HTEXT("Editor.Menu", "File", "File").toString(), {
        E::heading(HTEXT("Editor.Menu", "Section.Scene", "Scene").toString()),
        E::cmd("File.NewScene"),
        E::cmd("File.OpenScene"),
        E::heading(HTEXT("Editor.Menu", "Section.Save", "Save").toString()),
        E::cmd("File.SaveScene"),
        E::cmd("File.SaveSceneAs"),
    }, 0.f });
    defs.push_back({ HTEXT("Editor.Menu", "Edit", "Edit").toString(), {
        E::heading(HTEXT("Editor.Menu", "Section.History", "History").toString()),
        E::cmd("Edit.Undo"),
        E::cmd("Edit.Redo"),
        E::heading(HTEXT("Editor.Menu", "Section.Edit", "Edit").toString()),
        E::cmd("Edit.Copy"),
        E::cmd("Edit.Paste"),
        E::cmd("Edit.Duplicate"),
        E::cmd("Edit.Delete"),
        E::heading(HTEXT("Editor.Menu", "Section.Configuration", "Configuration").toString()),
        E::submenu(HTEXT("Editor.Menu", "View.Language", "Language").toString(), std::move(languages), "globe"),
        E::cmd("Edit.AudioOutput"),
    }, 0.f });
    defs.push_back({ HTEXT("Editor.Menu", "Actor", "Actor").toString(), {
        E::heading(HTEXT("Editor.Menu", "Section.Create", "Create").toString()),
        E::cmd("Actor.AddEmpty"),
        E::heading(HTEXT("Editor.Menu", "Section.Hierarchy", "Hierarchy").toString()),
        E::cmd("Actor.AttachToPrev"),
        E::cmd("Actor.Detach"),
    }, 0.f });
    defs.push_back({ HTEXT("Editor.Menu", "View", "View").toString(), {
        E::heading(HTEXT("Editor.Menu", "Section.Viewport", "Viewport").toString()),
        E::cmd("View.FrameSelected"),
        E::cmd("View.GizmoSize"),
    }, 0.f });
    std::vector<E> window;
    window.push_back(E::heading(HTEXT("Editor.Menu", "Section.Panels", "Panels").toString()));
    for (E& e : panels) window.push_back(std::move(e));
    window.push_back(E::heading(HTEXT("Editor.Menu", "Section.Layout", "Layout").toString()));
    window.push_back(E::cmd("Window.ResetLayout"));
    defs.push_back({ HTEXT("Editor.Menu", "Window", "Window").toString(), std::move(window), 0.f });
    menus_.setDefs(std::move(defs));
}

// ── 명령 동작 (H2b) ─────────────────────────────────────────────────────────────────────────────────────────
// canExecute 는 그 동작이 아무 일도 하지 않는 경우와 같게 둡니다(단축키 동작이 예전과 같도록, EditorCommandRegistry.hpp).
// 메뉴는 canExecute 가 거짓인 항목을 흐리게 그립니다.
void EditorApp::registerCommands() {
    heditorcmd::addDefaultSpecs(commands_);
    auto hasRoots = [this] { return !selectionRoots().empty(); };
    commands_.bind("File.NewScene",    [this] { newScene(); });   // 카메라만 남기고 비움
    commands_.bind("File.OpenScene",   [this] { openSceneDialog(); });
    commands_.bind("File.SaveScene",   [this] { saveScene(); });
    commands_.bind("File.SaveSceneAs", [this] { saveSceneAs(); });
    commands_.bind("Edit.Undo", [this] { undo(); },
                   [this] { return cmdStack_.canUndo() && !viewport_.gizmo().isDragging(); });
    commands_.bind("Edit.Redo", [this] { redo(); },
                   [this] { return cmdStack_.canRedo() && !viewport_.gizmo().isDragging(); });
    commands_.bind("Edit.Copy",      [this] { copySelection(); },      [this, hasRoots] { return ctx_.window && hasRoots(); });
    commands_.bind("Edit.Paste",     [this] { pasteClipboard(); });    // 클립보드가 비면 경고 로그(예전과 같음)
    commands_.bind("Edit.Duplicate", [this] { duplicateSelection(); }, hasRoots);
    commands_.bind("Edit.Delete",    [this] { deleteSelection(); },
                   [this, hasRoots] { return ctx_.world && ctx_.assets && hasRoots(); });
    // 디테일 속성 행 오른쪽 메뉴(H3a). 메뉴를 연 행에 대해 동작하고, 되돌릴 것이 없으면(기본값과 같음 · 기본값 없음) 흐립니다.
    commands_.bind("Details.ResetToDefault", [this] { detailsPanel_.resetMenuRow(); detailsPanel_.markDirty(); },
                   [this] { return detailsPanel_.menuRowCanReset(); });
    commands_.bind("Details.CopyValue", [this] { if (ctx_.window) ctx_.window->setClipboardText(detailsPanel_.menuRowText()); },
                   [this] { return ctx_.window && detailsPanel_.menuRowCanCopy(); });
    commands_.bind("Details.PasteValue", [this] {
        const std::string text = ctx_.window ? ctx_.window->getClipboardText() : std::string();
        if (!detailsPanel_.pasteMenuRow(text))
            HLOG(LogEditor, Warning) << "Paste: the clipboard text does not fit this property";
        detailsPanel_.markDirty();
    }, [this] { return ctx_.window && detailsPanel_.menuRowCanPaste(); });
    commands_.bind("View.FrameSelected", [this] { frameSelection(); },
                   [this] { return viewport_.cameraActor() && viewport_.camera() && !ctx_.selection.empty(); });
    // 기즈모 크기(카메라 거리 배율)를 작게 0.08 · 보통 0.12 · 크게 0.18 로 돌립니다. 툴바는 폭 사다리 때문에 칩을 더하지 않습니다.
    commands_.bind("View.GizmoSize", [this] {
        const float f = viewport_.gizmo().sizeFactor();
        const float n = f < 0.10f ? 0.12f : (f < 0.15f ? 0.18f : 0.08f);
        viewport_.gizmo().setSizeFactor(n);
        HLOG(LogEditor, Log) << "Gizmo size " << (n < 0.10f ? "small" : (n < 0.15f ? "medium" : "large"));
    });
    commands_.bind("Edit.AudioOutput", [this] { openAudioOutputDialog(); });
    commands_.bind("Actor.AddEmpty", [this] {
        selectOnly(ctx_.world->spawnActor(outlinerPanel_.nextActorName()));
        recordActorCreation({ ctx_.selected });
    }, [this] { return ctx_.world != nullptr; });
    // 부모 바꾸기는 아웃라이너에서 행을 끌어 놓는 것이 기본이고(2026-10-15), 메뉴도 남겨 둡니다. 둘 다 월드 자세를
    // 유지하고 Undo 됩니다. 부모는 "직전에 선택했던 액터", 자식은 "지금 선택한 액터"입니다.
    commands_.bind("Actor.AttachToPrev", [this] {
        Actor* child  = ctx_.selected;
        Actor* parent = prevSelected2_;
        if (!reparentUndoable(child, parent))
            HLOG(LogEditor, Warning) << "Cannot attach '" << child->name << "' under '" << parent->name
                                     << "' (an actor cannot go under itself or its child)";
    }, [this] { return ctx_.selected && prevSelected2_ && ctx_.selected != prevSelected2_; });
    commands_.bind("Actor.Detach", [this] { reparentUndoable(ctx_.selected, nullptr); },
                   [this] { return ctx_.selected && ctx_.selected != viewport_.cameraActor(); });
    commands_.bind("Window.ResetLayout", [this] { dock_.buildDefaultLayout(); dock_.rebuildArea(); dock_.saveLayout(); });
    // Ctrl+F → 아웃라이너 검색창 포커스 (기존 검색어는 전체 선택 → 바로 교체 가능). 아웃라이너가
    // 닫혀 있거나 가려진 탭이면 먼저 앞으로 꺼냅니다. 그러지 않으면 보이지 않는 검색창으로 타이핑이 들어갑니다.
    // 아웃라이너가 떠 있는 창에 있으면 그 창을 앞으로 가져온 뒤 포커스를 줍니다(M3b). 글자는 그 창이 OS 포커스를 받은 뒤부터
    // 들어갑니다(칸은 그 칸이 있는 창에 키보드가 있을 때만 받음).
    commands_.bind("Outliner.FocusSearch", [this] {
        bringPanelForward("Outliner", outlinerVisible_);
        setTextFocus(EFocusTarget::Search);
    });
    // 툴바(모양 그대로, 같은 명령 id).
    commands_.bind("Play.Play",  [this] { beginPlay(); });
    commands_.bind("Play.Pause", [this] { pausePlay(); }, [this] { return playState_ != EPlayState::Editor; });
    commands_.bind("Play.Stop",  [this] { stopPlay(); },  [this] { return playState_ != EPlayState::Editor; });
    commands_.bind("Content.Refresh", [this] { contentPanel_.scanAll(); rebuildBottomPanel(); });
}

// ── Play state ─────────────────────────────────────────────────────────────────
void EditorApp::beginPlay() {
    if (playState_ == EPlayState::Playing) return;
    if (playState_ == EPlayState::Editor) {
        worldSnapshot_ = serializeWorld();
        doc_.beginPlay(cmdStack_.stateToken());
        viewport_.setViewPreferred(false);   // Play 중에는 씬 카메라(플레이어)가 먼저
        HLOG(LogEditor, Log) << "Play";
    } else {
        HLOG(LogEditor, Log) << "Resume";
    }
    playState_ = EPlayState::Playing;
    toolbar_.markDirty();
}

void EditorApp::pausePlay() {
    if (playState_ == EPlayState::Playing) {
        playState_ = EPlayState::Paused;
        HLOG(LogEditor, Log) << "Paused";
    } else if (playState_ == EPlayState::Paused) {
        playState_ = EPlayState::Playing;
        HLOG(LogEditor, Log) << "Resumed";
    }
    toolbar_.markDirty();
}

void EditorApp::stopPlay() {
    if (playState_ == EPlayState::Editor) return;
    playState_ = EPlayState::Editor;
    viewport_.setViewPreferred(true);
    deserializeWorld(worldSnapshot_);
    worldSnapshot_.clear();
    cmdStack_.clear();
    doc_.endPlay(cmdStack_.stateToken());
    HLOG(LogEditor, Log) << "Stopped - world restored";
    toolbar_.markDirty();
}

// ── Serialization ──────────────────────────────────────────────────────────────
// 씬 형식과 블록·부모 규칙은 SceneSerializer 가 소유합니다. 게임 모듈이 시작 씬을 같은
// 함수로 읽으므로, 여기에 별도 구현을 두면 두 경로가 어긋납니다. 에디터 카메라는 씬이
// 아니라 에디터 소유라 저장하지도 지우지도 않습니다.
std::string EditorApp::serializeWorld() {
    return SceneSerializer::writeScene(*ctx_.world, viewport_.cameraActor());
}

void EditorApp::deserializeWorld(const std::string& data) {
    std::vector<Actor*> toDestroy;
    for (auto& ap : ctx_.world->getActors())
        if (ap.get() != viewport_.cameraActor())
            toDestroy.push_back(ap.get());
    selectOnly(nullptr);
    for (Actor* a : toDestroy) ctx_.world->destroyActor(a);

    SceneSerializer::readScene(data, *ctx_.world, *ctx_.assets);

    // 씬 로드는 기존 액터를 전부 파괴하고 새 id 로 다시 스폰하므로, 아웃라이너의
    // 세션 상태(접힘·스크롤)를 그대로 두면 엉뚱한 액터가 접힌 채로 나타납니다.
    outlinerPanel_.resetForWorld();
    prevSelected2_ = nullptr;
}

// 크롬의 지연 리빌드를 소진합니다. 위젯 콜백이 자기 컨테이너를 직접 비우면 순회 중인
// 슬롯과 실행 중인 std::function 이 함께 파괴되므로, 콜백은 플래그만 세우고 여기서
// 한 번에 처리합니다. 아웃라이너보다 뒤에 두어, 메뉴 항목이 도크 변형을 요청한 경우
// 도크 쪽이 먼저 정리되게 합니다.
void EditorApp::flushChromeRebuilds() {
    // 마우스로 누른 대화상자 버튼은 맨 먼저 실행합니다. 그 동작(씬 열기 · 새 씬)은 액터를 바로 파괴하므로, 아웃라이너 ·
    // 디테일을 그 뒤에 다시 만들어야 이번 프레임의 그리기가 해제된 액터 · 컴포넌트를 읽지 않습니다(DEF-130). 예전에는
    // 대화상자 동작이 맨 끝이라 디테일이 낡은 채로 한 프레임 그려졌습니다. 디테일 칸은 액터 포인터를 붙잡습니다.
    flushModal();
    outlinerPanel_.flushRebuild();
    menus_.flushRebuilds();
    toolbar_.flushRebuild();
    detailsPanel_.flushRebuild();
    logPanel_.flushRebuilds(bottomTab_ == 1);
    componentPicker_.flushRebuild();
    assetPicker_.flushRebuild();
    colorPicker_.flushRebuild();
    if (bottomTab_ == 0) contentPanel_.flushRebuild();
    contextMenu_.flushRebuild();
    // HSlate 지연 변경 큐(H1b). 지금은 하단 패널 리빌드(requestBottomRebuild)만 여기로 옮겼고, 예전 bottomDirty_ 와
    // 같은 맨 끝 자리입니다. 나머지 패널의 지연 플래그는 소진 순서 · 조건(DEF-83 누름 보류, 앵커 배치 대기 등)이 있어
    // 위 줄에 그대로 둡니다.
    slateRenderer_.context().flushDeferred();
}

void EditorApp::requestBottomRebuild() {
    if (bottomRebuildQueued_) return;
    bottomRebuildQueued_ = true;
    slateRenderer_.context().deferred([this] {
        bottomRebuildQueued_ = false;
        rebuildBottomPanel();
    });
}

// ── rebuildLog ─────────────────────────────────────────────────────────────────
void EditorApp::rebuildLog() {
    rebuildBottomPanel();
}

// ── rebuildContent ─────────────────────────────────────────────────────────────
void EditorApp::rebuildContent() {
    rebuildBottomPanel();
}

// ── rebuildBottomPanel ─────────────────────────────────────────────────────────
void EditorApp::rebuildBottomPanel() {
    if (!bottomContent_) return;
    bottomContent_->clearSlots();
    // Output Log 는 스크롤 상자 밖에 둡니다(buildWidgetTree 의 설명 참고). 탭이 바뀌면 스크롤을
    // 맨 위로 돌리고, 같은 탭을 다시 그릴 때(Refresh 등)는 위치를 지킵니다.
    if (bottomHost_ && bottomScroll_) {
        if (bottomTab_ == 1 || bottomTab_ == 0) {   // 로그 · 콘텐츠 브라우저는 자체 스크롤을 가짐(2026-11-21)
            bottomScroll_->content(nullptr);
            bottomHost_->content(bottomContent_);
        } else {
            bottomScroll_->content(bottomContent_);
            bottomHost_->content(bottomScroll_);
        }
        if (bottomTab_ != bottomScrollTab_) { bottomScroll_->scrollToTop(); bottomScrollTab_ = bottomTab_; }
    }

    // Output Log 탭에만 있는 위젯의 참조를 놓습니다. 다른 탭으로 가면 팝업을 닫고 로그 검색
    // 포커스도 풉니다. 풀지 않으면 보이지 않는 검색창으로 타이핑이 계속 들어갑니다.
    logPanel_.releaseTab(bottomTab_ == 1);
    contentPanel_.releaseTab(bottomTab_ == 0);   // 콘텐츠 탭이면 아래 buildTab 이 다시 잡음(검색 포커스 유지)
    if (contextMenu_.panel() == "Bottom") contextMenu_.close();   // 디테일이 연 메뉴(H3a)는 그대로
    if (bottomTab_ != 1 && (currentFocus() == EFocusTarget::LogSearch || currentFocus() == EFocusTarget::Console))
        setTextFocus(EFocusTarget::None);

    if (bottomTab_ == 0)      contentPanel_.buildTab(*bottomContent_);
    else if (bottomTab_ == 1) logPanel_.buildTab(*bottomContent_);
    else                      gpuStatsPanel_.buildTab(*bottomContent_);
}

// ── syncEulerCache / undo / redo ───────────────────────────────────────────────
void EditorApp::syncEulerCache() {
    if (ctx_.selected)
        cachedEuler_ = quatToEulerDeg(ctx_.selected->transform().rotation());
    else
        cachedEuler_ = {};
}

// 기즈모를 끄는 도중에는 되돌리지 않습니다. 생성 · 삭제 Undo 는 선택 액터를 지우거나 다른 액터로 바꿀 수
// 있는데, 그러면 드래그가 시작 때 떠 둔 트랜스폼을 엉뚱한 액터에 적용하고 그 결과를 커맨드로 쌓습니다.
void EditorApp::undo() {
    if (!cmdStack_.canUndo() || viewport_.gizmo().isDragging()) return;
    cmdStack_.undo();
    syncEulerCache();
    // 토글 · 열거 · 켜기 칸의 글자와 아웃라이너 이름 · 계층은 만들 때 정해지므로 다시 만듭니다(지연 리빌드).
    detailsPanel_.markDirty();
    outlinerPanel_.markDirty();
}

void EditorApp::redo() {
    if (!cmdStack_.canRedo() || viewport_.gizmo().isDragging()) return;
    cmdStack_.redo();
    syncEulerCache();
    // 토글 · 열거 · 켜기 칸의 글자와 아웃라이너 이름 · 계층은 만들 때 정해지므로 다시 만듭니다(지연 리빌드).
    detailsPanel_.markDirty();
    outlinerPanel_.markDirty();
}

bool EditorApp::editDragActive() const { return viewport_.gizmo().isDragging() || SNumberField::anyPressed(); }

// ── tickShortcuts ──────────────────────────────────────────────────────────────
// 표준 단축키(Ctrl+Z · Ctrl+Y · Ctrl+S …)는 명령 레지스트리의 chord 로 정하고(H2b), 고른 명령을 레지스트리로 실행합니다. 어느 키가
// 어느 명령인지 · 조건(대화상자 · 카메라 비행 · 글 칸 · 끄는 중)은 예전 이 함수의 if 사다리와 같습니다(EditorCommandRegistry.hpp,
// 시험 editor_commands 가 모든 입력 조합으로 대조).
void EditorApp::tickShortcuts() {
    if (!ctx_.window) return;
    // 키는 키보드 창(OS 포커스가 있는 창, 없으면 메인)에서 읽습니다(M3b). 떠 있는 창에서도 Ctrl+S · Ctrl+Z · F 가 됩니다(4절 6).
    // 카메라 모드(RMB 비행)는 메인 창의 상태입니다.
    IWindow* kw = windowById(keyboardWindowId());
    IWindow& win = kw ? *kw : *ctx_.window;
    IWindow& mainWin = *ctx_.window;

    // 엣지는 조건과 상관없이 매 프레임 잽니다(예전과 같음: 막힌 동안 누른 키가 풀린 뒤 엣지로 잡히지 않게).
    if (prevShortcutKeys_.empty())
        for (int key : commands_.polledKeys()) prevShortcutKeys_.push_back({ key, false });
    std::vector<int> edges;
    for (auto& [key, prev] : prevShortcutKeys_) {
        const bool now = win.isKeyDown(key);
        if (now && !prev) edges.push_back(key);
        prev = now;
    }
    // 열린 메뉴가 키보드를 쥐고 있으면(H2b) 단축키는 받지 않습니다.
    if (menuKeyCapture_) return;

    EditorShortcutInput in;
    in.keyEdge    = [&edges](int key) { return std::find(edges.begin(), edges.end(), key) != edges.end(); };
    in.ctrl       = win.isCtrlDown();
    in.shift      = win.isShiftDown();
    in.modal      = modal_ != nullptr;                  // 대화상자가 떠 있으면 단축키를 받지 않습니다
    in.cameraMode = mainWin.isCameraModeActive();       // WASD 비행 중 충돌 방지
    in.textFocus  = currentFocus() != EFocusTarget::None;
    // 기즈모 · 숫자 칸을 끄는 동안에는 씬을 바꾸거나 저장하는 단축키를 받지 않습니다(Undo · Redo 와 같은 규칙). Ctrl+D 는
    // 주 선택을 새 액터로 바꿔 끄기 결과가 새 액터의 커맨드로 쌓이고(DEF-132), Ctrl+S 는 끄기 중간 값을 파일에 넣어 저장
    // 표지가 어긋납니다(DEF-133). 열기 · 새 씬도 끄는 대상을 없애므로 함께 막습니다.
    in.editDrag   = editDragActive();
    const EditorShortcutResult r = commands_.resolveShortcut(in);
    if (r.dragNotice)
        HLOG(LogEditor, Log) << "Finish the drag first (shortcut ignored while dragging a gizmo or a value)";
    if (r.command) commands_.execute(r.command->id);
}

// ── 메뉴 키보드 조작 (H2b) ─────────────────────────────────────────────────────────────────────────────────────
int EditorApp::menuWindowId() {
    if (menus_.isOpen()) return 0;   // 상단 메뉴는 메인 창에만 있음
    if (contextMenu_.isOpen()) {
        const HSlateContext* c = contextMenu_.targetContext();
        if (!c) return -1;
        if (c == &slateRenderer_.context()) return 0;
        for (const auto& w : floatWindows_.windows())
            if (w->ctx && w->ctx.get() == c) return w->floatId;
    }
    return -1;
}

void EditorApp::tickMenuKeys() {
    static constexpr int kKeys[6] = { HInput::KEY_UP, HInput::KEY_DOWN, HInput::KEY_LEFT, HInput::KEY_RIGHT,
                                      HInput::KEY_ENTER, HInput::KEY_ESCAPE };
    static constexpr hmenu::Key kMap[6] = { hmenu::Key::Up, hmenu::Key::Down, hmenu::Key::Left, hmenu::Key::Right,
                                            hmenu::Key::Enter, hmenu::Key::Escape };
    const int kb = keyboardWindowId();
    IWindow* kw = windowById(kb);
    // 엣지는 늘 잽니다. 키보드 창이 바뀌면 그 창의 지금 상태로 다시 심습니다(다른 창의 눌림이 엣지로 잡히지 않게, 글 칸과 같은 규칙).
    std::vector<hmenu::Key> keys;
    if (kw) {
        const bool reseed = kb != menuKeyWin_;
        menuKeyWin_ = kb;
        for (int i = 0; i < 6; ++i) {
            const bool now  = kw->isKeyDown(kKeys[i]);
            const bool edge = now && !prevMenuKeys_[i] && !reseed;
            prevMenuKeys_[i] = now;
            // ↑ ↓ 는 누르고 있으면 OS 자동 반복으로 거듭 움직입니다(편집 키와 같은 규칙, DEF-58).
            int n = edge ? 1 : 0;
            if (!reseed && i < 2) n = std::max(n, kw->keyPressCount(kKeys[i]));
            for (int k = 0; k < n; ++k) keys.push_back(kMap[i]);
        }
    }
    const int mw = menuWindowId();
    menuKeyCapture_ = mw >= 0 && mw == kb;
    // 시험 대본 키(그 창이 키보드 창이고 메뉴의 창일 때만) · 덤프 키(--ui-menu-keys, 메뉴가 떠 있을 때 한 프레임에 하나).
    bool scripted = false;
    if (!scriptMenuKeys_.empty()) {
        const auto [win, k] = scriptMenuKeys_.front();
        scriptMenuKeys_.erase(scriptMenuKeys_.begin());
        if (mw >= 0 && win == mw && win == kb) {   // 실제 키와 같은 규칙: 그 창이 키보드 창이고 메뉴가 그 창에 있을 때
            keys.push_back(k);
            scripted = true;
            HLOG(LogEditor, Log) << "--ui-float-input: menu key on window " << win;
        } else {
            HLOG(LogEditor, Log) << "--ui-float-input: key for window " << win << " ignored (menu window " << mw
                                 << ", keyboard window " << kb << ")";
        }
    }
    const bool menuShown = (menus_.isOpen() && menus_.popupShown()) || (contextMenu_.isOpen() && contextMenu_.popupContext());
    if (dumpMode_ && menuShown && uiMenuKeyNext_ < uiMenuKeys_.size()) {
        keys.push_back(uiMenuKeys_[uiMenuKeyNext_++]);
        scripted = true;
    }
    if (!menuKeyCapture_ && !scripted) return;
    for (hmenu::Key k : keys) {
        if (menus_.isOpen())            menus_.handleKey(k);
        else if (contextMenu_.isOpen()) contextMenu_.handleKey(k);
    }
}

// ── handleInput ────────────────────────────────────────────────────────────────
void EditorApp::handleInput(float mouseX, float mouseY,
                             bool leftDown, bool leftJustUp,
                             float scrollDelta, float winW, float winH) {
    // 떠 있는 창의 이번 프레임 포인터(M3b). 메인 처리가 콘텐츠 끌기(떠 있는 콘텐츠 브라우저 → 메인 뷰포트)에 쓰므로 먼저 읽습니다.
    readFloatPointers();
    // 시험 대본에 메인 포인터 단계가 있으면(M4: 메인 탭을 창 밖 · 떠 있는 창으로 끌기) 메인 포인터도 대본 값입니다.
    if (mainScripted_) {
        mouseX     = mainSim_.pos.x;
        mouseY     = mainSim_.pos.y;
        leftDown   = mainSim_.left;
        leftJustUp = mainSimPrevLeft_ && !mainSim_.left;
        mainSimPrevLeft_ = mainSim_.left;
    }
    handleInputEvents(mouseX, mouseY, leftDown, leftJustUp, scrollDelta, winW, winH);
    // 떠 있는 창마다 같은 순서로(M3b). 메인의 조기 return 과 상관없이 돕니다.
    handleFloatInputs();
    // 위젯 콜백(펼침·눈·삭제)은 리빌드를 직접 하지 않고 플래그만 세웁니다. 슬롯 순회 도중 clearSlots 가 돌면 지금 실행
    // 중인 버튼과 그 std::function 자체가 파괴되기 때문입니다. 소진은 이벤트 디스패치가 전부 끝난 뒤, 이 프레임의
    // drawFrame 전에 합니다. 예전에는 handleInput 의 조기 return 때문에 스코프 가드(DeferredRebuildGuard)의 소멸자로
    // 했고, H1b 에서 본문을 handleInputEvents 로 떼어 같은 자리에서 부릅니다.
    outlinerPanel_.flushRebuild();
    flushChromeRebuilds();
    // 떠 있는 창의 OS 닫기 · 생성 · 파괴(M3a). 이번 프레임의 입력이 도크 배치를 바꿨을 수 있으므로 그리기 직전, 이벤트 펌프 밖입니다.
    syncFloatWindows();
}

void EditorApp::handleInputEvents(float mouseX, float mouseY,
                                  bool leftDown, bool leftJustUp,
                                  float scrollDelta, float winW, float winH) {
    if (dumpMode_ && uiHoverActive_) {
        mouseX = uiHoverPos_.x;
        mouseY = uiHoverPos_.y;
    }
    // 디테일 패널이 보여 주던 액터가 에디터 tick 뒤(게임 tick·게임 모듈)에 파괴됐다면,
    // 이벤트를 보내기 전에 패널부터 다시 만듭니다. 낡은 패널의 칸에 누름·드래그·확정이
    // 닿으면 해제된 액터와 컴포넌트를 읽고 씁니다(DEF-92). 아래 포커스 해제의 확정도
    // 그 접근자를 부르므로 반드시 그보다 앞이어야 합니다.
    detailsPanel_.flushRebuild();

    // 이벤트를 보내기 전에 배치부터 맞춥니다. 이번 프레임 tick 에서 리빌드된 위젯(아웃라이너
    // 행·디테일 칸 등)은 아직 배치되지 않아 크기가 0 이라 클릭을 받지 못했습니다(DEF-84).
    // 아래의 모든 히트테스트(메뉴·스플리터·탭·포커스)가 이 배치를 씁니다.
    if (ctx_.window) {
        int fw = 0, fh = 0;
        ctx_.window->getFramebufferSize(fw, fh);
        slateRenderer_.arrange({ (float)fw, (float)fh });
    }

    // 패널의 포인터 판정은 그 패널이 있는 창의 포인터로만 합니다(M3b, hwininput::panelGetsPointer). 떠 있는 창에 있는 패널은
    // handleFloatInput 이 그 창 좌표 · 버튼으로 같은 일을 합니다. 패널이 메인에 있거나 닫혀 있으면 예전과 같습니다.
    const bool outlinerHere   = hwininput::panelGetsPointer(dock_.floatOfPanel("Outliner"), 0);
    const bool bottomHere     = hwininput::panelGetsPointer(dock_.floatOfPanel("Bottom"), 0);
    const bool outlinerInMain = dock_.panelActiveInMain("Outliner");
    const bool bottomInMain   = dock_.panelActiveInMain("Bottom");
    // 아웃라이너 목록 리빌드 보류(DEF-83)는 버튼을 떼면 풉니다. 거는 곳은 아래 UI 디스패치입니다.
    if (!leftDown && outlinerHere) outlinerPanel_.releasePressHeld();
    // 아웃라이너 행 끌어 놓기(부모 바꾸기). 누른 행 버튼의 클릭과 함께 돕니다.
    if (outlinerHere) outlinerPanel_.tickDrag(mouseX, mouseY, leftDown, prevLeftDown_, (bool)modal_, outlinerInMain);

    // 편집 중에 다른 곳을 누르면 편집을 끝냅니다. 이 처리가 스플리터·탭·기즈모의 조기
    // return 보다 **앞**에 있어야 합니다. 뒤에 있으면 그 경로들이 커밋을 건너뛰어 편집
    // 모드가 갇히고, 나중에 누른 Enter 가 낡은 값으로 기즈모 결과를 덮어씁니다.
    if (leftDown && !prevLeftDown_ && currentFocus() != EFocusTarget::None &&
        !focusWidgetContains(0, mouseX, mouseY)) {
        setTextFocus(EFocusTarget::None);
    }

    // 팝업 바깥(H2a: 메인 문맥 팝업 층). 로그 Categories · Add Component · 오른쪽 메뉴(PassThrough) 바깥을 누르면 닫기만 하고 클릭은
    // 그대로 흘려보냅니다. 전면 캐처로 받으면 그 클릭이 닫기에만 쓰여 삼켜집니다(메뉴 팝업의 DEF-64). 메뉴 드롭다운(Consume)은 층이
    // 배경 누름을 먹고 닫습니다. 다른 창(떠 있는 창)의 팝업은 이 창을 누르면 모두 닫습니다.
    HPopupLayer& mainPopups = slateRenderer_.context().popups();
    if (leftDown && !prevLeftDown_) {
        mainPopups.dismissOutside(glm::vec2{ mouseX, mouseY }, 0);
        dismissPopupsExcept(0);
    }
    // 오른쪽 누름: 열린 메뉴 밖이면 닫고(오른쪽 메뉴만 오른쪽 누름으로 닫힘), 콘텐츠 브라우저 위면 그 자리의 메뉴를 엽니다(2026-11-21).
    {
        const bool rightDown = ctx_.window && ctx_.window->isMouseButtonDown(HInput::MOUSE_BUTTON_RIGHT);
        if (rightDown && !prevRightDown_) {
            const glm::vec2 p{ mouseX, mouseY };
            mainPopups.dismissOutside(p, 1);
            dismissPopupsExcept(0);
            if (!modal_ && bottomTab_ == 0 && bottomInMain) contentPanel_.openContextMenuAt(p);
            // 디테일 속성 행(H3a): Reset to Default · Copy · Paste.
            if (!modal_ && dock_.panelActiveInMain("Details") && hwininput::panelGetsPointer(dock_.floatOfPanel("Details"), 0))
                detailsPanel_.openRowMenuAt(p);
        }
        prevRightDown_ = rightDown;
        // Esc: closeOnEscape 팝업(오른쪽 메뉴)을 닫습니다. Add Component 는 검색 칸의 Esc(onCancel)가 닫습니다.
        if (!mainPopups.empty() && ctx_.window && ctx_.window->isKeyDown(HInput::KEY_ESCAPE)) mainPopups.dismissOnEscape();
    }
    // 콘텐츠 브라우저 끌어 놓기(2026-11-21): 에셋 보기의 항목을 누른 채 끌어 뷰포트에 놓습니다. 끄는 동안 커서 옆 이름표와
    // 놓일 자리 표시를 그리고, 놓으면 종류별로 놓습니다(Undo 가능). Esc 는 취소입니다.
    {
        glm::vec2 p{ mouseX, mouseY };
        if (leftDown && !prevLeftDown_ && !modal_ && bottomTab_ == 0 && bottomInMain) contentPanel_.pointerPress(p);
        if (!dumpDrag_ && bottomHere) contentPanel_.pointerMove(p, leftDown);
        // 끄는 중에는 콘텐츠 브라우저가 있는 창의 포인터를 메인 좌표로 바꿔 씁니다(M3b: 떠 있는 콘텐츠 브라우저에서 끌어 메인 뷰포트에
        // 놓기, OS 가 누른 창에 커서를 잡아 두어 창 밖 좌표도 옴). 메인이면 그대로입니다.
        bool dragLeft = leftDown;
        if (contentPanel_.dragging() && !bottomHere && !dumpDrag_ && !contentPointerInMain(mouseX, mouseY, leftDown, p, dragLeft)) {
            contentPanel_.cancelDrag();   // 그 창이 사라짐
        }
        if (contentPanel_.dragging()) {
            const std::string path = contentPanel_.dragPath();
            glm::vec3 point{ 0.f };
            Actor* hit = nullptr;
            // 디테일 에셋 칸 위(H3b, 메인 창의 디테일만): 종류가 맞으면 그 칸에 넣고, 아니면 거부(커서 이름표 · 칸 테두리 빨강).
            const bool overDetails = !modal_ && dock_.panelActiveInMain("Details") && detailsPanel_.overAssetRow(p);
            const bool canDetails  = overDetails && detailsPanel_.canDropAsset(p, path);
            const bool can = overDetails ? canDetails
                                         : (!modal_ && contentPanel_.canDropInViewport(path) &&
                                            viewport_.dropPoint(p.x, p.y, winW, winH, point, &hit));
            detailsPanel_.setDropHover(p, path, overDetails && (dragLeft || dumpDrag_));
            IWindow* kw = windowById(keyboardWindowId());
            if (kw && kw->isKeyDown(HInput::KEY_ESCAPE) && !dumpDrag_) {
                contentPanel_.cancelDrag();
                detailsPanel_.setDropHover(p, path, false);
            } else if (!dragLeft && !dumpDrag_) {
                if (overDetails) { if (canDetails) detailsPanel_.dropAsset(p, path); }
                else if (can)    contentPanel_.dropInViewport(path, point, hit);
                contentPanel_.cancelDrag();
                detailsPanel_.setDropHover(p, path, false);
            } else if (overDetails) {
                contentPanel_.updateDragVisual(p, can);
            } else {
                if (can) {
                    // 놓일 자리: 바닥 원 + 세운 선(화면에서 같은 크기로 보이게 카메라 거리에 비례). 소리를 소리 액터 위에 놓으면
                    // 그 액터의 소리를 바꾸므로 그 액터 자리에 구를 그립니다.
                    const glm::vec4 col{ 0.30f, 0.65f, 1.f, 1.f };
                    const Actor* cam = viewport_.cameraActor();
                    const float dist = cam ? std::max(0.2f, glm::length(cam->worldPosition() - point)) : 5.f;
                    hdebug::circle(point, glm::vec3(0.f, 1.f, 0.f), dist * 0.03f, col);
                    hdebug::line(point, point + glm::vec3(0.f, dist * 0.06f, 0.f), col);
                    const hcb::Item* item = contentPanel_.index().find(path);
                    const bool sound = item && (item->kind == hcb::AssetKind::Audio || item->kind == hcb::AssetKind::Cue);
                    if (sound && hit && hit != viewport_.cameraActor() && hit->getComponent<AudioComponent>())
                        hdebug::sphere(hit->worldPosition(), dist * 0.04f, glm::vec4(0.35f, 0.9f, 0.45f, 1.f));
                }
                contentPanel_.updateDragVisual(p, can);
            }
        }
        if (!contentPanel_.dragging()) {
            contentPanel_.updateDragVisual(p, false);   // 이름표 지우기(한 번)
            detailsPanel_.setDropHover(p, std::string(), false);   // 디테일 칸 테두리 강조 지우기(H3b, 바뀔 때만 다시 만듦)
        }
    }

    // ── 메뉴 hover 전환 ──────────────────────────────────────────────────────────
    menus_.handleHoverSwitch(glm::vec2{ mouseX, mouseY });
    // 드롭다운 · 오른쪽 메뉴의 행 hover: 하위 메뉴 열기 · 닫기, 강조(H2b).
    menus_.hoverRows(glm::vec2{ mouseX, mouseY });
    contextMenu_.hover(&slateRenderer_.context(), glm::vec2{ mouseX, mouseY });

    // 열린 팝업(메뉴 드롭다운과 그 캐처, Categories 패널) 위의 누름은 아래 스플리터·탭 판정보다
    // UI 가 먼저 받아야 합니다. 예전에는 File > Load Scene 행의 왼쪽 끝을 누르면 그 밑의 Outliner
    // 탭이 클릭을 가져가 메뉴가 열린 채로 남았습니다(DEF-64 의 우회 경로).
    // 팝업 층(H2a): 열린 팝업 영역 안이거나 Consume 팝업(메뉴 드롭다운)이 열려 있으면 참. 메뉴는 열었지만 아직 층에 들어가기 전인
    // 프레임도 있어 isOpen() 을 함께 봅니다(예전과 같음).
    const bool overPopup =
        modal_ != nullptr || menus_.isOpen() || mainPopups.overPopup(glm::vec2{ mouseX, mouseY });

    // ── Dock splitter resize & tab drag ─────────────────────────────────────────
    if (dock_.handleSplitterInput(0, mouseX, mouseY, leftDown, prevLeftDown_, overPopup, ctx_.window) ||
        dock_.handleTabInput(0, mouseX, mouseY, leftDown, prevLeftDown_, overPopup, ctx_.window)) {
        prevLeftDown_ = leftDown;
        return;
    }

    HPointerEvent e;
    e.position = { mouseX, mouseY };
    // 숫자 칸 끌기 배율(Shift 빠르게 · Ctrl 정밀하게)에 씁니다(DEF-46).
    if (ctx_.window) {
        e.shift = ctx_.window->isShiftDown();
        e.ctrl  = ctx_.window->isCtrlDown();
    }

    if (scrollDelta != 0.f) {
        e.wheelDelta = scrollDelta;
        slateRenderer_.handlePointerEvent(e);
        e.wheelDelta = 0.f;
    }

    // ── Gizmo drag ─────────────────────────────────────────────────────────────
    const bool gizmoConsumed = viewport_.handleGizmoDrag(mouseX, mouseY, winW, winH, leftDown, prevLeftDown_);

    // ── UI event + actor pick ─────────────────────────────────────────────────
    if (!gizmoConsumed && leftDown && !prevLeftDown_) {
        if (outlinerInMain)   // 메인에서 활성 탭일 때만(떠 있는 창이면 그 창 처리가 그 창 좌표로)
            outlinerPanel_.setPressHeldIfContains(glm::vec2{ mouseX, mouseY });
        e.button = 0; e.down = true;
        HReply r = slateRenderer_.handlePointerEvent(e);
        // Play 중 월드 UI 패널을 누른 클릭은 패널이 받으므로 액터를 고르지 않습니다(3D UI 구간 5).
        if (!r.handled)
            viewport_.onViewportPress(mouseX, mouseY, winW, winH, pointerOverWorldUi_);
    }
    viewport_.releaseOrbit(leftDown);
    if (leftJustUp && !gizmoConsumed) {
        e.button = 0; e.down = false;
        slateRenderer_.handlePointerEvent(e);
    }
    // 이동 이벤트에도 좌버튼의 현재 상태를 실어 보냅니다. 버튼류가 "눌린 채 리빌드되어
    // 남은 프레스 상태"를 스스로 정리할 때 이 값을 봅니다.
    e.button = -1;
    e.down   = leftDown;
    slateRenderer_.handlePointerEvent(e);

    prevLeftDown_ = leftDown;
}

// ── Viewport delegation ───────────────────────────────────────────────────────
bool EditorApp::pointerRay(float mx, float my, float winW, float winH, glm::vec3& origin, glm::vec3& dir) const {
    return viewport_.pointerRay(mx, my, winW, winH, origin, dir);
}

bool EditorApp::inViewport(float mx, float my, float winW, float winH) const {
    return viewport_.inViewport(mx, my, winW, winH);
}

// ── deleteSelectedActor ────────────────────────────────────────────────────────
void EditorApp::deleteSelectedActor() {
    deleteSelection();   // 고른 루트 전부를 한 커맨드로(2026-10-15)
}

std::unique_ptr<ICommand> EditorApp::makePresenceCmd(bool create, std::vector<uint64_t> rootIds) {
    using Kind = ActorPresenceCmd::Kind;
    SnapshotWriter write = [](const std::vector<const Actor*>& ordered, const std::vector<int>& parentIndex) {
        return SceneSerializer::writeActorSet(ordered, parentIndex);
    };
    SnapshotReader read = [this](const std::string& text) {
        return SceneSerializer::readScene(text, *ctx_.world, *ctx_.assets);
    };
    // 되살린 액터를 선택하고 그 행이 보이게 합니다(UE 도 되살린 액터를 선택함).
    auto restored = [this](Actor* root) {
        addSelect(root);
        outlinerPanel_.revealActor(root);
        outlinerPanel_.revealSelected();
        outlinerPanel_.markDirty();
        detailsPanel_.markDirty();
    };
    return std::make_unique<ActorPresenceCmd>(create ? Kind::Create : Kind::Delete, *ctx_.world,
                                              std::move(rootIds), std::move(write), std::move(read),
                                              std::move(restored));
}

void EditorApp::deleteActorUndoable(Actor* root) {
    if (!root || root == viewport_.cameraActor() || !ctx_.world || !ctx_.assets) return;
    // 커맨드는 모두 액터를 ID 로 기억하므로(EditorCommands.hpp) 지워도 걷히지 않습니다. 삭제를 되돌리면 원래 ID 로
    // 되살아나 그 액터의 앞선 편집 이력도 다시 이어집니다(2026-10-15).
    auto cmd = makePresenceCmd(false, { root->id });
    cmd->redo();
    cmdStack_.push(std::move(cmd));
    outlinerPanel_.markDirty();
}

void EditorApp::recordActorCreation(const std::vector<Actor*>& roots) {
    if (!ctx_.world || !ctx_.assets) return;
    std::vector<uint64_t> ids;
    for (Actor* a : roots)
        if (a) ids.push_back(a->id);
    if (ids.empty()) return;
    cmdStack_.push(makePresenceCmd(true, std::move(ids)));
}
