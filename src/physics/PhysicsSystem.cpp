#include "PhysicsSystem.hpp"
#include "../core/World.hpp"
#include "../core/Actor.hpp"
#include "../components/RigidBodyComponent.hpp"

// Jolt headers
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Body/BodyLock.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <thread>
#include "../core/HLog.hpp"

static constinit HLogCategory LogPhysics{ "Physics", ELogVerbosity::Log };

// ── Layer definitions ─────────────────────────────────────────────────────────
namespace Layers {
    static constexpr JPH::ObjectLayer STATIC  = 0;
    static constexpr JPH::ObjectLayer DYNAMIC = 1;
    static constexpr JPH::uint        NUM     = 2;
}

namespace BPLayers {
    static constexpr JPH::BroadPhaseLayer STATIC  {0};
    static constexpr JPH::BroadPhaseLayer DYNAMIC {1};
    static constexpr JPH::uint            NUM     = 2;
}

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return l == Layers::STATIC ? BPLayers::STATIC : BPLayers::DYNAMIC;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override {
        return (JPH::uint8)l == 0 ? "STATIC" : "DYNAMIC";
    }
#endif
};

class ObjVsBPFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer obj, JPH::BroadPhaseLayer bp) const override {
        if (obj == Layers::STATIC)  return (JPH::uint8)bp == (JPH::uint8)BPLayers::DYNAMIC;
        if (obj == Layers::DYNAMIC) return true;
        return false;
    }
};

class ObjLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::STATIC)  return b == Layers::DYNAMIC;
        if (a == Layers::DYNAMIC) return true;
        return false;
    }
};

// ── JoltImpl ──────────────────────────────────────────────────────────────────
struct JoltImpl {
    BPLayerInterface                        bpLayerInterface;
    ObjVsBPFilter                           objVsBpFilter;
    ObjLayerPairFilter                      objLayerPairFilter;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem>     physics;
};

static constexpr JPH::uint kBodyMutexes    = 0;     // auto-detect
// 콜라이더 반지름 · 반변의 최솟값(스케일 적용 뒤). 이보다 작으면 바디를 만들지 않고 알립니다(DEF-107).
// 0 크기 상자는 Jolt 가 만들어 주지만 질량 · 관성이 0 이 되어 동적 바디에서 NaN 이 납니다.
static constexpr float     kMinColliderHalf = 1e-4f;
static constexpr JPH::uint kMaxBodyPairs   = 65536;
static constexpr JPH::uint kMaxConstraints = 2048;

PhysicsSystem::PhysicsSystem() = default;
PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::startup() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    impl_ = std::make_unique<JoltImpl>();
    impl_->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(8 * 1024 * 1024);
    impl_->jobSystem     = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        (int)std::thread::hardware_concurrency() - 1);

    impl_->physics = std::make_unique<JPH::PhysicsSystem>();
    impl_->physics->Init((JPH::uint)maxBodies_, kBodyMutexes, kMaxBodyPairs, kMaxConstraints,
        impl_->bpLayerInterface, impl_->objVsBpFilter, impl_->objLayerPairFilter);
    impl_->physics->SetGravity(JPH::Vec3(0.f, -9.81f, 0.f));

    HLOG(LogPhysics, Log) << "Jolt initialized";
}

void PhysicsSystem::shutdown() {
    if (!impl_) return;

    if (world_) {
        auto& bi = impl_->physics->GetBodyInterface();
        for (auto& ap : world_->getActors()) {
            auto* rb = ap->getComponent<RigidBodyComponent>();
            if (!rb || rb->bodyId_ == 0xFFFFFFFFu) continue;
            JPH::BodyID id(rb->bodyId_);
            bi.RemoveBody(id);
            bi.DestroyBody(id);
            rb->bodyId_  = 0xFFFFFFFFu;
            rb->physSys_ = nullptr;
        }
    }

    impl_.reset();
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    HLOG(LogPhysics, Log) << "Jolt shutdown";
}

