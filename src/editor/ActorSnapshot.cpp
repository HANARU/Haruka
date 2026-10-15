#include "ActorSnapshot.hpp"
#include "../core/Actor.hpp"
#include "../core/HLog.hpp"
#include "../core/World.hpp"

#include <unordered_map>

static constinit HLogCategory LogEditor{ "Editor", ELogVerbosity::Log };

namespace hsnap {

std::vector<const Actor*> collectSubtree(const Actor* root) {
    std::vector<const Actor*> out;
    if (!root) return out;
    std::vector<const Actor*> stack{ root };
    while (!stack.empty()) {
        const Actor* a = stack.back();
        stack.pop_back();
        out.push_back(a);
        // 스택이라 뒤에서부터 넣어야 붙인 순서대로 꺼냅니다.
        const auto& kids = a->getChildren();
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    }
    return out;
}

ActorSnapshot capture(const Actor* root, const SnapshotWriter& write) {
    ActorSnapshot snap;
    if (!root) return snap;
    const std::vector<const Actor*> ordered = collectSubtree(root);

    std::unordered_map<const Actor*, int> indexOf;
    for (int i = 0; i < (int)ordered.size(); ++i) indexOf[ordered[i]] = i;
    std::vector<int> parentIndex(ordered.size(), -1);
    for (size_t i = 1; i < ordered.size(); ++i) {   // 루트(0)의 부모는 세트 밖이라 -1
        auto it = indexOf.find(ordered[i]->getParent());
        if (it != indexOf.end()) parentIndex[i] = it->second;
    }

    snap.text = write(ordered, parentIndex);
    snap.ids.reserve(ordered.size());
    for (const Actor* a : ordered) snap.ids.push_back(a->id);
    snap.parentId = root->getParent() ? root->getParent()->id : 0;
    return snap;
}

Actor* restore(const ActorSnapshot& snap, World& world, const SnapshotReader& read) {
    if (snap.ids.empty()) return nullptr;
    for (uint64_t id : snap.ids) {
        if (world.findById(id)) {
            HLOG(LogEditor, Warning) << "Undo restore skipped: actor id " << id << " is already in use";
            return nullptr;
        }
    }

    std::vector<Actor*> spawned = read(snap.text);
    // 블록 수가 다르면 ID 를 맞출 수 없습니다. 만든 것을 지우고 실패로 돌립니다.
    if (spawned.size() != snap.ids.size() || !spawned[0]) {
        HLOG(LogEditor, Error) << "Undo restore failed: snapshot produced " << spawned.size()
                               << " actor slot(s), expected " << snap.ids.size();
        for (Actor* a : spawned)
            if (a && !a->getParent()) world.destroyActor(a);   // 루트만 지우면 자손도 함께 지워짐
        return nullptr;
    }

    for (size_t i = 0; i < spawned.size(); ++i)
        if (spawned[i]) world.reassignId(spawned[i], snap.ids[i]);

    Actor* root = spawned[0];
    if (snap.parentId != 0) {
        if (Actor* parent = world.findById(snap.parentId)) {
            root->attachTo(parent);   // 로컬 트랜스폼을 그대로 두므로 지우기 전 월드 자세로 돌아옴
        } else {
            HLOG(LogEditor, Warning) << "Undo restore: original parent of '" << root->name
                                     << "' no longer exists; restored at the world root";
        }
    }
    return root;
}

} // namespace hsnap
