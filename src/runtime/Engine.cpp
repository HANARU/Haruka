#include "Engine.hpp"

#include "../renderer/Renderer.hpp"
#include "../assets/AssetManager.hpp"
#include "../audio/AudioSystem.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../input/HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include "../xr/XrSystem.hpp"
#include "../core/HLog.hpp"
#include "../core/FieldParse.hpp"
#include "../core/CrashHandler.hpp"
#include "IGameModule.hpp"
#include "../scene/ComponentRegistry.hpp"
#include "../core/EngineSettings.hpp"
#include "../core/HText.hpp"
#include "../core/ProjectDescriptor.hpp"
#include "../core/ProjectPaths.hpp"
#include "../core/FileUtil.hpp"
#include "../core/AppPaths.hpp"
#include "../core/SaveGame.hpp"
#include "../core/TextFloat.hpp"
#include "../core/DebugDraw.hpp"
#include "../assets/GassetCache.hpp"
#include "../vfs/Vfs.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../components/CoreComponents.hpp"
#include "../components/XrComponents.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/RenderExtract.hpp"
#include "../xr/SyntheticEyes.hpp"
#include "../vfs/PathUtf8.hpp"
#include "ShaderManifest.hpp"
#include "XrSessionController.hpp"
#include "XrViewOrigin.hpp"
#include "../core/FrameTimings.hpp"
#include "../core/Profiler.hpp"
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
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

// 프레임마다 백그라운드 모델을 올리는 예산(비동기 로딩 D2).
static constexpr float kUploadBudgetMs = 2.f;

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
    stats_.markBoot();
    xrSession_  = std::make_unique<XrSessionController>();
    viewOrigin_ = std::make_unique<XrViewOrigin>();
    if (!frontend) throw std::runtime_error("Engine needs a frontend (editor or game)");
    frontend_ = std::move(frontend);
    policy_   = frontend_->policy();
    options_  = options;
    // CPU 추적(Docs/CPU_Profiling.md D6): --trace 만 있으면 여기서 시작해 시작 · IBL 굽기 · 첫 씬 읽기까지 담습니다.
    HPROF_THREAD_NAME("Main");
    trace_.configure(options_.tracePath, options_.traceFrom, options_.traceTo);
    HPROF_SCOPE("Engine.Startup");

    // 기본 컴포넌트 타입을 씬을 읽기 전에 등록합니다(DEF-22). 씬 · 프리팹의 COMPONENT 블록과 에디터의
    // 컴포넌트 추가 버튼이 이 등록을 씁니다.
    registerCoreComponents();
    registerXrComponents();   // 플레이어 리그 · 추적 자세 · 이동 · 잡기(2026-10-15)

    // 프로젝트 루트(2026-10-15, P3a 2026-11-19). --project=<폴더> 또는 에디터 기본값(HARUKA_PROJECT_DIR)을 씁니다.
    // 프로젝트 폴더가 있으면 *.hproject 를 읽어 검증하고(없으면 오류로 멈춤), <프로젝트>/Content 를 /Game/ 으로,
    // <프로젝트>/Config 를 /Config/ 으로 마운트합니다.
    bool projectFolder = false;
    const bool explicitProject = !options.projectDir.empty();
    const std::string& rawProjectDir = explicitProject ? options.projectDir : policy_.projectDir;
    if (!rawProjectDir.empty()) {
        std::error_code ec;
        const std::filesystem::path project = rawProjectDir;
        if (explicitProject || std::filesystem::is_directory(project, ec)) {
            const std::filesystem::path projFile = project_descriptor::findInDirectory(project);
            if (projFile.empty()) {
                HLOG(LogEngine, Error) << "Project folder has no .hproject file: " << project.generic_string();
                throw std::runtime_error("Missing .hproject in project folder");
            }
            ProjectDescriptor desc;
            std::vector<std::string> warnings, errors;
            if (!project_descriptor::loadFromFile(projFile, desc, warnings, errors)) {
                for (const auto& w : warnings) HLOG(LogEngine, Warning) << projFile.generic_string() << ": " << w;
                for (const auto& e : errors)   HLOG(LogEngine, Error)   << projFile.generic_string() << ": " << e;
                throw std::runtime_error("Invalid .hproject file");
            }
            for (const auto& w : warnings) HLOG(LogEngine, Warning) << projFile.generic_string() << ": " << w;
            HLOG(LogEngine, Log) << "Project: " << desc.name << " (company '" << desc.company
                                 << "', packageId '" << desc.packageId << "', engineVersion '" << desc.engineVersion << "')";
            if (!std::filesystem::is_directory(project / "Content", ec)) {
                HLOG(LogEngine, Error) << "Project folder has no Content directory: " << project.generic_string();
                throw std::runtime_error("Missing Content directory in project folder");
            }
            hproj::setRoot(project);
            projectFolder = true;
        }
    }
    if (projectFolder) {
        HLOG(LogEngine, Log) << "Asset root: project folder " << hproj::root().generic_string() << "/Content";
    } else {
        // 실행 폴더의 낱개/묶음 파일을 작업 폴더가 아니라 실행 파일 기준으로 찾습니다(2026-10-19).
        hproj::setRoot(happ::exeDir());
        HLOG(LogEngine, Log) << "Asset root: next to the executable (" << happ::exeDir().generic_string() << ")";
    }
    // 엔진이 읽는 패키지 파일(셰이더 · 에셋)은 가상 파일 시스템으로 찾습니다(2026-10-24, vfs/Vfs.hpp).
    hvfs::reset();
    hvfs::mountDirectory(happ::exeDir(), projectFolder ? "shaders/" : "", 0);
    if (projectFolder) {
        hvfs::mountDirectoryAs(hproj::root() / "Content", "/Game/",   10);
        hvfs::mountDirectoryAs(hproj::root() / "Config",  "/Config/", 10);
    } else {
        std::error_code ec;
        if (std::filesystem::is_directory(happ::exeDir() / "Content", ec))
            hvfs::mountDirectoryAs(happ::exeDir() / "Content", "/Game/",   0);
        if (std::filesystem::is_directory(happ::exeDir() / "Config", ec))
            hvfs::mountDirectoryAs(happ::exeDir() / "Config",  "/Config/", 0);
    }
    // 엔진 콘텐츠(/Engine/, D3 2026-11-19): 앞단이 준 폴더(에디터 = <엔진>/Engine/Content), 없으면 실행 폴더의 Engine/Content.
    {
        std::error_code ec;
        std::filesystem::path engineContent;
        if (!policy_.engineContentDir.empty() && std::filesystem::is_directory(policy_.engineContentDir, ec))
            engineContent = policy_.engineContentDir;
        else if (std::filesystem::is_directory(happ::exeDir() / "Engine" / "Content", ec))
            engineContent = happ::exeDir() / "Engine" / "Content";
        if (!engineContent.empty()) hvfs::mountDirectoryAs(engineContent, "/Engine/", 10);   // 마운트 목록 로그에 나옴
    }
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
        if (policy_.mountGamePacks) {
            hvfs::mountDirectory(happ::exeDir(), "", hvfs::kLoosePriority);
            std::error_code ec;
            if (std::filesystem::is_directory(happ::exeDir() / "Content", ec))
                hvfs::mountDirectoryAs(happ::exeDir() / "Content", "/Game/",   hvfs::kLoosePriority);
            if (std::filesystem::is_directory(happ::exeDir() / "Config", ec))
                hvfs::mountDirectoryAs(happ::exeDir() / "Config",  "/Config/", hvfs::kLoosePriority);
        }
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
            // 플랫폼 접미사 줄(이름.android 등, 2026-11-18)은 이 빌드의 플랫폼 것을 씁니다. --settings-platform 으로 바꿔 시험합니다.
            const std::string platform = options.settingsPlatform.empty() ? std::string(settings_platform::native())
                                                                          : options.settingsPlatform;
            project_ = project_settings::parse(text, messages, platform);
            for (const auto& m : messages) HLOG(LogEngine, Warning) << ProjectSettings::kPath << ": " << m;
            if (!options.settingsPlatform.empty()) {
                HLOG(LogEngine, Log) << "Project settings read for platform '" << platform << "' (--settings-platform)";
            }
        } else {
            HLOG(LogEngine, Warning) << ProjectSettings::kPath << " not found; using defaults (start scene "
                                     << project_.startScene << ")";
        }
    }

    // 사용자 설정(쓰기 루트의 Config/user_settings.ini, 2026-11-20). 프로젝트 설정 위에 플레이어가 바꾼 값만 덮습니다.
    loadUserSettings();
    saves_ = std::make_unique<SaveStore>(happ::inWriteRoot("Saves"));
    setupCVars();

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
    else                                           window_ = std::make_unique<GlfwWindow>(options.windowW, options.windowH, "Haruka Engine", options.hiddenWindow);
