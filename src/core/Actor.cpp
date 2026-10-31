#include "Actor.hpp"
#include "SceneLog.hpp"
#include <algorithm>

// ── 월드 트랜스폼 — 온디맨드 유도 ─────────────────────────────────────────────
//
// 예전에는 cachedWorld_ / worldDirty_ 로 캐싱했는데, 에디터 편집이 원본 transform 을
// 직접 쓰면서 더티 플래그를 세우지 않아 렌더러가 낡은 캐시를 읽었습니다(DEF-49).
// 즉 편집이 화면에 반영되지 않았습니다.
//
// 캐시를 없애 매 호출 즉시 계산합니다. 무효화할 상태가 없으므로 stale 이 원리적으로
// 불가능합니다. 현재 씬은 액터 수십 개, 계층 깊이 1~2 라 부모 체인 순회 비용이
// 사실상 0 입니다. 계층이 수천 개로 커져 프로파일러가 이 순회를 지목하면, 그때
// 세대 스탬프 방식으로 올립니다(Docs/Transform_Design.md 5-1절 안 나). worldMatrix()
// 시그니처가 그대로라 소급이 쉽습니다.

glm::mat4 Actor::worldMatrix() const {
    return parent_ ? parent_->worldMatrix() * transform_.matrix()
                   : transform_.matrix();
}

glm::vec3 Actor::worldPosition() const {
    return glm::vec3(worldMatrix()[3]);
}

glm::quat Actor::worldRotation() const {
    // 부모 월드 회전 * 로컬 회전. 쿼터니언 합성이라 정확합니다.
    return parent_ ? parent_->worldRotation() * transform_.rotation()
                   : transform_.rotation();
}

glm::vec3 Actor::worldScale() const {
    // 성분별 누적. 부모가 비균등 스케일 + 자식 회전이면 참 월드 스케일은 순수
    // 스케일로 분해되지 않습니다(shear). 이 값은 그 경우 근사이며, 소비 측이
    // (예: 물리) 필요하면 최대 성분으로 보수 근사합니다.
    return parent_ ? parent_->worldScale() * transform_.scale()
                   : transform_.scale();
}

void Actor::setWorldPosition(const glm::vec3& p) {
    if (!parent_) { transform_.setPosition(p); return; }
    transform_.setPosition(glm::vec3(glm::inverse(parent_->worldMatrix()) * glm::vec4(p, 1.f)));
}

void Actor::setWorldRotation(const glm::quat& q) {
    if (!parent_) { transform_.setRotation(q); return; }
    transform_.setRotation(glm::normalize(glm::inverse(parent_->worldRotation()) * q));
}

void Actor::setActive(bool v) {
    if (activeSelf_ == v) return;
    activeSelf_ = v;
    // activeSelf_ 를 먼저 바꾼 뒤 갱신합니다. 부모가 비활성이면 실효는 여전히 false 라
    // 각 syncLive 가 멱등하게 무시하므로, 별도의 실효 변화 검사 없이 항상 호출해도 됩니다.
    refreshSubtreeActive();
}

// 이 액터와 자식 서브트리의 컴포넌트 실효 활성을 현재 activeInHierarchy 기준으로 다시
// 맞춥니다. syncLive 가 엣지를 기억하므로 상태가 그대로면 발화하지 않습니다(멱등).
void Actor::refreshSubtreeActive() {
    for (auto& c : components) c->syncLive();
    for (Actor* child : children_) child->refreshSubtreeActive();
}

void Actor::attachTo(Actor* parent, int index) {
    if (parent && parent_ == parent) {
        // 같은 부모 안에서 자리만 옮깁니다(실효 활성은 그대로).
        auto& c = parent_->children_;
        c.erase(std::remove(c.begin(), c.end(), this), c.end());
        const size_t at = (index < 0 || (size_t)index > c.size()) ? c.size() : (size_t)index;
        c.insert(c.begin() + (std::ptrdiff_t)at, this);
        return;
    }
    if (!parent) { detach(); return; }
    attachTo(parent);
    if (parent_ != parent) return;   // 사이클로 거부됨
    auto& c = parent_->children_;
    if (index < 0 || (size_t)index >= c.size()) return;   // 이미 끝에 있음
    c.erase(std::remove(c.begin(), c.end(), this), c.end());
    c.insert(c.begin() + index, this);
}

