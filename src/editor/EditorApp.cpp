#include "EditorApp.hpp"
#include "../core/FrameTimings.hpp"
#include "EditorCommands.hpp"
#include "../renderer/Renderer.hpp"
#include "../core/World.hpp"
#include "../core/Actor.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../renderer/MeshHandle.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/LightComponent.hpp"
#include "../components/AudioComponent.hpp"
#include "../audio/AudioSystem.hpp"
#include "../components/RigidBodyComponent.hpp"
#include "../assets/AssetManager.hpp"
#include "../assets/PrefabManager.hpp"
#include "../assets/SceneSerializer.hpp"
#include "../platform/IWindow.hpp"
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>

// ── Helpers ────────────────────────────────────────────────────────────────────
static glm::vec3 quatToEulerDeg(const glm::quat& q) {
    return glm::degrees(glm::vec3(glm::pitch(q), glm::yaw(q), glm::roll(q)));
}

struct PropsSnapshotCmd : ICommand {
    // 이 스냅샷이 속한 액터입니다. props 의 setF/getF 람다가 컴포넌트 원시 포인터를
    // 캡처하므로, 액터가 파괴되면 purge 로 이 커맨드를 반드시 걷어내야 undo/redo 에서
    // 해제된 컴포넌트를 역참조하지 않습니다. refersTo 가 그 연결 고리입니다.
    const Actor*          actor;
    std::vector<PropDesc> props;
    std::vector<float>    before, after;

    PropsSnapshotCmd(const Actor* owner, std::vector<PropDesc> p,
                     std::vector<float> b, std::vector<float> a)
        : actor(owner), props(std::move(p)), before(std::move(b)), after(std::move(a)) {}

    void undo() override { size_t j=0; for (auto& pd : props) if (pd.setF) pd.setF(before[j++]); }
    void redo() override { size_t j=0; for (auto& pd : props) if (pd.setF) pd.setF(after[j++]);  }
    bool refersTo(const Actor* a) const override { return actor == a; }
};

namespace fs = std::filesystem;

// ── Widget helpers ─────────────────────────────────────────────────────────────
std::shared_ptr<HWidget> EditorApp::spacer() const {
    return HNew(SBorder)->background({0.f, 0.f, 0.f, 0.f});
}

std::shared_ptr<HWidget> EditorApp::panelHeader(const std::string& title) const {
    return HNew(SBorder)
        ->background(kBgHeader)->padding(HPadding{s(8.f), s(6.f)})
        ->content(HNew(STextBlock)->text(title)->fontSize(s(13.f))->color(kTextHdr)
            ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center));
}

std::shared_ptr<HWidget> EditorApp::sectionHeader(const std::string& title,
                                                  glm::vec4 color, float fontSize) const {
    return HNew(SBorder)
        ->background(kBgHeader)->padding(HPadding{s(8.f), s(3.f)})
        ->content(HNew(STextBlock)->text(title)->fontSize(s(fontSize))->color(color)
            ->hAlign(EHAlign::Left));
}

std::shared_ptr<SButton> EditorApp::textButton(const std::string& label, float fontSize,
                                               glm::vec4 textColor, HPadding pad,
                                               glm::vec4 normal, glm::vec4 hovered,
                                               std::function<void()> onClick) const {
    return HNew(SButton)
        ->content(HNew(STextBlock)->text(label)->fontSize(s(fontSize))->color(textColor))
        ->padding(HPadding{s(pad.left), s(pad.top), s(pad.right), s(pad.bottom)})
        ->normalColor(normal)->hoveredColor(hovered)
        ->onClick(std::move(onClick));
}

// ── init ───────────────────────────────────────────────────────────────────────
void EditorApp::init(Renderer& renderer, World& world, AssetManager& assets, IWindow& window) {
    ctx_.world    = &world;
    ctx_.assets   = &assets;
    ctx_.renderer = &renderer;
    ctx_.window   = &window;

    // 액터 파괴를 구조적으로 감시합니다. 어떤 파괴 경로(삭제 버튼·단축키·씬 로드·
    // 향후 재귀 파괴)든 이 구독 하나로, 그 액터를 참조하는 undo/redo 커맨드를
    // 걷어내고 선택 포인터가 그 액터를 가리키고 있었다면 비웁니다. 각 호출부가
    // cmdStack_.clear() 를 기억해야 했던 규율을 이벤트 메커니즘으로 대체합니다.
    actorDestroyedSub_ = world.onActorDestroyed.add([this](Actor* a) {
        cmdStack_.purge(a);
        if (ctx_.selected == a) ctx_.selected = nullptr;
    });

    editorCamActor_ = world.spawnActor("Camera");
    editorCamActor_->setLocalPosition({ 0.f, 0.3f, 2.5f });
    editorCam_ = editorCamActor_->addComponent<CameraComponent>();

    origCout_ = std::cout.rdbuf();
    origCerr_ = std::cerr.rdbuf();
    coutBuf_  = new LogStreamBuf(&ctx_, origCout_);
    cerrBuf_  = new LogStreamBuf(&ctx_, origCerr_);
    std::cout.rdbuf(coutBuf_);
    std::cerr.rdbuf(cerrBuf_);

    auto beginCmd = [&renderer]() { return renderer.beginOneTimeCmd(); };
    auto endCmd   = [&renderer](VkCommandBuffer cmd) { renderer.endOneTimeCmd(cmd); };

    slateRenderer_.init(renderer.getDevice(), renderer.getPhysDevice(),
                        renderer.getSwapFormat(), beginCmd, endCmd);

    scanModelFiles();
    buildWidgetTree();
    rebuildOutliner();
    rebuildDetails();
    rebuildBottomPanel();

    renderer.setUIRenderer(&slateRenderer_);
    hotReloader_.start("shaders", renderer);
    std::cout << "[Editor] Ready. Press Play to run game simulation.\n";
}

void EditorApp::cleanup() {
    // 월드가 에디터보다 오래 살 수 있으므로, this 를 캡처한 리스너를 먼저 뗍니다.
    if (ctx_.world) ctx_.world->onActorDestroyed.remove(actorDestroyedSub_);
    hotReloader_.stop();
    slateRenderer_.cleanup();
    std::cout.rdbuf(origCout_); origCout_ = nullptr;
    std::cerr.rdbuf(origCerr_); origCerr_ = nullptr;
    delete coutBuf_; coutBuf_ = nullptr;
    delete cerrBuf_; cerrBuf_ = nullptr;
}

// ── tick ───────────────────────────────────────────────────────────────────────
void EditorApp::tick(float dt) {
    if (!ctx_.world) return;

    if (hotReloader_.reloadPending() && ctx_.renderer)
        ctx_.renderer->reloadPipelines();

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
        vpTimings_.taaBlitMs       = t.taaBlitMs;
        vpTimings_.uiMs            = t.uiMs;
        vpTimings_.totalMs         = t.totalMs;
        vpTimings_.drawCalls       = s.drawCalls;
        vpTimings_.culled          = s.culled;
        vpTimings_.transparentDraws= s.transparentDraws;
    }

    tickEditorCamera(dt);
    tickRenameInput();
    tickShortcuts();

    // Gizmo hotkeys Q/W/E/R (edge trigger, only when not in camera mode / not typing)
    if (ctx_.window && !renaming_ && !outlinerSearchFocused_ &&
        !SNumberField::focusedField() && !ctx_.window->isCameraModeActive()) {
        IWindow& win = *ctx_.window;
        bool qNow = win.isKeyDown(HInput::KEY_Q);
        bool wNow = win.isKeyDown(HInput::KEY_W);
        bool eNow = win.isKeyDown(HInput::KEY_E);
        bool rNow = win.isKeyDown(HInput::KEY_R);
        if (qNow && !prevGizQDown_) gizmoMode_ = 0;
        if (wNow && !prevGizWDown_) gizmoMode_ = 1;
        if (eNow && !prevGizEDown_) gizmoMode_ = 2;
        if (rNow && !prevGizRDown_) gizmoMode_ = 3;
        prevGizQDown_ = qNow; prevGizWDown_ = wNow;
        prevGizEDown_ = eNow; prevGizRDown_ = rNow;
    }

    int fw = 1280, fh = 720;
    if (ctx_.window) ctx_.window->getFramebufferSize(fw, fh);

    float newScale = std::clamp(fh / 1080.0f, 1.0f, 2.5f);
    if (std::abs(newScale - uiScale_) > 0.04f) {
        uiScale_ = newScale;
        buildWidgetTree();
        rebuildOutliner();
        rebuildDetails();
        rebuildBottomPanel();
    }

    if (ctx_.renderer && ctx_.world && ctx_.window) {
        float mx, my;
        ctx_.window->getMousePosition(mx, my);
        int wW = 1, wH = 1;
        ctx_.window->getWindowSize(wW, wH);
        // 씬을 이 뷰포트 크기·위치로 렌더하도록 렌더러에 알립니다. 값이 바뀔 때만 타깃을
        // 재생성합니다. 렌더러의 씬 렌더 대상과 아래 worldVP 좌표가 같은 rect 를 씁니다.
        const HViewportRect vfr = viewportFbRect();
        ctx_.renderer->setViewportRect((int)vfr.left, (int)vfr.top,
                                       (int)vfr.width, (int)vfr.height);
        // 좌표는 worldVP 하나로만 만듭니다. inView 게이팅만 윈도 좌표를 씁니다.
        const WorldViewport wvp = worldVP(mx, my, (float)wW, (float)wH);
        const bool inView = inViewport(mx, my, (float)wW, (float)wH);
        // 기즈모와 아이콘을 한 리스트에 모아 한 번에 설정합니다.
        std::vector<LineVertex> lines, tris;
        gizmo_.tick(ctx_.selected, *ctx_.world, wvp, s(18.f),
                    gizmoMode_, worldSpace_, inView, lines, tris);
        buildEditorIcons(*ctx_.world, wvp, lines, tris);
        ctx_.renderer->setEditorLines(std::move(lines));
        ctx_.renderer->setGizmoTris(std::move(tris));
    }

    int  actorCount    = (int)ctx_.world->getActors().size();
    bool selChanged    = (ctx_.selected != prevSelected_);
    bool renamChanged  = (renaming_ != prevRenaming_);

    if (actorCount != prevActorCount_ || selChanged || renamChanged) {
        rebuildOutliner();
        prevActorCount_ = actorCount;
    }
    prevRenaming_ = renaming_;

    if (selChanged) {
        syncEulerCache();
        rebuildDetails();
        prevSelected_ = ctx_.selected;
        if (renaming_) { renaming_ = false; }
    }
    // exchange로 플래그를 먼저 내려야 리빌드 중 도착한 로그가 다음 프레임에 잡힌다
    if (ctx_.logDirty.exchange(false, std::memory_order_acquire))
        rebuildBottomPanel();
}

// ── scanModelFiles ─────────────────────────────────────────────────────────────
void EditorApp::scanModelFiles() {
    modelFiles_.clear();
    try {
        fs::path dir = "assets/models";
        if (fs::exists(dir)) {
            for (auto& e : fs::recursive_directory_iterator(dir)) {
                auto ext = e.path().extension();
                if (ext == ".glb" || ext == ".gltf")
                    modelFiles_.push_back(e.path().generic_string());
            }
            std::sort(modelFiles_.begin(), modelFiles_.end());
        }
    } catch (...) {}

    audioFiles_.clear();
    try {
        fs::path dir = "assets/audio";
        if (fs::exists(dir)) {
            for (auto& e : fs::directory_iterator(dir)) {
                auto ext = e.path().extension().string();
                if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
                    audioFiles_.push_back(e.path().filename().string());
            }
            std::sort(audioFiles_.begin(), audioFiles_.end());
        }
    } catch (...) {}

    prefabFiles_ = PrefabManager::scan();
}

