#include "PrefabManager.hpp"
#include "PrefabRules.hpp"
#include "../assets/AssetLog.hpp"
#include "SceneSerializer.hpp"
#include "PrefabLink.hpp"
#include "../scene/World.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../scene/Actor.hpp"
#include "../core/FileUtil.hpp"
#include "../core/ProjectPaths.hpp"
#include "../vfs/Vfs.hpp"
#include "../vfs/PathUtf8.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

// 프리팹 v3(2026-10-15)은 액터 하나가 아니라 서브트리입니다(UE · Unity · Godot 의 프리팹처럼). 내용은 씬과 같은 액터
// 블록이고 부모는 블록 순서의 인덱스(PARENT:)로 가리키며, 첫 블록이 루트입니다. v1 · v2(액터 하나, NAME: 줄)도 읽습니다.
bool PrefabManager::save(const Actor* root, const std::string& path, std::string* written) {
    if (!root) return false;
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

    // 안의 인스턴스가 저장할 파일에 (중첩으로라도) 기대면 순환이라 거부합니다(D8). 루트가 바로 그 프리팹의 인스턴스면 풀어 씁니다.
    const hprefablink::Loader load = hprefablink::loaderFor(root->world());
    const std::string canon = hvfs::toCanonical(path);
    for (size_t i = 0; i < set.size(); ++i) {
        const std::string& src = set[i]->prefabLink.source;
        if (src.empty() || (i == 0 && hvfs::toCanonical(src) == canon)) continue;
        if (hprefablink::dependsOn(src, path, load)) {
            HLOG(LogAsset, Error) << "Prefab not saved: '" << set[i]->name << "' is an instance of " << src
                                  << ", which uses " << path << " (a prefab cannot contain itself)";
            return false;
        }
    }

    // 에디터 빌드는 프로젝트 폴더에 씁니다(hproj). 실행 폴더에 쓰면 다음 빌드의 에셋 복사가 덮어씁니다.
    // 임시 파일에 다 쓴 뒤 바꿔, 쓰다 실패해도 원래 프리팹이 남습니다.
    const std::string text = std::string(hprefab::kPrefabHeaderV6) + "\n" + hprefablink::writeCompact(set, parents, load, path);
    std::string err;
    if (!hfile::writeAtomic(hproj::resolve(path), text, &err)) {
        HLOG(LogAsset, Error) << "Prefab cannot write: " << path << " (" << err << ")";
        return false;
    }
    if (written) *written = text;
    HLOG(LogAsset, Log) << "Prefab saved: " << path << " (" << set.size() << " actors)";
    return true;
}

int PrefabManager::refreshInstances(World& world, AssetManager& assets, const std::string& path, const std::string& newText) {
    // 다시 펼침(D7): 옛 캐시 기준으로 인스턴스마다 덮어쓰기를 구해 글로 떠 두고, 지우고, 캐시를 바꾸고, 다시 펼칩니다.
    const hprefablink::Loader oldLoad = hprefablink::loaderFor(&world);
    std::vector<Actor*> affected;
    for (const auto& ap : world.getActors()) {
        Actor* a = ap.get();
        if (a->prefabLink.source.empty()) continue;
        if (hprefablink::dependsOn(a->prefabLink.source, path, oldLoad)) affected.push_back(a);
    }
    // 조상도 다시 펼치면 그 안에서 함께 펼쳐집니다.
    std::vector<Actor*> roots;
    for (Actor* a : affected) {
        bool inner = false;
        for (Actor* p = a->getParent(); p && !inner; p = p->getParent())
            inner = std::find(affected.begin(), affected.end(), p) != affected.end();
        if (!inner) roots.push_back(a);
    }

    struct Pending {
        std::string                         text;
        std::unordered_map<HGuid, uint64_t> ids;
        uint64_t                            parentId = 0;
        int                                 index    = -1;
    };
    std::vector<Pending> pending;
    for (Actor* r : roots) {
        std::vector<const Actor*> set;
        std::vector<int>          parents;
        std::function<void(const Actor*, int)> walk = [&](const Actor* a, int parent) {
            const int me = (int)set.size();
            set.push_back(a);
            parents.push_back(parent);
            for (const Actor* c : a->getChildren()) walk(c, me);
        };
        walk(r, -1);
        Pending p;
        p.text = hprefablink::writeCompact(set, parents, oldLoad);
        for (const Actor* a : set) p.ids[a->guid()] = a->id;
        if (Actor* parent = r->getParent()) {
            p.parentId = parent->id;
            const auto& kids = parent->getChildren();
            p.index = (int)(std::find(kids.begin(), kids.end(), r) - kids.begin());
        }
        pending.push_back(std::move(p));
    }
    for (Actor* r : roots) world.destroyActor(r);

    if (auto* cache = world.service<hprefablink::PrefabSourceCache>()) cache->set(path, newText);

    for (const Pending& p : pending) {
        const std::vector<Actor*> slots = SceneSerializer::readScene(p.text, world, assets);
        for (Actor* a : slots)
            if (a)
                if (auto it = p.ids.find(a->guid()); it != p.ids.end()) world.reassignId(a, it->second);
        if (!slots.empty() && slots[0] && p.parentId)
            if (Actor* parent = world.findById(p.parentId)) slots[0]->attachTo(parent, p.index);
    }
    if (!pending.empty()) {
        HLOG(LogAsset, Log) << "Prefab " << path << " changed: " << pending.size() << " instance(s) rebuilt";
    }
    return (int)pending.size();
}

