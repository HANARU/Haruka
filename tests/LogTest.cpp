// 로그 카테고리 체계(core/HLog)를 검산합니다.
// Vulkan 도, 창도, World 도 필요 없습니다. HLog.cpp 하나만 링크합니다.
//
// 이 테스트가 존재하는 이유: 로그는 모든 서브시스템이 어느 스레드에서나 부르는 경로라,
// 기록 수준 판정이 틀리거나 링 버퍼의 seq 가 어긋나면 화면에서는 "가끔 로그가 빠진다" 로만
// 보이고 재현이 어렵습니다. 특히 "꺼진 로그는 인자를 평가하지 않는다" 는 성능 계약은
// 눈으로 확인할 방법이 없습니다.
//
// 실행: build/Release/HarukaLogTest.exe

#include "../src/core/HLog.hpp"

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

// 같은 이름을 서로 다른 변수로 두 번 선언합니다(두 cpp 가 각자 static 으로 선언한 상황).
constinit HLogCategory LogTestA      { "TestShared", ELogVerbosity::Log };
constinit HLogCategory LogTestB      { "TestShared", ELogVerbosity::Log };
constinit HLogCategory LogTestLevels { "TestLevels", ELogVerbosity::Log };
constinit HLogCategory LogTestRing   { "TestRing",   ELogVerbosity::Log };
constinit HLogCategory LogTestThread { "TestThread", ELogVerbosity::Log };

HLogRecord lastRecord() {
    std::vector<HLogRecord> out;
    const uint64_t latest = HLog::latestSeq();
    HLog::copySince(latest > 0 ? latest - 1 : 0, out);
    return out.empty() ? HLogRecord{} : out.back();
}

std::string categoryName(uint16_t index) {
    const HLogCategoryState* s = HLog::categoryAt(index);
    return s ? s->name : std::string("<none>");
}

void testLazyArguments() {
    std::printf("\n[1] 꺼진 로그는 인자를 평가하지 않음\n");
    int evaluated = 0;
    auto sideEffect = [&] { ++evaluated; return 42; };

    HLOG(LogTestLevels, Verbose) << "value " << sideEffect();   // 기본 수준 Log 라 꺼짐
    check(evaluated == 0, "Verbose (기본 수준 Log) 의 인자는 평가되지 않음");

    HLOG(LogTestLevels, Log) << "value " << sideEffect();
    check(evaluated == 1, "Log 의 인자는 한 번 평가됨");

    // 바깥 if/else 와 결합이 꼬이지 않는지(매크로가 else 를 가로채면 컴파일 에러나 오동작).
    bool elseTaken = false;
    if (false) HLOG(LogTestLevels, Log) << "never";
    else       elseTaken = true;
    check(elseTaken, "바깥 if/else 의 else 가 매크로에 흡수되지 않음");
}

void testLevels() {
    std::printf("\n[2] 기록 수준 변경\n");
    HLogCategoryState* st = LogTestLevels.state();
    check(LogTestLevels.isEnabled(ELogVerbosity::Error),    "기본 Log: Error 켜짐");
    check(LogTestLevels.isEnabled(ELogVerbosity::Log),      "기본 Log: Log 켜짐");
    check(!LogTestLevels.isEnabled(ELogVerbosity::Verbose), "기본 Log: Verbose 꺼짐");

    st->level.store((uint8_t)ELogVerbosity::Verbose);
    check(LogTestLevels.isEnabled(ELogVerbosity::Verbose), "Verbose 로 올리면 즉시 켜짐");

    st->level.store((uint8_t)ELogVerbosity::Off);
    check(!LogTestLevels.isEnabled(ELogVerbosity::Error), "Off 면 Error 도 꺼짐");
    check(!LogTestLevels.isEnabled(ELogVerbosity::Off),   "Off 는 로그 심각도로 쓰이지 않음");

    st->level.store((uint8_t)ELogVerbosity::Log);
    check(st->defaultLevel == ELogVerbosity::Log, "선언 기본값은 바뀌지 않음");
}