// ── buildWidgetTree ────────────────────────────────────────────────────────────
// 패널을 헤더 없이(도크 탭 바가 제목을 대신) 만들어 PanelRegistry 에 등록하고, 도크
// 트리(dockRoot_)를 dockHost_ 에 조합합니다. root 는 [toolbar / dockHost_] 뿐이라, 이후
// 메뉴바·상태바 같은 고정 크롬은 도크 영역 바깥에 얹으면 됩니다.
void EditorApp::buildWidgetTree() {
    using VR = SVerticalBox;
    using HR = SHorizontalBox;
    using BR = SBorder;

    // ── Panel: Outliner (search + list) ───────────────────────────────────────
    outlinerList_ = HNew(VR);
    auto outlinerSearch = HNew(SSearchBox)
        ->placeholder("Search...")
        ->text(AttrStr([this]() -> std::string { return outlinerSearch_; }))
        ->focused([this]() -> bool { return outlinerSearchFocused_; })
        ->selectedAll([this]() -> bool { return searchSelAll_; })
        ->onClick([this]{
            outlinerSearchFocused_ = !outlinerSearchFocused_;
            searchSelAll_          = false;
        })
        ->fontSize(s(12.f));
    auto outlinerPanel = HNew(BR)
        ->background(kBgPanel)
        ->content(HNew(VR)
            ->addSlot(HNew(BR)->background({0,0,0,0})->padding(HPadding{s(4.f), s(3.f)})
                ->content(outlinerSearch), VR::ESizeRule::Fixed, s(34.f))
            ->addSlot(outlinerList_, VR::ESizeRule::Fill));

    // ── Panel: Details ─────────────────────────────────────────────────────────
    detailsList_ = HNew(VR);
    auto detailsPanel = HNew(BR)->background(kBgPanel)->content(detailsList_);

    // ── Panel: Viewport (transparent + stats overlay) ─────────────────────────
    viewportWidget_ = HNew(BR)->background({0.f, 0.f, 0.f, 0.f});
    auto statsText = HNew(STextBlock)
        ->text(AttrStr([this]() -> std::string {
            // GPU 줄과 CPU 줄을 함께 보여 줍니다. GPU 합계가 작은데 Loop 가 크면
            // 그 차이가 곧 "어느 계측기에도 잡히지 않는 시간"이고, 그때는
            // Fence / Acquire / Present 중 어디서 막히는지를 아래 줄로 봅니다.
            const auto& f = gFrameTimings();
            char buf[512];
            snprintf(buf, sizeof(buf),
                "%d FPS  |  Draw: %d  Culled: %d  Transp: %d  |  Swapchain recreate/s: %.0f\n"
                "GPU   Shadow %.2f  Main %.2f  AO %.2f  Bloom %.2f  Tonemap %.2f  TAA %.2f  UI %.2f  Total %.2f\n"
                "CPU   Fence %.2f  Acquire %.2f  ImgFence %.2f  Extract %.2f  Record %.2f (UI %.2f)  Submit %.2f  Present %.2f\n"
                "LOOP  Poll %.2f  Tick %.2f  Input %.2f  DrawFrame %.2f  |  Frame %.2f ms",
                vpTimings_.fps, vpTimings_.drawCalls, vpTimings_.culled,
                vpTimings_.transparentDraws, vpTimings_.recreatePerSec,
                vpTimings_.shadowMs, vpTimings_.mainMs, vpTimings_.aoMs, vpTimings_.bloomMs,
                vpTimings_.tonemapMs, vpTimings_.taaBlitMs, vpTimings_.uiMs, vpTimings_.totalMs,
                f.fenceWait.v, f.acquire.v, f.imgFence.v, f.extract.v,
                f.record.v, f.uiRecord.v, f.submit.v, f.present.v,
                f.pollEvents.v, f.tick.v, f.handleInput.v, f.drawFrame.v, f.loopTotal.v);
            return buf;
        }))
        ->fontSize(s(12.f))->color({0.85f, 0.90f, 0.85f, 1.f})
        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Top);
    auto viewportPanel = HNew(SOverlay);
    viewportPanel->addSlot(viewportWidget_);
    viewportPanel->addSlot(
        HNew(BR)->background({0.f, 0.f, 0.f, 0.55f})->padding(HPadding{s(6.f), s(4.f)})
            ->content(statsText),
        EHAlign::Left, EVAlign::Top, HPadding{s(6.f), s(6.f)});

    // ── Panel: Bottom (internal Content/Log/GPU tabs) ─────────────────────────
    bottomContent_ = HNew(VR);
    auto tabBar = HNew(STabBar)
        ->addTab("Content Browser")
        ->addTab("Output Log")
        ->addTab("GPU Stats")
        ->tabWidth(s(145.f))->height(s(30.f))->fontSize(s(13.f))
        ->activeTab([this]() -> int { return bottomTab_; })
        ->onTabChanged([this](int i){ bottomTab_ = i; rebuildBottomPanel(); });
    auto bottomPanel = HNew(BR)
        ->background(kBgPanel)
        ->content(HNew(VR)
            ->addSlot(tabBar,         VR::ESizeRule::Fixed, s(30.f))
            ->addSlot(HNew(BR)->background({0,0,0,0})->content(bottomContent_), VR::ESizeRule::Fill));

    // ── Register panels (신규 위젯도 add 한 줄이면 도킹 대상 — 확장성 seam) ──────
    panelReg_.add("Outliner", "Outliner",       outlinerPanel);
    panelReg_.add("Viewport", "Viewport",       viewportPanel, /*closable=*/false);
    panelReg_.add("Details",  "Details",        detailsPanel);
    panelReg_.add("Bottom",   "Content Drawer", bottomPanel);

    if (!dockRoot_) buildDefaultDockLayout();

    // ── Menu bar (최상단 크롬) ──────────────────────────────────────────────────
    menuBarBox_ = HNew(HR);
    auto menuBar = HNew(BR)
        ->background(kBgHeader)->borderColor(kBorder)->borderThickness(1.f)
        ->content(menuBarBox_);
    buildMenus();
    rebuildMenuBar();

    // ── Toolbar (도크 영역 바깥의 고정 크롬) ────────────────────────────────────
    toolbarBox_ = HNew(HR);
    auto toolbar = HNew(BR)
        ->background(kBgToolbar)->borderColor(kBorder)->borderThickness(1.f)
        ->padding(HPadding{4.f, 0.f})
        ->content(toolbarBox_);
    rebuildToolbar();

    // ── Dock host + root ───────────────────────────────────────────────────────
    // root = SOverlay[ (menuBar / toolbar / dock area) , 메뉴 팝업 , 드롭존 오버레이 ].
    // dockHost_ 는 반드시 투명이어야 합니다(불투명이면 도크 영역 전체를 덮어 뷰포트 위젯
    // 자리의 씬이 가려짐). 좌표가 geometry 기반이라 메뉴 바를 얹어도 뷰포트가 자동으로 따라옵니다.
    dockHost_      = HNew(BR)->background({0.f, 0.f, 0.f, 0.f});
    dockOverlay_   = HNew(SDockOverlay);
    menuPopupHost_ = HNew(BR)->background({0.f, 0.f, 0.f, 0.f});
    auto rootStack = HNew(VR)
        ->addSlot(menuBar,   VR::ESizeRule::Fixed, s(kMenuBarH))
        ->addSlot(toolbar,   VR::ESizeRule::Fixed, s(kToolbarH))
        ->addSlot(dockHost_, VR::ESizeRule::Fill,  1.f);
    auto root = HNew(SOverlay);
    root->addSlot(rootStack);
    root->addSlot(menuPopupHost_);
    root->addSlot(dockOverlay_);
    slateRenderer_.setRootWidget(root);

    rebuildDockArea();
    rebuildMenuPopup();
}

// ── buildDefaultDockLayout ───────────────────────────────────────────────────────
// 기본 배치 = 예전 하드코딩 레이아웃과 동일: 세로[ 가로[Outliner|Viewport|Details] / Bottom ].
void EditorApp::buildDefaultDockLayout() {
    using S = DockNode::Split;
    std::vector<std::unique_ptr<DockNode>> topKids;
    topKids.push_back(DockNode::makeLeaf({"Outliner"}));
    topKids.push_back(DockNode::makeLeaf({"Viewport"}));
    topKids.push_back(DockNode::makeLeaf({"Details"}));
    auto topRow = DockNode::makeSplit(S::Horizontal, std::move(topKids), {0.20f, 0.60f, 0.20f});

    std::vector<std::unique_ptr<DockNode>> rootKids;
    rootKids.push_back(std::move(topRow));
    rootKids.push_back(DockNode::makeLeaf({"Bottom"}));
    dockRoot_ = DockNode::makeSplit(S::Vertical, std::move(rootKids), {0.72f, 0.28f});
}

// ── rebuildDockArea ──────────────────────────────────────────────────────────────
void EditorApp::rebuildDockArea() {
    dockSplitters_.clear();
    dockTabHits_.clear();
    dockLeafHits_.clear();
    if (dockHost_ && dockRoot_)
        dockHost_->content(buildDockWidget(dockRoot_.get()));
}

// ── buildDockWidget ──────────────────────────────────────────────────────────────
// 도크 노드를 재귀적으로 조합합니다. 리프 = 탭 바 + 활성 패널, 분할 = Fill 가중치(=비율)로
// 배치한 SHorizontalBox/SVerticalBox + 자식 사이 얇은 핸들(스플리터). 핸들 지오메트리는
// dockSplitters_ 에 기록해 handleInput 의 드래그가 히트테스트합니다.
std::shared_ptr<HWidget> EditorApp::buildDockWidget(DockNode* node) {
    using VR = SVerticalBox;
    using HR = SHorizontalBox;
    using BR = SBorder;
    if (!node) return spacer();

    if (node->isLeaf()) {
        auto tabRow = HNew(HR);
        for (int i = 0; i < (int)node->panelIds.size(); ++i) {
            const PanelDef* def = panelReg_.find(node->panelIds[i]);
            std::string title = def ? def->title : node->panelIds[i];
            const bool active = (i == node->activeTab);
            // 탭은 시각 위젯입니다(onClick 없음). 클릭=활성 전환, 드래그=재도킹은
            // handleInput 이 dockTabHits_ 로 처리합니다(SButton onClick 과의 충돌 회피).
            auto tab = HNew(BR)
                ->background(active ? kBgPanel : kBgHeader)
                ->borderColor(kBorder)->borderThickness(1.f)
                ->padding(HPadding{s(12.f), s(6.f)})
                ->content(HNew(STextBlock)->text(title)->fontSize(s(12.f))
                    ->color(active ? kTextBody : kTextDim)->vAlign(EVAlign::Center));
            tabRow->addSlot(tab, HR::ESizeRule::Auto);
            dockTabHits_.push_back({ tab, node, i, node->panelIds[i] });
        }
        tabRow->addSlot(spacer(), HR::ESizeRule::Fill);

        std::shared_ptr<HWidget> content;
        if (node->activeTab >= 0 && node->activeTab < (int)node->panelIds.size()) {
            const PanelDef* def = panelReg_.find(node->panelIds[node->activeTab]);
            if (def) content = def->widget;
        }

        auto leaf = HNew(VR)
            ->addSlot(HNew(BR)->background(kBgHeader)->borderColor(kBorder)->borderThickness(1.f)
                ->content(tabRow), VR::ESizeRule::Fixed, s(26.f))
            ->addSlot(content ? content : spacer(), VR::ESizeRule::Fill);
        dockLeafHits_.push_back({ leaf, node });
        return leaf;
    }

    const bool vertical = (node->split == DockNode::Split::Vertical);
    if (vertical) {
        auto box = HNew(VR);
        for (size_t i = 0; i < node->children.size(); ++i) {
            const float r = (i < node->ratios.size()) ? node->ratios[i] : 1.f;
            box->addSlot(buildDockWidget(node->children[i].get()), VR::ESizeRule::Fill, r);
            if (i + 1 < node->children.size()) {
                auto handle = HNew(BR)->background(kBorder);
                box->addSlot(handle, VR::ESizeRule::Fixed, s(5.f));
                dockSplitters_.push_back({ handle, box, node, (int)i, true });
            }
        }
        return box;
    }
    auto box = HNew(HR);
    for (size_t i = 0; i < node->children.size(); ++i) {
        const float r = (i < node->ratios.size()) ? node->ratios[i] : 1.f;
        box->addSlot(buildDockWidget(node->children[i].get()), HR::ESizeRule::Fill, r);
        if (i + 1 < node->children.size()) {
            auto handle = HNew(BR)->background(kBorder);
            box->addSlot(handle, HR::ESizeRule::Fixed, s(5.f));
            dockSplitters_.push_back({ handle, box, node, (int)i, false });
        }
    }
    return box;
}

// ── 도크 트리 변형 헬퍼 ──────────────────────────────────────────────────────────
// 노드 포인터는 위젯과 독립이라 트리 변형 사이에서도 안정적입니다. 부모 찾기·비율 정규화.
static DockNode* dockFindParent(DockNode* cur, DockNode* target, int& outIdx) {
    if (!cur || cur->isLeaf()) return nullptr;
    for (size_t i = 0; i < cur->children.size(); ++i) {
        if (cur->children[i].get() == target) { outIdx = (int)i; return cur; }
        if (DockNode* p = dockFindParent(cur->children[i].get(), target, outIdx)) return p;
    }
    return nullptr;
}
static void dockNormalize(DockNode* n) {
    if (!n || n->ratios.empty()) return;
    float sum = 0.f; for (float r : n->ratios) sum += r;
    if (sum <= 1e-6f) { for (float& r : n->ratios) r = 1.f / (float)n->ratios.size(); return; }
    for (float& r : n->ratios) r /= sum;
}

