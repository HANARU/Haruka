#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <algorithm>
#include <typeindex>
#include <unordered_map>
#include "../core/Transform.hpp"
#include "../core/Guid.hpp"
#include "Component.hpp"

// 구조 버전(2026-11-18, 성능 안정화 1-5). 액터 생성 · 삭제 · ID 재배치와 컴포넌트 추가 · 제거 · 순서 변경 때 오릅니다.
// World::componentsOf<T> 가 이 값이 바뀔 때만 목록을 다시 모읍니다. 모든 World 가 함께 쓰는 값이라 한 월드의 변경이 다른
// 월드의 목록도 다시 모으게 하지만, 틀리지는 않습니다. 메인 스레드에서만 바꿉니다.
class World;

namespace hcore {
inline uint64_t gStructureVersion = 1;
inline void     bumpStructure() { ++gStructureVersion; }
} // namespace hcore

class Actor {
public:
    uint64_t    id     = 0;   // assigned by World::spawnActor
    std::string name;

    // 파일에 남는 고유 ID(참조 체계 D1, Docs/Object_References.md). World::spawnActor 가 새 값을 주고, 씬 · 프리팹을 읽을 때
    // World::assignGuid 로 파일 값을 넣습니다. id 는 실행 중 번호(Undo 커맨드가 씀)라 열 때마다 바뀌지만 guid 는 그대로입니다.
    HGuid guid() const { return guid_; }

    // ── 프리팹 링크 (2026-11-21, Docs/Linked_Prefabs.md) ───────────────────────────
    // 씬 · 프리팹 파일의 인스턴스 블록을 펼친 액터에 붙습니다. 저장할 때 원본과 비교해 다른 값만 덮어쓰기로 씁니다(D1).
    //   source = 인스턴스 루트만: 원본 프리팹 경로. 비어 있으면 루트가 아님.
    //   pair   = 인스턴스 소속 액터(루트 포함): 펼친 원본 안의 GUID(원본 액터와의 짝, D3).
    //   raw    = 루트만: 펼치지 못한 인스턴스 블록(원본 없음 · 순환)의 줄. 다시 저장할 때 그대로 씁니다(D8).
    struct PrefabLink {
        std::string              source;
        HGuid                    pair;
        std::vector<std::string> raw;
        bool empty() const { return source.empty() && !pair.valid() && raw.empty(); }
    };
    PrefabLink prefabLink;

    // 이 액터를 만든 월드(World::spawnActor 가 정함, 2026-11-18 S3). 컴포넌트가 월드 단위 서비스를 찾을 때 씁니다
    // (owner->world()->service<PhysicsSystem>() 등, World.hpp).
    World* world() const { return world_; }

    // ── 활성 상태 (유니티 activeSelf / activeInHierarchy) ─────────────────────
    // activeSelf_ 는 이 액터 자신의 플래그입니다. activeInHierarchy() 는 부모 체인을
    // 따라 계산한 실효 상태로, 자신과 모든 조상이 활성일 때만 참입니다. 렌더·틱·조회
    // 게이트는 모두 실효 상태를 봅니다. 부모가 없으면 실효 == self 라, 평면 계층인
    // 현재 씬에서는 기존 동작이 그대로 유지됩니다. (A-2, Docs/Roadmap.md)
    bool activeSelf() const { return activeSelf_; }
    bool activeInHierarchy() const {
        return activeSelf_ && (parent_ ? parent_->activeInHierarchy() : true);
    }
    // 자신의 활성 플래그를 바꾸고, 실효가 뒤집히면 이 액터와 자식 서브트리의 컴포넌트
    // onEnable/onDisable 을 발화시킵니다. 정의는 Actor.cpp 에 있습니다.
    void setActive(bool v);

