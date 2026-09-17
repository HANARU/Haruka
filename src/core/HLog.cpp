#include "HLog.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <unordered_map>

#ifdef __ANDROID__
#include <android/log.h>
#endif

// 로그 체계 자신의 경고(같은 이름 카테고리의 기본 수준 충돌 등)와, 접두어 없는 표준 출력.
static constinit HLogCategory LogLogging{ "Logging", ELogVerbosity::Log };
static constinit HLogCategory LogStdout { "Stdout",  ELogVerbosity::Log };

const char* logVerbosityName(ELogVerbosity v) {
    switch (v) {
        case ELogVerbosity::Off:     return "Off";
        case ELogVerbosity::Error:   return "Error";
        case ELogVerbosity::Warning: return "Warning";
        case ELogVerbosity::Log:     return "Log";
        case ELogVerbosity::Verbose: return "Verbose";
    }
    return "?";
}

namespace {

// ── 레지스트리 ────────────────────────────────────────────────────────────────
// 락 순서 규칙: 레지스트리 락을 쥔 채로 로그를 남기지 않습니다. 제출 락(Sink::mtx)과는
// 서로 다른 뮤텍스이며, 두 락을 동시에 쥐는 경로가 없습니다.
struct Registry {
    std::mutex                                          mtx;
    std::vector<HLogCategoryState*>                     states;    // 등록 순서, 해제 안 함
    std::unordered_map<std::string, HLogCategoryState*> byName;
    std::unordered_map<std::string, HLogCategory*>      runtime;   // category(name) 가 만든 것
};

// 의도적 누수. 정적 소멸 중에 남는 로그가 이미 해제된 레지스트리를 건드리지 않게 합니다.
Registry& registry() {
    static Registry* r = new Registry();
    return *r;
}

HLogCategoryState* findOrCreate(std::string_view name, ELogVerbosity def, bool* conflict) {
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);
    const std::string key(name);
    if (auto it = r.byName.find(key); it != r.byName.end()) {
        if (conflict) *conflict = (it->second->defaultLevel != def);
        return it->second;
    }
    auto* s = new HLogCategoryState();
    s->name         = key;
    s->defaultLevel = def;
    s->level.store((uint8_t)def, std::memory_order_relaxed);
    s->index        = (uint16_t)r.states.size();
    r.states.push_back(s);
    r.byName.emplace(key, s);
    return s;
}

// ── 링 버퍼와 출력처 ──────────────────────────────────────────────────────────
struct Sink {
    std::mutex              mtx;
    std::vector<HLogRecord> ring = std::vector<HLogRecord>(HLog::kCapacity);  // mtx 보호
    size_t                  head  = 0;       // 다음에 쓸 칸
    size_t                  count = 0;
    uint64_t                seq   = 0;
    std::atomic<uint64_t>   latest { 0 };    // seq 의 락 없는 사본(소비자의 변경 감지용)
    // 가로채기를 설치하면 교체 전의 원래 streambuf 를 여기 둡니다. 비어 있으면 std::cout 의
    // 현재 버퍼로 씁니다. 설치 중에 쓰면 어댑터로 되돌아 들어와 이 뮤텍스를 재진입하므로,
    // 설치는 반드시 이 값을 먼저 채운 뒤 rdbuf 를 바꿉니다.
    std::streambuf*         out = nullptr;
    std::streambuf*         err = nullptr;
    bool                    console = true;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

Sink& sink() {
    static Sink* s = new Sink();
    return *s;
}

void writeConsoleLocked(Sink& k, const HLogCategoryState& st, ELogVerbosity v, std::string_view line) {
#ifdef __ANDROID__
    const int prio = v == ELogVerbosity::Error   ? ANDROID_LOG_ERROR
                   : v == ELogVerbosity::Warning ? ANDROID_LOG_WARN
                   : v == ELogVerbosity::Log     ? ANDROID_LOG_INFO
                                                 : ANDROID_LOG_VERBOSE;
    __android_log_print(prio, "Haruka", "[%s] %.*s", st.name.c_str(), (int)line.size(), line.data());
    (void)k;
#else
    if (!k.console) return;
    const bool      toErr  = (v == ELogVerbosity::Error || v == ELogVerbosity::Warning);
    std::streambuf* target = toErr ? (k.err ? k.err : std::cerr.rdbuf())
                                   : (k.out ? k.out : std::cout.rdbuf());
    if (!target) return;

    std::string s;
    s.reserve(st.name.size() + line.size() + 16);
    s += '[';
    s += st.name;
    s += "] ";
    if (v == ELogVerbosity::Error)   s += "Error: ";
    if (v == ELogVerbosity::Warning) s += "Warning: ";
    s += line;
    s += '\n';
    target->sputn(s.data(), (std::streamsize)s.size());
#endif
}

void pushLocked(Sink& k, const HLogCategoryState& st, ELogVerbosity v, std::string_view line, float t) {
    HLogRecord& r = k.ring[k.head];
    r.seq       = ++k.seq;
    r.time      = t;
    r.category  = st.index;
    r.verbosity = v;
    r.text.assign(line.data(), line.size());   // 칸의 문자열 버퍼를 재사용합니다
    k.head = (k.head + 1) % HLog::kCapacity;
    if (k.count < HLog::kCapacity) ++k.count;
    k.latest.store(k.seq, std::memory_order_relaxed);
    writeConsoleLocked(k, st, v, line);
}

// ── 표준 출력 어댑터 ──────────────────────────────────────────────────────────
// 원래 버퍼로 직접 전달하지 않습니다. 콘솔 출력은 제출 경로의 출력처가 맡습니다(두 번 찍힘 방지).
class LegacyStreamBuf : public std::streambuf {
public:
    explicit LegacyStreamBuf(bool fromStderr) : err_(fromStderr) {}

