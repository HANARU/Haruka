#include "HotReloader.hpp"
#include "../renderer/Renderer.hpp"
#include <thread>
#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "../core/HLog.hpp"

// 워처 스레드에서도 로그를 남깁니다. HLOG 는 어느 스레드에서나 안전합니다(Threading.md 규칙 4).
static constinit HLogCategory LogHotReload{ "HotReload", ELogVerbosity::Log };

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool HotReloader::reloadPending() {
    if (!pending_.load(std::memory_order_acquire)) return false;
    if (nowMs() - lastChangeMs_.load(std::memory_order_acquire) < kQuietMs) return false;
    return pending_.exchange(false, std::memory_order_acq_rel);
}

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
        HLOG(LogHotReload, Warning) << "Cannot watch directory: " << watchDir_;
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

        bool changed = false;
        if (returned == 0) {
            // 버퍼가 넘쳐 OS 가 변경 목록을 버렸습니다. 무엇이 바뀌었는지 모르므로 변경으로 칩니다.
            // 예전에는 이때 지난번 버퍼 내용을 다시 읽었습니다.
            changed = true;
        } else {
            // Scan notification records for .spv changes
            const FILE_NOTIFY_INFORMATION* info =
                reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buf);
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
        }

        if (changed) {
            if (!pending_.load(std::memory_order_acquire))
                HLOG(LogHotReload, Log) << "Shader change detected - reload queued";
            lastChangeMs_.store(nowMs(), std::memory_order_release);
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
