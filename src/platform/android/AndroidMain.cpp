// Android(Quest) 진입점(2026-10-31, 패키징 3단계). 데스크톱의 main.cpp 와 같은 일(쓰기 위치 · 로그 · 옵션 · Engine)을
// NativeActivity 의 android_main 에서 합니다. HarukaGame 의 .so 에만 들어갑니다.
//
// 순서는 Khronos hello_xr 과 Meta 예제를 따릅니다: 스레드를 JavaVM 에 붙이고 → 폴더 · 로그 → 액티비티가 RESUME · 창을 받을
// 때까지 이벤트를 처리(UI 스레드가 그 명령의 처리를 기다리므로 오래 막으면 ANR) → Engine 실행 → 끝나면 액티비티를 끝내 달라고
// 하고 프로세스를 끝냅니다. 같은 프로세스가 android_main 을 다시 부르면 정적 상태(로그 · 레지스트리 · 물리 팩토리)가 남아
// 있으므로, android_main 은 돌아가지 않고 _exit 로 끝냅니다.
//
// 끝낼 때 DESTROY 까지 기다리지 않습니다(2026-11-01, DEF-154). 보이던 액티비티의 DESTROY 는 다음 액티비티(Home)가 쉴 때까지
// (최대 약 10초) 미뤄질 수 있고, 예전에는 그 사이 다시 실행하면 같은 프로세스에 새 액티비티가 생겨 이 스레드의 _exit 에 함께
// 끝났습니다. 지금은 시스템이 끝내기를 받았다는 첫 표시(PAUSE · STOP · DESTROY)까지만, 최대 1초 기다립니다. 끝내는 중인
// 액티비티의 프로세스가 끝나는 것은 시스템이 정상으로 처리합니다(크래시 대화상자 없음).
//
// 개발 스위치: 명령줄이 없으므로 <외부 파일 폴더>/Config/args.txt 의 공백 · 줄로 나눈 옵션(--frames=600 등)을 읽습니다
// (adb push 로 넣음). XR 은 언제나 켜고(Quest 에는 데스크톱 화면이 없음), 첫 실행은 소리 장치를 열지 않습니다(2026-10-31 결정).

#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../core/AppPaths.hpp"
#include "../../core/Engine.hpp"
#include "../../core/EngineOptions.hpp"
#include "../../core/FileUtil.hpp"
#include "../../core/HLog.hpp"
#include "../../core/IEngineFrontend.hpp"

namespace {

// 액티비티 상태. 처리기는 android_main 이 두고 끝까지 바꾸지 않습니다(AndroidWindow 의 pollEvents 도 같은 처리기로 감).
struct AppState {
    bool resumed     = false;
    bool hasWindow   = false;
    bool finishing   = false;   // ANativeActivity_finish 를 불렀음
    bool finishSeen  = false;   // 그 뒤 시스템이 PAUSE · STOP · DESTROY 중 하나를 보냄(끝내기를 받음)
};

// android_main 에 들어온 횟수(DEF-154). 두 번째부터는 앞 실행이 끝나는 중인 같은 프로세스입니다.
std::atomic<int> gEntries{ 0 };

void onAppCmd(android_app* app, int32_t cmd) {
    auto* s = static_cast<AppState*>(app->userData);
    if (!s) return;
    switch (cmd) {
        case APP_CMD_RESUME:      s->resumed = true;    break;
        case APP_CMD_PAUSE:       s->resumed = false;   break;
        case APP_CMD_INIT_WINDOW: s->hasWindow = true;  break;
        case APP_CMD_TERM_WINDOW: s->hasWindow = false; break;
        default: break;
    }
    if (s->finishing && (cmd == APP_CMD_PAUSE || cmd == APP_CMD_STOP || cmd == APP_CMD_DESTROY)) s->finishSeen = true;
}

void pumpOnce(android_app* app, int timeoutMs) {
    int events = 0;
    android_poll_source* source = nullptr;
    if (ALooper_pollOnce(timeoutMs, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0 && source)
        source->process(app, source);
}

// RESUME 와 창을 받을 때까지(최대 timeoutMs) 이벤트를 처리합니다.
void pumpUntilReady(android_app* app, const AppState& s, int timeoutMs) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!app->destroyRequested && !(s.resumed && s.hasWindow) && std::chrono::steady_clock::now() < end)
        pumpOnce(app, 50);
}

