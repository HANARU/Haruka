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
#include <unordered_set>

// ── Helpers ────────────────────────────────────────────────────────────────────
static glm::vec3 quatToEulerDeg(const glm::quat& q) {
    return glm::degrees(glm::vec3(glm::pitch(q), glm::yaw(q), glm::roll(q)));
}

struct PropsSnapshotCmd : ICommand {
    // props 의 setF/getF 람다가 **컴포넌트 원시 포인터**를 캡처하므로, 그 컴포넌트가
    // 사라지면 이 커맨드를 반드시 걷어내야 undo/redo 가 해제된 메모리를 만지지 않습니다.
    // 사라지는 경로가 둘이라 연결 고리도 둘입니다.
    //   1) 액터 파괴 → onActorDestroyed → purge(Actor*)      → refersTo
    //   2) 컴포넌트만 제거(디테일 패널 x) → purgeComponent   → refersToComponent
    // 2번은 액터가 살아남아 이벤트가 발화하지 않으므로 1번이 잡지 못합니다.
    const Actor*          actor;
    const Component*      comp;
    std::vector<PropDesc> props;
    std::vector<float>    before, after;

    PropsSnapshotCmd(const Actor* owner, const Component* c, std::vector<PropDesc> p,
                     std::vector<float> b, std::vector<float> a)
        : actor(owner), comp(c), props(std::move(p)),
          before(std::move(b)), after(std::move(a)) {}

    void undo() override { size_t j=0; for (auto& pd : props) if (pd.setF) pd.setF(before[j++]); }
    void redo() override { size_t j=0; for (auto& pd : props) if (pd.setF) pd.setF(after[j++]);  }
    bool refersTo(const Actor* a) const override { return actor == a; }
    bool refersToComponent(const Component* c) const override { return comp == c; }
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
        // 파괴는 erase 앞에서 방송되므로 여기서 a->id 를 읽는 것은 안전합니다.
        // 서브트리 재귀 파괴 시 자손마다 따로 방송되어 접힘 상태가 남지 않습니다.
        outlinerCollapsed_.erase(a->id);
        if (prevSelected2_ == a) prevSelected2_ = nullptr;
        if (ctx_.selected == a) {
            ctx_.selected = nullptr;
            // 리네임 중인 액터가 (조상 삭제로) 함께 사라질 수 있습니다. 여기서 끄지 않으면
            // 다음 Enter 가 tickRenameInput 에서 널 selected 를 역참조합니다.
            renaming_     = false;
            renameSelAll_ = false;
        }
        outlinerDirty_ = true;
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
    saveDockLayout();   // 최종 레이아웃(활성 탭 포함)을 종료 시점에 영속화
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
// 전방 선언: 정의는 아래 "뷰포트 툴바" 절에 있으나 tick 의 휠 라우팅이 먼저 씁니다.
static float cycleSpeed(float cur, int dir);

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

    // 휠은 프로세스 전체에서 이 한 줄로만 읽습니다(getScrollDelta 는 consume-on-read라
    // 두 곳에서 읽으면 한쪽이 항상 0 을 받습니다). 커서가 아웃라이너 목록 위이고 카메라
    // 모드가 아닐 때만 목록 스크롤로 가로채고, 나머지는 카메라 돌리로 넘깁니다.
    float scroll = ctx_.window ? ctx_.window->getScrollDelta() : 0.f;
    // 좌버튼을 누르고 있는 동안에는 목록을 스크롤하지 않습니다. 스크롤하면 리빌드가 행
    // 위젯을 통째로 갈아 끼우는데, HSlate 에는 포인터 캡처도 프레스 추적도 없어서 마우스-업이
    // "그 자리에 새로 온" 다른 액터의 버튼으로 배달됩니다(누른 적 없는 액터가 삭제됨).
    // 아웃라이너가 비활성 탭이면 위젯이 배치되지 않아 geometry 가 stale 하므로 제외합니다.
    if (scroll != 0.f && outlinerList_ && outlinerVisible_ && ctx_.window &&
        !ctx_.window->isCameraModeActive() &&
        !ctx_.window->isMouseButtonDown(HInput::MOUSE_BUTTON_LEFT)) {
        float mx, my;
        ctx_.window->getMousePosition(mx, my);
        // 히트테스트 좌표계는 handleInput 의 e.position 과 같은 윈도 좌표로 통일합니다.
        if (outlinerList_->cachedGeometry().contains(glm::vec2{mx, my})) {
            outlinerScrollTop_ = std::max(0, outlinerScrollTop_ + (scroll > 0.f ? -3 : 3));
            outlinerDirty_     = true;
            scroll             = 0.f;
        }
    }
    // RMB 비행 중 휠은 돌리가 아니라 이동 속도 조절입니다. 카메라 모드를 놓으면
    // 종전처럼 돌리로 돌아갑니다.
    if (scroll != 0.f && ctx_.window && ctx_.window->isCameraModeActive()) {
        camSpeed_ = cycleSpeed(camSpeed_, scroll > 0.f ? 1 : -1);
        scroll    = 0.f;
    }
    tickEditorCamera(dt, scroll);
    tickRenameInput();
    tickShortcuts();

    // Gizmo hotkeys Q/W/E/R (edge trigger, only when not in camera mode / not typing)
    // 드래그 중에는 모드를 바꾸지 않습니다. 바뀌면 UI 와 실제 조작이 어긋나고,
    // 크기 균등 핸들(axis_ == 3) 상태로 이동·회전 분기에 들어가 축 배열을 넘겨 읽습니다.
    if (ctx_.window && !renaming_ && !outlinerSearchFocused_ && !gizmo_.isDragging() &&
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
        // 눈으로 끈 액터는 뷰포트에서 그려지지도 잡히지도 않으므로 기즈모도 띄우지
        // 않습니다. 그러지 않으면 화면에 없는 것을 축 화살표만 보고 움직이게 됩니다.
        Actor* const gizmoTarget =
            (ctx_.selected && ctx_.selected->activeInHierarchy()) ? ctx_.selected : nullptr;
        gizmo_.setSnap(snap_);
        gizmo_.tick(gizmoTarget, *ctx_.world, wvp, s(18.f),
                    gizmoMode_, worldSpace_, inView, lines, tris);
        buildEditorIcons(*ctx_.world, wvp, lines, tris);
        ctx_.renderer->setEditorLines(std::move(lines));
        ctx_.renderer->setGizmoTris(std::move(tris));
    }

    int  actorCount    = (int)ctx_.world->getActors().size();
    bool selChanged    = (ctx_.selected != prevSelected_);

    // 리네임 해제를 리빌드 **앞**으로 옮깁니다. 뒤에 두면 선택이 바뀌는 프레임에 새 행이
    // 직전 액터의 renameBuffer_ 를 한 프레임 그대로 보여 줍니다.
    if (selChanged && renaming_) { renaming_ = false; renameSelAll_ = false; }
    // 접힌 부모 아래 자식을 뷰포트에서 픽하면 조상을 펼치고 그 행으로 스크롤합니다.
    if (selChanged && ctx_.selected) { revealActor(ctx_.selected); outlinerRevealSel_ = true; }

    bool renamChanged  = (renaming_ != prevRenaming_);

    // 스플리터·도킹으로 패널 크기가 바뀌면 표시 가능한 행 수와 말줄임 폭이 달라집니다.
    // 4px 로 양자화해 드래그 중 매 프레임 리빌드가 되지 않게 합니다.
    if (outlinerList_) {
        const HGeometry& lg = outlinerList_->cachedGeometry();
        const glm::ivec2 sig{ (int)(lg.size.x / 4.f), (int)(lg.size.y / 4.f) };
        if (sig != outlinerLayoutSig_) outlinerDirty_ = true;
    }

    if (actorCount != prevActorCount_ || selChanged || renamChanged || outlinerDirty_) {
        rebuildOutliner();
        prevActorCount_ = actorCount;
        outlinerDirty_  = false;
    }
    prevRenaming_ = renaming_;

    if (selChanged) {
        syncEulerCache();
        rebuildDetails();
        prevSelected2_ = prevSelected_;
        prevSelected_  = ctx_.selected;
    }
    // 뷰포트 툴바 축약 단계. 더 넓은 구성으로 되돌아갈 때만 여유 폭을 요구해, 스플리터를
    // 임계 폭 근처에서 끌 때 단계가 매 프레임 오가며 리빌드가 반복되는 것을 막습니다.
    {
        int lv = computeVpToolbarLevel();
        if (vpToolbarLevel_ >= 0 && lv < vpToolbarLevel_) {
            static constexpr float kUpgrade[5] = { 660.f, 595.f, 540.f, 455.f, 365.f };
            const float w = viewportFbRect().width / std::max(0.01f, uiScale_);
            // 여유가 모자라면 한 칸만 덜 펼칩니다. 직전 레벨을 그대로 되돌리면, 폭이
            // 한 프레임에 크게 뛰었을 때(빠른 드래그·창 최대화) 숨김 단계에 갇혀
            // 툴바가 영영 돌아오지 않습니다.
            if (w < kUpgrade[lv]) lv = std::min(vpToolbarLevel_, lv + 1);
        }
        if (lv != vpToolbarLevel_) { vpToolbarLevel_ = lv; vpToolbarDirty_ = true; }
        if (vpToolbarDirty_) { vpToolbarDirty_ = false; rebuildViewportToolbar(); }
    }

    // exchange로 플래그를 먼저 내려야 리빌드 중 도착한 로그가 다음 프레임에 잡힌다
    if (ctx_.logDirty.exchange(false, std::memory_order_acquire))
        rebuildBottomPanel();

