// CPU 구간 계측 시험(2026-11-20, 정본 Docs/CPU_Profiling.md 3절 P1).
// 중첩 · 스레드 구분 · 시작 / 멈춤 / 상한 · Chrome trace JSON(작은 파서로 다시 읽음) · 끈 번역 단위(ProfilerOffTest.cpp) ·
// FrameTimings 와 공유하는 측정 지점 · 추적 파일 경로 규칙 · 콘솔 명령 · 프레임 구간 기록을 창 · GPU 없이 검산합니다.

#include "../src/core/AppPaths.hpp"
#include "../src/core/CVar.hpp"
#include "../src/core/FrameTimings.hpp"
#include "../src/core/Profiler.hpp"
#include "../src/core/TraceRecorder.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

int profOffRun(int x);   // ProfilerOffTest.cpp(HARUKA_PROFILER 0)

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed;
    else ++g_failed;
}

// 시간이 조금 흐르게(스코프 길이가 0 이 아니게).
void spin(int us) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
    while (std::chrono::steady_clock::now() < end) {}
}

// ── 작은 JSON 파서(시험용) ──────────────────────────────────────────────────────
struct Json {
    enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
    bool                        b = false;
    double                      n = 0.0;
    std::string                 s;
    std::vector<Json>           a;
    std::map<std::string, Json> o;
    const Json* get(const std::string& k) const {
        const auto it = o.find(k);
        return it == o.end() ? nullptr : &it->second;
    }
};

struct Parser {
    const std::string& src;
    size_t             i  = 0;
    bool               ok = true;

    void ws() { while (i < src.size() && (src[i] == ' ' || src[i] == '\n' || src[i] == '\r' || src[i] == '\t')) ++i; }
    bool eat(char c) {
        ws();
        if (i < src.size() && src[i] == c) { ++i; return true; }
        return false;
    }
    std::string str() {
        std::string out;
        if (!eat('"')) { ok = false; return out; }
        while (i < src.size() && src[i] != '"') {
            char c = src[i++];
            if ((unsigned char)c < 0x20) { ok = false; return out; }   // 이스케이프하지 않은 제어 문자
            if (c != '\\') { out += c; continue; }
            if (i >= src.size()) { ok = false; return out; }
            c = src[i++];
            switch (c) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (i + 4 > src.size()) { ok = false; return out; }
                const unsigned v = (unsigned)std::stoul(src.substr(i, 4), nullptr, 16);
                i += 4;
                if (v < 0x80) out += (char)v; else ok = false;   // 시험에는 ASCII 제어 문자만
                break;
            }
            default: ok = false; return out;
            }
        }
        if (i >= src.size()) { ok = false; return out; }
        ++i;
        return out;
    }
    Json value() {
        Json j;
        ws();
        if (i >= src.size()) { ok = false; return j; }
        const char c = src[i];
        if (c == '{') {
            ++i;
            j.t = Json::T::Obj;
            if (eat('}')) return j;
            do {
                std::string k = str();
                if (!eat(':')) { ok = false; return j; }
                j.o[k] = value();
            } while (ok && eat(','));
            if (!eat('}')) ok = false;
        } else if (c == '[') {
            ++i;
            j.t = Json::T::Arr;
            if (eat(']')) return j;
            do { j.a.push_back(value()); } while (ok && eat(','));
            if (!eat(']')) ok = false;
        } else if (c == '"') {
            j.t = Json::T::Str;
            j.s = str();
        } else if (src.compare(i, 4, "true") == 0) { j.t = Json::T::Bool; j.b = true; i += 4; }
        else if (src.compare(i, 5, "false") == 0) { j.t = Json::T::Bool; i += 5; }
        else if (src.compare(i, 4, "null") == 0) { i += 4; }
        else {
            const size_t st = i;
            if (i < src.size() && src[i] == '-') ++i;
            while (i < src.size() && ((src[i] >= '0' && src[i] <= '9') || src[i] == '.' || src[i] == 'e' || src[i] == 'E' ||
                                      src[i] == '+' || src[i] == '-')) ++i;
            if (st == i) { ok = false; return j; }
            j.t = Json::T::Num;
            j.n = std::stod(src.substr(st, i - st));
        }
        return j;
    }
};

