#include "PrefabManager.hpp"
#include "AssetLog.hpp"
#include "SceneSerializer.hpp"
#include "../core/Actor.hpp"
#include "../core/FileUtil.hpp"
#include "../core/ProjectPaths.hpp"
#include "../vfs/Vfs.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

// 프리팹 v3(2026-10-15)은 액터 하나가 아니라 서브트리입니다(UE · Unity · Godot 의 프리팹처럼). 내용은 씬과 같은 액터
// 블록이고 부모는 블록 순서의 인덱스(PARENT:)로 가리키며, 첫 블록이 루트입니다. v1 · v2(액터 하나, NAME: 줄)도 읽습니다.
void PrefabManager::save(const Actor* root, const std::string& path) {
    // 전위 순회(부모가 자식보다 앞). 씬 저장과 같은 형식이라 SceneSerializer 가 그대로 읽습니다.
    std::vector<const Actor*> set;
    std::vector<int>          parents;
    std::function<void(const Actor*, int)> walk = [&](const Actor* a, int parent) {
        const int me = (int)set.size();
        set.push_back(a);
        parents.push_back(parent);
        for (const Actor* c : a->getChildren()) walk(c, me);
    };
    walk(root, -1);

    // 에디터 빌드는 프로젝트 폴더에 씁니다(hproj). 실행 폴더에 쓰면 다음 빌드의 에셋 복사가 덮어씁니다.
    // 임시 파일에 다 쓴 뒤 바꿔, 쓰다 실패해도 원래 프리팹이 남습니다.
    std::string err;
    if (!hfile::writeAtomic(hproj::resolve(path), "# Haruka Prefab v3\n" + SceneSerializer::writeActorSet(set, parents), &err)) {
        HLOG(LogAsset, Error) << "Prefab cannot write: " << path << " (" << err << ")";
        return;
    }
    HLOG(LogAsset, Log) << "Prefab saved: " << path << " (" << set.size() << " actors)";
}

Actor* PrefabManager::spawn(const std::string& path, World& world, AssetManager& assets) {
    // 읽기는 가상 파일 시스템으로 합니다(에디터는 프로젝트 폴더, 게임은 묶음 파일, 2026-10-24). 줄 끝은 그대로 오며
    // 아래 두 파서가 '\r' 을 지웁니다.
    std::string text;
    if (!hvfs::readText(path, text)) { HLOG(LogAsset, Error) << "Prefab not found: " << path; return nullptr; }

    if (text.rfind("# Haruka Prefab v3", 0) == 0) {
        const std::vector<Actor*> slots = SceneSerializer::readScene(text, world, assets);
        if (slots.empty() || !slots[0]) { HLOG(LogAsset, Error) << "Prefab has no root actor: " << path; return nullptr; }
        HLOG(LogAsset, Log) << "Prefab spawned: " << slots[0]->name << " from " << path << " (" << slots.size() << " actors)";
        return slots[0];
    }

    // v1 · v2: 액터 하나(NAME: 줄 + 액터 줄)
    SceneSerializer::ActorData data;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
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