// PC 에서 만든 파일을 adb push 로 넣으므로 BOM 이 있는 UTF-8 · UTF-16 도 읽습니다(DEF-152, hfile::readConfigText).
std::vector<std::string> readArgsFile(const std::filesystem::path& path) {
    std::vector<std::string> args;
    std::string text, why;
    if (!hfile::readConfigText(path, text, &why)) {
        if (!why.empty()) { HLOG(LogEngine, Error) << "Config/args.txt was not read: " << why; }
        return args;
    }
    std::istringstream in(text);
    std::string token;
    while (in >> token) args.push_back(token);
    return args;
}

} // namespace

extern "C" void android_main(android_app* app) {
    // 같은 프로세스에 들어온 두 번째 실행입니다. 앞 실행의 정적 상태가 남아 있고 그 스레드가 곧 프로세스를 끝내므로, 이 실행은
    // 부팅하지 않고 바로 끝냅니다. 다시 누르면 새 프로세스로 시작합니다.
    if (gEntries.fetch_add(1) > 0) {
        __android_log_print(ANDROID_LOG_ERROR, "Haruka",
                            "android_main entered again while the previous run is closing; exiting (launch the app again)");
        _exit(1);
    }
    AppState state;
    app->userData = &state;
    app->onAppCmd = onAppCmd;

    // OpenXR 로더 · 런타임이 이 스레드에서 JNI 를 씁니다(activity->env 는 UI 스레드 것이라 쓸 수 없음).
    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);

    // 콘텐츠 · 쓰기 폴더는 앱의 외부 파일 폴더(/sdcard/Android/data/<패키지>/files)입니다. 묶음 파일은 adb push 로 넣고,
    // 그 폴더에 넣을 수 없는 기기를 위해 OBB 폴더도 찾습니다.
    const char* ext = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
    const std::filesystem::path files = ext ? std::filesystem::path(ext) : std::filesystem::path("/data/local/tmp");
    happ::setPlatformDirs(files, files, "HarukaGame");
    if (app->activity->obbPath) happ::addPackDir(app->activity->obbPath);

    std::unique_ptr<IEngineFrontend> frontend = createEngineFrontend();
    happ::initWriteRoot(frontend->policy().userDataFolder);   // 폴더는 위에서 정함(아무것도 하지 않음)
    const std::string logPath = happ::inWriteRoot("Logs/" + happ::exeStem() + ".log").string();
    const bool logFileOk = HLog::openFileSink(logPath);
    std::string levelsWhy;
    HLog::loadLevelOverrides(happ::inWriteRoot(HLog::kLevelOverridesPath).string(), &levelsWhy);
    HLog::installStdStreamCapture();
    if (!levelsWhy.empty()) { HLOG(LogEngine, Warning) << HLog::kLevelOverridesPath << " was not read: " << levelsWhy; }
    HLOG(LogEngine, Log) << "Android: write folder " << happ::writeRoot().generic_string()
                         << (app->activity->obbPath ? std::string(", OBB folder ") + app->activity->obbPath : std::string());
    if (!logFileOk) { HLOG(LogEngine, Warning) << "Could not open the log file " << logPath << "; logging to logcat only"; }

    pumpUntilReady(app, state, 3000);

    std::vector<std::string> unknown;
    EngineOptions options = hopt::parse(readArgsFile(happ::inWriteRoot("Config/args.txt")), unknown);
    for (const auto& a : unknown) HLOG(LogEngine, Warning) << "Unknown option in Config/args.txt ignored: " << a;
    options.xr         = true;
    options.xrRequired = true;
    options.noAudio    = true;
    options.platform   = { app, app->activity->vm, app->activity->clazz };

    int exitCode = 0;
    try {
        Engine engine(options, std::move(frontend));
        exitCode = engine.run();
    } catch (const std::exception& e) {
        HLOG(LogEngine, Error) << "FATAL: " << e.what();
        exitCode = 1;
    }
    HLOG(LogEngine, Log) << "Android: engine finished (exit code " << exitCode << ")";
    HLog::uninstallStdStreamCapture();
    HLog::closeFileSink();

    // 끝내기를 시스템이 받을 때까지만 이벤트를 처리합니다(DEF-154, 위 머리 주석). UI 스레드는 PAUSE 처리를 기다리므로 그것은
    // 처리해 주고, DESTROY 는 기다리지 않습니다.
    if (!app->destroyRequested) {
        state.finishing = true;
        ANativeActivity_finish(app->activity);
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        while (!app->destroyRequested && !state.finishSeen && std::chrono::steady_clock::now() < end) pumpOnce(app, 50);
    }
    app->activity->vm->DetachCurrentThread();
    _exit(exitCode);
}
