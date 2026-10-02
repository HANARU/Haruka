#include "PrefabManager.hpp"
#include "AssetLog.hpp"
#include "SceneSerializer.hpp"
#include "../core/Actor.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

void PrefabManager::save(const Actor* actor, const std::string& path) {
    fs::create_directories(kDir);
    std::ofstream f(path);
    if (!f) { HLOG(LogAsset, Error) << "Prefab cannot write: " << path; return; }

    f << "# Haruka Prefab v1\n";
    f << "NAME:" << actor->name << "\n";
    SceneSerializer::writeActor(f, actor);
    HLOG(LogAsset, Log) << "Prefab saved: " << path;
}

Actor* PrefabManager::spawn(const std::string& path, World& world, AssetManager& assets) {
    std::ifstream f(path);
    if (!f) { HLOG(LogAsset, Error) << "Prefab not found: " << path; return nullptr; }

    SceneSerializer::ActorData data;
    std::string line;
    while (std::getline(f, line)) {
        // readScene 과 같은 규칙입니다. 텍스트 모드가 아닌 경로에서는 CRLF 의 \r 이 값 끝에
        // 남아 PRIM 값과 파일 경로가 조용히 어긋납니다.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        if (key == "NAME") data.name = val;
        else               SceneSerializer::parseLine(key, val, data);
    }

    Actor* a = SceneSerializer::spawn(data, world, assets);
    if (!a) { HLOG(LogAsset, Error) << "Prefab missing NAME in: " << path; return nullptr; }

    HLOG(LogAsset, Log) << "Prefab spawned: " << data.name << " from " << path;
    return a;
}

std::vector<std::string> PrefabManager::scan() {
    std::vector<std::string> result;
    std::error_code ec;
    if (!fs::exists(kDir, ec)) return result;
    for (auto& e : fs::directory_iterator(kDir, ec)) {
        if (e.path().extension() == kExt)
            result.push_back(e.path().stem().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}
