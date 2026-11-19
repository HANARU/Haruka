#pragma once
// 검사 매크로(크래시 보고 D1, Docs/Crash_Reporting.md). UE 의 check · checkf · ensure · checkSlow 와 같은 자리입니다.
//
//   HCHECK(식)              모든 구성에서 켜짐. 거짓이면 Fatal 로그 → 크래시 보고서(CrashHandler) → 끝.
//   HCHECK_MSG(식, 스트림)   위와 같고 메시지를 붙임: HCHECK_MSG(i < n, "index " << i << " of " << n);
//   HENSURE(식)             식의 참 · 거짓을 돌려줌. 거짓이면 그 자리에서 처음 한 번만 Error 로그 + 콜 스택을 남기고 계속합니다.
//                           if (!HENSURE(mesh)) return;   디버거가 붙어 있으면 멈춥니다. 자동 실행은 종료 코드 5 로 알립니다.
//   HCHECK_SLOW(식)         Debug 만(NDEBUG 이면 식을 계산하지 않음). 프레임마다 도는 곳의 예전 assert 자리입니다.
//
// 파일 이름은 컴파일할 때 "src/" 뒤(또는 마지막 폴더 이름부터)만 남깁니다. __FILE__ 의 절대 경로가 실행 파일에 들어가지 않게 합니다
// (배포 파일에 빌드 PC 의 경로가 남지 않음).

#include <atomic>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>

namespace hcrash {

[[noreturn]] void checkFailed(const char* expr, const char* file, int line, const std::string& message);
// 처음 실패한 자리면 보고합니다. 언제나 false 를 돌려줍니다(HENSURE 의 값).
bool ensureFailed(const char* expr, const char* file, int line, std::atomic<bool>& reported);

namespace detail {

// "…/src/core/X.cpp" → "core/X.cpp". 표지가 없으면 마지막 두 단계("Source/X.cpp" 등).
constexpr size_t trimPos(std::string_view f) {
    constexpr std::string_view keys[] = { "/src/", "\\src\\", "/tests/", "\\tests\\", "/Projects/", "\\Projects\\" };
    size_t best = std::string_view::npos;
    size_t bestLen = 0;
    for (std::string_view k : keys) {
        const size_t at = f.rfind(k);
        if (at != std::string_view::npos && (best == std::string_view::npos || at > best)) { best = at; bestLen = k.size(); }
    }
    if (best != std::string_view::npos) return best + bestLen;
    size_t slashes = 0;
    for (size_t i = f.size(); i > 0; --i)
        if ((f[i - 1] == '/' || f[i - 1] == '\\') && ++slashes == 2) return i;
    return 0;
}

template <size_t N>
struct FixedName { char s[N] = {}; };

template <size_t N>
constexpr FixedName<N> makeName(std::string_view v) {
    FixedName<N> out;
    for (size_t i = 0; i + 1 < N && i < v.size(); ++i) out.s[i] = v[i] == '\\' ? '/' : v[i];
    return out;
}

} // namespace detail
} // namespace hcrash

// 줄인 파일 이름(정적 저장, 실행 파일에는 줄인 글만 들어감).
#define HCRASH_FILE                                                                                                   \
    ([]() -> const char* {                                                                                            \
        constexpr std::string_view hcrashFull_ = __FILE__;                                                            \
        constexpr size_t           hcrashPos_  = ::hcrash::detail::trimPos(hcrashFull_);                              \
        static constexpr auto hcrashName_ = ::hcrash::detail::makeName<hcrashFull_.size() - hcrashPos_ + 1>(hcrashFull_.substr(hcrashPos_)); \
        return hcrashName_.s;                                                                                         \
    }())

#define HCHECK(expr)                                                                       \
    do {                                                                                   \
        if (!(expr)) [[unlikely]] ::hcrash::checkFailed(#expr, HCRASH_FILE, __LINE__, {}); \
    } while (0)

#define HCHECK_MSG(expr, msg)                                                       \
    do {                                                                            \
        if (!(expr)) [[unlikely]] {                                                 \
            std::ostringstream hcrashMsg_;                                          \
            hcrashMsg_ << msg;                                                      \
            ::hcrash::checkFailed(#expr, HCRASH_FILE, __LINE__, hcrashMsg_.str());  \
        }                                                                           \
    } while (0)

#define HENSURE(expr)                                                                                       \
    ((expr) ? true                                                                                          \
            : ::hcrash::ensureFailed(#expr, HCRASH_FILE, __LINE__,                                          \
                                     []() -> std::atomic<bool>& { static std::atomic<bool> r{ false }; return r; }()))

#ifdef NDEBUG
#define HCHECK_SLOW(expr) ((void)sizeof(!(expr)))
#else
#define HCHECK_SLOW(expr) HCHECK(expr)
#endif
