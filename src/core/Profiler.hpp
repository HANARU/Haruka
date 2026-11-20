#pragma once
// CPU 구간 계측(2026-11-20, Docs/CPU_Profiling.md D1 ~ D9).
//
// 이름 붙인 중첩 스코프를 스레드별 버퍼에 기록하고, 모아서 Chrome Trace Event JSON(chrome://tracing · Perfetto UI)으로 씁니다.
//
//   void Foo::update() {
//       HPROF_FUNCTION();                    // 함수 이름(__func__)
//       { HPROF_SCOPE("Foo.Collide"); ... }  // 이름은 정적 수명(글자 그대로 등). 포인터만 남깁니다
//   }
//   HPROF_THREAD_NAME("AssetWorker");       // 스레드 표시 이름(복사함)
//
// 컴파일 스위치: HARUKA_PROFILER(기본 1, CMake 옵션 HARUKA_PROFILER). 0 이면 매크로가 static_cast<void>(0) 이 되어 비용이 없습니다.
// 켠 빌드도 기록 중이 아니면 스코프마다 원자 변수 한 번 읽기뿐이고 시간을 읽지 않습니다. 기록은 hprof::start · stop 이 켜고 끕니다
// (엔진은 --trace · trace.start 콘솔 명령).
//
// 진단용 프로세스 전역입니다(HLog 와 같은 자리, D9). 게임 · 렌더링 로직이 이 기록에 의존하게 만들지 마십시오.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifndef HARUKA_PROFILER
#define HARUKA_PROFILER 1
#endif

namespace hprof {

// 기록 하나. 시각은 steady_clock 의 나노초입니다(파일에 쓸 때 시작 시각을 0 으로).
struct Event {
    const char* name = nullptr;
    uint64_t    t0   = 0;
    uint64_t    t1   = 0;
};

struct ThreadTrack {
    uint32_t           tid = 0;   // 등록 순서(1 부터)
    std::string        name;      // HPROF_THREAD_NAME, 없으면 "Thread <tid>"
    std::vector<Event> events;    // 끝난 순서
    uint64_t           dropped = 0;
};

struct Capture {
    uint64_t                 startNs = 0, endNs = 0;
    std::vector<ThreadTrack> threads;   // tid 순
    size_t   eventCount() const;
    uint64_t droppedCount() const;
};

namespace detail {
inline std::atomic<bool> gRecording{ false };
}

inline bool recording() { return detail::gRecording.load(std::memory_order_relaxed); }

inline uint64_t toNs(std::chrono::steady_clock::time_point t) {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}
inline uint64_t nowNs() { return toNs(std::chrono::steady_clock::now()); }

// 스레드당 기본 상한(D3). 넘으면 버리고 셉니다.
inline constexpr size_t kDefaultMaxEventsPerThread = 1u << 20;

// 기록을 시작합니다. 이미 기록 중이면 false. 모든 스레드 버퍼를 비웁니다.
bool start(size_t maxEventsPerThread = kDefaultMaxEventsPerThread);
// 기록을 멈추고 모은 것을 돌려줍니다(기록 중이 아니었으면 빈 것). 끝난 스레드의 버퍼는 여기서 정리합니다.
Capture stop();
// 기록 중이면 이 스레드의 버퍼에 넣습니다. name 은 정적 수명이어야 합니다.
void emit(const char* name, uint64_t t0, uint64_t t1);
// 이 스레드의 표시 이름(기록 여부와 상관없이 기억합니다).
void setThreadName(std::string_view name);

// Chrome Trace Event JSON(D5).
std::string toChromeJson(const Capture& capture);
bool writeChromeJson(const std::filesystem::path& path, const Capture& capture, std::string* why = nullptr);

// HPROF_SCOPE 가 만드는 RAII 스코프. 기록 중일 때만 시간을 읽습니다.
class Scope {
public:
    explicit Scope(const char* name) noexcept : name_(recording() ? name : nullptr), t0_(name_ ? nowNs() : 0) {}
    ~Scope() { if (name_) emit(name_, t0_, nowNs()); }
    Scope(const Scope&)            = delete;
    Scope& operator=(const Scope&) = delete;
private:
    const char* name_;
    uint64_t    t0_;
};

} // namespace hprof

#define HPROF_CONCAT_INNER(a, b) a##b
#define HPROF_CONCAT(a, b)       HPROF_CONCAT_INNER(a, b)

#if HARUKA_PROFILER
#define HPROF_SCOPE(name)       ::hprof::Scope HPROF_CONCAT(hprofScope_, __LINE__)(name)
#define HPROF_FUNCTION()        HPROF_SCOPE(__func__)
#define HPROF_THREAD_NAME(name) ::hprof::setThreadName(name)
#else
#define HPROF_SCOPE(name)       static_cast<void>(0)
#define HPROF_FUNCTION()        static_cast<void>(0)
#define HPROF_THREAD_NAME(name) static_cast<void>(0)
#endif
