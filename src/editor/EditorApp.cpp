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

static constinit HLogCategory LogEditor{ "Editor", ELogVerbosity::Log };

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

    auto beginCmd = [&renderer]() { return renderer.beginOneTimeCmd(); };
    auto endCmd   = [&renderer](VkCommandBuffer cmd) { renderer.endOneTimeCmd(cmd); };

    slateRenderer_.init(renderer.getDevice(), renderer.getPhysDevice(),
                        renderer.getSwapFormat(), beginCmd, endCmd);

    scanModelFiles();
    loadLogFilter();   // 첫 syncLogCategories 보다 먼저. 숨긴 카테고리는 등록되는 순간 적용됩니다
    buildWidgetTree();
    rebuildOutliner();
    rebuildDetails();
    rebuildBottomPanel();

    renderer.setUIRenderer(&slateRenderer_);
    hotReloader_.start("shaders", renderer);
    HLOG(LogEditor, Log) << "Ready. Press Play to run game simulation.";
}

void EditorApp::cleanup() {
    saveDockLayout();   // 최종 레이아웃(활성 탭 포함)을 종료 시점에 영속화
    // 월드가 에디터보다 오래 살 수 있으므로, this 를 캡처한 리스너를 먼저 뗍니다.
    if (ctx_.world) ctx_.world->onActorDestroyed.remove(actorDestroyedSub_);
    hotReloader_.stop();
    slateRenderer_.cleanup();
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
    // Output Log 도 같은 우회로 스크롤합니다(DEF-57). 로그 행은 위젯을 다시 만들지 않고
    // 문자열만 바꾸므로, 아웃라이너와 달리 좌버튼을 누른 채 굴려도 클릭이 엇갈리지 않습니다.
    // 이동량은 여기서 쌓기만 하고, 필터를 적용한 실제 이동은 refreshLogView 가 합니다.
    if (scroll != 0.f && logRowsBox_ && bottomVisible_ && bottomTab_ == 1 && ctx_.window &&
        !ctx_.window->isCameraModeActive()) {
        float mx, my;
        ctx_.window->getMousePosition(mx, my);
        if (logRowsBox_->cachedGeometry().contains(glm::vec2{mx, my})) {
            logScrollReq_ += (scroll > 0.f) ? 3 : -3;
            logViewDirty_  = true;
            scroll         = 0.f;
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
    if (ctx_.window && currentFocus() == EFocusTarget::None && !gizmo_.isDragging() &&
        !ctx_.window->isCameraModeActive()) {
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
    if (selChanged && renaming_) setTextFocus(EFocusTarget::None);
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

    // 새 로그는 seq 비교로 감지합니다. 읽은 seq 를 먼저 기록하므로, 리빌드 도중 다른
    // 스레드가 남긴 로그는 다음 프레임에 잡힙니다. 하단 패널은 Output Log 탭일 때만
    // 다시 만듭니다. 예전에는 Content Browser 탭에서도 로그마다 리빌드되어, 그 사이에
    // 누른 버튼의 프레스 상태가 사라졌습니다(DEF-70).
    bool statusDirty = false;
    if (const uint64_t logSeq = HLog::latestSeq(); logSeq != logSeenSeq_) {
        logSeenSeq_   = logSeq;
        logViewDirty_ = true;   // 위젯은 다시 만들지 않고 행 문자열만 갱신합니다
        statusDirty   = true;
    }
    // Output Log 행 영역의 높이가 바뀌면(스플리터·도킹·창 크기) 행 수를 다시 잡습니다.
    // 폭만 바뀌어도 말줄임 폭이 달라지므로 문자열을 다시 계산합니다.
    if (bottomTab_ == 1 && logRowsBox_) {
        const HGeometry& lg = logRowsBox_->cachedGeometry();
        const glm::ivec2 sig{ (int)(lg.size.x / 4.f), (int)(lg.size.y / 4.f) };
        if (sig.y != logRowsSig_.y)      rebuildLogRows();
        else if (sig != logRowsSig_)   { logRowsSig_ = sig; logViewDirty_ = true; }
    }
    // 카테고리는 로그를 남기지 않아도(꺼진 로그의 isEnabled 호출만으로) 등록됩니다.
    // 팝업이 열려 있는 동안에는 새 카테고리가 목록에 바로 나타나게 합니다.
    if (logPopupOpen_) syncLogCategories();
    // 상태바 폭이 바뀌면 잘라 둔 문자열과 축약 단계를 다시 잡습니다(4px 양자화).
    if (statusBarBox_) {
        const HGeometry& g = statusBarBox_->cachedGeometry();
        const glm::ivec2 sig{ (int)(g.size.x / 4.f), (int)(g.size.y / 4.f) };
        if (sig != statusSizeSig_) {
            statusSizeSig_ = sig;
            const float w  = g.size.x / std::max(0.01f, uiScale_);
            const int   lv = (w >= 260.f) ? 0 : (w >= 150.f) ? 1 : 2;
            if (lv != statusNarrow_) statusNarrow_ = lv;
            statusDirty = true;
        }
    }
    if (statusDirty) updateStatusMessage();

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
    outlinerSearchBox_ = HNew(SSearchBox);
    auto outlinerSearch = outlinerSearchBox_
        ->placeholder("Search...")
        ->text(AttrStr([this]() -> std::string { return outlinerSearch_; }))
        ->focused([this]() -> bool { return outlinerSearchFocused_; })
        ->selectedAll([this]() -> bool { return searchSelAll_; })
        ->onClick([this]{
            setTextFocus(currentFocus() == EFocusTarget::Search ? EFocusTarget::None
                                                                : EFocusTarget::Search);
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
    logPopupHost_  = HNew(BR)->background({0.f, 0.f, 0.f, 0.f});
    // 위젯 트리를 새로 만들면 팝업이 붙어 있던 버튼도 사라지므로 팝업을 닫습니다.
    logPopupOpen_  = false;
    logPopupPanel_.reset();
    // ── 하단 상태바 ───────────────────────────────────────────────────────────
    // 클릭 대상이 없고 슬롯을 다시 만들지 않습니다. 폭에 따른 축약은 문자열 길이로만
    // 처리하므로(빈 문자열 → Auto 슬롯 폭 0) 리빌드 함수 자체가 필요 없습니다.
    statusBarBox_ = HNew(HR);
    statusBarBox_
        ->addSlot(spacer(), HR::ESizeRule::Fixed, s(8.f))
        ->addSlot(HNew(STextBlock)
                      ->text(AttrStr([this]() -> std::string { return statusMsg_; }))
                      // 색은 반드시 Attr 로 읽습니다. 값으로 넘기면 생성 시점 색에 고정되어
                      // 오류·경고여도 회색으로 남습니다.
                      ->fontSize(s(11.f))->color(Attr<glm::vec4>([this] { return statusMsgColor_; }))
                      ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                  HR::ESizeRule::Fill, 1.f)
        ->addSlot(spacer(), HR::ESizeRule::Fixed, s(10.f))
        ->addSlot(HNew(STextBlock)
                      ->text(AttrStr([this]() -> std::string { return statusFocusText(); }))
                      ->fontSize(s(11.f))->color(kTextDim)
                      ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
                  HR::ESizeRule::Auto)
        ->addSlot(spacer(), HR::ESizeRule::Fixed, s(8.f));
    auto statusBar = HNew(BR)->background(kBgHeader)
        ->borderColor(kBorder)->borderThickness(1.f)
        ->content(statusBarBox_);

    // 상태바는 반드시 Fixed 입니다. Auto 면 내용에 따라 높이가 흔들려, 그때마다
    // 뷰포트 rect 가 바뀌고 렌더 타깃이 재생성됩니다.
    auto rootStack = HNew(VR)
        ->addSlot(menuBar,   VR::ESizeRule::Fixed, s(kMenuBarH))
        ->addSlot(toolbar,   VR::ESizeRule::Fixed, s(kToolbarH))
        ->addSlot(dockHost_, VR::ESizeRule::Fill,  1.f)
        ->addSlot(statusBar, VR::ESizeRule::Fixed, s(kStatusBarH));
    auto root = HNew(SOverlay);
    root->addSlot(rootStack);
    root->addSlot(logPopupHost_);
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
    bottomVisible_   = dockRoot_ && dockPanelIsActive(dockRoot_.get(), "Bottom");   // Output Log 휠
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
    if (!f) { HLOG(LogEditor, Error) << "Layout save failed"; return; }
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
            // attachTo 는 사이클을 Scene 카테고리 경고로만 알리고 조용히 무시합니다(반환값 없음).
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
        HLOG(LogEditor, Log) << "Play";
    } else {
        HLOG(LogEditor, Log) << "Resume";
    }
    playState_ = EPlayState::Playing;
    toolbarDirty_ = true;
}

void EditorApp::pausePlay() {
    if (playState_ == EPlayState::Playing) {
        playState_ = EPlayState::Paused;
        HLOG(LogEditor, Log) << "Paused";
    } else if (playState_ == EPlayState::Paused) {
        playState_ = EPlayState::Playing;
        HLOG(LogEditor, Log) << "Resumed";
    }
    toolbarDirty_ = true;
}

void EditorApp::stopPlay() {
    if (playState_ == EPlayState::Editor) return;
    playState_ = EPlayState::Editor;
    deserializeWorld(worldSnapshot_);
    worldSnapshot_.clear();
    cmdStack_.clear();
    HLOG(LogEditor, Log) << "Stopped - world restored";
    toolbarDirty_ = true;
}

// ── Serialization ──────────────────────────────────────────────────────────────
// 씬 형식과 블록·부모 규칙은 SceneSerializer 가 소유합니다. 게임 모듈이 시작 씬을 같은
// 함수로 읽으므로, 여기에 별도 구현을 두면 두 경로가 어긋납니다. 에디터 카메라는 씬이
// 아니라 에디터 소유라 저장하지도 지우지도 않습니다.
std::string EditorApp::serializeWorld() {
    return SceneSerializer::writeScene(*ctx_.world, editorCamActor_);
}

void EditorApp::deserializeWorld(const std::string& data) {
    std::vector<Actor*> toDestroy;
    for (auto& ap : ctx_.world->getActors())
        if (ap.get() != editorCamActor_)
            toDestroy.push_back(ap.get());
    ctx_.selected = nullptr;
    for (Actor* a : toDestroy) ctx_.world->destroyActor(a);

    SceneSerializer::readScene(data, *ctx_.world, *ctx_.assets);

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
    if (!f) { HLOG(LogEditor, Error) << "Save failed"; return; }
    f << serializeWorld();
    HLOG(LogEditor, Log) << "Scene saved to scene.hscene";
}

void EditorApp::loadScene() {
    std::ifstream f("scene.hscene");
    if (!f) { HLOG(LogEditor, Error) << "Load failed: scene.hscene not found"; return; }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    deserializeWorld(data);
    cmdStack_.clear();
    HLOG(LogEditor, Log) << "Scene loaded from scene.hscene";
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
    // 접두어 폭은 길이에 따라 늘어나므로 이분 탐색으로 가장 긴 후보를 찾습니다. 예전의 선형
    // 탐색은 수백 자짜리 로그(검증 레이어 메시지 등)에서 행마다 수백 번 폰트 실측을 반복했습니다.
    size_t lo = 1, hi = text.size() - 1, best = 0;
    while (lo <= hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (fonts.measureText(text.substr(0, mid) + "..", fontPx) <= maxPx) { best = mid; lo = mid + 1; }
        else                                                                  { hi = mid - 1; }
    }
    return best ? text.substr(0, best) + ".." : text.substr(0, 1);
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

// ── 텍스트 입력 포커스 중재 ───────────────────────────────────────────────────
// 실제 포커스는 세 플래그에서 유도합니다. 숫자 필드는 위젯이 소유한 정적 상태라
// 여기서 진입시킬 수 없으므로, 그것이 잡혀 있으면 언제나 최우선으로 봅니다.
EditorApp::EFocusTarget EditorApp::currentFocus() const {
    if (SNumberField::focusedField()) return EFocusTarget::NumberField;
    if (renaming_)                    return EFocusTarget::Rename;
    if (outlinerSearchFocused_)       return EFocusTarget::Search;
    if (logSearchFocused_)            return EFocusTarget::LogSearch;
    return EFocusTarget::None;
}

void EditorApp::setTextFocus(EFocusTarget t) {
    const EFocusTarget prev = currentFocus();
    if (prev == t) return;

    // 이전 포커스를 정리합니다. 숫자 필드는 편집 중이던 값을 확정해야, 나중에 다른 곳에서
    // 누른 Enter 가 낡은 값으로 되살아나지 않습니다.
    if (prev == EFocusTarget::NumberField) {
        if (auto* nf = SNumberField::focusedField()) nf->commitEdit();
    } else if (prev == EFocusTarget::Rename) {
        renaming_ = false; renameSelAll_ = false; outlinerDirty_ = true;
    } else if (prev == EFocusTarget::Search) {
        outlinerSearchFocused_ = false; searchSelAll_ = false; outlinerDirty_ = true;
    } else if (prev == EFocusTarget::LogSearch) {
        logSearchFocused_ = false; logSearchSelAll_ = false; logViewDirty_ = true;
    }

    if (t == EFocusTarget::Rename) {
        if (ctx_.selected && ctx_.selected != editorCamActor_) {
            renaming_     = true;
            renameBuffer_ = ctx_.selected->name;
            renameSelAll_ = true;   // 윈도우 표준: 이름 변경 시작 시 전체 선택
        }
        outlinerDirty_ = true;
    } else if (t == EFocusTarget::Search) {
        outlinerSearchFocused_ = true;
        searchSelAll_          = !outlinerSearch_.empty();
        outlinerDirty_         = true;
    } else if (t == EFocusTarget::LogSearch) {
        logSearchFocused_ = true;
        logSearchSelAll_  = !logSearch_.empty();
        logViewDirty_     = true;
    }

    if (ctx_.window) {
        IWindow& w = *ctx_.window;
        // 포커스가 없던 동안 쌓인 문자를 버립니다. 그러지 않으면 그동안 누른 글자가
        // 입력창을 여는 순간 한꺼번에 쏟아집니다.
        std::vector<unsigned int> junk;
        w.drainCharQueue(junk);
        // 키 엣지를 현재 물리 상태로 다시 심습니다. 누른 채로 진입하면 그 키가 곧바로
        // 엣지로 잡혀(예: Enter 를 누른 채 리네임 진입) 편집이 즉시 끝나 버립니다.
        prevEnterDown_     = w.isKeyDown(HInput::KEY_ENTER);
        prevBackspaceDown_ = w.isKeyDown(HInput::KEY_BACKSPACE);
        prevF2Down_        = w.isKeyDown(HInput::KEY_F2);
        prevDeleteDown_    = w.isKeyDown(HInput::KEY_DELETE);
        prevVDown_         = w.isKeyDown(HInput::KEY_V);
        prevCDown_         = w.isKeyDown(HInput::KEY_C);
        prevADown_         = w.isKeyDown(HInput::KEY_A);
    }

    // 텍스트 입력을 시작하면 열려 있던 메뉴는 닫습니다.
    if (t != EFocusTarget::None && openMenu_ >= 0) {
        openMenu_     = -1;
        menuBarDirty_ = menuPopupDirty_ = true;
    }
}

bool EditorApp::focusWidgetContains(float x, float y) const {
    const glm::vec2 p{ x, y };
    switch (currentFocus()) {
        case EFocusTarget::NumberField:
            if (auto* nf = SNumberField::focusedField())
                return nf->cachedGeometry().contains(p);
            return false;
        case EFocusTarget::Search:
            return outlinerSearchBox_ && outlinerSearchBox_->cachedGeometry().contains(p);
        case EFocusTarget::LogSearch:
            return logSearchBox_ && logSearchBox_->cachedGeometry().contains(p);
        case EFocusTarget::Rename:
            // 리네임 행 위젯은 리빌드마다 새로 만들어지므로 목록 전체를 기준으로 봅니다.
            return outlinerList_ && outlinerList_->cachedGeometry().contains(p);
        default:
            return false;
    }
}

// 로그 한 줄의 표시 색. 심각도 필드로 정합니다. 예전에는 문자열에 ERROR/WARN 이 들어
// 있는지로 판정해, 그 단어를 쓰는 로그가 거의 없어 오류가 전부 회색이었습니다(DEF-72).
glm::vec4 EditorApp::logColor(ELogVerbosity v) {
    switch (v) {
        case ELogVerbosity::Error:   return { 0.90f, 0.35f, 0.35f, 1.f };
        case ELogVerbosity::Warning: return { 0.90f, 0.75f, 0.30f, 1.f };
        case ELogVerbosity::Verbose: return { 0.32f, 0.32f, 0.35f, 1.f };
        default:                     return kTextDim;
    }
}

// 콘솔과 같은 "[Category] Warning: 본문" 형식. 폰트가 ASCII 전용이라 그릴 수 없는 바이트는
// 물음표로 바꿉니다. 레코드의 원문(UTF-8)은 그대로 남으므로 콘솔에는 영향이 없습니다.
std::string EditorApp::formatLogLine(const HLogRecord& rec) {
    const HLogCategoryState* cat = HLog::categoryAt(rec.category);
    std::string line;
    line.reserve(rec.text.size() + 24);
    line += '[';
    line += cat ? cat->name : std::string("?");
    line += "] ";
    if (rec.verbosity == ELogVerbosity::Error)   line += "Error: ";
    if (rec.verbosity == ELogVerbosity::Warning) line += "Warning: ";
    line += rec.text;
    return toDisplayAscii(std::move(line));
}

std::string EditorApp::toDisplayAscii(std::string s) {
    for (char& c : s)
        if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    return s;
}

// 상태바 왼쪽 칸. 마지막 로그 한 줄을 상태바 폭에 맞춰 잘라 캐시합니다. 자르는 비용이
// 폰트 실측 반복이라 싸지 않으므로, Attr 안에서 부르지 않고 로그가 바뀌거나 폭이 달라진
// 프레임에만 호출합니다.
void EditorApp::updateStatusMessage() {
    // Verbose 는 건너뜁니다. 키 입력 같은 소음을 켜 두어도 상태바가 덮이지 않게 합니다.
    // Output Log 의 표시 필터는 따르지 않습니다(숨긴 카테고리의 오류도 여기에는 떠야 합니다).
    HLogRecord rec;
    if (!HLog::latestAtMost(ELogVerbosity::Log, rec)) {
        statusMsg_      = "Ready";
        statusMsgColor_ = kTextDim;
        return;
    }
    statusMsgColor_ = logColor(rec.verbosity);
    const std::string line = formatLogLine(rec);

    float avail = 0.f;
    if (statusBarBox_) {
        const HGeometry& g = statusBarBox_->cachedGeometry();
        avail = g.size.x - s(26.f) - (statusNarrow_ >= 2 ? 0.f : s(80.f));
    }
    statusMsg_ = (avail > s(24.f)) ? ellipsizeText(line, avail, s(11.f)) : std::string();
}

// 상태바 오른쪽 칸. 지금 키보드 입력이 어디로 가는지 알려 줍니다. Attr 로 매 프레임
// 평가되므로 짧은 리터럴만 돌려줍니다(문자열 조립·할당 금지).
std::string EditorApp::statusFocusText() const {
    if (statusNarrow_ >= 2) return {};
    switch (currentFocus()) {
        case EFocusTarget::Rename:      return statusNarrow_ ? "RENAME" : "KBD: Rename";
        case EFocusTarget::Search:      return statusNarrow_ ? "SEARCH" : "KBD: Search";
        case EFocusTarget::NumberField: return statusNarrow_ ? "VALUE"  : "KBD: Value";
        case EFocusTarget::LogSearch:   return statusNarrow_ ? "LOG"    : "KBD: Log Search";
        default:                        return {};
    }
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
    if (logPopupDirty_)  { logPopupDirty_  = false; rebuildLogPopup();  }
    // 로그 행은 위젯을 만들지 않고 문자열만 바꾸므로 콜백 안에서 불려도 안전하지만, 필터
    // 토글을 여러 번 누른 프레임에 한 번만 계산하려고 같은 소진 지점에 둡니다.
    if (logViewDirty_ && bottomTab_ == 1 && logRowsBox_) { logViewDirty_ = false; refreshLogView(); }
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

// ── Output Log (로그 카테고리 L3) ──────────────────────────────────────────────
// 코어 링의 새 레코드를 사본으로 옮깁니다. 사본이 있어야 필터를 바꿨을 때 지난 로그에
// 소급 적용할 수 있고, 매번 링 전체(최대 2048개 문자열)를 복사하지 않아도 됩니다.
void EditorApp::pullLogRecords() {
    std::vector<HLogRecord> fresh;
    HLog::copySince(logMirrorSeq_, fresh);
    if (fresh.empty()) return;

    for (HLogRecord& r : fresh) {
        if (r.category >= logCatNames_.size()) syncLogCategories();
        if (r.category < logCatCounts_.size()) ++logCatCounts_[r.category];
        logMirror_.push_back(std::move(r));
    }
    while (logMirror_.size() > HLog::kCapacity) {
        const uint16_t c = logMirror_.front().category;
        if (c < logCatCounts_.size()) --logCatCounts_[c];
        logMirror_.pop_front();
    }
    logMirrorSeq_ = logMirror_.back().seq;

    // 누계는 새 로그가 들어올 때만 바뀌므로 여기서 한 번 문자열로 만들어 둡니다
    // (Attr 은 매 프레임 여러 번 평가됩니다).
    uint32_t errs = 0, warns = 0;
    for (const HLogCategoryState* st : HLog::categories()) {
        errs  += st->errors.load(std::memory_order_relaxed);
        warns += st->warnings.load(std::memory_order_relaxed);
    }
    logErrText_  = "E " + std::to_string(errs);
    logWarnText_ = "W " + std::to_string(warns);
    logHasErr_   = errs  > 0;
    logHasWarn_  = warns > 0;
}

// 카테고리 인덱스는 등록 순서라 한 번 정해지면 바뀌지 않습니다. 늘어난 만큼만 붙입니다.
// 숨김 여부는 저장된 이름 집합에서 가져오므로, 지난 실행에서 숨긴 카테고리는 이번 실행에서
// 처음 등록되는 순간부터 숨겨집니다.
void EditorApp::syncLogCategories() {
    const std::vector<HLogCategoryState*> cats = HLog::categories();
    if (cats.size() == logCatNames_.size()) return;
    for (size_t i = logCatNames_.size(); i < cats.size(); ++i) {
        logCatNames_.push_back(toDisplayAscii(cats[i]->name));
        logCatHidden_.push_back(logHiddenNames_.count(cats[i]->name) ? 1 : 0);
    }
    logCatCounts_.resize(cats.size(), 0);
    if (logPopupOpen_) logPopupDirty_ = true;
}

void EditorApp::setLogCategoryHidden(uint16_t index, bool hidden) {
    if (index >= logCatHidden_.size()) return;
    logCatHidden_[index] = hidden ? 1 : 0;
    if (const HLogCategoryState* st = HLog::categoryAt(index)) {
        if (hidden) logHiddenNames_.insert(st->name);
        else        logHiddenNames_.erase(st->name);
    }
    logViewDirty_ = true;
}

// ── Output Log 설정 보존 (로그 카테고리 L4) ────────────────────────────────────
// 표시 필터 파일. 한 줄에 하나씩 "severity <수준> on|off" 와 "hide <카테고리>" 입니다.
// 모르는 줄은 건너뛰므로, 항목이 늘어도 예전 파일을 그대로 읽을 수 있습니다.
static constexpr const char* kLogFilterPath = "Config/editor_log.txt";
static constexpr ELogVerbosity kLogSeverities[] = { ELogVerbosity::Error, ELogVerbosity::Warning,
                                                    ELogVerbosity::Log,   ELogVerbosity::Verbose };

void EditorApp::loadLogFilter() {
    std::ifstream f(kLogFilterPath);
    if (!f) return;   // 첫 실행: 기본값(전부 표시)을 그대로 씁니다
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key, a, b;
        ls >> key >> a >> b;
        ELogVerbosity v = ELogVerbosity::Off;
        if (key == "severity" && parseLogVerbosity(a, v) && v != ELogVerbosity::Off &&
            (b == "on" || b == "off"))
            logShowVerb_[(int)v] = (b == "on");
        else if (key == "hide" && !a.empty())
            logHiddenNames_.insert(a);
    }
}

void EditorApp::saveLogFilter() const {
    std::error_code ec;
    fs::create_directories("Config", ec);   // 없으면 생성(실패해도 아래 open 이 처리)
    std::ofstream f(kLogFilterPath, std::ios::trunc);
    if (!f) { HLOG(LogEditor, Warning) << "Output Log filter save failed (" << kLogFilterPath << ")"; return; }
    f << "# Haruka Output Log display filter v1. Search text is not saved.\n";
    for (ELogVerbosity v : kLogSeverities)
        f << "severity " << logVerbosityName(v) << (logShowVerb_[(int)v] ? " on\n" : " off\n");
    // 이번 실행에서 아직 등록되지 않은 이름도 그대로 씁니다(지우면 설정이 사라집니다).
    for (const std::string& name : logHiddenNames_) f << "hide " << name << '\n';
}

void EditorApp::saveLogLevels() const {
    if (!HLog::saveLevelOverrides(HLog::kLevelOverridesPath))
        HLOG(LogEditor, Warning) << "log level save failed (" << HLog::kLevelOverridesPath << ")";
}

bool EditorApp::logRecordVisible(const HLogRecord& r, const std::string& needleLower) const {
    const int v = (int)r.verbosity;
    if (v <= 0 || v >= 5 || !logShowVerb_[v]) return false;
    if (r.category < logCatHidden_.size() && logCatHidden_[r.category]) return false;
    if (needleLower.empty()) return true;

    // 대소문자를 무시한 부분 일치. 본문과 카테고리 이름을 모두 봅니다.
    auto icontains = [&](const std::string& hay) {
        return std::search(hay.begin(), hay.end(), needleLower.begin(), needleLower.end(),
                           [](char a, char b) {
                               return std::tolower((unsigned char)a) == (unsigned char)b;
                           }) != hay.end();
    };
    if (icontains(r.text)) return true;
    return r.category < logCatNames_.size() && icontains(logCatNames_[r.category]);
}

// 행 위젯 N개를 만듭니다. 각 행은 logViewRows_[i] 를 Attr 로 읽기만 하므로, 로그가 들어와도
// 이 함수는 다시 불리지 않습니다. 행 영역 높이가 바뀌었을 때와 탭을 다시 열 때만 불립니다.
void EditorApp::rebuildLogRows() {
    if (!logRowsBox_) return;
    logRowsBox_->clearSlots();

    // 실측이 없을 때(위젯을 막 만든 프레임)만 시드로 추정하고, 다음 프레임의 크기 비교가
    // 실측으로 바로잡습니다.
    const HGeometry& g = logRowsBox_->cachedGeometry();
    const float rowH = s(kLogRowH);
    const float h    = (g.size.y >= 1.f) ? g.size.y : std::max(rowH * 6.f, s(bottomH_) - s(90.f));
    logRowsSig_ = { (int)(g.size.x / 4.f), (int)(g.size.y / 4.f) };
    logRowCap_  = std::max(1, (int)(h / rowH));

    using HR = SHorizontalBox;
    for (int i = 0; i < logRowCap_; ++i) {
        auto field = [this, i](std::string LogViewRow::* member) {
            return AttrStr([this, i, member]() -> std::string {
                return i < (int)logViewRows_.size() ? logViewRows_[i].*member : std::string();
            });
        };
        logRowsBox_->addSlot(
            HNew(HR)
                ->addSlot(HNew(STextBlock)->text(field(&LogViewRow::time))->fontSize(s(11.f))
                              ->color({ 0.36f, 0.36f, 0.40f, 1.f })
                              ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
                          HR::ESizeRule::Fixed, s(kLogTimeW))
                ->addSlot(spacer(), HR::ESizeRule::Fixed, s(8.f))
                ->addSlot(HNew(STextBlock)->text(field(&LogViewRow::category))->fontSize(s(11.f))
                              ->color({ 0.50f, 0.58f, 0.70f, 1.f })
                              ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                          HR::ESizeRule::Fixed, s(kLogCatW))
                ->addSlot(HNew(STextBlock)->text(field(&LogViewRow::text))->fontSize(s(12.f))
                              ->color(Attr<glm::vec4>([this, i] {
                                  return i < (int)logViewRows_.size() ? logViewRows_[i].color : kTextDim;
                              }))
                              ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                          HR::ESizeRule::Fill),
            SVerticalBox::ESizeRule::Fixed, rowH);
    }
    logViewDirty_ = true;
}

// 필터를 적용해 화면에 보일 N개를 고르고 행 문자열을 만듭니다. 위젯은 만들지 않습니다.
//
// 스크롤 위치는 화면 맨 아래 행의 seq(logAnchorSeq_) 하나입니다. 0 이면 꼬리를 따라갑니다.
// 줄 수가 아니라 seq 로 기억하므로, 고정 중에 새 로그가 들어오거나 필터가 바뀌어도 보던
// 자리가 밀리지 않습니다. 사본은 seq 오름차순이라 앵커 위치를 이분 탐색으로 찾습니다.
void EditorApp::refreshLogView() {
    pullLogRecords();

    std::string needle = logSearch_;
    for (char& c : needle) c = (char)std::tolower((unsigned char)c);
    auto shown      = [&](size_t i) { return logRecordVisible(logMirror_[i], needle); };
    auto firstAfter = [&](uint64_t seq) {   // seq 보다 뒤인 첫 레코드의 위치
        return (size_t)(std::upper_bound(logMirror_.begin(), logMirror_.end(), seq,
                                         [](uint64_t v, const HLogRecord& r) { return v < r.seq; })
                        - logMirror_.begin());
    };

    const size_t n   = logMirror_.size();
    const size_t lo  = firstAfter(logClearedSeq_);   // Clear 이전 레코드는 보이지 않습니다
    const int    cap = std::max(1, logRowCap_);

    // 화면 맨 아래 행 바로 다음 위치. 꼬리 추종이면 사본의 끝입니다. 앵커가 사본에서 밀려났으면
    // 가장 오래된 자리로 붙습니다.
    size_t end = logAnchorSeq_ ? std::max(lo, firstAfter(logAnchorSeq_)) : n;

    // 휠 이동은 보이는 행 단위로 셉니다. 필터에 걸러진 레코드는 건너뜁니다.
    for (int k = logScrollReq_; k > 0 && end > lo; ) { --end; if (shown(end)) --k; }     // 위(과거)로
    for (int k = -logScrollReq_; k > 0 && end < n; ++end) if (shown(end)) --k;         // 아래(최신)로
    logScrollReq_ = 0;

    // end 에서 위로 cap 개를 모읍니다. 맨 위에 닿아 모자라면 아래쪽에서 채워, 맨 위를 넘겨
    // 올라가 빈 줄이 생기지 않게 합니다.
    std::vector<size_t> picked;
    picked.reserve((size_t)cap);
    for (size_t i = end; i > lo && (int)picked.size() < cap; ) { --i; if (shown(i)) picked.push_back(i); }
    std::reverse(picked.begin(), picked.end());   // 위가 오래된 것, 아래가 최신
    for (; end < n && (int)picked.size() < cap; ++end) if (shown(end)) picked.push_back(end);

    // 화면 아래에 보이는 레코드가 남았으면 위치를 고정하고, 없으면 꼬리에 닿은 것이므로 추종으로
    // 돌아갑니다. "new lines" 는 고정을 시작한 뒤에 들어온 줄만 셉니다(원래 아래에 있던 줄 제외).
    if (!logAnchorSeq_) logPinSeq_ = logMirrorSeq_;
    size_t below = 0, fresh = 0;
    for (size_t i = n; i > end; ) {
        --i;
        if (!shown(i)) continue;
        ++below;
        if (logMirror_[i].seq > logPinSeq_) ++fresh;
    }
    if (below == 0 || picked.empty()) {
        logAnchorSeq_ = 0;
        logPinSeq_    = 0;
        logJumpText_.clear();
    } else {
        logAnchorSeq_ = logMirror_[picked.back()].seq;
        logJumpText_  = fresh == 0 ? std::string("v Latest")
                      : fresh == 1 ? std::string("v 1 new line")
                                   : "v " + std::to_string(fresh) + " new lines";
    }

    // 폰트가 ASCII 전용이고 클리핑이 없어, 폭을 넘는 문자열은 이웃 패널로 넘칩니다.
    // 카테고리와 본문 모두 칸 폭에 맞춰 자릅니다.
    const HGeometry& g   = logRowsBox_ ? logRowsBox_->cachedGeometry() : HGeometry{};
    const float      msgW = g.size.x - s(kLogTimeW) - s(8.f) - s(kLogCatW) - s(4.f);

    logViewRows_.resize(picked.size());
    for (size_t i = 0; i < picked.size(); ++i) {
        const HLogRecord& r   = logMirror_[picked[i]];
        LogViewRow&       row = logViewRows_[i];

        char tb[32];
        std::snprintf(tb, sizeof tb, "%.2f", (double)r.time);
        row.time = tb;

        const std::string& cat = r.category < logCatNames_.size() ? logCatNames_[r.category]
                                                                  : std::string("?");
        row.category = ellipsizeText(cat, s(kLogCatW) - s(4.f), s(11.f));

        std::string text;
        text.reserve(r.text.size() + 10);
        if (r.verbosity == ELogVerbosity::Error)   text += "Error: ";
        if (r.verbosity == ELogVerbosity::Warning) text += "Warning: ";
        text += r.text;
        text     = toDisplayAscii(std::move(text));
        row.text = (msgW > s(24.f)) ? ellipsizeText(text, msgW, s(12.f)) : std::string();
        row.color = logColor(r.verbosity);
    }
}

// Categories 팝업. 한 행에 두 장치를 나란히 둡니다: Show(표시 필터, 에디터)와
// Capture(기록 수준, 코어). 개수·수준 표시는 Attr 이라 로그가 들어와도 다시 만들지 않습니다.
void EditorApp::rebuildLogPopup() {
    if (!logPopupHost_) return;
    logPopupPanel_.reset();
    if (!logPopupOpen_ || !logCatButton_ || bottomTab_ != 1) {
        logPopupOpen_ = false;
        logPopupHost_->content(nullptr);
        return;
    }
    syncLogCategories();

    std::vector<HLogCategoryState*> cats = HLog::categories();
    std::sort(cats.begin(), cats.end(), [](const HLogCategoryState* a, const HLogCategoryState* b) {
        return a->name < b->name;
    });

    using HR = SHorizontalBox;
    using VR = SVerticalBox;
    const glm::vec4 btnN{ 0.16f, 0.16f, 0.19f, 1.f };
    const glm::vec4 btnH{ 0.26f, 0.26f, 0.31f, 1.f };
    const float rowH    = s(22.f);
    const float panelW  = s(330.f);
    const float headerH = s(28.f) + s(20.f);
    const float panelH  = headerH + rowH * (float)cats.size() + s(8.f);
    const HPadding vpad{ 0.f, s(2.f) };

    auto colText = [&](const char* label, EHAlign ha) {
        return HNew(STextBlock)->text(label)->fontSize(s(10.f))->color(kTextDim)
            ->hAlign(ha)->vAlign(EVAlign::Center);
    };

    auto list = HNew(VR);
    list->addSlot(HNew(HR)
        ->addSlot(spacer(), HR::ESizeRule::Fixed, s(6.f))
        // All 은 아직 등록되지 않아 목록에 없는 카테고리의 숨김까지 풉니다. 남겨 두면 "전부 보기"
        // 를 누른 뒤에 처음 쓰이는 카테고리가 숨은 채로 나타납니다.
        ->addSlot(textButton("All", 11.f, kTextBody, HPadding{8.f, 2.f}, btnN, btnH, [this] {
                      std::fill(logCatHidden_.begin(), logCatHidden_.end(), (char)0);
                      logHiddenNames_.clear();
                      logViewDirty_ = true;
                      saveLogFilter();
                  }), HR::ESizeRule::Auto, 0.f, HPadding{ 0.f, s(4.f) })
        ->addSlot(spacer(), HR::ESizeRule::Fixed, s(4.f))
        ->addSlot(textButton("None", 11.f, kTextBody, HPadding{8.f, 2.f}, btnN, btnH, [this] {
                      for (uint16_t i = 0; i < (uint16_t)logCatHidden_.size(); ++i) setLogCategoryHidden(i, true);
                      saveLogFilter();
                  }), HR::ESizeRule::Auto, 0.f, HPadding{ 0.f, s(4.f) })
        ->addSlot(spacer(), HR::ESizeRule::Fill)
        ->addSlot(textButton("Reset levels", 11.f, kTextBody, HPadding{8.f, 2.f}, btnN, btnH, [this] {
                      HLog::resetLevels();   // 적용 대기 중인 항목도 버립니다
                      saveLogLevels();
                  }), HR::ESizeRule::Auto, 0.f, HPadding{ 0.f, s(4.f) })
        ->addSlot(spacer(), HR::ESizeRule::Fixed, s(6.f)),
        VR::ESizeRule::Fixed, s(28.f));
    list->addSlot(HNew(HR)
        ->addSlot(spacer(), HR::ESizeRule::Fixed, s(8.f))
        ->addSlot(colText("Show", EHAlign::Left),     HR::ESizeRule::Fixed, s(44.f))
        ->addSlot(colText("Category", EHAlign::Left), HR::ESizeRule::Fill)
        ->addSlot(colText("Count", EHAlign::Right),   HR::ESizeRule::Fixed, s(50.f))
        ->addSlot(spacer(), HR::ESizeRule::Fixed, s(12.f))
        ->addSlot(colText("Capture", EHAlign::Left),  HR::ESizeRule::Fixed, s(80.f))
        ->addSlot(spacer(), HR::ESizeRule::Fixed, s(8.f)),
        VR::ESizeRule::Fixed, s(20.f));

    for (HLogCategoryState* st : cats) {
        const uint16_t idx = st->index;
        list->addSlot(HNew(HR)
            ->addSlot(spacer(), HR::ESizeRule::Fixed, s(8.f))
            ->addSlot(HNew(SToggleButton)
                          ->isActive([this, idx] { return !(idx < logCatHidden_.size() && logCatHidden_[idx]); })
                          ->onClick([this, idx] {
                              if (idx >= logCatHidden_.size()) return;
                              setLogCategoryHidden(idx, !logCatHidden_[idx]);
                              saveLogFilter();
                          })
                          ->normalColor(btnN)->hoveredColor(btnH)->activeColor({ 0.20f, 0.22f, 0.27f, 1.f })
                          ->padding(HPadding{ s(6.f), s(1.f) })
                          ->content(HNew(STextBlock)
                              ->text(AttrStr([this, idx]() -> std::string {
                                  return (idx < logCatHidden_.size() && logCatHidden_[idx]) ? "off" : "on";
                              }))
                              ->fontSize(s(10.f))->color(kTextBody)
                              ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center)),
                      HR::ESizeRule::Fixed, s(36.f), vpad)
            ->addSlot(spacer(), HR::ESizeRule::Fixed, s(8.f))
            ->addSlot(HNew(STextBlock)->text(toDisplayAscii(st->name))->fontSize(s(12.f))->color(kTextBody)
                          ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                      HR::ESizeRule::Fill)
            ->addSlot(HNew(STextBlock)
                          ->text(AttrStr([this, idx]() -> std::string {
                              return idx < logCatCounts_.size() ? std::to_string(logCatCounts_[idx]) : "0";
                          }))
                          ->fontSize(s(11.f))->color(kTextDim)
                          ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
                      HR::ESizeRule::Fixed, s(50.f))
            ->addSlot(spacer(), HR::ESizeRule::Fixed, s(12.f))
            // 기록 수준 순환: Off → Error → Warning → Log → Verbose → Off. 선언 기본값과 다르면
            // 강조색으로 표시합니다(키 입력 로그를 켜 둔 채 잊는 일을 막기 위해).
            ->addSlot(HNew(SButton)
                          ->normalColor(btnN)->hoveredColor(btnH)->pressedColor({ 0.12f, 0.12f, 0.15f, 1.f })
                          ->padding(HPadding{ s(6.f), s(1.f) })->hAlign(EHAlign::Left)
                          ->onClick([this, st] {
                              const uint8_t next = (uint8_t)((st->level.load(std::memory_order_relaxed) + 1) % 5);
                              st->level.store(next, std::memory_order_relaxed);
                              saveLogLevels();
                          })
                          ->content(HNew(STextBlock)
                              ->text(AttrStr([st]() -> std::string { return logVerbosityName(st->currentLevel()); }))
                              ->fontSize(s(11.f))
                              ->color(Attr<glm::vec4>([st] {
                                  return st->currentLevel() != st->defaultLevel
                                      ? glm::vec4{ 0.45f, 0.72f, 1.00f, 1.f } : kTextBody;
                              }))
                              ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                      HR::ESizeRule::Fixed, s(80.f), vpad)
            ->addSlot(spacer(), HR::ESizeRule::Fixed, s(8.f)),
            VR::ESizeRule::Fixed, rowH);
    }

    auto panel = HNew(SBox)->widthOverride(panelW)->heightOverride(panelH)->content(
        HNew(SBorder)
            ->background({ 0.055f, 0.055f, 0.07f, 1.f })
            ->borderColor({ 0.34f, 0.34f, 0.40f, 1.f })->borderThickness(1.f)
            ->padding(HPadding{ 0.f, s(4.f) })
            ->content(list));
    // 패널의 빈 곳(여백·열 제목)을 누른 클릭이 아래 패널(뷰포트 픽 등)로 새지 않게, 같은 자리에
    // 투명 버튼을 깔아 받아 둡니다. SButton 은 자식에게 클릭을 넘기지 않으므로 패널을 감싸지
    // 않고 형제로 뒤에 둡니다(오버레이는 앞 슬롯부터 판정).
    auto absorber = HNew(SBox)->widthOverride(panelW)->heightOverride(panelH)->content(
        HNew(SButton)->normalColor({ 0.f, 0.f, 0.f, 0.f })->hoveredColor({ 0.f, 0.f, 0.f, 0.f })
            ->pressedColor({ 0.f, 0.f, 0.f, 0.f })->content(spacer()));

    // 버튼이 화면 아래쪽 절반에 있으면 위로, 위쪽이면 아래로 엽니다. Output Log 는 도킹으로
    // 어디든 옮겨질 수 있습니다.
    const HGeometry& host = logPopupHost_->cachedGeometry();
    const HGeometry& btn  = logCatButton_->cachedGeometry();
    const bool  up   = (btn.position.y + btn.size.y * 0.5f) > (host.position.y + host.size.y * 0.5f);
    float       top  = up ? btn.position.y - panelH - s(2.f) : btn.position.y + btn.size.y + s(2.f);
    const float minT = host.position.y + s(kMenuBarH);
    top = std::clamp(top, minT, std::max(minT, host.position.y + host.size.y - panelH));
    float left = std::clamp(btn.position.x, host.position.x,
                            std::max(host.position.x, host.position.x + host.size.x - panelW));

    const HPadding at{ left - host.position.x, top - host.position.y, 0.f, 0.f };
    auto inner = HNew(SOverlay);
    inner->addSlot(absorber, EHAlign::Left, EVAlign::Top, at);
    inner->addSlot(panel,    EHAlign::Left, EVAlign::Top, at);
    logPopupPanel_ = panel;
    logPopupHost_->content(inner);
}

// ── rebuildBottomPanel ─────────────────────────────────────────────────────────
void EditorApp::rebuildBottomPanel() {
    if (!bottomContent_) return;
    bottomContent_->clearSlots();

    // Output Log 탭에만 있는 위젯의 참조를 놓습니다. 다른 탭으로 가면 팝업을 닫고 로그 검색
    // 포커스도 풉니다. 풀지 않으면 보이지 않는 검색창으로 타이핑이 계속 들어갑니다.
    logRowsBox_.reset();
    logSearchBox_.reset();
    logCatButton_.reset();
    if (bottomTab_ != 1) {
        if (logPopupOpen_) { logPopupOpen_ = false; logPopupDirty_ = true; }
        if (currentFocus() == EFocusTarget::LogSearch) setTextFocus(EFocusTarget::None);
    }

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
                                HLOG(LogEditor, Error) << "Spawn failed: " << ex.what();
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
                            // 선택한 액터에 지정합니다. 오디오 컴포넌트가 없으면 붙입니다. 예전에는
                            // 선택이 없거나 컴포넌트가 없으면 아무 반응 없이 무시됐습니다.
                            Actor* sel = ctx_.selected;
                            if (!sel || sel == editorCamActor_) {
                                HLOG(LogEditor, Warning) << "select an actor first to assign '" << stem << "'";
                                return;
                            }
                            auto* ac = sel->getComponent<AudioComponent>();
                            if (!ac) ac = sel->addComponent<AudioComponent>();
                            ac->setFile(fullPath);
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
        // Output Log. 필터 행 + 로그 행 풀. 필터 위젯의 콜백은 상태와 logViewDirty_ 만 바꾸고
        // 위젯을 비우지 않습니다(콜백 중 자기 컨테이너 파괴 금지).
        using HR = SHorizontalBox;
        const glm::vec4 btnN{ 0.16f, 0.16f, 0.19f, 1.f };
        const glm::vec4 btnH{ 0.26f, 0.26f, 0.31f, 1.f };

        logSearchBox_ = HNew(SSearchBox);
        logSearchBox_
            ->placeholder("Search log...")
            ->text(AttrStr([this]() -> std::string { return logSearch_; }))
            ->focused([this]() -> bool { return logSearchFocused_; })
            ->selectedAll([this]() -> bool { return logSearchSelAll_; })
            ->onClick([this] {
                setTextFocus(currentFocus() == EFocusTarget::LogSearch ? EFocusTarget::None
                                                                       : EFocusTarget::LogSearch);
            })
            ->fontSize(s(12.f));

        logCatButton_ = textButton("Categories", 11.f, kTextBody, HPadding{8.f, 3.f}, btnN, btnH,
                                   [this] { logPopupOpen_ = !logPopupOpen_; logPopupDirty_ = true; });

        // 심각도 토글. 켜진 상태는 SToggleButton 의 하단 막대로 보입니다.
        auto verbToggle = [&](const char* label, ELogVerbosity v) {
            const int vi = (int)v;
            glm::vec4 textCol = (v == ELogVerbosity::Log) ? kTextBody : logColor(v);
            if (v == ELogVerbosity::Verbose) textCol = { 0.55f, 0.55f, 0.60f, 1.f };
            return HNew(SToggleButton)
                ->isActive([this, vi] { return logShowVerb_[vi]; })
                ->onClick([this, vi] {
                    logShowVerb_[vi] = !logShowVerb_[vi];
                    logViewDirty_    = true;
                    saveLogFilter();
                })
                ->normalColor(btnN)->hoveredColor(btnH)->activeColor({ 0.20f, 0.22f, 0.27f, 1.f })
                ->padding(HPadding{ s(7.f), s(2.f) })
                ->content(HNew(STextBlock)->text(label)->fontSize(s(11.f))->color(textCol)
                    ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center));
        };
        // 부팅 이후 오류·경고 누계. 누르면 그 심각도만 보이게 합니다.
        auto counter = [&](bool errors) {
            const ELogVerbosity v = errors ? ELogVerbosity::Error : ELogVerbosity::Warning;
            return HNew(SButton)
                ->normalColor({ 0.f, 0.f, 0.f, 0.f })->hoveredColor(btnH)->pressedColor(btnN)
                ->padding(HPadding{ s(5.f), s(2.f) })
                ->onClick([this, v] {
                    for (bool& on : logShowVerb_) on = false;
                    logShowVerb_[(int)v] = true;
                    logViewDirty_ = true;
                    saveLogFilter();
                })
                ->content(HNew(STextBlock)
                    ->text(AttrStr([this, errors]() -> std::string { return errors ? logErrText_ : logWarnText_; }))
                    ->fontSize(s(11.f))
                    ->color(Attr<glm::vec4>([this, errors, v] {
                        return (errors ? logHasErr_ : logHasWarn_) ? logColor(v) : kTextDim;
                    })));
        };
        const HPadding vpad{ 0.f, s(3.f) };
        auto filterRow = HNew(HR)
            ->addSlot(spacer(),      HR::ESizeRule::Fixed, s(4.f))
            ->addSlot(logSearchBox_, HR::ESizeRule::Fixed, s(200.f), vpad)
            ->addSlot(spacer(),      HR::ESizeRule::Fixed, s(6.f))
            ->addSlot(logCatButton_, HR::ESizeRule::Auto, 0.f, vpad)
            ->addSlot(spacer(),      HR::ESizeRule::Fixed, s(10.f))
            ->addSlot(verbToggle("Err",  ELogVerbosity::Error),   HR::ESizeRule::Auto, 0.f, vpad)
            ->addSlot(verbToggle("Warn", ELogVerbosity::Warning), HR::ESizeRule::Auto, 0.f, vpad)
            ->addSlot(verbToggle("Log",  ELogVerbosity::Log),     HR::ESizeRule::Auto, 0.f, vpad)
            ->addSlot(verbToggle("Verb", ELogVerbosity::Verbose), HR::ESizeRule::Auto, 0.f, vpad)
            ->addSlot(spacer(),      HR::ESizeRule::Fill)
            ->addSlot(counter(true),  HR::ESizeRule::Auto, 0.f, vpad)
            ->addSlot(counter(false), HR::ESizeRule::Auto, 0.f, vpad)
            ->addSlot(spacer(),      HR::ESizeRule::Fixed, s(6.f))
            ->addSlot(textButton("Clear", 11.f, kTextBody, HPadding{8.f, 3.f}, btnN, btnH,
                                 [this] {
                                     // 코어 링은 비우지 않습니다. 파일·상태바에는 영향이 없습니다.
                                     pullLogRecords();
                                     logClearedSeq_ = logMirrorSeq_;
                                     logAnchorSeq_  = 0;   // 비운 뒤에는 새 로그를 따라갑니다
                                     logScrollReq_  = 0;
                                     logViewDirty_  = true;
                                 }),
                      HR::ESizeRule::Auto, 0.f, vpad)
            ->addSlot(spacer(),      HR::ESizeRule::Fixed, s(4.f));

        logRowsBox_ = HNew(SVerticalBox);
        // 스크롤을 고정한 동안만 보이는 "v N new lines" 버튼. 누르면 꼬리 추종으로 돌아갑니다.
        // 위젯은 항상 두고 가시성만 Attr 로 바꿉니다. 고정 상태는 휠과 새 로그에 따라 수시로
        // 바뀌는데, 그때마다 위젯을 다시 만들면 행 풀이 없앤 리빌드(DEF-70)가 되살아납니다.
        auto jumpButton = HNew(SButton)
            ->normalColor({ 0.10f, 0.30f, 0.50f, 1.f })->hoveredColor(kAccent)
            ->pressedColor({ 0.06f, 0.22f, 0.38f, 1.f })
            ->padding(HPadding{ s(8.f), s(2.f) })
            ->onClick([this] {
                logAnchorSeq_ = 0;
                logScrollReq_ = 0;
                logViewDirty_ = true;
            })
            ->content(HNew(STextBlock)
                ->text(AttrStr([this]() -> std::string { return logJumpText_; }))
                ->fontSize(s(11.f))->color(kTextHdr)
                ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center));
        jumpButton->visibility.bind([this] {
            return logAnchorSeq_ != 0 ? EVisibility::Visible : EVisibility::Collapsed;
        });
        auto rowsArea = HNew(SOverlay);
        rowsArea->addSlot(logRowsBox_);
        rowsArea->addSlot(jumpButton, EHAlign::Right, EVAlign::Bottom, HPadding{ 0.f, 0.f, s(10.f), s(4.f) });

        bottomContent_->addSlot(HNew(SBorder)->background(kBgHeader)->content(filterRow),
                                SVerticalBox::ESizeRule::Fixed, s(28.f));
        // 패딩은 슬롯이 아니라 감싼 SBorder 에 줍니다. Fill 슬롯의 패딩은 이중 계산됩니다(DEF-62).
        bottomContent_->addSlot(HNew(SBorder)->background({ 0.f, 0.f, 0.f, 0.f })
                                    ->padding(HPadding{ s(4.f), s(2.f) })->content(rowsArea),
                                SVerticalBox::ESizeRule::Fill);
        rebuildLogRows();
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
        setTextFocus(EFocusTarget::Search);
        return;
    }

    // 텍스트 입력 중에는 씬 undo/redo가 직관에 어긋나므로 차단
    if (currentFocus() != EFocusTarget::None) return;   // 텍스트 입력 중에는 단축키를 막습니다

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
    if (leftDown && !prevLeftDown_ && logPopupOpen_) {
        const glm::vec2 mp{ mouseX, mouseY };
        const bool inPanel  = logPopupPanel_ && logPopupPanel_->cachedGeometry().contains(mp);
        const bool onButton = logCatButton_  && logCatButton_->cachedGeometry().contains(mp);
        if (!inPanel && !onButton) { logPopupOpen_ = false; logPopupDirty_ = true; }
    }

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
        if (g.size.x >= 1.f && g.size.y >= 1.f) {
            lastValidVpRect_ = { std::floor(g.position.x), std::floor(g.position.y),
                                 std::floor(g.size.x),     std::floor(g.size.y) };
            return lastValidVpRect_;
        }
    }
    // 위젯이 아직 배치되지 않은 프레임(첫 프레임, uiScale 변경 직후의 재조립)입니다.
    // 고정 치수로 되돌아가면 크롬 줄 수가 바뀔 때마다 어긋나므로, 직전 유효 rect 를
    // 그대로 돌려줍니다. 그러면 setViewportRect 가 값 비교에서 early-return 해
    // 렌더 타깃 재생성도 일어나지 않습니다.
    if (lastValidVpRect_.width >= 1.f && lastValidVpRect_.height >= 1.f)
        return lastValidVpRect_;
    // 진짜 첫 프레임. 렌더러도 이때는 renderExtent == 스왑체인 extent 로 시작하므로
    // 프레임버퍼 전체가 실제 렌더 결과와 일치합니다.
    return fullFbRect();
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
    // Output Log 의 Categories 팝업은 뷰포트 위로 열립니다. 팝업 안의 클릭이 기즈모 드래그나
    // RMB 카메라 모드로 새지 않게 툴바와 같은 방식으로 배제합니다(기즈모는 UI 디스패치보다 먼저 판정).
    if (logPopupOpen_ && logPopupPanel_) {
        const HGeometry& pg = logPopupPanel_->cachedGeometry();
        const HViewportRect pr{ pg.position.x, pg.position.y, pg.size.x, pg.size.y };
        if (pr.width >= 1.f && pr.contains(fb.x, fb.y)) return false;
    }
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

    // 키 엣지를 여기서 **한 번에** 계산하고 prev 상태를 조건 없이 갱신합니다. 예전에는
    // 숫자 필드 분기가 중간에서 return 해 F2·Delete 엣지가 갱신되지 않았고, 그 탓에 값을
    // 확정한 직후 눌린 채였던 Delete 가 선택 액터를 지웠습니다(undo 없음).
    const bool ctrlHeld = win.isKeyDown(HInput::KEY_LEFT_CTRL);
    const bool vNow   = win.isKeyDown(HInput::KEY_V);
    const bool cNow   = win.isKeyDown(HInput::KEY_C);
    const bool aNow   = win.isKeyDown(HInput::KEY_A);
    const bool f2Now  = win.isKeyDown(HInput::KEY_F2);
    const bool delNow = win.isKeyDown(HInput::KEY_DELETE);
    const bool enNow  = win.isKeyDown(HInput::KEY_ENTER);
    const bool bsNow  = win.isKeyDown(HInput::KEY_BACKSPACE);
    const bool escNow = win.isKeyDown(HInput::KEY_ESCAPE);

    const bool pasteEdge  = ctrlHeld && vNow && !prevVDown_;
    const bool copyEdge   = ctrlHeld && cNow && !prevCDown_;
    const bool selAllEdge = ctrlHeld && aNow && !prevADown_;
    const bool f2Edge     = f2Now  && !prevF2Down_;
    const bool delEdge    = delNow && !prevDeleteDown_;
    const bool enterEdge  = enNow  && !prevEnterDown_;
    const bool bsEdge     = bsNow  && !prevBackspaceDown_;
    const bool escEdge    = escNow && !prevEscapeDown_;

    prevVDown_         = vNow;
    prevCDown_         = cNow;
    prevADown_         = aNow;
    prevF2Down_        = f2Now;
    prevDeleteDown_    = delNow;
    prevEnterDown_     = enNow;
    prevBackspaceDown_ = bsNow;
    prevEscapeDown_    = escNow;

    const EFocusTarget focus = currentFocus();

    // Esc — 무엇을 편집하고 있든 취소하고 빠져나옵니다(숫자 필드는 값을 확정).
    if (escEdge && focus != EFocusTarget::None) {
        setTextFocus(EFocusTarget::None);
        return;
    }

    // F2 — 리네임 진입·종료. 다른 텍스트 편집 중에는 반응하지 않습니다.
    if (f2Edge && (focus == EFocusTarget::None || focus == EFocusTarget::Rename)) {
        setTextFocus(focus == EFocusTarget::Rename ? EFocusTarget::None
                                                   : EFocusTarget::Rename);
        return;
    }

    // ── 숫자 필드 인라인 편집 — 입력 모드 중에는 모든 문자가 필드로만 갑니다 ──
    if (focus == EFocusTarget::NumberField) {
        auto* nf = SNumberField::focusedField();
        std::vector<unsigned int> chars;
        win.drainCharQueue(chars);
        for (unsigned int cp : chars) nf->onChar(cp);

        if (selAllEdge) nf->selectAll();
        if (pasteEdge)
            for (char c : win.getClipboardText()) nf->onChar((unsigned char)c);
        if (copyEdge)  win.setClipboardText(nf->editBuffer());
        if (bsEdge)    nf->onBackspace();
        if (enterEdge) nf->commitEdit();
        return;
    }

    // ── 인라인 리네임 / 아웃라이너 검색 / 로그 검색 — 같은 편집 규칙을 공유합니다 ─
    if (focus == EFocusTarget::Rename || focus == EFocusTarget::Search ||
        focus == EFocusTarget::LogSearch) {
        const bool isRename = (focus == EFocusTarget::Rename);
        const bool isLog    = (focus == EFocusTarget::LogSearch);
        std::string& buf    = isRename ? renameBuffer_ : isLog ? logSearch_       : outlinerSearch_;
        bool&        selAll = isRename ? renameSelAll_ : isLog ? logSearchSelAll_ : searchSelAll_;
        bool dirty = false;

        // 전체 선택 상태에서 입력·붙여넣기·지우기 → 기존 내용 교체
        auto clearIfSelAll = [&] {
            if (selAll) { buf.clear(); selAll = false; dirty = true; }
        };

        if (selAllEdge && !buf.empty()) { selAll = true; dirty = true; }

        std::vector<unsigned int> chars;
        win.drainCharQueue(chars);
        for (unsigned int cp : chars) {
            // 폰트 아틀라스가 ASCII 전용이라, 그릴 수 없는 글자는 애초에 받지 않습니다.
            if (cp >= 32 && cp < 127) {
                clearIfSelAll();
                buf += (char)cp;
                dirty = true;
            }
        }

        if (pasteEdge) {
            clearIfSelAll();
            for (char c : win.getClipboardText())
                if (c >= 32 && c < 127) { buf += c; dirty = true; }
        }
        if (copyEdge) win.setClipboardText(buf);

        if (bsEdge && !buf.empty()) {
            if (selAll) clearIfSelAll();
            else        buf.pop_back();
            dirty = true;
        }

        if (enterEdge) {
            // 빈 이름은 적용하지 않되 편집은 끝냅니다. 예전에는 버퍼가 비면 Enter 가
            // 아무 일도 하지 않아 확정도 탈출도 못 하는 상태에 갇혔습니다.
            if (isRename && !buf.empty() && ctx_.selected)
                ctx_.selected->name = buf;
            setTextFocus(EFocusTarget::None);
            return;
        }

        if (dirty) (isLog ? logViewDirty_ : outlinerDirty_) = true;
        return;
    }

    // ── 포커스 없음 ───────────────────────────────────────────────────────────
    if (delEdge && ctx_.selected && ctx_.selected != editorCamActor_)
        deleteSelectedActor();

    // 아무도 받지 않는 문자는 버립니다. 쌓아 두면 다음에 입력창을 여는 순간
    // 그동안 누른 글자가 한꺼번에 쏟아집니다.
    std::vector<unsigned int> junk;
    win.drainCharQueue(junk);
}

// ── tickEditorCamera ───────────────────────────────────────────────────────────
void EditorApp::tickEditorCamera(float dt, float scroll) {
    if (!editorCam_ || !editorCamActor_ || !ctx_.window) return;
    IWindow& win = *ctx_.window;

    const glm::vec3 fwd = editorCam_->forward();
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
