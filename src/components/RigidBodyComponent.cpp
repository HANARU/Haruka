#include "RigidBodyComponent.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../core/Actor.hpp"
#include "../core/Reflect.hpp"
#include <algorithm>
#include <cstdlib>

void RigidBodyComponent::releaseBody() {
    if (bodyId_ != 0xFFFFFFFFu && physSys_) physSys_->removeBody(bodyId_);
    bodyId_  = 0xFFFFFFFFu;
    physSys_ = nullptr;
}

void RigidBodyComponent::onDestroy() {
    releaseBody();
}

// 액터를 끄거나(계층 포함) 컴포넌트를 끄면 바디도 사라져야 합니다. 예전에는 남은 바디가
// 계속 시뮬레이션·충돌하고 위치를 되썼습니다(DEF-80). 다시 켜지면 물리 틱이 지금
// 트랜스폼으로 새로 만듭니다.
void RigidBodyComponent::onDisable() {
    releaseBody();
}

void RigidBodyComponent::rebuildBody() {
    releaseBody();
    createFailed_ = false;
}

bool RigidBodyComponent::bodyParamsChanged() const {
    return shape != createdShape_ || motionType != createdMotion_ || mass != createdMass_ ||
           halfExtent.x != createdHalf_.x || halfExtent.y != createdHalf_.y ||
           halfExtent.z != createdHalf_.z;
}

// 편집을 확정할 때 바디를 다시 만듭니다. 기준은 "세터가 불렸는가" 가 아니라 "만들 때 쓴 값과
// 지금 값이 다른가" 입니다. 세터 기준이면, 마찰만 바꾼 편집의 undo(모든 실수 세터를 다시 부름)나
// 질량을 끌었다가 제자리로 돌린 드래그에도 바디를 다시 만들어 속도를 잃습니다.
void RigidBodyComponent::onPropsCommitted() {
    createFailed_ = false;   // 만들기에 실패했던 바디는 값이 바뀌었을 수 있으니 다시 시도합니다
    if (bodyId_ != 0xFFFFFFFFu && bodyParamsChanged()) rebuildBody();
}

void RigidBodyComponent::onTransformCommitted() {
    if (bodyId_ == 0xFFFFFFFFu || !physSys_ || !owner) return;
    // 콜라이더 치수는 스케일로 정해지므로(DEF-41) 스케일이 바뀌면 다시 만듭니다. 위치·회전만
    // 바뀌었으면 바디를 그 자리로 옮깁니다. 예전에는 플레이 중 기즈모로 옮겨도 바디가 남았습니다.
    const glm::vec3 sc = glm::abs(owner->worldScale());
    const glm::vec3 d  = glm::abs(sc - createdScale_);
    if (std::max({ d.x, d.y, d.z }) > 1e-5f) { rebuildBody(); return; }
    physSys_->teleport(bodyId_, *owner);
}

std::vector<PropDesc> RigidBodyComponent::buildProps() {
    auto* p = this;

    // 바디를 다시 만들어야 하는 값(셰이프·모션·크기·질량)은 세터에서 값만 바꾸고, 편집이
    // 확정될 때(onPropsCommitted) 만들 때 쓴 값과 달라졌으면 한 번 다시 만듭니다. 숫자 칸
    // 드래그는 마우스가 움직일 때마다 세터를 불러, 예전에는 플레이 중 프레임마다 바디를 지우고
    // 만들었습니다(DEF-43). 질량도 여기에 듭니다. 예전에는 이미 만든 바디에 반영되지 않았습니다(DEF-81).
    // 마찰·반발은 재생성 없이 살아 있는 바디에 바로 씁니다(DEF-81).
    auto applyMaterial = [p] {
        if (p->bodyId_ != 0xFFFFFFFFu && p->physSys_)
            p->physSys_->setMaterial(p->bodyId_, p->friction, p->restitution);
    };

    return {
        makePropEnum("Shape", {"Box", "Sphere", "Capsule"},
            [p]{ return (int)p->shape; },
            [p](int v){ p->shape = (EPhysicsShape)v; }),
        makePropEnum("Motion", {"Static", "Dynamic", "Kinematic"},
            [p]{ return (int)p->motionType; },
            [p](int v){ p->motionType = (EMotionType)v; }),
        makePropFloat("Friction",    PropColor::Neutral, 0.01f,
            [p]{ return p->friction; },
            [p, applyMaterial](float v){ p->friction = std::max(0.f, v); applyMaterial(); }, PropUnit::Ratio),
        makePropFloat("Restitution", PropColor::Neutral, 0.01f,
            [p]{ return p->restitution; },
            [p, applyMaterial](float v){ p->restitution = std::clamp(v, 0.f, 1.f); applyMaterial(); }, PropUnit::Ratio),
        makePropFloat("Mass",        PropColor::Neutral, 0.1f,
            [p]{ return p->mass; },
            [p](float v){ p->mass = std::max(0.001f, v); }, PropUnit::Size),
        makePropFloat("HalfX",       PropColor::Red,   0.01f,
            [p]{ return p->halfExtent.x; },
            [p](float v){ p->halfExtent.x = std::max(0.001f, v); }, PropUnit::Size),
        makePropFloat("HalfY",       PropColor::Green, 0.01f,
            [p]{ return p->halfExtent.y; },
            [p](float v){ p->halfExtent.y = std::max(0.001f, v); }, PropUnit::Size),
        makePropFloat("HalfZ",       PropColor::Blue,  0.01f,
            [p]{ return p->halfExtent.z; },
            [p](float v){ p->halfExtent.z = std::max(0.001f, v); }, PropUnit::Size),
    };
}

// ── 씬 파일용 이름 ──────────────────────────────────────────────────────────────
const char* RigidBodyComponent::shapeName(EPhysicsShape s) {
    switch (s) {
        case EPhysicsShape::Sphere:  return "sphere";
        case EPhysicsShape::Capsule: return "capsule";
        default:                     return "box";
    }
}

const char* RigidBodyComponent::motionName(EMotionType m) {
    switch (m) {
        case EMotionType::Static:    return "static";
        case EMotionType::Kinematic: return "kinematic";
        default:                     return "dynamic";
    }
}

// 옛 파일의 순번(0~2)도 받습니다. 범위를 벗어난 숫자는 거부합니다(예전에는 검사 없이 캐스트).
static bool legacyOrdinal(const std::string& t, int count, int& out) {
    if (t.empty() || t.size() > 2) return false;
    char* end = nullptr;
    const long v = std::strtol(t.c_str(), &end, 10);
    if (end != t.c_str() + t.size() || v < 0 || v >= count) return false;
    out = (int)v;
    return true;
}

bool RigidBodyComponent::shapeFromText(const std::string& t, EPhysicsShape& out) {
    if (t == "box")     { out = EPhysicsShape::Box;     return true; }
    if (t == "sphere")  { out = EPhysicsShape::Sphere;  return true; }
    if (t == "capsule") { out = EPhysicsShape::Capsule; return true; }
    int v = 0;
    if (!legacyOrdinal(t, 3, v)) return false;
    out = (EPhysicsShape)v;
    return true;
}

bool RigidBodyComponent::motionFromText(const std::string& t, EMotionType& out) {
    if (t == "static")    { out = EMotionType::Static;    return true; }
    if (t == "dynamic")   { out = EMotionType::Dynamic;   return true; }
    if (t == "kinematic") { out = EMotionType::Kinematic; return true; }
    int v = 0;
    if (!legacyOrdinal(t, 3, v)) return false;
    out = (EMotionType)v;
    return true;
}