// ── performRedock ────────────────────────────────────────────────────────────────
// 드롭 시 소스 탭을 빼서 타깃에 붙입니다. Center=탭화, L/R/T/B=분할(부모 방향이 같으면
// 형제로 삽입, 아니면 타깃을 새 SplitNode 로 감싸기). 소스 리프가 비면 접어 정리합니다.
void EditorApp::performRedock(DockNode* src, int srcIdx, const std::string& panelId,
                              DockNode* target, EDockZone zone) {
    if (!src || !target || zone == EDockZone::None) return;
    if (srcIdx < 0 || srcIdx >= (int)src->panelIds.size()) return;
    // 자기 자신에 드롭: Center 이거나 유일 패널이면 무의미
    if (src == target && (zone == EDockZone::Center || src->panelIds.size() <= 1)) return;

    // 1) 소스에서 제거
    src->panelIds.erase(src->panelIds.begin() + srcIdx);
    if (src->activeTab >= (int)src->panelIds.size())
        src->activeTab = std::max(0, (int)src->panelIds.size() - 1);

    // 2) 타깃에 추가
    if (zone == EDockZone::Center) {
        target->panelIds.push_back(panelId);
        target->activeTab = (int)target->panelIds.size() - 1;
    } else {
        const bool horiz  = (zone == EDockZone::Left || zone == EDockZone::Right);
        const bool before = (zone == EDockZone::Left || zone == EDockZone::Top);
        const DockNode::Split want = horiz ? DockNode::Split::Horizontal : DockNode::Split::Vertical;
        auto newLeaf = DockNode::makeLeaf({ panelId });

        int pidx = -1;
        DockNode* parent = dockFindParent(dockRoot_.get(), target, pidx);
        if (parent && parent->split == want) {
            const int insertAt = before ? pidx : pidx + 1;
            const float share = (pidx < (int)parent->ratios.size())
                              ? parent->ratios[pidx]
                              : 1.f / (float)std::max<size_t>(1, parent->children.size());
            const float half = share * 0.5f;
            if (pidx < (int)parent->ratios.size()) parent->ratios[pidx] = half;
            parent->children.insert(parent->children.begin() + insertAt, std::move(newLeaf));
            parent->ratios.insert(parent->ratios.begin() + insertAt, half);
            dockNormalize(parent);
        } else {
            std::unique_ptr<DockNode> targetOwned =
                parent ? std::move(parent->children[pidx]) : std::move(dockRoot_);
            std::vector<std::unique_ptr<DockNode>> kids;
            if (before) { kids.push_back(std::move(newLeaf)); kids.push_back(std::move(targetOwned)); }
            else        { kids.push_back(std::move(targetOwned)); kids.push_back(std::move(newLeaf)); }
            auto split = DockNode::makeSplit(want, std::move(kids), { 0.5f, 0.5f });
            if (parent) parent->children[pidx] = std::move(split);
            else        dockRoot_ = std::move(split);
        }
    }

    // 3) 소스 리프가 비면 정리
    if (src->panelIds.empty())
        collapseEmptyLeaf(src);

    rebuildDockArea();
}

// ── collapseEmptyLeaf ────────────────────────────────────────────────────────────
// 빈 리프를 부모에서 제거하고, 부모의 자식이 하나만 남으면 부모를 접어 그 자식으로 대체합니다.
void EditorApp::collapseEmptyLeaf(DockNode* leaf) {
    int idx = -1;
    DockNode* parent = dockFindParent(dockRoot_.get(), leaf, idx);
    if (!parent) return;   // 루트 리프가 비는 경우는 없음(방어적)
    parent->children.erase(parent->children.begin() + idx);
    if (idx < (int)parent->ratios.size()) parent->ratios.erase(parent->ratios.begin() + idx);
    dockNormalize(parent);

    if (parent->children.size() == 1) {
        int pidx = -1;
        DockNode* grand = dockFindParent(dockRoot_.get(), parent, pidx);
        std::unique_ptr<DockNode> only = std::move(parent->children[0]);
        if (grand) grand->children[pidx] = std::move(only);
        else       dockRoot_ = std::move(only);
    }
}

// ── Menu bar (Phase 1) ───────────────────────────────────────────────────────────
void EditorApp::buildMenus() {
    menus_.clear();
    menus_.push_back({ "File", {
        { "Save Scene", [this]{ saveScene(); } },
        { "Load Scene", [this]{ loadScene(); } },
    }, 0.f });
    menus_.push_back({ "Edit", {
        { "Undo", [this]{ undo(); } },
        { "Redo", [this]{ redo(); } },
    }, 0.f });
    menus_.push_back({ "Actor", {
        { "Add Empty", [this]{
            if (ctx_.world)
                ctx_.selected = ctx_.world->spawnActor("Actor_" + std::to_string(actorCounter_++));
        } },
    }, 0.f });
    menus_.push_back({ "View", {
        { "Reset Layout", [this]{ buildDefaultDockLayout(); rebuildDockArea(); } },
    }, 0.f });
}

void EditorApp::rebuildMenuBar() {
    if (!menuBarBox_) return;
    menuBarBox_->clearSlots();
    menuBarBox_->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(6.f));
    float x = s(6.f);
    for (int mi = 0; mi < (int)menus_.size(); ++mi) {
        menus_[mi].x = x;                     // 팝업 위치용 x(스케일 픽셀)
        const float w = s(58.f);
        const bool open = (openMenu_ == mi);
        menuBarBox_->addSlot(
            HNew(SButton)
                ->normalColor (open ? glm::vec4{0.20f, 0.20f, 0.26f, 1.f}
                                    : glm::vec4{0.f,   0.f,   0.f,   0.f})
                ->hoveredColor({0.18f, 0.18f, 0.22f, 1.f})
                ->pressedColor({0.24f, 0.24f, 0.30f, 1.f})
                ->padding(HPadding{s(10.f), s(4.f)})
                ->onClick([this, mi]{
                    openMenu_ = (openMenu_ == mi) ? -1 : mi;
                    rebuildMenuBar(); rebuildMenuPopup();
                })
                ->content(HNew(STextBlock)->text(menus_[mi].name)->fontSize(s(13.f))
                    ->color(kTextBody)->hAlign(EHAlign::Center)->vAlign(EVAlign::Center)),
            SHorizontalBox::ESizeRule::Fixed, w);
        x += w;
    }
    menuBarBox_->addSlot(spacer(), SHorizontalBox::ESizeRule::Fill);
}

void EditorApp::rebuildMenuPopup() {
    if (!menuPopupHost_) return;
    if (openMenu_ < 0 || openMenu_ >= (int)menus_.size()) {
        menuPopupHost_->content(nullptr);
        return;
    }
    const MenuDef& md = menus_[openMenu_];

    auto items = HNew(SVerticalBox);
    for (int k = 0; k < (int)md.items.size(); ++k) {
        items->addSlot(
            HNew(SButton)
                ->normalColor ({0.14f, 0.14f, 0.17f, 1.f})
                ->hoveredColor({0.20f, 0.35f, 0.55f, 1.f})
                ->pressedColor({0.10f, 0.30f, 0.50f, 1.f})
                ->padding(HPadding{s(16.f), s(6.f)})
                ->onClick([this, k]{
                    if (openMenu_ < 0 || openMenu_ >= (int)menus_.size()) return;
                    if (k < 0 || k >= (int)menus_[openMenu_].items.size()) return;
                    auto act = menus_[openMenu_].items[k].action;
                    openMenu_ = -1;
                    rebuildMenuBar(); rebuildMenuPopup();
                    if (act) act();
                })
                ->content(HNew(STextBlock)->text(md.items[k].label)->fontSize(s(13.f))
                    ->color(kTextBody)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
            SVerticalBox::ESizeRule::Fixed, s(28.f));
    }
    auto dropdown = HNew(SBorder)
        ->background({0.10f, 0.10f, 0.12f, 1.f})->borderColor(kBorder)->borderThickness(1.f)
        ->content(items);

    // 캐처(전면 투명 버튼) 위에 드롭다운. 바깥 클릭 시 닫힘. (메뉴 전환은 2클릭 — 추후 개선)
    auto inner = HNew(SOverlay);
    inner->addSlot(
        HNew(SButton)->normalColor({0,0,0,0})->hoveredColor({0,0,0,0})->pressedColor({0,0,0,0})
            ->onClick([this]{ openMenu_ = -1; rebuildMenuBar(); rebuildMenuPopup(); })
            ->content(spacer()));
    inner->addSlot(dropdown, EHAlign::Left, EVAlign::Top, HPadding{md.x, s(kMenuBarH)});
    menuPopupHost_->content(inner);
}

// ── rebuildToolbar ─────────────────────────────────────────────────────────────
void EditorApp::rebuildToolbar() {
    if (!toolbarBox_) return;
    toolbarBox_->clearSlots();

    using HR = SHorizontalBox;

    // ── Gizmo mode buttons ─────────────────────────────────────────────────────
    auto gizmoBtn = [&](const char* label, int mode) {
        toolbarBox_->addSlot(
            HNew(SToggleButton)
                ->content(HNew(STextBlock)->text(label)->fontSize(s(13.f))->color(kTextBody))
                ->padding(HPadding{s(12.f), s(6.f)})
                ->isActive([this, mode]() -> bool { return gizmoMode_ == mode; })
                ->onClick([this, mode]{ gizmoMode_ = mode; }),
            HR::ESizeRule::Auto);
    };
    gizmoBtn("Q", 0); gizmoBtn("W", 1); gizmoBtn("E", 2); gizmoBtn("R", 3);

    toolbarBox_->addSlot(HNew(SBorder)->background(kBorder), HR::ESizeRule::Fixed, 1.f);

    // World / Local toggle
    toolbarBox_->addSlot(
        HNew(SToggleButton)
            ->content(HNew(STextBlock)->text("World")->fontSize(s(13.f))->color(kTextBody))
            ->padding(HPadding{s(12.f), s(6.f)})
            ->isActive([this]() -> bool { return worldSpace_; })
            ->onClick([this]{ worldSpace_ = true; }),
        HR::ESizeRule::Auto);
    toolbarBox_->addSlot(
        HNew(SToggleButton)
            ->content(HNew(STextBlock)->text("Local")->fontSize(s(13.f))->color(kTextBody))
            ->padding(HPadding{s(12.f), s(6.f)})
            ->isActive([this]() -> bool { return !worldSpace_; })
            ->onClick([this]{ worldSpace_ = false; }),
        HR::ESizeRule::Auto);

    toolbarBox_->addSlot(HNew(SBorder)->background(kBorder), HR::ESizeRule::Fixed, 1.f);
    toolbarBox_->addSlot(spacer(), HR::ESizeRule::Fixed, s(4.f));

    // ── Play / Pause / Stop ────────────────────────────────────────────────────
    const bool playing = (playState_ == EPlayState::Playing);
    const bool paused  = (playState_ == EPlayState::Paused);
    const bool inGame  = playing || paused;

    auto playBtn = [&](const char* label, glm::vec4 norm, glm::vec4 hov, std::function<void()> fn) {
        toolbarBox_->addSlot(textButton(label, 13.f, kTextBody, {12.f, 6.f}, norm, hov, std::move(fn)),
                             HR::ESizeRule::Auto);
        toolbarBox_->addSlot(spacer(), HR::ESizeRule::Fixed, s(2.f));
    };

    glm::vec4 playNorm  = playing ? glm::vec4{0.10f, 0.50f, 0.20f, 1.f} : glm::vec4{0.20f, 0.20f, 0.22f, 1.f};
    glm::vec4 pauseNorm = paused  ? glm::vec4{0.68f, 0.50f, 0.08f, 1.f} : glm::vec4{0.20f, 0.20f, 0.22f, 1.f};
    glm::vec4 stopNorm  = inGame  ? glm::vec4{0.50f, 0.10f, 0.10f, 1.f} : glm::vec4{0.14f, 0.14f, 0.16f, 1.f};

    playBtn(paused ? "Resume" : "Play", playNorm, {0.18f, 0.62f, 0.30f, 1.f}, [this]{ beginPlay(); });
    playBtn("Pause", pauseNorm, {0.78f, 0.60f, 0.10f, 1.f}, [this]{ if (playState_ != EPlayState::Editor) pausePlay(); });
    playBtn("Stop",  stopNorm,  {0.65f, 0.15f, 0.15f, 1.f}, [this]{ if (playState_ != EPlayState::Editor) stopPlay(); });

    toolbarBox_->addSlot(HNew(SBorder)->background(kBorder), HR::ESizeRule::Fixed, 1.f);
    toolbarBox_->addSlot(spacer(), HR::ESizeRule::Fixed, s(4.f));

    // ── Scene buttons ──────────────────────────────────────────────────────────
    auto sceneBtn = [&](const char* label, std::function<void()> cb) {
        toolbarBox_->addSlot(
            textButton(label, 13.f, kTextBody, {10.f, 6.f},
                       {0.20f, 0.20f, 0.22f, 1.f}, {0.30f, 0.30f, 0.35f, 1.f}, std::move(cb)),
            HR::ESizeRule::Auto);
        toolbarBox_->addSlot(spacer(), HR::ESizeRule::Fixed, s(2.f));
    };
    sceneBtn("Save",    [this]{ saveScene(); });
    sceneBtn("Load",    [this]{ loadScene(); });
    sceneBtn("Refresh", [this]{ scanModelFiles(); rebuildBottomPanel(); });

    toolbarBox_->addSlot(spacer(), HR::ESizeRule::Fill);

    // ── VRAM stats (right-aligned) ────────────────────────────────────────────
    toolbarBox_->addSlot(
        HNew(STextBlock)
            ->text(AttrStr([this]() -> std::string {
                if (!ctx_.renderer) return "";
                auto st = ctx_.renderer->getAllocStats();
                char buf[128];
                snprintf(buf, sizeof(buf), "VRAM  A: %.1f/%.0fMB  C: %.1f/%.0fMB",
                    st.zoneAUsed / (1024.f * 1024.f),
                    st.zoneATotal / (1024.f * 1024.f),
                    st.zoneCUsed / (1024.f * 1024.f),
                    st.zoneCTotal / (1024.f * 1024.f));
                return buf;
            }))
            ->fontSize(s(12.f))->color(kTextDim)
            ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
        HR::ESizeRule::Fixed, s(260.f));

    toolbarBox_->addSlot(spacer(), HR::ESizeRule::Fixed, s(6.f));
}

// ── Play state ─────────────────────────────────────────────────────────────────
void EditorApp::beginPlay() {
    if (playState_ == EPlayState::Playing) return;
    if (playState_ == EPlayState::Editor) {
        worldSnapshot_ = serializeWorld();
        std::cout << "[Editor] ▶ Play\n";
    } else {
        std::cout << "[Editor] ▶ Resume\n";
    }
    playState_ = EPlayState::Playing;
    rebuildToolbar();
}

void EditorApp::pausePlay() {
    if (playState_ == EPlayState::Playing) {
        playState_ = EPlayState::Paused;
        std::cout << "[Editor] ⏸ Paused\n";
    } else if (playState_ == EPlayState::Paused) {
        playState_ = EPlayState::Playing;
        std::cout << "[Editor] ▶ Resumed\n";
    }
    rebuildToolbar();
}

void EditorApp::stopPlay() {
    if (playState_ == EPlayState::Editor) return;
    playState_ = EPlayState::Editor;
    deserializeWorld(worldSnapshot_);
    worldSnapshot_.clear();
    cmdStack_.clear();
    std::cout << "[Editor] ⏹ Stopped — world restored\n";
    rebuildToolbar();
}

// ── Serialization ──────────────────────────────────────────────────────────────
std::string EditorApp::serializeWorld() {
    std::ostringstream oss;
    oss << "# Haruka Scene v2\n";

    // 직렬화 세트(에디터 카메라 제외)에서 각 액터에 순번을 매겨, 부모를 인덱스로
    // 참조합니다. 부모가 세트 밖(예: 에디터 카메라)이면 -1 로 기록해 링크를 남기지 않습니다.
    std::unordered_map<const Actor*, int> indexOf;
    int n = 0;
    for (auto& ap : ctx_.world->getActors()) {
        if (ap.get() == editorCamActor_) continue;
        indexOf[ap.get()] = n++;
    }

    for (auto& ap : ctx_.world->getActors()) {
        Actor* a = ap.get();
        if (a == editorCamActor_) continue;

        int parentIdx = -1;
        if (Actor* p = a->getParent()) {
            auto it = indexOf.find(p);
            if (it != indexOf.end()) parentIdx = it->second;
        }

        oss << "ACTOR:" << a->name << "\n";
        SceneSerializer::writeActor(oss, a, parentIdx);
        oss << "END_ACTOR\n";
    }
    return oss.str();
}

void EditorApp::deserializeWorld(const std::string& data) {
    std::vector<Actor*> toDestroy;
    for (auto& ap : ctx_.world->getActors())
        if (ap.get() != editorCamActor_)
            toDestroy.push_back(ap.get());
    ctx_.selected = nullptr;
    for (Actor* a : toDestroy) ctx_.world->destroyActor(a);

    // 직렬화 순서대로 블록마다 정확히 한 슬롯을 만듭니다(스폰 실패 시 nullptr 자리표시자).
    // serializeWorld 가 액터마다 위치 인덱스를 매기므로, 이름 빈 블록 등으로 스폰이
    // 실패해도 슬롯을 예약해야 그 뒤 PARENT 인덱스가 밀리지 않습니다. 부모 배선은 전부
    // 스폰된 뒤에 하며, 파일에서 부모가 자식보다 뒤에 올 수도 있어 전방 참조를 허용합니다.
    std::vector<Actor*> spawned;   // 블록당 한 항목, 스폰 실패면 nullptr
    std::vector<int>    parentOf;
    bool                inBlock = false;
    SceneSerializer::ActorData cur;

    auto closeBlock = [&] {
        if (!inBlock) return;
        Actor* a = SceneSerializer::spawn(cur, *ctx_.world, *ctx_.assets);
        spawned.push_back(a);
        parentOf.push_back(a ? cur.parentIndex : -1);
        cur     = SceneSerializer::ActorData{};
        inBlock = false;
    };

    std::string line;
    std::istringstream iss(data);
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#') continue;
        // END_ACTOR 는 콜론이 없으므로 콜론 검사보다 먼저 처리해 블록을 닫습니다.
        if (line == "END_ACTOR") { closeBlock(); continue; }
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        if (key == "ACTOR") {
            closeBlock();          // END_ACTOR 없이 다음 블록이 와도 이전 블록을 닫음
            inBlock  = true;
            cur.name = val;
        } else {
            SceneSerializer::parseLine(key, val, cur);
        }
    }
    closeBlock();                  // END_ACTOR 없이 끝나는 마지막 블록

    // 부모 배선: PARENT 인덱스는 직렬화(블록) 순서이며 spawned 인덱스와 1:1 대응입니다.
    // attachTo 가 사이클을 거부하고 실효 활성을 다시 맞추므로, 비활성 부모 아래 자식은
    // 여기서 꺼지고, 순환 참조가 있어도 크래시하지 않습니다. null 슬롯은 건너뜁니다.
    for (size_t i = 0; i < spawned.size(); ++i) {
        int p = parentOf[i];
        if (p >= 0 && p < (int)spawned.size()
            && spawned[i] && spawned[p] && spawned[p] != spawned[i])
            spawned[i]->attachTo(spawned[p]);
    }

    // Sync actorCounter_ so "Add Empty" names don't collide with loaded actors
    actorCounter_ = 0;
    for (auto& ap : ctx_.world->getActors()) {
        const std::string& nm = ap->name;
        if (nm.size() > 6 && nm.substr(0, 6) == "Actor_") {
            try { actorCounter_ = std::max(actorCounter_, std::stoi(nm.substr(6)) + 1); }
            catch (...) {}
        }
    }
}

