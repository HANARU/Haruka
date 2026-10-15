#pragma once
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include "Actor.hpp"
#include "HDelegate.hpp"

class StaticMeshComponent;
class CameraComponent;
class LightComponent;

class World {
public:
    Actor* spawnActor(const std::string& name = "Actor");

    // Destroys immediately if called outside tick, deferred if called during tick.
    void   destroyActor(Actor* actor);

    void   tick(float dt);

    // Look up any actor by its unique ID (returns nullptr if not found / already destroyed).
    Actor* findById(uint64_t id) const;

    // 지웠던 액터를 되살릴 때 원래 ID 를 돌려줍니다(에디터 생성 · 삭제 Undo). 새 ID 가 0 이거나 다른
    // 액터가 쓰고 있으면 false 입니다. actors 는 ID 오름차순(= 스폰 순서)을 유지하므로, 바꾼 액터를 ID 자리로
    // 옮깁니다. 그래야 되살린 뒤 저장해도 파일 안의 순서와 아웃라이너 자리가 지우기 전과 같습니다.
    // 틱 중에는 부르지 마십시오(actors 를 순회하는 중일 수 있음).
    bool   reassignId(Actor* actor, uint64_t newId);

    // Fired after an actor is added / before it is removed
    HEvent<Actor*> onActorSpawned;
    HEvent<Actor*> onActorDestroyed;

    CameraComponent*                           getActiveCamera()  const;
    std::vector<StaticMeshComponent*>          getVisibleMeshes() const;
    std::vector<LightComponent*>               getLights()        const;
    const std::vector<std::unique_ptr<Actor>>& getActors()        const { return actors; }

private:
    std::vector<std::unique_ptr<Actor>> actors;
    std::unordered_map<uint64_t, Actor*> idMap_;
    std::vector<Actor*>                  pendingDestroy_;
    uint64_t                             nextId_ = 1;
    bool                                 ticking_ = false;

    void destroyImmediate(Actor* actor);
    void flushDestroyQueue();
};
