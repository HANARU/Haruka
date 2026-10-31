#include "Engine.hpp"

#include "../renderer/Renderer.hpp"
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
#include "ProjectPaths.hpp"
#include "FileUtil.hpp"
#include "AppPaths.hpp"
#include "DebugDraw.hpp"
#include "../assets/GassetCache.hpp"
#include "../vfs/Vfs.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../components/CoreComponents.hpp"
#include "../components/XrComponents.hpp"
#include "../components/CameraComponent.hpp"
#include "../xr/SyntheticEyes.hpp"
#include "../vfs/PathUtf8.hpp"
#include "ShaderManifest.hpp"
#include "FrameTimings.hpp"
#include "stb_image_write.h"   // 구현은 GltfLoader.cpp(STB_IMAGE_WRITE_IMPLEMENTATION)

#ifdef ANDROID
#include "../platform/android/AndroidWindow.hpp"
#else
#include "../platform/glfw/GlfwWindow.hpp"
#include "../platform/headless/HeadlessWindow.hpp"
#endif

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <thread>

// MSVC 의 "예외 없는 STL"(_HAS_EXCEPTIONS=0)에서는 std::runtime_error 가 메시지를 복사하지 않아, 문자열로 만든 FATAL 문구가
// 깨집니다(DEF-157, 예전에는 Jolt 의 PUBLIC 정의로 새어 들어왔음). 그런 구성으로 다시 빌드되지 않게 막습니다.
#if defined(_MSC_VER) && defined(_HAS_EXCEPTIONS) && !_HAS_EXCEPTIONS
#error "The engine needs _HAS_EXCEPTIONS=1 (see CPP_EXCEPTIONS_ENABLED for Jolt in CMakeLists.txt, DEF-157)"
#endif

// 상한 설정을 읽습니다(DEF-10). 두 겹입니다(2026-10-19): 실행 폴더의 파일(패키지 기본값) 위에 쓰기 루트의 파일(사용자 값)을
// 덮습니다(UE 의 Default · User ini, Godot 의 override.cfg 와 같은 구조). 에디터는 두 자리가 같은 실행 폴더라 한 파일이고,
// 없으면 기본값으로 만들어 두어 찾아서 고칠 수 있게 합니다(예전과 같음). 게임 실행 파일은 파일을 만들지 않습니다.
static EngineLimits loadEngineLimits(const char* rel) {
    EngineLimits l;
    auto applyFile = [&](const std::filesystem::path& p) {
        std::string text;
        if (!hfile::readAll(p, text)) return false;
        std::vector<std::string> messages;
        l = engine_settings::parse(text, messages, l);
        for (const auto& m : messages) HLOG(LogEngine, Warning) << p.generic_string() << ": " << m;
        return true;
    };
    const std::filesystem::path base = happ::inExeDir(rel);
    const bool haveBase = applyFile(base);
    if (!happ::writeRootIsExeDir()) {
        applyFile(happ::inWriteRoot(rel));
    } else if (!haveBase) {
        std::error_code ec;
        std::filesystem::create_directories(base.parent_path(), ec);
        std::ofstream out(base, std::ios::binary);
        out << engine_settings::defaultText();
        HLOG(LogEngine, Log) << "Created " << rel << " with the default limits";
    }
    return l;
}

// 실제로 읽힐 셰이더(묶음 · 낱개 중 우선순위가 이기는 쪽)가 이 실행 파일을 빌드할 때 컴파일한 것과 같은지 봅니다(DEF-149).
// 다르면 셰이더의 바인딩 · 푸시 상수가 C++ 쪽 파이프라인 레이아웃과 어긋날 수 있고, 기기에는 검증 레이어가 없어 결과가
// 정의되지 않으므로 멈춥니다. 흔한 원인은 Android 에서 adb install -r 뒤에도 남는 외부 파일 폴더의 옛 game.hpak 입니다.
// --loose 는 묶음을 다시 만들지 않고 바꿔 보는 개발용이라 경고만 합니다. 안내 문구는 묶음을 마운트했는지에 따라 다릅니다
// (2026-11-01 독립 검토): 묶음이 없는 데스크톱 개발 실행에서 어긋나면 셰이더만 다시 빌드한 경우이고(Shaders 대상만 빌드하는 핫
// 리로드), Android 에서 묶음이 없으면 아직 push 하지 않은 것입니다. 묶음 파일이 있었지만 마운트하지 못했으면(잘림 · 손상 · 형식
// 버전) 그렇다고 알립니다(packFailed, 이유는 바로 앞 "Pack not mounted" 줄).
static void checkShadersMatchBuild(bool loose, bool anyPack, bool packFailed) {
    const auto bad = hpak::compareShaderManifest(hbuild::kShaderManifest, hbuild::kShaderManifestCount,
                                                 [](const char* path, uint64_t& h) { return hvfs::contentHash(path, h); });
    if (bad.empty()) {
        HLOG(LogEngine, Log) << "Shaders match this build (" << hbuild::kShaderManifestCount << " files)";
        return;
    }
    std::string list;
    for (size_t i = 0; i < bad.size() && i < 4; ++i)
        list += (i ? ", " : "") + bad[i].path + (bad[i].missing ? " (missing)" : " (different)");
    if (bad.size() > 4) list += ", ...";
#ifdef ANDROID
    const char* advice = anyPack    ? "The game pack is from another build; push the pack made by this Android build "
                                      "(build/android/pack/game.hpak) into the app's files folder with adb"
                       : packFailed ? "The game pack could not be mounted (see \"Pack not mounted\" above); push "
                                      "build/android/pack/game.hpak again with adb"
                                    : "No game pack was found; push build/android/pack/game.hpak into the app's files folder "
                                      "with adb";
#else
    const bool allMissing = bad.size() == hbuild::kShaderManifestCount &&
                            std::all_of(bad.begin(), bad.end(), [](const hpak::ShaderMismatch& m) { return m.missing; });
    const char* advice = anyPack    ? "The game pack is from another build; rebuild the GamePack target and replace the "
                                      "game.hpak next to the executable"
                       : packFailed ? "The game pack next to the executable could not be mounted (see \"Pack not mounted\" "
                                      "above); replace it with the game.hpak made by this build"
                       : allMissing ? "Neither game.hpak nor a shaders folder was found next to the executable"
                                    : "The shader files next to the executable are newer or older than the executable "
                                      "(for example only the Shaders target was rebuilt); rebuild HarukaGame";
#endif
    const std::string msg = std::to_string(bad.size()) + " of " + std::to_string(hbuild::kShaderManifestCount) +
                            " shaders do not match this build: " + list + ". " + advice;
    if (loose) {
        HLOG(LogEngine, Warning) << msg << " (--loose: continuing)";
        return;
    }
    throw std::runtime_error(msg);
}