void EditorApp::saveScene() {
    std::ofstream f("scene.hscene");
    if (!f) { std::cerr << "[Editor] Save failed\n"; return; }
    f << serializeWorld();
    std::cout << "[Editor] Scene saved to scene.hscene\n";
}

void EditorApp::loadScene() {
    std::ifstream f("scene.hscene");
    if (!f) { std::cerr << "[Editor] Load failed: scene.hscene not found\n"; return; }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    deserializeWorld(data);
    cmdStack_.clear();
    std::cout << "[Editor] Scene loaded from scene.hscene\n";
}

// ── rebuildOutliner ────────────────────────────────────────────────────────────
void EditorApp::rebuildOutliner() {
    if (!outlinerList_ || !ctx_.world) return;
    outlinerList_->clearSlots();

    // + Add Empty Actor button
    outlinerList_->addSlot(
        HNew(SBorder)->background({0,0,0,0})->padding({s(6.f), s(4.f)})
            ->content(
                textButton("+ Add Empty", 13.f, kAccent, {6.f, 4.f},
                           {0.18f, 0.18f, 0.22f, 1.f}, {0.28f, 0.28f, 0.35f, 1.f},
                           [this]{
                               std::string nm = "Actor_" + std::to_string(actorCounter_++);
                               ctx_.selected = ctx_.world->spawnActor(nm);
                           })),
        SVerticalBox::ESizeRule::Fixed, s(34.f));

    // Case-insensitive search filter
    std::string filter = outlinerSearch_;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    for (auto& actorPtr : ctx_.world->getActors()) {
        Actor* actor = actorPtr.get();

        if (!filter.empty()) {
            std::string nameLow = actor->name;
            std::transform(nameLow.begin(), nameLow.end(), nameLow.begin(), ::tolower);
            if (nameLow.find(filter) == std::string::npos) continue;
        }
        bool   sel   = (actor == ctx_.selected);
        bool   ren   = sel && renaming_;

        bool renSelAll = ren && renameSelAll_;
        std::string displayName = ren ? (renSelAll ? renameBuffer_ : renameBuffer_ + "|")
                                      : actor->name;

        std::shared_ptr<HWidget> nameWidget =
            HNew(STextBlock)->text(displayName)->fontSize(s(14.f))
                ->color(sel ? glm::vec4{1.f,1.f,1.f,1.f} : kTextBody)
                ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);
        if (renSelAll)   // Ctrl+A 전체 선택 하이라이트
            nameWidget = HNew(SBorder)->background({0.20f, 0.45f, 0.85f, 1.f})
                             ->content(nameWidget);

        outlinerList_->addSlot(
            HNew(SHorizontalBox)
                ->addSlot(
                    HNew(SButton)
                        ->normalColor ({sel ? kSelected : glm::vec4{0.f,0.f,0.f,0.f}})
                        ->hoveredColor({sel ? kSelected : glm::vec4{0.25f,0.25f,0.30f,1.f}})
                        ->pressedColor({0.15f, 0.30f, 0.55f, 1.f})
                        ->padding({s(14.f), s(6.f)})
                        ->onClick([this, actor]{
                            renaming_ = false;
                            ctx_.selected = (ctx_.selected == actor) ? nullptr : actor;
                        })
                        ->content(nameWidget),
                    SHorizontalBox::ESizeRule::Fill)
                ->addSlot(
                    textButton("x", 12.f, {0.9f, 0.4f, 0.4f, 1.f}, {5.f, 3.f},
                               {0.f, 0.f, 0.f, 0.f}, {0.40f, 0.12f, 0.12f, 1.f},
                               [this, actor]{
                                   if (actor == editorCamActor_) return;
                                   if (ctx_.selected == actor) {
                                       ctx_.selected = nullptr;
                                       renaming_     = false;
                                   }
                                   // 이 액터를 참조하는 커맨드는 onActorDestroyed
                                   // 구독이 정밀 제거합니다(무관한 이력은 보존).
                                   ctx_.world->destroyActor(actor);
                               }),
                    SHorizontalBox::ESizeRule::Fixed, s(26.f)),
            SVerticalBox::ESizeRule::Fixed, s(30.f));
    }
}

