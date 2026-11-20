#include "AssetStreamer.hpp"
#include "AssetLog.hpp"
#include "GassetCache.hpp"
#include "GltfLoader.hpp"
#include "../core/Profiler.hpp"

#include <exception>

AssetStreamer::AssetStreamer(int workers) {
    if (workers < 1) workers = 1;
    threads_.reserve((size_t)workers);
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { run(); });
}

AssetStreamer::~AssetStreamer() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
        for (const Job& j : queue_) states_.erase(j.path);
        queue_.clear();
    }
    cv_.notify_all();
    for (std::thread& t : threads_) if (t.joinable()) t.join();
}

bool AssetStreamer::request(const std::string& path, const ImportSettings& settings) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stop_ || states_.count(path)) return false;
        states_[path] = State::Queued;
        queue_.push_back({ path, settings });
    }
    cv_.notify_one();
    return true;
}

AssetStreamer::State AssetStreamer::state(const std::string& path) const {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = states_.find(path);
    return it == states_.end() ? State::None : it->second;
}

std::vector<AssetStreamer::Result> AssetStreamer::takeFinished(size_t max) {
    std::vector<Result> out;
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = done_.begin(); it != done_.end() && out.size() < max;) {
        out.push_back(std::move(it->second));
        states_.erase(it->first);
        it = done_.erase(it);
    }
    return out;
}

bool AssetStreamer::waitFor(const std::string& path, Result& out) {
    std::unique_lock<std::mutex> lk(mu_);
    if (!states_.count(path)) return false;
    doneCv_.wait(lk, [&] { return done_.count(path) || !states_.count(path); });
    const auto it = done_.find(path);
    if (it == done_.end()) return false;   // 멈춤으로 버려짐
    out = std::move(it->second);
    done_.erase(it);
    states_.erase(path);
    return true;
}

size_t AssetStreamer::inFlight() const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t n = 0;
    for (const auto& [p, s] : states_) n += (s == State::Queued || s == State::Loading) ? 1 : 0;
    return n;
}

size_t AssetStreamer::pending() const {
    std::lock_guard<std::mutex> lk(mu_);
    return states_.size();
}

bool AssetStreamer::decode(const std::string& path, std::vector<MeshSourceData>& out, std::string& error) {
    return decode(path, ImportSettings{}, out, error, nullptr);
}

bool AssetStreamer::decode(const std::string& path, const ImportSettings& settings, std::vector<MeshSourceData>& out,
                           std::string& error, std::vector<std::string>* deps) {
    std::vector<std::string> localDeps;
    std::vector<std::string>& d = deps ? *deps : localDeps;
    try {
        const uint64_t sh = settings.hash();
        if (GassetCache::isValid(path, sh, &d)) {
            try {
                out = GassetCache::load(path);
                return true;
            } catch (const std::exception& cacheEx) {
                HLOG(LogAsset, Warning) << "Cache corrupted (" << path << "): " << cacheEx.what() << " -> reloading from source";
            }
        }
        out = GltfLoader::loadAll(path, settings, &d);
        GassetCache::save(path, out, sh, d);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        out.clear();
        return false;
    }
}

void AssetStreamer::attachTextures(const std::string& path, std::vector<MeshSourceData>& meshes, htex::Target t) {
    if (t == htex::Target::Off) return;
    const htex::AttachStats st = htex::attachCompressed(meshes, t);
    if (st.missing || st.invalid) {
        HLOG(LogAsset, Warning) << "Texture compression (" << path << "): " << st.found << " KTX2 attached, " << st.missing
                                << " missing, " << st.invalid << " unreadable -> those use the RGBA8 originals";
    } else if (st.found) {
        HLOG(LogAsset, Verbose) << "Texture compression (" << path << "): " << st.found << " KTX2 attached";
    }
}

void AssetStreamer::run() {
    HPROF_THREAD_NAME("AssetWorker");
    for (;;) {
        std::string    path;
        ImportSettings settings;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            path     = std::move(queue_.front().path);
            settings = queue_.front().settings;
            queue_.pop_front();
            states_[path] = State::Loading;
        }
        Result r;
        r.path = path;
        {
            HPROF_SCOPE("Asset.Decode");
            r.ok = decode(path, settings, r.meshes, r.error, &r.deps);
            if (r.ok) attachTextures(path, r.meshes, (htex::Target)texTarget_.load());
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            states_[path] = State::Done;
            done_[path] = std::move(r);
        }
        doneCv_.notify_all();
    }
}
