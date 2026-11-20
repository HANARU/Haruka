#pragma once
// 다시 읽은 모델의 옛 자원 고르기(2026-11-20, Docs/Asset_Hot_Reload.md D5). 순수 계산이라 GPU 없이 시험합니다.
// 옛 AssetPair 목록에서 메시 · LOD · 머티리얼 핸들을 한 번씩 모으되, 새 목록에도 있는 핸들(같은 핸들을 다시 쓴 경우)과
// null 은 뺍니다. AssetManager 가 이 목록을 렌더러의 은퇴 목록(retireMesh · retireMaterial)으로 보냅니다.

#include "AssetManager.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace hreload {

struct RetireList {
    std::vector<MeshHandle*>     meshes;      // 본 메시 + LOD
    std::vector<MaterialHandle*> materials;
};

inline RetireList retireList(const std::vector<AssetPair>& oldPairs, const std::vector<AssetPair>& freshPairs) {
    std::unordered_set<const void*> keep;
    for (const AssetPair& p : freshPairs) {
        keep.insert(p.mesh);
        keep.insert(p.material);
        for (MeshHandle* l : p.lods) keep.insert(l);
    }
    RetireList out;
    std::unordered_set<const void*> seen;
    auto addMesh = [&](MeshHandle* m) {
        if (m && !keep.count(m) && seen.insert(m).second) out.meshes.push_back(m);
    };
    for (const AssetPair& p : oldPairs) {
        for (MeshHandle* l : p.lods) addMesh(l);
        addMesh(p.mesh);
        if (p.material && !keep.count(p.material) && seen.insert(p.material).second) out.materials.push_back(p.material);
    }
    return out;
}

} // namespace hreload
