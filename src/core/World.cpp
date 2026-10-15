#include "World.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/LightComponent.hpp"
#include <algorithm>

Actor* World::spawnActor(const std::string& name) {
    auto  actor  = std::make_unique<Actor>();
    actor->name  = name;
    actor->id    = nextId_++;
    Actor* ptr   = actor.get();
    idMap_[ptr->id] = ptr;
    actors.push_back(std::move(actor));
    onActorSpawned.broadcast(ptr);
    return ptr;
}

void World::destroyActor(Actor* actor) {
    if (ticking_) {
        pendingDestroy_.push_back(actor);
        return;
    }
    destroyImmediate(actor);
}

void World::destroyImmediate(Actor* actor) {
    // 멱등 가드: 이 월드가 더 이상 소유하지 않는 액터면 broadcast·역참조 없이
    // 조용히 무시합니다. 포인터 값만 비교하므로 actor 를 역참조하지 않습니다.
    // 막는 것: 틱 중 같은 액터를 두 번 큐잉하는 순차 이중 파괴, 이 월드 소유가
    //   아닌 포인터.
    // 막지 못하는 것(현재 트리에는 해당 경로 없음, 미래 주의): (a) 파괴 핸들러가
    //   자기 액터를 다시 파괴하는 재진입 — erase 가 함수 끝에 있어 안쪽 프레임이
    //   먼저 해제하면 바깥 프레임이 UAF. (b) 해제 후 주소 재사용 시 옛 포인터가
    //   새 액터와 우연히 일치. 파괴된 액터의 포인터를 보관했다 넘기지 마세요.
    auto owned = std::find_if(actors.begin(), actors.end(),
        [actor](const auto& a) { return a.get() == actor; });
    if (owned == actors.end()) return;

    onActorDestroyed.broadcast(actor);
    actor->notifyDestroy();
    idMap_.erase(actor->id);
    // 자식 서브트리 재귀 파괴 (부모 파괴 시 자식도 파괴 — 유니티 규약). 복사 후
    // 순회하는 이유는 destroyImmediate → 자식 detach 가 원본 children_ 을 수정하기
    // 때문입니다. 각 자식도 onActorDestroyed 를 발행하므로 커맨드 purge·선택 포인터
    // 무효화가 서브트리 전체에 함께 걸립니다. 이 액터 자신은 마지막에 erase 되므로
    // 재귀 도중에도 살아 있어 UAF 가 없습니다(재귀는 다른 액터에 대한 것).
    auto children = actor->getChildren();
    for (Actor* child : children) destroyImmediate(child);
    actor->detach();   // 부모가 있으면 자신을 그 children_ 에서 분리
    actors.erase(std::remove_if(actors.begin(), actors.end(),
        [actor](const auto& a) { return a.get() == actor; }),
        actors.end());
}

void World::flushDestroyQueue() {
    // Work on a local copy — destroyImmediate could theoretically queue more
    std::vector<Actor*> queue;
    queue.swap(pendingDestroy_);
    for (Actor* a : queue) destroyImmediate(a);
}

void World::tick(float dt) {
    ticking_ = true;
    for (auto& a : actors)
        if (a->activeInHierarchy()) a->tick(dt);
    ticking_ = false;
    flushDestroyQueue();
}

Actor* World::findById(uint64_t id) const {
    auto it = idMap_.find(id);
    return it != idMap_.end() ? it->second : nullptr;
}

bool World::reassignId(Actor* actor, uint64_t newId) {
    if (!actor || newId == 0 || ticking_) return false;
    if (actor->id == newId) return true;
    if (idMap_.count(newId)) return false;
    auto owned = std::find_if(actors.begin(), actors.end(),
        [actor](const auto& a) { return a.get() == actor; });
    if (owned == actors.end()) return false;

    idMap_.erase(actor->id);
    actor->id      = newId;
    idMap_[newId]  = actor;
    // 앞으로 스폰할 액터가 이 ID 를 다시 받지 않게 합니다. 되살리는 ID 는 예전에 나눠 준 값이라
    // 보통은 이미 nextId_ 보다 작습니다.
    if (newId >= nextId_) nextId_ = newId + 1;

    // ID 오름차순 자리로 옮깁니다.
    std::unique_ptr<Actor> moved = std::move(*owned);
    actors.erase(owned);
    auto at = std::lower_bound(actors.begin(), actors.end(), newId,
        [](const std::unique_ptr<Actor>& a, uint64_t id) { return a->id < id; });
    actors.insert(at, std::move(moved));
    return true;
}

CameraComponent* World::getActiveCamera() const {
    for (auto& a : actors)
        if (a->activeInHierarchy())
            if (auto* c = a->getComponent<CameraComponent>())
                if (c->isEnabled() && c->primary) return c;
    return nullptr;
}

std::vector<StaticMeshComponent*> World::getVisibleMeshes() const {
    std::vector<StaticMeshComponent*> result;
    for (auto& a : actors)
        if (a->activeInHierarchy())
            if (auto* m = a->getComponent<StaticMeshComponent>())
                // 머티리얼은 거르지 않습니다. 없으면 렌더러가 오류 머티리얼로 그립니다(DEF-28).
                if (m->isEnabled() && m->visible && m->mesh)
                    result.push_back(m);
    return result;
}

std::vector<LightComponent*> World::getLights() const {
    std::vector<LightComponent*> result;
    for (auto& a : actors)
        if (a->activeInHierarchy())
            if (auto* l = a->getComponent<LightComponent>())
                if (l->isEnabled())
                    result.push_back(l);
    return result;
}