bool parseJson(const std::string& text, Json& out) {
    Parser p{ text };
    out = p.value();
    p.ws();
    return p.ok && p.i == text.size();
}

const hprof::ThreadTrack* trackNamed(const hprof::Capture& c, const std::string& name) {
    for (const auto& t : c.threads) if (t.name == name) return &t;
    return nullptr;
}

const hprof::Event* eventNamed(const hprof::ThreadTrack& t, const std::string& name) {
    for (const auto& e : t.events) if (e.name && name == e.name) return &e;
    return nullptr;
}

size_t countNamed(const hprof::Capture& c, const std::string& name) {
    size_t n = 0;
    for (const auto& t : c.threads)
        for (const auto& e : t.events) if (e.name && name == e.name) ++n;
    return n;
}

// ── P1-1 중첩 ─────────────────────────────────────────────────────────────────
void nestedWork() {
    HPROF_FUNCTION();
    {
        HPROF_SCOPE("Test.Inner");
        spin(50);
    }
    {
        HPROF_SCOPE("Test.Inner2");
        spin(20);
    }
}

void testNesting() {
    std::printf("\n== P1-1 nesting ==\n");
    HPROF_THREAD_NAME("Main");
    check(hprof::start(), "start");
    {
        HPROF_SCOPE("Test.Outer");
        spin(10);
        nestedWork();
    }
    const hprof::Capture c = hprof::stop();
    const hprof::ThreadTrack* m = trackNamed(c, "Main");
    check(m && m->events.size() == 4, "메인 스레드 이벤트 4개(Outer · 함수 · Inner · Inner2)");
    if (!m || m->events.size() != 4) return;
    // 끝난 순서대로: Inner, Inner2, 함수, Outer
    check(std::string(m->events[0].name) == "Test.Inner" && std::string(m->events[1].name) == "Test.Inner2" &&
          std::string(m->events[2].name) == "nestedWork" && std::string(m->events[3].name) == "Test.Outer",
          "끝난 순서로 기록, HPROF_FUNCTION 은 함수 이름");
    const auto& in = m->events[0];
    const auto& in2 = m->events[1];
    const auto& fn = m->events[2];
    const auto& out = m->events[3];
    check(fn.t0 >= out.t0 && fn.t1 <= out.t1 && in.t0 >= fn.t0 && in.t1 <= fn.t1 && in2.t0 >= fn.t0 && in2.t1 <= fn.t1,
          "자식 구간이 부모 안");
    check(in.t1 <= in2.t0, "형제는 겹치지 않음");
    check(in.t1 - in.t0 >= 50000, "Inner 길이 >= 50 us");
    check(c.startNs <= out.t0 && out.t1 <= c.endNs, "모든 구간이 기록 시간 안");

    // 파일에서는 시작 오름차순, 같은 시작이면 긴 것(부모) 먼저.
    const std::string json = hprof::toChromeJson(c);
    const size_t po = json.find("\"Test.Outer\""), pf = json.find("\"nestedWork\""), pi = json.find("\"Test.Inner\""),
                 pi2 = json.find("\"Test.Inner2\"");
    check(po < pf && pf < pi && pi < pi2, "JSON 안 순서: Outer → 함수 → Inner → Inner2");
}