    // 개행 없이 남은 조각을 한 줄로 제출합니다(해제 직전에 호출).
    void flushPartial() {
        std::string rest;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            rest.swap(line_);
        }
        if (!rest.empty()) HLog::feedLegacyLine(rest, err_);
    }

protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        const char ch = traits_type::to_char_type(c);
        xsputn(&ch, 1);
        return c;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        // 완성된 줄은 락 밖에서 제출합니다. 제출 경로가 다른 락을 잡기 때문입니다.
        std::vector<std::string> done;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            for (std::streamsize i = 0; i < n; ++i) {
                if (s[i] == '\n') { done.emplace_back(); done.back().swap(line_); }
                else              line_ += s[i];
            }
        }
        for (const std::string& l : done) HLog::feedLegacyLine(l, err_);
        return n;
    }

    // std::flush / std::endl 이 반쯤 쓴 줄을 끊어 내보내지 않게 합니다.
    int sync() override { return 0; }

private:
    const bool  err_;
    std::mutex  mtx_;
    std::string line_;   // mtx_ 보호
};

struct Capture {
    LegacyStreamBuf* out = nullptr;
    LegacyStreamBuf* err = nullptr;
};

Capture& capture() {
    static Capture c;
    return c;
}

bool isTagChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '.';
}

} // namespace

// ── HLogCategory ──────────────────────────────────────────────────────────────
HLogCategoryState* HLogCategory::resolve() const noexcept {
    bool conflict = false;
    HLogCategoryState* s = findOrCreate(name_ ? name_ : "?", default_, &conflict);
    HLogCategoryState* expected = nullptr;
    const bool first = state_.compare_exchange_strong(expected, s, std::memory_order_acq_rel);
    // 레지스트리 락은 findOrCreate 안에서 이미 풀렸습니다.
    if (conflict && first)
        HLOG(LogLogging, Warning) << "category '" << s->name << "' is declared with different default "
                                  << "levels; keeping " << logVerbosityName(s->defaultLevel);
    return s;
}

// ── 제출 ──────────────────────────────────────────────────────────────────────
void HLog::submit(const HLogCategory& cat, ELogVerbosity v, std::string_view text) {
    if (v == ELogVerbosity::Off) return;
    HLogCategoryState& st = *cat.state();
    if (v == ELogVerbosity::Error)   st.errors.fetch_add(1, std::memory_order_relaxed);
    if (v == ELogVerbosity::Warning) st.warnings.fetch_add(1, std::memory_order_relaxed);

    Sink& k = sink();
    std::lock_guard<std::mutex> lock(k.mtx);
    const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - k.start).count();

    // 줄마다 레코드 하나. 끝의 개행은 빈 레코드를 만들지 않습니다.
    size_t pos = 0;
    for (;;) {
        const size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        pushLocked(k, st, v, line, t);
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
        if (pos >= text.size()) break;
    }
}

// ── 소비 ──────────────────────────────────────────────────────────────────────
uint64_t HLog::latestSeq() {
    return sink().latest.load(std::memory_order_relaxed);
}

size_t HLog::copySince(uint64_t afterSeq, std::vector<HLogRecord>& out) {
    Sink& k = sink();
    std::lock_guard<std::mutex> lock(k.mtx);
    if (k.count == 0 || afterSeq >= k.seq) return 0;

    const uint64_t oldest = k.seq - k.count + 1;
    uint64_t from    = afterSeq + 1;
    size_t   dropped = 0;
    if (from < oldest) { dropped = (size_t)(oldest - from); from = oldest; }

    out.reserve(out.size() + (size_t)(k.seq - from + 1));
    for (uint64_t s = from; s <= k.seq; ++s) {
        // 최신 레코드는 head-1 칸이고, seq 가 하나 작아질 때마다 한 칸씩 뒤로 갑니다.
        const size_t idx = (k.head + kCapacity - 1 - (size_t)(k.seq - s)) % kCapacity;
        out.push_back(k.ring[idx]);
    }
    return dropped;
}

