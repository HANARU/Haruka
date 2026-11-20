#pragma once
// 씬 전환(2026-11-20, Docs/Async_Loading.md D6). 씬 글을 읽고 참조한 모델을 백그라운드로 미리 요청한 뒤, 기다리는 동안 지금 씬을
// 그대로 틱 · 그리게 두고(XR 프레임 제출 유지), 모두 올라가면 한 프레임에 액터를 바꿉니다. 바꾼 뒤 새 씬이 쓰지 않는 모델은
// 해제합니다. update 는 월드 틱 밖(앞단 tick)에서 부르십시오.

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

class World;
class AssetManager;

class SceneTransition {
public:
    enum class Phase { Idle, Loading };

    struct Stats {
        std::string path;
        size_t      models = 0;          // 씬이 참조한 모델 수
        int         waitFrames = 0;      // 요청부터 바꾸기까지 지난 update 수
        float       waitMs = 0.f;        // 요청부터 바꾸기까지 실제 시간
        float       longestFrameMs = 0.f;// 기다리는 동안 update 사이 가장 긴 간격(프레임 시간)
        float       swapMs = 0.f;        // 액터를 지우고 새 씬을 읽은 시간
        size_t      released = 0;        // 해제한 모델 수
        size_t      actors = 0;
    };

    // 씬을 요청합니다. 글을 읽지 못하면 false. sync 면 미리 읽기 없이 다음 update 에서 바로 바꿉니다(비교용, 모델은 동기로 읽힘).
    bool request(const std::string& logicalPath, AssetManager& assets, bool sync = false);
    // 매 프레임 부릅니다. 이번 호출에서 씬을 바꿨으면 true(stats() 가 그 결과).
    bool update(World& world, AssetManager& assets);

    Phase              phase() const { return phase_; }
    const std::string& target() const { return path_; }
    const Stats&       stats() const { return stats_; }

    // 씬 글에서 모델 참조(.glb · .gltf, 정식 논리 경로)를 모읍니다(시험용으로 공개).
    static std::vector<std::string> modelRefs(const std::string& file, const std::string& text);

private:
    using Clock = std::chrono::steady_clock;
    Phase                    phase_ = Phase::Idle;
    bool                     sync_  = false;
    std::string              path_;
    std::string              text_;
    std::vector<std::string> models_;
    Clock::time_point        start_{}, last_{};
    Stats                    stats_;
};
