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