    // 단축키·메뉴 액션처럼 위젯 이벤트를 거치지 않고 상태만 바뀌는 경로도 여기서 소진합니다.
    flushChromeRebuilds();
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
            ->addSlot(outlinerList_, VR::ESizeRule::Fill)
            // 액터 개수 줄. outlinerList_ 바깥이라 rebuildOutliner 의 clearSlots 와 무관하게
            // 남습니다. 문자열은 rebuildOutliner 가 캐시해 두고 여기서는 읽기만 합니다
            // (Attr 은 computeDesiredSize/paint 에서 매 프레임 여러 번 평가됩니다).
            ->addSlot(HNew(BR)->background(kBgHeader)->padding(HPadding{s(8.f), s(2.f)})
                ->content(HNew(STextBlock)
                    ->text(AttrStr([this]() -> std::string { return outlinerFooter_; }))
                    ->fontSize(s(11.f))->color(kTextDim)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                VR::ESizeRule::Fixed, s(kOutFooterH)));

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
    // 뷰포트 오버레이는 [툴바 / 간격 / 통계 / 남는 공간] 을 한 세로 상자에 쌓습니다.
    // 통계를 상수 패딩으로 밀어내는 방식이면 폰트·uiScale·툴바 축약 단계가 바뀔 때마다
    // 어긋나는데, HSlate 에는 클리핑이 없어 글자끼리 경고 없이 그대로 포개집니다.
    viewportToolbarBox_ = HNew(HR);
    viewportToolbarBar_ = HNew(BR)
        ->background({0.08f, 0.09f, 0.11f, 0.82f})
        ->borderColor(kBorder)->borderThickness(1.f)
        ->padding(HPadding{s(6.f), s(4.f)})
        ->content(viewportToolbarBox_);
    auto vpLayer = HNew(VR)
        ->addSlot(viewportToolbarBar_, VR::ESizeRule::Auto, 0.f, HPadding{}, EHAlign::Left)
        ->addSlot(spacer(), VR::ESizeRule::Fixed, s(6.f))
        ->addSlot(HNew(BR)->background({0.f, 0.f, 0.f, 0.55f})->padding(HPadding{s(6.f), s(4.f)})
                      ->content(statsText),
                  VR::ESizeRule::Auto, 0.f, HPadding{}, EHAlign::Left)
        ->addSlot(spacer(), VR::ESizeRule::Fill)
        // 기즈모 드래그 값. 툴바 안에 두면 드래그 중 폭이 늘어 사다리 계산이 흔들리므로
        // 뷰포트 하단에 따로 둡니다. 드래그 중이 아니면 빈 문자열이라 자리를 차지하지 않습니다.
        ->addSlot(HNew(STextBlock)
                      ->text(AttrStr([this] { return gizmoReadoutText(); }))
                      ->fontSize(s(13.f))->color({1.f, 0.85f, 0.35f, 1.f})
                      ->hAlign(EHAlign::Left)->vAlign(EVAlign::Bottom),
                  VR::ESizeRule::Auto, 0.f, HPadding{}, EHAlign::Left);

    auto viewportPanel = HNew(SOverlay);
    viewportPanel->addSlot(viewportWidget_);
    // 레이어 슬롯은 반드시 Fill/Fill 입니다. 툴바 크기에 딱 맞춘 슬롯이면 SOverlay 가
    // 자신을 포함하는 슬롯에만 마우스 이동을 내려보내므로, 커서가 툴바를 벗어나는 순간
    // 이동 이벤트가 끊겨 버튼 하이라이트가 굳습니다. Fill 이면 뷰포트 어디로 움직여도
    // 세로 상자가 모든 자식에게 이동을 전달해 hover 가 풀립니다.
    viewportPanel->addSlot(vpLayer, EHAlign::Fill, EVAlign::Fill,
                           HPadding{s(kVpToolbarInset), s(kVpToolbarInset)});

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

