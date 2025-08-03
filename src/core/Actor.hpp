#pragma once
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <typeindex>
#include <unordered_map>
#include "Transform.hpp"
#include "Component.hpp"

class Actor {
public:
    uint64_t    id     = 0;   // assigned by World::spawnActor
    std::string name;

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

    // ── 로컬 트랜스폼 ────────────────────────────────────────────────────────
    // 읽기는 transform(), 쓰기는 아래 세터로만 합니다. transform_ 은 private 라
    // 외부에서 필드를 직접 쓸 수 없습니다. 나중에 파생값 캐시를 되살릴 때, 이 세터
    // 들이 무효화의 단일 통로가 됩니다 (Spatial_Representation_Design.md 5절).
    const Transform& transform() const { return transform_; }

    void setLocalPosition(const glm::vec3& p) { transform_.setPosition(p); }
    void setLocalRotation(const glm::quat& r) { transform_.setRotation(r); }
    void setLocalScale   (const glm::vec3& s) { transform_.setScale(s);    }
    void translateLocal  (const glm::vec3& d) { transform_.translate(d);   }
    void setLocalTransform(const Transform& t){ transform_ = t;            }   // undo/redo·직렬화

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
        return ptr;
    }

    template<typename T>
    T* getComponent() const {
        auto it = componentCache_.find(std::type_index(typeid(T)));
        return it != componentCache_.end() ? static_cast<T*>(it->second) : nullptr;
    }

    const std::vector<std::unique_ptr<Component>>& getComponents() const { return components; }

    template<typename T>
    bool removeComponent() {
        componentCache_.erase(std::type_index(typeid(T)));
        auto it = std::find_if(components.begin(), components.end(),
            [](const auto& c){ return dynamic_cast<T*>(c.get()) != nullptr; });
        if (it == components.end()) return false;
        if ((*it)->isEnabled()) (*it)->setEnabled(false);
        (*it)->onDestroy();
        components.erase(it);
        return true;
    }

    void tick(float dt) {
        for (auto& c : components) c->tickIfReady(dt);
    }

    // ── World-space transform (온디맨드 유도, 캐시 없음) ──────────────────────
    glm::mat4 worldMatrix()   const;
    glm::vec3 worldPosition() const;
    glm::quat worldRotation() const;
    glm::vec3 worldScale()    const;

    // ── Scene hierarchy ───────────────────────────────────────────────────────
    // attachTo/detach 는 조상 체인을 바꾸므로, 재부착 뒤 이 서브트리의 컴포넌트
    // 실효 활성(onEnable/onDisable)을 다시 맞춥니다.
    void attachTo(Actor* parent);
    void detach();

    Actor*                     getParent()   const { return parent_;   }
    const std::vector<Actor*>& getChildren() const { return children_; }

    // Called by World::destroyActor — fires onDestroy on every component.
    void notifyDestroy();

private:
    // 이 액터와 자식 서브트리의 모든 컴포넌트에 대해 실효 활성을 다시 계산하고,
    // 전이된 것만 onEnable/onDisable 을 발화시킵니다. setActive·attachTo·detach 가 호출합니다.
    void refreshSubtreeActive();
    // 부모의 children_ 에서 자신을 떼고 parent_ 를 비웁니다(실효 갱신은 하지 않음).
    // attachTo/detach 가 중간 상태에서 불필요한 onEnable/onDisable 이 발화되지 않도록,
    // 리스트 조작과 실효 갱신을 분리하는 내부 헬퍼입니다.
    void unlinkFromParent();

    std::vector<std::unique_ptr<Component>>      components;
    std::unordered_map<std::type_index, Component*> componentCache_;

    Transform           transform_;
    Actor*              parent_   = nullptr;
    std::vector<Actor*> children_;
    bool                activeSelf_ = true;
};