    // ── 태그 (2026-11-20, Docs/Gameplay_Tools.md D3) ─────────────────────────────
    // 종류로 찾을 때 씁니다(UE Actor Tags · Unity tag · Godot 그룹). 이름은 [A-Za-z0-9_.-], 대소문자를 가립니다. 찾기는
    // World::actorsWithTag(색인). 씬 파일에는 태그가 있을 때만 TAGS: 줄을 씁니다.
    const std::vector<std::string>& tags() const { return tags_; }
    bool hasTag(std::string_view tag) const;
    bool addTag(std::string_view tag);      // 틀린 이름 · 이미 있음이면 false
    bool removeTag(std::string_view tag);   // 없었으면 false
    void clearTags();
    static bool validTag(std::string_view tag);

    // ── 로컬 트랜스폼 ────────────────────────────────────────────────────────
    // 읽기는 transform(), 쓰기는 아래 세터로만 합니다. transform_ 은 private 라
    // 외부에서 필드를 직접 쓸 수 없습니다. 이 세터들이 월드 행렬 캐시(O2)를 무효화하는 단일 통로입니다
    // (localVersion_ 을 올림, Spatial_Representation_Design.md 5절).
    const Transform& transform() const { return transform_; }

    void setLocalPosition(const glm::vec3& p) { transform_.setPosition(p); ++localVersion_; }
    void setLocalRotation(const glm::quat& r) { transform_.setRotation(r); ++localVersion_; }
    void setLocalScale   (const glm::vec3& s) { transform_.setScale(s);    ++localVersion_; }
    void translateLocal  (const glm::vec3& d) { transform_.translate(d);   ++localVersion_; }
    void setLocalTransform(const Transform& t){ transform_ = t;            ++localVersion_; }   // undo/redo·직렬화

    // 이름 호환용 별칭. DefaultGameModule/물리가 씁니다.
    void setPosition(const glm::vec3& p) { setLocalPosition(p); }
    void setRotation(const glm::quat& r) { setLocalRotation(r); }
    void setScale   (const glm::vec3& s) { setLocalScale(s);    }

    // ── Component management ──────────────────────────────────────────────────
    template<typename T, typename... Args>
    T* addComponent(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T*   ptr  = comp.get();
        comp->owner = this;
        comp->onAttach();
        comp->syncLive();   // 액터가 실효 활성이고 컴포넌트가 enabled 면 onEnable 발화
        // emplace preserves first-added component on duplicate type
        componentCache_.emplace(std::type_index(typeid(T)), ptr);
        components.push_back(std::move(comp));
        hcore::bumpStructure();
        return ptr;
    }

    template<typename T>
    T* getComponent() const {
        auto it = componentCache_.find(std::type_index(typeid(T)));
        return it != componentCache_.end() ? static_cast<T*>(it->second) : nullptr;
    }

    const std::vector<std::unique_ptr<Component>>& getComponents() const { return components; }

    // T(또는 T 를 상속한 형)인 컴포넌트를 붙은 순서대로 모두(2026-11-17, Roadmap A-3). getComponent<T> 는 정확한 형의 첫 것만 찾습니다.
    template<typename T>
    std::vector<T*> getComponents() const {
        std::vector<T*> out;
        for (const auto& c : components)
            if (auto* t = dynamic_cast<T*>(c.get())) out.push_back(t);
        return out;
    }

    // 자신부터 자손까지(너비 우선, 자식 순서대로) 처음 찾은 T(정확한 형). includeInactive 가 거짓이면 꺼진 액터(activeInHierarchy
    // 거짓)는 건너뜁니다(Unity GetComponentInChildren 과 같은 규칙). 손 액터가 자식 장갑의 손 자세를 찾을 때 씁니다.
    template<typename T>
    T* getComponentInChildren(bool includeInactive = false) const {
        std::vector<const Actor*> queue{ this };
        for (size_t i = 0; i < queue.size(); ++i) {
            const Actor* a = queue[i];
            if (!includeInactive && !a->activeInHierarchy()) continue;
            if (T* t = a->getComponent<T>()) return t;
            for (const Actor* c : a->children_) queue.push_back(c);
        }
        return nullptr;
    }

