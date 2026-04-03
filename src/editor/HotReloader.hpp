#pragma once
#include <atomic>
#include <thread>
#include <string>

class Renderer;

// Watches a directory for .spv file changes and signals when a shader reload is needed.
// Call reloadPending() once per frame (after fence wait); if true, call reloadPipelines().
class HotReloader {
public:
    void start(const std::string& watchDir, Renderer& renderer);
    void stop();

    // Returns true and clears the flag if a .spv change was detected since last poll.
    bool reloadPending() {
        return pending_.exchange(false, std::memory_order_acq_rel);
    }

private:
    Renderer*         renderer_ = nullptr;
    std::string       watchDir_;
    std::atomic<bool> pending_  { false };
    std::atomic<bool> running_  { false };
    std::thread       thread_;

    void watchLoop();
};
