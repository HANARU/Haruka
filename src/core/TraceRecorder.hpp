#pragma once
// 추적 파일 기록의 시작 · 끝 · 쓰기(2026-11-20, Docs/CPU_Profiling.md D6).
//
// 엔진이 하나 소유합니다. 명령줄 --trace=<파일> · --trace-frames=<a>-<b> 와 콘솔 명령 trace.start [파일] · trace.stop 이 같은 길을
// 씁니다. 파일은 언제나 쓰기 루트의 Traces/ 아래이고, 상대 경로만 받습니다. 메인 스레드 전용입니다(기록 자체는 모든 스레드, hprof).
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

class CVarRegistry;

class TraceRecorder {
public:
    ~TraceRecorder();

    // 상대 경로를 <쓰기 루트>/Traces/<rel> 로. 비었거나 절대 경로 · 드라이브 · ".." 가 있으면 빈 경로이고 why 에 이유.
    static std::filesystem::path resolve(std::string_view rel, std::string* why = nullptr);
    // 파일 이름이 없을 때 쓰는 이름(trace_<YYYYmmdd_HHMMSS>.json).
    static std::string defaultFileName();

    // 기록을 시작합니다. 이미 기록 중(이 기록기든 다른 곳이든)이거나 경로가 틀리면 false 와 이유.
    bool start(std::string_view file, std::string* why = nullptr);
    // 멈추고 파일을 씁니다. 결과 글(경로 · 이벤트 수 · 스레드 수 · 버린 수)을 summary 에. 기록 중이 아니면 false.
    bool stop(std::string* summary = nullptr);
    bool active() const { return active_; }
    const std::filesystem::path& path() const { return path_; }

    // --trace · --trace-frames(D6). from == to == 0 이면 지금 바로 시작해 finish 까지(file 이 비면 하지 않음), 아니면 onFrameDrawn 이
    // 정한 프레임에(file 이 비면 기본 이름).
    void configure(std::string_view file, uint32_t fromFrame, uint32_t toFrame);
    // 프레임을 하나 그린 뒤(drawn = 지금까지 그린 수) 부릅니다.
    void onFrameDrawn(uint32_t drawn);
    // 엔진을 닫을 때. 기록 중이면 멈추고 씁니다.
    void finish();

    // trace.start · trace.stop 콘솔 명령을 등록합니다.
    void registerCommands(CVarRegistry& cvars);

private:
    bool                  active_ = false;
    std::filesystem::path path_;
    std::string           pendingFile_;   // --trace-frames 를 기다리는 파일
    uint32_t              from_ = 0, to_ = 0;
    bool                  framesDone_ = false;
};
