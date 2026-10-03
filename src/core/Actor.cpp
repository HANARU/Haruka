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