// ── Bootstrap ─────────────────────────────────────────────────────────────────
// 코어 서비스는 초기화 순서가 고정입니다: 창 → 렌더러(창 필요) → 에셋(렌더러 필요).
// 그다음 동적 모듈을 등록하고, 마지막에 에디터·게임을 올립니다.
Engine::Engine(const EngineOptions& options, std::unique_ptr<IEngineFrontend> frontend) {
    if (!frontend) throw std::runtime_error("Engine needs a frontend (editor or game)");
    frontend_ = std::move(frontend);
    policy_   = frontend_->policy();
    options_  = options;

    // 기본 컴포넌트 타입을 씬을 읽기 전에 등록합니다(DEF-22). 씬 · 프리팹의 COMPONENT 블록과 에디터의
    // 컴포넌트 추가 버튼이 이 등록을 씁니다.
    registerCoreComponents();
    registerXrComponents();   // 플레이어 리그 · 추적 자세 · 이동 · 잡기(2026-10-15)

    // 에셋 루트(2026-10-15). 에디터는 프로젝트 폴더의 assets/ 를 직접 읽고 씁니다(ProjectPaths.hpp). 그 경로는 에디터 앞단만
    // 알려 주고(2026-10-19), 그 아래 assets/ 가 없으면(다른 PC 로 옮긴 실행 파일 등) 실행 폴더의 복사본을 씁니다.
    bool projectFolder = false;
    if (!policy_.projectDir.empty()) {
        std::error_code ec;
        const std::filesystem::path project = policy_.projectDir;
        if (std::filesystem::is_directory(project / "assets", ec)) { hproj::setRoot(project); projectFolder = true; }
    }
    if (projectFolder) {
        HLOG(LogEngine, Log) << "Asset root: project folder " << hproj::root().generic_string() << "/assets";
    } else {
        // 실행 폴더의 assets/ 를 작업 폴더가 아니라 실행 파일 기준으로 찾습니다(2026-10-19). 예전에는 작업 폴더 기준이라
        // 바로가기나 다른 폴더에서 실행하면 씬 · 모델을 찾지 못했습니다.
        hproj::setRoot(happ::exeDir());
        HLOG(LogEngine, Log) << "Asset root: assets/ next to the executable (" << happ::exeDir().generic_string() << ")";
    }
    // 엔진이 읽는 패키지 파일(셰이더 · 에셋)은 가상 파일 시스템으로 찾습니다(2026-10-24, vfs/Vfs.hpp). 실행 폴더의 낱개
    // 파일이 바닥이고, 에디터는 프로젝트 폴더의 assets/ 를, 게임 실행 파일은 실행 폴더의 묶음 파일(game.hpak 등)을 그 위에
    // 올립니다. --loose 를 주면 낱개 파일이 묶음보다 위입니다(개발 중 묶음을 다시 만들지 않고 에셋을 바꿔 보기).
    // 프로젝트 폴더를 쓰는 에디터는 실행 폴더에서 shaders/ 만 찾습니다(DEF-124). 빌드의 에셋 복사는 지운 파일을 남기므로,
    // 실행 폴더 전체를 바닥에 두면 프로젝트에서 지우거나 이름을 바꾼 모델 · 소리를 옛 복사본으로 아무 표시 없이 읽었습니다.
    hvfs::reset();
    hvfs::mountDirectory(happ::exeDir(), projectFolder ? "shaders/" : "", 0);
    if (projectFolder) hvfs::mountDirectory(hproj::root(), "assets/", 10);
    bool anyPack    = false;   // 게임 묶음을 하나라도 마운트함(셰이더 확인의 안내 문구가 이것을 봄)
    bool packFailed = false;   // 묶음 파일이 있었지만 마운트하지 못함
    if (policy_.mountGamePacks) {
        // 묶음을 찾을 폴더는 실행 폴더가 먼저이고, Android 는 OBB 폴더가 그 다음입니다(2026-10-31). 묶음이 있는 첫 폴더만
        // 씁니다(두 폴더에 같은 이름의 묶음이 있어도 겹쳐 마운트하지 않음). 뒤 폴더에도 묶음이 있으면 쓰지 않는다고 알립니다
        // (DEF-149). 예전에는 외부 파일 폴더의 옛 묶음이 OBB 폴더의 새 묶음을 아무 표시 없이 가렸습니다.
        std::filesystem::path usedDir;
        for (const auto& dir : happ::packDirs()) {
            if (anyPack) {
                if (hvfs::hasPackFiles(dir)) {
                    HLOG(LogEngine, Warning) << "Game packs in " << hpak::utf8Of(dir) << " are not used; the packs in "
                                             << hpak::utf8Of(usedDir) << " come first";
                }
                continue;
            }
            const hvfs::MountReport rep = hvfs::mountGamePacks(dir, 20);
            for (const auto& f : rep.failed) HLOG(LogEngine, Error) << "Pack not mounted: " << f;
            if (!rep.failed.empty()) packFailed = true;
            if (!rep.mounted.empty()) { anyPack = true; usedDir = dir; }
        }
        if (!anyPack) {
            HLOG(LogEngine, Log) << (packFailed ? "No game pack could be mounted; reading loose files"
                                                : "No game pack next to the executable; reading loose files");
        }
    }
    // --loose 는 묶음을 마운트하는 게임 실행 파일에만 뜻이 있습니다(DEF-126). 에디터에 주면 실행 폴더의 에셋 복사본이 프로젝트
    // 폴더를 가리므로 무시합니다. 우선순위는 패치 번호가 커도 모든 묶음보다 위입니다.
    if (options.loose) {
        if (policy_.mountGamePacks) hvfs::mountDirectory(happ::exeDir(), "", hvfs::kLoosePriority);
        else HLOG(LogEngine, Warning) << "--loose is for the game executable; ignored";
    }
    for (const auto& m : hvfs::describe()) HLOG(LogEngine, Log) << "Mounted " << m;
    // 게임 실행 파일은 묶음과 실행 파일의 짝을 확인합니다(DEF-149). 에디터는 Shaders 대상만 다시 빌드해 실행 중에 바뀐 셰이더를
    // 읽는(핫 리로드) 흐름이 있어 확인하지 않습니다.
    if (policy_.mountGamePacks) checkShadersMatchBuild(options.loose, anyPack, packFailed);
    // glTF 캐시(원본에서 만든 파생 데이터)는 실행 중에 쓰는 파일입니다. 에디터는 예전처럼 실행 폴더의 assets/cache,
    // 게임 실행 파일은 쓰기 루트의 Cache/models 입니다.
    GassetCache::setDirectory((happ::writeRootIsExeDir() ? happ::inExeDir("assets/cache")
                                                         : happ::inWriteRoot("Cache/models")).string());

    // 프로젝트 설정(시작 씬 · 입력 바인딩). 없으면 기본값으로 갑니다. 저장소에 기본 파일이 들어 있습니다.
    {
        std::string text;
        if (hvfs::readText(ProjectSettings::kPath, text)) {
            std::vector<std::string> messages;
            project_ = project_settings::parse(text, messages);
            for (const auto& m : messages) HLOG(LogEngine, Warning) << ProjectSettings::kPath << ": " << m;
        } else {
            HLOG(LogEngine, Warning) << ProjectSettings::kPath << " not found; using defaults (start scene "
                                     << project_.startScene << ")";
        }
    }

    // 그리기 행 · 라이트 · 물리 바디 상한(DEF-10). 렌더러 · 물리를 만들기 전에 읽습니다.
    const EngineLimits limits = loadEngineLimits("Config/engine.ini");
    HLOG(LogEngine, Log) << "Limits: draw rows " << limits.maxDrawRows << ", lights " << limits.maxLights
                         << ", physics bodies " << limits.maxBodies << " (Config/engine.ini)";

    // 창(2026-10-31). Android 는 화면 표면이 없는 창이고, PC 의 게임 실행 파일은 --headless 면 OS 창 없이 돕니다. 에디터는
    // 창 없이는 쓸 수 없어(0x0 이면 Play 를 누를 수 없고 XR 세션도 열리지 않음) 무시합니다.
#ifdef ANDROID
    window_ = std::make_unique<AndroidWindow>(options.platform.androidApp);
#else
    if (options.headless && !policy_.allowHeadless) {
        HLOG(LogEngine, Warning) << "--headless is for the game executable; opening a window";
    }
    if (options.headless && policy_.allowHeadless) window_ = std::make_unique<HeadlessWindow>();
    else                                           window_ = std::make_unique<GlfwWindow>(1600, 900, "Haruka Engine");
#endif
    window_->init();
    headless_ = !window_->providesSurface();

    // XR 은 렌더러보다 먼저입니다. OpenXR 이 헤드셋이 연결된 GPU 를 고르고 Vulkan 인스턴스 · 장치를
    // 만들기 때문입니다(UE · Unity · Godot 같음). 런타임이나 헤드셋이 없으면 데스크톱으로 계속합니다.
    // 켤지는 명령줄이 먼저이고, 없으면 앞단 정책을 따릅니다(게임 실행 파일은 프로젝트 설정 xr.startInVr, 2026-10-19).
    xrWanted_ = options.xr.has_value() ? *options.xr : (policy_.startXrFromProject && project_.xrStartInVr);
    if (xrWanted_ && !options.xr.has_value())
        HLOG(LogEngine, Log) << "XR requested by the project setting xr.startInVr (" << ProjectSettings::kPath << ")";
    if (xrWanted_) {
        xr_ = std::make_unique<XrSystem>();
        if (!xr_->init(options.platform)) xr_.reset();
    }
    if (options.xrRequired && !xr_)
        throw std::runtime_error("XR is required on this platform, but the OpenXR runtime or headset is not available "
                                 "(see the XR log lines above)");

    renderer_ = std::make_unique<Renderer>();
    renderer_->setLimits(limits.maxDrawRows, limits.maxLights);
    renderer_->setForceNoSync2(options.vkNoSync2);
    renderer_->setForceDedicatedMsaa(options.vkDedicatedMsaa);
    renderer_->init(window_.get(), xr_.get());
    if (xr_) {
        // 세션은 Play 때 만듭니다(syncXrWithPlay). 인스턴스는 쓸 수 없어도 남겨 둡니다. 렌더러의 Vulkan
        // 인스턴스 · 장치를 그것으로 만들었을 수 있어서, 엔진이 끝날 때 렌더러 다음에 지웁니다.
        xrUsable_ = renderer_->xrCompatible();
        if (xrUsable_) HLOG(LogEngine, Log) << "XR ready; the headset session starts with Play";
        else           HLOG(LogEngine, Warning) << "XR is disabled for this run; continuing in desktop mode";
    }
    if (options.xrRequired && !xrUsable_)
        throw std::runtime_error("XR is required on this platform, but the renderer could not use the OpenXR runtime's "
                                 "Vulkan device (see the Renderer and XR log lines above)");
    if (options.emulateStereo) {
        if (xrUsable_) {
            HLOG(LogEngine, Warning) << "--emulate-stereo is ignored while XR is enabled";
        } else if (headless_) {
            HLOG(LogEngine, Warning) << "--emulate-stereo needs a window; ignored (use --synthetic-eyes)";
        } else {
            renderer_->setStereoEmulation(true);
        }
    }
    // 합성 눈: 헤드셋 없이 XR 프레임 경로로 두 눈을 그립니다(2026-10-31). XR 세션과 같은 자리를 쓰므로 XR 을 쓸 수 있으면
    // 붙이지 않습니다.
    if (options.syntheticEyes) {
        if (xrUsable_) {
            HLOG(LogEngine, Warning) << "--synthetic-eyes is ignored while XR is enabled";
        } else {
            synthetic_ = renderer_->attachSyntheticEyes({ options.syntheticEyeW, options.syntheticEyeH });
        }
    }
    if (headless_ && !xrUsable_ && !synthetic_) {
        HLOG(LogEngine, Warning) << "No window, no XR and no synthetic eyes: nothing will be drawn";
    }

    assets_ = std::make_unique<AssetManager>(renderer_.get());

    // 동적 모듈: 표준 세트(Audio, Physics). 각자 월드를 참조합니다. noAudio(Quest 첫 실행)면 소리 장치를 열지 않습니다.
    if (options.noAudio) { HLOG(LogEngine, Log) << "Audio output is off for this run"; }
    auto* audio   = modules_.registerModule(std::make_unique<AudioSystem>(options.noAudio));
    audio->setWorld(&world_);
    audio_ = audio;
    auto physicsModule = std::make_unique<PhysicsSystem>();
    physicsModule->setMaxBodies(limits.maxBodies);   // startup 이 등록 안에서 일어나므로 그 전에
    auto* physics = modules_.registerModule(std::move(physicsModule));
    physics->setWorld(&world_);
    physics_ = physics;

    // 앞단(에디터 또는 게임).
    frontend_->init(*renderer_, world_, *assets_, *window_);
    frontend_->setAudio(audio_);   // 에디터: 저장된 소리 출력 장치를 openInitialScene 에서 적용(2026-10-16)

    setupInputBindings();

    // 게임 모듈(씬 콘텐츠) + 프레임 컨텍스트.
    ctx_.reset(new EngineContext{ world_, *assets_, *window_, modules_, *renderer_,
                                  ComponentRegistry::get() });
    game_ = std::make_unique<DefaultGameModule>();
    game_->onStart(*ctx_);

    // 처음 열 씬은 앞단이 정합니다. 에디터는 마지막으로 연 씬(없으면 프로젝트 시작 씬), 게임 실행 파일은 프로젝트 시작
    // 씬입니다. 예전에는 게임 모듈이 코드에 박힌 시작 씬을 읽어, 에디터가 지금 열린 파일을 몰랐습니다.
    frontend_->openInitialScene(project_.startScene);
}

