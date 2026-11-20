#pragma once
// 프로젝트 폴더 감시(2026-11-20, Docs/Asset_Hot_Reload.md D1). 에디터 전용입니다(게임 실행 파일은 만들지 않음).
// Windows 는 ReadDirectoryChangesW(하위 폴더 포함, 쓰기 · 이름 · 크기 변경)로 감시하는 스레드가 바뀐 파일 이름만 모으고, 메인
// 스레드가 drain 으로 꺼냅니다. 이름은 프로젝트 기준 상대 경로에서 정식 논리 경로로 바꾸며(Content/ → /Game/, Config/import.ini),
// 그 밖(Saved/ 등)은 버립니다. 알림 버퍼가 넘치면 overflow 로 알립니다(무엇이 바뀌었는지 모름). 다른 플랫폼에서는 아무것도 하지 않습니다.

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AssetWatcher {
public:
    AssetWatcher() = default;
    ~AssetWatcher() { stop(); }
    AssetWatcher(const AssetWatcher&) = delete;
    AssetWatcher& operator=(const AssetWatcher&) = delete;

    // 감시를 시작합니다. 폴더를 열지 못하거나 지원하지 않는 플랫폼이면 false.
    bool start(const std::filesystem::path& projectRoot);
    void stop();
    bool running() const { return running_.load(std::memory_order_acquire); }

    // 그동안 모인 바뀐 파일(정식 논리 경로, 들어온 순서, 중복 가능). overflow 가 있으면 넘침이 있었는지.
    std::vector<std::string> drain(bool* overflow = nullptr);

private:
    void watchLoop();

    std::filesystem::path    root_;
    std::atomic<bool>        running_{ false };
    std::thread              thread_;
    std::mutex               mu_;
    std::vector<std::string> changed_;    // mu_
    bool                     overflow_ = false;   // mu_
#ifdef _WIN32
    void*                    dir_   = nullptr;    // HANDLE
    void*                    event_ = nullptr;    // HANDLE
#endif
};
