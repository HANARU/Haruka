#include "HotReloader.hpp"
#include "../renderer/Renderer.hpp"
#include <iostream>
#include <thread>
#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void HotReloader::start(const std::string& watchDir, Renderer& renderer) {
    renderer_ = &renderer;
    watchDir_ = watchDir;
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { watchLoop(); });
}

void HotReloader::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

void HotReloader::watchLoop() {
#ifdef _WIN32
    HANDLE hDir = CreateFileA(
        watchDir_.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (hDir == INVALID_HANDLE_VALUE) {
        std::cerr << "[HotReloader] Cannot watch directory: " << watchDir_ << "\n";
        return;
    }

    alignas(DWORD) char buf[4096];
    OVERLAPPED ov{};
    HANDLE hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    ov.hEvent = hEvent;

    while (running_.load(std::memory_order_acquire)) {
        ResetEvent(hEvent);
        DWORD returned = 0;
        BOOL ok = ReadDirectoryChangesW(
            hDir,
            buf, sizeof(buf),
            FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            &returned, &ov, nullptr);

        if (!ok && GetLastError() != ERROR_IO_PENDING) break;

        // Poll in 250ms intervals so we can check running_ promptly
        DWORD wait = WaitForSingleObject(hEvent, 250);
        if (!running_.load(std::memory_order_acquire)) break;
        if (wait != WAIT_OBJECT_0) continue;

        if (!GetOverlappedResult(hDir, &ov, &returned, FALSE)) continue;

        // Scan notification records for .spv changes
        const FILE_NOTIFY_INFORMATION* info =
            reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buf);
        bool changed = false;
        do {
            std::wstring wname(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (wname.size() >= 4 &&
                wname.compare(wname.size() - 4, 4, L".spv") == 0) {
                changed = true;
            }
            if (info->NextEntryOffset == 0) break;
            info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<const char*>(info) + info->NextEntryOffset);
        } while (true);

        if (changed) {
            std::cout << "[HotReloader] Shader change detected — reload queued\n";
            pending_.store(true, std::memory_order_release);
        }
    }

    CloseHandle(hEvent);
    CloseHandle(hDir);
#else
    // Non-Windows: hot-reload not implemented, loop idles until stopped
    while (running_.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif
}
