#pragma once
// 바뀐 파일 모으기(2026-11-20, Docs/Asset_Hot_Reload.md D2 · D3). 감시자(에디터)가 알려 준 정식 논리 경로를 경로별 마지막 알림
// 시각과 함께 들고 있다가, quietMs 동안 새 알림이 없으면 한 번만 내보냅니다. 제작 도구는 파일 하나를 여러 번 나눠 쓰고, 알림이
// 복사가 끝나기 전에 오기도 해서 셰이더 감시자(HotReloader::kQuietMs)와 같이 조용해질 때까지 기다립니다. 시각은 부르는 쪽이
// 넘기므로(ms) 시험에서 시계를 흉내 낼 수 있습니다. 메인 스레드 전용입니다.

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hreload {

enum class ChangeKind { Model, Scene, ImportSettings, Other };

// 확장자 · 경로로 분류합니다. Other 도 올라간 모델의 의존 파일(.bin · 이미지)일 수 있어, 부르는 쪽이 AssetManager 에 따로 묻습니다.
inline ChangeKind classify(std::string_view canonicalPath) {
    auto endsWith = [&](std::string_view suf) {
        if (canonicalPath.size() < suf.size()) return false;
        for (size_t i = 0; i < suf.size(); ++i) {
            char c = canonicalPath[canonicalPath.size() - suf.size() + i];
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != suf[i]) return false;
        }
        return true;
    };
    if (endsWith(".glb") || endsWith(".gltf")) return ChangeKind::Model;
    if (endsWith(".hscene"))                   return ChangeKind::Scene;
    if (endsWith("/config/import.ini") && canonicalPath.rfind("/Config/", 0) == 0) return ChangeKind::ImportSettings;
    return ChangeKind::Other;
}

// 프로젝트 폴더 기준 상대 경로('/' 구분)를 정식 논리 경로로 바꿉니다. Content/ 아래 → /Game/..., Config/import.ini → /Config/import.ini,
// 그 밖(Saved/ · Source/ · 프로젝트 파일 등)은 빈 문자열(버림).
inline std::string logicalFromProjectRelative(std::string_view rel) {
    auto startsFold = [](std::string_view s, std::string_view pre) {
        if (s.size() < pre.size()) return false;
        for (size_t i = 0; i < pre.size(); ++i) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != pre[i]) return false;
        }
        return true;
    };
    if (startsFold(rel, "content/") && rel.size() > 8) return "/Game/" + std::string(rel.substr(8));
    if (rel.size() == 17 && startsFold(rel, "config/import.ini")) return "/Config/import.ini";
    return {};
}

class AssetChangeQueue {
public:
    static constexpr int64_t kQuietMs = 300;

    explicit AssetChangeQueue(int64_t quietMs = kQuietMs) : quietMs_(quietMs) {}

    void notify(const std::string& canonicalPath, int64_t nowMs) {
        if (canonicalPath.empty()) return;
        lastMs_[canonicalPath] = nowMs;
    }
    // 버퍼 넘침 등으로 무엇이 바뀌었는지 모를 때: 다음 내보내기에 "모두 다시 확인" 표시를 붙입니다.
    void notifyOverflow(int64_t nowMs) { overflowMs_ = nowMs; overflow_ = true; }

    // quietMs 동안 새 알림이 없던 경로를 꺼냅니다(알림 순서와 무관하게 이름순). rescanAll 은 넘침이 조용해졌으면 true.
    std::vector<std::string> takeReady(int64_t nowMs, bool* rescanAll = nullptr) {
        std::vector<std::string> out;
        for (auto it = lastMs_.begin(); it != lastMs_.end();) {
            if (nowMs - it->second >= quietMs_) { out.push_back(it->first); it = lastMs_.erase(it); }
            else ++it;
        }
        std::sort(out.begin(), out.end());
        const bool all = overflow_ && nowMs - overflowMs_ >= quietMs_;
        if (all) overflow_ = false;
        if (rescanAll) *rescanAll = all;
        return out;
    }
    size_t pending() const { return lastMs_.size() + (overflow_ ? 1 : 0); }
    bool   has(const std::string& canonicalPath) const { return lastMs_.count(canonicalPath) != 0; }

private:
    int64_t quietMs_;
    std::unordered_map<std::string, int64_t> lastMs_;
    bool    overflow_   = false;
    int64_t overflowMs_ = 0;
};

} // namespace hreload
