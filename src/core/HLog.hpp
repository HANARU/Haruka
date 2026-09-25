#pragma once
// ── 로그 카테고리 ──────────────────────────────────────────────────────────────
// 설계 정본: Docs/Log_Category_Design.md
//
// 핵심은 "로그를 끈다" 를 두 장치로 나누는 것입니다.
//   - 기록 수준(이 파일): 카테고리마다 어디까지 남길지. 꺼진 로그는 << 오른쪽 인자조차
//     평가하지 않고, 링 버퍼·콘솔·파일 어디에도 가지 않습니다. 모든 스레드가 읽습니다.
//   - 표시 필터(에디터 Output Log): 이미 기록된 것 중 무엇을 화면에 보일지.
// 둘을 한 스위치로 합치지 마십시오. 화면에서 숨긴 로그가 파일에서도 사라집니다.
//
// 선언:
//   inline constinit HLogCategory LogRenderer{ "Renderer", ELogVerbosity::Log };   // 여러 파일이 공유
//   static constinit HLogCategory LogHotReload{ "HotReload", ELogVerbosity::Log }; // 한 파일 전용
// 사용:
//   HLOG(LogRenderer, Warning) << "megabuffer usage " << pct << "%";
//
// 줄 끝에 "\n" 을 쓰지 않습니다. 레코드 하나가 한 줄이고, 메시지 안의 개행은 제출할 때
// 줄마다 레코드로 나뉩니다.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// 숫자가 작을수록 심각합니다. 기록 수준 L 은 "L 이하의 로그만 남긴다" 는 뜻이고,
// Off 는 기록 수준에만 쓰입니다(로그 자체의 심각도로 쓰면 제출되지 않습니다).
enum class ELogVerbosity : uint8_t { Off = 0, Error = 1, Warning = 2, Log = 3, Verbose = 4 };

const char* logVerbosityName(ELogVerbosity v);
// logVerbosityName 의 역. 대소문자를 구분하지 않습니다.
bool        parseLogVerbosity(std::string_view name, ELogVerbosity& out);

// 이름 하나당 하나씩 레지스트리가 소유합니다. 해제하지 않습니다(정적 소멸 중의 로그 대비).
struct HLogCategoryState {
    std::string           name;
    ELogVerbosity         defaultLevel = ELogVerbosity::Log;
    std::atomic<uint8_t>  level        { (uint8_t)ELogVerbosity::Log };  // 쓰기는 메인 스레드만
    uint16_t              index        = 0;                              // 등록 순서
    std::atomic<uint32_t> errors       { 0 };   // 부팅 이후 누계. 링에서 밀려나도 남습니다
    std::atomic<uint32_t> warnings     { 0 };

    ELogVerbosity currentLevel() const {
        return (ELogVerbosity)level.load(std::memory_order_relaxed);
    }
};

class HLogCategory {
public:
    // constexpr 이라 constinit 으로 선언할 수 있습니다. 전역 객체의 동적 초기화 순서는
    // 번역 단위 사이에서 정해져 있지 않지만, 상수 초기화에는 그 문제가 없습니다.
    // 레지스트리 등록은 isEnabled 의 첫 호출에서 일어납니다.
    constexpr HLogCategory(const char* name, ELogVerbosity defaultLevel) noexcept
        : name_(name), default_(defaultLevel) {}

    HLogCategory(const HLogCategory&)            = delete;
    HLogCategory& operator=(const HLogCategory&) = delete;

    bool isEnabled(ELogVerbosity v) const noexcept {
        return v != ELogVerbosity::Off &&
               (uint8_t)v <= state()->level.load(std::memory_order_relaxed);
    }

    // 같은 이름으로 선언된 카테고리는 모두 같은 상태를 가리킵니다.
    HLogCategoryState* state() const noexcept {
        HLogCategoryState* s = state_.load(std::memory_order_acquire);
        return s ? s : resolve();
    }

    const char* name() const noexcept { return name_; }

private:
    HLogCategoryState* resolve() const noexcept;

    const char*                             name_;
    ELogVerbosity                           default_;
    mutable std::atomic<HLogCategoryState*> state_ { nullptr };
};

struct HLogRecord {
    uint64_t      seq       = 0;       // 1부터 단조 증가. 소비자는 마지막으로 본 seq 만 기억합니다
    float         time      = 0.f;     // 로그 체계 첫 사용 후 경과 초
    uint16_t      category  = 0;       // HLogCategoryState::index
    ELogVerbosity verbosity = ELogVerbosity::Log;
    std::string   text;                // 접두어·개행을 뺀 본문(UTF-8 원문)
};