// ── Teardown (부팅의 역순) ─────────────────────────────────────────────────────
// 완전히 구성된 Engine 만 여기 도달합니다. 부팅 중 예외로 미완성이면 이미 생성된
// 멤버의 소멸자만 돌아가며, 그때의 안전성은 예전 main() 의 스택 파괴와 동일합니다.
Engine::~Engine() {
    if (game_)     game_->onShutdown();
    modules_.shutdownAll();
    if (assets_)   assets_->clear();
    if (frontend_) frontend_->cleanup();
    // XR 세션은 그것이 쓰는 Vulkan 장치보다 먼저 지웁니다. 합성 눈도 같은 자리에서 뗍니다(Zone B 할당).
    if (synthetic_ && renderer_) renderer_->detachSyntheticEyes();
    if (xr_ && renderer_) renderer_->detachXrSwapchain(false);
    if (xr_)       xr_->destroySession();
    if (renderer_) renderer_->cleanup();
    if (xr_)       xr_->shutdown();
}

void Engine::syncXrWithPlay(bool inGame) {
    if (!xrUsable_ || inGame == xrPlayWanted_) return;
    xrPlayWanted_ = inGame;
    if (inGame) {
        // 앞 Play 의 세션이 아직 끝나는 중이면(Stop 직후 곧바로 Play) 다 끝난 뒤 루프가 다시 엽니다. XR 이 필수인 플랫폼
        // (Quest)에서 첫 세션을 못 만들면 이유를 남기고 멈춥니다. 다시 시도해도 같은 이유로 실패하기 때문입니다(그래픽
        // 연결 · 액션 바인딩). 연결이 잠시 끊겼다 돌아오는 경우는 아래의 다시 열기가 맡습니다.
        if (!xr_->hasSession() && !startXrSession() && options_.xrRequired)
            throw std::runtime_error("XR is required on this platform, but the XR session could not be created "
                                     "(see the XR log lines above)");
    } else if (xr_->hasSession() && !xr_->requestExit()) {
        endXr("Play stopped");   // 돌고 있지 않으면(헤드셋을 벗음 등) 요청할 것이 없어 바로 지웁니다
    }
}