void PhysicsSystem::onBeginPlay() {
    if (!world_) return;
    for (auto& ap : world_->getActors()) {
        auto* rb = ap->getComponent<RigidBodyComponent>();
        if (rb && rb->bodyId_ == 0xFFFFFFFFu)
            rb->createFailed_ = false;
    }
}

void PhysicsSystem::setStepRate(float hz) {
    // 너무 느리거나 빠른 값은 막습니다(30 ~ 240 Hz).
    step_        = 1.f / std::clamp(hz, 30.f, 240.f);
    accumulator_ = std::min(accumulator_, step_);
}

void PhysicsSystem::removeBody(uint32_t id) {
    if (!impl_) return;
    JPH::BodyID jid(id);
    auto& bi = impl_->physics->GetBodyInterface();
    frameForces_.erase(id);
    impulseBudget_.erase(id);
    if (!bi.IsAdded(jid)) return;
    // 지우기 전에 차지하던 영역을 기억했다가, 지운 뒤 그 영역에 닿는 바디를 깨웁니다.
    // 그러지 않으면 바닥을 끈 뒤에도 그 위에서 잠든 상자가 공중에 그대로 남습니다(DEF-80).
    JPH::AABox bounds = bi.GetTransformedShape(jid).GetWorldSpaceBounds();
    bi.RemoveBody(jid);
    bi.DestroyBody(jid);
    bounds.ExpandBy(JPH::Vec3::sReplicate(0.05f));
    bi.ActivateBodiesInAABox(bounds, JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter());
}

void PhysicsSystem::setMaterial(uint32_t id, float friction, float restitution) {
    if (!impl_) return;
    JPH::BodyID jid(id);
    auto& bi = impl_->physics->GetBodyInterface();
    if (!bi.IsAdded(jid)) return;
    bi.SetFriction(jid, friction);
    bi.SetRestitution(jid, restitution);
}

