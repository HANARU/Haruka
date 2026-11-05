#include "core/Engine.hpp"
#include "core/EngineOptions.hpp"
#include "core/HLog.hpp"
#include "core/AppPaths.hpp"

#include <filesystem>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
// NOMINMAX 가 없으면 windows.h 의 min/max 매크로가 다른 곳의 std::min/std::max 를
// 망가뜨린다. LEAN_AND_MEAN 은 여기서 쓰지 않는 winsock/OLE 헤더를 걷어낸다.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#endif

// ── Headless stdio ────────────────────────────────────────────────────────────
// 윈도우 빌드는 /SUBSYSTEM:WINDOWS 로 링크되어 콘솔 창을 열지 않는다. 그러면 실행
// 파일을 직접 띄웠을 때 stdout 이 갈 곳이 없고, 엔진 전체에 흩어진 std::cout /
// std::cerr 출력이 조용히 사라진다.
//
// 다만 run.bat 은 exe 를 Tee-Object 로 파이프에 물려 Logs/latest.txt 를 만든다.
// 그때 stdout 은 유효한 파이프 핸들이므로 이 우회가 끼어들면 안 된다. 끼어들면
// tee 가 받을 것이 없어져 로그 파일이 빈 채로 남는다. 그래서 핸들이 실제로 없을
// 때에만 파일로 돌린다.
//
// 엔진 로그의 정본은 이제 HLog 의 파일 출력처(Logs/<실행 파일 이름>.log)다. 이 우회는 HLog 를
// 거치지 않는 C stdio 출력(서드파티의 printf·fprintf)만 받으므로 파일 이름을 따로 둔다.
// 우회가 일어나면 true 를 돌려주고, 호출부는 콘솔 출력처를 꺼서 같은 줄이 두 파일에
// 중복되지 않게 한다. Logs 는 쓰기 루트 아래다(에디터는 실행 폴더, 게임은 사용자 폴더, 2026-10-19).
static bool redirectStdioIfHeadless() {
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE)
        return false;   // 콘솔이든 파이프든 받아줄 곳이 이미 있다

    std::error_code ec;
    const std::filesystem::path logs = happ::inWriteRoot("Logs");
    std::filesystem::create_directories(logs, ec);
    FILE* f = nullptr;
    if (_wfreopen_s(&f, (logs / "stdout.txt").c_str(), L"w", stdout) != 0 || f == nullptr)
        return false;
    // stderr 를 같은 파일에 따로 열면 두 스트림이 서로의 내용을 덮어쓴다.
    // 기술자를 복제해서 한 스트림을 공유하게 한다.
    _dup2(_fileno(stdout), _fileno(stderr));
    return true;
#else
    return false;
#endif
}

// 진입점은 프로세스 수준의 stdio 처리만 맡고, 엔진의 부팅·루프·teardown 은 전부
// Engine 이 소유합니다(core/Engine.hpp). 예전에는 이 함수가 200줄로 그 전부를
// 떠안고 있었습니다. 에디터 실행 파일과 게임 실행 파일이 이 파일을 함께 쓰고, 어느 앞단을
// 쓸지는 실행 파일마다 링크한 createEngineFrontend 가 정합니다(2026-10-19).
// 실행 옵션(core/EngineOptions.cpp). 모르는 인자는 경고만 남기고 무시합니다(로그 체계가 준비된 뒤에 알립니다).
//   --xr             OpenXR 런타임(Quest Link 등)을 준비합니다. 에디터는 Play 동안, 게임은 시작부터 헤드셋에 그립니다.
//                    런타임이나 헤드셋이 없으면 데스크톱으로 계속합니다.
//   --no-xr          VR 없이 실행합니다(게임 실행 파일의 프로젝트 설정 xr.startInVr 을 덮어씀).
//   --emulate-stereo 헤드셋 없이 두 눈 경로를 확인합니다(뷰포트를 좌우로 나눠 두 눈을 그림).
//   --loose          실행 폴더의 낱개 파일을 묶음 파일(game.hpak)보다 먼저 찾습니다(게임 실행 파일).
// 창 없는 실행과 자동 확인(2026-10-31, 배포 실행 파일에도 남김):
//   --headless             OS 창 없이 실행합니다(게임 실행 파일). 표면이 없어 XR 이나 합성 눈에만 그립니다.
//   --synthetic-eyes[=WxH] 헤드셋 없이 XR 프레임 경로로 두 눈을 그립니다(기본 512x512).
//   --frames=N             N 프레임을 그리면 끝냅니다. --timeout=S 초 안에 못 그리면 종료 코드 2(기본 60초).
//   --capture=path         끝낼 때 합성 눈 왼쪽 층을 PNG 로 남깁니다. 종료 코드 3 = 검증 오류(Debug), 4 = 빈 화면.
//   --vk-no-sync2          synchronization2 가 있어도 Vulkan 1.0 배리어로 기록합니다(Quest 의 대체 경로 시험, DEF-04).
//   --vk-dedicated-msaa    MSAA 컬러를 핵심 힙 밖 별도 할당으로 둡니다(Quest 의 지연 할당 경로를 PC 에서 시험, DEF-06).
//   --ssao · --no-ssao     화면 공간 AO 를 켜거나 끕니다(AO 를 뺀 비교 · 끔 경로 시험). 프로젝트 설정 render.ao 를
//                          덮어씁니다. 머티리얼 occlusion 텍스처는 그대로입니다.

