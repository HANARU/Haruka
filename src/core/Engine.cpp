#include "Engine.hpp"

#include "../renderer/Renderer.hpp"
#include "../editor/EditorApp.hpp"
#include "../assets/AssetManager.hpp"
#include "../audio/AudioSystem.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../game/DefaultGameModule.hpp"
#include "../input/HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include "../xr/XrSystem.hpp"
#include "HLog.hpp"
#include "IGameModule.hpp"
#include "ComponentRegistry.hpp"
#include "EngineSettings.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../components/CoreComponents.hpp"
#include "../components/CameraComponent.hpp"
#include "FrameTimings.hpp"

#ifndef ANDROID
#include "../platform/glfw/GlfwWindow.hpp"
#endif

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

// 상한 설정을 읽습니다(DEF-10). 파일이 없으면 기본값으로 만들어 두어 찾아서 고칠 수 있게 합니다.
static EngineLimits loadEngineLimits(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        out << engine_settings::defaultText();
        HLOG(LogEngine, Log) << "Created " << path << " with the default limits";
        return EngineLimits{};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::vector<std::string> messages;
    const EngineLimits l = engine_settings::parse(ss.str(), messages);
    for (const auto& m : messages) HLOG(LogEngine, Warning) << path << ": " << m;
    return l;
}

// ── Bootstrap ─────────────────────────────────────────────────────────────────
// 코어 서비스는 초기화 순서가 고정입니다: 창 → 렌더러(창 필요) → 에셋(렌더러 필요).
// 그다음 동적 모듈을 등록하고, 마지막에 에디터·게임을 올립니다.
Engine::Engine(const EngineOptions& options) {
    // 기본 컴포넌트 타입을 씬을 읽기 전에 등록합니다(DEF-22). 씬 · 프리팹의 COMPONENT 블록과 에디터의
    // 컴포넌트 추가 버튼이 이 등록을 씁니다.
    registerCoreComponents();

    // 그리기 행 · 라이트 · 물리 바디 상한(DEF-10). 렌더러 · 물리를 만들기 전에 읽습니다.
    const EngineLimits limits = loadEngineLimits("Config/engine.ini");
    HLOG(LogEngine, Log) << "Limits: draw rows " << limits.maxDrawRows << ", lights " << limits.maxLights
                         << ", physics bodies " << limits.maxBodies << " (Config/engine.ini)";

    auto win = std::make_unique<GlfwWindow>(1600, 900, "Haruka Engine");
    win->init();
    window_ = std::move(win);

    // XR 은 렌더러보다 먼저입니다. OpenXR 이 헤드셋이 연결된 GPU 를 고르고 Vulkan 인스턴스 · 장치를
    // 만들기 때문입니다(UE · Unity · Godot 같음). 런타임이나 헤드셋이 없으면 데스크톱으로 계속합니다.
    if (options.xr) {
        xr_ = std::make_unique<XrSystem>();
        if (!xr_->init()) xr_.reset();
    }

    renderer_ = std::make_unique<Renderer>();
    renderer_->setLimits(limits.maxDrawRows, limits.maxLights);
    renderer_->init(window_.get(), xr_.get());
    if (xr_) {
        if (renderer_->xrCompatible() &&
            xr_->createSession(renderer_->getInstance(), renderer_->getPhysDevice(), renderer_->getDevice(),
                               renderer_->graphicsQueueFamily(), renderer_->getSwapFormat())) {
            renderer_->attachXrSwapchain(xr_->swapchainImages(), xr_->swapchainFormat(), xr_->eyeExtent());
            HLOG(LogEngine, Log) << "XR ready; the session starts when the runtime reports READY";
        } else {
            // 인스턴스는 남겨 둡니다. 렌더러의 Vulkan 인스턴스 · 장치를 그것으로 만들었을 수 있어서,
            // 엔진이 끝날 때 렌더러 다음에 지웁니다.
            HLOG(LogEngine, Warning) << "XR session could not be created; continuing in desktop mode";
            xr_->destroySession();
        }
    }
    if (options.emulateStereo) {
        if (xr_ && xr_->hasSession()) HLOG(LogEngine, Warning) << "--emulate-stereo is ignored while XR is active";
        else                          renderer_->setStereoEmulation(true);
    }

    assets_ = std::make_unique<AssetManager>(renderer_.get());

    // 동적 모듈: 표준 세트(Audio, Physics). 각자 월드를 참조합니다.
    auto* audio   = modules_.registerModule(std::make_unique<AudioSystem>());
    audio->setWorld(&world_);
    audio_ = audio;
    auto physicsModule = std::make_unique<PhysicsSystem>();
    physicsModule->setMaxBodies(limits.maxBodies);   // startup 이 등록 안에서 일어나므로 그 전에
    auto* physics = modules_.registerModule(std::move(physicsModule));
    physics->setWorld(&world_);
    physics_ = physics;

    // 에디터 레이어.
    editor_ = std::make_unique<EditorApp>();
    editor_->init(*renderer_, world_, *assets_, *window_);

    setupInputBindings();

    // 게임 모듈(씬 콘텐츠) + 프레임 컨텍스트.
    ctx_.reset(new EngineContext{ world_, *assets_, *window_, modules_, *renderer_,
                                  ComponentRegistry::get() });
    game_ = std::make_unique<DefaultGameModule>();
    game_->onStart(*ctx_);
}

