#include "RigidBodyComponent.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include "PhysicsSystem.hpp"
#include "PhysicsLayers.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
#include "../scene/Reflect.hpp"
#include "../scene/ComponentFields.hpp"
#include <algorithm>
#include <cmath>
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

void RigidBodyComponent::addForce(const glm::vec3& f) {
    if (hasBody()) physSys_->addForce(bodyId_, f);
}
void RigidBodyComponent::addForceAtPosition(const glm::vec3& f, const glm::vec3& p) {
    if (hasBody()) physSys_->addForceAtPosition(bodyId_, f, p);
}
void RigidBodyComponent::addTorque(const glm::vec3& t) {
    if (hasBody()) physSys_->addTorque(bodyId_, t);
}
void RigidBodyComponent::addImpulse(const glm::vec3& j) {
    if (hasBody()) physSys_->addImpulse(bodyId_, j);
}
void RigidBodyComponent::addImpulseAtPosition(const glm::vec3& j, const glm::vec3& p) {
    if (hasBody()) physSys_->addImpulseAtPosition(bodyId_, j, p);
}
void RigidBodyComponent::setLinearVelocity(const glm::vec3& v) {
    if (hasBody()) physSys_->setLinearVelocity(bodyId_, v);
}
glm::vec3 RigidBodyComponent::linearVelocity() const {
    return hasBody() ? physSys_->linearVelocity(bodyId_) : glm::vec3(0.f);
}
void RigidBodyComponent::setAngularVelocity(const glm::vec3& w) {
    if (hasBody()) physSys_->setAngularVelocity(bodyId_, w);
}
glm::vec3 RigidBodyComponent::angularVelocity() const {
    return hasBody() ? physSys_->angularVelocity(bodyId_) : glm::vec3(0.f);
}
glm::vec3 RigidBodyComponent::centerOfMass() const {
    glm::vec3 c(0.f);
    if (hasBody() && physSys_->centerOfMass(bodyId_, c)) return c;
    return owner ? owner->worldPosition() : glm::vec3(0.f);
}

bool RigidBodyComponent::setMotionRuntime(EMotionType m) {
    if (m == EMotionType::Static || motionType == EMotionType::Static) return false;
    if (m == motionType) return true;
    motionType = m;
    if (hasBody()) {
        physSys_->setMotionKinematic(bodyId_, m == EMotionType::Kinematic);
        createdMotion_ = m;   // 바디를 다시 만들 이유가 아님
        synced_        = false;   // 물리가 다음 틱에 쓴 자세부터 순간 이동 판정
        interpValid_   = false;   // 손을 따라가던 자세에서 보간을 다시 시작(P4)
    }
    return true;
}

void RigidBodyComponent::setContinuousRuntime(bool on) {
    if (hasBody()) physSys_->setContinuous(bodyId_, on);
}

void RigidBodyComponent::setGravityFactorRuntime(float factor) {
    if (hasBody()) physSys_->setGravityFactor(bodyId_, factor);
}

void RigidBodyComponent::rebuildBody() {
    releaseBody();
    createFailed_ = false;
}

