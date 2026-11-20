#include "AssetRebind.hpp"
#include "StaticMeshComponent.hpp"
#include "SkinnedMeshComponent.hpp"
#include "../assets/AssetManager.hpp"
#include "../scene/Actor.hpp"
#include "../scene/SceneLog.hpp"
#include "../scene/World.hpp"
#include "../vfs/LogicalPath.hpp"

#include <algorithm>

namespace hreload {

namespace {

// 새 목록 안의 번호. 범위를 넘으면 마지막 것으로 줄이고 알립니다. 목록이 비면 -1.
int pickIndex(int want, size_t count, const Actor* owner, const std::string& path) {
    if (count == 0) return -1;
    const int idx = std::clamp(want, 0, (int)count - 1);
    if (idx != want) {
        HLOG(LogScene, Warning) << "actor '" << (owner ? owner->name : std::string("?")) << "': " << path << " now has "
                                << count << " mesh(es); meshIndex " << want << " -> " << idx;
    }
    return idx;
}

} // namespace

size_t rebindModel(World& world, const std::string& canonicalPath, const std::vector<AssetPair>& fresh) {
    size_t n = 0;
    for (StaticMeshComponent* c : world.componentsOf<StaticMeshComponent>()) {
        if (c->primitive || c->modelPath.empty() || hvfs::toCanonical(c->modelPath) != canonicalPath) continue;
        const int idx = pickIndex(c->meshIndex, fresh.size(), c->owner, canonicalPath);
        if (idx < 0) continue;
        c->assignMeshFromPair(fresh[(size_t)idx]);
        c->material  = fresh[(size_t)idx].material;
        c->meshIndex = idx;
        ++n;
    }
    for (SkinnedMeshComponent* c : world.componentsOf<SkinnedMeshComponent>()) {
        if (c->modelPath.empty() || hvfs::toCanonical(c->modelPath) != canonicalPath) continue;
        const int idx = pickIndex(c->meshIndex, fresh.size(), c->owner, canonicalPath);
        if (idx < 0) continue;
        const AssetPair& a = fresh[(size_t)idx];
        c->meshIndex = idx;
        c->baseMesh  = a.mesh;
        c->material  = a.material;
        c->triCount  = a.triCount;
        if (a.skin && a.bindVertices) {
            c->setSkinData(a.skin, a.bindVertices);
        } else {
            HLOG(LogScene, Warning) << "actor '" << (c->owner ? c->owner->name : std::string("?")) << "': " << canonicalPath
                                    << " mesh " << idx << " has no skin after reload; SkinnedMeshComponent draws nothing";
            c->setSkinData(nullptr, nullptr);
        }
        ++n;
    }
    return n;
}

} // namespace hreload