namespace HLog {

inline constexpr size_t kCapacity = 2048;

// 레코드를 링 버퍼와 출력처(콘솔·logcat)에 전달합니다. 기록 수준은 검사하지 않습니다
// (HLOG 매크로가 이미 검사했습니다). 어느 스레드에서나 안전합니다.
void submit(const HLogCategory& cat, ELogVerbosity v, std::string_view text);

// ── 소비 API ──────────────────────────────────────────────────────────────────
uint64_t latestSeq();
// afterSeq 보다 뒤의 레코드를 out 에 덧붙입니다. 반환값은 이미 링에서 밀려나 주지 못한 개수입니다.
size_t   copySince(uint64_t afterSeq, std::vector<HLogRecord>& out);
// 심각도가 maxVerbosity 이하인 가장 최근 레코드. 상태바가 Verbose 에 덮이지 않게 하는 용도입니다.
bool     latestAtMost(ELogVerbosity maxVerbosity, HLogRecord& out);

// ── 레지스트리 ────────────────────────────────────────────────────────────────
// 실행 중 이름으로 카테고리를 찾거나 만듭니다(기본 수준 Log). 반환된 참조는 영구히 유효합니다.
HLogCategory&                   category(std::string_view name);
std::vector<HLogCategoryState*> categories();                 // 등록 순서
HLogCategoryState*              categoryAt(uint16_t index);   // 범위 밖이면 nullptr

// ── 출력처와 표준 출력 가로채기 ───────────────────────────────────────────────
void setConsoleSinkEnabled(bool enabled);   // 테스트용. 기본은 켜짐
// std::cout / std::cerr 의 streambuf 를 어댑터로 바꿉니다. 한 줄이 끝날 때마다 [Tag] 접두어를
// 떼어 같은 이름의 카테고리로 제출하고, 접두어가 없으면 Stdout 카테고리로 보냅니다.
// cout 은 Log, cerr 는 Warning 입니다. 콘솔 출력은 교체 전의 원래 streambuf 로 나갑니다.
// main() 에서 한 번 설치하고, 다른 스레드가 모두 멈춘 뒤 해제합니다.
void installStdStreamCapture();
void uninstallStdStreamCapture();
// 어댑터가 한 줄을 처리하는 경로입니다. 테스트가 직접 호출합니다.
void feedLegacyLine(std::string_view line, bool fromStderr);

// UTF-8 파일 출력처. 실행마다 새로 쓰고, 줄마다 flush 합니다(크래시 직전 로그 보존).
// 형식은 콘솔과 같되 앞에 경과 시간이 붙습니다. 상위 폴더가 없으면 만듭니다.
bool openFileSink(const std::string& path);
void closeFileSink();

// ── 기록 수준 보존 ────────────────────────────────────────────────────────────
// 부팅(main)이 읽고 에디터가 씁니다. 두 곳이 서로 다른 경로를 보면 설정이 조용히 사라지므로
// 경로는 여기 한 곳에만 둡니다.
inline constexpr const char* kLevelOverridesPath = "Config/log_levels.txt";
// 파일 형식: 한 줄에 "카테고리 수준" (예: "Input Verbose"). '#' 줄은 주석입니다.
// 아직 등록되지 않은 이름은 보관해 두었다가, 그 카테고리가 등록되는 순간에 적용합니다
// (카테고리는 첫 사용 시점에 등록되므로 부팅 시점에는 대부분 아직 없습니다).
bool loadLevelOverrides(const std::string& path);
// 선언 기본값과 다른 수준만 씁니다. 이번 실행에서 한 번도 등록되지 않아 적용 대기 중인
// 항목도 지우지 않고 그대로 보존합니다.
bool saveLevelOverrides(const std::string& path);
// 모든 카테고리를 선언 기본값으로 되돌리고, 적용 대기 중인 항목도 버립니다. 대기 항목을
// 남기면 사용자가 되돌린 뒤에 그 카테고리가 처음 쓰이는 순간 예전 수준이 되살아납니다.
void resetLevels();

// HLOG 매크로의 임시 객체. 문장이 끝날 때 소멸자가 제출합니다.
class Line {
public:
    Line(const HLogCategory& cat, ELogVerbosity v) : cat_(cat), v_(v) {}
    ~Line() { submit(cat_, v_, oss_.str()); }

    Line(const Line&)            = delete;
    Line& operator=(const Line&) = delete;

    template <class T>
    Line& operator<<(const T& value) { oss_ << value; return *this; }
    Line& operator<<(std::ostream& (*manip)(std::ostream&))       { oss_ << manip; return *this; }
    Line& operator<<(std::ios_base& (*manip)(std::ios_base&))     { oss_ << manip; return *this; }

private:
    const HLogCategory& cat_;
    ELogVerbosity       v_;
    std::ostringstream  oss_;
};

} // namespace HLog

// 꺼져 있으면 << 오른쪽 식을 평가하지 않습니다(비용은 atomic 읽기와 분기 하나).
// if {} else 형태라 호출부를 감싼 바깥 if/else 와 결합이 꼬이지 않습니다.
// 켜진 로그는 힙 할당을 동반하므로, 매 프레임 경로에서는 Verbose 로만 남기십시오.
#define HLOG(Cat, Verb)                                              \
    if (!(Cat).isEnabled(ELogVerbosity::Verb)) {} else               \
        HLog::Line((Cat), ELogVerbosity::Verb)

// 엔진 전역 카테고리. 서브시스템 카테고리는 각자의 헤더나 cpp 에 선언합니다.
inline constinit HLogCategory LogEngine{ "Engine", ELogVerbosity::Log };
