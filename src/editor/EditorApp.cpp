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
        [this](glm::vec2 p) { return logPanel_.popupContains(p); },
        [this](const Actor* a) { return isSelected(a); },
        [this](Actor* a) { selectOnly(a); },
        [this](Actor* a) { toggleSelect(a); },
        [this] { syncEulerCache(); },
        [this] { beginGroupDrag(); },
        [this] { updateGroupDrag(); },
        [this] { groupDrag_.clear(); },
        [this](const Transform& before, const Transform& after) { commitGizmoDrag(before, after); }
    });
    viewport_.spawnEditorCamera(world);

    initPanels();

    contentPanel_.scanAll();
    logPanel_.loadFilter();   // 첫 syncCategories 보다 먼저. 숨긴 카테고리는 등록되는 순간 적용됩니다
    buildWidgetTree();
    outlinerPanel_.rebuild();
    detailsPanel_.rebuild();
    rebuildBottomPanel();

    renderer.setUIRenderer(&slateRenderer_);
    hotReloader_.start(happ::inExeDir("shaders").string(), renderer);
    startAssetWatch();
    HLOG(LogEditor, Log) << "Ready. Press Play to run game simulation.";
}

void EditorApp::initPanels() {
    dock_.init({
        &ui_, &panelReg_, &LogEditor, &uiLayout_,
        [] { return happ::inWriteRoot("Config/editor_layout.txt").string(); },
        [] { std::error_code ec; fs::create_directories(happ::inWriteRoot("Config"), ec); },
        [this](bool outlinerVis, bool bottomVis, bool detailsVis) {
            outlinerVisible_ = outlinerVis;
            const EFocusTarget f = currentFocus();
            if ((!outlinerVis && (f == EFocusTarget::Search || f == EFocusTarget::Rename)) ||
                (!bottomVis   && (f == EFocusTarget::LogSearch || f == EFocusTarget::Console)) ||
                (!detailsVis  && f == EFocusTarget::NumberField))
                setTextFocus(EFocusTarget::None);
            if (!bottomVis) logPanel_.closePopup();
        },
        [this] { outlinerPanel_.cancelDrag(); }
    });
    menus_.init(&ui_, kMenuBarH);
    toolbar_.init({
        &ui_, &ctx_,
        [this] { return playState_ == EPlayState::Playing; },
        [this] { return playState_ == EPlayState::Paused;  },
        [this] { beginPlay(); },
        [this] { if (playState_ != EPlayState::Editor) pausePlay(); },
        [this] { if (playState_ != EPlayState::Editor) stopPlay();  },
        [this] { saveScene(); },
        [this] { openSceneDialog(); },
        [this] { contentPanel_.scanAll(); rebuildBottomPanel(); }
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
        }
    });
    textInput_.init({
        &ctx_.window,
        [this] { return outlinerPanel_.renameField(); },
        [this] { return outlinerPanel_.searchField(); },
        [this] { return logPanel_.searchField(); },
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
        [this] { if (!outlinerVisible_) dock_.showPanel("Outliner"); },
        [this] { return editDragActive(); },
        [this] { deleteSelection(); },
        [this] { copySelection(); },
        [this] { pasteClipboard(); },
        [this] { return logPanel_.consoleField(); }
    });
    detailsPanel_.init({ &ui_, &ctx_, &cmdStack_, &cachedEuler_, &undoSnapshot_,
                         &LogEditor,
                         [this](const std::string& path, const std::string& text) {
                             refreshPrefab(path, text);
                             contentPanel_.refreshPrefabs();
                             rebuildBottomPanel();
                         },
                         [this](const std::string& title, uint64_t current, std::function<void(uint64_t)> pick) {
                             openActorPicker(title, current, std::move(pick));
                         },
                         [this] { outlinerPanel_.markDirty(); } });
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
        [this](const std::string& path) { openAssetActions(path); }
    });
    gpuStatsPanel_.init(&ui_, &ctx_, &vpTimings_, dumpMode_);
    logPanel_.init(&ui_, &LogEditor, kMenuBarH, dumpMode_, [this] {
        setTextFocus(currentFocus() == EFocusTarget::LogSearch ? EFocusTarget::None
                                                               : EFocusTarget::LogSearch);
    });
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
    detailsPanel_.checkWidthChange();

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
    if (!pendingCulture_.empty()) {
        const std::string c = std::move(pendingCulture_);
        pendingCulture_.clear();
        applyCulture(c, true);
    }
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

    if (dumpMode_ && !dumpStateApplied_ && dumpFrameCount_ >= 1) {
        applyDumpState();
        dumpStateApplied_ = true;
    }
    ++dumpFrameCount_;

    // 단축키·메뉴 액션처럼 위젯 이벤트를 거치지 않고 상태만 바뀌는 경로도 여기서 소진합니다.
    flushChromeRebuilds();
    slateRenderer_.context().tickTooltip(dumpMode_ ? (uiHoverActive_ ? 1.0f : 0.f) : dt);
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
    panelReg_.add("Viewport", HTEXT("Editor.Panels", "Viewport", "Viewport").toString(),      viewportPanel, /*closable=*/false);
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
    auto menuPopupHost = menus_.resetPopupHost();
    auto logPopupHost = logPanel_.resetPopupHost();
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
    root->addSlot(logPopupHost);
    root->addSlot(menuPopupHost);
    root->addSlot(dockOverlay);
    root->addSlot(outlinerPanel_.resetDropOverlay());
    root->addSlot(modalHost_);   // 맨 위: 대화상자가 떠 있으면 모든 누름을 받습니다
    slateRenderer_.setRootWidget(root);

    dock_.rebuildArea();
    menus_.rebuildPopup();
    rebuildModal();
}

