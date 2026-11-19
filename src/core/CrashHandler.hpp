#pragma once
// 크래시 처리기(크래시 보고 D2 · D3, Docs/Crash_Reporting.md).
//
// install 뒤에 프로세스가 죽으면 <root>/<시각>_<이름>/ 에 crash.txt(이유 · 주소 · 콜 스택 · 최근 로그)와 crash.dmp(Windows 미니덤프)를 씁니다.
//   Windows: 처리되지 않은 예외 · std::terminate · 순수 가상 호출 · CRT 잘못된 인자 · abort. 보고서는 미리 띄운 도우미 스레드가 써서
//            스택 넘침에도 씁니다. Windows 오류 대화상자는 띄우지 않습니다(숨은 창 자동 실행이 멈추지 않게).
//   Android: SIGSEGV · SIGBUS · SIGFPE · SIGILL · SIGABRT 를 대체 스택에서 받아 글 보고서를 쓰고, 원래 처리기로 다시 보내 tombstone 도 남깁니다.
// 같은 폴더의 pending.txt 에 마지막 보고서 이름을 남겨 다음 실행이 알릴 수 있게 합니다(takePending). 보고서는 최근 keep 개만 남깁니다.

#include <cstdint>
#include <filesystem>
#include <string>

namespace hcrash {

struct Options {
    std::filesystem::path root;        // 보고서 폴더들이 들어갈 곳(<쓰기 루트>/Logs/Crashes)
    std::string           app = "app"; // 폴더 이름 끝(실행 파일 이름)
    size_t                keep = 10;   // 남길 보고서 수
};

// 한 번만 설치합니다(다시 부르면 무시하고 true). 실패하면 false(보고서 없이 계속 돔).
bool install(const Options& options);
bool installed();

// 보고서를 쓰고 프로세스를 끝냅니다(종료 코드 1). HCHECK 실패 · 개발 스위치가 씁니다. 설치 전이면 표준 오류에 쓰고 abort.
[[noreturn]] void fatal(const std::string& reason);

// 지금 스레드의 상태로 보고서를 쓰고 돌아옵니다(main 이 잡은 std::exception). 보고서 폴더를 돌려줍니다(못 쓰면 빈 경로).
std::filesystem::path writeReport(const std::string& reason);

// 마지막으로 쓴 보고서 폴더(없으면 빈 경로).
std::filesystem::path lastReport();

// 지난 실행이 남긴 보고서 폴더(없으면 빈 경로). 한 번 읽으면 지웁니다(pending.txt).
std::filesystem::path takePending(const std::filesystem::path& root);

// 지금 스레드의 콜 스택(함수 이름은 기호가 있을 때). skip 은 맨 위에서 뺄 프레임 수.
std::string callStack(int skip = 0);

// HENSURE 실패 수(같은 자리가 여러 번 실패하면 모두 셈).
uint32_t ensureCount();

// 개발 스위치(--debug-crash): segv · stack · purecall · abort · terminate · throw · check · ensure. 모르는 이름이면 false.
// throw 는 std::runtime_error 를 던지고, ensure 는 HENSURE 를 두 번 실패시킨 뒤 돌아옵니다. 나머지는 돌아오지 않습니다.
bool debugCrash(const std::string& kind);

} // namespace hcrash