// ── rebuildDetails ─────────────────────────────────────────────────────────────
void EditorApp::rebuildDetails() {
    if (!detailsList_) return;
    detailsList_->clearSlots();

    detailsList_->addSlot(
        HNew(SBorder)->background({0,0,0,0})->padding({s(8.f), s(8.f)})
            ->content(HNew(STextBlock)
                ->text(AttrStr([this]() -> std::string {
                    return ctx_.selected ? ctx_.selected->name : "(No Selection)";
                }))
                ->fontSize(s(16.f))->color(ctx_.selected ? kTextBody : kTextDim)
                ->hAlign(EHAlign::Left)),
        SVerticalBox::ESizeRule::Fixed, s(46.f));

    if (!ctx_.selected) return;

    detailsList_->addSlot(HNew(SBorder)->background(kBorder), SVerticalBox::ESizeRule::Fixed, 1.f);

    static constexpr glm::vec4 kRed  {0.9f, 0.3f, 0.3f, 1.f};
    static constexpr glm::vec4 kGreen{0.3f, 0.9f, 0.3f, 1.f};
    static constexpr glm::vec4 kBlue {0.3f, 0.6f, 1.0f, 1.f};

    Actor* sel = ctx_.selected;

    auto secHeader = [&](const std::string& t) {
        detailsList_->addSlot(sectionHeader(t, kTextHdr, 13.f),
                              SVerticalBox::ESizeRule::Fixed, s(26.f));
    };

    auto addNumRow = [&](const char* lbl, glm::vec4 lc,
                         std::function<float()>     getter,
                         std::function<void(float)> setter,
                         float step,
                         std::function<void()> bFn,
                         std::function<void()> eFn) {
        detailsList_->addSlot(
            HNew(SHorizontalBox)
                ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
                ->addSlot(
                    HNew(SNumberField)
                        ->label(lbl)->labelColor(lc)->fontSize(s(12.f))
                        ->get(std::move(getter))->set(std::move(setter))->step(step)
                        ->onDragBegin(std::move(bFn))->onDragEnd(std::move(eFn)),
                    SHorizontalBox::ESizeRule::Fill)
                ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f)),
            SVerticalBox::ESizeRule::Fixed, s(26.f));
    };

    // ── Transform (collapsible) ───────────────────────────────────────────────
    auto txBegin = [this, sel]{ undoSnapshot_ = sel->transform(); };
    auto txEnd   = [this, sel]{ cmdStack_.push(std::make_unique<SetTransformCmd>(sel, undoSnapshot_, sel->transform())); };

    auto txBody = HNew(SVerticalBox);
    auto addTxRow = [&](const char* lbl, glm::vec4 lc,
                        std::function<float()> getter, std::function<void(float)> setter, float step) {
        txBody->addSlot(
            HNew(SHorizontalBox)
                ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
                ->addSlot(HNew(SNumberField)
                    ->label(lbl)->labelColor(lc)->fontSize(s(12.f))
                    ->get(std::move(getter))->set(std::move(setter))->step(step)
                    ->onDragBegin(txBegin)->onDragEnd(txEnd),
                    SHorizontalBox::ESizeRule::Fill)
                ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f)),
            SVerticalBox::ESizeRule::Fixed, s(26.f));
    };

    txBody->addSlot(sectionHeader("LOCATION"), SVerticalBox::ESizeRule::Fixed, s(22.f));
    // 성분별 세터는 값을 읽어 한 성분만 바꾸고 세터로 되돌려 넣습니다(필드 직접 쓰기 불가).
    auto setPosX = [sel](float v){ glm::vec3 p = sel->transform().position(); p.x = v; sel->setLocalPosition(p); };
    auto setPosY = [sel](float v){ glm::vec3 p = sel->transform().position(); p.y = v; sel->setLocalPosition(p); };
    auto setPosZ = [sel](float v){ glm::vec3 p = sel->transform().position(); p.z = v; sel->setLocalPosition(p); };
    addTxRow("X", kRed,   [sel]{ return sel->transform().position().x; }, setPosX, 0.01f);
    addTxRow("Y", kGreen, [sel]{ return sel->transform().position().y; }, setPosY, 0.01f);
    addTxRow("Z", kBlue,  [sel]{ return sel->transform().position().z; }, setPosZ, 0.01f);
    txBody->addSlot(spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));

    txBody->addSlot(sectionHeader("ROTATION"), SVerticalBox::ESizeRule::Fixed, s(22.f));
    auto setEuler = [this,sel]{ sel->setLocalRotation(glm::quat(glm::radians(cachedEuler_))); };
    addTxRow("X", kRed,   [this]{ return cachedEuler_.x; }, [this,setEuler](float v){ cachedEuler_.x=v; setEuler(); }, 0.5f);
    addTxRow("Y", kGreen, [this]{ return cachedEuler_.y; }, [this,setEuler](float v){ cachedEuler_.y=v; setEuler(); }, 0.5f);
    addTxRow("Z", kBlue,  [this]{ return cachedEuler_.z; }, [this,setEuler](float v){ cachedEuler_.z=v; setEuler(); }, 0.5f);
    txBody->addSlot(spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));

    txBody->addSlot(sectionHeader("SCALE"), SVerticalBox::ESizeRule::Fixed, s(22.f));
    auto setSclX = [sel](float v){ glm::vec3 s2 = sel->transform().scale(); s2.x = v; sel->setLocalScale(s2); };
    auto setSclY = [sel](float v){ glm::vec3 s2 = sel->transform().scale(); s2.y = v; sel->setLocalScale(s2); };
    auto setSclZ = [sel](float v){ glm::vec3 s2 = sel->transform().scale(); s2.z = v; sel->setLocalScale(s2); };
    addTxRow("X", kRed,   [sel]{ return sel->transform().scale().x; }, setSclX, 0.01f);
    addTxRow("Y", kGreen, [sel]{ return sel->transform().scale().y; }, setSclY, 0.01f);
    addTxRow("Z", kBlue,  [sel]{ return sel->transform().scale().z; }, setSclZ, 0.01f);

    detailsList_->addSlot(
        HNew(SCollapseSection)->title("TRANSFORM")->body(txBody)->expanded(true)
            ->headerHeight(s(28.f))->fontSize(s(13.f)),
        SVerticalBox::ESizeRule::Auto);

    // ── Components ────────────────────────────────────────────────────────────
    auto addEnumRow = [&](const char* lbl,
                          const std::vector<const char*>& opts,
                          std::function<int()>    getI,
                          std::function<void(int)> setI) {
        int         idx = getI();
        const char* cur = (idx >= 0 && idx < (int)opts.size()) ? opts[idx] : "?";
        detailsList_->addSlot(
            HNew(SHorizontalBox)
                ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
                ->addSlot(
                    HNew(STextBlock)->text(lbl)->fontSize(s(12.f))->color(kTextBody)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                    SHorizontalBox::ESizeRule::Fill)
                ->addSlot(
                    textButton(cur, 12.f, kTextBody, {5.f, 2.f},
                               {0.22f, 0.22f, 0.28f, 1.f}, {0.35f, 0.35f, 0.40f, 1.f},
                               [this, opts, getI, setI]{
                                   setI((getI() + 1) % (int)opts.size());
                                   rebuildDetails();
                               }),
                    SHorizontalBox::ESizeRule::Fixed, s(88.f))
                ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f)),
            SVerticalBox::ESizeRule::Fixed, s(26.f));
    };

    auto addBoolRow = [&](const char* lbl,
                          std::function<bool()>    getB,
                          std::function<void(bool)> setB) {
        bool v = getB();
        detailsList_->addSlot(
            HNew(SHorizontalBox)
                ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
                ->addSlot(
                    HNew(STextBlock)->text(lbl)->fontSize(s(12.f))->color(kTextBody)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                    SHorizontalBox::ESizeRule::Fill)
                ->addSlot(
                    textButton(v ? "Yes" : "No", 12.f, kTextBody, {5.f, 2.f},
                               v ? glm::vec4{0.18f,0.50f,0.22f,1.f}
                                 : glm::vec4{0.38f,0.18f,0.18f,1.f},
                               {0.35f, 0.35f, 0.40f, 1.f},
                               [this, getB, setB]{ setB(!getB()); rebuildDetails(); }),
                    SHorizontalBox::ESizeRule::Fixed, s(44.f))
                ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f)),
            SVerticalBox::ESizeRule::Fixed, s(26.f));
    };

    for (auto& compPtr : sel->getComponents()) {
        Component* comp = compPtr.get();

        detailsList_->addSlot(spacer(), SVerticalBox::ESizeRule::Fixed, s(8.f));

        // Remove callback — type dispatch
        std::function<void()> removeFn;
        if (dynamic_cast<StaticMeshComponent*>(comp))
            removeFn = [this, sel]{ sel->removeComponent<StaticMeshComponent>(); rebuildDetails(); };
        else if (dynamic_cast<LightComponent*>(comp))
            removeFn = [this, sel]{ sel->removeComponent<LightComponent>(); rebuildDetails(); };
        else if (dynamic_cast<CameraComponent*>(comp))
            removeFn = [this, sel]{ sel->removeComponent<CameraComponent>(); rebuildDetails(); };
        else if (dynamic_cast<AudioComponent*>(comp))
            removeFn = [this, sel]{ sel->removeComponent<AudioComponent>(); rebuildDetails(); };
        else if (dynamic_cast<RigidBodyComponent*>(comp))
            removeFn = [this, sel]{ sel->removeComponent<RigidBodyComponent>(); rebuildDetails(); };

        // Component header row
        bool en = comp->isEnabled();
        detailsList_->addSlot(
            HNew(SBorder)->background(kBgHeader)->padding({s(4.f), s(3.f)})
                ->content(
                    HNew(SHorizontalBox)
                        ->addSlot(
                            HNew(STextBlock)->text(comp->typeName())->fontSize(s(12.f))
                                ->color(kTextHdr)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                            SHorizontalBox::ESizeRule::Fill)
                        ->addSlot(
                            textButton(en ? "On" : "Off", 12.f, kTextBody, {5.f, 2.f},
                                       en ? glm::vec4{0.18f,0.50f,0.22f,1.f}
                                          : glm::vec4{0.38f,0.18f,0.18f,1.f},
                                       {0.35f, 0.35f, 0.40f, 1.f},
                                       [this, comp]{ comp->setEnabled(!comp->isEnabled()); rebuildDetails(); }),
                            SHorizontalBox::ESizeRule::Fixed, s(42.f))
                        ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(4.f))
                        ->addSlot(
                            textButton("x", 12.f, {0.9f,0.4f,0.4f,1.f}, {5.f, 2.f},
                                       {0.28f, 0.12f, 0.12f, 1.f}, {0.44f, 0.16f, 0.16f, 1.f},
                                       removeFn ? removeFn : std::function<void()>([]{})),
                            SHorizontalBox::ESizeRule::Fixed, s(26.f))),
            SVerticalBox::ESizeRule::Fixed, s(28.f));

        // ── Generic property rows via buildProps() ────────────────────────────
        auto props      = std::make_shared<std::vector<PropDesc>>(comp->buildProps());
        auto snapBefore = std::make_shared<std::vector<float>>();

        auto compBegin = [props, snapBefore] {
            snapBefore->clear();
            for (auto& pd : *props)
                if (pd.getF) snapBefore->push_back(pd.getF());
        };
        auto compEnd = [this, sel, props, snapBefore] {
            std::vector<float> after;
            after.reserve(props->size());
            for (auto& pd : *props)
                if (pd.getF) after.push_back(pd.getF());
            // sel(소유 액터)을 함께 넘겨, 그 액터가 파괴될 때 purge 가 이 커맨드를
            // 걷어낼 수 있게 합니다. compEnd 는 패널이 살아있는 동안만 호출되므로 sel 은 유효합니다.
            cmdStack_.push(std::make_unique<PropsSnapshotCmd>(sel, *props, *snapBefore, std::move(after)));
        };

        for (auto& pd : *props) {
            if (pd.kind == PropDesc::Float)
                addNumRow(pd.label, pd.color,
                    [pd]{ return pd.getF(); }, [pd](float v){ pd.setF(v); },
                    pd.step, compBegin, compEnd);
            else if (pd.kind == PropDesc::Toggle)
                addBoolRow(pd.label, pd.getB, pd.setB);
            else if (pd.kind == PropDesc::Enum)
                addEnumRow(pd.label, pd.options, pd.getI, pd.setI);
        }

        std::string info = comp->readOnlyInfo();
        if (!info.empty())
            detailsList_->addSlot(
                HNew(SBorder)->background({0,0,0,0})->padding({s(16.f), s(1.f)})
                    ->content(HNew(STextBlock)->text(info)->fontSize(s(12.f))
                        ->color(kTextDim)->hAlign(EHAlign::Left)),
                SVerticalBox::ESizeRule::Fixed, s(18.f));
    }

    // ── Add Component ─────────────────────────────────────────────────────────
    detailsList_->addSlot(spacer(), SVerticalBox::ESizeRule::Fixed, s(8.f));
    secHeader("ADD COMPONENT");

    auto addRow = HNew(SHorizontalBox);
    addRow->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f));

    auto addCompBtn = [&](const std::string& label, std::function<void()> fn) {
        addRow->addSlot(
            textButton(label, 12.f, kAccent, {7.f, 4.f},
                       {0.14f, 0.14f, 0.18f, 1.f}, {0.26f, 0.26f, 0.32f, 1.f}, std::move(fn)),
            SHorizontalBox::ESizeRule::Auto);
        addRow->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(4.f));
    };

    if (!sel->getComponent<LightComponent>())
        addCompBtn("+ Light", [this, sel]{
            sel->addComponent<LightComponent>();
            rebuildDetails();
        });
    if (!sel->getComponent<CameraComponent>())
        addCompBtn("+ Camera", [this, sel]{
            sel->addComponent<CameraComponent>();
            rebuildDetails();
        });
    if (!sel->getComponent<StaticMeshComponent>())
        addCompBtn("+ Mesh", [this, sel]{
            sel->addComponent<StaticMeshComponent>();
            rebuildDetails();
        });
    if (!sel->getComponent<AudioComponent>())
        addCompBtn("+ Audio", [this, sel]{
            sel->addComponent<AudioComponent>();
            rebuildDetails();
        });
    if (!sel->getComponent<RigidBodyComponent>())
        addCompBtn("+ RigidBody", [this, sel]{
            sel->addComponent<RigidBodyComponent>();
            rebuildDetails();
        });

    detailsList_->addSlot(addRow, SVerticalBox::ESizeRule::Fixed, s(34.f));

    // ── Save as Prefab ────────────────────────────────────────────────────────
    if (sel) {
        detailsList_->addSlot(spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));
        detailsList_->addSlot(
            HNew(SBorder)->background({0,0,0,0})->padding({s(8.f), s(2.f)})
                ->content(
                    textButton("Save as Prefab", 12.f, kTextDim, {7.f, 4.f},
                               {0.14f, 0.14f, 0.18f, 1.f}, {0.26f, 0.26f, 0.32f, 1.f},
                               [this, sel]{
                                   std::string p = std::string(PrefabManager::kDir) + "/" + sel->name + PrefabManager::kExt;
                                   PrefabManager::save(sel, p);
                                   prefabFiles_ = PrefabManager::scan();
                                   rebuildBottomPanel();
                               })),
            SVerticalBox::ESizeRule::Fixed, s(32.f));
    }
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

    if (bottomTab_ == 0) {
        // Content Browser — Models
        if (!modelFiles_.empty()) {
            bottomContent_->addSlot(
                sectionHeader("MODELS", kTextDim),
                SVerticalBox::ESizeRule::Fixed, s(22.f));
            for (const auto& path : modelFiles_) {
                std::string display = fs::path(path).stem().string();
                bottomContent_->addSlot(
                    HNew(SButton)
                        ->normalColor ({0.f, 0.f, 0.f, 0.f})
                        ->hoveredColor({0.20f, 0.20f, 0.25f, 1.f})
                        ->pressedColor({0.07f, 0.33f, 0.58f, 1.f})
                        ->padding({s(8.f), s(3.f)})
                        ->onClick([this, path, display]{
                            try {
                                auto pairs = ctx_.assets->loadAll(path);
                                Actor* first = nullptr;
                                for (int i = 0; i < (int)pairs.size(); ++i) {
                                    std::string nm = display +
                                        (pairs.size() > 1 && i > 0 ? "_" + std::to_string(i) : "");
                                    Actor* a  = ctx_.world->spawnActor(nm);
                                    auto*  mc = a->addComponent<StaticMeshComponent>();
                                    mc->mesh        = pairs[i].mesh;
                                    mc->material    = pairs[i].material;
                                    mc->modelPath   = path;
                                    mc->meshIndex   = i;
                                    mc->transparent = pairs[i].isBlend;
                                    mc->vertCount   = pairs[i].vertCount;
                                    mc->triCount    = pairs[i].triCount;
                                    if (!first) first = a;
                                }
                                ctx_.selected = first;
                            } catch (const std::exception& ex) {
                                std::cerr << "[Editor] Spawn failed: " << ex.what() << "\n";
                            }
                        })
                        ->content(HNew(SHorizontalBox)
                            ->addSlot(HNew(STextBlock)->text("[M]")->fontSize(s(12.f))->color(kAccent)
                                ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fixed, s(30.f))
                            ->addSlot(HNew(STextBlock)->text(display)->fontSize(s(13.f))->color(kTextBody)
                                ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill)),
                    SVerticalBox::ESizeRule::Fixed, s(28.f));
            }
        }

        // Content Browser — Audio
        if (!audioFiles_.empty()) {
            bottomContent_->addSlot(spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));
            bottomContent_->addSlot(
                sectionHeader("AUDIO", kTextDim),
                SVerticalBox::ESizeRule::Fixed, s(22.f));
            for (const auto& stem : audioFiles_) {
                std::string fullPath = "assets/audio/" + stem;
                bottomContent_->addSlot(
                    HNew(SButton)
                        ->normalColor ({0.f, 0.f, 0.f, 0.f})
                        ->hoveredColor({0.20f, 0.20f, 0.25f, 1.f})
                        ->pressedColor({0.07f, 0.33f, 0.58f, 1.f})
                        ->padding({s(8.f), s(3.f)})
                        ->onClick([this, stem, fullPath]{
                            if (!ctx_.selected) return;
                            auto* ac = ctx_.selected->getComponent<AudioComponent>();
                            if (!ac) return;
                            if (ac->sound_ && ac->audioSys_) {
                                ac->audioSys_->destroySound(ac->sound_);
                                ac->sound_    = nullptr;
                                ac->audioSys_ = nullptr;
                            }
                            ac->filePath    = fullPath;
                            ac->loadFailed_ = false;
                            rebuildDetails();
                        })
                        ->content(HNew(SHorizontalBox)
                            ->addSlot(HNew(STextBlock)->text("[A]")->fontSize(s(12.f))
                                ->color({0.9f, 0.8f, 0.3f, 1.f})
                                ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fixed, s(30.f))
                            ->addSlot(HNew(STextBlock)->text(stem)->fontSize(s(13.f))->color(kTextBody)
                                ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill)),
                    SVerticalBox::ESizeRule::Fixed, s(28.f));
            }
        }

        // Content Browser — Prefabs
        if (!prefabFiles_.empty()) {
            bottomContent_->addSlot(spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));
            bottomContent_->addSlot(
                sectionHeader("PREFABS", kTextDim),
                SVerticalBox::ESizeRule::Fixed, s(22.f));
            for (const auto& stem : prefabFiles_) {
                std::string path = std::string(PrefabManager::kDir) + "/" + stem + PrefabManager::kExt;
                bottomContent_->addSlot(
                    HNew(SButton)
                        ->normalColor ({0.f, 0.f, 0.f, 0.f})
                        ->hoveredColor({0.20f, 0.20f, 0.25f, 1.f})
                        ->pressedColor({0.07f, 0.33f, 0.58f, 1.f})
                        ->padding({s(8.f), s(3.f)})
                        ->onClick([this, path]{
                            if (!ctx_.world || !ctx_.assets) return;
                            Actor* a = PrefabManager::spawn(path, *ctx_.world, *ctx_.assets);
                            if (a) ctx_.selected = a;
                        })
                        ->content(HNew(SHorizontalBox)
                            ->addSlot(HNew(STextBlock)->text("[P]")->fontSize(s(12.f))->color({0.7f, 0.9f, 0.5f, 1.f})
                                ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fixed, s(30.f))
                            ->addSlot(HNew(STextBlock)->text(stem)->fontSize(s(13.f))->color(kTextBody)
                                ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill)),
                    SVerticalBox::ESizeRule::Fixed, s(28.f));
            }
        }

        if (modelFiles_.empty() && audioFiles_.empty() && prefabFiles_.empty()) {
            bottomContent_->addSlot(
                HNew(SBorder)->background({0,0,0,0})->padding({s(12.f), s(8.f)})
                    ->content(HNew(STextBlock)->text("No assets found")
                        ->fontSize(s(12.f))->color(kTextDim)->hAlign(EHAlign::Left)),
                SVerticalBox::ESizeRule::Fixed, s(30.f));
        }
    } else if (bottomTab_ == 1) {
        // Output Log — 워처 스레드가 동시에 push할 수 있으므로 스냅샷으로 순회
        std::vector<std::string> logSnap = ctx_.snapshotLog();
        const int kMaxDisplay = std::max(6, (int)((bottomH_ - 30.f) / 16.f) - 1);
        int start = std::max(0, (int)logSnap.size() - kMaxDisplay);
        for (int i = start; i < (int)logSnap.size(); ++i) {
            const std::string& line = logSnap[i];
            glm::vec4 col = kTextDim;
            if (line.find("ERROR") != std::string::npos || line.find("FATAL") != std::string::npos)
                col = {0.9f, 0.3f, 0.3f, 1.f};
            else if (line.find("WARN") != std::string::npos)
                col = {0.9f, 0.8f, 0.2f, 1.f};
            bottomContent_->addSlot(
                HNew(SBorder)->background({0,0,0,0})->padding({s(4.f), 0.f})
                    ->content(HNew(STextBlock)->text(line)->fontSize(s(12.f))->color(col)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                SVerticalBox::ESizeRule::Fixed, s(16.f));
        }
    } else {
        // GPU Stats
        auto addStat = [&](const char* label, AttrStr val) {
            bottomContent_->addSlot(
                HNew(SHorizontalBox)
                    ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
                    ->addSlot(HNew(STextBlock)->text(label)->fontSize(s(12.f))->color(kTextDim)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fixed, s(140.f))
                    ->addSlot(HNew(STextBlock)->text(std::move(val))->fontSize(s(12.f))->color(kTextBody)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill),
                SVerticalBox::ESizeRule::Fixed, s(22.f));
        };

        addStat("FPS",        AttrStr([this]{ return std::to_string(vpTimings_.fps); }));
        addStat("Draw Calls", AttrStr([this]{ return std::to_string(vpTimings_.drawCalls); }));
        addStat("Culled",     AttrStr([this]{ return std::to_string(vpTimings_.culled); }));
        addStat("Transparent",AttrStr([this]{ return std::to_string(vpTimings_.transparentDraws); }));
        addStat("Shadow",     AttrStr([this]{ char b[32]; snprintf(b,32,"%.3f ms",vpTimings_.shadowMs); return std::string(b); }));
        addStat("Main Pass",  AttrStr([this]{ char b[32]; snprintf(b,32,"%.3f ms",vpTimings_.mainMs);   return std::string(b); }));
        addStat("AO",         AttrStr([this]{ char b[32]; snprintf(b,32,"%.3f ms",vpTimings_.aoMs);      return std::string(b); }));
        addStat("Bloom",      AttrStr([this]{ char b[32]; snprintf(b,32,"%.3f ms",vpTimings_.bloomMs);   return std::string(b); }));
        addStat("TAA blit",   AttrStr([this]{ char b[32]; snprintf(b,32,"%.3f ms",vpTimings_.taaBlitMs); return std::string(b); }));
        addStat("Tonemap",    AttrStr([this]{ char b[32]; snprintf(b,32,"%.3f ms",vpTimings_.tonemapMs);return std::string(b); }));
        addStat("UI Pass",    AttrStr([this]{ char b[32]; snprintf(b,32,"%.3f ms",vpTimings_.uiMs);     return std::string(b); }));
        addStat("CPU Frame",  AttrStr([]{ char b[32]; snprintf(b,32,"%.3f ms",gFrameTimings().loopTotal.v); return std::string(b); }));
        addStat("Recreate/s", AttrStr([this]{ char b[32]; snprintf(b,32,"%.0f",vpTimings_.recreatePerSec); return std::string(b); }));
        addStat("Total GPU",  AttrStr([this]{ char b[32]; snprintf(b,32,"%.3f ms",vpTimings_.totalMs);  return std::string(b); }));
        addStat("MSAA",       AttrStr([this]{ return std::string(ctx_.renderer ? "4x" : "N/A"); }));
    }
}

