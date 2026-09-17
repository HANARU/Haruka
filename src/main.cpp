#include "core/Engine.hpp"
#include "core/HLog.hpp"

#include <stdexcept>
#include <cstdlib>
#include <cstdio>

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
static void redirectStdioIfHeadless() {
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE)
        return;   // 콘솔이든 파이프든 받아줄 곳이 이미 있다

    FILE* f = nullptr;
    if (freopen_s(&f, "HarukaEngine.log", "w", stdout) != 0 || f == nullptr)
        return;
    // stderr 를 같은 파일에 따로 열면 두 스트림이 서로의 내용을 덮어쓴다.
    // 기술자를 복제해서 한 스트림을 공유하게 한다.
    _dup2(_fileno(stdout), _fileno(stderr));
#endif
}

// 진입점은 프로세스 수준의 stdio 처리만 맡고, 엔진의 부팅·루프·teardown 은 전부
// Engine 이 소유합니다(core/Engine.hpp). 예전에는 이 함수가 200줄로 그 전부를
// 떠안고 있었습니다.
int main() {
    redirectStdioIfHeadless();   // setvbuf 보다 먼저. 새 스트림에 적용되어야 한다.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // 표준 출력 가로채기는 엔진 부팅보다 먼저 설치합니다. 예전에는 EditorApp::init 이
    // 설치해서, 그 전에 나온 부팅 로그(GPU·힙·오디오·물리 초기화)가 Output Log 에 없었습니다
    // (DEF-71). 해제는 Engine 이 소멸해 다른 스레드가 모두 멈춘 뒤, 어느 경로로 나가든 일어납니다.
    HLog::installStdStreamCapture();
    struct CaptureGuard { ~CaptureGuard() { HLog::uninstallStdStreamCapture(); } } captureGuard;

    try {
        Engine engine;   // 부팅(생성자) → 실행 → 소멸자에서 역순 teardown.
        engine.run();
    } catch (const std::exception& e) {
        HLOG(LogEngine, Error) << "FATAL: " << e.what();
        return EXIT_FAILURE;
    }

    HLOG(LogEngine, Log) << "Clean exit.";
    return EXIT_SUCCESS;
}