    // 저장된 레이아웃을 복원하고(등록된 패널 기준으로 검증), 없거나 손상되면 기본 배치로 폴백합니다.
    // uiScale 변경 등으로 buildWidgetTree 가 재호출될 때는 dockRoot_ 가 이미 살아 있으므로 재로딩하지
    // 않습니다 — 세션 중 사용자가 바꾼 레이아웃을 디스크 값으로 되돌리지 않기 위함입니다.
    if (!dockRoot_ && !loadDockLayout())
        buildDefaultDockLayout();

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
    rebuildViewportToolbar();

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
// 전방 선언: 정의는 아래 "도크 트리 변형 헬퍼" 절에 있으나 rebuildDockArea 가 먼저 사용합니다.
static bool dockPanelIsActive(const DockNode* n, const std::string& id);

void EditorApp::rebuildDockArea() {
    dockSplitters_.clear();
    dockTabHits_.clear();
    dockLeafHits_.clear();
    // 뷰포트가 활성 탭일 때만 그 위젯이 배치되어 viewportFbRect 가 유효합니다. 비활성이면
    // inViewport 게이팅을 꺼서(아래) stale rect 위 클릭이 픽·오빗·기즈모로 새지 않게 합니다.
    viewportVisible_ = dockRoot_ && dockPanelIsActive(dockRoot_.get(), "Viewport");
    // 아웃라이너도 같은 이유로 필요합니다(휠 라우팅이 이 패널의 geometry 를 봅니다).
    outlinerVisible_ = dockRoot_ && dockPanelIsActive(dockRoot_.get(), "Outliner");
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

// ── 도크 트리 직렬화 (Phase 0 · 2c) ───────────────────────────────────────────────
// 노드를 전위 순회로 텍스트화합니다. 각 노드가 자기 자식 수를 명시하므로 재귀 하강으로
// 모호함 없이 되읽습니다. 저장 대상은 제목이 아니라 **패널 id**(공백 없는 레지스트리 키)라
// 공백 구분으로 충분합니다.
//   리프 :  L <activeTab> <count> <id0> <id1> ...
//   분할 :  S <H|V> <count> <r0> <r1> ...        (뒤에 자식 노드 count 개가 이어짐)
static void dockSerialize(const DockNode* n, std::ostringstream& o) {
    if (!n) { o << "L 0 0\n"; return; }             // 방어적: 빈 리프로 기록
    if (n->isLeaf()) {
        o << "L " << n->activeTab << ' ' << n->panelIds.size();
        for (const auto& id : n->panelIds) o << ' ' << id;
        o << '\n';
    } else {
        o << "S " << (n->split == DockNode::Split::Vertical ? 'V' : 'H')
          << ' ' << n->children.size();
        for (size_t i = 0; i < n->children.size(); ++i)
            o << ' ' << ((i < n->ratios.size()) ? n->ratios[i] : 0.f);
        o << '\n';
        for (const auto& c : n->children) dockSerialize(c.get(), o);
    }
}

// 재귀 하강 파서. 형식 오류·과도한 중첩·비정상 개수는 ok=false 로 표시해 트리 전체를 폐기하고
// 기본 레이아웃으로 폴백하게 합니다(부분 트리를 억지로 살리지 않음 — 예측 가능성 우선).
static std::unique_ptr<DockNode> dockParse(std::istringstream& in, int depth, bool& ok) {
    if (!ok) return nullptr;
    if (depth > 32) { ok = false; return nullptr; }     // 깊이 폭주(손상 파일) 방어
    std::string tag;
    if (!(in >> tag)) { ok = false; return nullptr; }
    if (tag == "L") {
        int active = 0, count = 0;
        if (!(in >> active >> count) || count < 0 || count > 64) { ok = false; return nullptr; }
        std::vector<std::string> ids;
        for (int i = 0; i < count; ++i) {
            std::string id;
            if (!(in >> id)) { ok = false; return nullptr; }
            ids.push_back(std::move(id));
        }
        return DockNode::makeLeaf(std::move(ids), active);
    }
    if (tag == "S") {
        std::string orient; int count = 0;
        if (!(in >> orient >> count) || (orient != "H" && orient != "V")
            || count < 1 || count > 64) { ok = false; return nullptr; }
        std::vector<float> ratios(count, 0.f);
        for (int i = 0; i < count; ++i)
            if (!(in >> ratios[i])) { ok = false; return nullptr; }
        std::vector<std::unique_ptr<DockNode>> kids;
        for (int i = 0; i < count; ++i) {
            auto c = dockParse(in, depth + 1, ok);
            if (!ok) return nullptr;
            kids.push_back(std::move(c));
        }
        return DockNode::makeSplit(orient == "V" ? DockNode::Split::Vertical
                                                 : DockNode::Split::Horizontal,
                                   std::move(kids), std::move(ratios));
    }
    ok = false;
    return nullptr;
}

// 알 수 없는 패널 id 를 걷어내고(전방 호환), 빈 리프는 제거하며, 자식이 하나만 남은 분할은
// 접습니다. valid(id) 는 PanelRegistry 조회입니다. 반환 nullptr = 이 서브트리 전체 소멸.
// seen 은 **트리 전체** 기준 중복 제거용입니다. 인앱 재도킹은 "한 패널은 한 리프에만" 불변식을
// 지키지만(performRedock 이 소스에서 먼저 제거), 로더는 임의의 외부 파일을 받으므로 같은 id 가
// 두 리프에 실릴 수 있습니다. 그러면 같은 위젯 shared_ptr 이 두 부모에 배치되어 배치·히트테스트가
// 깨집니다. valid(id) && 첫 등장일 때만 남겨 이를 막습니다.
template <class Valid>
static std::unique_ptr<DockNode> dockPrune(std::unique_ptr<DockNode> n, const Valid& valid,
                                           std::unordered_set<std::string>& seen) {
    if (!n) return nullptr;
    if (n->isLeaf()) {
        std::string activeId;   // 살아남으면 활성 탭을 원래 패널로 되돌리기 위해 보관
        if (n->activeTab >= 0 && n->activeTab < (int)n->panelIds.size())
            activeId = n->panelIds[n->activeTab];
        std::vector<std::string> kept;
        for (auto& id : n->panelIds)
            if (valid(id) && seen.insert(id).second) kept.push_back(id);   // 유효 + 트리 전체 첫 등장만
        if (kept.empty()) return nullptr;
        n->panelIds  = std::move(kept);
        n->activeTab = 0;
        for (int i = 0; i < (int)n->panelIds.size(); ++i)
            if (n->panelIds[i] == activeId) { n->activeTab = i; break; }
        return n;
    }
    std::vector<std::unique_ptr<DockNode>> kids;
    std::vector<float>                     ratios;
    for (size_t i = 0; i < n->children.size(); ++i) {
        auto c = dockPrune(std::move(n->children[i]), valid, seen);
        if (c) { kids.push_back(std::move(c));
                 ratios.push_back(i < n->ratios.size() ? n->ratios[i] : 1.f); }
    }
    if (kids.empty())    return nullptr;
    if (kids.size() == 1) return std::move(kids[0]);   // 자식 1 → 분할을 접음
    n->children = std::move(kids);
    n->ratios   = std::move(ratios);
    dockNormalize(n.get());
    return n;
}

// 트리 어딘가에 특정 패널 id 가 있는지 검사합니다(뷰포트 필수 패널 보증용).
static bool dockContains(const DockNode* n, const std::string& id) {
    if (!n) return false;
    if (n->isLeaf()) {
        for (const auto& p : n->panelIds) if (p == id) return true;
        return false;
    }
    for (const auto& c : n->children) if (dockContains(c.get(), id)) return true;
    return false;
}

// 특정 패널이 자신이 속한 리프에서 "활성 탭"인지(= 이번 프레임에 실제로 배치되는지) 검사합니다.
// 비활성이면 그 위젯은 트리에 없어 arrange 되지 않고 geometry 가 stale 해집니다.
static bool dockPanelIsActive(const DockNode* n, const std::string& id) {
    if (!n) return false;
    if (n->isLeaf()) {
        return n->activeTab >= 0 && n->activeTab < (int)n->panelIds.size()
            && n->panelIds[n->activeTab] == id;
    }
    for (const auto& c : n->children) if (dockPanelIsActive(c.get(), id)) return true;
    return false;
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
    saveDockLayout();      // 재도킹 결과를 즉시 영속화(비정상 종료에도 보존)
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

// ── 레이아웃 저장/복원 (Phase 0 · 2c) ─────────────────────────────────────────────
// 도크 트리를 Config/editor_layout.txt 로 저장하고, 부팅 시 복원합니다. 파일이 없거나
// 파싱·검증에 실패하면 호출부가 기본 레이아웃으로 폴백합니다.
void EditorApp::saveDockLayout() const {
    if (!dockRoot_) return;
    std::error_code ec;
    fs::create_directories("Config", ec);           // 없으면 생성(실패해도 아래 open 이 처리)
    std::ofstream f("Config/editor_layout.txt");
    if (!f) { std::cerr << "[Editor] Layout save failed\n"; return; }
    std::ostringstream oss;
    oss << "# Haruka Dock Layout v1\n";
    dockSerialize(dockRoot_.get(), oss);
    f << oss.str();
}

bool EditorApp::loadDockLayout() {
    std::ifstream f("Config/editor_layout.txt");
    if (!f) return false;
    // '#' 주석 줄을 걷어내고 나머지를 토큰 스트림으로 파싱합니다.
    std::string body, line;
    while (std::getline(f, line)) {
        if (!line.empty() && line[0] == '#') continue;
        body += line; body += '\n';
    }
    std::istringstream in(body);
    bool ok = true;
    auto parsed = dockParse(in, 0, ok);
    if (!ok || !parsed) return false;
    // 등록된 패널만, 그리고 트리 전체에서 처음 나온 것만 남깁니다(알 수 없는 id 스킵 — 전방 호환,
    // + 손상 파일의 중복 id 로 같은 위젯이 두 리프에 실리는 것 방지).
    std::unordered_set<std::string> seen;
    auto pruned = dockPrune(std::move(parsed),
        [this](const std::string& id){ return panelReg_.find(id) != nullptr; }, seen);
    // 필수 뷰포트가 사라진 손상 파일이면 기본으로 폴백해 검은 화면(뷰포트 위젯 미배치)을 피합니다.
    if (!pruned || !dockContains(pruned.get(), "Viewport")) return false;
    dockRoot_ = std::move(pruned);
    return true;
}

// ── Menu bar (Phase 1) ───────────────────────────────────────────────────────────
void EditorApp::buildMenus() {
    menus_.clear();
    menus_.push_back({ "File", {
        { "New Scene",  [this]{ deserializeWorld(""); cmdStack_.clear(); } },  // 카메라만 남기고 비움
        { "Save Scene", [this]{ saveScene(); } },
        { "Load Scene", [this]{ loadScene(); } },
    }, 0.f });
    menus_.push_back({ "Edit", {
        { "Undo", [this]{ undo(); } },
        { "Redo", [this]{ redo(); } },
        { "Delete Selected", [this]{ deleteSelectedActor(); } },
    }, 0.f });
    menus_.push_back({ "Actor", {
        { "Add Empty", [this]{
            if (ctx_.world)
                ctx_.selected = ctx_.world->spawnActor("Actor_" + std::to_string(actorCounter_++));
        } },
        // 계층을 만들 수단이 지금은 씬 역직렬화뿐이라, 아웃라이너 트리를 실기로 확인할
        // 방법이 없습니다. 재부모화 드래그&드롭은 아직 계획에 없으므로 메뉴로 노출합니다.
        // 부모는 "직전에 선택했던 액터", 자식은 "지금 선택한 액터"입니다.
        { "Attach To Prev Selected", [this]{
            Actor* child  = ctx_.selected;
            Actor* parent = prevSelected2_;
            if (!child || !parent || child == parent) return;
            if (child == editorCamActor_ || parent == editorCamActor_) return;
            // attachTo 는 사이클을 std::cerr 로만 알리고 조용히 무시합니다(반환값 없음).
            // 호출 전에 조상 체인을 직접 확인해야 UI 가 성공한 줄 착각하지 않습니다.
            for (Actor* p = parent; p; p = p->getParent())
                if (p == child) return;
            child->attachTo(parent);
            // 선택 액터는 그대로라 tick 의 selChanged 자동 노출이 걸리지 않습니다. 새 부모와
            // 그 조상을 모두 펼치고 결과 행으로 스크롤해, 조작 결과가 반드시 보이게 합니다.
            outlinerCollapsed_.erase(parent->id);
            revealActor(child);
            outlinerRevealSel_ = true;
            outlinerDirty_     = true;
        } },
        { "Detach Selected", [this]{
            if (!ctx_.selected || ctx_.selected == editorCamActor_) return;
            ctx_.selected->detach();
            // 루트로 올라가면 id 순 정렬에서 자리가 크게 바뀌므로 그 행으로 스크롤합니다.
            outlinerRevealSel_ = true;
            outlinerDirty_     = true;
        } },
    }, 0.f });
    menus_.push_back({ "View", {
        { "Reset Layout", [this]{ buildDefaultDockLayout(); rebuildDockArea(); saveDockLayout(); } },
    }, 0.f });
}

void EditorApp::rebuildMenuBar() {
    if (!menuBarBox_) return;
    menuBarBox_->clearSlots();
    menuButtons_.assign(menus_.size(), nullptr);   // hover 전환 히트테스트용(menus_ 와 1:1)
    menuBarBox_->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(6.f));
    float x = s(6.f);
    for (int mi = 0; mi < (int)menus_.size(); ++mi) {
        menus_[mi].x = x;                     // 팝업 위치용 x(스케일 픽셀)
        const float w = s(58.f);
        const bool open = (openMenu_ == mi);
        auto btn = HNew(SButton)
                ->normalColor (open ? glm::vec4{0.20f, 0.20f, 0.26f, 1.f}
                                    : glm::vec4{0.f,   0.f,   0.f,   0.f})
                ->hoveredColor({0.18f, 0.18f, 0.22f, 1.f})
                ->pressedColor({0.24f, 0.24f, 0.30f, 1.f})
                ->padding(HPadding{s(10.f), s(4.f)})
                ->onClick([this, mi]{
                    openMenu_ = (openMenu_ == mi) ? -1 : mi;
                    menuBarDirty_ = menuPopupDirty_ = true;
                })
                ->content(HNew(STextBlock)->text(menus_[mi].name)->fontSize(s(13.f))
                    ->color(kTextBody)->hAlign(EHAlign::Center)->vAlign(EVAlign::Center));
        menuButtons_[mi] = btn;
        menuBarBox_->addSlot(btn, SHorizontalBox::ESizeRule::Fixed, w);
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

    const float rowH  = s(30.f);
    const float dropW = s(200.f);            // 최소 너비 고정 → Undo/Redo 같은 짧은 항목도 반듯한 박스

    auto items = HNew(SVerticalBox);
    for (int k = 0; k < (int)md.items.size(); ++k) {
        items->addSlot(
            HNew(SButton)
                ->normalColor ({0.11f, 0.11f, 0.14f, 1.f})
                ->hoveredColor({0.20f, 0.42f, 0.68f, 1.f})
                ->pressedColor({0.14f, 0.34f, 0.56f, 1.f})
                ->padding(HPadding{s(18.f), s(7.f)})
                ->hAlign(EHAlign::Fill)
                ->onClick([this, k]{
                    if (openMenu_ < 0 || openMenu_ >= (int)menus_.size()) return;
                    if (k < 0 || k >= (int)menus_[openMenu_].items.size()) return;
                    auto act = menus_[openMenu_].items[k].action;
                    openMenu_ = -1;
                    menuBarDirty_ = menuPopupDirty_ = true;
                    if (act) act();
                })
                ->content(HNew(STextBlock)->text(md.items[k].label)->fontSize(s(13.f))
                    ->color(kTextBody)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
            SVerticalBox::ESizeRule::Fixed, rowH);
    }
    // 툴바(0.12)보다 확실히 어두운 배경 + 밝은 테두리 → 툴바 위에 "떠 있는" 패널로 읽히게 합니다.
    // (기존엔 배경 0.10 이 툴바색과 거의 같아 겹치면 글자만 툴바에 얹힌 듯 지저분했습니다.)
    auto dropdown = HNew(SBox)->widthOverride(dropW)->content(
        HNew(SBorder)
            ->background({0.055f, 0.055f, 0.07f, 1.f})
            ->borderColor({0.34f, 0.34f, 0.40f, 1.f})->borderThickness(1.f)
            ->content(items));
    // 드롭 섀도(반투명 검정)를 살짝 어긋나게 깔아 툴바와의 경계를 더 분명히 합니다.
    auto shadow = HNew(SBox)->widthOverride(dropW)
        ->heightOverride((float)md.items.size() * rowH)
        ->content(HNew(SBorder)->background({0.f, 0.f, 0.f, 0.35f}));

    // 캐처(전면 투명 버튼)로 바깥 클릭을 받아 메뉴를 닫습니다. 단, **메뉴 바 행(y < kMenuBarH)은
    // 덮지 않습니다** — 캐처가 그 위까지 덮으면 다른 메뉴 버튼 클릭을 캐처가 먼저 삼켜(닫힘만 발생)
    // 메뉴 전환에 2클릭이 필요했습니다. VBox[ 상단 spacer(kMenuBarH) / 캐처(Fill) ] 로 캐처를 메뉴
    // 바 아래로만 두면, 메뉴 바 클릭은 spacer(Unhandled)를 통과해 곧장 메뉴 버튼으로 전달됩니다(1클릭).
    auto catcher = HNew(SVerticalBox)
        ->addSlot(spacer(), SVerticalBox::ESizeRule::Fixed, s(kMenuBarH))
        ->addSlot(
            HNew(SButton)->normalColor({0,0,0,0})->hoveredColor({0,0,0,0})->pressedColor({0,0,0,0})
                ->onClick([this]{ openMenu_ = -1; menuBarDirty_ = menuPopupDirty_ = true; })
                ->content(spacer()),
            SVerticalBox::ESizeRule::Fill);
    // 슬롯 순서 = z(뒤→앞): 캐처 / 섀도 / 드롭다운. 섀도는 드롭다운보다 (3,3)px 어긋나 깔립니다.
    auto inner = HNew(SOverlay);
    inner->addSlot(catcher);
    inner->addSlot(shadow,   EHAlign::Left, EVAlign::Top, HPadding{md.x + s(3.f), s(kMenuBarH) + s(3.f)});
    inner->addSlot(dropdown, EHAlign::Left, EVAlign::Top, HPadding{md.x,          s(kMenuBarH)});
    menuPopupHost_->content(inner);
}

// ── rebuildToolbar ─────────────────────────────────────────────────────────────
void EditorApp::rebuildToolbar() {
    if (!toolbarBox_) return;
    toolbarBox_->clearSlots();

    using HR = SHorizontalBox;

    // 변환 도구(Q/W/E/R)와 World/Local 좌표계는 Phase 2 에서 뷰포트 툴바로 이관했습니다
    // (rebuildViewportToolbar). 조작 대상 바로 위에 두는 편이 시선 이동이 짧습니다.

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

// ── 뷰포트 툴바 (Phase 2) ──────────────────────────────────────────────────────
// 카메라 이동 속도 프리셋(m/s). 1 unit = 1 meter 규약 기준이며 기본값 5 는 개편 전
// 하드코딩과 같은 값이라, 아무것도 만지지 않으면 종전과 똑같이 움직입니다.
static constexpr float kCamSpeeds[] = { 1.f, 2.f, 5.f, 10.f, 20.f, 40.f };
// 스냅 눈금 프리셋. 1 unit = 1 meter 규약 기준이라, UE 의 uu 값을 그대로 옮기지 않고
// 미터로 다시 잡았습니다(그대로 옮기면 10 m 스냅이 되어 쓸 수 없습니다).
static constexpr float kSnapMove[]   = { 0.05f, 0.1f, 0.25f, 0.5f, 1.f, 5.f };
static constexpr float kSnapRotate[] = { 5.f, 10.f, 15.f, 30.f, 45.f, 90.f };
static constexpr float kSnapScale[]  = { 0.05f, 0.1f, 0.25f, 0.5f, 1.f };

// 현재 값에 가장 가까운 프리셋을 찾아 dir 만큼 옮깁니다(양끝에서 순환).
template <size_t N>
static float cycleFrom(const float (&arr)[N], float cur, int dir) {
    int   best  = 0;
    float bestD = std::abs(arr[0] - cur);
    for (int i = 1; i < (int)N; ++i) {
        const float d = std::abs(arr[i] - cur);
        if (d < bestD) { bestD = d; best = i; }
    }
    return arr[((best + dir) % (int)N + (int)N) % (int)N];
}

// 현재 값에 가장 가까운 프리셋을 찾아 dir 만큼 옮깁니다(양끝에서 순환).
static float cycleSpeed(float cur, int dir) {
    constexpr int n = (int)(sizeof(kCamSpeeds) / sizeof(kCamSpeeds[0]));
    int   best = 0;
    float bestD = std::abs(kCamSpeeds[0] - cur);
    for (int i = 1; i < n; ++i) {
        const float d = std::abs(kCamSpeeds[i] - cur);
        if (d < bestD) { bestD = d; best = i; }
    }
    return kCamSpeeds[((best + dir) % n + n) % n];
}

// 뷰포트 폭에 따른 축약 단계입니다. HSlate 에는 클리핑이 없어서, 넘친 툴바는 이웃 패널
// 위에 그려지면서 정작 클릭은 조상 컨테이너의 영역 판정에 막혀 죽습니다(아웃라이너에서
// 겪은 폭 사다리와 같은 유형). 임계값은 레이아웃이 실제로 쓰는 폭 근사
// (글자 수 x 폰트 크기 x 0.6 + 버튼 좌우 여백)로 계산했으므로, 라벨 문구나 폰트 크기를
// 바꾸면 이 표도 함께 고쳐야 합니다.
int EditorApp::computeVpToolbarLevel() const {
    const HViewportRect r = viewportFbRect();
    const float w = r.width / std::max(0.01f, uiScale_);
    // 각 단계가 실제로 요구하는 폭(미스케일, 인셋·바 패딩 포함): 0=620, 1=555, 2=499,
    // 3=414, 4=328. 임계값은 그보다 넉넉해야 합니다. 임계값이 요구 폭보다 작으면 그
    // 구간에서 툴바가 뷰포트를 넘어가 이웃 패널에 가려지고, 가려진 칩은 보이지도
    // 눌리지도 않습니다(클리핑이 없어 그림만 넘고 클릭은 조상에서 걸립니다).
    if (w >= 640.f) return 0;   // 전체 라벨 + World/Local 두 버튼 + 스냅 토글·값
    if (w >= 575.f) return 1;   // 도구 라벨 축약
    if (w >= 520.f) return 2;   // 좌표계를 순환 버튼 하나로
    if (w >= 435.f) return 3;   // 스냅 값 칩 생략(토글은 유지)
    if (w >= 345.f) return 4;   // 스냅 그룹 생략 + 카메라 칩 축약
    return 5;                   // 숨김
}

// 드래그 중인 기즈모 값. 스냅이 실제로 걸리는지 눈으로 확인시키는 것이 목적입니다.
std::string EditorApp::gizmoReadoutText() const {
    int   axis = -1;
    float v    = 0.f;
    if (!gizmo_.dragReadout(axis, v)) return {};
    const char* an = (axis >= 0 && axis <= 2) ? (axis == 0 ? "X" : axis == 1 ? "Y" : "Z") : "";
    char buf[64];
    switch (gizmoMode_) {
        case 1:  std::snprintf(buf, sizeof buf, "Move %s %+.3f m",     an, v); break;
        case 2:  std::snprintf(buf, sizeof buf, "Rotate %s %+.1f deg", an, v); break;
        case 3:  std::snprintf(buf, sizeof buf, "Scale %s x%.3f",      an, v); break;
        default: return {};
    }
    return buf;
}

void EditorApp::rebuildViewportToolbar() {
    if (!viewportToolbarBox_ || !viewportToolbarBar_) return;
    viewportToolbarBox_->clearSlots();

    const int lv = std::max(0, vpToolbarLevel_);
    if (lv >= 5) {
        // 숨길 때는 슬롯을 비우고 여백까지 없애 실제 크기를 0 으로 만듭니다. 보이지만
        // 않게 하면(visibility) 컨테이너들이 영역만 보고 이벤트를 계속 삼킵니다.
        // 테두리도 함께 꺼야 합니다. SBorder 는 크기와 무관하게 테두리를 그리므로,
        // 크기 0 인 채로 두께가 남으면 좌상단에 작은 점이 찍힙니다.
        viewportToolbarBar_->padding(HPadding{0.f, 0.f})->borderThickness(0.f);
        return;
    }
    viewportToolbarBar_->padding(HPadding{s(6.f), s(4.f)})->borderThickness(1.f);

    auto label = [this](const std::string& t) {
        return HNew(STextBlock)->text(t)->fontSize(s(12.f))->color(kTextBody)
                 ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center);
    };
    auto labelAttr = [this](std::function<std::string()> fn) {
        return HNew(STextBlock)->text(AttrStr(std::move(fn)))->fontSize(s(12.f))
                 ->color(kTextBody)->hAlign(EHAlign::Center)->vAlign(EVAlign::Center);
    };
    // 상태 표시는 전부 isActive/AttrStr 로 매 프레임 읽습니다. 그래서 클릭이 리빌드를
    // 부를 일이 없고(자기 콜백 안에서 자기 컨테이너를 지우는 자기 파괴가 원천 차단),
    // 단축키로 바꾼 상태도 UI 에 즉시 반영됩니다.
    auto chip = [this](std::shared_ptr<HWidget> content,
                       std::function<bool()> active, std::function<void()> click) {
        viewportToolbarBox_->addSlot(
            HNew(SToggleButton)->padding(HPadding{s(10.f), s(5.f)})
                ->isActive(std::move(active))->onClick(std::move(click))
                ->content(std::move(content)),
            SHorizontalBox::ESizeRule::Auto);
    };
    auto divider = [this] {
        viewportToolbarBox_->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(4.f));
        viewportToolbarBox_->addSlot(HNew(SBorder)->background(kBorder),
                                     SHorizontalBox::ESizeRule::Fixed, 1.f);
        viewportToolbarBox_->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(4.f));
    };

    // ── 변환 도구 ─────────────────────────────────────────────────────────────
    static const char* kToolLong [4] = { "Select", "Move", "Rotate", "Scale" };
    static const char* kToolShort[4] = { "Sel",    "Mov",  "Rot",    "Scl"   };
    for (int m = 0; m < 4; ++m)
        chip(label(lv <= 0 ? kToolLong[m] : kToolShort[m]),
             [this, m] { return gizmoMode_ == m; },
             [this, m] { gizmoMode_ = m; });

    divider();

    // ── 좌표계 ────────────────────────────────────────────────────────────────
    if (lv <= 1) {
        chip(label("World"), [this] { return worldSpace_;  }, [this] { worldSpace_ = true;  });
        chip(label("Local"), [this] { return !worldSpace_; }, [this] { worldSpace_ = false; });
    } else {
        chip(labelAttr([this] { return worldSpace_ ? std::string("World")
                                                   : std::string("Local"); }),
             [] { return false; }, [this] { worldSpace_ = !worldSpace_; });
    }

    // ── 스냅 ──────────────────────────────────────────────────────────────────
    // 값 칩의 라벨은 지금 기즈모 모드를 따라갑니다. 스냅이 꺼져 있으면 괄호로 감싸
    // "지금은 적용되지 않는 값"임을 알립니다(아이콘이 없으므로 ASCII 로만 구분).
    if (lv <= 3) {
        divider();
        chip(label("Snap"), [this] { return snap_.enabled; },
             [this] { snap_.enabled = !snap_.enabled; });
        if (lv <= 2)
            chip(labelAttr([this] {
                     char v[24];
                     switch (gizmoMode_) {
                         case 1:  std::snprintf(v, sizeof v, "%.3f m", snap_.move);      break;
                         case 2:  std::snprintf(v, sizeof v, "%g deg", snap_.rotateDeg); break;
                         case 3:  std::snprintf(v, sizeof v, "x%.3f", snap_.scale);      break;
                         default: std::snprintf(v, sizeof v, "--");                      break;
                     }
                     char out[32];
                     std::snprintf(out, sizeof out, snap_.enabled ? "%s" : "(%s)", v);
                     return std::string(out);
                 }),
                 [this] { return snap_.enabled; },
                 [this] {
                     const int dir = (ctx_.window &&
                                      ctx_.window->isKeyDown(HInput::KEY_LEFT_SHIFT)) ? -1 : 1;
                     switch (gizmoMode_) {
                         case 1: snap_.move      = cycleFrom(kSnapMove,   snap_.move,      dir); break;
                         case 2: snap_.rotateDeg = cycleFrom(kSnapRotate, snap_.rotateDeg, dir); break;
                         case 3: snap_.scale     = cycleFrom(kSnapScale,  snap_.scale,     dir); break;
                         default: break;
                     }
                 });
    }

    divider();

    // ── 카메라 속도 ───────────────────────────────────────────────────────────
    // 클릭하면 프리셋을 한 칸 올리고 Shift 를 누른 채면 내립니다. 기본값(5)에서 벗어나
    // 있을 때만 활성으로 표시해, 종전과 다른 속도라는 것을 눈에 띄게 합니다.
    chip(labelAttr([this, lv] {
             char buf[32];
             std::snprintf(buf, sizeof buf, lv <= 3 ? "Cam %g m/s" : "%g m/s", camSpeed_);
             return std::string(buf);
         }),
         [this] { return camSpeed_ != 5.f; },
         [this] {
             const bool back = ctx_.window &&
                               ctx_.window->isKeyDown(HInput::KEY_LEFT_SHIFT);
             camSpeed_ = cycleSpeed(camSpeed_, back ? -1 : 1);
         });

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
    toolbarDirty_ = true;
}

void EditorApp::pausePlay() {
    if (playState_ == EPlayState::Playing) {
        playState_ = EPlayState::Paused;
        std::cout << "[Editor] ⏸ Paused\n";
    } else if (playState_ == EPlayState::Paused) {
        playState_ = EPlayState::Playing;
        std::cout << "[Editor] ▶ Resumed\n";
    }
    toolbarDirty_ = true;
}

void EditorApp::stopPlay() {
    if (playState_ == EPlayState::Editor) return;
    playState_ = EPlayState::Editor;
    deserializeWorld(worldSnapshot_);
    worldSnapshot_.clear();
    cmdStack_.clear();
    std::cout << "[Editor] ⏹ Stopped — world restored\n";
    toolbarDirty_ = true;
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

    // 씬 로드는 기존 액터를 전부 파괴하고 새 id 로 다시 스폰하므로, 아웃라이너의
    // 세션 상태(접힘·스크롤)를 그대로 두면 엉뚱한 액터가 접힌 채로 나타납니다.
    outlinerCollapsed_.clear();
    outlinerRows_.clear();
    outlinerScrollTop_ = 0;
    prevSelected2_     = nullptr;
    outlinerDirty_     = true;

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

// ── 아웃라이너 계층 헬퍼 (Phase 3) ─────────────────────────────────────────────
// Type 라벨은 컴포넌트에서 유도하되, 추가 순서에 흔들리지 않도록 고정 우선순위로
// 하나를 고릅니다. 색은 각 컴포넌트가 이미 선언한 editorIcon 색을 재사용해서,
// 아이콘 아틀라스 없이도 타입이 눈에 구분되게 합니다(타입 아이콘은 설계상 보류).
std::string EditorApp::outlinerTypeLabel(const Actor* a, glm::vec4& outColor) const {
    outColor = kTextDim;
    if (!a) return "Actor";

    int         bestRank     = 99;
    int         contributors = 0;
    std::string label;
    glm::vec4   color = kTextDim;

    for (const auto& comp : a->getComponents()) {
        const std::string tn = comp->typeName();
        int         rank = 6;
        std::string lbl;
        if (tn == "StaticMeshComponent")     { rank = 1; lbl = "Mesh"; }
        else if (tn == "LightComponent")     {
            rank = 2;
            const auto* lc = static_cast<const LightComponent*>(comp.get());
            lbl = (lc->lightType == LightType::Point) ? "PointLight"
                : (lc->lightType == LightType::Spot)  ? "SpotLight"
                                                      : "DirLight";
        }
        else if (tn == "CameraComponent")    { rank = 3; lbl = "Camera"; }
        else if (tn == "AudioComponent")     { rank = 4; lbl = "Audio";  }
        else if (tn == "RigidBodyComponent") { rank = 5; lbl = "Body";   }
        else {
            lbl = tn;
            const std::string suffix = "Component";
            if (lbl.size() > suffix.size() &&
                lbl.compare(lbl.size() - suffix.size(), suffix.size(), suffix) == 0)
                lbl.erase(lbl.size() - suffix.size());
        }
        ++contributors;
        if (rank < bestRank) {
            bestRank = rank;
            label    = lbl;
            glm::vec4 ic;
            color    = comp->editorIcon(ic) ? ic : kTextDim;
        }
    }
    if (label.empty()) return "Actor";
    outColor = color;
    return contributors > 1 ? label + "+" : label;   // 다른 컴포넌트도 있다는 표시
}

// 폭을 넘는 텍스트를 ".." 로 줄입니다. 폰트 실측을 쓰므로 const 가 아닙니다
// (HSlateRenderer::fontAtlas 가 비const 참조를 돌려줍니다).
std::string EditorApp::ellipsizeText(const std::string& text, float maxPx, float fontPx) {
    if (text.empty() || maxPx <= 1.f) return text;
    HFontAtlas& fonts = slateRenderer_.fontAtlas();
    if (fonts.measureText(text, fontPx) <= maxPx) return text;
    for (size_t n = text.size(); n > 1; --n) {
        std::string cand = text.substr(0, n - 1) + "..";
        if (fonts.measureText(cand, fontPx) <= maxPx) return cand;
    }
    return text.substr(0, 1);
}

// 루트부터 전위 순회하며 화면에 그릴 행을 평탄화합니다. 필터가 걸리면 매치도 아니고
// 매치 자손도 없는 서브트리는 out 을 되감아 통째로 버립니다(단일 패스, 순서 보존).
void EditorApp::flattenOutliner(const std::string& filt,
                                std::vector<OutlinerRow>& out) const {
    out.clear();
    if (!ctx_.world) return;
    const bool filtering = !filt.empty();

    auto matches = [&](const Actor* a) {
        std::string n = a->name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return n.find(filt) != std::string::npos;
    };

    std::function<bool(Actor*, int)> visit = [&](Actor* a, int depth) -> bool {
        if (depth > 64) return false;              // 병리적 깊이·사이클 안전 브레이크
        const size_t mine = out.size();
        out.push_back({ a->id, a, depth, false, true, false, !a->activeInHierarchy() });

        std::vector<Actor*> kids = a->getChildren();
        std::sort(kids.begin(), kids.end(),
                  [](const Actor* x, const Actor* y) { return x->id < y->id; });

        // 필터 중에는 접힘을 무시합니다. 그러지 않으면 매치된 자손이 접힌 조상 아래
        // 숨어 검색이 사실상 회귀합니다.
        const bool open = filtering ||
                          outlinerCollapsed_.find(a->id) == outlinerCollapsed_.end();
        bool keptChild = false;
        if (open)
            for (Actor* k : kids) keptChild = visit(k, depth + 1) || keptChild;

        const bool selfMatch = !filtering || matches(a);
        if (!selfMatch && !keptChild) { out.resize(mine); return false; }

        out[mine].hasKids  = filtering ? keptChild : !kids.empty();
        out[mine].expanded = open;
        out[mine].dimPath  = filtering && !selfMatch;   // 경로로만 남은 조상
        return true;
    };

    // 루트 판정에서 에디터 카메라는 완전히 제외합니다(아래 rebuildOutliner 주석 참고).
    std::vector<Actor*> roots;
    for (const auto& ap : ctx_.world->getActors()) {
        Actor* a = ap.get();
        if (a == editorCamActor_) continue;
        Actor* p = a->getParent();
        if (!p || p == editorCamActor_) roots.push_back(a);
    }
    // getActors 는 스폰 순서고 getChildren 은 attachTo 순서라 둘 다 표시 순서의
    // 진리원이 못 됩니다(씬 로드는 부모가 자식보다 뒤에 올 수 있음). id 로 정렬합니다.
    std::sort(roots.begin(), roots.end(),
              [](const Actor* x, const Actor* y) { return x->id < y->id; });
    for (Actor* r : roots) visit(r, 0);
}

void EditorApp::revealActor(Actor* a) {
    if (!a) return;
    for (Actor* p = a->getParent(); p; p = p->getParent())
        outlinerCollapsed_.erase(p->id);
}

void EditorApp::flushOutlinerRebuild() {
    if (!outlinerDirty_) return;
    rebuildOutliner();
    outlinerDirty_ = false;
}

// 크롬의 지연 리빌드를 소진합니다. 위젯 콜백이 자기 컨테이너를 직접 비우면 순회 중인
// 슬롯과 실행 중인 std::function 이 함께 파괴되므로, 콜백은 플래그만 세우고 여기서
// 한 번에 처리합니다. 아웃라이너보다 뒤에 두어, 메뉴 항목이 도크 변형을 요청한 경우
// 도크 쪽이 먼저 정리되게 합니다.
void EditorApp::flushChromeRebuilds() {
    if (menuBarDirty_)   { menuBarDirty_   = false; rebuildMenuBar();   }
    if (menuPopupDirty_) { menuPopupDirty_ = false; rebuildMenuPopup(); }
    if (toolbarDirty_)   { toolbarDirty_   = false; rebuildToolbar();   }
    if (detailsDirty_)   { detailsDirty_   = false; rebuildDetails();   }
}

// 행 하나를 조립합니다. 슬롯은 SHorizontalBox 가 penX 를 단조 누적하므로 서로 겹치지
// 않고, 그래서 화살표·눈·삭제는 이름 버튼의 자식이 아니라 반드시 형제여야 합니다
// (SButton 은 좌클릭을 자식에게 넘기지 않고 항상 Handled 로 끊습니다).
void EditorApp::addOutlinerRow(const OutlinerRow& r, float listW) {
    Actor* const   a         = r.actor;
    const uint64_t id        = r.id;
    const bool     sel       = (a == ctx_.selected);
    const bool     ren       = sel && renaming_;
    const bool     renAll    = ren && renameSelAll_;
    const bool     filtering = !outlinerSearch_.empty();

    // 폭 사다리. 고정폭 합이 패널 폭을 넘으면 이름 Fill 슬롯이 0 이 되어 그리기는
    // 넘쳐 보여도 히트테스트가 죽습니다(행 선택 자체가 불가능해짐). 들여쓰기 클램프 →
    // Type 컬럼 강등 → 들여쓰기 제거 순으로 폭을 반납합니다.
    float indent = std::min((float)r.depth * s(kOutIndent), std::max(0.f, listW * 0.35f));
    float typeW  = s(kOutTypeW);
    float fixed  = s(4.f) + indent + s(kOutArrowW) + typeW + s(kOutEyeW) + s(kOutDelW);
    if (listW - fixed < s(kOutMinLabelW)) { fixed -= typeW;  typeW  = 0.f; }
    if (listW - fixed < s(kOutMinLabelW)) { fixed -= indent; indent = 0.f; }
    const float nameW = std::max(s(kOutMinLabelW), listW - fixed) - s(18.f);   // 버튼 padding 보정

    // 같은 행을 다시 누르면 선택이 풀리는 것은 개편 전부터의 동작이라 그대로 둡니다.
    auto selectRow = [this, id] {
        renaming_ = false;
        if (Actor* t = ctx_.world->findById(id))
            ctx_.selected = (ctx_.selected == t) ? nullptr : t;
    };

    glm::vec4   typeCol;
    std::string typeStr = outlinerTypeLabel(a, typeCol);
    std::string shown   = ren ? (renAll ? renameBuffer_ : renameBuffer_ + "|")
                              : ellipsizeText(a->name, nameW, s(14.f));

    std::shared_ptr<HWidget> nameWidget =
        HNew(STextBlock)->text(shown)->fontSize(s(14.f))
            ->color(sel ? glm::vec4{1.f, 1.f, 1.f, 1.f}
                        : (r.dimPath || r.dimOff) ? kTextDim : kTextBody)
            ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);
    if (renAll)   // Ctrl+A 전체 선택 하이라이트
        nameWidget = HNew(SBorder)->background({0.20f, 0.45f, 0.85f, 1.f})
                         ->content(nameWidget);

    // 폰트 아틀라스는 ASCII 전용이라 유니코드 삼각형·눈은 .notdef 로 찍힙니다.
    // 글리프는 'v' '>' 'O' 'o' '-' 만 씁니다.
    const char* arrowGlyph = (!r.hasKids || filtering) ? "" : (r.expanded ? "v" : ">");
    // 작은 타깃은 SToggleButton 을 씁니다. 마우스-업에서 위치를 재검사하므로, 눌렀다가
    // 다른 칸에서 떼면 발화하지 않습니다(SButton 은 재검사 없이 발화).
    auto smallBtn = [&] {
        return HNew(SToggleButton)->padding(HPadding{0.f, 0.f})
                 ->isActive([] { return false; })          // 강제 파란 액센트 바 회피
                 ->normalColor({0.f, 0.f, 0.f, 0.f})
                 ->hoveredColor({0.25f, 0.25f, 0.30f, 1.f});
    };

    // 자식이 없거나 필터 중이면 화살표 자리는 '죽은 칸'이 아니라 행 선택으로 동작합니다.
    auto arrowBtn = smallBtn()
        ->content(HNew(STextBlock)->text(arrowGlyph)->fontSize(s(12.f))->color(kTextDim)
                    ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center))
        ->onClick((r.hasKids && !filtering)
            ? std::function<void()>([this, id] {
                  if (outlinerCollapsed_.count(id)) outlinerCollapsed_.erase(id);
                  else                              outlinerCollapsed_.insert(id);
                  outlinerDirty_ = true;   // 콜백 안에서 리빌드하면 자기 파괴가 됩니다
              })
            : std::function<void()>(selectRow));

    // 눈 글리프는 Attr 로 매 프레임 재평가되므로 클릭 즉시 상태가 뒤집혀 보입니다.
    // 'O' = 보임, '-' = 자신을 껐음, 'o' = 자신은 켜져 있으나 조상이 꺼짐.
    auto eyeBtn = smallBtn()
        ->content(HNew(STextBlock)
            ->text(AttrStr([this, id]() -> std::string {
                Actor* t = ctx_.world ? ctx_.world->findById(id) : nullptr;
                if (!t) return "-";
                if (!t->activeSelf())          return "-";
                return t->activeInHierarchy() ? "O" : "o";
            }))
            ->fontSize(s(12.f))->color(r.dimOff ? kTextDim : kTextBody)
            ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center))
        ->onClick([this, id] {
            Actor* t = ctx_.world ? ctx_.world->findById(id) : nullptr;
            // 에디터 카메라를 끄면 getActiveCamera 가 비어 뷰포트·기즈모·픽이 전부
            // 죽습니다. 아웃라이너에 노출하지 않지만 여기서도 이중으로 막습니다.
            if (!t || t == editorCamActor_) return;
            t->setActive(!t->activeSelf());
            outlinerDirty_ = true;
        });

