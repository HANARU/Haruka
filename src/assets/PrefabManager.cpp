#include "PrefabManager.hpp"
#include "SceneSerializer.hpp"
#include "../core/Actor.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

void PrefabManager::save(const Actor* actor, const std::string& path) {
    fs::create_directories(kDir);
    std::ofstream f(path);
    if (!f) { std::cerr << "[Prefab] Cannot write: " << path << "\n"; return; }

    f << "# Haruka Prefab v1\n";
    f << "NAME:" << actor->name << "\n";
    SceneSerializer::writeActor(f, actor);
    std::cout << "[Prefab] Saved: " << path << "\n";
}

Actor* PrefabManager::spawn(const std::string& path, World& world, AssetManager& assets) {
    std::ifstream f(path);
    if (!f) { std::cerr << "[Prefab] Not found: " << path << "\n"; return nullptr; }

    SceneSerializer::ActorData data;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        if (key == "NAME") data.name = val;
        else               SceneSerializer::parseLine(key, val, data);
    }

    Actor* a = SceneSerializer::spawn(data, world, assets);
    if (!a) { std::cerr << "[Prefab] Missing NAME in: " << path << "\n"; return nullptr; }

    std::cout << "[Prefab] Spawned: " << data.name << " from " << path << "\n";
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