void testSharedName() {
    std::printf("\n[3] 같은 이름은 같은 상태를 공유\n");
    check(LogTestA.state() == LogTestB.state(), "두 선언이 같은 상태 객체를 가리킴");

    LogTestA.state()->level.store((uint8_t)ELogVerbosity::Error);
    check(!LogTestB.isEnabled(ELogVerbosity::Warning), "A 의 수준 변경이 B 에 반영됨");
    LogTestA.state()->level.store((uint8_t)ELogVerbosity::Log);

    check(&HLog::category("TestShared") == &HLog::category("TestShared"),
          "실행 중 이름 조회는 같은 객체를 돌려줌");
    check(HLog::category("TestShared").state() == LogTestA.state(),
          "실행 중 조회도 선언된 카테고리와 상태를 공유");
}

void testRecordsAndSplit() {
    std::printf("\n[4] 레코드 필드와 여러 줄 분할\n");
    const uint64_t before = HLog::latestSeq();
    HLOG(LogTestLevels, Warning) << "first\nsecond\r\nthird\n";
    std::vector<HLogRecord> out;
    HLog::copySince(before, out);
    check(out.size() == 3, "개행 셋(끝 개행 포함)이 레코드 셋이 됨 (실제 " + std::to_string(out.size()) + ")");
    if (out.size() == 3) {
        check(out[0].text == "first" && out[1].text == "second" && out[2].text == "third",
              "줄 내용과 \\r 제거");
        check(out[0].seq + 1 == out[1].seq && out[1].seq + 1 == out[2].seq, "seq 가 연속");
        check(out[2].verbosity == ELogVerbosity::Warning, "심각도 보존");
        check(categoryName(out[2].category) == "TestLevels", "카테고리 인덱스로 이름을 되찾음");
    }
    const uint32_t warnings = LogTestLevels.state()->warnings.load();
    HLOG(LogTestLevels, Warning) << "a\nb";
    check(LogTestLevels.state()->warnings.load() == warnings + 1, "경고 누계는 줄 수가 아니라 메시지 수");
}

void testRing() {
    std::printf("\n[5] 링 버퍼 용량과 밀려남 보고\n");
    const uint64_t before = HLog::latestSeq();
    const size_t   extra  = 10;
    for (size_t i = 0; i < HLog::kCapacity + extra; ++i)
        HLOG(LogTestRing, Log) << i;

    check(HLog::latestSeq() == before + HLog::kCapacity + extra, "latestSeq 가 제출 수만큼 증가");

    std::vector<HLogRecord> out;
    const size_t dropped = HLog::copySince(before, out);
    check(dropped == extra, "밀려난 개수를 보고 (실제 " + std::to_string(dropped) + ")");
    check(out.size() == HLog::kCapacity, "링 용량만큼만 돌려줌");
    bool consecutive = true;
    for (size_t i = 1; i < out.size(); ++i)
        if (out[i].seq != out[i - 1].seq + 1) { consecutive = false; break; }
    check(consecutive, "돌려준 레코드의 seq 가 끊김 없이 연속");
    check(!out.empty() && out.back().text == std::to_string(HLog::kCapacity + extra - 1), "마지막이 최신");

    std::vector<HLogRecord> none;
    check(HLog::copySince(HLog::latestSeq(), none) == 0 && none.empty(), "최신 seq 이후에는 아무것도 없음");
}

void testLatestAtMost() {
    std::printf("\n[6] 상태바용 조회는 Verbose 를 건너뜀\n");
    LogTestLevels.state()->level.store((uint8_t)ELogVerbosity::Verbose);
    HLOG(LogTestLevels, Log)     << "visible";
    HLOG(LogTestLevels, Verbose) << "noise";
    HLogRecord rec;
    check(HLog::latestAtMost(ELogVerbosity::Log, rec) && rec.text == "visible",
          "최신 Verbose 대신 그 앞의 Log 를 돌려줌");
    check(HLog::latestAtMost(ELogVerbosity::Verbose, rec) && rec.text == "noise",
          "Verbose 까지 허용하면 최신을 돌려줌");
    LogTestLevels.state()->level.store((uint8_t)ELogVerbosity::Log);
}