    auto row = HNew(SHorizontalBox)
        ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(4.f) + indent)
        ->addSlot(arrowBtn, SHorizontalBox::ESizeRule::Fixed, s(kOutArrowW))
        ->addSlot(
            HNew(SButton)
                ->normalColor ({0.f, 0.f, 0.f, 0.f})
                ->hoveredColor(sel ? kSelected : glm::vec4{0.25f, 0.25f, 0.30f, 1.f})
                ->pressedColor({0.15f, 0.30f, 0.55f, 1.f})
                ->padding({s(8.f), s(6.f)})
                // SButton 기본 정렬은 Center 입니다. 그대로 두면 들여쓰기가 절반만 반영되고
                // 이름 길이에 따라 시작 x 가 흔들립니다. Fill 로 두어야 자식이 슬롯 폭을
                // 그대로 받아 STextBlock 의 Left 정렬이 실제 왼쪽 정렬이 됩니다.
                ->hAlign(EHAlign::Fill)
                ->onClick(selectRow)
                ->content(nameWidget),
            SHorizontalBox::ESizeRule::Fill);

    if (typeW > 0.f)
        row->addSlot(
            smallBtn()
                ->content(HNew(STextBlock)
                    ->text(ellipsizeText(typeStr, typeW - s(6.f), s(11.f)))
                    ->fontSize(s(11.f))->color(r.dimOff ? kTextDim : typeCol)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center))
                ->onClick(selectRow),
            SHorizontalBox::ESizeRule::Fixed, typeW);

    row->addSlot(eyeBtn, SHorizontalBox::ESizeRule::Fixed, s(kOutEyeW))
       ->addSlot(
           textButton("x", 12.f, {0.9f, 0.4f, 0.4f, 1.f}, {5.f, 3.f},
                      {0.f, 0.f, 0.f, 0.f}, {0.40f, 0.12f, 0.12f, 1.f},
                      [this, id] {
                          Actor* t = ctx_.world ? ctx_.world->findById(id) : nullptr;
                          if (!t || t == editorCamActor_) return;
                          if (ctx_.selected == t) {
                              ctx_.selected = nullptr;
                              renaming_     = false;
                          }
                          // 이 액터를 참조하는 커맨드는 onActorDestroyed 구독이
                          // 정밀 제거합니다(무관한 이력은 보존).
                          ctx_.world->destroyActor(t);
                          outlinerDirty_ = true;
                      }),
           SHorizontalBox::ESizeRule::Fixed, s(kOutDelW));

    // 선택 하이라이트는 행 전체에 깔되, SBorder 는 자식에게 이벤트를 그대로 넘기므로
    // 히트테스트에 개입하지 않습니다.
    outlinerList_->addSlot(
        HNew(SBorder)->background(sel ? kSelected : glm::vec4{0.f, 0.f, 0.f, 0.f})
            ->content(row),
        SVerticalBox::ESizeRule::Fixed, s(kOutRowH));
}