// ── syncEulerCache / undo / redo ───────────────────────────────────────────────
void EditorApp::syncEulerCache() {
    if (ctx_.selected)
        cachedEuler_ = quatToEulerDeg(ctx_.selected->transform().rotation());
    else
        cachedEuler_ = {};
}

void EditorApp::undo() {
    if (!cmdStack_.canUndo()) return;
    cmdStack_.undo();
    syncEulerCache();
}

void EditorApp::redo() {
    if (!cmdStack_.canRedo()) return;
    cmdStack_.redo();
    syncEulerCache();
}

// ── tickShortcuts ──────────────────────────────────────────────────────────────
// 표준 단축키: Ctrl+Z(undo) / Ctrl+Y·Ctrl+Shift+Z(redo) / Ctrl+S(씬 저장)
void EditorApp::tickShortcuts() {
    if (!ctx_.window) return;
    IWindow& win = *ctx_.window;

    bool zNow = win.isKeyDown(HInput::KEY_Z);
    bool yNow = win.isKeyDown(HInput::KEY_Y);
    bool sNow = win.isKeyDown(HInput::KEY_S);
    bool fNow = win.isKeyDown(HInput::KEY_F);
    bool zEdge = zNow && !prevZDown_;
    bool yEdge = yNow && !prevYDown_;
    bool sEdge = sNow && !prevSDown_;
    bool fEdge = fNow && !prevFDown_;
    prevZDown_ = zNow;
    prevYDown_ = yNow;
    prevSDown_ = sNow;
    prevFDown_ = fNow;

    if (win.isCameraModeActive()) return;                 // WASD 비행 중 충돌 방지
    if (!win.isKeyDown(HInput::KEY_LEFT_CTRL)) return;

    if (sEdge) { saveScene(); return; }

    // Ctrl+F → 아웃라이너 검색창 포커스 (기존 검색어는 전체 선택 → 바로 교체 가능)
    if (fEdge) {
        if (auto* nf = SNumberField::focusedField()) nf->commitEdit();
        renaming_              = false;
        renameSelAll_          = false;
        outlinerSearchFocused_ = true;
        searchSelAll_          = !outlinerSearch_.empty();
        rebuildOutliner();
        return;
    }

    // 텍스트 입력 중에는 씬 undo/redo가 직관에 어긋나므로 차단
    if (renaming_ || outlinerSearchFocused_ || SNumberField::focusedField()) return;

    bool shift = win.isKeyDown(HInput::KEY_LEFT_SHIFT);
    if (zEdge && !shift)                 undo();
    else if (yEdge || (zEdge && shift))  redo();
}