void testLegacyAdapter() {
    std::printf("\n[7] 표준 출력 어댑터의 태그 해석\n");
    HLog::feedLegacyLine("[LegacyTag] hello world", false);
    HLogRecord r = lastRecord();
    check(categoryName(r.category) == "LegacyTag" && r.text == "hello world" &&
          r.verbosity == ELogVerbosity::Log, "[Tag] 접두어 → 같은 이름 카테고리, cout 은 Log");

    HLog::feedLegacyLine("[LegacyTag] oops", true);
    check(lastRecord().verbosity == ELogVerbosity::Warning, "cerr 는 Warning");

    HLog::feedLegacyLine("no prefix here", false);
    r = lastRecord();
    check(categoryName(r.category) == "Stdout" && r.text == "no prefix here", "접두어가 없으면 Stdout");

    HLog::feedLegacyLine("[not a tag!] text", false);
    r = lastRecord();
    check(categoryName(r.category) == "Stdout" && r.text == "[not a tag!] text",
          "태그 문자가 아닌 것이 섞이면 접두어로 보지 않음");

    HLog::category("LegacyTag").state()->level.store((uint8_t)ELogVerbosity::Off);
    const uint64_t before = HLog::latestSeq();
    HLog::feedLegacyLine("[LegacyTag] suppressed", false);
    check(HLog::latestSeq() == before, "어댑터 경로도 기록 수준을 따름");
}

void testThreads() {
    std::printf("\n[8] 여러 스레드 동시 제출\n");
    const uint64_t before    = HLog::latestSeq();
    const int      kThreads  = 4;
    const int      kPerThread = 10000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([t] {
            for (int i = 0; i < kPerThread; ++i)
                HLOG(LogTestThread, Log) << t << ":" << i;
        });
    for (auto& th : threads) th.join();

    check(HLog::latestSeq() == before + (uint64_t)kThreads * kPerThread, "seq 누락·중복 없음 (총 증가량 일치)");

    std::vector<HLogRecord> out;
    HLog::copySince(before, out);
    bool consecutive = out.size() == HLog::kCapacity;
    for (size_t i = 1; consecutive && i < out.size(); ++i)
        consecutive = out[i].seq == out[i - 1].seq + 1;
    check(consecutive, "링에 남은 구간이 연속");

    // 스레드마다 자기 번호가 증가 순서로만 나타나야 합니다(제출 순서 보존).
    std::vector<int> lastSeen(kThreads, -1);
    bool ordered = true;
    for (const HLogRecord& r : out) {
        const size_t colon = r.text.find(':');
        if (colon == std::string::npos) { ordered = false; break; }
        const int t = std::stoi(r.text.substr(0, colon));
        const int i = std::stoi(r.text.substr(colon + 1));
        if (t < 0 || t >= kThreads || i <= lastSeen[t]) { ordered = false; break; }
        lastSeen[t] = i;
    }
    check(ordered, "스레드별 제출 순서가 보존됨");
}

} // namespace

int main() {
    std::printf("HarukaLogTest\n");
    HLog::setConsoleSinkEnabled(false);   // 수만 줄이 콘솔로 나가지 않게 합니다

    testLazyArguments();
    testLevels();
    testSharedName();
    testRecordsAndSplit();
    testRing();
    testLatestAtMost();
    testLegacyAdapter();
    testThreads();

    std::printf("\n%s (%d failed)\n", g_failed == 0 ? "ALL PASSED" : "FAILED", g_failed);
    return g_failed == 0 ? 0 : 1;
}