bool HLog::latestAtMost(ELogVerbosity maxVerbosity, HLogRecord& out) {
    Sink& k = sink();
    std::lock_guard<std::mutex> lock(k.mtx);
    for (size_t i = 0; i < k.count; ++i) {
        const HLogRecord& r = k.ring[(k.head + kCapacity - 1 - i) % kCapacity];
        if ((uint8_t)r.verbosity <= (uint8_t)maxVerbosity) { out = r; return true; }
    }
    return false;
}

// ── 레지스트리 ────────────────────────────────────────────────────────────────
HLogCategory& HLog::category(std::string_view name) {
    HLogCategoryState* s = findOrCreate(name, ELogVerbosity::Log, nullptr);
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);
    HLogCategory*& slot = r.runtime[s->name];
    // 이름 문자열은 영구히 해제되지 않는 상태 객체가 소유하므로 포인터를 넘겨도 안전합니다.
    // 새 객체의 첫 resolve 는 같은 이름으로 같은 상태를 찾습니다.
    if (!slot) slot = new HLogCategory(s->name.c_str(), s->defaultLevel);
    return *slot;
}

std::vector<HLogCategoryState*> HLog::categories() {
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);
    return r.states;
}

HLogCategoryState* HLog::categoryAt(uint16_t index) {
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);
    return index < r.states.size() ? r.states[index] : nullptr;
}

// ── 출력처와 가로채기 ─────────────────────────────────────────────────────────
void HLog::setConsoleSinkEnabled(bool enabled) {
    Sink& k = sink();
    std::lock_guard<std::mutex> lock(k.mtx);
    k.console = enabled;
}

void HLog::installStdStreamCapture() {
    Capture& c = capture();
    if (c.out) return;
    Sink& k = sink();
    {
        std::lock_guard<std::mutex> lock(k.mtx);
        k.out = std::cout.rdbuf();
        k.err = std::cerr.rdbuf();
    }
    c.out = new LegacyStreamBuf(false);
    c.err = new LegacyStreamBuf(true);
    std::cout.rdbuf(c.out);
    std::cerr.rdbuf(c.err);
}

void HLog::uninstallStdStreamCapture() {
    Capture& c = capture();
    if (!c.out) return;
    Sink& k = sink();
    std::streambuf* origOut = nullptr;
    std::streambuf* origErr = nullptr;
    {
        std::lock_guard<std::mutex> lock(k.mtx);
        origOut = k.out;
        origErr = k.err;
    }
    // 먼저 스트림을 원래 버퍼로 돌려 새 쓰기가 어댑터를 거치지 않게 하고, 남은 조각을
    // 제출한 뒤(출력처는 아직 원래 버퍼를 가리킴) 출력처 대상을 비웁니다.
    std::cout.rdbuf(origOut);
    std::cerr.rdbuf(origErr);
    c.out->flushPartial();
    c.err->flushPartial();
    {
        std::lock_guard<std::mutex> lock(k.mtx);
        k.out = nullptr;
        k.err = nullptr;
    }
    delete c.out; c.out = nullptr;
    delete c.err; c.err = nullptr;
}

void HLog::feedLegacyLine(std::string_view line, bool fromStderr) {
    const ELogVerbosity v = fromStderr ? ELogVerbosity::Warning : ELogVerbosity::Log;

    const HLogCategory* cat  = &LogStdout;
    std::string_view    body = line;
    // "[Tag] 본문" 형태면 태그를 카테고리 이름으로 씁니다. 태그 문자가 아닌 것이 섞이면
    // 로그 본문의 대괄호일 수 있으므로 접두어로 보지 않습니다.
    if (line.size() >= 3 && line[0] == '[') {
        const size_t close = line.find(']');
        if (close != std::string_view::npos && close >= 2 && close <= 32) {
            const std::string_view tag = line.substr(1, close - 1);
            bool ok = true;
            for (char ch : tag) if (!isTagChar(ch)) { ok = false; break; }
            if (ok) {
                cat  = &HLog::category(tag);
                body = line.substr(close + 1);
                if (!body.empty() && body.front() == ' ') body.remove_prefix(1);
            }
        }
    }
    if (!cat->isEnabled(v)) return;
    submit(*cat, v, body);
}