int main(int argc, char** argv) {
    // 앞단(에디터 또는 게임)을 먼저 정합니다. 쓰기 루트(설정 · 로그 · 캐시를 둘 폴더)가 앞단 정책에 달려 있어,
    // 로그를 열기 전에 알아야 합니다(2026-10-19).
    std::unique_ptr<IEngineFrontend> frontend = createEngineFrontend();
    const bool writeRootOk = happ::initWriteRoot(frontend->policy().userDataFolder);

    // 표준 출력 핸들이 없어 파일로 돌렸는지입니다(창 없는 실행 --headless 와는 다른 뜻).
    const bool stdioRedirected = redirectStdioIfHeadless();   // setvbuf 보다 먼저. 새 스트림에 적용되어야 한다.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // 로그 체계는 엔진 부팅보다 먼저 준비합니다. 예전에는 EditorApp::init 이 표준 출력을
    // 가로채서, 그 전에 나온 부팅 로그(GPU·힙·오디오·물리 초기화)가 Output Log 에 없었습니다
    // (DEF-71). 파일 출력처는 실행 방법(run.bat·바로가기·디버거)과 무관하게 같은 UTF-8 파일을
    // 남깁니다. run.bat 의 Logs/latest.txt 는 PowerShell 이 UTF-16 으로 저장합니다.
    // 해제는 Engine 이 소멸해 다른 스레드가 모두 멈춘 뒤, 어느 경로로 나가든 일어납니다.
    if (stdioRedirected) HLog::setConsoleSinkEnabled(false);
    const std::string logPath = happ::inWriteRoot("Logs/" + happ::exeStem() + ".log").string();
    const bool logFileOk = HLog::openFileSink(logPath);
    std::string levelsWhy;
    HLog::loadLevelOverrides(happ::inWriteRoot(HLog::kLevelOverridesPath).string(), &levelsWhy);
    HLog::installStdStreamCapture();
    if (!levelsWhy.empty())
        HLOG(LogEngine, Warning) << HLog::kLevelOverridesPath << " was not read: " << levelsWhy;
    // 예전에는 로그 파일을 못 열어도 아무 표시가 없어, 쓸 수 없는 폴더에서 실행하면 로그 없이 조용히 돌았습니다.
    if (!writeRootOk)
        HLOG(LogEngine, Warning) << "Could not use the user data folder; settings, logs and caches go next to the executable";
    if (!logFileOk)
        HLOG(LogEngine, Warning) << "Could not open the log file " << logPath << "; the log is kept in memory only";
    if (!happ::writeRootIsExeDir())
        HLOG(LogEngine, Log) << "Write folder (settings, logs, caches): " << happ::writeRoot().generic_string();
    struct LogGuard {
        ~LogGuard() {
            HLog::uninstallStdStreamCapture();   // 남은 조각을 제출하므로 파일보다 먼저
            HLog::closeFileSink();
        }
    } logGuard;

    std::vector<std::string> unknownArgs;
    const EngineOptions options = hopt::parse(std::vector<std::string>(argv + (argc > 0 ? 1 : 0), argv + argc), unknownArgs);
    for (const auto& a : unknownArgs)
        HLOG(LogEngine, Warning) << "Unknown command line option ignored: " << a;

    int exitCode = EXIT_SUCCESS;
    uint32_t errorsBeforeTeardown = 0;
    try {
        Engine engine(options, std::move(frontend));   // 부팅(생성자) → 실행 → 소멸자에서 역순 teardown.
        exitCode = engine.run();                       // 자동 실행(--frames 등)의 실패는 0 이 아닌 값
        errorsBeforeTeardown = Engine::validationErrorCount();
    } catch (const std::exception& e) {
        HLOG(LogEngine, Error) << "FATAL: " << e.what();
        return EXIT_FAILURE;
    }
    // 정리(장치 · 인스턴스 파괴) 중에 난 검증 오류(지우지 않은 객체 등)도 자동 실행의 실패로 봅니다. 요약 줄은 정리 전에 남으므로
    // 따로 알립니다.
    if (exitCode == hopt::kExitOk && hopt::isAutomated(options) && Engine::validationErrorCount() > errorsBeforeTeardown) {
        HLOG(LogEngine, Error) << "Validation errors during shutdown: "
                               << Engine::validationErrorCount() - errorsBeforeTeardown << "; exit code "
                               << hopt::kExitValidation;
        exitCode = hopt::kExitValidation;
    }

    HLOG(LogEngine, Log) << "Clean exit.";
    return exitCode;
}