// ── rebuildOutliner ────────────────────────────────────────────────────────────
// HSlate 에는 스크롤 컨테이너도 클리핑도 없습니다. 그래서 목록이 패널보다 길어지면
// 이웃 패널 위로 그대로 새어 나갑니다. 여기서는 "보이는 창"에 드는 행만 슬롯으로
// 만들어(가상 스크롤) 그 문제를 원천에서 없앱니다. 창 밖 행은 존재하지 않습니다.
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
        SVerticalBox::ESizeRule::Fixed, s(kOutHeaderH));

    // Case-insensitive search filter
    std::string filter = outlinerSearch_;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    flattenOutliner(filter, outlinerRows_);

    // 지난 프레임 실측 지오메트리. 첫 프레임(또는 위젯 트리 재구성 직후)에는 0 이라
    // 시드값으로 폴백하고, 레이아웃 시그니처 비교가 다음 프레임에 자가 교정합니다.
    // 실측이 아예 없을 때(위젯 트리를 막 만든 프레임)만 시드로 폴백합니다. 실측이 작다고
    // 큰 시드를 쓰면 짧은 패널에 행을 과하게 만들어 그대로 패널 밖으로 넘칩니다.
    const HGeometry& lg = outlinerList_->cachedGeometry();
    const float listW = (lg.size.x >= 1.f) ? lg.size.x : s(outlinerW_);
    const float listH = (lg.size.y >= 1.f) ? lg.size.y : s(320.f);
    outlinerLayoutSig_ = { (int)(lg.size.x / 4.f), (int)(lg.size.y / 4.f) };

    outlinerRowCap_ = std::max(0, (int)((listH - s(kOutHeaderH)) / s(kOutRowH)));
    const int total = (int)outlinerRows_.size();

    // 검색어가 바뀌면 결과 집합 자체가 달라지므로 스크롤을 맨 위로 되돌립니다. 유지하면
    // 매치된 앞부분이 창 밖에 남아 "검색했는데 아무것도 안 나온" 것처럼 보입니다.
    if (filter != outlinerFilterPrev_) { outlinerScrollTop_ = 0; outlinerFilterPrev_ = filter; }

    int top = std::clamp(outlinerScrollTop_, 0, std::max(0, total - outlinerRowCap_));

    if (outlinerRevealSel_ && ctx_.selected) {     // 선택 행을 창 안으로 끌어옵니다
        for (int i = 0; i < total; ++i) {
            if (outlinerRows_[i].actor != ctx_.selected) continue;
            if (i < top)                          top = i;
            else if (i >= top + outlinerRowCap_)  top = i - outlinerRowCap_ + 1;
            break;
        }
    }
    outlinerRevealSel_ = false;
    outlinerScrollTop_ = top;

    const int end = std::min(top + outlinerRowCap_, total);
    for (int i = top; i < end; ++i) addOutlinerRow(outlinerRows_[i], listW);

    // 개수는 직렬화 집합과 같은 기준(에디터 카메라 제외)으로 셉니다.
    int actorTotal = 0;
    for (const auto& ap : ctx_.world->getActors())
        if (ap.get() != editorCamActor_) ++actorTotal;

    // 스크롤바가 없으므로, 창이 넘칠 때는 어디를 보고 있는지 푸터가 유일한 단서입니다.
    // 검색 중에도 같은 이유로 범위를 붙입니다.
    char buf[96];
    const bool windowed = total > outlinerRowCap_;
    if (!filter.empty() && windowed)
        std::snprintf(buf, sizeof buf, "%d of %d actors  (%d-%d)",
                      total, actorTotal, top + 1, end);
    else if (!filter.empty())
        std::snprintf(buf, sizeof buf, "%d of %d actors", total, actorTotal);
    else if (windowed)
        std::snprintf(buf, sizeof buf, "%d actors  (%d-%d)", actorTotal, top + 1, end);
    else
        std::snprintf(buf, sizeof buf, "%d actor%s", actorTotal, actorTotal == 1 ? "" : "s");
    outlinerFooter_ = buf;
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
                                   detailsDirty_ = true;
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
                               [this, getB, setB]{ setB(!getB()); detailsDirty_ = true; }),
                    SHorizontalBox::ESizeRule::Fixed, s(44.f))
                ->addSlot(spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f)),
            SVerticalBox::ESizeRule::Fixed, s(26.f));
    };

    for (auto& compPtr : sel->getComponents()) {
        Component* comp = compPtr.get();

        detailsList_->addSlot(spacer(), SVerticalBox::ESizeRule::Fixed, s(8.f));

        // Remove callback — type dispatch.
        // 제거 **전에** 그 컴포넌트를 참조하는 커맨드를 걷어냅니다. 프로퍼티 스냅샷의
        // 값 접근자가 컴포넌트 포인터를 붙잡고 있어서, 걷어내지 않으면 다음 Ctrl+Z 가
        // 해제된 메모리에 씁니다. 대상은 removeComponent 와 같은 기준(첫 매치)으로 얻습니다.
        auto dropCmds = [this](const Component* c) { if (c) cmdStack_.purgeComponent(c); };
        std::function<void()> removeFn;
        if (dynamic_cast<StaticMeshComponent*>(comp))
            removeFn = [this, sel, dropCmds]{ dropCmds(sel->getComponent<StaticMeshComponent>());
                sel->removeComponent<StaticMeshComponent>(); detailsDirty_ = true; };
        else if (dynamic_cast<LightComponent*>(comp))
            removeFn = [this, sel, dropCmds]{ dropCmds(sel->getComponent<LightComponent>());
                sel->removeComponent<LightComponent>(); detailsDirty_ = true; };
        else if (dynamic_cast<CameraComponent*>(comp))
            removeFn = [this, sel, dropCmds]{ dropCmds(sel->getComponent<CameraComponent>());
                sel->removeComponent<CameraComponent>(); detailsDirty_ = true; };
        else if (dynamic_cast<AudioComponent*>(comp))
            removeFn = [this, sel, dropCmds]{ dropCmds(sel->getComponent<AudioComponent>());
                sel->removeComponent<AudioComponent>(); detailsDirty_ = true; };
        else if (dynamic_cast<RigidBodyComponent*>(comp))
            removeFn = [this, sel, dropCmds]{ dropCmds(sel->getComponent<RigidBodyComponent>());
                sel->removeComponent<RigidBodyComponent>(); detailsDirty_ = true; };

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
                                       [this, comp]{ comp->setEnabled(!comp->isEnabled()); detailsDirty_ = true; }),
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
        auto compEnd = [this, sel, comp, props, snapBefore] {
            std::vector<float> after;
            after.reserve(props->size());
            for (auto& pd : *props)
                if (pd.getF) after.push_back(pd.getF());
            // 소유 액터와 대상 컴포넌트를 함께 넘겨, 둘 중 어느 쪽이 사라지든 커맨드가
            // 걷히게 합니다. compEnd 는 패널이 살아있는 동안만 호출되므로 둘 다 유효합니다.
            cmdStack_.push(std::make_unique<PropsSnapshotCmd>(sel, comp, *props,
                                                             *snapBefore, std::move(after)));
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
            detailsDirty_ = true;
        });
    if (!sel->getComponent<CameraComponent>())
        addCompBtn("+ Camera", [this, sel]{
            sel->addComponent<CameraComponent>();
            detailsDirty_ = true;
        });
    if (!sel->getComponent<StaticMeshComponent>())
        addCompBtn("+ Mesh", [this, sel]{
            sel->addComponent<StaticMeshComponent>();
            detailsDirty_ = true;
        });
    if (!sel->getComponent<AudioComponent>())
        addCompBtn("+ Audio", [this, sel]{
            sel->addComponent<AudioComponent>();
            detailsDirty_ = true;
        });
    if (!sel->getComponent<RigidBodyComponent>())
        addCompBtn("+ RigidBody", [this, sel]{
            sel->addComponent<RigidBodyComponent>();
            detailsDirty_ = true;
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
                            detailsDirty_ = true;
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
    // 위젯 콜백(펼침·눈·삭제)은 리빌드를 직접 하지 않고 플래그만 세웁니다. 슬롯 순회
    // 도중 clearSlots 가 돌면 지금 실행 중인 버튼과 그 std::function 자체가 파괴되기
    // 때문입니다. 이 함수에는 조기 return 이 여러 개라 소진은 스코프 가드로 합니다.
    // 소멸자는 이벤트 디스패치가 전부 끝난 뒤, 이 프레임의 drawFrame 전에 돕니다.
    struct DeferredRebuildGuard {
        EditorApp* self;
        ~DeferredRebuildGuard() {
            self->flushOutlinerRebuild();
            self->flushChromeRebuilds();
        }
    } deferredRebuildGuard{ this };

    // ── 메뉴 hover 전환 ──────────────────────────────────────────────────────────
    // 메뉴가 열려 있을 때 다른 메뉴 버튼 위로 마우스를 옮기면 그 메뉴로 전환합니다(표준 메뉴 바
    // 동작). 버튼 위젯은 프레임 간 유지되므로 직전 프레임의 실측 geometry 로 히트테스트합니다.
    if (openMenu_ >= 0) {
        const glm::vec2 mm{ mouseX, mouseY };
        for (int mi = 0; mi < (int)menuButtons_.size(); ++mi) {
            if (mi == openMenu_ || !menuButtons_[mi]) continue;
            if (menuButtons_[mi]->cachedGeometry().contains(mm)) {
                openMenu_ = mi;
                menuBarDirty_ = menuPopupDirty_ = true;
                break;
            }
        }
    }

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
                // sum <= 2*minR 이면 clamp 의 상한(sum-minR)이 하한(minR)보다 작아져 std::clamp 의
                // 전제(lo <= hi)를 위반합니다(MSVC 는 release 에서도 _STL_VERIFY 로 abort). 두 패널이
                // 이미 최소치 근처라는 뜻이므로 이 경우 리사이즈를 건너뜁니다.
                if (sum > 2.f * minR) {
                    const float newL = std::clamp(dockDragStartRatioL_ + dRatio, minR, sum - minR);
                    if (newL != R[i]) { R[i] = newL; R[i + 1] = sum - newL; rebuildDockArea(); }
                }
            }
            prevLeftDown_ = leftDown;
            return;
        }
        if (!leftDown && dockDragNode_) {
            dockDragNode_ = nullptr;
            saveDockLayout();            // 드래그 종료 시 비율 영속화
            // 이 프레임의 마우스-업을 소비합니다. 소비하지 않으면 leftJustUp 업 이벤트가 Slate 로
            // 흘러가 릴리즈 지점의 버튼(Load/Stop/Play 등)이 오발됩니다(드래그-무브·탭 릴리즈와 동일 규약).
            prevLeftDown_ = leftDown;
            return;
        }
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
    // 진행 중인 드래그는 반드시 handleDrag 의 종료 분기까지 도달해야 합니다. 드래그 도중
    // 액터를 눈으로 끄면(activeInHierarchy 거짓) 호출 자체가 끊겨 dragging_ 이 남고, 그다음
    // 클릭이 이어붙습니다. 시작 조건만 활성으로 막고, 진행 중이면 계속 넘깁니다.
    if (ctx_.world && ctx_.window && ctx_.selected && ctx_.selected != editorCamActor_ &&
        (ctx_.selected->activeInHierarchy() || gizmo_.isDragging())) {
        // 좌표는 worldVP 하나로만 만듭니다. inView 게이팅도 fb 공간으로 통일합니다.
        const bool inView = inViewport(mouseX, mouseY, winW, winH) &&
                            ctx_.selected->activeInHierarchy();
        gizmoConsumed = gizmo_.handleDrag(ctx_.selected, *ctx_.world, cmdStack_,
                                          worldVP(mouseX, mouseY, winW, winH),
                                          leftDown, prevLeftDown_,
                                          gizmoMode_, worldSpace_, inView);
    } else if (gizmo_.isDragging()) {
        gizmo_.cancel();   // 선택이 사라진 경우. handleDrag 는 !selected 에서 즉시 반환합니다.
    }
    // 기즈모로 회전시킨 뒤 디테일 패널의 ROTATION 행은 cachedEuler_ 를 읽으므로, 드래그가
    // 끝나는 프레임에 캐시를 맞춰 주지 않으면 패널 값이 낡은 채로 남습니다.
    {
        const bool dragNow = gizmo_.isDragging();
        // 회전 드래그에만 맞춥니다. 이동·크기 뒤에도 갱신하면 회전은 그대로인데 표시만
        // 다른 오일러 표현으로 튀어(같은 회전을 나타내는 값이 여러 개라) 값이 바뀐 것처럼 보입니다.
        if (prevGizmoDragging_ && !dragNow && gizmoMode_ == 2) syncEulerCache();
        prevGizmoDragging_ = dragNow;
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
    // 이동 이벤트에도 좌버튼의 현재 상태를 실어 보냅니다. 버튼류가 "눌린 채 리빌드되어
    // 남은 프레스 상태"를 스스로 정리할 때 이 값을 봅니다.
    e.button = -1;
    e.down   = leftDown;
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
        // 아웃라이너의 눈으로 끈 액터는 아이콘도 사라져야 합니다. 메시 렌더는 이미
        // getVisibleMeshes 에서 게이트되므로, 에디터 오버레이만 맞춰 줍니다.
        if (!a->activeInHierarchy()) continue;
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
    // 뷰포트가 비활성 탭이면 그 위젯은 배치되지 않아 viewportFbRect 가 stale 합니다. 그 rect 로
    // 판정하면 뷰포트를 가린 패널의 빈 영역 클릭이 픽·오빗으로, RMB 가 카메라 모드로 새므로
    // (뷰포트가 화면에 없을 때는) 항상 false 로 게이팅합니다.
    if (!viewportVisible_) return false;
    // 히트테스트도 프레임버퍼 공간으로 통일합니다. 마우스(윈도 픽셀)를 fb 로 변환해
    // 뷰포트 위젯의 실측 fb rect 와 비교하므로, 도킹·리사이즈·HiDPI 와 무관하게 정확합니다.
    const glm::vec2 fb = mouseToFb(mx, my, winW, winH);
    // 뷰포트 위에 떠 있는 툴바가 가린 영역은 3D 가 아닙니다. 이 한 줄로 픽·LMB 오빗·
    // 기즈모 호버·기즈모 드래그 시작·RMB 카메라 모드가 한꺼번에 게이팅됩니다.
    const HViewportRect tb = viewportToolbarFbRect();
    if (tb.width >= 1.f && tb.contains(fb.x, fb.y)) return false;
    return viewportFbRect().contains(fb.x, fb.y);
}

// 뷰포트 툴바가 가린 영역(프레임버퍼 픽셀). 배제 rect 는 반드시 툴바 막대 위젯의
// geometry 여야 합니다. 툴바를 담은 Fill 레이어의 geometry 를 쓰면 뷰포트 전체가
// 배제되어 픽도 카메라도 죽습니다.
HViewportRect EditorApp::viewportToolbarFbRect() const {
    if (!viewportToolbarBar_) return {};
    const HGeometry& g = viewportToolbarBar_->cachedGeometry();
    if (g.size.x < 1.f || g.size.y < 1.f) return {};
    return { g.position.x, g.position.y, g.size.x, g.size.y };
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
void EditorApp::tickEditorCamera(float dt, float scroll) {
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
        const float speed = camSpeed_ * dt;
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

    // Scroll dolly — 휠은 tick 이 이미 읽어 라우팅했습니다(consume-on-read 라
    // 여기서 다시 읽으면 한쪽이 항상 0 을 받습니다).
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
            if (!a->activeInHierarchy()) continue;   // 안 보이는 것은 잡히지도 않습니다
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
        if (!a->activeInHierarchy()) continue;   // 눈으로 끈 액터는 픽에서도 제외

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
