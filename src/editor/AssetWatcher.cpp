#include "AssetWatcher.hpp"
#include "../assets/AssetChangeQueue.hpp"
#include "../core/HLog.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

// 감시 스레드에서도 로그를 남깁니다. HLOG 는 어느 스레드에서나 안전합니다(Threading.md 규칙 4).
static constinit HLogCategory LogAssetWatch{ "HotReload", ELogVerbosity::Log };

#ifdef _WIN32
static std::string utf8FromWide(const wchar_t* w, int len) {
    if (len <= 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, ' ');
    WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(), n, nullptr, nullptr);
    for (char& c : out) if (c == '\\') c = '/';
    return out;
}
#endif

bool AssetWatcher::start(const std::filesystem::path& projectRoot) {
    stop();
    root_ = projectRoot;
#ifdef _WIN32
    HANDLE h = CreateFileW(projectRoot.wstring().c_str(), FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        HLOG(LogAssetWatch, Warning) << "Cannot watch project folder for asset changes: " << projectRoot.generic_string();
        return false;
    }
    dir_   = h;
    event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { watchLoop(); });
    // 시작 알림은 Verbose 입니다(Output Log 에 보이면 UI 덤프 기준이 바뀜).
    HLOG(LogAssetWatch, Verbose) << "Watching " << projectRoot.generic_string() << " for asset changes";
    return true;
#else
    return false;
#endif
}

void AssetWatcher::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
#ifdef _WIN32
    if (event_) SetEvent(static_cast<HANDLE>(event_));   // 기다리는 스레드를 깨움
#endif
    if (thread_.joinable()) thread_.join();
#ifdef _WIN32
    if (dir_)   { CancelIo(static_cast<HANDLE>(dir_)); CloseHandle(static_cast<HANDLE>(dir_)); dir_ = nullptr; }
    if (event_) { CloseHandle(static_cast<HANDLE>(event_)); event_ = nullptr; }
#endif
}

std::vector<std::string> AssetWatcher::drain(bool* overflow) {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    out.swap(changed_);
    if (overflow) *overflow = overflow_;
    overflow_ = false;
    return out;
}

void AssetWatcher::watchLoop() {
#ifdef _WIN32
    HANDLE dir = static_cast<HANDLE>(dir_);
    HANDLE ev  = static_cast<HANDLE>(event_);
    std::vector<DWORD> bufStore(16 * 1024);   // 64 KiB, DWORD 정렬(FILE_NOTIFY_INFORMATION)
    char* const buf = reinterpret_cast<char*>(bufStore.data());
    const DWORD bufBytes = (DWORD)(bufStore.size() * sizeof(DWORD));
    OVERLAPPED ov{};
    bool ioPending = false;
    ov.hEvent = ev;
    while (running_.load(std::memory_order_acquire)) {
        ResetEvent(ev);
        DWORD returned = 0;
        const BOOL ok = ReadDirectoryChangesW(dir, buf, bufBytes, TRUE,
                                              FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                                                  FILE_NOTIFY_CHANGE_SIZE,
                                              &returned, &ov, nullptr);
        if (!ok) {
            HLOG(LogAssetWatch, Warning) << "Asset watcher stopped (ReadDirectoryChangesW error " << GetLastError() << ")";
            break;
        }
        ioPending = true;
        // 250 ms 마다 깨어 멈춤 요청을 확인합니다(셰이더 감시자와 같음). stop 은 이벤트를 직접 켜서 바로 깨웁니다.
        DWORD wait = WAIT_TIMEOUT;
        while (running_.load(std::memory_order_acquire) && (wait = WaitForSingleObject(ev, 250)) == WAIT_TIMEOUT) {}
        if (!running_.load(std::memory_order_acquire)) break;
        if (wait != WAIT_OBJECT_0) continue;
        ioPending = false;
        if (!GetOverlappedResult(dir, &ov, &returned, FALSE)) continue;

        std::vector<std::string> names;
        bool overflow = false;
        if (returned == 0) {
            overflow = true;   // 버퍼가 넘쳐 OS 가 목록을 버림
        } else {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buf);
            for (;;) {
                const DWORD act = info->Action;
                if (act == FILE_ACTION_ADDED || act == FILE_ACTION_MODIFIED || act == FILE_ACTION_RENAMED_NEW_NAME) {
                    const std::string rel = utf8FromWide(info->FileName, (int)(info->FileNameLength / sizeof(wchar_t)));
                    std::string logical = hreload::logicalFromProjectRelative(rel);
                    if (!logical.empty()) names.push_back(std::move(logical));
                }
                if (info->NextEntryOffset == 0) break;
                info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(reinterpret_cast<const char*>(info) +
                                                                       info->NextEntryOffset);
            }
        }
        if (!names.empty() || overflow) {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& n : names) changed_.push_back(std::move(n));
            overflow_ = overflow_ || overflow;
        }
    }
    if (ioPending) {   // 버퍼(bufStore)가 사라지기 전에 걸린 읽기를 취소하고 끝날 때까지 기다림
        CancelIo(dir);
        DWORD ignored = 0;
        GetOverlappedResult(dir, &ov, &ignored, TRUE);
    }
#endif
}
