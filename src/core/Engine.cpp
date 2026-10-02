#include "Engine.hpp"

#include "../renderer/Renderer.hpp"
#include "../editor/EditorApp.hpp"
#include "../assets/AssetManager.hpp"
#include "../audio/AudioSystem.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../game/DefaultGameModule.hpp"
#include "../input/HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include "IGameModule.hpp"
#include "FrameTimings.hpp"

#ifndef ANDROID
#include "../platform/glfw/GlfwWindow.hpp"
#endif

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <string>

// ── Bootstrap ─────────────────────────────────────────────────────────────────
// 코어 서비스는 초기화 순서가 고정입니다: 창 → 렌더러(창 필요) → 에셋(렌더러 필요).
// 그다음 동적 모듈을 등록하고, 마지막에 에디터·게임을 올립니다.
Engine::Engine() {
    auto win = std::make_unique<GlfwWindow>(1600, 900, "Haruka Engine");
    win->init();
    window_ = std::move(win);

    renderer_ = std::make_unique<Renderer>();
    renderer_->init(window_.get());

    assets_ = std::make_unique<AssetManager>(renderer_.get());

    // 동적 모듈: 표준 세트(Audio, Physics). 각자 월드를 참조합니다.
    auto* audio   = modules_.registerModule(std::make_unique<AudioSystem>());
    audio->setWorld(&world_);
    auto* physics = modules_.registerModule(std::make_unique<PhysicsSystem>());
    physics->setWorld(&world_);

    // 에디터 레이어.
    editor_ = std::make_unique<EditorApp>();
    editor_->init(*renderer_, world_, *assets_, *window_);

    setupInputBindings();

    // 게임 모듈(씬 콘텐츠) + 프레임 컨텍스트.
    ctx_.reset(new EngineContext{ world_, *assets_, *window_, modules_ });
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
    if (renderer_) renderer_->cleanup();
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
            editor.tick(dt);
            HInputSystem::get().update(dt, win);
            if (editor.isPlaying()) {
                world.tick(dt);
                gameModule.onTick(dt, ctx);
                modules.tickAll(dt);
            }
        }

        // ── Editor input (단축키는 EditorApp::tickShortcuts에서 처리) ──────
        {
            ScopedCpuTimer t(gFrameTimings().handleInput);
            editor.handleInput(mx, my, lmb, lmbJustUp, 0.f,
                               (float)winW, (float)winH);
        }

        // ── Render ─────────────────────────────────────────────────────────
        renderer.drawFrame(world);

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
    }
}
