// 에셋 핫리로드(2026-11-20, Docs/Asset_Hot_Reload.md): 프로젝트 폴더 감시 → 모으기 → 모델 · 의존 파일 · 씬 · 가져오기 설정 처리.
#include "EditorApp.hpp"
#include "../assets/AssetManager.hpp"
#include "../sceneio/PrefabManager.hpp"
#include "../components/AssetRebind.hpp"
#include "../core/FileUtil.hpp"
#include "../core/ProjectPaths.hpp"
#include "../core/HText.hpp"
#include "../scene/World.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../vfs/Vfs.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

static constinit HLogCategory LogHotReload{ "HotReload", ELogVerbosity::Log };

namespace {
int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

uint64_t EditorApp::sceneTextHash(const std::string& text) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
    return h;
}

void EditorApp::startAssetWatch() {
    // 프로젝트 폴더가 없으면(옮긴 실행 파일) 감시할 원본이 없습니다.
    if (hproj::root().empty()) return;
    assetWatcher_.start(hproj::root());
}

void EditorApp::tickAssetReload() {
    ++reloadFrame_;
    for (; touchNext_ < touchAssetAt_.size() && reloadFrame_ >= touchAssetAt_[touchNext_].first; ++touchNext_)
        touchAssetAndWait(touchAssetAt_[touchNext_].second);
    pumpAssetChanges();
}

size_t EditorApp::pumpAssetChanges() {
    bool overflow = false;
    const std::vector<std::string> names = assetWatcher_.drain(&overflow);
    const int64_t now = steadyMs();
    for (const auto& n : names) assetChanges_.notify(n, now);
    if (overflow) assetChanges_.notifyOverflow(now);
    if (assetChanges_.pending() == 0) return 0;

    bool rescan = false;
    const std::vector<std::string> ready = assetChanges_.takeReady(now, &rescan);
    for (const auto& p : ready) processAssetChange(p);
    if (rescan && ctx_.assets && ctx_.world) {
        HLOG(LogHotReload, Warning) << "Too many file changes at once; checking every loaded model";
        AssetManager::ReloadStats st;
        World& world = *ctx_.world;
        const auto done = ctx_.assets->reloadStale(
            [&world](const std::string& key, const std::vector<AssetPair>& fresh) { hreload::rebindModel(world, key, fresh); }, &st);
        HLOG(LogHotReload, Log) << "Hot reload: " << done.size() << " stale model(s) reloaded";
    }
    return ready.size() + (rescan ? 1 : 0);
}

void EditorApp::processAssetChange(const std::string& path) {
    if (!ctx_.assets || !ctx_.world) return;
    World& world = *ctx_.world;
    const hreload::ChangeKind kind = hreload::classify(path);

    if (kind == hreload::ChangeKind::Scene) { onSceneFileChanged(path); return; }

    if (kind == hreload::ChangeKind::Prefab) {
        // 링크된 프리팹(Docs/Linked_Prefabs.md D7): 캐시와 글이 다를 때만 다시 펼칩니다. 에디터가 쓴 저장은 캐시를 먼저 바꾸므로 넘어갑니다.
        contentPanel_.refreshPrefabs();
        std::string text, cached;
        if (!hvfs::readText(path, text)) return;
        if (!prefabCache_.peek(path, &cached)) { HLOG(LogHotReload, Verbose) << "Prefab changed (not used): " << path; return; }
        // 줄 끝만 다른 글(바깥 편집기가 CRLF 로 다시 저장 · 예전 에디터의 텍스트 모드 저장)은 같은 내용입니다(DEF-178).
        auto noCr = [](std::string s) { s.erase(std::remove(s.begin(), s.end(), '\r'), s.end()); return s; };
        if (cached == text || noCr(cached) == noCr(text)) return;
        refreshPrefab(path, text);
        return;
    }

    if (kind == hreload::ChangeKind::ImportSettings) {
        std::string text;
        std::vector<std::string> messages;
        ImportSettingsTable table;
        // 파일이 지워졌으면(DEF-177) 빈 표 = 모든 모델이 기본 설정으로 돌아갑니다.
        if (hvfs::readText(ImportSettingsTable::kPath, text)) table = ImportSettingsTable::parse(text, messages);
        for (const auto& m : messages) HLOG(LogHotReload, Warning) << ImportSettingsTable::kPath << ": " << m;
        ctx_.assets->setImportSettings(std::move(table));
        AssetManager::ReloadStats st;
        size_t rebound = 0;
        const auto done = ctx_.assets->reloadForSettingsChange(
            [&](const std::string& key, const std::vector<AssetPair>& fresh) { rebound += hreload::rebindModel(world, key, fresh); },
            &st);
        HLOG(LogHotReload, Log) << "Import settings reloaded (" << ctx_.assets->importSettings().rules().size() << " rule(s)); "
                                << done.size() << " model(s) reimported, " << rebound << " components rebound";
        return;
    }

    // 모델 자체이거나 올라간 모델의 의존 파일(.gltf 옆의 .bin · 이미지).
    const std::vector<std::string> models = ctx_.assets->modelsUsing(path);
    if (models.empty()) {
        HLOG(LogHotReload, Verbose) << "Changed (not loaded, ignored): " << path;
        return;
    }
    {
        // 지워졌거나 다른 이름으로 옮겨진 파일(DEF-177): 올라간 모델은 그대로 둡니다(다시 만들어지면 그때 알림이 옴).
        if (!hvfs::exists(path)) {
            HLOG(LogHotReload, Warning) << path << " was removed or renamed on disk; keeping the loaded copy of "
                                        << models.size() << " model(s)";
            return;
        }
    }
    for (const auto& key : models) {
        AssetManager::ReloadStats st;
        size_t rebound = 0;
        const bool ok = ctx_.assets->reloadModel(
            key, [&](const std::string& k, const std::vector<AssetPair>& fresh) { rebound += hreload::rebindModel(world, k, fresh); },
            &st);
        if (!ok) continue;
        HLOG(LogHotReload, Log) << "Hot reload: " << key << (key == path ? std::string() : " (" + path + " changed)") << " ("
                                << rebound << " components rebound, retired " << st.meshesRetired << " meshes, "
                                << st.materialsRetired << " materials)";
    }
    detailsPanel_.markDirty();   // 정점 · 삼각형 수
}