bool PhysicsSystem::queryBody(uint32_t id, BodyInfo& out) const {
    if (!impl_) return false;
    JPH::BodyID jid(id);
    const auto& bi = impl_->physics->GetBodyInterface();
    if (!bi.IsAdded(jid)) return false;
    out.active      = bi.IsActive(jid);
    out.friction    = bi.GetFriction(jid);
    out.restitution = bi.GetRestitution(jid);
    const JPH::RVec3 p = bi.GetPosition(jid);
    out.posY        = (float)p.GetY();
    out.position    = { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
    return true;
}

// ── 물리 API (DEF-19) ─────────────────────────────────────────────────────────
namespace {
JPH::Vec3  toJ(const glm::vec3& v)  { return JPH::Vec3(v.x, v.y, v.z); }
JPH::RVec3 toJR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
glm::vec3  toG(JPH::Vec3Arg v)      { return { v.GetX(), v.GetY(), v.GetZ() }; }
}

bool PhysicsSystem::isDynamic(uint32_t id) const {
    if (!impl_ || id == 0xFFFFFFFFu) return false;
    const auto& bi = impl_->physics->GetBodyInterface();
    const JPH::BodyID jid(id);
    return bi.IsAdded(jid) && bi.GetMotionType(jid) == JPH::EMotionType::Dynamic;
}

void PhysicsSystem::setGravity(const glm::vec3& g) {
    if (!impl_) return;
    impl_->physics->SetGravity(toJ(g));
    // 잠든 바디는 중력이 바뀌어도 스스로 깨지 않습니다. 모두 깨워 새 중력을 받게 합니다.
    JPH::BodyIDVector ids;
    impl_->physics->GetBodies(ids);
    if (!ids.empty())
        impl_->physics->GetBodyInterface().ActivateBodies(ids.data(), (int)ids.size());
}

glm::vec3 PhysicsSystem::gravity() const {
    return impl_ ? toG(impl_->physics->GetGravity()) : glm::vec3(0.f, -9.81f, 0.f);
}

void PhysicsSystem::addForce(uint32_t body, const glm::vec3& force) {
    if (!isDynamic(body)) return;
    frameForces_[body].force += force;
}

void PhysicsSystem::addForceAtPosition(uint32_t body, const glm::vec3& force, const glm::vec3& worldPoint) {
    if (!isDynamic(body)) return;
    // 질량 중심에 거는 힘 + 그 점에서 생기는 토크로 나눠 둡니다(부른 순간의 질량 중심 기준).
    const auto& bi = impl_->physics->GetBodyInterface();
    const JPH::RVec3 com = bi.GetCenterOfMassPosition(JPH::BodyID(body));
    const glm::vec3 r = worldPoint - glm::vec3((float)com.GetX(), (float)com.GetY(), (float)com.GetZ());
    ForceTorque& ft = frameForces_[body];
    ft.force  += force;
    ft.torque += glm::cross(r, force);
}

void PhysicsSystem::addTorque(uint32_t body, const glm::vec3& torque) {
    if (!isDynamic(body)) return;
    frameForces_[body].torque += torque;
}

void PhysicsSystem::addImpulse(uint32_t body, const glm::vec3& impulse) {
    if (!isDynamic(body)) return;
    impl_->physics->GetBodyInterface().AddImpulse(JPH::BodyID(body), toJ(impulse));
}

void PhysicsSystem::addImpulseAtPosition(uint32_t body, const glm::vec3& impulse, const glm::vec3& worldPoint) {
    if (!isDynamic(body)) return;
    impl_->physics->GetBodyInterface().AddImpulse(JPH::BodyID(body), toJ(impulse), toJR(worldPoint));
}

void PhysicsSystem::setLinearVelocity(uint32_t body, const glm::vec3& v) {
    if (!isDynamic(body)) return;
    impl_->physics->GetBodyInterface().SetLinearVelocity(JPH::BodyID(body), toJ(v));
}

glm::vec3 PhysicsSystem::linearVelocity(uint32_t body) const {
    if (!impl_ || body == 0xFFFFFFFFu) return glm::vec3(0.f);
    const auto& bi = impl_->physics->GetBodyInterface();
    return bi.IsAdded(JPH::BodyID(body)) ? toG(bi.GetLinearVelocity(JPH::BodyID(body))) : glm::vec3(0.f);
}

void PhysicsSystem::setAngularVelocity(uint32_t body, const glm::vec3& w) {
    if (!isDynamic(body)) return;
    impl_->physics->GetBodyInterface().SetAngularVelocity(JPH::BodyID(body), toJ(w));
}

glm::vec3 PhysicsSystem::angularVelocity(uint32_t body) const {
    if (!impl_ || body == 0xFFFFFFFFu) return glm::vec3(0.f);
    const auto& bi = impl_->physics->GetBodyInterface();
    return bi.IsAdded(JPH::BodyID(body)) ? toG(bi.GetAngularVelocity(JPH::BodyID(body))) : glm::vec3(0.f);
}

bool PhysicsSystem::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance,
                            RaycastHit& out) const {
    out = {};
    if (!impl_ || !world_ || maxDistance <= 0.f) return false;
    const float len = glm::length(dir);
    if (len < 1e-8f) return false;
    const glm::vec3 d = dir / len;
    const JPH::RRayCast ray{ toJR(origin), toJ(d * maxDistance) };
    JPH::RayCastResult hit;
    if (!impl_->physics->GetNarrowPhaseQuery().CastRay(ray, hit)) return false;

    out.distance = hit.mFraction * maxDistance;
    out.point    = origin + d * out.distance;
    JPH::BodyLockRead lock(impl_->physics->GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        out.normal = toG(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
        out.actor  = world_->findById(body.GetUserData());
    }
    return true;
}

void PhysicsSystem::overlapSphere(const glm::vec3& center, float radius, std::vector<Actor*>& out) const {
    out.clear();
    if (!impl_ || !world_ || radius <= 0.f) return;
    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();   // 스택에 둔 셰이프라 참조 수로 지우지 않게 합니다
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
    impl_->physics->GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sReplicate(1.f),
        JPH::RMat44::sTranslation(toJR(center)), settings, JPH::RVec3::sZero(), hits);
    for (const auto& h : hits.mHits) {
        JPH::BodyLockRead lock(impl_->physics->GetBodyLockInterface(), h.mBodyID2);
        if (!lock.Succeeded()) continue;
        Actor* a = world_->findById(lock.GetBody().GetUserData());
        if (a && std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
    }
}

void PhysicsSystem::teleport(uint32_t id, const Actor& a) {
    if (!impl_) return;
    JPH::BodyID jid(id);
    auto& bi = impl_->physics->GetBodyInterface();
    if (!bi.IsAdded(jid)) return;
    const glm::vec3 pos = a.worldPosition();
    const glm::quat q   = a.worldRotation();
    // 옮기기 전후 자리에 닿는 바디를 깨웁니다. Jolt 는 옮긴 바디 자신만 깨우고(정적 바디는 그것도
    // 안 함), 받치던 바닥을 내리면 그 위에서 잠든 상자가 공중에 남습니다(removeBody 와 같은 이유).
    JPH::AABox before = bi.GetTransformedShape(jid).GetWorldSpaceBounds();
    bi.SetPositionAndRotation(jid, JPH::RVec3(pos.x, pos.y, pos.z),
                              JPH::Quat(q.x, q.y, q.z, q.w), JPH::EActivation::Activate);
    JPH::AABox after = bi.GetTransformedShape(jid).GetWorldSpaceBounds();
    before.ExpandBy(JPH::Vec3::sReplicate(0.05f));
    after.ExpandBy(JPH::Vec3::sReplicate(0.05f));
    bi.ActivateBodiesInAABox(before, JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter());
    bi.ActivateBodiesInAABox(after,  JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter());
}

void PhysicsSystem::tick(float dt) {
    if (!impl_ || !world_) return;

    auto& bi = impl_->physics->GetBodyInterface();

    // Lazy body creation
    for (auto& ap : world_->getActors()) {
        Actor* a  = ap.get();
        auto*  rb = a->getComponent<RigidBodyComponent>();
        // activeInHierarchy() 게이트(A-2): 비활성 액터(또는 비활성 부모 아래)는 바디를
        // 만들지 않습니다. 바디가 없으면 아래 sync 루프(bodyId_ 검사)도 자동으로 건너뛰어,
        // 렌더·틱과 물리가 일관되게 꺼집니다. 이미 만든 바디는 플레이 도중 꺼질 때
        // RigidBodyComponent::onDisable 이 지웁니다(DEF-80).
        if (!rb || !rb->isEnabled() || !a->activeInHierarchy()
            || rb->bodyId_ != 0xFFFFFFFFu || rb->createFailed_) continue;

        // ── 셰이프 생성 ──────────────────────────────────────────────────────
        // DEF-41: 예전에는 halfExtent 를 그대로 Jolt 에 넘겨 Transform.scale 을
        // 무시했습니다. 그래서 스케일 3 인 단위 큐브가 3 m 로 그려지고 1 m 로
        // 충돌했습니다. 절차 도형은 크기를 전적으로 scale 에 싣기 때문에
        // (Primitive_And_Lighting_Design.md 규칙 R1) 이 보정이 선결 조건입니다.
        //
        // DEF-53: 로컬이 아니라 월드 스케일/회전을 씁니다. 부모가 없으면 로컬=월드라
        // 기존 동작이 그대로 유지되고, 부모가 생기면 콜라이더가 시각적 위치와 겹칩니다.
        const glm::vec3 sc = glm::abs(a->worldScale());

        // Sphere 와 Capsule 은 비균등 스케일에 해석적 대응이 없습니다.
        // Jolt 의 ScaledShape 도 이 둘에 균등 스케일을 요구합니다.
        // 조용히 틀리는 것보다 시끄럽게 근사하는 편이 낫습니다.
        const float maxSc = std::max({ sc.x, sc.y, sc.z });
        auto uniformScale = [&](const char* shapeName) -> float {
            const float minSc = std::min({ sc.x, sc.y, sc.z });
            if (maxSc - minSc > 1e-4f * std::max(1.f, maxSc))
                HLOG(LogPhysics, Warning) << "'" << a->name << "': " << shapeName
                                          << " collider supports only uniform scale; scale("
                                          << sc.x << ", " << sc.y << ", " << sc.z
                                          << ") approximated by its largest component " << maxSc;
            return maxSc;
        };

        // 셰이프를 만들지 못하면(크기 0 등) 경고하고 바디를 만들지 않습니다. Jolt 의 ShapeResult 는
        // 오류 문자열과 결과를 같은 자리에 담아, 실패한 결과를 Get() 으로 꺼내면 쓰레기 포인터가 나옵니다.
        // 예전에는 확인하지 않아 두께 10cm 인 상자만 있어도 Play 에서 엔진이 죽었습니다(DEF-107).
        JPH::ShapeSettings::ShapeResult made;
        const char* tooSmall = nullptr;
        switch (rb->shape) {
            case EPhysicsShape::Sphere: {
                const float r = rb->halfExtent.x * uniformScale("Sphere");
                if (!(r >= kMinColliderHalf)) { tooSmall = "radius"; break; }
                made = JPH::SphereShapeSettings(r).Create();
                break;
            }
            case EPhysicsShape::Capsule: {
                // halfExtent.y 는 원통부만의 반높이이고 halfExtent.x 가 반지름입니다.
                // 총 높이는 2*(halfExtent.y + halfExtent.x) 입니다 (규칙 P3).
                const float s = uniformScale("Capsule");
                if (!(rb->halfExtent.x * s >= kMinColliderHalf) || !(rb->halfExtent.y * s >= 0.f)) {
                    tooSmall = "radius"; break;
                }
                made = JPH::CapsuleShapeSettings(rb->halfExtent.y * s,
                                                 rb->halfExtent.x * s).Create();
                break;
            }
            default: {
                const JPH::Vec3 he(rb->halfExtent.x * sc.x, rb->halfExtent.y * sc.y, rb->halfExtent.z * sc.z);
                if (!(he.ReduceMin() >= kMinColliderHalf)) { tooSmall = "half extent"; break; }
                // 상자의 모서리 반경(convex radius)은 가장 짧은 반변보다 클 수 없습니다. Jolt 기본값 5cm 를
                // 그대로 쓰면 두께 10cm 이하의 바닥 · 벽은 만들어지지 않았습니다. Godot 의 Jolt 모듈과 UE Chaos 처럼
                // 얇은 쪽에 맞춰 줄입니다(반변의 절반까지). 두께 10cm 이상이면 예전과 같습니다.
                const float cr = std::min(JPH::cDefaultConvexRadius, 0.5f * he.ReduceMin());
                made = JPH::BoxShapeSettings(he, cr).Create();
                break;
            }
        }
        if (tooSmall || made.HasError() || made.Get() == nullptr) {
            rb->createFailed_ = true;
            HLOG(LogPhysics, Warning) << "'" << a->name << "': collider could not be created ("
                << (tooSmall ? std::string(tooSmall) + " below 0.1 mm after scale"
                             : made.HasError() ? std::string(made.GetError().c_str()) : std::string("no shape"))
                << "); the actor has no physics body until its size is fixed";
            continue;
        }
        const JPH::ShapeRefC shape = made.Get();

        // Motion type mapping
        JPH::EMotionType mt = JPH::EMotionType::Static;
        JPH::ObjectLayer  ol = Layers::STATIC;
        if (rb->motionType == EMotionType::Dynamic) {
            mt = JPH::EMotionType::Dynamic;
            ol = Layers::DYNAMIC;
        } else if (rb->motionType == EMotionType::Kinematic) {
            mt = JPH::EMotionType::Kinematic;
            ol = Layers::DYNAMIC;
        }

        glm::vec3 pos = a->worldPosition();
        glm::quat q   = a->worldRotation();

        JPH::BodyCreationSettings bcs(
            shape,
            JPH::RVec3(pos.x, pos.y, pos.z),
            JPH::Quat(q.x, q.y, q.z, q.w),
            mt, ol);

        bcs.mFriction    = rb->friction;
        bcs.mRestitution = rb->restitution;
        bcs.mUserData    = a->id;   // 레이캐스트 · 겹침 질의에서 액터를 찾을 때 씁니다
        if (mt == JPH::EMotionType::Dynamic) {
            bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bcs.mMassPropertiesOverride.mMass = rb->mass;
        }

        JPH::Body* body = bi.CreateBody(bcs);
        if (!body) {
            // 바디 상한(physics.maxBodies)에 닿으면 Jolt 는 아무 말 없이 null 을 돌려줍니다. 실패를
            // 표시해 매 틱 셰이프를 다시 만들며 재시도하지 않게 하고(Play·속성 편집 때 다시
            // 시도), 처음 한 번 알립니다(DEF-10).
            rb->createFailed_ = true;
            if (!bodyCapWarned_) {
                bodyCapWarned_ = true;
                HLOG(LogPhysics, Warning) << "Body creation failed for '" << a->name << "' ("
                    << impl_->physics->GetNumBodies() << "/" << impl_->physics->GetMaxBodies()
                    << " bodies). Further failures are not logged; raise physics.maxBodies in Config/engine.ini";
            }
            continue;
        }
        bi.AddBody(body->GetID(), JPH::EActivation::Activate);
        rb->bodyId_       = body->GetID().GetIndexAndSequenceNumber();
        rb->physSys_      = this;
        // 편집 확정 때 바디를 다시 만들어야 하는지 이 값들과 비교합니다(RigidBodyComponent).
        rb->createdScale_  = sc;
        rb->createdShape_  = rb->shape;
        rb->createdMotion_ = rb->motionType;
        rb->createdHalf_   = rb->halfExtent;
        rb->createdMass_   = rb->mass;
        rb->synced_        = false;   // 아직 물리가 액터에 쓴 적 없음
    }

    // ── 게임 코드가 옮긴 Dynamic 바디: 그 자리로 순간 이동, 속도 유지(DEF-19) ──────
    // 물리가 마지막으로 쓴 로컬 자세와 다르면 게임 코드(또는 에디터)가 옮긴 것입니다. 로컬로 비교하므로
    // 부모가 움직인 것은 순간 이동으로 치지 않습니다(부모 아래 Dynamic 바디는 월드 자세 유지).
    // Kinematic 바디는 목표 자세(액터의 월드 자세)를 아래 스텝 루프에서 나눠 따라갑니다(DEF-99).
    struct KinematicMove { JPH::BodyID id; glm::vec3 p0, p1; glm::quat q0, q1; };
    std::vector<KinematicMove> kinematic;
    for (auto& ap : world_->getActors()) {
        Actor* a  = ap.get();
        auto*  rb = a->getComponent<RigidBodyComponent>();
        if (!rb || rb->bodyId_ == 0xFFFFFFFFu) continue;
        const JPH::BodyID id(rb->bodyId_);
        if (rb->motionType == EMotionType::Dynamic) {
            const Transform& t = a->transform();
            // teleport 는 속도를 건드리지 않고, 옮기기 전후 자리의 잠든 바디도 깨웁니다.
            if (rb->synced_ && (t.position() != rb->syncedLocalPos_ || t.rotation() != rb->syncedLocalRot_))
                teleport(rb->bodyId_, *a);
        } else if (rb->motionType == EMotionType::Kinematic) {
            const JPH::RVec3 bp = bi.GetPosition(id);
            const JPH::Quat  bq = bi.GetRotation(id);
            kinematic.push_back({ id,
                glm::vec3((float)bp.GetX(), (float)bp.GetY(), (float)bp.GetZ()), a->worldPosition(),
                glm::quat(bq.GetW(), bq.GetX(), bq.GetY(), bq.GetZ()), a->worldRotation() });
        }
    }

    // ── 힘 · 토크: 이번 틱의 충격량을 예산에 더합니다 ───────────────────────────
    for (const auto& [id, ft] : frameForces_) {
        ForceTorque& b = impulseBudget_[id];
        b.force  += ft.force  * dt;
        b.torque += ft.torque * dt;
    }
    frameForces_.clear();

    // ── Fixed-step simulation ─────────────────────────────────────────────────
    accumulator_ += dt;
    const int steps = (int)std::floor(accumulator_ / step_);
    for (int i = 0; i < steps; ++i) {
        // Kinematic: 틱 시작 자세에서 목표까지 스텝마다 나눠 옮깁니다. 마지막 스텝에 정확히 목표에 닿고,
        // 그 속도로 부딪힌 Dynamic 바디를 밉니다.
        const float alpha = float(i + 1) / float(steps);
        for (const auto& k : kinematic) {
            const glm::vec3 p = glm::mix(k.p0, k.p1, alpha);
            const glm::quat q = glm::slerp(k.q0, k.q1, alpha);
            bi.MoveKinematic(k.id, JPH::RVec3(p.x, p.y, p.z), JPH::Quat(q.x, q.y, q.z, q.w), step_);
        }
        // 힘 · 토크: 남은 충격량을 남은 스텝에 고르게 나눕니다. Jolt 는 건 힘을 이 스텝 한 번에만 씁니다.
        const float share = 1.f / float(steps - i);
        for (auto& [id, b] : impulseBudget_) {
            const glm::vec3 j = b.force * share, l = b.torque * share;
            b.force  -= j;
            b.torque -= l;
            const JPH::BodyID jid(id);
            if (!bi.IsAdded(jid)) continue;
            bi.AddForceAndTorque(jid, JPH::Vec3(j.x, j.y, j.z) / step_, JPH::Vec3(l.x, l.y, l.z) / step_);
        }

        const JPH::EPhysicsUpdateError err =
            impl_->physics->Update(step_, 1, impl_->tempAllocator.get(), impl_->jobSystem.get());
        // 접촉·바디 쌍 버퍼가 넘치면 일부 충돌을 건너뛴 것입니다. 예전에는 반환값을 버렸습니다(DEF-10).
        if (err != JPH::EPhysicsUpdateError::None && !updateErrorWarned_) {
            updateErrorWarned_ = true;
            const auto has = [err](JPH::EPhysicsUpdateError f) { return ((uint32_t)err & (uint32_t)f) != 0; };
            HLOG(LogPhysics, Warning) << "Physics step overflow:"
                << (has(JPH::EPhysicsUpdateError::ManifoldCacheFull)      ? " manifold-cache" : "")
                << (has(JPH::EPhysicsUpdateError::BodyPairCacheFull)      ? " body-pair-cache" : "")
                << (has(JPH::EPhysicsUpdateError::ContactConstraintsFull) ? " contact-constraints" : "")
                << " full; some contacts were ignored this step (warned once)";
        }
        accumulator_ -= step_;
    }
    // 스텝이 없던 틱의 충격량은 다음 틱의 스텝에 넣습니다.
    if (steps > 0) impulseBudget_.clear();

    // ── Sync transforms: Dynamic 바디의 월드 자세를 액터에 씁니다 ────────────────
    // 월드 세터로 씁니다. 부모가 있으면 부모의 지금 월드 트랜스폼으로 로컬을 구합니다(DEF-53). 잠든
    // 바디도 씁니다. 그래야 부모가 움직여도 자식 바디가 월드 자리에 남습니다. 쓴 로컬 값을 기억해 두었다가
    // 다음 틱에 게임 코드가 바꿨는지 비교합니다.
    for (auto& ap : world_->getActors()) {
        Actor* a  = ap.get();
        auto*  rb = a->getComponent<RigidBodyComponent>();
        if (!rb || rb->bodyId_ == 0xFFFFFFFFu || rb->motionType != EMotionType::Dynamic) continue;
        const JPH::BodyID id(rb->bodyId_);
        const JPH::RVec3 p = bi.GetPosition(id);
        const JPH::Quat  r = bi.GetRotation(id);
        a->setWorldPosition(glm::vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ()));
        a->setWorldRotation(glm::quat(r.GetW(), r.GetX(), r.GetY(), r.GetZ()));
        rb->syncedLocalPos_ = a->transform().position();
        rb->syncedLocalRot_ = a->transform().rotation();
        rb->synced_         = true;
    }
}
