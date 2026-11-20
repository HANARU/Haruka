#include "SceneTransition.hpp"

#include "../assets/AssetManager.hpp"
#include "../core/AssetRefs.hpp"
#include "../core/HLog.hpp"
#include "../scene/World.hpp"
#include "../sceneio/SceneSerializer.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../vfs/Vfs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

static constinit HLogCategory LogSceneTransition{ "Scene", ELogVerbosity::Log };

std::vector<std::string> SceneTransition::modelRefs(const std::string& file, const std::string& text) {
    std::vector<std::string> out;
    for (const hrefs::Ref& r : hrefs::refsInText(file, text)) {
        std::string ext = r.target.size() > 5 ? r.target.substr(r.target.size() - 5) : r.target;
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        const bool model = ext == ".gltf" || (ext.size() >= 4 && ext.substr(ext.size() - 4) == ".glb");
        if (!model) continue;
        const std::string key = hvfs::toCanonical(r.target);
        if (std::find(out.begin(), out.end(), key) == out.end()) out.push_back(key);
    }
    return out;
}

bool SceneTransition::request(const std::string& logicalPath, AssetManager& assets, bool sync) {
    std::string text;
    if (!hvfs::readText(logicalPath, text)) {
        HLOG(LogSceneTransition, Error) << "Scene not found: " << logicalPath;
        return false;
    }
    if (phase_ == Phase::Loading) {
        HLOG(LogSceneTransition, Log) << "Scene transition to " << path_ << " is replaced by " << logicalPath
                                      << " (models already requested stay loaded until the next swap releases them)";
    }
    path_   = logicalPath;
    text_   = std::move(text);
    sync_   = sync;
    models_ = modelRefs(logicalPath, text_);
    stats_  = Stats{};
    stats_.path   = path_;
    stats_.models = models_.size();
    if (!sync_) for (const std::string& m : models_) assets.requestLoad(m);
    phase_ = Phase::Loading;
    start_ = last_ = Clock::now();
    HLOG(LogSceneTransition, Log) << "Scene transition requested: " << path_ << " (" << models_.size() << " model(s)"
                                  << (sync_ ? ", synchronous" : ", loading in the background") << ")";
    return true;
}

bool SceneTransition::update(World& world, AssetManager& assets) {
    if (phase_ != Phase::Loading) return false;
    const auto now = Clock::now();
    stats_.longestFrameMs = std::max(stats_.longestFrameMs, std::chrono::duration<float, std::milli>(now - last_).count());
    last_ = now;
    if (!sync_) {
        for (const std::string& m : models_)
            if (!assets.isReady(m)) { ++stats_.waitFrames; return false; }
    }
    stats_.waitMs = std::chrono::duration<float, std::milli>(now - start_).count();

    // 바꾸기: 액터를 모두 지우고(틱 밖이라 바로) 새 씬을 읽습니다. 모델은 캐시에 있어 바로 붙습니다(동기 모드는 여기서 읽음).
    const auto t0 = Clock::now();
    std::vector<Actor*> all;
    for (const auto& a : world.getActors()) all.push_back(a.get());
    for (Actor* a : all) world.destroyActor(a);
    SceneSerializer::readScene(text_, world, assets);
    stats_.swapMs = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
    stats_.actors = world.getActors().size();

    // 새 씬이 쓰지 않는 모델을 해제합니다(GPU 자원은 렌더러가 그리던 프레임이 끝난 뒤 지움).
    std::unordered_set<std::string> keep(models_.begin(), models_.end());
    stats_.released = assets.releaseUnused(keep);
    phase_ = Phase::Idle;
    text_.clear();

    char buf[256];
    std::snprintf(buf, sizeof(buf), "waited %d frame(s) %.1f ms, longest frame while loading %.2f ms, swap %.2f ms, "
                  "released %zu model(s), %zu actor(s)", stats_.waitFrames, stats_.waitMs, stats_.longestFrameMs,
                  stats_.swapMs, stats_.released, stats_.actors);
    HLOG(LogSceneTransition, Log) << "Scene transition done: " << path_ << ": " << buf;
    return true;
}