// ── P1-2 스레드 ───────────────────────────────────────────────────────────────
void testThreads() {
    std::printf("\n== P1-2 threads ==\n");
    check(hprof::start(), "start");
    auto work = [](const char* threadName, int n) {
        HPROF_THREAD_NAME(threadName);
        for (int k = 0; k < n; ++k) {
            HPROF_SCOPE("Test.Work");
            spin(1);
        }
    };
    std::thread a(work, "Worker A", 100);
    std::thread b(work, "Worker B", 60);
    a.join();
    b.join();   // 두 스레드는 이미 끝남: 버퍼는 수집할 때까지 남아야 함
    {
        HPROF_SCOPE("Test.MainAfterJoin");
    }
    const hprof::Capture c = hprof::stop();
    const hprof::ThreadTrack* ta = trackNamed(c, "Worker A");
    const hprof::ThreadTrack* tb = trackNamed(c, "Worker B");
    const hprof::ThreadTrack* tm = trackNamed(c, "Main");
    check(ta && ta->events.size() == 100, "Worker A 100개(끝난 스레드도 수집)");
    check(tb && tb->events.size() == 60, "Worker B 60개");
    check(tm && eventNamed(*tm, "Test.MainAfterJoin"), "메인 스레드 이벤트");
    check(ta && tb && tm && ta->tid != tb->tid && ta->tid != tm->tid && tb->tid != tm->tid, "세 스레드의 tid 가 모두 다름");
    bool sorted = true;
    for (size_t k = 1; k < c.threads.size(); ++k) sorted = sorted && c.threads[k - 1].tid < c.threads[k].tid;
    check(sorted, "스레드는 tid 순");

    // 끝난 스레드의 버퍼는 수집 뒤 정리: 다음 기록에 빈 트랙으로 나오지 않음.
    check(hprof::start(), "다시 start");
    { HPROF_SCOPE("Test.Second"); }
    const hprof::Capture c2 = hprof::stop();
    check(c2.threads.size() == 1 && !trackNamed(c2, "Worker A"), "다음 기록에는 끝난 스레드가 없음");
}

// ── P1-3 시작 · 멈춤 · 상한 ───────────────────────────────────────────────────
void testStartStop() {
    std::printf("\n== P1-3 start / stop / cap ==\n");
    check(!hprof::recording(), "기본은 기록하지 않음");
    { HPROF_SCOPE("Test.Idle"); }
    hprof::emit("Test.IdleEmit", 1, 2);
    check(hprof::start(), "start");
    check(!hprof::start(), "기록 중 start 는 false");
    const hprof::Capture c = hprof::stop();
    check(countNamed(c, "Test.Idle") == 0 && countNamed(c, "Test.IdleEmit") == 0, "기록 전 스코프 · emit 은 남지 않음");
    const hprof::Capture none = hprof::stop();
    check(none.threads.empty() && none.startNs == 0, "기록 중이 아닐 때 stop 은 빈 것");

    check(hprof::start(), "start(첫 기록)");
    { HPROF_SCOPE("Test.First"); }
    // 첫 기록 중에 열려 멈춤 · 다시 시작을 걸친 스코프는 끝날 때의 기록(둘째)에 들어갑니다.
    hprof::Capture first;
    {
        HPROF_SCOPE("Test.Straddle");
        first = hprof::stop();
        check(hprof::start(), "start(둘째 기록, Straddle 은 열린 채)");
    }
    { HPROF_SCOPE("Test.SecondRun"); }
    const hprof::Capture second = hprof::stop();
    check(countNamed(first, "Test.First") == 1, "첫 기록에 First");
    check(countNamed(second, "Test.First") == 0 && countNamed(second, "Test.SecondRun") == 1, "둘째 기록에 앞 기록이 남지 않음");
    check(countNamed(second, "Test.Straddle") == 1, "멈춤 · 다시 시작을 걸친 스코프는 둘째 기록에 들어감(시작 시각으로 잘림)");

    check(hprof::start(5), "start(상한 5)");
    for (int k = 0; k < 12; ++k) { HPROF_SCOPE("Test.Capped"); }
    const hprof::Capture capped = hprof::stop();
    check(countNamed(capped, "Test.Capped") == 5 && capped.droppedCount() == 7, "상한 5: 5개 남고 7개 버림");
    const std::string json = hprof::toChromeJson(capped);
    Json j;
    check(parseJson(json, j) && j.get("otherData") && j.get("otherData")->get("droppedEvents") &&
          j.get("otherData")->get("droppedEvents")->n == 7.0, "버린 수가 파일의 otherData.droppedEvents");
}