// ── Menu bar (Phase 1) ───────────────────────────────────────────────────────────
void EditorApp::buildMenus() {
    std::vector<EditorMenus::Def> defs;
    // 글은 지금 문화권(글자 체계 T3), 단축키는 오른쪽 열(번역 안 함).
    defs.push_back({ HTEXT("Editor.Menu", "File", "File").toString(), {
        { HTEXT("Editor.Menu", "File.NewScene", "New Scene").toString(),     [this]{ newScene(); }, {}, "Ctrl+N" },   // 카메라만 남기고 비움
        { HTEXT("Editor.Menu", "File.OpenScene", "Open Scene...").toString(),  [this]{ openSceneDialog(); }, {}, "Ctrl+O" },
        { HTEXT("Editor.Menu", "File.SaveScene", "Save Scene").toString(),    [this]{ saveScene(); }, {}, "Ctrl+S" },
        { HTEXT("Editor.Menu", "File.SaveSceneAs", "Save Scene As...").toString(), [this]{ saveSceneAs(); }, {}, "Ctrl+Shift+S" },
    }, 0.f });
    defs.push_back({ HTEXT("Editor.Menu", "Edit", "Edit").toString(), {
        { HTEXT("Editor.Menu", "Edit.Undo", "Undo").toString(),          [this]{ undo(); }, {}, "Ctrl+Z" },
        { HTEXT("Editor.Menu", "Edit.Redo", "Redo").toString(),          [this]{ redo(); }, {}, "Ctrl+Y" },
        { HTEXT("Editor.Menu", "Edit.Copy", "Copy").toString(),          [this]{ copySelection(); }, {}, "Ctrl+C" },
        { HTEXT("Editor.Menu", "Edit.Paste", "Paste").toString(),         [this]{ pasteClipboard(); }, {}, "Ctrl+V" },
        { HTEXT("Editor.Menu", "Edit.Duplicate", "Duplicate").toString(), [this]{ duplicateSelection(); }, {}, "Ctrl+D" },
        { HTEXT("Editor.Menu", "Edit.Delete", "Delete").toString(),       [this]{ deleteSelection(); }, {}, "Del" },
        { HTEXT("Editor.Menu", "Edit.FrameSelected", "Frame Selected").toString(), [this]{ frameSelection(); }, {}, "F" },
        // 기즈모 크기(카메라 거리 배율)를 작게 0.08 · 보통 0.12 · 크게 0.18 로 돌립니다. 툴바는 폭 사다리 때문에 칩을 더하지 않습니다.
        { HTEXT("Editor.Menu", "Edit.GizmoSize", "Gizmo Size (S/M/L)").toString(),      [this]{
            const float f = viewport_.gizmo().sizeFactor();
            const float n = f < 0.10f ? 0.12f : (f < 0.15f ? 0.18f : 0.08f);
            viewport_.gizmo().setSizeFactor(n);
            HLOG(LogEditor, Log) << "Gizmo size " << (n < 0.10f ? "small" : (n < 0.15f ? "medium" : "large"));
        } },
        { HTEXT("Editor.Menu", "Edit.AudioOutput", "Audio Output...").toString(),         [this]{ openAudioOutputDialog(); } },
    }, 0.f });
    defs.push_back({ HTEXT("Editor.Menu", "Actor", "Actor").toString(), {
        { HTEXT("Editor.Menu", "Actor.AddEmpty", "Add Empty").toString(), [this]{
            if (!ctx_.world) return;
            selectOnly(ctx_.world->spawnActor(outlinerPanel_.nextActorName()));
            recordActorCreation({ ctx_.selected });
        } },
        // 부모 바꾸기는 아웃라이너에서 행을 끌어 놓는 것이 기본이고(2026-10-15), 메뉴도 남겨 둡니다. 둘 다 월드 자세를
        // 유지하고 Undo 됩니다. 부모는 "직전에 선택했던 액터", 자식은 "지금 선택한 액터"입니다.
        { HTEXT("Editor.Menu", "Actor.AttachToPrev", "Attach To Prev Selected").toString(), [this]{
            Actor* child  = ctx_.selected;
            Actor* parent = prevSelected2_;
            if (!child || !parent || child == parent) return;
            if (!reparentUndoable(child, parent))
                HLOG(LogEditor, Warning) << "Cannot attach '" << child->name << "' under '" << parent->name
                                         << "' (an actor cannot go under itself or its child)";
        } },
        { HTEXT("Editor.Menu", "Actor.Detach", "Detach Selected").toString(), [this]{
            if (!ctx_.selected || ctx_.selected == viewport_.cameraActor()) return;
            reparentUndoable(ctx_.selected, nullptr);
        } },
    }, 0.f });
    std::vector<EditorMenus::Item> viewItems = {
        { HTEXT("Editor.Menu", "View.ResetLayout", "Reset Layout").toString(), [this]{ dock_.buildDefaultLayout(); dock_.rebuildArea(); dock_.saveLayout(); } },
    };
    // 언어(글자 체계 T3-10). 고르면 다음 프레임에 표를 읽고 UI 를 다시 만듭니다(누른 단추가 처리 중에 사라지지 않게).
    for (const htext::CultureInfo& c : htext::knownCultures()) {
        const std::string code = c.code;
        viewItems.push_back({ HTEXT("Editor.Menu", "View.Language", "Language").toString() + ": " + c.nativeName,
                              [this, code] { pendingCulture_ = code; },
                              [code] { return htext::culture() == code; } });
    }
    defs.push_back({ HTEXT("Editor.Menu", "View", "View").toString(), std::move(viewItems), 0.f });
    // 등록된 패널을 등록 순서로 보입니다(DEF-69). 열린 패널은 체크 표시가 붙고, 누르면 그 탭을
    // 앞으로 가져옵니다. 탭의 x 로 닫은 패널은 여기서 다시 엽니다.
    std::vector<EditorMenus::Item> windowItems;
    for (const std::string& id : panelReg_.order()) {
        const PanelDef* def = panelReg_.find(id);
        if (!def) continue;
        windowItems.push_back({ def->title,
                                [this, id] { dock_.showPanel(id); },
                                [this, id] { return dock_.contains(id); } });
    }
    defs.push_back({ HTEXT("Editor.Menu", "Window", "Window").toString(), std::move(windowItems), 0.f });
    menus_.setDefs(std::move(defs));
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
    if (bottomDirty_) { bottomDirty_ = false; rebuildBottomPanel(); }
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
        if (bottomTab_ == 1) {
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
// 표준 단축키: Ctrl+Z(undo) / Ctrl+Y·Ctrl+Shift+Z(redo) / Ctrl+S(씬 저장)
void EditorApp::tickShortcuts() {
    if (!ctx_.window) return;
    IWindow& win = *ctx_.window;

    bool zNow = win.isKeyDown(HInput::KEY_Z);
    bool yNow = win.isKeyDown(HInput::KEY_Y);
    bool sNow = win.isKeyDown(HInput::KEY_S);
    bool fNow = win.isKeyDown(HInput::KEY_F);
    bool oNow = win.isKeyDown(HInput::KEY_O);
    bool nNow = win.isKeyDown(HInput::KEY_N);
    bool dNow = win.isKeyDown(HInput::KEY_D);
    bool dEdge = dNow && !prevDDown_;
    prevDDown_ = dNow;
    bool zEdge = zNow && !prevZDown_;
    bool yEdge = yNow && !prevYDown_;
    bool sEdge = sNow && !prevSDown_;
    bool fEdge = fNow && !prevFDown_;
    bool oEdge = oNow && !prevODown_;
    bool nEdge = nNow && !prevNDown_;
    prevZDown_ = zNow;
    prevYDown_ = yNow;
    prevSDown_ = sNow;
    prevFDown_ = fNow;
    prevODown_ = oNow;
    prevNDown_ = nNow;

    if (modal_) return;                                   // 대화상자가 떠 있으면 단축키를 받지 않습니다
    if (win.isCameraModeActive()) return;                 // WASD 비행 중 충돌 방지
    // F — 선택 대상으로 화면 맞추기(Ctrl+F 는 아웃라이너 검색)
    if (fEdge && !win.isCtrlDown() && currentFocus() == EFocusTarget::None) { frameSelection(); return; }
    if (!win.isCtrlDown()) return;
    // 기즈모 · 숫자 칸을 끄는 동안에는 씬을 바꾸거나 저장하는 단축키를 받지 않습니다(Undo · Redo 와 같은 규칙). Ctrl+D 는
    // 주 선택을 새 액터로 바꿔 끄기 결과가 새 액터의 커맨드로 쌓이고(DEF-132), Ctrl+S 는 끄기 중간 값을 파일에 넣어 저장
    // 표지가 어긋납니다(DEF-133). 열기 · 새 씬도 끄는 대상을 없애므로 함께 막습니다.
    if (editDragActive()) {
        if (sEdge || dEdge || oEdge || nEdge) {
            HLOG(LogEditor, Log) << "Finish the drag first (shortcut ignored while dragging a gizmo or a value)";
        }
        return;
    }
    if (dEdge && currentFocus() == EFocusTarget::None) { duplicateSelection(); return; }

    if (sEdge) { if (win.isShiftDown()) saveSceneAs(); else saveScene(); return; }
    if (oEdge) { openSceneDialog(); return; }
    if (nEdge) { newScene(); return; }

    // Ctrl+F → 아웃라이너 검색창 포커스 (기존 검색어는 전체 선택 → 바로 교체 가능). 아웃라이너가
    // 닫혀 있거나 가려진 탭이면 먼저 앞으로 꺼냅니다. 그러지 않으면 보이지 않는 검색창으로 타이핑이 들어갑니다.
    if (fEdge) {
        if (!outlinerVisible_) dock_.showPanel("Outliner");
        setTextFocus(EFocusTarget::Search);
        return;
    }

    // 텍스트 입력 중에는 씬 undo/redo가 직관에 어긋나므로 차단
    if (currentFocus() != EFocusTarget::None) return;   // 텍스트 입력 중에는 단축키를 막습니다

    bool shift = win.isShiftDown();
    if (zEdge && !shift)                 undo();
    else if (yEdge || (zEdge && shift))  redo();
}

// ── handleInput ────────────────────────────────────────────────────────────────
void EditorApp::handleInput(float mouseX, float mouseY,
                             bool leftDown, bool leftJustUp,
                             float scrollDelta, float winW, float winH) {
    if (dumpMode_ && uiHoverActive_) {
        mouseX = uiHoverPos_.x;
        mouseY = uiHoverPos_.y;
    }
    // 위젯 콜백(펼침·눈·삭제)은 리빌드를 직접 하지 않고 플래그만 세웁니다. 슬롯 순회
    // 도중 clearSlots 가 돌면 지금 실행 중인 버튼과 그 std::function 자체가 파괴되기
    // 때문입니다. 이 함수에는 조기 return 이 여러 개라 소진은 스코프 가드로 합니다.
    // 소멸자는 이벤트 디스패치가 전부 끝난 뒤, 이 프레임의 drawFrame 전에 돕니다.
    struct DeferredRebuildGuard {
        EditorApp* self;
        ~DeferredRebuildGuard() {
            self->outlinerPanel_.flushRebuild();
            self->flushChromeRebuilds();
        }
    } deferredRebuildGuard{ this };

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

    // 아웃라이너 목록 리빌드 보류(DEF-83)는 버튼을 떼면 풉니다. 거는 곳은 아래 UI 디스패치입니다.
    if (!leftDown) outlinerPanel_.releasePressHeld();
    // 아웃라이너 행 끌어 놓기(부모 바꾸기). 누른 행 버튼의 클릭과 함께 돕니다.
    outlinerPanel_.tickDrag(mouseX, mouseY, leftDown, prevLeftDown_, (bool)modal_, outlinerVisible_);

    // 편집 중에 다른 곳을 누르면 편집을 끝냅니다. 이 처리가 스플리터·탭·기즈모의 조기
    // return 보다 **앞**에 있어야 합니다. 뒤에 있으면 그 경로들이 커밋을 건너뛰어 편집
    // 모드가 갇히고, 나중에 누른 Enter 가 낡은 값으로 기즈모 결과를 덮어씁니다.
    if (leftDown && !prevLeftDown_ && currentFocus() != EFocusTarget::None &&
        !focusWidgetContains(mouseX, mouseY)) {
        setTextFocus(EFocusTarget::None);
    }

    // Categories 팝업 바깥을 누르면 닫기만 하고 클릭은 그대로 흘려보냅니다. 전면 캐처로 받으면
    // 그 클릭이 닫기에만 쓰여 삼켜집니다(메뉴 팝업의 DEF-64). 팝업 제거는 지연 플래그로
    // 프레임 끝에 일어나므로, 이번 프레임의 디스패치는 팝업이 있는 상태에서 판정됩니다.
    if (leftDown && !prevLeftDown_) {
        logPanel_.handleOutsideClick(glm::vec2{ mouseX, mouseY });
    }

    // ── 메뉴 hover 전환 ──────────────────────────────────────────────────────────
    menus_.handleHoverSwitch(glm::vec2{ mouseX, mouseY });

    // 열린 팝업(메뉴 드롭다운과 그 캐처, Categories 패널) 위의 누름은 아래 스플리터·탭 판정보다
    // UI 가 먼저 받아야 합니다. 예전에는 File > Load Scene 행의 왼쪽 끝을 누르면 그 밑의 Outliner
    // 탭이 클릭을 가져가 메뉴가 열린 채로 남았습니다(DEF-64 의 우회 경로).
    const bool overPopup =
        modal_ != nullptr || menus_.isOpen() ||
        logPanel_.popupContains(glm::vec2{ mouseX, mouseY });

    // ── Dock splitter resize & tab drag ─────────────────────────────────────────
    if (dock_.handleSplitterInput(mouseX, mouseY, leftDown, prevLeftDown_, overPopup, ctx_.window) ||
        dock_.handleTabInput(mouseX, mouseY, leftDown, prevLeftDown_, overPopup, ctx_.window)) {
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
        if (outlinerVisible_)
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