void EditorApp::onSceneFileChanged(const std::string& path) {
    if (doc_.untitled() || hvfs::toCanonical(doc_.path()) != hvfs::toCanonical(path)) return;
    std::string text;
    if (!hfile::readAll(hproj::resolve(path), text)) return;   // 지워짐 · 옮겨짐: 다음 저장이 다시 만듦
    if (sceneTextHash(text) == sceneDiskHash_) return;        // 에디터 자신의 저장 · 내용이 같은 쓰기
    if (playState_ != EPlayState::Editor || sceneDirty()) {
        // 편집 내용을 덮지 않습니다. 같은 변경을 두 번 알리지 않도록 지금 디스크 내용을 기억합니다.
        sceneDiskHash_ = sceneTextHash(text);
        HLOG(LogHotReload, Warning) << path << " was changed on disk, but the open scene "
                                    << (playState_ != EPlayState::Editor ? "is in Play" : "has unsaved changes")
                                    << "; not reloaded (reopen it to see the file)";
        return;
    }
    if (autoReloadScene_) {
        HLOG(LogHotReload, Log) << path << " was changed on disk; reloading (--auto-reload-scene)";
        openScene(path);
        return;
    }
    sceneDiskHash_ = sceneTextHash(text);   // 그대로 두기를 고르면 같은 내용으로 다시 묻지 않음
    if (modalOpen()) {
        HLOG(LogHotReload, Warning) << path << " was changed on disk; reopen it to see the file (a dialog is already open)";
        return;
    }
    ModalSpec m;
    m.title   = HTEXT("Editor.Dialog", "Reload.Title", "Scene Changed on Disk").toString();
    m.message = HText::format(HTEXT("Editor.Dialog", "Reload.Message", "{0} was changed outside the editor.\nReload it?"),
                              { path }).toString();
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Reload", "Reload").toString(), [this, path] { openScene(path); }, true, false });
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Keep", "Keep Current").toString(), {}, false, true });
    openModal(std::move(m));
}

// --touch-asset-at(D9): 수정 시각만 지금으로 바꾸고(내용은 그대로), 감시자 알림이 모여 처리될 때까지 최대 3 초 기다립니다.
void EditorApp::touchAssetAndWait(const std::string& logicalPath) {
    const std::string canon = hvfs::toCanonical(logicalPath);
    const std::filesystem::path file = hproj::resolve(canon);
    std::error_code ec;
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), ec);
    if (ec) {
        HLOG(LogHotReload, Error) << "touch-asset: cannot touch " << canon << " (" << ec.message() << ")";
        return;
    }
    if (!assetWatcher_.running()) {
        HLOG(LogHotReload, Error) << "touch-asset: " << canon << " touched, but the asset watcher is not running";
        return;
    }
    HLOG(LogHotReload, Log) << "touch-asset: " << canon << " (frame " << reloadFrame_ << ")";
    const int64_t start = steadyMs();
    bool seen = false;
    while (steadyMs() - start < 3000) {
        pumpAssetChanges();
        if (assetChanges_.has(canon)) seen = true;
        else if (seen) break;   // 모였다가 처리됨
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!seen || assetChanges_.has(canon)) {
        HLOG(LogHotReload, Error) << "touch-asset: no change notification processed for " << canon << " within 3 s";
    } else {
        HLOG(LogHotReload, Log) << "touch-asset: processed " << canon << " after " << (steadyMs() - start) << " ms";
    }
}

void EditorApp::refreshPrefab(const std::string& path, const std::string& text) {
    if (!ctx_.world || !ctx_.assets) return;
    std::vector<uint64_t> sel;
    for (const Actor* a : ctx_.selection) if (a) sel.push_back(a->id);
    const uint64_t primary = ctx_.selected ? ctx_.selected->id : 0;
    const int n = PrefabManager::refreshInstances(*ctx_.world, *ctx_.assets, path, text);
    if (n > 0) {
        // 다시 펼친 액터는 새 객체지만 옛 실행 번호를 받으므로 같은 ID 로 다시 고릅니다.
        ctx_.selection.clear();
        ctx_.selected = nullptr;
        for (uint64_t id : sel)
            if (Actor* a = ctx_.world->findById(id)) ctx_.selection.push_back(a);
        if (primary) ctx_.selected = ctx_.world->findById(primary);
        if (!ctx_.selected && !ctx_.selection.empty()) ctx_.selected = ctx_.selection.back();
        prevSelected2_ = nullptr;
    }
    outlinerPanel_.markDirty();
    detailsPanel_.markDirty();
    HLOG(LogHotReload, Log) << "Prefab " << path << ": " << n << " instance(s) rebuilt";
}
