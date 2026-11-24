#pragma once
#include <vector>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include "Actor.hpp"
#include "TweenSystem.hpp"
#include "TimerManager.hpp"
#include "EventBus.hpp"
#include <string_view>
#include "../core/HDelegate.hpp"

class World {
public:
    Actor* spawnActor(const std::string& name = "Actor");

    // Destroys immediately if called outside tick, deferred if called during tick.
    void   destroyActor(Actor* actor);

    // 받은 dt(실제 경과)에 시간 배율을 곱해 PrePhysics 컴포넌트 · 타이머 · 트윈을 진행합니다(D1). 일시정지는 부르지 않는 것입니다.
    void   tick(float dt);
    // 모듈(물리 · 소리) 틱 뒤에 PostPhysics 컴포넌트를 tick 과 같은 dt 로 틱합니다(D5). 그런 컴포넌트가 없으면 훑지 않습니다.
    void   tickPostPhysics();
    // 표시 단계(D2a): 프레임마다 1회, 프레임 dt(시간 배율 적용)로 Display 컴포넌트를 틱합니다. 없으면 훑지 않습니다.
    void   tickDisplay(float dt);
    // 표시 보간(D2b): 시뮬레이션 틱 시작 전 이전 틱 자세를 기록 / 씬 로드·Play 시작 시 보간 상태 초기화
    void   snapshotTickPoses();
    void   resetAllInterpolation();

    // ── 게임 시간 (2026-11-20, Docs/Gameplay_Tools.md D1) ─────────────────────────────────────────────
    // 시간 배율(0 ~ 100, 기본 1, UE TimeDilation · Unity timeScale). 0 이면 컴포넌트는 dt 0 으로 틱합니다(일시정지와 달리 틱은 돎).
    float  timeScale() const { return timeScale_; }
    void   setTimeScale(float s);
    // 배율을 적용한 누적 게임 시간(초)과 마지막 틱의 dt. 엔진은 lastDt 를 게임 onTick 과 모듈에 줍니다.
    double gameTime() const { return gameTime_; }
    float  lastDt() const   { return lastDt_; }

    // 타이머(D2): 게임 시간으로 세고, 주인 액터를 지우면 함께 취소합니다.
    TimerManager&       timers()       { return timers_; }
    const TimerManager& timers() const { return timers_; }
    // 이벤트 버스(D4): 형으로 구독 · 발행합니다.
    EventBus&           events()       { return events_; }

    // 그 태그를 가진 액터(액터 순서, D3). 구조 버전 · 태그 버전이 같으면 지난 결과를 돌려줍니다(프레임마다 훑지 않음). 활성은 거르지
    // 않습니다. 돌려준 목록을 도는 동안 액터를 만들거나 지우거나 태그를 바꾸지 마십시오.
    const std::vector<Actor*>& actorsWithTag(std::string_view tag) const;
    Actor* firstWithTag(std::string_view tag) const;
    void   noteTagsChanged() { ++tagVersion_; }

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

    // 트윈(애니메이션 A7): 월드 틱 끝에 진행하고, 액터를 지우면 그 액터의 트윈도 지웁니다.
    TweenSystem&       tweens()       { return tweens_; }
    const TweenSystem& tweens() const { return tweens_; }

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
    TweenSystem  tweens_;
    TimerManager timers_;
    EventBus     events_;
    float        timeScale_ = 1.f;
    double       gameTime_  = 0.0;
    float        lastDt_    = 0.f;
    uint64_t     tagVersion_ = 1;
    mutable uint64_t tagCacheStructure_ = 0, tagCacheTags_ = 0;
    mutable std::unordered_map<std::string, std::vector<Actor*>> tagCache_;
    mutable uint64_t postPhysicsVersion_ = 0;   // 물리 뒤 컴포넌트가 있는지 본 구조 버전
    mutable bool     anyPostPhysics_     = false;
    mutable uint64_t displayVersion_     = 0;   // 표시 단계 컴포넌트가 있는지 본 구조 버전
    mutable bool     anyDisplay_         = false;

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
