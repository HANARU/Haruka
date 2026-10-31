#pragma once
#include <cstdint>
#include <string>

// ── SceneDocument ─────────────────────────────────────────────────────────────
// 에디터가 지금 열어 둔 씬 파일과, 그 파일을 저장한 뒤 바뀐 것이 있는지를 기억합니다(2026-10-15).
//
// "바뀜"은 커맨드를 쌓을 때 켜는 플래그가 아니라, 저장한 순간의 Undo 상태 표지(HCommandStack::stateToken)
// 와 지금 표지를 비교해 정합니다. 그래서 저장 뒤 편집을 되돌려 저장 시점으로 돌아오면 다시 깨끗해집니다.
// Undo 이력에 남지 않는 변경(되돌릴 수 없는 편집, 비워진 이력)은 markDirty 로 따로 표시합니다.
//
// Play 동안의 편집은 Stop 하면 버려지므로 씬을 바꾼 것으로 보지 않습니다. Play 를 시작할 때의 상태를
// 고정해 두었다가, Stop 이 이력을 비운 뒤 그 상태로 되돌립니다.
class SceneDocument {
public:
    // 새 파일을 열었거나 새 씬을 만들었습니다(path 가 비면 이름 없는 새 씬). 이력은 이미 비운 상태입니다.
    void reset(std::string logicalPath, uint64_t token) {
        path_ = std::move(logicalPath);
        clean_ = token;
        extra_ = false;
        playing_ = false;
    }
    // 저장했습니다. 다른 이름으로 저장했으면 경로도 바뀝니다.
    void markSaved(std::string logicalPath, uint64_t token) {
        path_ = std::move(logicalPath);
        clean_ = token;
        extra_ = false;
    }
    void markDirty() { if (!playing_) extra_ = true; }

    void beginPlay(uint64_t token) {
        frozenDirty_ = dirty(token);
        playing_ = true;
    }
    // Stop 이 월드를 되돌리고 이력을 비운 뒤 부릅니다. 지금 표지를 기준으로 Play 전의 상태를 되살립니다.
    void endPlay(uint64_t tokenAfterClear) {
        playing_ = false;
        clean_ = tokenAfterClear;
        extra_ = frozenDirty_;
    }

    bool dirty(uint64_t token) const {
        if (playing_) return frozenDirty_;
        return extra_ || token != clean_;
    }
    const std::string& path() const { return path_; }
    bool untitled() const { return path_.empty(); }
    // 제목 표시줄 · 상태바용 이름(확장자 포함, 폴더 제외). 이름 없는 새 씬은 "Untitled".
    std::string displayName() const {
        if (path_.empty()) return "Untitled";
        const size_t slash = path_.find_last_of("/\\");
        return slash == std::string::npos ? path_ : path_.substr(slash + 1);
    }

private:
    std::string path_;
    uint64_t    clean_       = 0;
    bool        extra_       = false;
    bool        playing_     = false;
    bool        frozenDirty_ = false;
};