bool Engine::startXrSession() {
    if (!xr_->createSession(renderer_->getInstance(), renderer_->getPhysDevice(), renderer_->getDevice(),
                            renderer_->graphicsQueueFamily(), renderer_->getSwapFormat())) {
        xr_->destroySession();
        HLOG(LogEngine, Warning) << "XR session could not be created; this Play runs on the desktop only";
        return false;
    }
    renderer_->attachXrSwapchain(xr_->swapchainImages(), xr_->swapchainFormat(), xr_->eyeExtent());
    HLOG(LogEngine, Log) << "XR session created for Play; drawing starts when the runtime reports READY";
    // XR Play 동안 소리를 헤드셋 장치로(2026-10-16, 에디터 설정에서 끌 수 있음). 세션이 끝나면 고른 장치로 돌아갑니다.
    if (audio_ && frontend_->headsetAudioInVr() && !xr_->headsetAudioOutputId().empty())
        audio_->setOutputOverride(xr_->headsetAudioOutputId());
    return true;
}

void Engine::endXr(const char* reason) {
    if (!xr_ || !xr_->hasSession()) return;
    renderer_->detachXrSwapchain();   // 장치가 쉬기를 기다린 뒤 이미지 뷰를 지움
    xr_->destroySession();
    HInputSystem::get().setXrInput({});
    if (physics_) physics_->setStepRate(60.f);
    if (audio_ && !audio_->outputOverride().empty()) audio_->setOutputOverride("");   // 고른 출력 장치로 돌아감
    implicitRigCam_ = 0;   // 다음 Play 에서 바닥을 다시 찾습니다
    HLOG(LogEngine, Log) << "XR session ended (" << reason << ")";
}

