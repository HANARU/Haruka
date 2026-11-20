#include "Profiler.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <system_error>

namespace hprof {

namespace {

// 스레드 하나의 버퍼. 잠금은 주인 스레드(emit)와 수집(start · stop)만 잡으므로 평소에는 다툼이 없습니다(D3).
struct Buffer {
    std::mutex         mu;
    uint32_t           tid = 0;
    std::string        name;
    std::vector<Event> events;
    uint64_t           dropped = 0;
    bool               alive   = true;   // 스레드가 끝나면 false(수집한 뒤 정리)
};

struct Registry {
    std::mutex                           mu;
    std::vector<std::shared_ptr<Buffer>> buffers;
    uint32_t                             nextTid = 1;
    uint64_t                             startNs = 0;
    std::atomic<size_t>                  maxEvents{ kDefaultMaxEventsPerThread };
};

Registry& registry() {
    static Registry r;
    return r;
}

// 스레드가 끝나도 버퍼는 레지스트리가 잡고 있어 수집할 때까지 남습니다(IBL 면별 스레드처럼 짧은 스레드).
struct ThreadHandle {
    std::shared_ptr<Buffer> buf;
    ~ThreadHandle() {
        if (!buf) return;
        std::lock_guard<std::mutex> lk(buf->mu);
        buf->alive = false;
    }
};

Buffer& threadBuffer() {
    thread_local ThreadHandle handle;
    if (!handle.buf) {
        auto b = std::make_shared<Buffer>();
        Registry& r = registry();
        std::lock_guard<std::mutex> lk(r.mu);
        b->tid = r.nextTid++;
        r.buffers.push_back(b);
        handle.buf = std::move(b);
    }
    return *handle.buf;
}

// 끝난 스레드의 버퍼를 지웁니다. r.mu 를 잡은 채로 부릅니다.
void pruneDead(Registry& r) {
    r.buffers.erase(std::remove_if(r.buffers.begin(), r.buffers.end(),
                                   [](const std::shared_ptr<Buffer>& b) {
                                       std::lock_guard<std::mutex> lk(b->mu);
                                       return !b->alive;
                                   }),
                    r.buffers.end());
}

void appendEscaped(std::string& out, const char* s) {
    if (!s) return;
    for (; *s; ++s) {
        const unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c < 0x20) {
            char u[8];
            std::snprintf(u, sizeof u, "\\u%04x", (unsigned)c);
            out += u;
        }
        else out += (char)c;
    }
}

// 나노초를 마이크로초(소수 셋째 자리)로. 정수 계산이라 로캘 · 반올림과 상관없습니다.
void appendMicros(std::string& out, uint64_t ns) {
    char b[48];
    std::snprintf(b, sizeof b, "%llu.%03u", (unsigned long long)(ns / 1000), (unsigned)(ns % 1000));
    out += b;
}

} // namespace

size_t Capture::eventCount() const {
    size_t n = 0;
    for (const ThreadTrack& t : threads) n += t.events.size();
    return n;
}

uint64_t Capture::droppedCount() const {
    uint64_t n = 0;
    for (const ThreadTrack& t : threads) n += t.dropped;
    return n;
}

bool start(size_t maxEventsPerThread) {
    Registry& r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    if (recording()) return false;
    pruneDead(r);
    for (const auto& b : r.buffers) {
        std::lock_guard<std::mutex> bl(b->mu);
        b->events.clear();
        b->dropped = 0;
    }
    r.maxEvents.store(maxEventsPerThread > 0 ? maxEventsPerThread : 1, std::memory_order_relaxed);
    r.startNs = nowNs();
    detail::gRecording.store(true, std::memory_order_release);
    return true;
}

