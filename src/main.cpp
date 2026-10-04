#include "core/Engine.hpp"
#include "core/HLog.hpp"

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
// 엔진 로그의 정본은 이제 HLog 의 파일 출력처(Logs/HarukaEngine.log)다. 이 우회는 HLog 를
// 거치지 않는 C stdio 출력(서드파티의 printf·fprintf)만 받으므로 파일 이름을 따로 둔다.
// 우회가 일어나면 true 를 돌려주고, 호출부는 콘솔 출력처를 꺼서 같은 줄이 두 파일에
// 중복되지 않게 한다.
static bool redirectStdioIfHeadless() {
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE)
        return false;   // 콘솔이든 파이프든 받아줄 곳이 이미 있다

    CreateDirectoryA("Logs", nullptr);   // 이미 있으면 실패하지만 상관없다
    FILE* f = nullptr;
    if (freopen_s(&f, "Logs/stdout.txt", "w", stdout) != 0 || f == nullptr)
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
// 떠안고 있었습니다.
// 실행 옵션. 모르는 인자는 경고만 남기고 무시합니다(로그 체계가 준비된 뒤에 알립니다).
//   --xr             OpenXR 런타임(Quest Link 등)으로 헤드셋에 그립니다. 없으면 데스크톱으로 계속합니다.
//   --emulate-stereo 헤드셋 없이 두 눈 경로를 확인합니다(뷰포트를 좌우로 나눠 두 눈을 그림).
static EngineOptions parseOptions(int argc, char** argv, std::vector<std::string>& unknown) {
    EngineOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if      (a == "--xr")             o.xr            = true;
        else if (a == "--emulate-stereo") o.emulateStereo = true;
        else                              unknown.push_back(a);
    }
    return o;
}

int main(int argc, char** argv) {
    const bool headless = redirectStdioIfHeadless();   // setvbuf 보다 먼저. 새 스트림에 적용되어야 한다.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // 로그 체계는 엔진 부팅보다 먼저 준비합니다. 예전에는 EditorApp::init 이 표준 출력을
    // 가로채서, 그 전에 나온 부팅 로그(GPU·힙·오디오·물리 초기화)가 Output Log 에 없었습니다
    // (DEF-71). 파일 출력처는 실행 방법(run.bat·바로가기·디버거)과 무관하게 같은 UTF-8 파일을
    // 남깁니다. run.bat 의 Logs/latest.txt 는 PowerShell 이 UTF-16 으로 저장합니다.
    // 해제는 Engine 이 소멸해 다른 스레드가 모두 멈춘 뒤, 어느 경로로 나가든 일어납니다.
    if (headless) HLog::setConsoleSinkEnabled(false);
    HLog::openFileSink("Logs/HarukaEngine.log");
    HLog::loadLevelOverrides(HLog::kLevelOverridesPath);
    HLog::installStdStreamCapture();
    struct LogGuard {
        ~LogGuard() {
            HLog::uninstallStdStreamCapture();   // 남은 조각을 제출하므로 파일보다 먼저
            HLog::closeFileSink();
        }
    } logGuard;

    std::vector<std::string> unknownArgs;
    const EngineOptions options = parseOptions(argc, argv, unknownArgs);
    for (const auto& a : unknownArgs)
        HLOG(LogEngine, Warning) << "Unknown command line option ignored: " << a;

    try {
        Engine engine(options);   // 부팅(생성자) → 실행 → 소멸자에서 역순 teardown.
        engine.run();
    } catch (const std::exception& e) {
        HLOG(LogEngine, Error) << "FATAL: " << e.what();
        return EXIT_FAILURE;
    }

    HLOG(LogEngine, Log) << "Clean exit.";
    return EXIT_SUCCESS;
}
