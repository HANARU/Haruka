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

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
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

static constexpr JPH::uint kMaxBodies      = 4096;
static constexpr JPH::uint kBodyMutexes    = 0;     // auto-detect
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
    impl_->physics->Init(kMaxBodies, kBodyMutexes, kMaxBodyPairs, kMaxConstraints,
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

void PhysicsSystem::removeBody(uint32_t id) {
    if (!impl_) return;
    JPH::BodyID jid(id);
    auto& bi = impl_->physics->GetBodyInterface();
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
    out.posY        = (float)bi.GetPosition(jid).GetY();
    return true;
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

        JPH::ShapeRefC shape;
        switch (rb->shape) {
            case EPhysicsShape::Sphere: {
                const float r = rb->halfExtent.x * uniformScale("Sphere");
                shape = JPH::SphereShapeSettings(r).Create().Get();
                break;
            }
            case EPhysicsShape::Capsule: {
                // halfExtent.y 는 원통부만의 반높이이고 halfExtent.x 가 반지름입니다.
                // 총 높이는 2*(halfExtent.y + halfExtent.x) 입니다 (규칙 P3).
                const float s = uniformScale("Capsule");
                shape = JPH::CapsuleShapeSettings(rb->halfExtent.y * s,
                                                  rb->halfExtent.x * s).Create().Get();
                break;
            }
            default:
                shape = JPH::BoxShapeSettings(
                    JPH::Vec3(rb->halfExtent.x * sc.x,
                              rb->halfExtent.y * sc.y,
                              rb->halfExtent.z * sc.z)).Create().Get();
                break;
        }
        if (!shape) { rb->createFailed_ = true; continue; }

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
        if (mt == JPH::EMotionType::Dynamic) {
            bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bcs.mMassPropertiesOverride.mMass = rb->mass;
        }

        JPH::Body* body = bi.CreateBody(bcs);
        if (!body) continue;
        bi.AddBody(body->GetID(), JPH::EActivation::Activate);
        rb->bodyId_       = body->GetID().GetIndexAndSequenceNumber();
        rb->physSys_      = this;
        // 편집 확정 때 바디를 다시 만들어야 하는지 이 값들과 비교합니다(RigidBodyComponent).
        rb->createdScale_  = sc;
        rb->createdShape_  = rb->shape;
        rb->createdMotion_ = rb->motionType;
        rb->createdHalf_   = rb->halfExtent;
        rb->createdMass_   = rb->mass;
    }

    // Fixed-step simulation
    accumulator_ += dt;
    while (accumulator_ >= kStep) {
        impl_->physics->Update(kStep, 1, impl_->tempAllocator.get(), impl_->jobSystem.get());
        accumulator_ -= kStep;
    }

    // Sync transforms
    for (auto& ap : world_->getActors()) {
        Actor* a  = ap.get();
        auto*  rb = a->getComponent<RigidBodyComponent>();
        if (!rb || rb->bodyId_ == 0xFFFFFFFFu) continue;

        JPH::BodyID id(rb->bodyId_);

        if (rb->motionType == EMotionType::Kinematic) {
            glm::vec3 pos = a->worldPosition();
            glm::quat q   = a->worldRotation();   // DEF-53: 로컬 아님
            bi.MoveKinematic(id,
                JPH::RVec3(pos.x, pos.y, pos.z),
                JPH::Quat(q.x, q.y, q.z, q.w),
                kStep);
        } else if (rb->motionType == EMotionType::Dynamic) {
            if (!bi.IsActive(id)) continue;
            auto p = bi.GetPosition(id);
            auto r = bi.GetRotation(id);
            // Jolt 는 월드 포즈를 주는데 세터는 로컬에 씁니다. 부모가 없으면
            // 로컬=월드라 정확하고, **부모가 있는 다이내믹 바디**만 어긋납니다.
            // 그 경우 월드→로컬 변환이 필요하며(별도 항목), 현재 씬엔 없습니다.
            a->setPosition(glm::vec3(p.GetX(), p.GetY(), p.GetZ()));
            a->setRotation(glm::quat(r.GetW(), r.GetX(), r.GetY(), r.GetZ()));
        }
    }
}