// ── Teardown (부팅의 역순) ─────────────────────────────────────────────────────
// 완전히 구성된 Engine 만 여기 도달합니다. 부팅 중 예외로 미완성이면 이미 생성된
// 멤버의 소멸자만 돌아가며, 그때의 안전성은 예전 main() 의 스택 파괴와 동일합니다.
Engine::~Engine() {
    if (game_)     game_->onShutdown();
    modules_.shutdownAll();
    if (assets_)   assets_->clear();
    if (editor_)   editor_->cleanup();
    // XR 세션은 그것이 쓰는 Vulkan 장치보다 먼저 지웁니다.
    if (xr_ && renderer_) renderer_->detachXrSwapchain(false);
    if (xr_)       xr_->destroySession();
    if (renderer_) renderer_->cleanup();
    if (xr_)       xr_->shutdown();
}

void Engine::endXr() {
    if (!xr_ || !xr_->hasSession()) return;
    renderer_->detachXrSwapchain();   // 장치가 쉬기를 기다린 뒤 이미지 뷰를 지움
    xr_->destroySession();
    HInputSystem::get().setXrInput({});
    if (physics_) physics_->setStepRate(60.f);
    HLOG(LogEngine, Log) << "XR ended; continuing in desktop mode";
}

// ── 입력 바인딩 (프로젝트 설정 레이어, 게임 코드와 분리) ────────────────────────
void Engine::setupInputBindings() {
    // 에디터 카메라는 EditorApp 이 raw input 으로 직접 처리합니다.
    // 런타임(Quest) 액션이 필요해지면 여기서 선언합니다.
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void Engine::run() {
    // 루프 본문을 예전 main() 과 동일하게 유지하기 위해 로컬 참조로 바인딩합니다.
    IWindow&       win        = *window_;
    Renderer&      renderer   = *renderer_;
    EditorApp&     editor     = *editor_;
    IGameModule&   gameModule = *game_;
    EngineContext& ctx        = *ctx_;
    ModuleManager& modules    = modules_;
    World&         world      = world_;

    using Clock = std::chrono::high_resolution_clock;
    auto prev       = Clock::now();
    auto fpsTimer   = prev;
    int  fpsFrames  = 0;
    bool prevLDown  = false;
    bool prevRDown  = false;
    bool prevInGame = false;
    bool prevPaused = false;

    while (!win.shouldClose()) {
        const auto loopT0 = Clock::now();
        {
            ScopedCpuTimer t(gFrameTimings().pollEvents);
            win.pollEvents();
        }
        // 창이 최소화되면(프레임버퍼 0x0) 에디터 UI 는 쉬고 시뮬레이션 · 소리 · XR 은 계속 돕니다. 예전에는
        // 스왑체인을 다시 만들며 복원될 때까지 루프 전체가 멈춰, 최소화한 채 창을 닫으면 엔진이 끝나지 않았습니다
        // (DEF-106). 에디터의 배치 계산은 0 크기를 다루지 않으므로 부르지 않습니다.
        int fbW = 0, fbH = 0;
        win.getFramebufferSize(fbW, fbH);
        const bool minimized = (fbW <= 0 || fbH <= 0);

        // ── XR 프레임 시작 ─────────────────────────────────────────────────
        // 세션이 돌면 xrWaitFrame 이 헤드셋 주사율에 맞춰 기다리므로 루프 속도를 헤드셋이 정합니다.
        // 입력은 이 프레임이 보일 예상 시각의 자세로 읽어 틱에 넘깁니다.
        XrSystem::FrameInfo xrFrame;
        bool xrBegun = false;
        if (xr_ && xr_->hasSession()) {
            xr_->pollEvents();
            // 입력은 매 프레임 새로 넣습니다. 세션이 멈추면(헤드셋을 벗음 등) 빈 상태가 들어가, 마지막
            // 버튼 · 스틱 값이 눌린 채 남지 않습니다.
            HXrInputState xin;
            if (xr_->sessionLost()) {
                endXr();
            } else if (xr_->sessionRunning()) {
                xrBegun = xr_->beginFrame(xrFrame);
                if (xrBegun) xr_->syncInput(xrFrame.displayTime, xin);
                // 물리 스텝을 헤드셋 주사율에 맞춥니다(1/60 이면 72 · 90Hz 에서 물체가 떨려 보임).
                const float hz = xr_->displayRefreshRate();
                if (physics_ && hz > 1.f && std::abs(physics_->stepRate() - hz) > 0.5f) {
                    physics_->setStepRate(hz);
                    HLOG(LogEngine, Log) << "Physics step matched to the headset: " << hz << " Hz";
                }
            }
            HInputSystem::get().setXrInput(xin);
        }
        // 소리 청취자: XR 세션이 입력을 받는 동안에는 헤드셋 자세를 따릅니다. 카메라는 XR 원점(위치 + 요)일
        // 뿐이라, 넘기지 않으면 고개를 돌려도 소리의 좌우가 바뀌지 않습니다. 손 자세와 같은 기준(원점 × 자세)입니다.
        if (audio_) {
            const HXrInputState& xr  = HInputSystem::get().xr();
            CameraComponent*     cam = world_.getActiveCamera();
            if (xr.focused && xr.head.valid && cam)
                audio_->setListenerPose(cam->xrOrigin() * glm::translate(glm::mat4(1.f), xr.head.position)
                                        * glm::mat4_cast(xr.head.orientation));
            else
                audio_->clearListenerPose();
        }

        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - prev).count();
        prev      = now;
        dt        = std::min(dt, 0.05f);

        int winW, winH;
        win.getWindowSize(winW, winH);

        float mx, my;
        win.getMousePosition(mx, my);

        auto inViewport = [&](float x, float y) {
            return editor.inViewport(x, y, (float)winW, (float)winH);
        };

        // LMB — viewport hit-test (pick / orbit gated to viewport)
        bool lmb      = win.isMouseButtonDown(HInput::MOUSE_BUTTON_LEFT);
        bool lmbJustUp= !lmb && prevLDown;
        prevLDown     = lmb;

        // RMB → camera mode
        bool rmb = win.isMouseButtonDown(HInput::MOUSE_BUTTON_RIGHT);
        if (rmb && !prevRDown && inViewport(mx, my)) win.activateCameraMode();
        if (!rmb && prevRDown) win.deactivateCameraMode();
        prevRDown = rmb;

        // ── Play state transitions ─────────────────────────────────────────
        const bool inGame = editor.isPlaying() || editor.isPaused();
        const bool paused = editor.isPaused();
        if (inGame && !prevInGame) {
            gameModule.onBeginPlay(ctx);
            modules.beginPlayAll();
        }
        if (!inGame && prevInGame) {
            gameModule.onEndPlay();
            modules.endPlayAll();
        }
        // 일시정지는 시작·끝 처리 뒤에 알립니다. Pause 상태에서 곧바로 Stop 하면 끝 처리가
        // 먼저 사운드를 멈추므로, 뒤따르는 재개 통지가 남은 소리를 다시 들려주지 않습니다.
        if (paused != prevPaused) modules.pauseChangedAll(paused);
        prevInGame = inGame;
        prevPaused = paused;

        // ── Tick ──────────────────────────────────────────────────────────
        {
            ScopedCpuTimer t(gFrameTimings().tick);
            if (!minimized) editor.tick(dt);
            HInputSystem::get().update(dt, win, editor.isTextInputActive());
            if (editor.isPlaying()) {
                world.tick(dt);
                gameModule.onTick(dt, ctx);
                modules.tickAll(dt);
            }
        }

        // ── Editor input (단축키는 EditorApp::tickShortcuts에서 처리) ──────
        if (!minimized) {
            ScopedCpuTimer t(gFrameTimings().handleInput);
            editor.handleInput(mx, my, lmb, lmbJustUp, 0.f,
                               (float)winW, (float)winH);
        }

        // ── Render ─────────────────────────────────────────────────────────
        // XR: 눈 자세를 이 프레임에 한 번 받아 컬링 · 렌더 · 제출에 함께 씁니다(DEF-32). 헤드셋이 화면을
        // 보여 주지 않거나(벗음 등) 추적을 잃으면 그 프레임은 데스크톱만 그리고 빈 프레임을 제출합니다.
        if (xrBegun) {
            XrFrameTarget target;
            bool acquired = false;
            if (xrFrame.shouldRender && xr_->locateViews(xrFrame.displayTime, target.eyes))
                acquired = xr_->acquireImage(target.imageIndex);
            const bool rendered = renderer.drawFrame(world, acquired ? &target : nullptr);
            if (acquired) xr_->releaseImage();
            xr_->endFrame(xrFrame.displayTime, acquired && rendered);
        } else {
            renderer.drawFrame(world);
        }

        // FPS title (every 0.5 s)
        ++fpsFrames;
        auto  fpsNow = Clock::now();
        float fpsDt  = std::chrono::duration<float>(fpsNow - fpsTimer).count();
        if (fpsDt >= 0.5f) {
            int fps = (int)(fpsFrames / fpsDt);
            win.setTitle("Haruka Engine  |  " + std::to_string(fps) + " FPS");
            fpsTimer  = fpsNow;
            fpsFrames = 0;
        }

        // 루프 1 회 전체입니다. 이것이 실제 프레임 간격이며, 위 구간들의 합과
        // 크게 벌어지면 그 차이가 곧 "어느 계측기에도 잡히지 않는 시간"입니다.
        gFrameTimings().loopTotal.add(std::chrono::duration<float, std::milli>(
                                          Clock::now() - loopT0).count());

        // 최소화 중에는 그리지 않아 루프가 코어 하나를 다 쓰므로 쉬어 갑니다(약 60Hz, dt 는 실제 시간이라
        // 시뮬레이션 속도는 그대로). 헤드셋이 돌면 xrWaitFrame 이 속도를 정하므로 쉬지 않습니다.
        if (minimized && !xrBegun)
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}