Actor* PrefabManager::spawn(const std::string& path, World& world, AssetManager& assets) {
    // 읽기는 가상 파일 시스템으로 합니다(에디터는 프로젝트 폴더, 게임은 묶음 파일, 2026-10-24). 줄 끝은 그대로 오며
    // 아래 두 파서가 '\r' 을 지웁니다.
    std::string text;
    if (!hvfs::readText(path, text)) { HLOG(LogAsset, Error) << "Prefab not found: " << path; return nullptr; }

    // BOM 을 떼고, 머리 줄 앞의 빈 줄 · 주석을 건너뛰어 v3 를 판별합니다(DEF-142).
    if (hprefab::stripBomAndDetectV3(text)) {
        // 링크된 인스턴스로 꺼냅니다(Docs/Linked_Prefabs.md). 루트에 새 GUID 를 주고, 안의 액터는 그 값에서 파생합니다(D3).
        // 프리팹 안을 가리키던 참조는 이번 인스턴스 안을 가리킵니다(R-5 의 Fresh 와 같은 효과).
        HGuid g;
        do { g = HGuid::make(); } while (world.findByGuid(g));
        const std::string inst = "PREFAB:" + path + "\nGUID:" + g.toString() + "\nEND_PREFAB\n";
        const std::vector<Actor*> slots = SceneSerializer::readScene(inst, world, assets);
        if (!slots.empty() && slots[0] && !slots[0]->prefabLink.raw.empty()) {
            // 펼치지 못함(순환 · 읽기 실패): 빈 루트를 남기지 않습니다.
            world.destroyActor(slots[0]);
            HLOG(LogAsset, Error) << "Prefab not spawned: " << path;
            return nullptr;
        }
        // 루트 · 중간 블록이 실패해 서브트리 밖에 남은 액터는 지웁니다(DEF-141). 루트가 없으면 꺼낸 것을 모두 지웁니다.
        if (const int n = hprefab::discardOrphans(world, slots); n > 0) {
            HLOG(LogAsset, Warning) << "Prefab " << path << ": removed " << n
                                    << " actor subtree(s) left outside the root after a failed block";
        }
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
    // 하위 폴더도 훑습니다(참조 체계: 옮긴 프리팹도 목록에 남게). 항목은 kDir 아래 상대 경로에서 확장자를 뺀 것(UTF-8)입니다.
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != kExt) continue;
        fs::path rel = fs::relative(it->path(), dir, ec);
        if (ec) { ec.clear(); continue; }
        rel.replace_extension();
        const std::string s = hpak::utf8Of(rel);
        if (!s.empty()) result.push_back(s);
    }
    std::sort(result.begin(), result.end());
    return result;
}
