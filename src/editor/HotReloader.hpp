#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
#include <string>

class Renderer;

// Watches a directory for .spv file changes and signals when a shader reload is needed.
// Call reloadPending() once per frame (after fence wait); if true, call reloadPipelines().
class HotReloader {
public:
    void start(const std::string& watchDir, Renderer& renderer);
    void stop();

    // Returns true and clears the flag once a .spv change was detected and no further change
    // arrived for kQuietMs. 빌드는 파일 여러 개를 연달아 쓰고, 알림이 복사가 끝나기 전에 올 수
    // 있어서 조용해질 때까지 기다립니다.
    bool reloadPending();

private:
    static constexpr int64_t kQuietMs = 300;

    Renderer*            renderer_ = nullptr;
    std::string          watchDir_;
    std::atomic<bool>    pending_      { false };
    std::atomic<int64_t> lastChangeMs_ { 0 };
    std::atomic<bool>    running_      { false };
    std::thread          thread_;

    void watchLoop();
};