// ── handleInput ────────────────────────────────────────────────────────────────
void EditorApp::handleInput(float mouseX, float mouseY,
                             bool leftDown, bool leftJustUp,
                             float scrollDelta, float winW, float winH) {
    // ── Dock splitter resize ────────────────────────────────────────────────────
    // 도크 트리의 스플리터 핸들을 히트테스트해 분할 비율(ratios)을 조정합니다. 드래그 상태는
    // 위젯이 아니라 노드 기준이라, rebuildDockArea 로 위젯이 갈려도 안정적으로 이어집니다.
    if (ctx_.window && !ctx_.window->isCameraModeActive()) {
        int cursorShape = 0;
        if (!dockDragNode_) {
            const float grab = s(4.f);   // 시각 핸들(5px)보다 넓은 잡기 존(좌표 오차·얇은 폭 보정)
            for (auto& h : dockSplitters_) {
                if (!h.handle) continue;
                const HGeometry& hg = h.handle->cachedGeometry();
                if (hg.size.x < 1.f && hg.size.y < 1.f) continue;
                const bool hit = mouseX >= hg.position.x - grab && mouseX < hg.position.x + hg.size.x + grab &&
                                 mouseY >= hg.position.y - grab && mouseY < hg.position.y + hg.size.y + grab;
                if (!hit) continue;
                cursorShape = h.vertical ? 2 : 1;   // 2 = 상하(↕), 1 = 좌우(↔)
                if (leftDown && !prevLeftDown_ && h.node) {
                    dockDragNode_        = h.node;
                    dockDragIndex_       = h.index;
                    dockDragVertical_    = h.vertical;
                    dockDragStartMouse_  = h.vertical ? mouseY : mouseX;
                    dockDragStartRatioL_ = (h.index < (int)h.node->ratios.size())
                                         ? h.node->ratios[h.index] : 0.5f;
                    dockDragAxisPx_ = 1.f;
                    if (h.container) {
                        const HGeometry& cg = h.container->cachedGeometry();
                        dockDragAxisPx_ = std::max(1.f, h.vertical ? cg.size.y : cg.size.x);
                    }
                }
                break;
            }
        } else {
            cursorShape = dockDragVertical_ ? 2 : 1;
        }
        ctx_.window->setCursorShape(cursorShape);

        if (dockDragNode_ && leftDown) {
            const float cur    = dockDragVertical_ ? mouseY : mouseX;
            const float dRatio = (cur - dockDragStartMouse_) / dockDragAxisPx_;
            auto&     R = dockDragNode_->ratios;
            const int i = dockDragIndex_;
            if (i >= 0 && i + 1 < (int)R.size()) {
                const float sum  = R[i] + R[i + 1];
                const float minR = 0.06f;
                const float newL = std::clamp(dockDragStartRatioL_ + dRatio, minR, sum - minR);
                if (newL != R[i]) { R[i] = newL; R[i + 1] = sum - newL; rebuildDockArea(); }
            }
            prevLeftDown_ = leftDown;
            return;
        }
        if (!leftDown && dockDragNode_) dockDragNode_ = nullptr;
    }

    // ── Dock tab drag (re-dock) ──────────────────────────────────────────────────
    // 탭을 프레스→드래그하면 재도킹, 프레스→릴리즈(이동 없음)면 활성 탭 전환입니다. 스플리터
    // 드래그 중이면 건너뜁니다. 프레스~릴리즈 동안 이벤트를 소비해 픽·기즈모로 새지 않게 합니다.
    if (ctx_.window && !ctx_.window->isCameraModeActive() && !dockDragNode_) {
        const glm::vec2 m{ mouseX, mouseY };

        if (leftDown && !prevLeftDown_ && !tabPressPending_) {
            for (auto& t : dockTabHits_) {
                if (t.widget && t.widget->cachedGeometry().contains(m)) {
                    tabPressPending_ = true; tabDragActive_ = false;
                    tabSrcStack_ = t.stack; tabSrcIndex_ = t.tabIndex;
                    tabSrcPanel_ = t.panelId; tabPressPos_ = m;
                    break;
                }
            }
        }

        if (tabPressPending_) {
            if (leftDown) {
                if (!tabDragActive_ && glm::length(m - tabPressPos_) > 6.f)
                    tabDragActive_ = true;
                if (tabDragActive_) {
                    // 타깃 리프 + 존(L/R/T/B/C) 판정 → 드롭존 하이라이트
                    tabTargetLeaf_ = nullptr; tabTargetZone_ = EDockZone::None;
                    if (dockOverlay_) dockOverlay_->active = false;
                    for (auto& lf : dockLeafHits_) {
                        if (!lf.widget) continue;
                        const HGeometry& g = lf.widget->cachedGeometry();
                        if (!g.contains(m)) continue;
                        tabTargetLeaf_ = lf.leaf;
                        const float rx = (m.x - g.position.x) / std::max(1.f, g.size.x);
                        const float ry = (m.y - g.position.y) / std::max(1.f, g.size.y);
                        const float edge = 0.28f;
                        HGeometry hi = g;
                        if      (rx < edge)       { tabTargetZone_ = EDockZone::Left;   hi.size.x *= 0.5f; }
                        else if (rx > 1.f - edge) { tabTargetZone_ = EDockZone::Right;  hi.position.x += g.size.x * 0.5f; hi.size.x *= 0.5f; }
                        else if (ry < edge)       { tabTargetZone_ = EDockZone::Top;    hi.size.y *= 0.5f; }
                        else if (ry > 1.f - edge) { tabTargetZone_ = EDockZone::Bottom; hi.position.y += g.size.y * 0.5f; hi.size.y *= 0.5f; }
                        else                      { tabTargetZone_ = EDockZone::Center; }
                        if (dockOverlay_) { dockOverlay_->active = true; dockOverlay_->rect = hi; }
                        break;
                    }
                    ctx_.window->setCursorShape(0);
                }
                prevLeftDown_ = leftDown;
                return;   // 프레스~드래그 동안 소비
            }
            // 릴리즈
            if (dockOverlay_) dockOverlay_->active = false;
            if (tabDragActive_ && tabTargetLeaf_ && tabTargetZone_ != EDockZone::None) {
                performRedock(tabSrcStack_, tabSrcIndex_, tabSrcPanel_, tabTargetLeaf_, tabTargetZone_);
            } else if (!tabDragActive_ && tabSrcStack_ &&
                       tabSrcIndex_ >= 0 && tabSrcIndex_ < (int)tabSrcStack_->panelIds.size()) {
                tabSrcStack_->activeTab = tabSrcIndex_;   // 클릭 = 활성 탭 전환
                rebuildDockArea();
            }
            tabPressPending_ = tabDragActive_ = false;
            tabSrcStack_ = tabTargetLeaf_ = nullptr;
            tabTargetZone_ = EDockZone::None;
            prevLeftDown_ = leftDown;
            return;
        }
    }

    HPointerEvent e;
    e.position = { mouseX, mouseY };

    if (scrollDelta != 0.f) {
        e.wheelDelta = scrollDelta;
        slateRenderer_.handlePointerEvent(e);
        e.wheelDelta = 0.f;
    }

    // ── Gizmo drag ─────────────────────────────────────────────────────────────
    bool gizmoConsumed = false;
    if (ctx_.selected && ctx_.selected != editorCamActor_ && ctx_.world && ctx_.window) {
        // 좌표는 worldVP 하나로만 만듭니다. inView 게이팅도 fb 공간으로 통일합니다.
        const bool inView = inViewport(mouseX, mouseY, winW, winH);
        gizmoConsumed = gizmo_.handleDrag(ctx_.selected, *ctx_.world, cmdStack_,
                                          worldVP(mouseX, mouseY, winW, winH),
                                          leftDown, prevLeftDown_,
                                          gizmoMode_, worldSpace_, inView);
    }

    // ── UI event + actor pick ─────────────────────────────────────────────────
    if (!gizmoConsumed && leftDown && !prevLeftDown_) {
        // 다른 곳 클릭 → 편집 중인 숫자 필드 확정
        if (auto* nf = SNumberField::focusedField()) nf->commitEdit();
        e.button = 0; e.down = true;
        HReply r = slateRenderer_.handlePointerEvent(e);
        if (!r.handled && inViewport(mouseX, mouseY, winW, winH)) {
            pickActor(mouseX, mouseY, winW, winH);
            lmbOrbit_ = true;
        }
    }
    if (!leftDown) lmbOrbit_ = false;
    if (leftJustUp && !gizmoConsumed) {
        e.button = 0; e.down = false;
        slateRenderer_.handlePointerEvent(e);
    }
    e.button = -1;
    slateRenderer_.handlePointerEvent(e);

    prevLeftDown_ = leftDown;
}

// ── inViewport ─────────────────────────────────────────────────────────────────
void EditorApp::buildEditorIcons(World& world, const WorldViewport& /*wvp*/,
                                 std::vector<LineVertex>& lines,
                                 std::vector<LineVertex>& tris) {
    CameraComponent* cam = world.getActiveCamera();
    if (!cam || !cam->owner) return;
    const glm::vec3 camPos  = cam->owner->worldPosition();
    const glm::mat4 invView = glm::inverse(cam->viewMatrix());
    const glm::vec3 right = glm::normalize(glm::vec3(invView[0]));
    const glm::vec3 up    = glm::normalize(glm::vec3(invView[1]));

    for (auto& ap : world.getActors()) {
        Actor* a = ap.get();
        if (a == editorCamActor_) continue;
        glm::vec4 color{0.f}; bool has = false;
        for (const auto& comp : a->getComponents())
            if (comp->editorIcon(color)) { has = true; break; }
        if (!has) continue;

        const glm::vec3 pos  = a->worldPosition();
        const float     dist = glm::length(camPos - pos);
        const float     ssz  = std::max(0.02f, dist * 0.03f);   // 화면 고정 크기
        const bool      sel  = (ctx_.selected == a);
        const glm::vec4 col  = sel ? glm::vec4(1.f, 1.f, 1.f, 1.f) : color;

        // 카메라를 향하는 사각형(빌보드)
        const glm::vec3 r = right * ssz, u = up * ssz;
        const glm::vec3 p0 = pos - r - u, p1 = pos + r - u, p2 = pos + r + u, p3 = pos - r + u;
        tris.push_back({p0,col}); tris.push_back({p1,col}); tris.push_back({p2,col});
        tris.push_back({p0,col}); tris.push_back({p2,col}); tris.push_back({p3,col});

        if (sel) {   // 선택 시 테두리
            const glm::vec4 oc{1.f, 0.85f, 0.15f, 1.f};
            lines.push_back({p0,oc}); lines.push_back({p1,oc});
            lines.push_back({p1,oc}); lines.push_back({p2,oc});
            lines.push_back({p2,oc}); lines.push_back({p3,oc});
            lines.push_back({p3,oc}); lines.push_back({p0,oc});
        }
    }
}

HViewportRect EditorApp::fullFbRect() const {
    int fw = 1, fh = 1;
    if (ctx_.window) ctx_.window->getFramebufferSize(fw, fh);
    return { 0.f, 0.f, (float)fw, (float)fh };
}

// 씬 렌더 대상(renderExtent + viewportOffset)과 기즈모·픽 좌표를 동시에 구동하는 단일 rect.
// **좌표 진리원을 고정 패널 치수가 아니라 뷰포트 위젯의 실측 arranged geometry 에서 유도합니다.**
// HSlate 는 스왑체인 extent(=프레임버퍼 픽셀)로 루트를 배치하므로(HSlateRenderer::record),
// viewportWidget_ 의 cachedGeometry 가 이미 프레임버퍼 픽셀입니다. 덕분에 뷰포트를 어디에
// 도킹·리사이즈해도 이 rect 가 자동으로 따라오고, 기즈모·픽·렌더가 함께 정확해집니다.
// 정수로 내림해 렌더러가 쓰는 int 값과 정확히 일치시킵니다.
HViewportRect EditorApp::viewportFbRect() const {
    if (viewportWidget_) {
        const HGeometry& g = viewportWidget_->cachedGeometry();
        if (g.size.x >= 1.f && g.size.y >= 1.f)
            return { std::floor(g.position.x), std::floor(g.position.y),
                     std::floor(g.size.x),     std::floor(g.size.y) };
    }
    // Fallback (첫 배치 전 프레임): 고정 치수 + 윈도→프레임버퍼 배율. 위젯 geometry 가
    // 아직 없을 때만 쓰이고, 다음 프레임부터는 위 실측 경로가 인계합니다.
    int wW = 1, wH = 1, fw = 1, fh = 1;
    if (ctx_.window) { ctx_.window->getWindowSize(wW, wH); ctx_.window->getFramebufferSize(fw, fh); }
    const float sx = (float)fw / std::max(1.f, (float)wW);
    const float sy = (float)fh / std::max(1.f, (float)wH);
    const float l = s(outlinerW_), t = s(kToolbarH);
    const float w = (float)wW - s(outlinerW_) - s(detailsW_);
    const float h = (float)wH - s(kToolbarH)  - s(bottomH_);
    return { std::floor(l * sx), std::floor(t * sy), std::floor(w * sx), std::floor(h * sy) };
}

glm::vec2 EditorApp::mouseToFb(float mx, float my, float winW, float winH) const {
    int fw = 1, fh = 1;
    if (ctx_.window) ctx_.window->getFramebufferSize(fw, fh);
    return { mx * (float)fw / std::max(1.f, winW),
             my * (float)fh / std::max(1.f, winH) };
}

WorldViewport EditorApp::worldVP(float mx, float my, float winW, float winH) const {
    // rect = 뷰포트 fb 서브렉트(오프셋 포함). WorldViewport::project/ray 가 rect.left/top 을
    // 오프셋으로 쓰므로, 씬이 스왑체인의 그 오프셋에 합성되는 것과 좌표가 정확히 일치합니다.
    return { viewportFbRect(), mouseToFb(mx, my, winW, winH) };
}

bool EditorApp::inViewport(float mx, float my, float winW, float winH) const {
    // 히트테스트도 프레임버퍼 공간으로 통일합니다. 마우스(윈도 픽셀)를 fb 로 변환해
    // 뷰포트 위젯의 실측 fb rect 와 비교하므로, 도킹·리사이즈·HiDPI 와 무관하게 정확합니다.
    const glm::vec2 fb = mouseToFb(mx, my, winW, winH);
    return viewportFbRect().contains(fb.x, fb.y);
}

// ── deleteSelectedActor ────────────────────────────────────────────────────────
void EditorApp::deleteSelectedActor() {
    if (!ctx_.selected || ctx_.selected == editorCamActor_) return;
    // 이 액터를 참조하는 커맨드는 onActorDestroyed 구독이 정밀 제거합니다.
    ctx_.world->destroyActor(ctx_.selected);
    ctx_.selected = nullptr;
    renaming_     = false;
    gizmo_.cancel();
}