    // 이 컴포넌트를 지웁니다(템플릿판과 같은 순서: 끄기 → onDestroy → 해제). 에디터의 컴포넌트 제거 Undo 가
    // 타입이 아니라 그 컴포넌트 하나를 지울 때 씁니다(2026-10-15). 없으면 false.
    bool removeComponent(Component* c);
    // 붙은 순서 안의 자리(없으면 -1)와, 그 자리로 옮기기. Undo 로 되살린 컴포넌트를 원래 순서로 돌립니다.
    int  componentIndex(const Component* c) const;
    void moveComponent(Component* c, size_t index);

    // 첫 T 를 지웁니다. 캐시 갱신은 포인터판이 맡아, 같은 형이 하나 더 있으면 남은 첫 것이 getComponent<T> 의 답이 됩니다
    // (Roadmap A-3: 예전에는 캐시를 지우기만 해 남은 것을 찾지 못했음).
    template<typename T>
    bool removeComponent() {
        auto it = std::find_if(components.begin(), components.end(),
            [](const auto& c){ return dynamic_cast<T*>(c.get()) != nullptr; });
        if (it == components.end()) return false;
        return removeComponent(it->get());
    }

    // 그 틱 단계의 컴포넌트만 틱합니다(D5). 기본은 PrePhysics 입니다.
    void tick(float dt, ETickGroup group = ETickGroup::PrePhysics) {
        for (auto& c : components)
            if (c->tickGroup == group) c->tickIfReady(dt);
    }

    // ── World-space transform (세대 스탬프 캐시, O2 2026-11-17) ─────────────────
    // worldMatrix 는 부모 사슬의 캐시를 확인하며 바뀐 곳만 다시 곱합니다(Actor.cpp 주석). 값은 캐시 없는 계산과 같습니다.
    glm::mat4 worldMatrix()   const;
    // 캐시 없이 다시 계산(시험 · 검증용).
    glm::mat4 worldMatrixUncached() const;
    glm::vec3 worldPosition() const;
    glm::quat worldRotation() const;
    glm::vec3 worldScale()    const;

    // 월드 기준으로 위치 · 회전을 정합니다. 부모가 있으면 부모의 지금 월드 트랜스폼으로 로컬을 구해
    // 씁니다(부모가 없으면 로컬 세터와 같음). 물리가 시뮬레이션한 월드 자세를 되쓸 때 씁니다(DEF-53).
    // 부모가 비균등 크기 + 회전이면 월드 회전이 순수 회전으로 나뉘지 않아 근사입니다(worldScale 과 같음).
    void setWorldPosition(const glm::vec3& p);
    void setWorldRotation(const glm::quat& q);

    // ── 표시 보간 · 명시적 순간 이동 (D2b) ────────────────────────────────────
    // 순간 이동은 이전 틱 자세와의 보간을 건너뛰고 즉시 새 자리에서 그립니다.
    // 텔레포트, 스냅 회전, 씬 로드/스폰에서 호출합니다.
    void resetInterpolation() {
        prevTickTransform_ = transform_;
        prevTickLocalVer_  = localVersion_;
        interpValid_       = false;
    }
    void teleportLocal   (const glm::vec3& p, const glm::quat& r) {
        transform_.setPosition(p);
        transform_.setRotation(r);
        ++localVersion_;
        resetInterpolation();
    }
    void teleportTo      (const glm::vec3& worldPos, const glm::quat& worldRot);
    void teleportPosition(const glm::vec3& worldPos);
    void teleportRotation(const glm::quat& worldRot);

    // 시뮬레이션 틱 경계에서 이전 틱 자세와의 보간 비율 alpha [0, 1] 로 섞은 표시용 월드 자세입니다.
    // Transform(시뮬레이션 상태)은 건드리지 않으며, 정적 액터는 캐시된 worldMatrix() 를 그대로 씁니다.
    glm::mat4 displayWorldMatrix  (float alpha) const;
    glm::vec3 displayWorldPosition(float alpha) const;
    glm::quat displayWorldRotation(float alpha) const;