void Engine::onXrInstanceLost() {
    endXr("the OpenXR instance was lost");
    xrUsable_       = false;   // 다음 Play 에서도 세션을 만들지 않습니다(잃은 인스턴스로는 만들 수 없음)
    xrRetryPending_      = false;
    xrRecovering_        = false;
    xrRecoveredRunSince_ = {};
    if (options_.xrRequired) {
        HLOG(LogEngine, Error) << "XR: the OpenXR instance was lost and this platform has no desktop view; closing";
        window_->setShouldClose(true);
    } else {
        HLOG(LogEngine, Warning) << "XR: the OpenXR instance was lost; VR is off for the rest of this run";
    }
}

// ── 입력 바인딩 (프로젝트 설정 레이어, 게임 코드와 분리) ────────────────────────
void Engine::setupInputBindings() {
    // 에디터 카메라는 EditorApp 이 raw input 으로 직접 처리합니다. 게임 입력은 프로젝트 설정(assets/project.ini)의
    // input. 줄로 정하고, 한 줄도 없으면 엔진 기본값(이동 · 회전 · 시선 · 잡기)을 씁니다(2026-10-15).
    HInputSystem& in = HInputSystem::get();
    in.clearBindings();
    const auto lines = project_.input.empty() ? HInputSystem::defaultBindings() : project_.input;
    int applied = 0;
    for (const auto& [key, value] : lines) {
        std::string err;
        if (in.applyBinding(key, value, err)) ++applied;
        else HLOG(LogEngine, Warning) << ProjectSettings::kPath << ": " << key << " = " << value << ": " << err << "; skipped";
    }
    HLOG(LogEngine, Log) << "Input bindings: " << applied << (project_.input.empty() ? " (engine defaults)" : " (project settings)");
    // 컨트롤러 진동은 XR 모듈로 보냅니다(세션이 입력을 받는 동안만 실제로 울림).
    in.setHapticSink([this](int hand, float amplitude, float seconds) {
        if (xr_ && xr_->hasSession()) xr_->applyHaptic(hand, amplitude, seconds);
    });
}

// 화면 카메라가 플레이어 리그의 머리면 그 리그를 돌려줍니다(카메라 액터에 TrackedPose[Head], 부모에 XrRig).
static const Actor* rigOfCamera(const CameraComponent* cam) {
    if (!cam || !cam->owner) return nullptr;
    const auto* tp = cam->owner->getComponent<TrackedPoseComponent>();
    const Actor* parent = cam->owner->getParent();
    if (!tp || tp->source != ETrackedSource::Head || !parent || !parent->getComponent<XrRigComponent>()) return nullptr;
    return parent;
}