int Actor::siblingIndex() const {
    if (!parent_) return -1;
    const auto& c = parent_->children_;
    const auto it = std::find(c.begin(), c.end(), this);
    return it == c.end() ? -1 : (int)(it - c.begin());
}

bool Actor::removeComponent(Component* comp) {
    auto it = std::find_if(components.begin(), components.end(),
                           [comp](const auto& c) { return c.get() == comp; });
    if (it == components.end()) return false;
    // 캐시에서 이 포인터를 빼고, 같은 타입의 다른 컴포넌트가 있으면 그것(먼저 붙은 것)을 다시 넣습니다.
    const std::type_index type(typeid(*comp));
    if (auto ci = componentCache_.find(type); ci != componentCache_.end() && ci->second == comp)
        componentCache_.erase(ci);
    if (comp->isEnabled()) comp->setEnabled(false);
    comp->onDestroy();
    components.erase(it);
    if (componentCache_.find(type) == componentCache_.end())
        for (auto& c : components)
            if (std::type_index(typeid(*c)) == type) { componentCache_.emplace(type, c.get()); break; }
    return true;
}

int Actor::componentIndex(const Component* comp) const {
    for (size_t i = 0; i < components.size(); ++i)
        if (components[i].get() == comp) return (int)i;
    return -1;
}

void Actor::moveComponent(Component* comp, size_t index) {
    const int from = componentIndex(comp);
    if (from < 0) return;
    std::unique_ptr<Component> owned = std::move(components[(size_t)from]);
    components.erase(components.begin() + from);
    const size_t at = std::min(index, components.size());
    components.insert(components.begin() + (std::ptrdiff_t)at, std::move(owned));
    // 같은 타입이 여럿이면 캐시는 "먼저 붙은 것"이라 순서가 바뀌면 다시 고릅니다.
    const std::type_index type(typeid(*comp));
    componentCache_.erase(type);
    for (auto& c : components)
        if (std::type_index(typeid(*c)) == type) { componentCache_.emplace(type, c.get()); break; }
}

void Actor::attachTo(Actor* parent) {
    if (parent_ == parent) return;
    // 사이클 가드: parent 가 자신이거나 자신의 자손이면 거부합니다. 그러지 않으면
    // parent_ 체인이 순환해 activeInHierarchy()/worldMatrix()/refreshSubtreeActive()
    // 가 무한 재귀로 스택 오버플로를 일으킵니다. parent 의 조상 체인에 this 가 있으면
    // parent 는 this 의 자손입니다. attachTo 가 parent_ 의 유일한 설정 경로이므로,
    // 이 한 곳을 막으면 사이클이 구조적으로 생길 수 없습니다.
    for (Actor* a = parent; a; a = a->parent_) {
        if (a == this) {
            HLOG(LogScene, Warning) << "attachTo rejected: '" << name
                                    << "' cannot be attached to itself or its descendant (cycle)";
            return;
        }
    }
    unlinkFromParent();
    parent_ = parent;
    if (parent_) parent_->children_.push_back(this);
    refreshSubtreeActive();   // 조상 체인이 바뀌어 실효가 뒤집힐 수 있음
}

void Actor::detach() {
    if (!parent_) return;
    unlinkFromParent();
    refreshSubtreeActive();
}

void Actor::unlinkFromParent() {
    if (!parent_) return;
    auto& c = parent_->children_;
    c.erase(std::remove(c.begin(), c.end(), this), c.end());
    parent_ = nullptr;
}

void Actor::notifyDestroy() {
    for (auto& c : components) {
        if (c->isEnabled()) c->setEnabled(false);
        c->onDestroy();
    }
}

void Actor::notifyTransformCommitted() {
    for (auto& c : components) c->onTransformCommitted();
    for (Actor* child : children_) child->notifyTransformCommitted();
}