#endif
    {
        HPROF_SCOPE("Startup.Window");
        window_->init();
    }
    headless_ = !window_->providesSurface();

    // XR 은 렌더러보다 먼저입니다. OpenXR 이 헤드셋이 연결된 GPU 를 고르고 Vulkan 인스턴스 · 장치를
    // 만들기 때문입니다(UE · Unity · Godot 같음). 런타임이나 헤드셋이 없으면 데스크톱으로 계속합니다.
    // 켤지는 명령줄이 먼저이고, 없으면 앞단 정책을 따릅니다(게임 실행 파일은 프로젝트 설정 xr.startInVr, 2026-10-19).
    xrWanted_ = options.xr.has_value() ? *options.xr : (policy_.startXrFromProject && project_.xrStartInVr);
    if (xrWanted_ && !options.xr.has_value())
        HLOG(LogEngine, Log) << "XR requested by the project setting xr.startInVr (" << ProjectSettings::kPath << ")";
    if (xrWanted_) {
        HPROF_SCOPE("Startup.Xr");
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
    renderer_->setPreferredGpu(options.gpuIndex);
    renderer_->setMultiview(options.multiview);
    // 품질 단계(2026-11-18, 성능 안정화 2-2). project.ini 의 render.* 품질 키(플랫폼 접미사 줄 포함).
    {
        Renderer::Quality q;
        q.csmCascades       = project_.renderCsmCascades;
        q.shadowDistance    = project_.renderShadowDistance;
        q.contactShadows    = project_.renderContactShadows;
        q.heightFog         = project_.renderHeightFog;
        q.reflectionCapture = project_.renderReflectionCapture;
        q.lightLimit        = project_.renderLightLimit;
        // 사용자 설정이 프로젝트 값 위에 덮습니다(다시 시작 항목, Docs/Save_And_Settings.md D7).
        q.csmCascades    = (uint32_t)userSettings_.getInt("render.csmCascades", (int)q.csmCascades);
        q.shadowDistance = userSettings_.getFloat("render.shadowDistance", q.shadowDistance);
        q.contactShadows = userSettings_.getBool("render.contactShadows", q.contactShadows);
        q.heightFog      = userSettings_.getBool("render.heightFog", q.heightFog);
        for (const UserSettings::KeyInfo& k : UserSettings::keys())
            if (k.apply == UserSettings::Apply::Restart) restartApplied_[std::string(k.name)] = userSettings_.getString(k.name);
        renderer_->setQuality(q);
        const Renderer::Quality d;
        if (q.csmCascades != d.csmCascades || q.shadowDistance != d.shadowDistance || q.contactShadows != d.contactShadows ||
            q.heightFog != d.heightFog || q.reflectionCapture != d.reflectionCapture || q.lightLimit != d.lightLimit) {
            HLOG(LogEngine, Log) << "Quality: shadow cascades " << q.csmCascades << ", shadow distance " << q.shadowDistance
                                 << " m, contact shadows " << (q.contactShadows ? "on" : "off") << ", height fog "
                                 << (q.heightFog ? "on" : "off") << ", reflection capture " << (q.reflectionCapture ? "on" : "off")
                                 << ", light limit " << (q.lightLimit ? std::to_string(q.lightLimit) : std::string("engine.ini"))
                                 << " (" << ProjectSettings::kPath
                                 << (userSettings_.has("render.csmCascades") || userSettings_.has("render.shadowDistance")
                                     || userSettings_.has("render.contactShadows") || userSettings_.has("render.heightFog")
                                         ? std::string(" + ") + UserSettings::kPath : std::string())
                                 << ")";
        }
    }
    // 후처리 패스 켬/끔(프로젝트 설정 render.ao · render.bloom). --ssao · --no-ssao 가 AO 를 덮어씁니다.
    renderer_->setPostFeatures(userSettings_.getBool("render.ao", project_.renderAo),
                               userSettings_.getBool("render.bloom", project_.renderBloom));
    if (options.ssao.has_value()) renderer_->setAoEnabled(*options.ssao);
    if (!renderer_->aoEnabled() || !renderer_->bloomEnabled()) {
        HLOG(LogEngine, Log) << "Post:" << (renderer_->aoEnabled() ? "" : " AO off")
                             << (renderer_->bloomEnabled() ? "" : " Bloom off") << " ("
                             << (options.ssao.has_value() && !*options.ssao ? "--no-ssao, " : "")
                             << ProjectSettings::kPath
                             << (userSettings_.has("render.ao") || userSettings_.has("render.bloom")
                                     ? std::string(" + ") + UserSettings::kPath : std::string())
                             << ")";
    }
    {
        HPROF_SCOPE("Startup.Renderer");   // Vulkan 장치 · 파이프라인 · IBL 굽기
        renderer_->init(window_.get(), xr_.get());
    }
    if (xr_) {
        // 세션은 Play 때 만듭니다(syncXrWithPlay). 인스턴스는 쓸 수 없어도 남겨 둡니다. 렌더러의 Vulkan
        // 인스턴스 · 장치를 그것으로 만들었을 수 있어서, 엔진이 끝날 때 렌더러 다음에 지웁니다.
        xrSession_->setUsable(renderer_->xrCompatible());
        if (xrSession_->usable()) HLOG(LogEngine, Log) << "XR ready; the headset session starts with Play";
        else           HLOG(LogEngine, Warning) << "XR is disabled for this run; continuing in desktop mode";
    }
    if (options.xrRequired && !xrSession_->usable())
        throw std::runtime_error("XR is required on this platform, but the renderer could not use the OpenXR runtime's "
                                 "Vulkan device (see the Renderer and XR log lines above)");
    if (options.emulateStereo) {
        if (xrSession_->usable()) {
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
        if (xrSession_->usable()) {
            HLOG(LogEngine, Warning) << "--synthetic-eyes is ignored while XR is enabled";
        } else {
            synthetic_ = renderer_->attachSyntheticEyes({ options.syntheticEyeW, options.syntheticEyeH });
        }
    }
    if (headless_ && !xrSession_->usable() && !synthetic_) {
        HLOG(LogEngine, Warning) << "No window, no XR and no synthetic eyes: nothing will be drawn";
    }

    assets_ = std::make_unique<AssetManager>(renderer_.get());
    assets_->setAutoLod(options_.autoLod);
    // 가져오기 설정(/Config/import.ini, Docs/Asset_Hot_Reload.md D8). 없으면 모두 기본값입니다(없다는 알림은 남기지 않음).
    {
        std::string text;
        if (hvfs::readText(ImportSettingsTable::kPath, text)) {
            std::vector<std::string> messages;
            assets_->setImportSettings(ImportSettingsTable::parse(text, messages));
            for (const auto& m : messages) HLOG(LogEngine, Warning) << ImportSettingsTable::kPath << ": " << m;
        }
    }
#ifdef __ANDROID__
    assets_->setStreamWorkers(1);   // Quest: 렌더 · XR 스레드와 코어를 나눠 씀(비동기 로딩 D1)
#else
    assets_->setStreamWorkers(2);
#endif

    // 동적 모듈: 표준 세트(Audio, Physics). 각자 월드를 참조합니다. noAudio(Quest 첫 실행)면 소리 장치를 열지 않습니다.
    if (options.noAudio) { HLOG(LogEngine, Log) << "Audio output is off for this run"; }
    HPROF_SCOPE("Startup.Modules");   // 소리 · 물리 · 게임 모듈 · 앞단 · 첫 씬(아래 Startup.FirstScene)
    auto* audio   = modules_.registerModule(std::make_unique<AudioSystem>(options.noAudio));
    audio->setWorld(&world_);
    audio->setListenerSelector(CameraComponent::activeOwnerIn);   // 청취자 = 화면 카메라의 액터(2026-11-18 S7)
    audio->registerCVars(cvars_);   // audio.bus.<이름>.volume · .mute · audio.limiter · audio.snapshot 등(Audio_Design.md 12절)
    audio_ = audio;
    auto physicsModule = std::make_unique<PhysicsSystem>();
    physicsModule->setMaxBodies(limits.maxBodies);   // startup 이 등록 안에서 일어나므로 그 전에
    {
        // 충돌 레이어 표(project.ini 의 physics. 줄, Docs/Physics_Joints_And_Events.md D1)
        std::vector<std::string> messages;
        physicsModule->setLayers(PhysicsLayerTable::parse(project_.physics, messages));
        for (const auto& m : messages) HLOG(LogEngine, Warning) << ProjectSettings::kPath << ": " << m;
    }
    auto* physics = modules_.registerModule(std::move(physicsModule));
    physics->setWorld(&world_);
    physics_ = physics;
    audio->setPhysics(physics);   // 소리 차폐 판정(2026-11-06, Audio_Design.md 9-4)
    // 편안함 설정은 월드 서비스로 둡니다(이동 컴포넌트 · 리그 높이가 읽음). 소리 크기와 함께 사용자 설정에서 적용합니다.
    world_.setService<ComfortSettings>(&comfort_);
    world_.setService<AccessibilityState>(&access_);   // 접근성(Docs/Accessibility.md D2): 글자 배율 · 비네트 · 앉은 자세
    world_.setService<SubtitleQueue>(&subtitles_);     // 자막 큐(D3): 소리 · 게임 코드가 줄을 넣음
    world_.setService<SubtitlePanel>(&subtitlePanel_); // 자막 표시(D4): 추출이 패널 목록 끝에 더함
    world_.setService<CVarRegistry>(&cvars_);
    applyLiveUserSettings(true);

    // 앞단(에디터 또는 게임).
    // 표시 글 문화권(글자 체계 T3): --culture 가 있으면 그 표(/Engine · /Game 의 Localization)를 읽습니다. 에디터는 없으면
    // 자기 설정(editor_state)을 씁니다.
    if (!options_.culture.empty()) {
        std::vector<std::string> errs;
        const size_t n = htext::useCulture(options_.culture, &errs);
        for (const auto& e : errs) HLOG(LogEngine, Warning) << "Localization: " << e;
        HLOG(LogEngine, Log) << "Culture: " << options_.culture << " (" << n << " entries)";
    }
    frontend_->setOptions(options_);
    frontend_->init(*renderer_, world_, *assets_, *window_);
    frontend_->setAudio(audio_);   // 에디터: 저장된 소리 출력 장치를 openInitialScene 에서 적용(2026-10-16)

    setupInputBindings();

    // XR 세션 제어(2026-11-18 S2). 세션이 끝나면 프레임 시간 분포를 남기고, 다음 Play 에서 바닥 · 원점 안내를 다시 합니다.
    {
        XrSessionController::Deps d;
        d.xr = xr_.get(); d.renderer = renderer_.get(); d.physics = physics_; d.audio = audio_;
        d.frontend = frontend_.get(); d.window = window_.get(); d.policy = &policy_; d.xrRequired = options_.xrRequired;
        d.onSessionEnded = [this] { stats_.logPacing("XR session"); viewOrigin_->reset(); };
        xrSession_->bind(d);
    }

    // 게임 모듈(씬 콘텐츠) + 프레임 컨텍스트.
    ctx_.reset(new EngineContext{ world_, *assets_, *window_, modules_, *renderer_,
                                  ComponentRegistry::get(), userSettings_, *saves_, cvars_, {} });
    ctx_->requestScene = [this](const std::string& path) { return frontend_->requestScene(path, false); };
    game_ = createGameModule();   // 링크한 게임 모듈(runtime/IGameModule.hpp)
    game_->onRegister();          // 씬 열기 전에 UIPanel 내용 · 게임 타입 등록
    game_->onStart(*ctx_);

    // 처음 열 씬은 앞단이 정합니다. 에디터는 마지막으로 연 씬(없으면 프로젝트 시작 씬), 게임 실행 파일은 프로젝트 시작
    // 씬입니다. 예전에는 게임 모듈이 코드에 박힌 시작 씬을 읽어, 에디터가 지금 열린 파일을 몰랐습니다.
    // --scene 이 있으면 그 씬이 시작 씬입니다(시험 장면 캡처, 2026-11-16).
    {
        HPROF_SCOPE("Startup.FirstScene");
        frontend_->openInitialScene(options_.scenePath.empty() ? project_.startScene : options_.scenePath);
    }
    // 게임 모듈까지 등록을 마쳤으니, 명령줄 · ini 로 들어왔는데 끝내 등록되지 않은 CVar 이름을 알리고 첫 변경 콜백을 부릅니다.
    for (const std::string& n : cvars_.pendingNames())
        HLOG(LogEngine, Warning) << "CVar '" << n << "' was set (command line or cvars.ini) but nothing registered it";
    cvars_.dispatchChanges();
    HLOG(LogEngine, Log) << "Startup: engine ready in " << stats_.msSinceBoot()
                         << " ms (window, renderer, IBL, modules, first scene)";
}

Engine::~Engine() {
    if (game_)     game_->onShutdown();
    if (settingsDirty_) saveUserSettings("shutdown");
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

// ── CVar (2026-11-20, Docs/Gameplay_Tools.md D6 ~ D8) ───────────────────────────────────────────────────
void Engine::setupCVars() {
    // 값 먼저: 쓰기 루트의 Config/cvars.ini, 그다음 --cvar(나중 것이 이김). 아직 등록되지 않은 이름은 레지스트리가 보관합니다.
    {
        const std::filesystem::path path = happ::inWriteRoot("Config/cvars.ini");
        std::string text, why;
        if (hfile::readConfigText(path, text, &why)) {
            std::vector<std::string> messages;
            cvars_.loadIni(text, messages);
            for (const auto& m : messages) HLOG(LogEngine, Warning) << "Config/cvars.ini: " << m;
            HLOG(LogEngine, Log) << "CVars read from Config/cvars.ini";
        } else if (!why.empty()) {
            HLOG(LogEngine, Warning) << "Config/cvars.ini: " << why;
        }
        for (const auto& [name, value] : options_.cvars) {
            std::string w;
            if (!cvars_.set(name, value, CVarRegistry::Source::CommandLine, &w)) {
                HLOG(LogEngine, Warning) << "--cvar=" << name << "=" << value << ": " << w;
            }
        }
    }
    cvTimeScale_ = cvars_.addFloat("time.scale", 1.f, 0.f, 100.f, "World time scale (0 freezes game time, 1 = normal)",
                                   [this](const CVarRegistry::CVar& v) {
                                       world_.setTimeScale(v.asFloat());
                                       HLOG(LogEngine, Log) << "Time scale: " << v.text() << " (cvar time.scale)";
                                   });
    cvMaxDt_ = cvars_.addFloat("time.maxDt", 0.05f, 0.005f, 0.25f, "Longest frame step in seconds (longer frames are slowed down)",
                               [](const CVarRegistry::CVar& v) {
                                   HLOG(LogEngine, Log) << "Max frame step: " << v.text() << " s (cvar time.maxDt)";
                               });
    cvDebugLines_ = cvars_.addBool("debug.lines", true, "Draw debug lines (hdebug) in the editor viewport",
                                   [](const CVarRegistry::CVar& v) {
                                       HLOG(LogEngine, Log) << "Debug lines: " << (v.asBool() ? "on" : "off") << " (cvar debug.lines)";
                                   });
    trace_.registerCommands(cvars_);   // trace.start · trace.stop(Docs/CPU_Profiling.md D6)
    // 접근성(Docs/Accessibility.md D3 · D7): 자막 확인 · 앉은 자세 다시 맞추기.
    cvars_.addCommand("subtitle.say", "subtitle.say <text> [speaker] [seconds]: show a subtitle line (quote text with spaces)",
                      [this](const std::vector<std::string>& args) -> std::string {
                          if (args.empty() || args.size() > 3) return "usage: subtitle.say \"text\" [speaker] [seconds]";
                          SubtitleLine l;
                          l.text = HText::fromString(args[0]);
                          if (args.size() > 1) l.speaker = HText::fromString(args[1]);
                          if (args.size() > 2 && !fieldparse::toFloat(args[2], l.duration)) return "seconds must be a number";
                          return subtitles_.push(std::move(l)) ? std::string("subtitle queued") : std::string("subtitle dropped (queue full)");
                      });
    cvVignetteDebug_ = cvars_.addFloat("comfort.vignetteDebug", 0.f, 0.f, 1.f,
                                       "Force the movement vignette to this strength (0 = off, for checking the look)");
    cvars_.addCommand("subtitle.clear", "subtitle.clear: remove all subtitle lines",
                      [this](const std::vector<std::string>&) -> std::string { subtitles_.clear(); return "subtitles cleared"; });
    cvars_.addCommand("comfort.recenterSeated", "comfort.recenterSeated: measure the seated head height again (VR seated mode)",
                      [this](const std::vector<std::string>&) -> std::string {
                          access_.requestSeatedRecalibrate();
                          return "seated height will be measured on the next head pose";
                      });
    cvars_.addCommand("scene", "scene <path>: switch to a scene in the background (game executable only)",
                      [this](const std::vector<std::string>& args) -> std::string {
                          if (args.size() != 1) return "usage: scene /Game/scenes/Name.hscene";
                          return frontend_->requestScene(args[0], false) ? "loading " + args[0]
                                                                         : "cannot switch to " + args[0] + " here (game executable only)";
                      });
}

// ── 사용자 설정 (2026-11-20, Docs/Save_And_Settings.md D6 ~ D10) ─────────────────────────────────────────
void Engine::loadUserSettings() {
    const std::filesystem::path path = happ::inWriteRoot(UserSettings::kPath);
    std::vector<std::string> messages;
    const bool found = userSettings_.load(path, messages);
    for (const auto& m : messages) HLOG(LogEngine, Warning) << UserSettings::kPath << ": " << m;
    // --user-setting 은 시작할 때 바꾸고 바로 저장합니다(다시 시작 확인용).
    bool changed = false;
    for (const auto& [name, value] : options_.userSettings) {
        std::string why;
        const uint64_t before = userSettings_.revision();
        if (!userSettings_.set(name, value, &why)) {
            HLOG(LogEngine, Warning) << "--user-setting=" << name << "=" << value << ": " << why;
            continue;
        }
        if (!why.empty()) { HLOG(LogEngine, Warning) << "--user-setting: " << why; }
        changed = changed || userSettings_.revision() != before;
    }
    if (changed) saveUserSettings("--user-setting");
    settingsApplied_ = userSettings_.revision();
    if (!found && userSettings_.values().empty()) return;
    std::string list;
    for (const auto& [k, v] : userSettings_.values()) list += (list.empty() ? "" : ", ") + k + "=" + v;
    HLOG(LogEngine, Log) << "User settings (" << UserSettings::kPath << "): "
                         << (list.empty() ? std::string("none changed") : list);
}

void Engine::applyLiveUserSettings(bool startup) {
    // 소리: 마스터와 버스 다섯. 덮어쓰지 않은 값은 1(원래 크기)입니다.
    if (audio_) {
        audio_->setMasterVolume(userSettings_.getFloat("audio.master", 1.f));
        static constexpr const char* kBus[kAudioBusCount] = { "audio.sfx", "audio.music", "audio.voice", "audio.ambient", "audio.ui" };
        for (int b = 0; b < kAudioBusCount; ++b) audio_->setBusVolume((EAudioBus)b, userSettings_.getFloat(kBus[b], 1.f));
        bool anyAudio = userSettings_.has("audio.master");
        for (const char* k : kBus) anyAudio = anyAudio || userSettings_.has(k);
        std::string vols = "master " + floatToText(audio_->masterVolume());
        for (int b = 0; b < kAudioBusCount; ++b)
            vols += std::string(", ") + audioBusName((EAudioBus)b) + " " + floatToText(audio_->busVolume((EAudioBus)b));
        if ((anyAudio || !audioLogged_.empty()) && vols != audioLogged_) {
            HLOG(LogEngine, Log) << "Audio volumes: " << vols << " (user settings)";
            audioLogged_ = vols;
        }
    }
    // 후처리: --ssao · --no-ssao 가 있으면 AO 는 그 값을 지킵니다. 시작할 때는 렌더러를 만들 때 이미 정했습니다.
    if (startup && renderer_) {
        postLogged_ = std::string("AO ") + (renderer_->aoEnabled() ? "on" : "off") + ", Bloom " + (renderer_->bloomEnabled() ? "on" : "off");
    } else if (renderer_) {
        const bool ao = options_.ssao.has_value() ? *options_.ssao : userSettings_.getBool("render.ao", project_.renderAo);
        renderer_->setPostFeatures(ao, userSettings_.getBool("render.bloom", project_.renderBloom));
        const std::string post = std::string("AO ") + (renderer_->aoEnabled() ? "on" : "off") + ", Bloom "
                                 + (renderer_->bloomEnabled() ? "on" : "off");
        if (post != postLogged_) {
            HLOG(LogEngine, Log) << "Post: " << post << " (user settings)";
            postLogged_ = post;
        }
    }
    // 편안함.
    const ComfortSettings c = ComfortSettings::from(userSettings_);
    if (!(c == comfort_) || (startup && !(c == ComfortSettings{}))) {
        comfort_ = c;
        HLOG(LogEngine, Log) << "Comfort: snap turn "
                             << (c.snapTurnDeg > 0.f ? floatToText(c.snapTurnDeg) + " deg" : std::string("scene"))
                             << ", teleport " << (c.teleport < 0 ? "scene" : c.teleport ? "on" : "off")
                             << ", height " << (c.height < 0 ? "scene" : c.height == 0 ? "tracked" : "fixed")
                             << ", eye height " << (c.eyeHeight > 0.f ? floatToText(c.eyeHeight) + " m" : std::string("scene"))
                             << ", vignette " << floatToText(c.vignette) << ", seated " << (c.seated ? "on" : "off")
                             << ", standing height " << (c.standingHeight > 0.f ? floatToText(c.standingHeight) + " m" : std::string("scene"))
                             << " (user settings)";
    }
    // 접근성(Docs/Accessibility.md D1): 월드 서비스 값 · 손 바꾸기. 기본값이면 시작할 때 알리지 않습니다.
    access_.access           = AccessSettings::from(userSettings_);
    access_.vignetteStrength = comfort_.vignette;
    HInputSystem::get().setHandSwap(access_.access.leftHanded);
    {
        static const char* kFilter[] = { "none", "protanopia", "deuteranopia", "tritanopia" };
        const AccessSettings& a = access_.access;
        const std::string line = std::string("subtitles ") + (a.subtitles ? "on" : "off") + ", subtitle size " + floatToText(a.subtitleSize)
                               + ", background " + floatToText(a.subtitleBackground) + ", text scale " + floatToText(a.textScale)
                               + ", hand " + (a.leftHanded ? "left" : "right") + ", color filter " + kFilter[std::clamp(a.colorFilter, 0, 3)];
        if (line != accessLogged_ && (!accessLogged_.empty() || !(a == AccessSettings{}))) {
            HLOG(LogEngine, Log) << "Accessibility: " << line << " (user settings)";
        }
        accessLogged_ = line;
    }
}

// 사용자 입력 덮어쓰기(input.action.* · input.axis.* · input.mod.* · input.context.*). 규칙(프로젝트 줄 위에 덮음, 지우면 되돌림,
// 틀린 글은 알리고 프로젝트 줄 유지, none = 비움)은 HInputSystem::applyUserOverrides 에 있습니다(Docs/Input_Extensions.md D7).
void Engine::applyUserInput(bool startup) {
    std::vector<std::string> warnings;
    const HInputSystem::OverrideResult r = HInputSystem::get().applyUserOverrides(userSettings_, warnings);
    for (const std::string& w : warnings)
        HLOG(LogEngine, Warning) << UserSettings::kPath << ": " << w << "; keeping the project binding";
    if (r.changed && (r.applied > 0 || !startup)) {
        HLOG(LogEngine, Log) << "Input bindings: " << r.applied << " user override(s) (" << UserSettings::kPath << ")";
    }
}

void Engine::updateUserSettings() {
    const auto now = std::chrono::steady_clock::now();
    if (userSettings_.revision() != settingsApplied_) {
        settingsApplied_ = userSettings_.revision();
        applyLiveUserSettings(false);
        applyUserInput(false);
        for (auto& [name, applied] : restartApplied_) {
            const std::string v = userSettings_.getString(name);
            if (v == applied) continue;
            HLOG(LogEngine, Log) << "User setting " << name << " = " << (v.empty() ? std::string("(project default)") : v)
                                 << " takes effect after a restart";
            applied = v;   // 같은 변경을 한 번만 알림
        }
        settingsDirty_     = true;
        settingsChangedAt_ = now;
    }
    // 슬라이더를 끄는 동안 매 프레임 쓰지 않게, 마지막 변경 1 초 뒤에 씁니다.
    if (settingsDirty_ && now - settingsChangedAt_ >= std::chrono::seconds(1)) saveUserSettings("changed");
}

void Engine::saveUserSettings(const char* why) {
    settingsDirty_ = false;
    std::string err;
    if (userSettings_.save(happ::inWriteRoot(UserSettings::kPath), &err)) {
        HLOG(LogEngine, Log) << "Saved " << UserSettings::kPath << " (" << why << ")";
    } else {
        HLOG(LogEngine, Warning) << "Saving " << UserSettings::kPath << " failed: " << err;
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
    in.setProjectLines(lines);   // 사용자 덮어쓰기 · 재지정의 기준(Docs/Input_Extensions.md D6)
    applyUserInput(true);
    // 컨트롤러 진동은 XR 모듈로 보냅니다(세션이 입력을 받는 동안만 실제로 울림).
    in.setHapticSink([this](int hand, float amplitude, float seconds) {
        if (xr_ && xr_->hasSession()) xr_->applyHaptic(hand, amplitude, seconds);
    });
}

// ── Main loop ─────────────────────────────────────────────────────────────────
// 루프 1 회의 값입니다. 단계가 차례로 채우고 뒤 단계가 읽습니다.
struct Engine::FrameState {
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point   loopT0;
    bool                minimized = false;      // 프레임버퍼 0x0(창 최소화 · 창 없는 실행)
    XrSystem::FrameInfo xrFrame;
    bool                xrBegun   = false;      // XR 프레임을 시작함(이번 프레임 안에 endFrame)
    XrViewChoice        view;                   // 화면 카메라와 그 XR 원점(프레임을 시작하기 전에 정함)
    float               dt        = 0.f;
    int                 winW = 0, winH = 0;
    float               mx = 0.f, my = 0.f;
    bool                lmb = false, lmbJustUp = false;
    bool                playing   = false;      // 월드 · 모듈이 도는 Play(일시정지 · 초점 잃음 제외)
    bool                drewSynthetic = false;
};

// 루프 사이에 이어지는 값입니다.
struct Engine::LoopState {
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point prev;
    bool prevLDown = false, prevRDown = false, prevInGame = false, prevPaused = false;
    // 자동 실행(2026-10-31): --frames=N 이면 N 프레임을 그린 뒤 닫고, 시간 제한(--timeout, frames 만 있으면 60초)을 넘기면
    // 실패로 닫습니다. 둘 다 없으면 예전처럼 창이 닫힐 때까지 돕니다.
    Clock::time_point runStart;
    float             timeLimitSec = 0.f;
    bool              closeAsked   = false;
};

int Engine::run() {
    LoopState loop;
    loop.prev         = LoopState::Clock::now();
    loop.runStart     = loop.prev;
    loop.timeLimitSec = options_.timeoutSec > 0.f ? options_.timeoutSec : (options_.frames > 0 ? 60.f : 0.f);
    stats_.startLoop();
    while (true) {
        HPROF_SCOPE("Frame");
        FrameState f;
        if (!beginLoop(f)) break;
        streamAssets();
        beginXrFrame(f);
        applyViewPose(f);
        readDesktopInput(f, loop);
        updatePlayState(f, loop);
        tickFrame(f);
        frontendInput(f);
        renderFrame(f);
        endLoop(f, loop);
    }
    return finishRun();
}

// 1. 창 닫기 요청은 앞단에 묻습니다. 에디터는 저장하지 않은 씬이 있으면 확인 창을 띄우고 닫기를 미루고, 게임은 바로 끝냅니다.
bool Engine::beginLoop(FrameState& f) {
    HPROF_SCOPE("Engine.BeginLoop");
    IWindow& win = *window_;
    if (win.shouldClose()) {
        if (frontend_->requestClose()) return false;
        win.setShouldClose(false);
    }
    f.loopT0 = FrameState::Clock::now();
    gFrameTimings().resetLast();
    {
        ScopedCpuTimer t(gFrameTimings().pollEvents);
        win.pollEvents();
    }
    // 창이 최소화되면(프레임버퍼 0x0) 에디터 UI 는 쉬고 시뮬레이션 · 소리 · XR 은 계속 돕니다. 예전에는
    // 스왑체인을 다시 만들며 복원될 때까지 루프 전체가 멈춰, 최소화한 채 창을 닫으면 엔진이 끝나지 않았습니다
    // (DEF-106). 에디터의 배치 계산은 0 크기를 다루지 않으므로 부르지 않습니다.
    int fbW = 0, fbH = 0;
    win.getFramebufferSize(fbW, fbH);
    f.minimized = (fbW <= 0 || fbH <= 0);
    return true;
}

// 2. XR 프레임 시작. 세션이 돌면 xrWaitFrame 이 헤드셋 주사율에 맞춰 기다리므로 루프 속도를 헤드셋이 정합니다. 입력은 이 프레임이
// 보일 예상 시각의 자세로 읽어 틱에 넘깁니다. Play 상태는 앞 프레임의 에디터 입력이 바꾸므로, 여기서 세션을 맞추면 진행 중인
// XR 프레임이 없습니다.
void Engine::beginXrFrame(FrameState& f) {
    HPROF_SCOPE("Engine.BeginXrFrame");
    xrSession_->syncWithPlay(frontend_->isPlaying() || frontend_->isPaused());
    xrSession_->pollWithoutSession();
    xrSession_->retryIfDue();
    // 화면 카메라가 씬 리그의 머리면 그 리그의 높이 모드를, 아니면 프로젝트 설정의 값을 씁니다. 프레임을 시작하기 전에 정해
    // 한 프레임 안에서는 바뀌지 않게 합니다.
    f.view = viewOrigin_->choose(world_);
    if (xr_) viewOrigin_->applyRigSpace(*xr_, f.view, project_, &comfort_, &access_);
    f.xrBegun = xrSession_->beginFrame(f.xrFrame);
}

// 3. 화면 카메라의 XR 원점과 소리 청취자. 청취자는 XR 세션이 입력을 받는 동안 헤드셋 자세를 따릅니다. 카메라는 XR 원점(위치 + 요)일
// 뿐이라, 넘기지 않으면 고개를 돌려도 소리의 좌우가 바뀌지 않습니다. 손 자세와 같은 기준(원점 × 자세)입니다.
void Engine::applyViewPose(FrameState& f) {
    HPROF_SCOPE("Engine.ViewPose");
    viewOrigin_->applyToCamera(f.view, xr_.get(), world_);
    if (audio_) {
        const HXrInputState& xr  = HInputSystem::get().xr();
        CameraComponent*     cam = CameraComponent::activeIn(world_);
        if (xr.focused && xr.head.valid && cam)
            audio_->setListenerPose(cam->xrOrigin() * glm::translate(glm::mat4(1.f), xr.head.position)
                                    * glm::mat4_cast(xr.head.orientation));
        else
            audio_->clearListenerPose();
    }
}

// 4. 프레임 시간 · 창 크기 · 마우스. 오른쪽 버튼을 뷰포트 안에서 누르면 카메라 모드(커서 잡기)입니다.
void Engine::readDesktopInput(FrameState& f, LoopState& loop) {
    HPROF_SCOPE("Engine.DesktopInput");
    IWindow& win = *window_;
    const auto now = LoopState::Clock::now();
    f.dt      = std::chrono::duration<float>(now - loop.prev).count();
    loop.prev = now;
    f.dt      = std::min(f.dt, cvMaxDt_ ? cvMaxDt_->asFloat() : 0.05f);   // time.maxDt(기본 0.05)
    if (options_.fixedDt > 0.f) f.dt = options_.fixedDt;   // 결정적 확인(애니메이션 D9)
    renderer_->advanceTime(f.dt);

    win.getWindowSize(f.winW, f.winH);
    win.getMousePosition(f.mx, f.my);

    // LMB — viewport hit-test (pick / orbit gated to viewport)
    f.lmb          = win.isMouseButtonDown(HInput::MOUSE_BUTTON_LEFT);
    f.lmbJustUp    = !f.lmb && loop.prevLDown;
    loop.prevLDown = f.lmb;

    // RMB → camera mode
    const bool rmb = win.isMouseButtonDown(HInput::MOUSE_BUTTON_RIGHT);
    if (rmb && !loop.prevRDown && frontend_->inViewport(f.mx, f.my, (float)f.winW, (float)f.winH)) win.activateCameraMode();
    if (!rmb && loop.prevRDown) win.deactivateCameraMode();
    loop.prevRDown = rmb;
}

// 5. Play 상태 전이. 게임 실행 파일은 헤드셋이 입력 초점을 잃으면(벗음 · 시스템 메뉴) 멈춥니다(2026-10-19, Meta VRC.Quest.Functional.2).
// 에디터의 Pause 와 같은 경로(월드 · 모듈 tick 정지, 소리 그룹 일시정지)이고, 초점이 돌아오면 이어서 갑니다.
void Engine::updatePlayState(FrameState& f, LoopState& loop) {
    HPROF_SCOPE("Engine.PlayState");
    IEngineFrontend& front = *frontend_;
    const bool focusLost = policy_.pauseOnXrFocusLoss && xr_ && xr_->hasSession() && !HInputSystem::get().xr().focused;
    if (focusLost != xrFocusPaused_) {
        xrFocusPaused_ = focusLost;
        HLOG(LogEngine, Log) << (focusLost ? "Paused: the headset does not have input focus (taken off or system menu)"
                                           : "Resumed: the headset has input focus");
    }
    const bool inGame = front.isPlaying() || front.isPaused();
    const bool paused = front.isPaused() || (front.isPlaying() && xrFocusPaused_);
    f.playing         = front.isPlaying() && !xrFocusPaused_;
    if (inGame && !loop.prevInGame) {
        game_->onBeginPlay(*ctx_);
        modules_.beginPlayAll();
    }
    if (!inGame && loop.prevInGame) {
        game_->onEndPlay();
        modules_.endPlayAll();
        // Play 동안 건 타이머 · 바꾼 시간 배율을 남기지 않습니다(에디터는 같은 월드를 씀, D2).
        world_.timers().clear();
        world_.setTimeScale(cvTimeScale_ ? cvTimeScale_->asFloat() : 1.f);
    }
    // 일시정지는 시작·끝 처리 뒤에 알립니다. Pause 상태에서 곧바로 Stop 하면 끝 처리가
    // 먼저 사운드를 멈추므로, 뒤따르는 재개 통지가 남은 소리를 다시 들려주지 않습니다.
    if (paused != loop.prevPaused) modules_.pauseChangedAll(paused);
    loop.prevInGame = inGame;
    loop.prevPaused = paused;
}

// 1-b. 백그라운드에서 읽은 모델을 프레임마다 정한 예산만큼 올리고(비동기 로딩 D2, Unity 기본 2 ms 와 같은 값), 준비된 씬 전환을
// 바꿉니다(D6). 이번 프레임의 시점(카메라 · 리그 포인터)을 고르기 전에 해야, 바꾸며 지운 액터를 가리키지 않습니다.
void Engine::streamAssets() {
    HPROF_SCOPE("Engine.StreamAssets");
    if (assets_) assets_->pump(kUploadBudgetMs);
    {
        HPROF_SCOPE("Engine.SceneTransition");
        frontend_->updateSceneTransition();
    }
    cvars_.dispatchChanges();   // 지난 프레임에 바뀐 CVar 의 콜백(콘솔 · 게임 코드, D6)
    updateUserSettings();
}

// 6. 틱. 앞단(에디터 UI) → 디버그 선 비우기 → 입력 → 월드 · 게임 · 모듈 → 따라오는 패널 → 월드 UI 포인터.
void Engine::tickFrame(FrameState& f) {
    ScopedCpuTimer t(gFrameTimings().tick);
    if (!f.minimized) {
        HPROF_SCOPE("Frontend.Tick");
        frontend_->tick(f.dt);
    }
    // 디버그 선은 한 프레임용입니다. 에디터는 tick 에서 앞 프레임의 선을 가져가 그리므로, 그 뒤 · 월드 틱 전에
    // 비웁니다. 예전에는 에디터만 비워, 에디터가 없거나(게임 실행 파일) 최소화로 에디터 tick 을 건너뛰면 선이
    // 끝없이 쌓였습니다(2026-10-19).
    hdebug::lines().clear();
    HInputSystem::get().update(f.dt, *window_, frontend_->isTextInputActive());
    if (f.playing) {
        {
            ScopedCpuTimer tw(gFrameTimings().worldTick);
            world_.tick(f.dt);                      // 시간 배율을 곱해 진행(Docs/Gameplay_Tools.md D1)
            game_->onTick(world_.lastDt(), *ctx_);   // 배율 1 이면 f.dt 와 같음
        }
        {
            ScopedCpuTimer tm(gFrameTimings().modules);
            modules_.tickAll(world_.lastDt());
        }
        {
            HPROF_SCOPE("World.TickPostPhysics");
            world_.tickPostPhysics();   // 물리 뒤 컴포넌트(D5). 없으면 훑지 않음
        }
        if (cvDebugLines_ && !cvDebugLines_->asBool()) hdebug::lines().clear();   // debug.lines 0: 이번 틱의 디버그 선을 버림
    }
    // 머리를 따라오는 패널(3D UI 구간 7)을 먼저 옮기고, 그 자세로 포인터가 가리킵니다.
    if (f.playing) worldUi_.updateFollowPanels(world_, xr_ && xr_->hasSession(), f.dt);
    // 월드 UI 패널(3D UI 구간 5 · 6): 틱이 옮긴 뒤의 자세로 가리키고, 앞단의 클릭 처리(handleInput)보다 먼저 알립니다.
    // 마우스는 XR 세션이 없을 때만, 창 없는 실행(--headless)에는 실제 마우스가 없어 끕니다(좌표 0,0 의 호버가
    // 캡처를 바꾸지 않게). XR 은 세션이 입력을 받는 동안 두 손 컨트롤러로 가리킵니다.
    {
        HPROF_SCOPE("WorldUi.Pointers");
        const bool xrLiveNow = xr_ && xr_->hasSession();
        WorldUiDriver::PointerInput pin;
        pin.mouseActive = f.playing && !f.minimized && !headless_ && !xrLiveNow;
        pin.xrActive    = f.playing && xrLiveNow;
        pin.mx = f.mx; pin.my = f.my; pin.lmb = f.lmb; pin.winW = f.winW; pin.winH = f.winH; pin.dt = f.dt;
        worldUi_.updatePointers(*frontend_, world_, physics_, *renderer_, pin);
    }
    // 마우스 시선은 이번 프레임 것만 씁니다(에디터가 다음 tick 에 다시 넣음).
    HInputSystem::get().setLookDelta(0.f, 0.f);
}

// 7. 앞단 입력(단축키는 EditorApp::tickShortcuts 에서 처리).
void Engine::frontendInput(FrameState& f) {
    if (f.minimized) return;
    ScopedCpuTimer t(gFrameTimings().handleInput);
    frontend_->handleInput(f.mx, f.my, f.lmb, f.lmbJustUp, 0.f, (float)f.winW, (float)f.winH);
}

// 8. 렌더. XR: 눈 자세를 이 프레임에 한 번 받아 컬링 · 렌더 · 제출에 함께 씁니다(DEF-32). 헤드셋이 화면을
// 보여 주지 않거나(벗음 등) 추적을 잃으면 그 프레임은 데스크톱만 그리고 빈 프레임을 제출합니다.
// 합성 눈(--synthetic-eyes)은 XR 세션 대신 렌더러가 만든 이미지에 같은 경로로 그립니다. 눈은 스테레오 흉내와 같은
// 두 평행한 눈이고, 원점은 XR 과 같습니다(씬 리그면 리그 액터 + 눈높이, 아니면 카메라 위치 + 요).
void Engine::renderFrame(FrameState& f) {
    HPROF_SCOPE("Engine.Render");
    Renderer& renderer = *renderer_;
    updateAccessibility(f);
    {
        HPROF_SCOPE("Engine.ExtractScene");
        extractRenderScene(world_, sceneInput_);
    }
    if (f.xrBegun) {
        XrFrameTarget target;
        bool acquired = false;
        if (f.xrFrame.shouldRender && xr_->locateViews(f.xrFrame.displayTime, target.eyes))
            acquired = xr_->acquireImage(target.imageIndex);
        const bool rendered = renderer.drawFrame(sceneInput_, acquired ? &target : nullptr);
        if (acquired) xr_->releaseImage();
        xr_->endFrame(f.xrFrame.displayTime, acquired && rendered);
        if (acquired && rendered) ++drawnFrames_;
    } else if (synthetic_) {
        static_assert(kMaxViews == 2, "synthetic eyes are a stereo pair");
        const float tanUp  = std::tan(glm::radians(f.view.cam ? f.view.cam->fovY : 45.f) * 0.5f);
        const float aspect = (float)options_.syntheticEyeW / (float)options_.syntheticEyeH;
        const auto* rigC   = f.view.rig ? f.view.rig->getComponent<XrRigComponent>() : nullptr;
        const std::array<XrEyePose, 2> eyes = hxr::syntheticEyes(hxr::kSyntheticIpd, tanUp, aspect,
                                                                 rigC ? rigC->eyeHeight : 0.f);
        XrFrameTarget target;
        target.imageIndex = syntheticImage_;
        target.eyes[0]    = eyes[0];
        target.eyes[1]    = eyes[1];
        f.drewSynthetic = renderer.drawFrame(sceneInput_, &target);
        if (f.drewSynthetic) {
            lastSynthetic_  = syntheticImage_;
            syntheticImage_ = (syntheticImage_ + 1) % renderer.syntheticImageCount();
            ++drawnFrames_;
        }
    } else {
        const bool wantCapture = !options_.capturePath.empty() &&
                                 (options_.frames <= 0 || drawnFrames_ + 1 >= options_.frames);
        renderer.setCaptureNextSwapchain(wantCapture);
        renderer.drawFrame(sceneInput_);
        if (renderer.lastFrameDesktop()) ++drawnFrames_;
    }
}

// 8 앞. 접근성(2026-11-21, Docs/Accessibility.md): 이동 컴포넌트가 알린 속도로 비네트 세기를 갱신하고, Play 중에는 자막 큐의 시간을
// 진행합니다. 자막이 보이면 화면 카메라 기준으로 자리를 정합니다: XR 세션이 머리를 주면 머리 고정(시선 아래), 합성 눈은 그 눈과 같은
// 원점(XR 원점 + 리그 눈높이)의 머리 고정, 아니면 화면 아래입니다.
void Engine::updateAccessibility(const FrameState& f) {
    HPROF_SCOPE("Engine.Accessibility");
    access_.tickVignette(f.dt);
    if (cvVignetteDebug_ && cvVignetteDebug_->asFloat() > 0.f) access_.setVignette(cvVignetteDebug_->asFloat());
    if (f.playing) subtitles_.update(f.dt);
    subtitlePanel_.update(subtitles_, access_.access);
    if (!subtitlePanel_.visible()) return;
    CameraComponent* cam = f.view.cam ? f.view.cam : CameraComponent::activeIn(world_);
    if (!cam || !cam->owner) return;
    glm::vec3 pos = cam->owner->worldPosition();
    glm::quat rot = cam->owner->worldRotation();
    bool      headLocked = false;
    auto fromMatrix = [&](const glm::mat4& m) {
        pos = glm::vec3(m[3]);
        rot = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                                       glm::normalize(glm::vec3(m[2]))));
        headLocked = true;
    };
    const HXrInputState& xr = HInputSystem::get().xr();
    if (xr_ && xr_->hasSession() && xr.focused && xr.head.valid) {
        fromMatrix(cam->xrOrigin() * glm::translate(glm::mat4(1.f), xr.head.position) * glm::mat4_cast(xr.head.orientation));
    } else if (synthetic_) {
        const auto* rigC = f.view.rig ? f.view.rig->getComponent<XrRigComponent>() : nullptr;
        fromMatrix(cam->xrOrigin() * glm::translate(glm::mat4(1.f), glm::vec3(0.f, rigC ? rigC->eyeHeight : 0.f, 0.f)));
    }
    const float aspect = synthetic_ ? (float)options_.syntheticEyeW / (float)std::max(options_.syntheticEyeH, 1u)
                       : (f.winW > 0 && f.winH > 0 ? (float)f.winW / (float)f.winH : 16.f / 9.f);
    subtitlePanel_.place(pos, rot, headLocked, cam->fovY, aspect);
}

// 9. 루프 끝: 자동 실행 끝내기 · FPS 제목 · 프레임 통계 · 최소화 중 쉼.
void Engine::endLoop(FrameState& f, LoopState& loop) {
    HPROF_SCOPE("Engine.EndLoop");
    IWindow& win = *window_;
    trace_.onFrameDrawn(drawnFrames_);   // --trace-frames(Docs/CPU_Profiling.md D6)
    // 개발 스위치(크래시 보고 D5): 세 프레임을 그린 뒤 한 번 일으킵니다. throw 는 main 이 잡아 보고서를 쓰고, ensure 는 계속 돕니다.
    // 개발 스위치(비동기 로딩 L3): 정한 프레임을 그린 뒤 씬 전환을 요청합니다.
    while (switchNext_ < options_.switchScenes.size() && drawnFrames_ >= options_.switchScenes[switchNext_].second) {
        const auto& sw = options_.switchScenes[switchNext_++];
        if (!frontend_->requestScene(sw.first, options_.switchSync)) {
            HLOG(LogEngine, Warning) << "--switch-scene: " << sw.first << " was not accepted (game executable only)";
        }
    }
    // 개발 스위치(게임플레이 도구 D9): 정한 프레임을 그린 뒤 콘솔 명령을 실행합니다(에디터 콘솔 줄과 같은 길).
    for (; consoleNext_ < options_.consoleAt.size() && drawnFrames_ >= options_.consoleAt[consoleNext_].frame; ++consoleNext_)
        cvars_.executeLogged(options_.consoleAt[consoleNext_].command);
    // 개발 스위치(저장 · 설정 S3-6): 정한 프레임을 그린 뒤 사용자 설정을 바꿉니다(다음 프레임 처음에 적용).
    for (; settingNext_ < options_.userSettingsAt.size() && drawnFrames_ >= options_.userSettingsAt[settingNext_].frame; ++settingNext_) {
        const auto& us = options_.userSettingsAt[settingNext_];
        std::string why;
        if (!userSettings_.set(us.name, us.value, &why)) {
            HLOG(LogEngine, Warning) << "--user-setting-at: " << why;
        } else {
            HLOG(LogEngine, Log) << "--user-setting-at: frame " << drawnFrames_ << " set " << us.name << " = " << us.value;
        }
    }
    if (!options_.debugCrash.empty() && !debugCrashDone_ && drawnFrames_ >= 3) {
        debugCrashDone_ = true;
        hcrash::debugCrash(options_.debugCrash);
    }
    if (!loop.closeAsked && options_.frames > 0 && drawnFrames_ >= options_.frames) {
        loop.closeAsked = true;
        win.setShouldClose(true);
    }
    if (!loop.closeAsked && loop.timeLimitSec > 0.f &&
        std::chrono::duration<float>(LoopState::Clock::now() - loop.runStart).count() > loop.timeLimitSec) {
        loop.closeAsked = true;
        timedOut_       = true;
        HLOG(LogEngine, Warning) << "Run time limit " << loop.timeLimitSec << " s reached after drawing " << drawnFrames_
                                 << " frames; closing";
        win.setShouldClose(true);
    }

    // FPS 제목(0.5 초마다). VR 을 원했는데 쓸 수 없거나(런타임 · 헤드셋 없음) 연결이 끊겨 기다리는 중이면 제목에 알립니다(2026-10-19).
    const char* vrNote = (xrWanted_ && !xrSession_->usable()) ? "  |  VR unavailable"
                       : xrSession_->retryPending()           ? "  |  VR disconnected" : "";
    stats_.updateTitle(win, frontend_->windowTitle(), vrNote, headless_);

    // 루프 1 회 전체입니다. 이것이 실제 프레임 간격이며, 위 구간들의 합과
    // 크게 벌어지면 그 차이가 곧 "어느 계측기에도 잡히지 않는 시간"입니다.
    const float loopMs = std::chrono::duration<float, std::milli>(FrameState::Clock::now() - f.loopT0).count();
    gFrameTimings().loopTotal.add(loopMs);
    // 프레임 시간 분포와 끊김 로그(2026-11-18, 성능 안정화 1-1). XR 세션 · 합성 눈 프레임만 셉니다.
    if (f.xrBegun || f.drewSynthetic)
        stats_.recordFrame(loopMs, f.xrBegun, f.drewSynthetic, f.xrBegun ? xr_->displayRefreshRate() : 0.f, drawnFrames_,
                           renderer_->getTimings().totalMs);

    // 최소화 중에는 그리지 않아 루프가 코어 하나를 다 쓰므로 쉬어 갑니다(약 60Hz, dt 는 실제 시간이라
    // 시뮬레이션 속도는 그대로). 헤드셋이 돌면 xrWaitFrame 이 속도를 정하므로 쉬지 않습니다. 합성 눈을 그린 프레임도
    // 쉬지 않습니다(창 없는 실행은 언제나 0x0 이라, 쉬면 자동 확인이 프레임마다 15ms 씩 느려짐).
    if (f.minimized && !f.xrBegun && !f.drewSynthetic)
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
}

uint32_t Engine::validationErrorCount() { return Renderer::validationErrorCount(); }

int Engine::finishRun() {
    trace_.finish();   // 루프가 끝났으니 기록 중인 추적을 씁니다(D6)
    if (!hopt::isAutomated(options_)) return hopt::kExitOk;

    bool blank = false;
    std::string captureNote = "no capture";
    if (!options_.capturePath.empty()) {
        std::vector<uint8_t> rgba;
        VkExtent2D           ext{};
        bool captured    = false;
        bool unsupported = false;
        if (synthetic_) {
            if (lastSynthetic_ == UINT32_MAX) {
                captureNote = "capture skipped (needs at least one drawn synthetic frame)";
                blank       = true;
            } else if (!renderer_->captureXrEye(lastSynthetic_, options_.captureEye, rgba, ext)) {
                captureNote = "capture failed";
                blank       = true;
            } else {
                captured = true;
            }
        } else {
            if (drawnFrames_ == 0) {
                captureNote = "capture skipped (no drawn frames)";
                blank       = true;
            } else if (!renderer_->captureSwapchain(rgba, ext, unsupported)) {
                if (unsupported) {
                    captureNote = "capture skipped (swapchain does not support TRANSFER_SRC)";
                } else {
                    captureNote = "capture failed";
                    blank       = true;
                }
            } else {
                captured = true;
            }
        }
        if (captured) {
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
            const char* eyeNote = synthetic_ && options_.captureEye ? " (right eye, " : " (";
            captureNote = (ok ? "capture " : "capture not written: ") + options_.capturePath + eyeNote +
                          std::to_string(ext.width) + "x" + std::to_string(ext.height) + (blank ? ", blank" : "") + ")";
            if (!ok) blank = true;
        }
    }
    frontend_->onRunFinished();
    stats_.logPacing("run");
    // GPU 구간 시간(타임스탬프, 지수 이동 평균). 요약 줄의 형식은 그대로 두고 따로 남깁니다(패스 비용 비교용).
    {
        const Renderer::GpuTimings t = renderer_->getTimings();
        char buf[192];
        std::snprintf(buf, sizeof(buf), "GPU ms: shadow %.3f, main %.3f, ao %.3f, bloom %.3f, tonemap %.3f, total %.3f",
                      t.shadowMs, t.mainMs, t.aoMs, t.bloomMs, t.tonemapMs, t.totalMs);
        HLOG(LogEngine, Log) << buf;
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
