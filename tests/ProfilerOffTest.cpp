// CPU 구간 계측: 끈 번역 단위(2026-11-20, Docs/CPU_Profiling.md P1-5).
// 이 파일만 HARUKA_PROFILER 0 으로 컴파일합니다. 매크로가 빈 문장이면 constexpr 함수 안에서 쓸 수 있고(RAII 객체가 생기면
// 컴파일 오류), 식의 형이 void 입니다. ProfilerTest.cpp 가 기록 중에 profOffRun 을 불러 이벤트가 0 인지 봅니다.

#undef HARUKA_PROFILER
#define HARUKA_PROFILER 0
#include "../src/core/Profiler.hpp"

#include <type_traits>

namespace {

constexpr int offScoped(int x) {
    HPROF_SCOPE("Off.Scope");
    HPROF_FUNCTION();
    HPROF_THREAD_NAME("Off thread");
    return x * 2;
}

static_assert(offScoped(21) == 42, "HPROF_* must compile away in a HARUKA_PROFILER=0 translation unit");
static_assert(std::is_void_v<decltype(HPROF_SCOPE("Off.Type"))>, "a disabled HPROF_SCOPE is a void expression");

} // namespace

int profOffRun(int x) {
    HPROF_THREAD_NAME("Off thread");
    HPROF_SCOPE("Off.Runtime");
    return offScoped(x);
}