// ── P1-4 JSON ─────────────────────────────────────────────────────────────────
void testJson() {
    std::printf("\n== P1-4 Chrome trace JSON ==\n");
    // 정해진 값으로 만든 기록: 시각 · 길이 표기를 정확히 봅니다.
    static const char* kWeird = "quote\" back\\slash\nnewline\ttab";
    hprof::Capture c;
    c.startNs = 1000;
    c.endNs   = 2'001'000;
    hprof::ThreadTrack t1;
    t1.tid  = 1;
    t1.name = "Main \"thread\"";
    t1.events.push_back({ "Child", 1500, 4000 });
    t1.events.push_back({ "Parent", 1500, 1'501'000 });
    t1.events.push_back({ kWeird, 900, 1200 });   // 기록 시작 전에 시작: 0 으로 잘림
    hprof::ThreadTrack t2;
    t2.tid  = 7;
    t2.name = "AssetWorker";
    t2.events.push_back({ "Asset.Decode", 2000, 2'000'999 });
    c.threads = { t1, t2 };
    const std::string text = hprof::toChromeJson(c);
    Json j;
    check(parseJson(text, j), "다시 읽힘(유효한 JSON)");
    const Json* evs = j.get("traceEvents");
    check(evs && evs->t == Json::T::Arr, "traceEvents 배열");
    if (!evs) return;
    std::map<int, std::string> threadNames;
    std::vector<const Json*>   xs;
    bool fieldsOk = true, processName = false;
    for (const Json& e : evs->a) {
        const Json* ph = e.get("ph");
        const Json* pid = e.get("pid");
        const Json* tid = e.get("tid");
        const Json* name = e.get("name");
        fieldsOk = fieldsOk && ph && pid && tid && name && pid->n == 1.0;
        if (!ph || !name || !tid) continue;
        if (ph->s == "M" && name->s == "thread_name") threadNames[(int)tid->n] = e.get("args")->get("name")->s;
        if (ph->s == "M" && name->s == "process_name") processName = e.get("args")->get("name")->s == "Haruka";
        if (ph->s == "X") {
            xs.push_back(&e);
            fieldsOk = fieldsOk && e.get("ts") && e.get("dur") && e.get("cat");
        }
    }
    check(fieldsOk, "모든 이벤트에 ph · pid · tid · name, X 에 ts · dur · cat");
    check(processName, "process_name 메타 = Haruka");
    check(threadNames.size() == 2 && threadNames[1] == "Main \"thread\"" && threadNames[7] == "AssetWorker",
          "thread_name 메타(따옴표 이스케이프 왕복)");
    check(xs.size() == 4, "X 이벤트 4개");
    if (xs.size() != 4) return;
    // 스레드 1: 정렬 뒤 weird(0) → Parent(0.5) → Child(0.5)
    check(xs[0]->get("name")->s == kWeird, "이름의 따옴표 · 역슬래시 · 줄바꿈 · 탭이 그대로 왕복");
    check(xs[0]->get("ts")->n == 0.0 && std::fabs(xs[0]->get("dur")->n - 0.2) < 1e-9, "기록 전 시작은 0 으로 잘림(길이 0.2 us)");
    check(xs[1]->get("name")->s == "Parent" && xs[2]->get("name")->s == "Child", "같은 시작이면 긴 것(부모) 먼저");
    check(text.find("\"ts\":0.500,\"dur\":1499.500") != std::string::npos, "Parent ts 0.500 · dur 1499.500(마이크로초, 소수 셋째)");
    check(text.find("\"ts\":0.500,\"dur\":2.500") != std::string::npos, "Child dur 2.500");
    check(text.find("\"ts\":1.000,\"dur\":1998.999") != std::string::npos, "다른 스레드 Asset.Decode");
    check(xs[3]->get("tid")->n == 7.0, "tid 는 트랙 번호");
    check(j.get("otherData")->get("durationUs")->n == 2000.0, "otherData.durationUs");
    check(j.get("displayTimeUnit")->s == "ms", "displayTimeUnit");

    // 빈 기록도 유효한 파일.
    Json je;
    check(parseJson(hprof::toChromeJson(hprof::Capture{}), je) && je.get("traceEvents")->a.size() == 1, "빈 기록 = 메타 하나뿐인 유효한 JSON");
}