    // ── Scene hierarchy ───────────────────────────────────────────────────────
    // attachTo/detach 는 조상 체인을 바꾸므로, 재부착 뒤 이 서브트리의 컴포넌트
    // 실효 활성(onEnable/onDisable)을 다시 맞춥니다.
    void attachTo(Actor* parent);
    // index 는 부모의 자식 목록 안 자리입니다(-1 또는 범위 밖 = 끝). 같은 부모면 자리만 옮깁니다.
    // 부모 변경 Undo 가 원래 형제 순서로 되돌릴 때 씁니다(2026-10-15).
    void attachTo(Actor* parent, int index);
    void detach();
    // 부모의 자식 목록 안 자리. 부모가 없으면 -1.
    int  siblingIndex() const;

    Actor*                     getParent()   const { return parent_;   }
    const std::vector<Actor*>& getChildren() const { return children_; }

    // Called by World::destroyActor — fires onDestroy on every component.
    void notifyDestroy();

    // 에디터가 이 액터의 트랜스폼 편집을 확정했음을 알립니다. 자식의 월드 트랜스폼도 함께
    // 바뀌므로 서브트리 전체 컴포넌트의 onTransformCommitted 를 부릅니다.
    void notifyTransformCommitted();

private:
    friend class World;   // world_ 를 정함

    // 이 액터와 자식 서브트리의 모든 컴포넌트에 대해 실효 활성을 다시 계산하고,
    // 전이된 것만 onEnable/onDisable 을 발화시킵니다. setActive·attachTo·detach 가 호출합니다.
    void refreshSubtreeActive();
    // 부모의 children_ 에서 자신을 떼고 parent_ 를 비웁니다(실효 갱신은 하지 않음).
    // attachTo/detach 가 중간 상태에서 불필요한 onEnable/onDisable 이 발화되지 않도록,
    // 리스트 조작과 실효 갱신을 분리하는 내부 헬퍼입니다.
    void unlinkFromParent();

    World*              world_ = nullptr;   // 컴포넌트보다 먼저 선언(컴포넌트를 지우는 중에도 읽힘)
    HGuid               guid_;              // World 만 바꿈(색인과 함께)
    std::vector<std::unique_ptr<Component>>      components;
    std::unordered_map<std::type_index, Component*> componentCache_;

    Transform           transform_;
    // 월드 행렬 캐시(O2). 로컬 세터가 localVersion_ 을 올리고, 캐시는 (로컬 버전, 부모, 부모 캐시 스탬프)가 모두 같을 때만
    // 씁니다. 스탬프는 다시 계산할 때마다 전역에서 새 값을 받으므로, 조상 어디가 바뀌어도 자손의 비교가 어긋나 다시 계산됩니다.
    uint32_t            localVersion_ = 0;
    mutable glm::mat4   worldCache_ { 1.f };
    mutable uint64_t    worldStamp_       = 0;   // 0 = 캐시 없음
    mutable uint32_t    cacheLocalVer_    = 0;
    mutable uint64_t    cacheParentStamp_ = 0;
    mutable const Actor* cacheParent_     = nullptr;
    // 표시 보간용 직전 시뮬레이션 틱 스냅샷(D2b)
    Transform           prevTickTransform_;
    uint32_t            prevTickLocalVer_ = 0;
    bool                interpValid_      = false;
    bool                anyInterp() const {
        return (interpValid_ && localVersion_ != prevTickLocalVer_) || (parent_ && parent_->anyInterp());
    }
    void                snapshotTickPose() {
        if (localVersion_ != prevTickLocalVer_) {
            prevTickTransform_ = transform_;
            prevTickLocalVer_  = localVersion_;
        }
        interpValid_ = true;
    }
    Actor*              parent_   = nullptr;
    std::vector<Actor*> children_;
    bool                activeSelf_ = true;
    std::vector<std::string> tags_;
};