// ── tickRenameInput ────────────────────────────────────────────────────────────
void EditorApp::tickRenameInput() {
    if (!ctx_.window) return;
    IWindow& win = *ctx_.window;

    // ── Ctrl+C/V/A 클립보드·전체선택 엣지 (모든 텍스트 입력 공용) ──────────────
    bool ctrlHeld = win.isKeyDown(HInput::KEY_LEFT_CTRL);
    bool vNow = win.isKeyDown(HInput::KEY_V);
    bool cNow = win.isKeyDown(HInput::KEY_C);
    bool aNow = win.isKeyDown(HInput::KEY_A);
    bool pasteEdge  = ctrlHeld && vNow && !prevVDown_;
    bool copyEdge   = ctrlHeld && cNow && !prevCDown_;
    bool selAllEdge = ctrlHeld && aNow && !prevADown_;
    prevVDown_ = vNow;
    prevCDown_ = cNow;
    prevADown_ = aNow;

    // ── 숫자 필드 인라인 편집 — 입력 모드 중에는 모든 키가 필드로만 간다 ──────
    if (auto* nf = SNumberField::focusedField()) {
        std::vector<unsigned int> chars;
        win.drainCharQueue(chars);
        for (unsigned int cp : chars) nf->onChar(cp);

        if (selAllEdge)
            nf->selectAll();
        if (pasteEdge)
            for (char c : win.getClipboardText()) nf->onChar((unsigned char)c);
        if (copyEdge)
            win.setClipboardText(nf->editBuffer());

        bool bsNow = win.isKeyDown(HInput::KEY_BACKSPACE);
        if (bsNow && !prevBackspaceDown_) nf->onBackspace();
        prevBackspaceDown_ = bsNow;

        bool enNow = win.isKeyDown(HInput::KEY_ENTER);
        if (enNow && !prevEnterDown_) nf->commitEdit();
        prevEnterDown_ = enNow;
        return;
    }

    bool f2Now  = win.isKeyDown(HInput::KEY_F2);
    bool delNow = win.isKeyDown(HInput::KEY_DELETE);
    bool f2Edge = f2Now  && !prevF2Down_;
    bool delEdge= delNow && !prevDeleteDown_;
    prevF2Down_     = f2Now;
    prevDeleteDown_ = delNow;

    if (f2Edge) {
        if (!renaming_ && ctx_.selected && ctx_.selected != editorCamActor_) {
            renaming_           = true;
            renameBuffer_       = ctx_.selected->name;
            renameSelAll_       = true;   // 윈도우 표준: 이름 변경 시작 시 전체 선택
            prevBackspaceDown_  = win.isKeyDown(HInput::KEY_BACKSPACE);
            prevEnterDown_      = win.isKeyDown(HInput::KEY_ENTER);
            rebuildOutliner();
        } else if (renaming_) {
            renaming_     = false;
            renameSelAll_ = false;
            rebuildOutliner();
        }
        return;
    }

    if (renaming_) {
        bool dirty = false;

        // 전체 선택 상태에서 입력/붙여넣기/지우기 → 기존 내용 교체
        auto clearIfSelAll = [&]{
            if (renameSelAll_) { renameBuffer_.clear(); renameSelAll_ = false; dirty = true; }
        };

        if (selAllEdge && !renameBuffer_.empty()) { renameSelAll_ = true; dirty = true; }

        std::vector<unsigned int> chars;
        win.drainCharQueue(chars);
        for (unsigned int cp : chars) {
            if (cp >= 32 && cp < 127) {
                clearIfSelAll();
                renameBuffer_ += (char)cp;
                dirty = true;
            }
        }

        if (pasteEdge) {
            clearIfSelAll();
            for (char c : win.getClipboardText())
                if (c >= 32 && c < 127) { renameBuffer_ += c; dirty = true; }
        }
        if (copyEdge) win.setClipboardText(renameBuffer_);

        bool bsNow = win.isKeyDown(HInput::KEY_BACKSPACE);
        if (bsNow && !prevBackspaceDown_ && !renameBuffer_.empty()) {
            if (renameSelAll_) clearIfSelAll();
            else               renameBuffer_.pop_back();
            dirty = true;
        }
        prevBackspaceDown_ = bsNow;

        bool enNow = win.isKeyDown(HInput::KEY_ENTER);
        if (enNow && !prevEnterDown_ && !renameBuffer_.empty()) {
            ctx_.selected->name = renameBuffer_;
            renaming_           = false;
            renameSelAll_       = false;
            dirty               = true;
        }
        prevEnterDown_ = enNow;

        if (dirty) rebuildOutliner();
    } else if (outlinerSearchFocused_) {
        bool dirty = false;

        auto clearIfSelAll = [&]{
            if (searchSelAll_) { outlinerSearch_.clear(); searchSelAll_ = false; dirty = true; }
        };

        if (selAllEdge && !outlinerSearch_.empty()) { searchSelAll_ = true; dirty = true; }

        std::vector<unsigned int> chars;
        win.drainCharQueue(chars);
        for (unsigned int cp : chars) {
            if (cp >= 32 && cp < 127) {
                clearIfSelAll();
                outlinerSearch_ += (char)cp;
                dirty = true;
            }
        }

        if (pasteEdge) {
            clearIfSelAll();
            for (char c : win.getClipboardText())
                if (c >= 32 && c < 127) { outlinerSearch_ += c; dirty = true; }
        }
        if (copyEdge) win.setClipboardText(outlinerSearch_);

        bool bsNow = win.isKeyDown(HInput::KEY_BACKSPACE);
        if (bsNow && !prevBackspaceDown_ && !outlinerSearch_.empty()) {
            if (searchSelAll_) clearIfSelAll();
            else               outlinerSearch_.pop_back();
            dirty = true;
        }
        prevBackspaceDown_ = bsNow;

        bool enNow = win.isKeyDown(HInput::KEY_ENTER);
        if (enNow && !prevEnterDown_) {
            outlinerSearchFocused_ = false;
            searchSelAll_          = false;
            dirty = true;
        }
        prevEnterDown_ = enNow;

        if (dirty) rebuildOutliner();
    } else {
        if (delEdge && ctx_.selected && ctx_.selected != editorCamActor_)
            deleteSelectedActor();
    }
}

// ── tickEditorCamera ───────────────────────────────────────────────────────────
void EditorApp::tickEditorCamera(float dt) {
    if (!editorCam_ || !editorCamActor_ || !ctx_.window) return;
    IWindow& win = *ctx_.window;

    const glm::vec3 fwd {
         cosf(editorCam_->pitch) * sinf(editorCam_->yaw),
        -sinf(editorCam_->pitch),
        -cosf(editorCam_->pitch) * cosf(editorCam_->yaw)
    };
    const glm::vec3 rgt = glm::normalize(glm::cross(fwd, glm::vec3(0.f, 1.f, 0.f)));

    // WASD fly — only when camera mode active (RMB held in viewport)
    if (win.isCameraModeActive()) {
        const float speed = 5.f * dt;
        if (win.isKeyDown(HInput::KEY_W)) editorCamActor_->translateLocal(fwd * speed);
        if (win.isKeyDown(HInput::KEY_S)) editorCamActor_->translateLocal(-fwd * speed);
        if (win.isKeyDown(HInput::KEY_D)) editorCamActor_->translateLocal(rgt * speed);
        if (win.isKeyDown(HInput::KEY_A)) editorCamActor_->translateLocal(-rgt * speed);
        if (win.isKeyDown(HInput::KEY_E)) editorCamActor_->translateLocal(glm::vec3(0,1,0) * speed);
        if (win.isKeyDown(HInput::KEY_Q)) editorCamActor_->translateLocal(-glm::vec3(0,1,0) * speed);
    }

    // Mouse look (RMB drag)
    float rx, ry;
    win.getRightMouseDelta(rx, ry);
    if (rx != 0 || ry != 0) {
        editorCam_->yaw   += rx * 0.003f;
        editorCam_->pitch  = std::clamp(editorCam_->pitch + ry * 0.003f, -1.5f, 1.5f);
    }

    // LMB orbit pan
    if (lmbOrbit_) {
        float lx, ly;
        win.getMouseDelta(lx, ly);
        if (lx != 0 || ly != 0) {
            glm::vec3 up = glm::cross(rgt, fwd);
            editorCamActor_->translateLocal(-rgt * (lx * 0.005f));
            editorCamActor_->translateLocal(up * (ly * 0.005f));
        }
    }

    // Scroll dolly
    float scroll = win.getScrollDelta();
    if (scroll != 0.f)
        editorCamActor_->translateLocal(fwd * (scroll * 0.3f));
}

// ── pickActor ──────────────────────────────────────────────────────────────────
void EditorApp::pickActor(float mx, float my, float winW, float winH) {
    if (!ctx_.world) return;
    CameraComponent* cam = ctx_.world->getActiveCamera();
    if (!cam || !cam->owner) return;

    // 씬은 뷰포트 서브렉트에 그려지므로(Viewport_Render_Design.md), 광선도 fb.left/top
    // 오프셋을 빼고 만들어야 화면 매핑이 렌더와 일치합니다. WorldViewport::ray 공용 구현을 씁니다.
    // 좌표는 기즈모와 같은 worldVP 생산자를 거칩니다 (뷰포트 rect 혼용 방지).
    const WorldViewport wvp = worldVP(mx, my, winW, winH);
    const HViewportRect& fb = wvp.fbRect;
    if (fb.width <= 0.f || fb.height <= 0.f) return;

    // 광선·투영은 WorldViewport 공용 구현을 씁니다(오프셋 처리 단일화, 기즈모와 동일).
    const glm::mat4 vp = cam->projMatrix(wvp.aspect()) * cam->viewMatrix();
    glm::vec3 rayO, rayD;
    if (!wvp.ray(vp, rayO, rayD)) return;

    // ── 아이콘 픽 우선 ────────────────────────────────────────────────────────
    // 라이트·카메라·오디오는 작고 명시적인 아이콘 타깃이라, 뒤의 솔리드 바운드보다
    // 먼저 화면 원판으로 판정합니다. 좌표는 wvp(프레임버퍼 기준)를 그대로 씁니다.
    {
        // 아이콘 화면 위치도 같은 공용 투영으로(위 vp 재사용). 오프셋 처리 일원화.
        auto toFb = [&](const glm::vec3& wp) { return wvp.project(vp, wp); };
        Actor* iconBest = nullptr;
        float  iconBestD = s(18.f);   // fb px
        for (auto& ap : ctx_.world->getActors()) {
            Actor* a = ap.get();
            if (a == editorCamActor_) continue;
            glm::vec4 c; bool has = false;
            for (const auto& comp : a->getComponents())
                if (comp->editorIcon(c)) { has = true; break; }
            if (!has) continue;
            float d = glm::length(toFb(a->worldPosition()) - wvp.mouseFb);
            if (d < iconBestD) { iconBestD = d; iconBest = a; }
        }
        if (iconBest) { ctx_.selected = iconBest; return; }
    }

    // 컴포넌트 종류를 분기하지 않고, 각 컴포넌트가 알리는 로컬 바운드(localBounds)에
    // 광선-OBB 판정을 합니다. 메시·3D UI 패널처럼 솔리드 몸체가 있는 것이 여기서 잡히고,
    // 라이트·카메라처럼 몸체가 없는 것은 아이콘 픽으로 넘어갑니다(다음 단계).
    Actor* best  = nullptr;
    float  bestT = std::numeric_limits<float>::max();

    for (auto& ap : ctx_.world->getActors()) {
        Actor* a = ap.get();

        glm::vec3 lo, hi;
        bool hasBounds = false;
        for (const auto& comp : a->getComponents()) {
            glm::vec3 cmin, cmax;
            if (comp->localBounds(cmin, cmax)) {
                if (!hasBounds) { lo = cmin; hi = cmax; hasBounds = true; }
                else { lo = glm::min(lo, cmin); hi = glm::max(hi, cmax); }   // 합집합
            }
        }
        if (!hasBounds) continue;

        const glm::mat4 wm  = a->worldMatrix();
        const glm::mat4 inv = glm::inverse(wm);
        const glm::vec3 ro  = glm::vec3(inv * glm::vec4(rayO, 1.f));
        const glm::vec3 rd  = glm::vec3(inv * glm::vec4(rayD, 0.f));   // 로컬 광선(정규화 안 함)

        float tmin = -std::numeric_limits<float>::max();
        float tmax =  std::numeric_limits<float>::max();
        bool  hit  = true;
        for (int k = 0; k < 3; ++k) {
            if (std::abs(rd[k]) < 1e-8f) {
                if (ro[k] < lo[k] || ro[k] > hi[k]) { hit = false; break; }
            } else {
                float t1 = (lo[k] - ro[k]) / rd[k];
                float t2 = (hi[k] - ro[k]) / rd[k];
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
            }
        }
        if (!hit || tmax < std::max(tmin, 0.f)) continue;

        const float     tHit     = (tmin >= 0.f) ? tmin : tmax;   // 안에서 시작하면 나가는 지점
        const glm::vec3 localHit = ro + rd * tHit;
        const glm::vec3 worldHit = glm::vec3(wm * glm::vec4(localHit, 1.f));
        const float     worldT   = glm::length(worldHit - rayO);
        if (worldT < bestT) { bestT = worldT; best = a; }
    }

    ctx_.selected = best;
}
