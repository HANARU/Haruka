#pragma once
#include <vector>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include "Actor.hpp"
#include "../core/HDelegate.hpp"

class World {
public:
    Actor* spawnActor(const std::string& name = "Actor");

    // Destroys immediately if called outside tick, deferred if called during tick.
    void   destroyActor(Actor* actor);

    void   tick(float dt);

    // Look up any actor by its unique ID (returns nullptr if not found / already destroyed).
    Actor* findById(uint64_t id) const;

    // 파일에 남는 고유 ID 로 찾습니다(참조 체계 R-2). 없거나 지워졌으면 nullptr.
    Actor* findByGuid(HGuid guid) const;
    // 액터의 GUID 를 바꿉니다(씬 · 프리팹을 읽을 때 파일 값을 넣음). 0 이거나 다른 액터가 쓰고 있으면 false 이고 바꾸지 않습니다.
    bool   assignGuid(Actor* actor, HGuid guid);

    // 지웠던 액터를 되살릴 때 원래 ID 를 돌려줍니다(에디터 생성 · 삭제 Undo). 새 ID 가 0 이거나 다른
    // 액터가 쓰고 있으면 false 입니다. actors 는 ID 오름차순(= 스폰 순서)을 유지하므로, 바꾼 액터를 ID 자리로
    // 옮깁니다. 그래야 되살린 뒤 저장해도 파일 안의 순서와 아웃라이너 자리가 지우기 전과 같습니다.
    // 틱 중에는 부르지 마십시오(actors 를 순회하는 중일 수 있음).
    bool   reassignId(Actor* actor, uint64_t newId);

    // Fired after an actor is added / before it is removed
    HEvent<Actor*> onActorSpawned;
    HEvent<Actor*> onActorDestroyed;

    // 화면 카메라 · 보이는 메시 · 라이트 찾기는 컴포넌트 쪽에 있습니다(2026-11-18, 엔진 구조 안정화 S3): CameraComponent::activeIn ·
    // StaticMeshComponent::visibleIn · LightComponent::enabledIn. 월드(scene)는 컴포넌트 형을 모릅니다. 에디터 카메라와 Play
    // 여부도 월드가 들지 않고, 에디터가 월드 단위 서비스(ViewCameraOverride)로 넣습니다.

    // ── 월드 단위 서비스 (2026-11-18 S3) ─────────────────────────────────────────────────────────────
    // 이 월드 하나에 붙은 시스템 · 정책입니다(UE UWorldSubsystem 과 같은 자리). 컴포넌트는 owner->world()->service<T>() 로 찾고,
    // 월드는 형을 모른 채 포인터만 듭니다. 지금 넣는 것: PhysicsSystem(startup ~ shutdown 사이, 예전 정적 포인터
    // PhysicsSystem::current 자리), ViewCameraOverride(에디터). 소유는 넣은 쪽이며, 없어지기 전에 nullptr 로 지웁니다.
    // 엔진 단위 서비스(창 · 렌더러 · 에셋 · 모듈)는 EngineContext 로 받습니다(runtime/IGameModule.hpp).
    template<class T>
    void setService(T* s) {
        if (s) services_[std::type_index(typeid(T))] = s;
        else   services_.erase(std::type_index(typeid(T)));
    }
    template<class T>
    T* service() const {
        auto it = services_.find(std::type_index(typeid(T)));
        return it != services_.end() ? static_cast<T*>(it->second) : nullptr;
    }

    const std::vector<std::unique_ptr<Actor>>& getActors()        const { return actors; }

    // 정확한 형 T 의 컴포넌트를 액터 순서대로 모은 목록입니다(액터마다 getComponent<T> 가 돌려주는 첫 것, 2026-11-18 성능
    // 안정화 1-5). 렌더러 · 물리 · 소리가 프레임마다 액터 전체를 훑던 자리에 씁니다. 구조 버전(hcore::gStructureVersion)이
    // 바뀔 때만 다시 모읍니다. 활성 · 켜짐은 거르지 않습니다(부르는 쪽이 지금처럼 거름).
    // 목록을 도는 동안 액터 · 컴포넌트를 만들거나 지우지 마십시오(다시 모으면 도는 목록이 바뀌고, 지운 컴포넌트는 매달린
    // 포인터가 됩니다). 그런 일을 하는 루프는 getActors() 를 쓰십시오.
    template<class T>
    const std::vector<T*>& componentsOf() const {
        std::unique_ptr<TypeListBase>& slot = typeLists_[std::type_index(typeid(T))];
        if (!slot) slot = std::make_unique<TypeList<T>>();
        auto* list = static_cast<TypeList<T>*>(slot.get());
        if (list->version != hcore::gStructureVersion) {
            list->items.clear();
            for (const auto& a : actors)
                if (T* c = a->getComponent<T>()) list->items.push_back(c);
            list->version = hcore::gStructureVersion;
        }
        return list->items;
    }

private:
    // 액터보다 먼저 선언합니다(나중에 파괴). 액터를 지우는 중에 컴포넌트가 서비스를 찾을 수 있습니다(리그의 캐릭터 해제 등).
    std::unordered_map<std::type_index, void*> services_;

    struct TypeListBase {
        virtual ~TypeListBase() = default;
        uint64_t version = 0;   // 0 = 아직 모으지 않음(구조 버전은 1 부터)
    };
    template<class T>
    struct TypeList : TypeListBase { std::vector<T*> items; };
    mutable std::unordered_map<std::type_index, std::unique_ptr<TypeListBase>> typeLists_;

    std::vector<std::unique_ptr<Actor>> actors;
    std::unordered_map<uint64_t, Actor*> idMap_;
    std::unordered_map<HGuid, Actor*>    guidMap_;
    std::vector<Actor*>                  pendingDestroy_;
    uint64_t                             nextId_ = 1;
    bool                                 ticking_ = false;

    void destroyImmediate(Actor* actor);
    void flushDestroyQueue();
};