// ── P1-5 끈 번역 단위 ─────────────────────────────────────────────────────────
void testCompiledOut() {
    std::printf("\n== P1-5 HARUKA_PROFILER 0 translation unit ==\n");
    // 컴파일 시점 확인(constexpr 함수 안의 매크로, void 형)은 ProfilerOffTest.cpp 의 static_assert 가 합니다. 여기서는 실행 시점.
    check(hprof::start(), "start");
    const int r = profOffRun(5);
    const hprof::Capture c = hprof::stop();
    check(r == 10, "끈 번역 단위 함수가 정상 동작");
    check(c.eventCount() == 0, "기록 중에 불러도 이벤트 0");
    check(!trackNamed(c, "Off thread"), "HPROF_THREAD_NAME 도 비어 있음");
}

// ── P1-6 FrameTimings 공유 측정 지점 ───────────────────────────────────────────
void testFrameTimings() {
    std::printf("\n== P1-6 FrameTimings shared timing points ==\n");
    FrameTimings::Avg named{ "Test.Slot" };
    FrameTimings::Avg unnamed;
    { ScopedCpuTimer t(named); spin(10); }   // 기록 전: 평균만
    check(named.last > 0.f, "기록하지 않아도 평균은 쌓임");
    named.last = 0.f;
    check(hprof::start(), "start");
    { ScopedCpuTimer t(named); spin(30); }
    { ScopedCpuTimer t(unnamed); spin(5); }
    const hprof::Capture c = hprof::stop();
    check(countNamed(c, "Test.Slot") == 1 && c.eventCount() == 1, "이름 있는 구간만 이벤트 하나");
    const hprof::ThreadTrack* m = trackNamed(c, "Main");
    const hprof::Event* e = m ? eventNamed(*m, "Test.Slot") : nullptr;
    const double evMs = e ? (double)(e->t1 - e->t0) / 1e6 : -1.0;
    check(e && std::fabs(evMs - (double)named.last) < 1e-4, "이벤트 길이 = 평균에 더한 값(같은 두 시각)");
    FrameTimings ft;
    check(ft.record.name && std::string(ft.record.name) == "Renderer.Record" && ft.loopTotal.name == nullptr,
          "엔진 구간 이름(Renderer.Record), loopTotal 은 이름 없음(Frame 스코프가 대신)");
}