Capture stop() {
    Capture c;
    Registry& r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    if (!recording()) return c;
    detail::gRecording.store(false, std::memory_order_release);
    c.startNs = r.startNs;
    c.endNs   = nowNs();
    for (const auto& b : r.buffers) {
        std::lock_guard<std::mutex> bl(b->mu);
        if (b->events.empty() && b->dropped == 0) continue;
        ThreadTrack t;
        t.tid     = b->tid;
        t.name    = b->name.empty() ? "Thread " + std::to_string(b->tid) : b->name;
        t.events  = std::move(b->events);
        t.dropped = b->dropped;
        b->events = {};
        b->dropped = 0;
        c.threads.push_back(std::move(t));
    }
    pruneDead(r);
    std::sort(c.threads.begin(), c.threads.end(), [](const ThreadTrack& a, const ThreadTrack& b) { return a.tid < b.tid; });
    return c;
}

void emit(const char* name, uint64_t t0, uint64_t t1) {
    if (!recording()) return;
    Buffer& b = threadBuffer();
    const size_t cap = registry().maxEvents.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(b.mu);
    if (b.events.size() >= cap) { ++b.dropped; return; }
    if (b.events.capacity() == 0) b.events.reserve(std::min<size_t>(4096, cap));
    b.events.push_back({ name, t0, t1 });
}

void setThreadName(std::string_view name) {
    Buffer& b = threadBuffer();
    std::lock_guard<std::mutex> lk(b.mu);
    b.name.assign(name.data(), name.size());
}

std::string toChromeJson(const Capture& capture) {
    std::string out;
    out.reserve(256 + capture.eventCount() * 96);
    out += "{\"displayTimeUnit\":\"ms\",\"otherData\":{\"producer\":\"Haruka\",\"droppedEvents\":";
    out += std::to_string(capture.droppedCount());
    out += ",\"durationUs\":";
    appendMicros(out, capture.endNs > capture.startNs ? capture.endNs - capture.startNs : 0);
    out += "},\"traceEvents\":[\n";
    out += "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"tid\":0,\"args\":{\"name\":\"Haruka\"}}";
    std::vector<const Event*> order;
    for (const ThreadTrack& t : capture.threads) {
        const std::string tid = std::to_string(t.tid);
        out += ",\n{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":" + tid + ",\"args\":{\"name\":\"";
        appendEscaped(out, t.name.c_str());
        out += "\"}}";
        out += ",\n{\"name\":\"thread_sort_index\",\"ph\":\"M\",\"pid\":1,\"tid\":" + tid + ",\"args\":{\"sort_index\":" + tid + "}}";
        // 같은 시각에 시작한 부모(더 긴 것)가 자식보다 먼저 오게(D5).
        order.clear();
        for (const Event& e : t.events) order.push_back(&e);
        std::stable_sort(order.begin(), order.end(), [](const Event* a, const Event* b) {
            return a->t0 != b->t0 ? a->t0 < b->t0 : a->t1 > b->t1;
        });
        for (const Event* e : order) {
            // 기록을 켜기 전에 시작한 구간(ScopedCpuTimer)은 시작 시각으로 자릅니다.
            const uint64_t t0 = std::max(e->t0, capture.startNs);
            const uint64_t t1 = std::max(e->t1, t0);
            out += ",\n{\"name\":\"";
            appendEscaped(out, e->name);
            out += "\",\"cat\":\"cpu\",\"ph\":\"X\",\"pid\":1,\"tid\":" + tid + ",\"ts\":";
            appendMicros(out, t0 - capture.startNs);
            out += ",\"dur\":";
            appendMicros(out, t1 - t0);
            out += '}';
        }
    }
    out += "\n]}\n";
    return out;
}

bool writeChromeJson(const std::filesystem::path& path, const Capture& capture, std::string* why) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (why) *why = "cannot open " + path.generic_string();
        return false;
    }
    const std::string text = toChromeJson(capture);
    f.write(text.data(), (std::streamsize)text.size());
    if (!f) {
        if (why) *why = "cannot write " + path.generic_string();
        return false;
    }
    return true;
}

} // namespace hprof