// ── Main loop ─────────────────────────────────────────────────────────────────
int Engine::run() {
    // 루프 본문을 예전 main() 과 동일하게 유지하기 위해 로컬 참조로 바인딩합니다.
    IWindow&         win        = *window_;
    Renderer&        renderer   = *renderer_;
    IEngineFrontend& front      = *frontend_;
    IGameModule&     gameModule = *game_;
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

    // 자동 실행(2026-10-31): --frames=N 이면 N 프레임을 그린 뒤 닫고, 시간 제한(--timeout, frames 만 있으면 60초)을 넘기면
    // 실패로 닫습니다. 둘 다 없으면 예전처럼 창이 닫힐 때까지 돕니다.
    const auto  runStart     = Clock::now();
    const float timeLimitSec = options_.timeoutSec > 0.f ? options_.timeoutSec : (options_.frames > 0 ? 60.f : 0.f);
    bool        closeAsked   = false;

    while (true) {
        // 창 닫기 요청은 앞단에 묻습니다. 에디터는 저장하지 않은 씬이 있으면 확인 창을 띄우고 닫기를 미루고, 게임은 바로 끝냅니다.
        if (win.shouldClose()) {
            if (front.requestClose()) break;
            win.setShouldClose(false);
        }
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
        // Play 상태는 앞 프레임의 에디터 입력이 바꿉니다. 여기서 세션을 맞추면 진행 중인 XR 프레임이 없습니다.
        syncXrWithPlay(front.isPlaying() || front.isPaused());
        // 세션이 없을 때도 XR 이벤트를 받습니다(DEF-155). 다시 열기를 기다리는 동안 인스턴스를 잃으면(런타임 재시작 등) 그것을
        // 알아야 끝없이 다시 시도하지 않습니다. 예전에는 세션이 있을 때만 받아 인스턴스 손실을 영영 받지 못했습니다.
        if (xr_ && xrUsable_ && !xr_->hasSession()) {
            xr_->pollEvents();
            if (xr_->lossReason() == XrSystem::LossReason::InstanceLost) onXrInstanceLost();
        }
        // 헤드셋 연결이 끊겼다가 돌아오면 세션을 다시 엽니다(게임 실행 파일, 2026-10-19). 정해진 간격(처음 2초)으로 조용히
        // 확인합니다. XR 이 필수인 플랫폼은 데스크톱으로 계속할 수 없으므로, 처음 잃은 때부터 30초 안에 다시 돌지 않으면 끝냅니다
        // (DEF-155). 마감 시각은 다시 연 세션이 시작하지 못하고 또 잃어도 처음 것을 씁니다.
        if (xrRetryPending_ && xr_ && !xr_->hasSession() && xrPlayWanted_) {
            const auto nowRetry = std::chrono::steady_clock::now();
            if (options_.xrRequired && nowRetry >= xrRetryGiveUpAt_) {
                xrRetryPending_      = false;
                xrRecovering_        = false;
                xrRecoveredRunSince_ = {};
                HLOG(LogEngine, Error) << "XR: the session did not come back and this platform has no desktop view; closing";
                win.setShouldClose(true);
            } else if (nowRetry >= xrRetryAt_) {
                xrRetryAt_ = nowRetry + xrRetryDelay_;
                if (xr_->headsetAvailable() && startXrSession()) {
                    xrRetryPending_ = false;
                    HLOG(LogEngine, Log) << "XR: the headset is back; session reopened";
                }
            }
        }
        XrSystem::FrameInfo xrFrame;
        bool xrBegun = false;
        // XR 에는 언제나 리그가 있습니다(2026-10-15 플레이어 리그, 2026-10-16 보이지 않는 리그). 화면 카메라가 씬 리그의
        // 머리면 그 리그의 높이 모드를, 아니면 프로젝트 설정의 값을 씁니다. 프레임을 시작하기 전에 정해 한 프레임 안에서는
        // 바뀌지 않게 합니다.
        CameraComponent* const viewCam = world_.getActiveCamera();
        const Actor* const     viewRig = rigOfCamera(viewCam);
        if (xr_ && xr_->hasSession()) {
            const auto* rc = viewRig ? viewRig->getComponent<XrRigComponent>() : nullptr;
            xr_->setRigSpace(rc ? rc->trackedHeight : project_.xrTrackedHeight, rc ? rc->eyeHeight : project_.xrEyeHeight);
        }
        if (xr_ && xr_->hasSession()) {
            xr_->pollEvents();
            // 입력은 매 프레임 새로 넣습니다. 세션이 멈추면(헤드셋을 벗음 등) 빈 상태가 들어가, 마지막
            // 버튼 · 스틱 값이 눌린 채 남지 않습니다.
            HXrInputState xin;
            if (xr_->sessionLost()) {
                const bool                 requested = xr_->exitRequested();
                const XrSystem::LossReason why       = xr_->lossReason();
                if (why == XrSystem::LossReason::InstanceLost) {
                    // 인스턴스를 잃으면 무엇을 요청했든 이번 실행에서는 다시 열 수 없습니다(DEF-155).
                    onXrInstanceLost();
                } else if (requested) {
                    // Stop 으로 끝냈습니다. 끝내는 중에 다시 Play 를 눌렀으면 곧바로 엽니다.
                    endXr("Play stopped");
                    if (xrPlayWanted_) startXrSession();
                } else if (why == XrSystem::LossReason::Exiting && policy_.quitOnXrExit) {
                    // 게임 실행 파일: 대시보드에서 종료하는 등 런타임이 끝내라고 하면 앱도 끝냅니다(2026-10-19, OpenXR 권고).
                    endXr("the runtime asked the app to exit");
                    win.setShouldClose(true);
                } else if (why == XrSystem::LossReason::Disconnected && policy_.retryXrSession) {
                    // 게임 실행 파일: 헤드셋 연결이 끊겼습니다. 데스크톱으로 계속하면서 돌아오기를 기다립니다.
                    // 복구 중에 또 잃었으면(다시 연 세션이 시작하지 못함) 마감 시각은 그대로 두고 간격을 늘립니다(DEF-155).
                    // 예전에는 손실 때마다 마감 시각을 새로 잡아, 시작에 계속 실패하는 세션이 2초마다 다시 열리며 끝나지 않았습니다.
                    endXr("the headset disconnected; continuing on the desktop until it is back");
                    const auto nowLoss = std::chrono::steady_clock::now();
                    xrRecoveredRunSince_ = {};
                    if (!xrRecovering_) {
                        xrRecovering_    = true;
                        xrRetryDelay_    = std::chrono::seconds(2);
                        xrRetryGiveUpAt_ = nowLoss + std::chrono::seconds(30);
                    } else {
                        xrRetryDelay_ = std::min(xrRetryDelay_ * 2, std::chrono::seconds(30));
                        HLOG(LogEngine, Warning) << "XR: the reopened session was lost before it started; next try in "
                                                 << xrRetryDelay_.count() << " s";
                    }
                    xrRetryPending_ = true;
                    xrRetryAt_      = nowLoss + xrRetryDelay_;
                } else if (options_.xrRequired) {
                    // XR 이 필수인 플랫폼(Quest)은 데스크톱으로 계속할 수 없으므로 끝냅니다(DEF-155). 예전에는 아래 에디터
                    // 갈래로 가서 세션 없이 빈 화면인 채 끝나지 않았습니다.
                    endXr("ended by the runtime");
                    HLOG(LogEngine, Error) << "XR: the session ended and this platform has no desktop view; closing";
                    win.setShouldClose(true);
                } else {
                    // 에디터: 런타임이 Play 중에 끝냈으면(대시보드에서 종료 등) Play 는 데스크톱으로 계속하고, 다음
                    // Play 에서 다시 엽니다.
                    endXr("ended by the runtime; Play continues on the desktop");
                }
            } else if (xr_->sessionRunning()) {
                xrBegun = xr_->beginFrame(xrFrame);
                if (xrBegun) xr_->syncInput(xrFrame.displayTime, xin);
                if (xrBegun && xrRecovering_) {
                    // 다시 연 세션이 5초 동안 이어서 돌면 복구가 끝난 것으로 봅니다(DEF-155). 그 전에 또 잃으면 처음 마감 시각을 씁니다.
                    const auto nowRun = std::chrono::steady_clock::now();
                    if (xrRecoveredRunSince_ == std::chrono::steady_clock::time_point{}) {
                        xrRecoveredRunSince_ = nowRun;
                    } else if (nowRun - xrRecoveredRunSince_ >= std::chrono::seconds(5)) {
                        xrRecovering_        = false;
                        xrRecoveredRunSince_ = {};
                        HLOG(LogEngine, Log) << "XR: the reopened session is running";
                    }
                }
                // 물리 스텝을 헤드셋 주사율에 맞춥니다(1/60 이면 72 · 90Hz 에서 물체가 떨려 보임).
                const float hz = xr_->displayRefreshRate();
                if (physics_ && hz > 1.f && std::abs(physics_->stepRate() - hz) > 0.5f) {
                    physics_->setStepRate(hz);
                    HLOG(LogEngine, Log) << "Physics step matched to the headset: " << hz << " Hz";
                }
            }
            HInputSystem::get().setXrInput(xin);
        }
        if (viewCam) {
            const bool xrLive = xr_ && xr_->hasSession();
            if (viewRig || !xrLive || !viewCam->owner) {
                // 씬 리그(원점 = 리그 액터), 또는 XR 이 아님(데스크톱 · 스테레오 흉내는 예전처럼 카메라 위치 + 요).
                viewCam->setXrRig(viewRig, xrLive ? xr_->rigLift() : 0.f);
                if (!xrLive) implicitRigCam_ = 0;
            } else {
                // 보이지 않는 리그: 이 카메라로 XR 을 처음 볼 때 카메라 아래 바닥을 한 번 찾고, 그 뒤에는 카메라를 따라 같은
                // 거리 아래에 둡니다. 아래에 아무것도 없으면(우주 씬 등) 카메라 위치에 둡니다.
                Actor* const camActor = viewCam->owner;
                if (implicitRigCam_ != camActor->id) {
                    const glm::vec3 cp = camActor->worldPosition();
                    float floorY = 0.f;
                    const bool found = hxr::floorBelow(world_, cp, camActor, floorY);
                    implicitRigDrop_ = found ? cp.y - floorY : 0.f;
                    implicitRigCam_  = camActor->id;
                    if (found) HLOG(LogEngine, Log) << "XR rig: none in the scene; standing on the floor " << implicitRigDrop_ << " m below the camera";
                    else       HLOG(LogEngine, Log) << "XR rig: none in the scene and nothing below the camera; rig placed at the camera";
                }
                viewCam->setXrImplicitRig(implicitRigDrop_, xr_->rigLift());
            }
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
            return front.inViewport(x, y, (float)winW, (float)winH);
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
        // 게임 실행 파일은 헤드셋이 입력 초점을 잃으면(벗음 · 시스템 메뉴) 멈춥니다(2026-10-19, Meta VRC.Quest.Functional.2).
        // 에디터의 Pause 와 같은 경로(월드 · 모듈 tick 정지, 소리 그룹 일시정지)이고, 초점이 돌아오면 이어서 갑니다.
        const bool focusLost = policy_.pauseOnXrFocusLoss && xr_ && xr_->hasSession() && !HInputSystem::get().xr().focused;
        if (focusLost != xrFocusPaused_) {
            xrFocusPaused_ = focusLost;
            HLOG(LogEngine, Log) << (focusLost ? "Paused: the headset does not have input focus (taken off or system menu)"
                                               : "Resumed: the headset has input focus");
        }
        const bool inGame  = front.isPlaying() || front.isPaused();
        const bool paused  = front.isPaused() || (front.isPlaying() && xrFocusPaused_);
        const bool playing = front.isPlaying() && !xrFocusPaused_;
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
            if (!minimized) front.tick(dt);
            // 디버그 선은 한 프레임용입니다. 에디터는 tick 에서 앞 프레임의 선을 가져가 그리므로, 그 뒤 · 월드 틱 전에
            // 비웁니다. 예전에는 에디터만 비워, 에디터가 없거나(게임 실행 파일) 최소화로 에디터 tick 을 건너뛰면 선이
            // 끝없이 쌓였습니다(2026-10-19).
            hdebug::lines().clear();
            HInputSystem::get().update(dt, win, front.isTextInputActive());
            if (playing) {
                world.tick(dt);
                gameModule.onTick(dt, ctx);
                modules.tickAll(dt);
            }
            // 마우스 시선은 이번 프레임 것만 씁니다(에디터가 다음 tick 에 다시 넣음).
            HInputSystem::get().setLookDelta(0.f, 0.f);
        }

        // ── Editor input (단축키는 EditorApp::tickShortcuts에서 처리) ──────
        if (!minimized) {
            ScopedCpuTimer t(gFrameTimings().handleInput);
            front.handleInput(mx, my, lmb, lmbJustUp, 0.f,
                              (float)winW, (float)winH);
        }

        // ── Render ─────────────────────────────────────────────────────────
        // XR: 눈 자세를 이 프레임에 한 번 받아 컬링 · 렌더 · 제출에 함께 씁니다(DEF-32). 헤드셋이 화면을
        // 보여 주지 않거나(벗음 등) 추적을 잃으면 그 프레임은 데스크톱만 그리고 빈 프레임을 제출합니다.
        // 합성 눈(--synthetic-eyes)은 XR 세션 대신 렌더러가 만든 이미지에 같은 경로로 그립니다. 눈은 스테레오 흉내와 같은
        // 두 평행한 눈이고, 원점은 XR 과 같습니다(씬 리그면 리그 액터 + 눈높이, 아니면 카메라 위치 + 요).
        bool drewSynthetic = false;
        if (xrBegun) {
            XrFrameTarget target;
            bool acquired = false;
            if (xrFrame.shouldRender && xr_->locateViews(xrFrame.displayTime, target.eyes))
                acquired = xr_->acquireImage(target.imageIndex);
            const bool rendered = renderer.drawFrame(world, acquired ? &target : nullptr);
            if (acquired) xr_->releaseImage();
            xr_->endFrame(xrFrame.displayTime, acquired && rendered);
            if (acquired && rendered) ++drawnFrames_;
        } else if (synthetic_) {
            static_assert(kMaxViews == 2, "synthetic eyes are a stereo pair");
            const float tanUp  = std::tan(glm::radians(viewCam ? viewCam->fovY : 45.f) * 0.5f);
            const float aspect = (float)options_.syntheticEyeW / (float)options_.syntheticEyeH;
            const auto* rigC   = viewRig ? viewRig->getComponent<XrRigComponent>() : nullptr;
            const std::array<XrEyePose, 2> eyes = hxr::syntheticEyes(hxr::kSyntheticIpd, tanUp, aspect,
                                                                     rigC ? rigC->eyeHeight : 0.f);
            XrFrameTarget target;
            target.imageIndex = syntheticImage_;
            target.eyes[0]    = eyes[0];
            target.eyes[1]    = eyes[1];
            drewSynthetic = renderer.drawFrame(world, &target);
            if (drewSynthetic) {
                lastSynthetic_  = syntheticImage_;
                syntheticImage_ = (syntheticImage_ + 1) % renderer.syntheticImageCount();
                ++drawnFrames_;
            }
        } else {
            renderer.drawFrame(world);
            if (renderer.lastFrameDesktop()) ++drawnFrames_;
        }

        if (!closeAsked && options_.frames > 0 && drawnFrames_ >= options_.frames) {
            closeAsked = true;
            win.setShouldClose(true);
        }
        if (!closeAsked && timeLimitSec > 0.f &&
            std::chrono::duration<float>(Clock::now() - runStart).count() > timeLimitSec) {
            closeAsked = true;
            timedOut_  = true;
            HLOG(LogEngine, Warning) << "Run time limit " << timeLimitSec << " s reached after drawing " << drawnFrames_
                                     << " frames; closing";
            win.setShouldClose(true);
        }

        // FPS title (every 0.5 s)
        ++fpsFrames;
        auto  fpsNow = Clock::now();
        float fpsDt  = std::chrono::duration<float>(fpsNow - fpsTimer).count();
        if (fpsDt >= 0.5f && !headless_) {
            int fps = (int)(fpsFrames / fpsDt);
            // VR 을 원했는데 쓸 수 없거나(런타임 · 헤드셋 없음) 연결이 끊겨 기다리는 중이면 제목에 알립니다(2026-10-19).
            const char* vrNote = (xrWanted_ && !xrUsable_) ? "  |  VR unavailable"
                               : xrRetryPending_          ? "  |  VR disconnected" : "";
            win.setTitle(front.windowTitle() + vrNote + "  |  " + std::to_string(fps) + " FPS");
            fpsTimer  = fpsNow;
            fpsFrames = 0;
        }

        // 루프 1 회 전체입니다. 이것이 실제 프레임 간격이며, 위 구간들의 합과
        // 크게 벌어지면 그 차이가 곧 "어느 계측기에도 잡히지 않는 시간"입니다.
        gFrameTimings().loopTotal.add(std::chrono::duration<float, std::milli>(
                                          Clock::now() - loopT0).count());

        // 최소화 중에는 그리지 않아 루프가 코어 하나를 다 쓰므로 쉬어 갑니다(약 60Hz, dt 는 실제 시간이라
        // 시뮬레이션 속도는 그대로). 헤드셋이 돌면 xrWaitFrame 이 속도를 정하므로 쉬지 않습니다. 합성 눈을 그린 프레임도
        // 쉬지 않습니다(창 없는 실행은 언제나 0x0 이라, 쉬면 자동 확인이 프레임마다 15ms 씩 느려짐).
        if (minimized && !xrBegun && !drewSynthetic)
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return finishRun();
}

uint32_t Engine::validationErrorCount() { return Renderer::validationErrorCount(); }

int Engine::finishRun() {
    if (!hopt::isAutomated(options_)) return hopt::kExitOk;

    bool blank = false;
    std::string captureNote = "no capture";
    if (!options_.capturePath.empty()) {
        std::vector<uint8_t> rgba;
        VkExtent2D           ext{};
        if (!synthetic_ || lastSynthetic_ == UINT32_MAX) {
            captureNote = "capture skipped (needs --synthetic-eyes and at least one drawn frame)";
            blank       = true;
        } else if (!renderer_->captureXrEye(lastSynthetic_, 0, rgba, ext)) {
            captureNote = "capture failed";
            blank       = true;
        } else {
            blank = hopt::looksBlank(rgba);
            std::filesystem::path out;
            if (!hpak::pathFromUtf8(options_.capturePath, out)) out = std::filesystem::path(options_.capturePath);
            std::error_code ec;
            if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary);
            auto sink = [](void* ctx, void* data, int size) {
                static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), size);
            };
            const bool ok = f.is_open() &&
                            stbi_write_png_to_func(sink, &f, (int)ext.width, (int)ext.height, 4, rgba.data(),
                                                   (int)ext.width * 4) != 0 && f.good();
            captureNote = (ok ? "capture " : "capture not written: ") + options_.capturePath + " (" +
                          std::to_string(ext.width) + "x" + std::to_string(ext.height) + (blank ? ", blank" : "") + ")";
            if (!ok) blank = true;
        }
    }
    const uint32_t errors = Renderer::validationErrorCount();
    const int      code   = hopt::headlessExitCode(drawnFrames_, options_.frames, timedOut_, errors, blank);
    const std::string summary = "Run summary: drawn " + std::to_string(drawnFrames_) + "/" + std::to_string(options_.frames) +
                                ", validation errors " + std::to_string(errors) + ", " + captureNote + ", exit code " +
                                std::to_string(code);
    if (code == hopt::kExitOk) { HLOG(LogEngine, Log) << summary; }
    else                       { HLOG(LogEngine, Error) << summary; }
    return code;
}
