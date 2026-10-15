#include "PrefabManager.hpp"
#include "AssetLog.hpp"
#include "SceneSerializer.hpp"
#include "../core/Actor.hpp"
#include "../core/ProjectPaths.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

void PrefabManager::save(const Actor* actor, const std::string& path) {
    // 에디터 빌드는 프로젝트 폴더에 씁니다(hproj). 실행 폴더에 쓰면 다음 빌드의 에셋 복사가 덮어씁니다.
    fs::create_directories(hproj::resolve(kDir));
    std::ofstream f(hproj::resolve(path));
    if (!f) { HLOG(LogAsset, Error) << "Prefab cannot write: " << path; return; }

    f << "# Haruka Prefab v2\n";   // v2: 컴포넌트를 COMPONENT 블록으로(DEF-22). v1 도 읽습니다
    f << "NAME:" << actor->name << "\n";
    SceneSerializer::writeActor(f, actor);
    HLOG(LogAsset, Log) << "Prefab saved: " << path;
}

Actor* PrefabManager::spawn(const std::string& path, World& world, AssetManager& assets) {
    std::ifstream f(hproj::resolve(path));
    if (!f) { HLOG(LogAsset, Error) << "Prefab not found: " << path; return nullptr; }

    SceneSerializer::ActorData data;
    std::string line;
    while (std::getline(f, line)) {
        // readScene 과 같은 규칙입니다. 텍스트 모드가 아닌 경로에서는 CRLF 의 \r 이 값 끝에
        // 남아 PRIM 값과 파일 경로가 조용히 어긋납니다.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("NAME:", 0) == 0 && !data.components.open()) data.name = line.substr(5);
        else SceneSerializer::parseActorLine(line, data);
    }

    Actor* a = SceneSerializer::spawn(data, world, assets);
    if (!a) { HLOG(LogAsset, Error) << "Prefab missing NAME in: " << path; return nullptr; }

    HLOG(LogAsset, Log) << "Prefab spawned: " << data.name << " from " << path;
    return a;
}

std::vector<std::string> PrefabManager::scan() {
    std::vector<std::string> result;
    std::error_code ec;
    const fs::path dir = hproj::resolve(kDir);
    if (!fs::exists(dir, ec)) return result;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() == kExt)
            result.push_back(e.path().stem().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}