bool RigidBodyComponent::bodyParamsChanged() const {
    return shape != createdShape_ || motionType != createdMotion_ || mass != createdMass_ ||
           halfExtent.x != createdHalf_.x || halfExtent.y != createdHalf_.y ||
           halfExtent.z != createdHalf_.z || center != createdCenter_ || trigger != createdTrigger_;
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
    const glm::vec3 sc = owner->worldScale();   // 부호까지(거울로 뒤집으면 메시 충돌체를 다시 만듦, DEF-170)
    const glm::vec3 d  = glm::abs(sc - createdScale_);
    if (std::max({ d.x, d.y, d.z }) > 1e-5f) { rebuildBody(); return; }
    physSys_->teleport(bodyId_, *owner);
    interpValid_ = false;
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
    // 레이어 칸은 프로젝트의 레이어 이름을 보여 줍니다(D2). 물리 시스템이 없으면(시험 · 도구) 기본 이름입니다. 이름 글은 물리 시스템이
    // 갖고 있으므로 프로퍼티보다 오래 삽니다(표는 시작 때 한 번 넣음).
    static const PhysicsLayerTable kDefaultLayers;
    const PhysicsSystem* ps = physSys_ ? physSys_ : (owner && owner->world() ? owner->world()->service<PhysicsSystem>() : nullptr);
    const PhysicsLayerTable& layers = ps ? ps->layers() : kDefaultLayers;
    std::vector<const char*> layerNames;
    for (const auto& n : layers.names) layerNames.push_back(n.c_str());

    return {
        makePropEnum("Shape", {"Box", "Sphere", "Capsule", "Convex Hull", "Mesh"},
            [p]{ return (int)p->shape; },
            [p](int v){ p->shape = (EPhysicsShape)v; }),
        makePropEnum("Motion", {"Static", "Dynamic", "Kinematic"},
            [p]{ return (int)p->motionType; },
            [p](int v){ p->motionType = (EMotionType)v; }),
        makePropEnum("Layer", std::move(layerNames),
            [p]{ return p->layer; },
            [p](int v){ p->layer = std::clamp(v, 0, PhysicsLayerTable::kCount - 1); if (p->hasBody()) p->physSys_->setLayer(p->bodyId_, p->layer); }),
        makePropToggle("Trigger",
            [p]{ return p->trigger; },
            [p](bool v){ p->trigger = v; }),   // 확정할 때 몸을 다시 만듦(onPropsCommitted)
        makePropToggle("Contact Events",
            [p]{ return p->contactEvents; },
            [p](bool v){ p->contactEvents = v; if (p->hasBody()) p->physSys_->setContactEvents(p->bodyId_, v); }),
        makePropFloat("Friction",    PropColor::Neutral, 0.01f,
            [p]{ return p->friction; },
            [p, applyMaterial](float v){ p->friction = std::max(0.f, v); applyMaterial(); }, PropUnit::Ratio),
        makePropFloat("Restitution", PropColor::Neutral, 0.01f,
            [p]{ return p->restitution; },
            [p, applyMaterial](float v){ p->restitution = std::clamp(v, 0.f, 1.f); applyMaterial(); }, PropUnit::Ratio),
        makePropToggle("Continuous Collision",
            [p]{ return p->continuous; },
            [p](bool v){ p->continuous = v; if (p->hasBody()) p->physSys_->setContinuous(p->bodyId_, v); }),
        makePropToggle("Interpolate",
            [p]{ return p->interpolate; },
            [p](bool v){ p->interpolate = v; p->interpValid_ = false; }),
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
        makePropFloat("CenterX",     PropColor::Red,   0.01f,
            [p]{ return p->center.x; }, [p](float v){ p->center.x = v; }, PropUnit::Size),
        makePropFloat("CenterY",     PropColor::Green, 0.01f,
            [p]{ return p->center.y; }, [p](float v){ p->center.y = v; }, PropUnit::Size),
        makePropFloat("CenterZ",     PropColor::Blue,  0.01f,
            [p]{ return p->center.z; }, [p](float v){ p->center.z = v; }, PropUnit::Size),
    };
}

// ── 씬 파일용 이름 ──────────────────────────────────────────────────────────────
const char* RigidBodyComponent::shapeName(EPhysicsShape s) {
    switch (s) {
        case EPhysicsShape::Sphere:  return "sphere";
        case EPhysicsShape::Capsule: return "capsule";
        case EPhysicsShape::ConvexHull: return "convex";
        case EPhysicsShape::Mesh:    return "mesh";
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
    if (t == "convex")  { out = EPhysicsShape::ConvexHull; return true; }
    if (t == "mesh")    { out = EPhysicsShape::Mesh;    return true; }
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

// ── 직렬화 (DEF-22) ─────────────────────────────────────────────────────────────
// 셰이프 · 모션은 이름으로 씁니다(DEF-44). 옛 순번(0~2)도 shapeFromText · motionFromText 가 받습니다.
void RigidBodyComponent::writeFields(ComponentFieldWriter& w) const {
    w.s ("shape",       shapeName(shape));
    w.s ("motion",      motionName(motionType));
    w.f ("friction",    friction);
    w.f ("restitution", restitution);
    w.f ("mass",        mass);
    w.v3("halfExtent",  { halfExtent.x, halfExtent.y, halfExtent.z });
    if (continuous) w.b("continuous", true);   // 기본값이면 쓰지 않음(기존 씬 파일이 바뀌지 않게)
    if (center != glm::vec3(0.f)) w.v3("center", center);
    if (interpolate) w.b("interpolate", true);
    if (layer != 0) w.i("layer", layer);   // 기본값은 쓰지 않음(옛 씬 파일이 바뀌지 않게, D2)
    if (trigger) w.b("trigger", true);
    if (contactEvents) w.b("contactEvents", true);
}

bool RigidBodyComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if      (key == "shape")       ok = shapeFromText (value, shape);
    else if (key == "motion")      ok = motionFromText(value, motionType);
    else if (key == "friction")    ok = fieldparse::toFloat(value, friction);
    else if (key == "restitution") ok = fieldparse::toFloat(value, restitution);
    else if (key == "mass")        ok = fieldparse::toFloat(value, mass);
    else if (key == "continuous")  ok = fieldparse::toBool(value, continuous);
    else if (key == "interpolate") ok = fieldparse::toBool(value, interpolate);
    else if (key == "trigger")     ok = fieldparse::toBool(value, trigger);
    else if (key == "contactEvents") ok = fieldparse::toBool(value, contactEvents);
    else if (key == "layer") {
        int l = 0;
        ok = fieldparse::toInt(value, l) && PhysicsLayerTable::valid(l);
        if (ok) layer = l;
    }
    else if (key == "center") {
        glm::vec3 c;
        ok = fieldparse::toVec3(value, c) && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
        if (ok) center = c;
    }
    else if (key == "halfExtent") {
        glm::vec3 h;
        ok = fieldparse::toVec3(value, h);
        if (ok) halfExtent = { h.x, h.y, h.z };
    }
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}

