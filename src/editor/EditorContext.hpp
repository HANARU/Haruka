#pragma once
#include <string>
#include <deque>
#include <vector>
#include <streambuf>
#include <mutex>
#include <atomic>

class World;
class Actor;
class AssetManager;
class Renderer;
class IWindow;

// ── Editor shared state ────────────────────────────────────────────────────────
struct EditorContext {
    World*        world    = nullptr;
    Actor*        selected = nullptr;
    AssetManager* assets   = nullptr;
    Renderer*     renderer = nullptr;
    IWindow*      window   = nullptr;

    static constexpr int kMaxLogLines = 200;

    // 로그는 메인 스레드 외에 HotReloader 워처 스레드 등에서도
    // std::cout 경유로 들어오므로 뮤텍스로 보호한다.
    std::atomic<bool> logDirty { false };

    void addLog(const std::string& line);          // thread-safe
    std::vector<std::string> snapshotLog() const;  // thread-safe 복사본 (UI 표시용)
    // 마지막 한 줄만 돌려줍니다. snapshotLog 는 최대 200개 문자열 전량 복사라
    // 매 프레임 경로에서 쓸 수 없습니다.
    std::string              lastLogLine() const;

private:
    mutable std::mutex      logMutex_;
    std::deque<std::string> logLines_;             // logMutex_로 보호
};

// ── std::ostream tee → EditorContext log ──────────────────────────────────────
// Forwards every character to the original streambuf AND captures lines into ctx.
// cout/cerr는 여러 스레드가 동시에 쓸 수 있으므로 내부 버퍼를 뮤텍스로 보호한다.
class LogStreamBuf : public std::streambuf {
public:
    LogStreamBuf(EditorContext* ctx, std::streambuf* orig);
    ~LogStreamBuf();
protected:
    int             overflow(int c) override;
    std::streamsize xsputn(const char* s, std::streamsize n) override;
private:
    EditorContext*  ctx_;
    std::streambuf* orig_;
    std::mutex      mtx_;
    std::string     line_;        // mtx_로 보호
    void            flushLine();  // mtx_ 보유 상태에서 호출
};