// ── P2 추적 파일 경로 · 기록기 · 콘솔 ────────────────────────────────────────────
std::string readAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void testRecorder(const std::filesystem::path& root) {
    std::printf("\n== P2 trace recorder ==\n");
    const std::filesystem::path traces = happ::writeRoot() / "Traces";
    std::string why;
    check(TraceRecorder::resolve("a.json") == traces / "a.json", "상대 경로 → 쓰기 루트/Traces");
    check(TraceRecorder::resolve("sub/b.json") == traces / "sub" / "b.json", "하위 폴더");
    check(TraceRecorder::resolve("x/../c.json") == traces / "c.json", "정규화 뒤 안쪽이면 됨");
    check(TraceRecorder::resolve("../x.json", &why).empty() && !why.empty(), "'..' 거절");
    check(TraceRecorder::resolve("a/../../x.json").empty(), "정규화해도 밖이면 거절");
    check(TraceRecorder::resolve((root / "abs.json").string()).empty(), "절대 경로 거절");
    check(TraceRecorder::resolve("/abs.json").empty(), "루트 경로 거절");
    check(TraceRecorder::resolve("").empty(), "빈 이름 거절");
    const std::string def = TraceRecorder::defaultFileName();
    check(def.rfind("trace_", 0) == 0 && def.size() == std::string("trace_20261120_195700.json").size(), "기본 이름 trace_<날짜_시각>.json");

    TraceRecorder rec;
    check(!rec.stop(), "기록 중이 아니면 stop 은 false");
    check(rec.start("rec.json", &why) && rec.active(), "start");
    check(!rec.start("again.json", &why), "기록 중 start 거절");
    { HPROF_SCOPE("Test.Recorder"); }
    std::string summary;
    check(rec.stop(&summary) && !rec.active(), "stop");
    Json j;
    check(std::filesystem::exists(traces / "rec.json") && parseJson(readAll(traces / "rec.json"), j) &&
          readAll(traces / "rec.json").find("\"Test.Recorder\"") != std::string::npos, "파일이 쓰기 루트/Traces 에 있고 유효함");
    check(summary.find("Trace written") == 0 && summary.find("1 events") != std::string::npos, "결과 글(이벤트 수)");

    // 콘솔 명령(에디터 콘솔 줄 · --console-at 이 같은 길).
    CVarRegistry cv;
    rec.registerCommands(cv);
    check(cv.execute("trace.stop").find("no trace") != std::string::npos, "trace.stop: 기록 중 아님");
    check(cv.execute("trace.start ../bad.json").find("cannot start") == 0, "trace.start: 틀린 경로 알림");
    check(cv.execute("trace.start console.json").find("recording") == 0, "trace.start console.json");
    check(cv.execute("trace.start other.json").find("already recording") != std::string::npos, "기록 중 trace.start 거절");
    { HPROF_SCOPE("Test.Console"); }
    check(cv.execute("trace.stop").find("Trace written") == 0 && std::filesystem::exists(traces / "console.json"), "trace.stop 이 파일을 씀");

    // --trace-frames=3-5: 프레임 3 을 그린 뒤 시작, 5 를 그린 뒤 멈춤.
    TraceRecorder frames;
    frames.configure("frames.json", 3, 5);
    for (uint32_t f = 1; f <= 7; ++f) {
        HPROF_SCOPE("Test.Frame");
        frames.onFrameDrawn(f);
        if (f == 2) check(!frames.active(), "프레임 2: 아직 아님");
        if (f == 3) check(frames.active(), "프레임 3: 시작");
        if (f == 5) check(!frames.active() && std::filesystem::exists(traces / "frames.json"), "프레임 5: 멈추고 씀");
    }
    check(!frames.active(), "구간이 끝나면 다시 시작하지 않음");
    const std::string ft = readAll(traces / "frames.json");
    size_t n = 0;
    for (size_t p = ft.find("\"Test.Frame\""); p != std::string::npos; p = ft.find("\"Test.Frame\"", p + 1)) ++n;
    check(n == 1, "기록 구간 안에서 끝난 프레임 스코프 1개(프레임 4)");

    // --trace(프레임 없음): 바로 시작해 finish 에서 씀. 소멸자도 같은 일.
    {
        TraceRecorder whole;
        whole.configure("whole.json", 0, 0);
        check(whole.active(), "--trace: 바로 시작");
        { HPROF_SCOPE("Test.Whole"); }
    }
    check(std::filesystem::exists(traces / "whole.json") && !hprof::recording(), "소멸(엔진 종료)에서 씀");
}

// ── 비용(참고 수치, 4절) ─────────────────────────────────────────────────────────
void testCost() {
    std::printf("\n== cost ==\n");
    constexpr int kN = 2'000'000;
    volatile int sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < kN; ++k) { HPROF_SCOPE("Test.IdleCost"); sink = sink + 1; }
    const double idleNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / kN;
    check(hprof::start(kN + 16), "start");
    t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < kN; ++k) { HPROF_SCOPE("Test.RecCost"); sink = sink + 1; }
    const double recNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / kN;
    const hprof::Capture c = hprof::stop();
    std::printf("  idle scope %.2f ns, recording scope %.2f ns (%zu events)\n", idleNs, recNs, c.eventCount());
    check(c.eventCount() == (size_t)kN, "기록한 수 = 스코프 수");
    check(idleNs < 50.0, "기록하지 않을 때 스코프 하나 < 50 ns");
    check(recNs < 2000.0, "기록할 때 스코프 하나 < 2 us");
}

} // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("haruka_profiler_test_" + std::to_string(
            (unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    happ::setWriteRoot(root);

    testNesting();
    testThreads();
    testStartStop();
    testJson();
    testCompiledOut();
    testFrameTimings();
    testRecorder(root);
    testCost();

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::printf("\n%s (%d passed, %d failed)\n", g_failed ? "FAILED" : "ALL PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
