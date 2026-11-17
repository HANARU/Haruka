// 물리 바디의 생명주기(끄기·편집 확정·트랜스폼 확정)를 화면 없이 검산합니다.
// Vulkan 도 창도 필요 없습니다. World + Actor + RigidBodyComponent + PhysicsSystem(Jolt) 만
// 링크하고, 에디터 대신 컴포넌트의 세터와 확정 훅을 직접 부릅니다.
//
// 이 테스트가 존재하는 이유:
//  - DEF-80: 플레이 중 끈 액터의 바디가 남아 보이지 않는 장애물이 됐습니다. 바디를 지우더라도
//    그 위에서 잠든 바디를 깨우지 않으면 공중에 뜬 채로 남습니다(Jolt 는 스스로 깨우지 않음).
//  - DEF-43: 숫자 칸 드래그가 세터를 프레임마다 불러, 플레이 중 바디를 프레임마다 다시 만들었습니다.
//  - DEF-81: 마찰·반발·질량을 바꿔도 이미 만든 바디에 반영되지 않았습니다.
//  - DEF-44: 셰이프·모션을 순번으로 기록했습니다. 이름으로 쓰고 옛 숫자도 읽어야 합니다.
//  - DEF-107: 두께 10cm 이하 상자 · 크기 0 콜라이더를 만들 때 실패를 확인하지 않아 엔진이 죽었습니다.
//
// 실행: build/Release/HarukaPhysicsTest.exe

#include "../src/core/World.hpp"
#include "../src/core/Actor.hpp"
#include "../src/components/RigidBodyComponent.hpp"
#include "../src/physics/PhysicsSystem.hpp"
#include "../src/geometry/CollisionMesh.hpp"
#include "../src/core/ComponentFields.hpp"
#include "../src/components/XrComponents.hpp"
#include "../src/components/CameraComponent.hpp"
#include "../src/input/HInputSystem.hpp"
#include "../src/assets/AssetManager.hpp"
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <memory>
#include <span>
#include <cstdio>
#include <string>
#include <vector>

// 잡기(XrComponents)가 손 자세 · 스킨 메시를 참조합니다. SkinnedMeshComponent::onFieldsLoaded 의 자리만 채웁니다.
std::vector<AssetPair> AssetManager::loadAll(const std::string&) { return {}; }

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

constexpr uint32_t kNoBody = 0xFFFFFFFFu;
constexpr float    kDt     = 1.f / 60.f;

void run(PhysicsSystem& ps, int frames) {
    for (int i = 0; i < frames; ++i) ps.tick(kDt);
}

// 에디터의 숫자 칸처럼 이름으로 프로퍼티를 찾아 세터를 부릅니다.
void setProp(RigidBodyComponent* rb, const char* label, float v) {
    for (auto& pd : rb->buildProps())
        if (std::string(pd.label) == label && pd.setF) { pd.setF(v); return; }
    std::printf("  (no property '%s')\n", label);
}

// 로컬 바운드만 알리는 시험용 컴포넌트입니다. GPU 메시 없이 바닥 찾기(hxr::floorBelow)를 시험합니다.
class BoundsOnly : public Component {
public:
    const char* typeName() const override { return "BoundsOnly"; }
    bool localBounds(glm::vec3& lo, glm::vec3& hi) const override {
        lo = glm::vec3(-0.5f);
        hi = glm::vec3(0.5f);
        return true;
    }
};

// 충돌용 메시 형상만 내놓는 시험용 컴포넌트입니다(P2). GPU 메시 없이 ConvexHull · Mesh 충돌체를 시험합니다.
class MeshProbe : public Component {
public:
    std::shared_ptr<CollisionMesh> mesh = std::make_shared<CollisionMesh>();
    const char* typeName() const override { return "MeshProbe"; }
    const CollisionMesh* collisionMesh() const override { return mesh.get(); }
    // y 높이의 수평 사각형 [x0, x1] × [0, 1] 을 위(+Y)를 보는 삼각형 둘로 더합니다.
    void addQuad(float x0, float x1, float y) {
        const uint32_t b = (uint32_t)mesh->positions.size();
        mesh->positions.insert(mesh->positions.end(), { { x0, y, 0.f }, { x1, y, 0.f }, { x1, y, 1.f }, { x0, y, 1.f } });
        mesh->indices.insert(mesh->indices.end(), { b, b + 3, b + 2, b, b + 2, b + 1 });
    }
};

// 바운드는 있지만 밟는 면이 아니라고 알리는 컴포넌트입니다(3D UI 패널과 같은 경우).
class NotFloor : public BoundsOnly {
public:
    const char* typeName() const override { return "NotFloor"; }
    bool blocksFloor() const override { return false; }
};

// 위치 · 크기를 주면 단위 상자 바운드를 가진 액터를 만듭니다.
Actor* boundsBox(World& w, const char* name, const glm::vec3& pos, const glm::vec3& scale) {
    Actor* a = w.spawnActor(name);
    a->setLocalPosition(pos);
    a->setLocalScale(scale);
    a->addComponent<BoundsOnly>();
    return a;
}

// 바닥(정적 상자, 윗면 y=0.5)과 그 위의 상자(동적, 반높이 0.5)를 만듭니다.
struct Stack {
    Actor*              floor  = nullptr;
    Actor*              box    = nullptr;
    RigidBodyComponent* floorRb = nullptr;
    RigidBodyComponent* boxRb   = nullptr;

    explicit Stack(World& w, float boxY) {
        floor = w.spawnActor("Floor");
        floorRb = floor->addComponent<RigidBodyComponent>();
        floorRb->motionType = EMotionType::Static;
        floorRb->halfExtent = { 5.f, 0.5f, 5.f };

        box = w.spawnActor("Box");
        box->setLocalPosition({ 0.f, boxY, 0.f });
        boxRb = box->addComponent<RigidBodyComponent>();
        boxRb->motionType = EMotionType::Dynamic;
    }
};

} // namespace

int main() {
    std::printf("== Physics body lifecycle ==\n");

    // 블록마다 월드와 물리 시스템을 새로 만듭니다. 월드 소멸은 컴포넌트 onDestroy 를 부르지
    // 않으므로, 물리를 먼저 shutdown 해(월드의 바디를 지움) 다음 블록에 바디가 남지 않게 합니다.

    // ── DEF-80 ────────────────────────────────────────────────────────────────
    {
        std::printf("\n-- DEF-80: 받치던 바닥을 끄면 위의 상자가 떨어짐 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        Stack s(w, 1.5f);
        run(ps, 360);   // 6 초: 떨어져 바닥에 닿고 잠들 시간
        PhysicsSystem::BodyInfo info;
        check(ps.queryBody(s.boxRb->bodyId_, info) && std::fabs(info.posY - 1.f) < 0.02f,
              "상자가 바닥 위(y=1)에 놓임");
        check(!info.active, "상자가 잠듦 (깨우기 확인의 전제)");

        s.floor->setActive(false);
        check(s.floorRb->bodyId_ == kNoBody, "바닥을 끄자 바디가 사라짐");
        run(ps, 30);
        const float y = s.box->transform().position().y;
        check(y < 0.5f, "잠들어 있던 상자가 깨어 떨어짐 (y=" + std::to_string(y) + ")");

        s.floor->setActive(true);
        run(ps, 1);
        check(s.floorRb->bodyId_ != kNoBody, "다시 켜면 바닥 바디가 새로 생김");
        ps.shutdown();
    }

    // ── DEF-43 · DEF-81 ───────────────────────────────────────────────────────
    {
        std::printf("\n-- DEF-43: 드래그 중에는 바디를 다시 만들지 않고 확정 때 한 번 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        Stack s(w, 1.0f);
        run(ps, 2);
        const uint32_t before = s.boxRb->bodyId_;
        check(before != kNoBody, "상자 바디가 만들어짐");

        for (int i = 1; i <= 10; ++i) {          // 숫자 칸 드래그: 세터가 연달아 불림
            setProp(s.boxRb, "HalfX", 0.5f + 0.01f * (float)i);
            run(ps, 1);
        }
        check(s.boxRb->bodyId_ == before, "드래그 동안 바디가 그대로 (재생성 0회)");

        s.boxRb->onPropsCommitted();             // 드래그 끝
        check(s.boxRb->bodyId_ == kNoBody, "확정하면 바디를 지우고 재생성을 요청");
        run(ps, 1);
        check(s.boxRb->bodyId_ != kNoBody, "다음 틱에 새 크기로 다시 만들어짐");

        std::printf("\n-- DEF-81: 마찰·반발은 재생성 없이 바로 반영 --\n");
        const uint32_t id = s.boxRb->bodyId_;
        setProp(s.boxRb, "Friction", 0.9f);
        setProp(s.boxRb, "Restitution", 0.1f);
        PhysicsSystem::BodyInfo info;
        check(ps.queryBody(id, info) && std::fabs(info.friction - 0.9f) < 1e-5f &&
              std::fabs(info.restitution - 0.1f) < 1e-5f, "살아 있는 바디의 마찰 0.9 · 반발 0.1");
        check(s.boxRb->bodyId_ == id, "마찰·반발 변경은 바디를 다시 만들지 않음");

        s.boxRb->onPropsCommitted();             // 마찰 칸 드래그 끝
        check(s.boxRb->bodyId_ == id, "마찰만 바꾼 확정은 바디를 다시 만들지 않음");

        // 프로퍼티 스냅샷 undo 는 모든 실수 세터를 다시 부릅니다(값은 그대로인 것도 포함).
        for (auto& pd : s.boxRb->buildProps())
            if (pd.setF && pd.getF) pd.setF(pd.getF());
        s.boxRb->onPropsCommitted();
        check(s.boxRb->bodyId_ == id, "값이 그대로인 세터 재호출(undo)은 바디를 다시 만들지 않음");

        setProp(s.boxRb, "Mass", 3.f);           // 질량을 끌었다가
        setProp(s.boxRb, "Mass", 1.f);           // 제자리로 돌림
        s.boxRb->onPropsCommitted();
        check(s.boxRb->bodyId_ == id, "제자리로 돌린 질량 드래그는 바디를 다시 만들지 않음");

        setProp(s.boxRb, "Mass", 5.f);
        s.boxRb->onPropsCommitted();
        check(s.boxRb->bodyId_ == kNoBody, "질량은 확정 때 재생성으로 반영");
        run(ps, 1);

        std::printf("\n-- 트랜스폼 확정: 위치는 옮기고, 스케일은 다시 만듦 --\n");
        // 정적 바닥을 옮기면 바디도 따라가야 합니다. 예전에는 플레이 중 기즈모로 옮겨도 남았습니다.
        const uint32_t floorId = s.floorRb->bodyId_;
        s.floor->setLocalPosition({ 0.f, -3.f, 0.f });
        s.floor->notifyTransformCommitted();
        check(s.floorRb->bodyId_ == floorId, "위치만 바뀌면 바디를 다시 만들지 않음");
        check(ps.queryBody(floorId, info) && std::fabs(info.posY + 3.f) < 1e-4f, "바닥 바디가 y=-3 으로 옮겨짐");

        s.floor->setLocalScale({ 2.f, 1.f, 2.f });
        s.floor->notifyTransformCommitted();
        check(s.floorRb->bodyId_ == kNoBody, "스케일이 바뀌면 재생성을 요청");
        run(ps, 1);
        check(s.floorRb->bodyId_ != kNoBody && s.floorRb->createdScale_.x == 2.f,
              "새 바디가 스케일 2 로 만들어짐");
        ps.shutdown();
    }

    // ── 트랜스폼 확정으로 옮긴 바닥 ────────────────────────────────────────────
    {
        std::printf("\n-- 받치던 바닥을 옮기면 위의 잠든 상자가 깨어 떨어짐 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        Stack s(w, 1.5f);
        run(ps, 360);
        PhysicsSystem::BodyInfo info;
        check(ps.queryBody(s.boxRb->bodyId_, info) && !info.active, "상자가 바닥 위에서 잠듦 (전제)");

        const uint32_t floorId = s.floorRb->bodyId_;
        s.floor->setLocalPosition({ 0.f, -2.f, 0.f });   // 플레이 중 기즈모로 바닥을 내림
        s.floor->notifyTransformCommitted();
        check(s.floorRb->bodyId_ == floorId, "바닥 바디는 다시 만들지 않고 옮김");
        run(ps, 30);
        const float y = s.box->transform().position().y;
        check(y < 0.9f, "잠들어 있던 상자가 깨어 떨어짐 (y=" + std::to_string(y) + ")");
        ps.shutdown();
    }

    // ── DEF-44 ────────────────────────────────────────────────────────────────
    {
        std::printf("\n-- DEF-44: 셰이프·모션 이름과 옛 순번 --\n");
        EPhysicsShape sh = EPhysicsShape::Box;
        EMotionType   mo = EMotionType::Dynamic;
        check(std::string(RigidBodyComponent::shapeName(EPhysicsShape::Capsule)) == "capsule" &&
              std::string(RigidBodyComponent::motionName(EMotionType::Kinematic)) == "kinematic",
              "이름으로 씀");
        check(RigidBodyComponent::shapeFromText("sphere", sh) && sh == EPhysicsShape::Sphere,
              "이름 sphere 를 읽음");
        check(RigidBodyComponent::motionFromText("static", mo) && mo == EMotionType::Static,
              "이름 static 을 읽음");
        check(RigidBodyComponent::shapeFromText("2", sh) && sh == EPhysicsShape::Capsule,
              "옛 순번 2 를 capsule 로 읽음");
        check(RigidBodyComponent::motionFromText("1", mo) && mo == EMotionType::Dynamic,
              "옛 순번 1 을 dynamic 으로 읽음");
        sh = EPhysicsShape::Sphere;
        check(!RigidBodyComponent::shapeFromText("7", sh) && sh == EPhysicsShape::Sphere,
              "범위 밖 순번 7 은 거부하고 값을 건드리지 않음");
        check(!RigidBodyComponent::shapeFromText("-1", sh) && !RigidBodyComponent::shapeFromText("", sh) &&
              !RigidBodyComponent::shapeFromText("Box", sh), "음수·빈 값·대문자 이름은 거부");
    }

    // ── DEF-19: 물리 API ──────────────────────────────────────────────────────
    // 무중력에서 질량 2kg 공 하나로 힘 · 충격량 · 속도 · 순간 이동을 확인합니다.
    auto makeBall = [](World& w, const glm::vec3& pos) {
        Actor* a = w.spawnActor("Ball");
        a->setLocalPosition(pos);
        auto* rb = a->addComponent<RigidBodyComponent>();
        rb->motionType = EMotionType::Dynamic;
        rb->shape      = EPhysicsShape::Sphere;
        rb->halfExtent = { 0.25f, 0.25f, 0.25f };
        rb->mass       = 2.f;
        return rb;
    };

    {
        std::printf("\n-- DEF-19: 중력 세터 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        RigidBodyComponent* rb = makeBall(w, { 0.f, 10.f, 0.f });
        ps.setGravity({ 0.f, 0.f, 0.f });
        run(ps, 60);
        check(std::fabs(rb->owner->worldPosition().y - 10.f) < 1e-4f, "중력 0 이면 1초 뒤에도 제자리");
        check(ps.gravity() == glm::vec3(0.f), "gravity() 가 설정한 값을 돌려줌");
        ps.setGravity({ 0.f, -9.81f, 0.f });
        run(ps, 30);
        check(rb->owner->worldPosition().y < 9.f, "중력을 되돌리면 떨어짐(그사이 잠든 바디도 깨움)");
        ps.shutdown();
    }

    {
        std::printf("\n-- DEF-19: 힘은 스텝 수와 상관없이 힘 x 프레임 시간 --\n");
        // 2N 을 한 프레임(0.02초)만 걸면 충격량 0.04 N·s, 2kg 이므로 속도 0.02 m/s 입니다. 물리 스텝이
        // 그 프레임에 한 번(60Hz) · 두 번(120Hz) · 0번(30Hz, 다음 프레임으로 넘어감)이어도 같아야 합니다.
        // 힘을 Jolt 에 그대로 넘기면 120Hz 에서 절반만 들어갑니다(Jolt 는 스텝 한 번 뒤에 힘을 지움).
        for (float hz : { 60.f, 120.f, 30.f }) {
            World w;
            PhysicsSystem ps;
            ps.startup();
            ps.setWorld(&w);
            ps.setGravity({ 0.f, 0.f, 0.f });
            ps.setStepRate(hz);
            RigidBodyComponent* rb = makeBall(w, { 0.f, 0.f, 0.f });
            ps.tick(0.02f);                       // 바디 생성
            rb->addForce({ 2.f, 0.f, 0.f });
            ps.tick(0.02f);
            for (int i = 0; i < 3 && rb->linearVelocity().x == 0.f; ++i) ps.tick(0.02f);   // 스텝 없던 프레임
            const float v = rb->linearVelocity().x;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "스텝 %.0fHz: 한 프레임 힘의 속도 변화 %.5f (기대 0.02000)", hz, v);
            check(std::fabs(v - 0.02f) < 1e-4f, buf);
            ps.shutdown();
        }
    }

    {
        std::printf("\n-- DEF-19: 충격량 · 속도 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        ps.setGravity({ 0.f, 0.f, 0.f });
        RigidBodyComponent* rb = makeBall(w, { 0.f, 0.f, 0.f });
        check(!rb->hasBody(), "물리 틱 전에는 바디가 없음(API 는 아무 일도 안 함)");
        rb->addImpulse({ 100.f, 0.f, 0.f });   // 바디가 없으니 무시
        ps.tick(kDt);
        check(rb->linearVelocity().x == 0.f, "바디가 생기기 전의 충격량은 버려짐");
        rb->addImpulse({ 2.f, 0.f, 0.f });
        check(std::fabs(rb->linearVelocity().x - 1.f) < 1e-5f, "충격량 2 N·s / 2kg = 1 m/s 가 바로 들어감");
        rb->setLinearVelocity({ 0.f, 0.f, 3.f });
        check(rb->linearVelocity() == glm::vec3(0.f, 0.f, 3.f), "setLinearVelocity · linearVelocity 왕복");
        rb->setAngularVelocity({ 0.f, 2.f, 0.f });
        check(std::fabs(rb->angularVelocity().y - 2.f) < 1e-5f, "setAngularVelocity · angularVelocity 왕복");
        ps.shutdown();
    }

    {
        std::printf("\n-- DEF-19: 게임 코드가 옮기면 순간 이동, 속도 유지 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        ps.setGravity({ 0.f, 0.f, 0.f });
        RigidBodyComponent* rb = makeBall(w, { 0.f, 0.f, 0.f });
        ps.tick(kDt);
        rb->setLinearVelocity({ 1.f, 0.f, 0.f });
        run(ps, 5);
        const float vBefore = rb->linearVelocity().x;   // Jolt 기본 감쇠(0.05/s)로 1 보다 조금 작음
        rb->owner->setLocalPosition({ 10.f, 0.f, 0.f });   // 게임 코드
        ps.tick(kDt);
        const float x = rb->owner->worldPosition().x;
        check(x > 10.f && x < 10.1f, "액터를 옮긴 자리에서 계속 움직임(예전에는 물리가 덮어써 옮긴 값이 사라짐)");
        check(vBefore > 0.9f && std::fabs(rb->linearVelocity().x - vBefore) < 1e-3f, "순간 이동 뒤에도 속도 유지");
        ps.shutdown();
    }

    {
        std::printf("\n-- DEF-99: Kinematic 은 같은 프레임에 목표에 닿고 지나치지 않음 --\n");
        // 스텝 120Hz · 프레임 0.02초면 한 프레임에 스텝이 두 번입니다. 예전에는 스텝 루프 뒤에 옮겨
        // 한 프레임 늦었고, 다음 프레임에는 두 스텝 모두 그 속도로 움직여 목표를 지나쳤습니다.
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        ps.setStepRate(120.f);
        Actor* a = w.spawnActor("Hand");
        auto* rb = a->addComponent<RigidBodyComponent>();
        rb->motionType = EMotionType::Kinematic;
        ps.tick(0.02f);   // 바디 생성
        a->setLocalPosition({ 1.f, 0.f, 0.f });
        ps.tick(0.02f);
        PhysicsSystem::BodyInfo info;
        ps.queryBody(rb->bodyId_, info);
        check(std::fabs(info.position.x - 1.f) < 1e-4f, "액터를 옮긴 그 프레임에 바디가 목표에 닿음");
        ps.tick(0.02f);
        ps.tick(0.02f);
        ps.queryBody(rb->bodyId_, info);
        check(std::fabs(info.position.x - 1.f) < 1e-4f, "목표가 그대로면 지나치지 않고 멈춤");
        ps.shutdown();
    }

    {
        std::printf("\n-- DEF-53: 부모 아래 Dynamic 바디는 월드 자세를 유지 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        ps.setGravity({ 0.f, 0.f, 0.f });
        Actor* parent = w.spawnActor("Parent");
        parent->setLocalPosition({ 5.f, 0.f, 0.f });
        parent->setLocalRotation(glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f)));
        RigidBodyComponent* rb = makeBall(w, { 0.f, 0.f, 2.f });
        rb->owner->attachTo(parent);   // 월드 (7, 0, 0)
        ps.tick(kDt);
        rb->setLinearVelocity({ 0.f, 1.f, 0.f });
        run(ps, 30);
        PhysicsSystem::BodyInfo info;
        ps.queryBody(rb->bodyId_, info);
        const glm::vec3 wp = rb->owner->worldPosition();
        check(glm::length(wp - info.position) < 1e-4f, "회전한 부모 아래 액터의 월드 위치 = 바디 위치(예전에는 로컬에 월드를 씀)");
        check(std::fabs(wp.x - 7.f) < 1e-3f && wp.y > 0.4f, "바디는 월드 (7, y, 0) 에서 위로 움직임");
        parent->setLocalPosition({ 5.f, 0.f, 10.f });   // 부모를 옮겨도
        ps.tick(kDt);
        ps.queryBody(rb->bodyId_, info);
        check(std::fabs(info.position.z) < 1e-3f, "부모가 움직여도 바디는 끌려가지 않음(UE 같음)");
        check(glm::length(rb->owner->worldPosition() - info.position) < 1e-4f, "액터는 바디 자리에 남음");
        ps.shutdown();
    }

    {
        std::printf("\n-- DEF-19: 레이캐스트 · 구 겹침 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        Actor* floor = w.spawnActor("Floor");
        auto* frb = floor->addComponent<RigidBodyComponent>();
        frb->motionType = EMotionType::Static;
        frb->halfExtent = { 5.f, 0.5f, 5.f };   // 윗면 y = 0.5
        ps.tick(kDt);
        PhysicsSystem::RaycastHit hit;
        const bool got = ps.raycast({ 1.f, 5.f, 1.f }, { 0.f, -2.f, 0.f }, 10.f, hit);
        check(got && hit.actor == floor, "아래로 쏜 레이가 바닥 액터에 맞음");
        check(got && std::fabs(hit.distance - 4.5f) < 1e-3f && std::fabs(hit.point.y - 0.5f) < 1e-3f,
              "맞은 거리 4.5 · 지점 y 0.5");
        check(got && glm::length(hit.normal - glm::vec3(0.f, 1.f, 0.f)) < 1e-3f, "법선이 위(+Y)");
        check(!ps.raycast({ 1.f, 5.f, 1.f }, { 0.f, -1.f, 0.f }, 3.f, hit), "최대 거리 안에 없으면 맞지 않음");
        std::vector<Actor*> found;
        ps.overlapSphere({ 0.f, 0.5f, 0.f }, 0.1f, found);
        check(found.size() == 1 && found[0] == floor, "바닥 윗면에 걸친 구는 바닥과 겹침");
        ps.overlapSphere({ 0.f, 3.f, 0.f }, 0.1f, found);
        check(found.empty(), "공중의 구는 아무것과도 겹치지 않음");
        ps.shutdown();
    }

    {
        // 소리 차폐 준비(2026-11-06): 청취자 → 음원 레이가 음원 자신 · 청취자 리그의 충돌체에 맞으면 모든 소리가 막힌 것으로
        // 나옵니다. 특히 볼록 셰이프 안에서 출발한 레이는 그 셰이프에 거리 0 으로 맞습니다.
        std::printf("\n-- 레이캐스트에서 액터 빼기 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        auto box = [&](const char* name, glm::vec3 pos) {
            Actor* a = w.spawnActor(name);
            a->setLocalPosition(pos);
            auto* rb = a->addComponent<RigidBodyComponent>();
            rb->motionType = EMotionType::Static;
            rb->halfExtent = { 0.5f, 0.5f, 0.5f };
            return a;
        };
        Actor* nearBox = box("Near", { 0.f, 0.f, -3.f });   // 앞면 z = -2.5
        Actor* farBox  = box("Far",  { 0.f, 0.f, -8.f });   // 앞면 z = -7.5
        ps.tick(kDt);
        PhysicsSystem::RaycastHit hit;
        const glm::vec3 o{ 0.f }, fwd{ 0.f, 0.f, -1.f };
        bool got = ps.raycast(o, fwd, 20.f, hit);
        check(got && hit.actor == nearBox && std::fabs(hit.distance - 2.5f) < 1e-3f, "빼지 않으면 가까운 상자(거리 2.5)");
        Actor* ignoreNear[] = { nearBox };
        got = ps.raycast(o, fwd, 20.f, hit, ignoreNear);
        check(got && hit.actor == farBox && std::fabs(hit.distance - 7.5f) < 1e-3f, "가까운 상자를 빼면 그 너머 상자(거리 7.5)");
        Actor* ignoreBoth[] = { nearBox, farBox };
        check(!ps.raycast(o, fwd, 20.f, hit, ignoreBoth) && hit.actor == nullptr, "둘 다 빼면 맞지 않음");
        Actor* withNull[] = { nullptr, nearBox };
        check(ps.raycast(o, fwd, 20.f, hit, withNull) && hit.actor == farBox, "nullptr 항목은 건너뜀");
        std::vector<Actor*> asVector{ nearBox };
        check(ps.raycast(o, fwd, 20.f, hit, asVector) && hit.actor == farBox, "std::vector 로도 넘길 수 있음");

        const glm::vec3 inside{ 0.f, 0.f, -3.f };   // 가까운 상자의 가운데(음원이 자기 충돌체 안에 있는 경우)
        got = ps.raycast(inside, fwd, 20.f, hit);
        check(got && hit.actor == nearBox && hit.distance < 1e-4f, "상자 안에서 쏘면 그 상자에 거리 0 으로 맞음(Jolt 기본)");
        got = ps.raycast(inside, fwd, 20.f, hit, ignoreNear);
        check(got && hit.actor == farBox && std::fabs(hit.distance - 4.5f) < 1e-3f, "출발점을 감싼 상자를 빼면 그 너머를 봄(거리 4.5)");
        ps.shutdown();
    }

    // ── 메시 충돌체(수학 보강 P2): 볼록 껍질 · 오목 메시 ─────────────────────────────
    {
        std::printf("\n-- P2: 볼록 껍질 · 메시 충돌체 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        ps.setGravity({ 0.f, 0.f, 0.f });
        // 높이가 다른 두 사각형: x [0,1] 은 y 0, x [2,3] 은 y 1. 가운데 x [1,2] 는 비어 있음(오목).
        auto twoSteps = [&](const char* name, EPhysicsShape shape, EMotionType motion, float xOffset) {
            Actor* a = w.spawnActor(name);
            a->setLocalPosition({ xOffset, 0.f, 0.f });
            a->setLocalScale({ 1.f, 2.f, 1.f });   // 비균등 크기: 높은 판이 y 2 로
            auto* probe = a->addComponent<MeshProbe>();
            probe->addQuad(0.f, 1.f, 0.f);
            probe->addQuad(2.f, 3.f, 1.f);
            auto* rb = a->addComponent<RigidBodyComponent>();
            rb->shape      = shape;
            rb->motionType = motion;
            return rb;
        };
        RigidBodyComponent* meshRb = twoSteps("MeshSteps", EPhysicsShape::Mesh, EMotionType::Static, 0.f);
        RigidBodyComponent* hullRb = twoSteps("HullSteps", EPhysicsShape::ConvexHull, EMotionType::Static, 10.f);
        RigidBodyComponent* dynRb  = twoSteps("DynMesh", EPhysicsShape::Mesh, EMotionType::Dynamic, 20.f);
        Actor* bare = w.spawnActor("NoMesh");
        bare->setLocalPosition({ 30.f, 0.f, 0.f });
        auto* bareRb = bare->addComponent<RigidBodyComponent>();
        bareRb->shape = EPhysicsShape::ConvexHull;
        run(ps, 1);
        check(meshRb->hasBody() && hullRb->hasBody() && dynRb->hasBody(), "메시 형상으로 Mesh · ConvexHull 바디를 만듦");
        check(!bareRb->hasBody(), "메시 형상이 없는 액터의 ConvexHull 은 바디를 만들지 않음(경고)");

        auto downHit = [&](float x, float& y) {   // 위(y 10)에서 아래로 쏜 광선이 맞은 높이
            PhysicsSystem::RaycastHit h;
            if (!ps.raycast({ x, 10.f, 0.5f }, { 0.f, -1.f, 0.f }, 20.f, h)) return false;
            y = h.point.y;
            return true;
        };
        float y = 0.f;
        check(downHit(0.5f, y) && std::fabs(y - 0.f) < 1e-3f, "Mesh: 낮은 판(x 0.5)은 y 0");
        check(downHit(2.5f, y) && std::fabs(y - 2.f) < 1e-3f, "Mesh: 높은 판은 비균등 크기(y × 2)로 y 2");
        check(!downHit(1.5f, y), "Mesh: 가운데 빈 곳(x 1.5)은 뚫려 있음(오목)");
        check(downHit(11.5f, y) && y > 0.5f && y < 2.f + 1e-3f, "ConvexHull: 같은 곳(x 11.5)은 껍질 경사면에 맞음");
        check(!downHit(13.5f, y), "ConvexHull: 껍질 밖(x 13.5)은 빈 곳(충돌체가 원점 중심이 아니라 메시 자리에 있음)");
        check(downHit(21.5f, y) && y > 0.5f, "Dynamic 인 Mesh 는 볼록 껍질로 대신 만듦(가운데도 막힘)");
        ps.shutdown();

        RigidBodyComponent src;
        src.shape = EPhysicsShape::ConvexHull;
        ComponentFieldWriter fw;
        src.writeFields(fw);
        RigidBodyComponent dst;
        for (const auto& kv : fw.fields()) dst.readField(kv.first, kv.second);
        EPhysicsShape m = EPhysicsShape::Box;
        check(dst.shape == EPhysicsShape::ConvexHull && RigidBodyComponent::shapeFromText("mesh", m) && m == EPhysicsShape::Mesh,
              "셰이프 이름 convex · mesh 왕복");
    }

    // ── 연속 충돌(수학 보강 P1): 빠르고 작은 공이 얇은 벽을 뚫지 않음 ──────────────────
    {
        std::printf("\n-- P1: 연속 충돌(LinearCast) --\n");
        auto shoot = [](bool continuous, float& outX, bool& outFlag) {
            World w;
            PhysicsSystem ps;
            ps.startup();
            ps.setWorld(&w);
            ps.setGravity({ 0.f, 0.f, 0.f });
            ps.setStepRate(90.f);
            Actor* wall = w.spawnActor("Wall");   // 두께 2 cm 벽(x = 0.85): 스텝 끝 자리 0.667 · 1.0 사이라 겹치는 순간이 없음
            wall->setLocalPosition({ 0.85f, 0.f, 0.f });
            wall->setLocalScale({ 0.02f, 2.f, 2.f });
            wall->addComponent<RigidBodyComponent>()->motionType = EMotionType::Static;
            Actor* ball = w.spawnActor("Ball");   // 지름 5 cm 공
            ball->setLocalScale({ 0.05f, 0.05f, 0.05f });
            auto* rb = ball->addComponent<RigidBodyComponent>();
            rb->shape      = EPhysicsShape::Sphere;
            rb->motionType = EMotionType::Dynamic;
            rb->continuous = continuous;
            run(ps, 1);
            outFlag = ps.isContinuous(rb->bodyId_);
            rb->setLinearVelocity({ 30.f, 0.f, 0.f });   // 스텝(1/90 s)당 33 cm
            run(ps, 12);                                  // 0.2 초
            outX = ball->worldPosition().x;
            ps.shutdown();
        };
        float xd = 0.f, xc = 0.f;
        bool  fd = true, fc = false;
        shoot(false, xd, fd);
        shoot(true, xc, fc);
        std::printf("  (참고) 끔: 공 x = %.3f m, 켬: 공 x = %.3f m\n", xd, xc);
        check(!fd && fc, "continuous 값대로 바디의 연속 충돌이 정해짐");
        check(xd > 1.5f, "(준비) 연속 충돌을 끄면 30 m/s 공이 2 cm 벽을 뚫음");
        check(xc < 0.85f, "연속 충돌을 켜면 벽 앞에서 막힘");
    }

    // ── P3: 충돌체 중심 오프셋 ──────────────────────────────────────────────────────
    {
        std::printf("\n-- P3: 충돌체 중심 오프셋 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        Actor* a = w.spawnActor("Offset");
        a->setLocalScale({ 2.f, 2.f, 2.f });
        auto* rb = a->addComponent<RigidBodyComponent>();
        rb->motionType = EMotionType::Static;
        rb->center     = { 0.f, 2.f, 0.f };   // 로컬 2 m 위 × 크기 2 = 월드 4 m 위, 반높이 0.5 × 2 = 1
        run(ps, 1);
        PhysicsSystem::RaycastHit hit;
        const bool got = ps.raycast({ 0.f, 10.f, 0.f }, { 0.f, -1.f, 0.f }, 20.f, hit);
        check(got && std::fabs(hit.point.y - 5.f) < 1e-3f, "오프셋 상자 윗면이 y = 5(중심 × 크기 + 반높이 × 크기)");
        check(std::fabs(a->worldPosition().y) < 1e-6f, "액터는 제자리(오프셋은 충돌체만 옮김)");
        // 편집으로 오프셋을 바꾸면 바디를 다시 만듦
        const uint32_t before = rb->bodyId_;
        for (auto& pd : rb->buildProps())
            if (std::string(pd.label) == "CenterY" && pd.setF) pd.setF(0.f);
        rb->onPropsCommitted();
        run(ps, 1);
        PhysicsSystem::RaycastHit hit2;
        ps.raycast({ 0.f, 10.f, 0.f }, { 0.f, -1.f, 0.f }, 20.f, hit2);
        check(rb->hasBody() && std::fabs(hit2.point.y - 1.f) < 1e-3f, "오프셋을 0 으로 바꾸면 다시 만들어 윗면 y = 1");
        (void)before;
        Actor* sph = w.spawnActor("OffsetSphere");
        sph->setLocalPosition({ 5.f, 0.f, 0.f });
        auto* rs = sph->addComponent<RigidBodyComponent>();
        rs->shape = EPhysicsShape::Sphere;
        rs->motionType = EMotionType::Static;
        rs->center = { 0.f, 0.f, -3.f };
        run(ps, 1);
        PhysicsSystem::RaycastHit hs;
        check(ps.raycast({ 5.f, 10.f, -3.f }, { 0.f, -1.f, 0.f }, 20.f, hs) && std::fabs(hs.point.y - 0.5f) < 1e-3f,
              "구도 오프셋 자리(z = −3)에 있음");
        ComponentFieldWriter fw;
        rs->interpolate = true;
        rs->writeFields(fw);
        RigidBodyComponent back;
        for (const auto& kv : fw.fields()) back.readField(kv.first, kv.second);
        check(back.center == glm::vec3(0.f, 0.f, -3.f) && back.interpolate, "center · interpolate 직렬화 왕복");
        RigidBodyComponent plain;
        ComponentFieldWriter fw2;
        plain.writeFields(fw2);
        bool extra = false;
        for (const auto& kv : fw2.fields()) extra = extra || kv.first == "center" || kv.first == "interpolate";
        check(!extra, "기본값(오프셋 0 · 보간 끔)이면 쓰지 않음");
        RigidBodyComponent bad;
        check(bad.readField("center", "1 nan 2") && bad.center == glm::vec3(0.f), "깨진 오프셋은 기본값 유지");
        ps.shutdown();
    }

    // ── P4: 동적 바디 렌더 보간 ─────────────────────────────────────────────────────
    {
        std::printf("\n-- P4: 렌더 보간 --\n");
        auto slide = [](bool interp, std::vector<float>& xs, float& teleX) {
            World w;
            PhysicsSystem ps;
            ps.startup();
            ps.setWorld(&w);
            ps.setGravity({ 0.f, 0.f, 0.f });
            ps.setStepRate(60.f);
            Actor* b = w.spawnActor("Slider");
            b->setLocalScale({ 0.2f, 0.2f, 0.2f });
            auto* rb = b->addComponent<RigidBodyComponent>();
            rb->motionType  = EMotionType::Dynamic;
            rb->interpolate = interp;
            ps.tick(1.f / 60.f);
            rb->setLinearVelocity({ 6.f, 0.f, 0.f });
            for (int i = 0; i < 30; ++i) { ps.tick(1.f / 180.f); xs.push_back(b->worldPosition().x); }   // 화면 180 Hz, 물리 60 Hz
            b->setLocalPosition({ 5.f, 0.f, 0.f });   // 게임 코드가 옮김(순간 이동)
            ps.tick(1.f / 180.f);
            teleX = b->worldPosition().x;
            ps.shutdown();
        };
        std::vector<float> xi, xn;
        float ti = 0.f, tn = 0.f;
        slide(true, xi, ti);
        slide(false, xn, tn);
        int still = 0;
        for (size_t i = 1; i < xn.size(); ++i) still += std::fabs(xn[i] - xn[i - 1]) < 1e-6f;
        bool even = true;
        for (size_t i = 6; i < xi.size(); ++i) {
            const float d = xi[i] - xi[i - 1];
            even = even && d > 0.02f && d < 0.047f;   // 6 m/s ÷ 180 Hz = 0.033 m
        }
        std::printf("  (참고) 보간 끔: 제자리 틱 %d / %zu\n", still, xn.size() - 1);
        check(still >= 15, "(준비) 보간을 끄면 물리 스텝 사이 틱에서 물체가 멈춤(계단)");
        check(even, "보간을 켜면 틱마다 고르게 움직임");
        check(xi.back() <= xn.back() + 1e-5f && xn.back() - xi.back() < 0.11f, "보간한 자리는 바디보다 한 스텝 안쪽 뒤");
        check(std::fabs(ti - 5.f) < 0.12f, "순간 이동은 보간하지 않고 바로 그 자리");
        (void)tn;
    }

    // ── P5: 속도 추적 잡기 ──────────────────────────────────────────────────────────
    {
        std::printf("\n-- P5: 속도 추적 잡기 --\n");
        glm::vec3 lin, ang;
        GrabInteractorComponent::trackingVelocity(glm::vec3(0.f), glm::quat(1.f, 0.f, 0.f, 0.f), { 0.1f, 0.f, 0.f },
                                                  glm::quat(1.f, 0.f, 0.f, 0.f), 0.01f, lin, ang);
        check(glm::length(lin - glm::vec3(10.f, 0.f, 0.f)) < 1e-4f && glm::length(ang) < 1e-6f, "목표까지 10 cm, dt 10 ms → 10 m/s");
        GrabInteractorComponent::trackingVelocity(glm::vec3(0.f), glm::quat(1.f, 0.f, 0.f, 0.f), { 5.f, 0.f, 0.f },
                                                  glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f)), 0.1f, lin, ang);
        check(std::fabs(glm::length(lin) - GrabInteractorComponent::kMaxTrackSpeed) < 1e-3f, "먼 목표는 상한 속도로 자름");
        check(glm::length(ang - glm::vec3(0.f, glm::radians(90.f) / 0.1f, 0.f)) < 1e-3f, "90° 를 0.1 s 에 → y 축 15.7 rad/s");

        auto carry = [](bool tracking, float& endX, float& gravityDrop) {
            World w;
            PhysicsSystem ps;
            ps.startup();
            ps.setWorld(&w);
            ps.setStepRate(90.f);
            Actor* wall = w.spawnActor("Wall");   // x = 1 의 벽(두께 10 cm)
            wall->setLocalPosition({ 1.f, 1.f, 0.f });
            wall->setLocalScale({ 0.1f, 2.f, 2.f });
            wall->addComponent<RigidBodyComponent>()->motionType = EMotionType::Static;
            Actor* cube = w.spawnActor("Held");
            cube->setLocalPosition({ 0.f, 1.f, 0.f });
            cube->setLocalScale({ 0.1f, 0.1f, 0.1f });
            auto* rb = cube->addComponent<RigidBodyComponent>();
            rb->motionType = EMotionType::Dynamic;
            rb->mass = 0.3f;
            auto* g = cube->addComponent<GrabbableComponent>();
            g->velocityTracking = tracking;
            Actor* handA = w.spawnActor("Hand");
            auto* ia = handA->addComponent<GrabInteractorComponent>();
            ps.tick(1.f / 90.f);
            const glm::quat hr(1.f, 0.f, 0.f, 0.f);
            glm::vec3 hand(0.f, 1.f, 0.f);
            ia->grab(g, hand, hr);
            // 손을 가만히 둔 0.5 초: 속도 추적이면 중력이 꺼져 처지지 않아야 함
            for (int i = 0; i < 45; ++i) { ia->follow(hand, hr, 1.f / 90.f); ps.tick(1.f / 90.f); }
            gravityDrop = 1.f - cube->worldPosition().y;
            // 손을 벽 너머(x = 2)로 1 초에 걸쳐 옮김
            for (int i = 0; i < 90; ++i) {
                hand.x = 2.f * float(i + 1) / 90.f;
                ia->follow(hand, hr, 1.f / 90.f);
                ps.tick(1.f / 90.f);
            }
            endX = cube->worldPosition().x;
            ia->release(glm::vec3(0.f), glm::vec3(0.f), false);
            ps.shutdown();
        };
        float xt = 0.f, xk = 0.f, dt0 = 0.f, dk = 0.f;
        carry(true, xt, dt0);
        carry(false, xk, dk);
        std::printf("  (참고) 속도 추적 x = %.3f, Kinematic x = %.3f, 처짐 %.4f m\n", xt, xk, dt0);
        check(xk > 1.9f, "(준비) Kinematic 잡기는 벽을 뚫고 손을 따라감");
        check(xt < 0.95f, "속도 추적 잡기는 벽에 막힘");
        check(std::fabs(dt0) < 0.01f, "속도 추적 중에는 중력으로 처지지 않음");
    }

    // ── 직렬화(DEF-22): 이름 붙은 값의 왕복 ─────────────────────────────────────
    {
        std::printf("\n-- DEF-22: RigidBody 값이 이름으로 왕복 --\n");
        RigidBodyComponent src;
        src.shape = EPhysicsShape::Capsule; src.motionType = EMotionType::Kinematic;
        src.friction = 0.125f; src.restitution = 0.75f; src.mass = 12.5f; src.halfExtent = { 0.3f, 0.9f, 0.3f };
        ComponentFieldWriter w;
        src.writeFields(w);
        RigidBodyComponent dst;
        bool allKnown = true;
        for (const auto& kv : w.fields()) allKnown = allKnown && dst.readField(kv.first, kv.second);
        check(allKnown, "쓴 이름을 모두 다시 읽음");
        check(dst.shape == EPhysicsShape::Capsule && dst.motionType == EMotionType::Kinematic &&
              dst.friction == 0.125f && dst.restitution == 0.75f && dst.mass == 12.5f &&
              dst.halfExtent.x == 0.3f && dst.halfExtent.y == 0.9f && dst.halfExtent.z == 0.3f,
              "셰이프 · 모션 · 마찰 · 반발 · 질량 · 크기가 그대로");
        bool named = false;
        for (const auto& kv : w.fields()) if (kv.first == "shape") named = (kv.second == "capsule");
        check(named, "셰이프는 이름(capsule)으로 씀");
        bool hasCont = false;
        for (const auto& kv : w.fields()) hasCont = hasCont || kv.first == "continuous";
        check(!hasCont, "연속 충돌이 기본값(끔)이면 쓰지 않음(기존 씬 파일 그대로)");
        src.continuous = true;
        ComponentFieldWriter w2;
        src.writeFields(w2);
        RigidBodyComponent dst2;
        for (const auto& kv : w2.fields()) dst2.readField(kv.first, kv.second);
        check(dst2.continuous, "연속 충돌을 켜면 continuous 로 왕복");
        RigidBodyComponent legacy;
        check(legacy.readField("motion", "2") && legacy.motionType == EMotionType::Kinematic, "옛 순번(2)도 받음");
        check(legacy.readField("mass", "heavy") && legacy.mass == 1.f, "깨진 값은 기본값 유지");
        check(!legacy.readField("bounciness", "1"), "모르는 이름은 false");
    }

    // ── DEF-107 ───────────────────────────────────────────────────────────────
    {
        std::printf("\n-- DEF-107: 얇은 상자 · 크기 0 콜라이더 --\n");
        // 예전에는 Jolt 기본 모서리 반경(5cm)보다 얇은 상자를 만들지 못했고, 실패한 결과를 그대로 써서
        // 엔진이 죽었습니다. 이 블록이 끝까지 돌면(죽지 않으면) 실패 처리가 있는 것입니다.
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);

        Actor* slab = w.spawnActor("Slab");               // 두께 10cm 바닥(윗면 y=0.05)
        slab->setLocalScale({ 4.f, 0.1f, 4.f });
        auto* slabRb = slab->addComponent<RigidBodyComponent>();
        slabRb->motionType = EMotionType::Static;

        Actor* sheet = w.spawnActor("Sheet");             // 두께 1cm 판
        sheet->setLocalPosition({ 10.f, 0.f, 0.f });
        sheet->setLocalScale({ 1.f, 0.02f, 1.f });
        auto* sheetRb = sheet->addComponent<RigidBodyComponent>();
        sheetRb->motionType = EMotionType::Static;

        Actor* ball = w.spawnActor("Ball");               // 반지름 0.2 구
        ball->setLocalPosition({ 0.f, 1.f, 0.f });
        ball->setLocalScale({ 0.4f, 0.4f, 0.4f });
        auto* ballRb = ball->addComponent<RigidBodyComponent>();
        ballRb->shape      = EPhysicsShape::Sphere;
        ballRb->motionType = EMotionType::Dynamic;

        Actor* flat = w.spawnActor("Flat");               // 두께 0: 만들 수 없음
        flat->setLocalPosition({ -10.f, 0.f, 0.f });
        flat->setLocalScale({ 1.f, 0.f, 1.f });
        auto* flatRb = flat->addComponent<RigidBodyComponent>();
        flatRb->motionType = EMotionType::Static;

        Actor* dot = w.spawnActor("Dot");                 // 반지름 0 구
        dot->setLocalPosition({ 0.f, 0.f, 10.f });
        dot->setLocalScale({ 0.f, 0.f, 0.f });
        auto* dotRb = dot->addComponent<RigidBodyComponent>();
        dotRb->shape      = EPhysicsShape::Sphere;
        dotRb->motionType = EMotionType::Dynamic;

        run(ps, 240);   // 4 초
        check(slabRb->bodyId_ != kNoBody && !slabRb->createFailed_, "두께 10cm 상자에 바디가 생김");
        check(sheetRb->bodyId_ != kNoBody && !sheetRb->createFailed_, "두께 1cm 상자에 바디가 생김");
        const float y = ball->transform().position().y;
        check(std::fabs(y - 0.25f) < 0.02f, "구가 얇은 바닥 위(y=0.25)에 놓임 (y=" + std::to_string(y) + ")");
        check(flatRb->bodyId_ == kNoBody && flatRb->createFailed_, "두께 0 상자는 바디 없이 실패로 표시");
        check(dotRb->bodyId_ == kNoBody && dotRb->createFailed_, "반지름 0 구는 바디 없이 실패로 표시");
        ps.shutdown();
    }

    // ── 잡기 · 던지기 (2026-10-15) ────────────────────────────────────────────
    {
        std::printf("\n-- 잡기 · 던지기: Kinematic 으로 손을 따라가고, 놓으면 Dynamic + 손 속도 --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        ps.setGravity({ 0.f, 0.f, 0.f });   // 던진 속도만 보려고 중력을 끕니다

        Actor* box = w.spawnActor("Box");
        box->setLocalPosition({ 0.f, 1.f, 0.f });
        box->setLocalScale({ 0.2f, 0.2f, 0.2f });
        auto* rb = box->addComponent<RigidBodyComponent>();
        rb->motionType = EMotionType::Dynamic;
        auto* g = box->addComponent<GrabbableComponent>();

        Actor* far = w.spawnActor("Far");
        far->setLocalPosition({ 5.f, 1.f, 0.f });
        far->addComponent<GrabbableComponent>();

        Actor* handA = w.spawnActor("HandA");
        auto*  ia    = handA->addComponent<GrabInteractorComponent>();
        Actor* handB = w.spawnActor("HandB");
        auto*  ib    = handB->addComponent<GrabInteractorComponent>();
        run(ps, 2);
        const uint32_t body = rb->bodyId_;
        check(body != kNoBody, "(준비) 바디가 생김");

        check(GrabInteractorComponent::pick({ 0.05f, 1.f, 0.f }, 0.12f) == g, "반지름 안의 가장 가까운 것을 고름");
        check(GrabInteractorComponent::pick({ 2.5f, 1.f, 0.f }, 0.12f) == nullptr, "반지름 밖이면 없음");

        const glm::vec3 hp{ 0.f, 1.2f, 0.f };
        const glm::quat hr(1.f, 0.f, 0.f, 0.f);
        ia->grab(g, hp, hr);
        check(rb->motionType == EMotionType::Kinematic && rb->bodyId_ == body && g->holder() == ia,
              "잡으면 같은 바디가 Kinematic 이 됨(다시 만들지 않음)");
        rb->onPropsCommitted();   // Play 중 디테일 패널에서 다른 값을 확정한 경우
        check(rb->bodyId_ == body, "잡은 동안 속성을 확정해도 바디를 다시 만들지 않음");
        // 손을 x 로 1m 옮기고 90° 돌림: 물체는 손 기준 자리(손 아래 0.2m)를 지킵니다.
        const glm::quat hr2 = glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 0.f, 1.f));
        ia->follow({ 1.f, 1.2f, 0.f }, hr2);
        const glm::vec3 want = glm::vec3(1.f, 1.2f, 0.f) + hr2 * glm::vec3(0.f, -0.2f, 0.f);
        check(glm::length(box->worldPosition() - want) < 1e-4f, "손을 따라 손 기준 자리로 옮김");
        run(ps, 1);
        PhysicsSystem::BodyInfo info;
        check(ps.queryBody(body, info) && glm::length(info.position - want) < 1e-3f, "Kinematic 바디가 그 자리로 감");

        ib->grab(g, { 1.f, 1.2f, 0.f }, hr);
        check(g->holder() == ib && ia->held() == nullptr, "다른 손이 잡으면 넘겨받음");

        ib->release({ 3.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }, true);
        check(rb->motionType == EMotionType::Dynamic && rb->bodyId_ == body && g->holder() == nullptr,
              "놓으면 같은 바디가 Dynamic 으로 돌아옴");
        check(std::fabs(rb->linearVelocity().x - 3.f) < 1e-3f, "놓는 순간 손 속도를 받음");
        const float x0 = box->worldPosition().x;
        run(ps, 30);   // 0.5 초
        const float dx = box->worldPosition().x - x0;
        check(std::fabs(dx - 1.5f) < 0.05f, "던진 속도로 날아감 (0.5초에 " + std::to_string(dx) + " m)");

        // 강체가 없는 소품: 따라가다 놓은 자리에 남습니다.
        ia->grab(far->getComponent<GrabbableComponent>(), { 5.f, 1.f, 0.f }, hr);
        ia->follow({ 6.f, 1.f, 0.f }, hr);
        ia->release({ 9.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }, true);
        check(std::fabs(far->worldPosition().x - 6.f) < 1e-5f, "강체 없는 소품은 놓은 자리에 남음");

        // 잡은 액터를 지우면 손이 놓습니다(해제된 컴포넌트를 가리키지 않음).
        ia->grab(g, box->worldPosition(), hr);
        w.destroyActor(box);
        check(ia->held() == nullptr, "잡은 액터가 사라지면 손이 비워짐");
        ia->follow({ 0.f, 0.f, 0.f }, hr);   // 아무 일도 없어야 함

        // 잡힌 동안 잡기 컴포넌트를 끄면 손이 놓고, 강체는 Dynamic 으로 돌아옵니다.
        Actor* box2 = w.spawnActor("Box2");
        box2->setLocalPosition({ 0.f, 3.f, 0.f });
        auto* rb2 = box2->addComponent<RigidBodyComponent>();
        rb2->motionType = EMotionType::Dynamic;
        auto* g2 = box2->addComponent<GrabbableComponent>();
        run(ps, 1);
        ia->grab(g2, box2->worldPosition(), hr);
        g2->setEnabled(false);
        check(ia->held() == nullptr && rb2->motionType == EMotionType::Dynamic, "잡힌 것을 끄면 손이 놓고 Dynamic 으로 돌아옴");

        // 던지지 않고 놓으면 잡은 동안 Kinematic 바디가 가졌던 손 속도가 남지 않습니다(DEF-143).
        Actor* box3 = w.spawnActor("Box3");
        box3->setLocalPosition({ 0.f, 6.f, 0.f });
        auto* rb3 = box3->addComponent<RigidBodyComponent>();
        rb3->motionType = EMotionType::Dynamic;
        auto* g3 = box3->addComponent<GrabbableComponent>();
        g3->throwable = false;
        Actor* box4 = w.spawnActor("Box4");
        box4->setLocalPosition({ 3.f, 6.f, 0.f });
        auto* rb4 = box4->addComponent<RigidBodyComponent>();
        rb4->motionType = EMotionType::Dynamic;
        auto* g4 = box4->addComponent<GrabbableComponent>();
        run(ps, 1);
        auto swing = [&](GrabbableComponent* gg, Actor* a) {   // 잡고 한 틱 동안 손을 1m 옮김
            ia->grab(gg, a->worldPosition(), hr);
            ia->follow(a->worldPosition() + glm::vec3(1.f, 0.f, 0.f), hr);
            run(ps, 1);
        };
        swing(g3, box3);
        check(glm::length(rb3->linearVelocity()) > 1.f, "(준비) 잡은 동안 Kinematic 바디가 손을 따라가는 속도를 가짐");
        ia->release({ 3.f, 0.f, 0.f }, { 0.f, 0.f, 4.f }, true);
        check(rb3->motionType == EMotionType::Dynamic && glm::length(rb3->linearVelocity()) < 1e-5f &&
                  glm::length(rb3->angularVelocity()) < 1e-5f,
              "throwable=false 면 놓아도 손 속도 · 남은 Kinematic 속도 없이 멈춤");
        const glm::vec3 p3 = box3->worldPosition();
        run(ps, 30);
        check(glm::length(box3->worldPosition() - p3) < 1e-3f, "throwable=false 로 놓은 물체는 그 자리에 머묾(중력 끔)");

        swing(g4, box4);
        ia->grab(g3, box3->worldPosition(), hr);   // 다른 물체로 바꿔 잡음: box4 는 던지지 않고 놓임
        check(rb4->motionType == EMotionType::Dynamic && glm::length(rb4->linearVelocity()) < 1e-5f,
              "바꿔 잡으면 먼저 잡던 물체는 속도 없이 놓임");
        ia->release(glm::vec3(0.f), glm::vec3(0.f), false);

        // 던지기 속도(수학 보강 A1): 강체 속도장 v(p) = v_손 + ω × (p − 손) + 리그 속도, 최근 프레임 평균.
        {
            const auto t0 = hxr::throwVelocity({ 0.f, 0.f, 0.f }, { 0.f, 0.f, 2.f }, { 0.f, 1.f, 0.f },
                                               { 0.5f, 1.f, 0.f }, { 0.f, 0.f, 0.f });
            check(glm::length(t0.linear - glm::vec3(0.f, 1.f, 0.f)) < 1e-6f && t0.angular == glm::vec3(0.f, 0.f, 2.f),
                  "손목만 돌려도(손 선속도 0) 0.5m 앞 질량 중심은 ω × r = 1 m/s 로 날아감");
            const auto t1 = hxr::throwVelocity({ 3.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }, { 0.f, 1.f, 0.f },
                                               { 0.f, 1.f, 0.f }, { 0.f, 0.f, -2.f });
            check(glm::length(t1.linear - glm::vec3(3.f, 0.f, -2.f)) < 1e-6f, "리그 이동 속도를 더함");
            const auto t2 = hxr::throwVelocity({ 1.f, 2.f, 3.f }, { 0.f, 0.f, 5.f }, { 1.f, 1.f, 1.f },
                                               { 1.f, 1.f, 1.f }, { 0.f, 0.f, 0.f });
            check(glm::length(t2.linear - glm::vec3(1.f, 2.f, 3.f)) < 1e-6f, "질량 중심이 손 원점이면 손 속도 그대로");

            hxr::VelocityHistory h;
            glm::vec3 l, a;
            check(!h.mean(l, a) && l == glm::vec3(0.f), "표본이 없으면 평균 없음(0)");
            h.push({ 1.f, 0.f, 0.f }, { 0.f, 2.f, 0.f });
            h.push({ 3.f, 0.f, 0.f }, { 0.f, 4.f, 0.f });
            check(h.mean(l, a) && std::fabs(l.x - 2.f) < 1e-6f && std::fabs(a.y - 3.f) < 1e-6f, "표본 둘의 평균");
            for (int i = 0; i < 7; ++i) h.push({ float(10 + i), 0.f, 0.f }, { 0.f, 0.f, 0.f });   // 10 ~ 16
            h.mean(l, a);
            check(h.count == hxr::VelocityHistory::kSize && std::fabs(l.x - 14.f) < 1e-5f,
                  "창을 넘으면 최근 kSize 개(12 ~ 16)만 평균");

            // 질량 중심 조회: 상자는 가운데. 바디가 없으면 액터 위치.
            Actor* box5 = w.spawnActor("Box5");
            box5->setLocalPosition({ -3.f, 2.f, 1.f });
            box5->setLocalScale({ 0.3f, 0.1f, 0.2f });
            auto* rb5 = box5->addComponent<RigidBodyComponent>();
            rb5->motionType = EMotionType::Dynamic;
            check(rb5->centerOfMass() == box5->worldPosition(), "바디가 생기기 전에는 액터 위치");
            run(ps, 1);
            check(rb5->hasBody() && glm::length(rb5->centerOfMass() - box5->worldPosition()) < 1e-4f,
                  "상자 바디의 질량 중심은 액터 위치");

            // 손목 튕기기 던지기: 손 원점은 가만히 있고 z 축으로 4 rad/s 돌면, 손 0.3m 앞(+x) 물체는 +y 로 1.2 m/s.
            const glm::vec3 hand5 = box5->worldPosition() - glm::vec3(0.3f, 0.f, 0.f);
            ia->grab(box5->addComponent<GrabbableComponent>(), hand5, hr);
            check(!ps.isContinuous(rb5->bodyId_), "(준비) 잡기 전 바디는 연속 충돌 꺼짐(기본)");
            const auto tv = hxr::throwVelocity(glm::vec3(0.f), { 0.f, 0.f, 4.f }, hand5, rb5->centerOfMass(), glm::vec3(0.f));
            ia->release(tv.linear, tv.angular, true);
            check(glm::length(rb5->linearVelocity() - glm::vec3(0.f, 1.2f, 0.f)) < 1e-3f &&
                      glm::length(rb5->angularVelocity() - glm::vec3(0.f, 0.f, 4.f)) < 1e-3f,
                  "놓은 물체가 ω × r 선속도와 손 각속도를 받음");
            check(ps.isContinuous(rb5->bodyId_) && !rb5->continuous,
                  "던지면 그 바디만 연속 충돌을 켬(P1, 저장 값 continuous 는 그대로)");
        }

        // 월드를 통째로 지우면 onDisable 없이 해제됩니다. 다른 월드의 손도 해제된 컴포넌트를 가리키지 않아야 합니다.
        {
            World w2;
            Actor* toy = w2.spawnActor("Toy");
            ia->grab(toy->addComponent<GrabbableComponent>(), { 0.f, 0.f, 0.f }, hr);
        }
        check(ia->held() == nullptr, "월드가 통째로 사라져도 손이 비워짐");
        ps.shutdown();
    }

    // ── 리그 · 추적 자세 · 기본 이동 (2026-10-15) ─────────────────────────────
    {
        std::printf("\n-- 플레이어 리그: 추적 자세 · 스틱 이동 · 스냅 회전 · 데스크톱 둘러보기 · XR 원점 --\n");
        HInputSystem& in = HInputSystem::get();
        in.clearBindings();
        std::string err;
        for (const auto& [k, v] : HInputSystem::defaultBindings()) in.applyBinding(k, v, err);

        World w;
        Actor* rig  = w.spawnActor("Rig");
        rig->addComponent<XrRigComponent>();
        auto*  loco = rig->addComponent<RigLocomotionComponent>();
        Actor* head = w.spawnActor("Head");
        head->attachTo(rig);
        head->setLocalPosition({ 0.f, 1.7f, 0.f });
        auto* cam = head->addComponent<CameraComponent>();
        head->addComponent<TrackedPoseComponent>()->source = ETrackedSource::Head;
        check(RigLocomotionComponent::findHead(*rig) == head, "리그의 머리를 찾음");

        // 추적 자세: 바닥 기준이면 그대로, 바닥 공간이 없으면 눈높이만큼 올림
        HXrInputState s;
        s.focused = true;
        s.head.valid = true;
        s.head.position = { 0.1f, 1.6f, 0.2f };
        in.setXrInput(s);
        head->getComponent<TrackedPoseComponent>()->tick(0.f);
        check(glm::length(head->transform().position() - glm::vec3(0.1f, 1.6f, 0.2f)) < 1e-6f, "머리 자세를 로컬로 씀");
        s.floorOffset = 1.7f;
        s.head.position = { 0.f, 0.f, 0.f };
        in.setXrInput(s);
        head->getComponent<TrackedPoseComponent>()->tick(0.f);
        check(std::fabs(head->transform().position().y - 1.7f) < 1e-6f, "바닥 공간이 없으면 눈높이만큼 올림");

        // XR 원점: 리그면 리그 자세(+ 올림), 아니면 카메라 위치 + 요
        rig->setLocalPosition({ 2.f, 0.f, 3.f });
        cam->setXrRig(rig, 0.f);
        check(glm::length(glm::vec3(cam->xrOrigin()[3]) - glm::vec3(2.f, 0.f, 3.f)) < 1e-6f, "원점은 리그 위치(바닥)");
        cam->setXrRig(rig, 1.7f);
        check(std::fabs(cam->xrOrigin()[3].y - 1.7f) < 1e-6f, "바닥 공간이 없으면 원점을 눈높이만큼 올림");
        cam->setXrRig(nullptr, 0.f);
        check(std::fabs(cam->xrOrigin()[3].y - head->worldPosition().y) < 1e-6f, "리그가 아니면 카메라 위치");

        // 원점 고르기(DEF-144): 머리 추적 카메라는 부모가 원점. 부모가 리그가 아니어도 카메라 위치를 쓰지 않습니다.
        check(hxr::chooseOrigin(head).kind == hxr::EXrOrigin::SceneRig && hxr::chooseOrigin(head).actor == rig, "리그의 머리는 리그가 원점");
        {
            Actor* car  = w.spawnActor("Car");
            car->setLocalPosition({ 5.f, 0.5f, -2.f });
            car->setLocalRotation(glm::angleAxis(glm::radians(30.f), glm::vec3(0.f, 1.f, 0.f)));
            Actor* seat = w.spawnActor("SeatCam");
            seat->attachTo(car);
            auto* sc = seat->addComponent<CameraComponent>();
            auto* tp = seat->addComponent<TrackedPoseComponent>();
            tp->source = ETrackedSource::Head;
            const hxr::XrOriginChoice oc = hxr::chooseOrigin(seat);
            check(oc.kind == hxr::EXrOrigin::TrackedParent && oc.actor == car, "리그가 아닌 부모 아래 머리 추적 카메라는 그 부모가 원점");
            // 머리가 30° 돌고 옆으로 움직인 자세: 카메라 월드 자세 = 부모 × 머리. 원점이 부모여야 눈 = 원점 × 머리 가 같은 자리.
            s = HXrInputState{};
            s.focused = true;
            s.head.valid = true;
            s.head.position    = { 0.3f, 1.6f, 0.f };
            s.head.orientation = glm::angleAxis(glm::radians(30.f), glm::vec3(0.f, 1.f, 0.f));
            in.setXrInput(s);
            tp->tick(0.f);
            sc->setXrTrackedParent(oc.actor, 0.f);
            const glm::mat4 eye = sc->xrOrigin() * glm::translate(glm::mat4(1.f), s.head.position) * glm::mat4_cast(s.head.orientation);
            const glm::vec3 eyeFwd = glm::vec3(eye * glm::vec4(0.f, 0.f, -1.f, 0.f));
            const glm::vec3 camFwd = seat->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
            check(glm::length(glm::vec3(eye[3]) - seat->worldPosition()) < 1e-5f && glm::length(eyeFwd - camFwd) < 1e-5f,
                  "눈 자세가 카메라 액터와 같은 자리 · 방향(머리 이동 · 요가 한 번만)");
            seat->detach();
            check(hxr::chooseOrigin(seat).kind == hxr::EXrOrigin::TrackedParent && hxr::chooseOrigin(seat).actor == nullptr,
                  "부모가 없으면 월드 원점");
            sc->setXrTrackedParent(nullptr, 1.5f);
            check(glm::length(glm::vec3(sc->xrOrigin()[3]) - glm::vec3(0.f, 1.5f, 0.f)) < 1e-6f, "월드 원점(+ 올림)");
            tp->setEnabled(false);
            check(hxr::chooseOrigin(seat).kind == hxr::EXrOrigin::CameraOnly, "머리 추적을 끄면 예전처럼 카메라 기준");
            Actor* plain = w.spawnActor("PlainCam");
            plain->addComponent<CameraComponent>();
            check(hxr::chooseOrigin(plain).kind == hxr::EXrOrigin::CameraOnly && hxr::chooseOrigin(nullptr).kind == hxr::EXrOrigin::CameraOnly,
                  "머리 추적이 없으면 카메라 기준(보이지 않는 리그)");
            w.destroyActor(car); w.destroyActor(seat); w.destroyActor(plain);
            in.setXrInput(HXrInputState{});
        }
        rig->setLocalPosition({ 0.f, 0.f, 0.f });
        head->setLocalPosition({ 0.f, 1.7f, 0.f });
        head->setLocalRotation(glm::quat(1.f, 0.f, 0.f, 0.f));

        // VR 스틱 이동: 머리가 보는 방향(-Z)으로, 수평면 위로
        s = HXrInputState{};
        s.focused = true;
        s.hands[0].thumbstick = { 0.f, 1.f };
        in.setXrInput(s);
        in.update(0.f, [](int) { return false; });
        loco->tick(0.5f);
        check(glm::length(rig->worldPosition() - glm::vec3(0.f, 0.f, -1.f)) < 1e-4f, "왼쪽 스틱 위 = 앞으로 2m/s × 0.5s");
        // 아래를 보고 있어도 걷기는 수평
        head->setLocalRotation(glm::angleAxis(glm::radians(-60.f), glm::vec3(1.f, 0.f, 0.f)));
        loco->tick(0.5f);
        check(std::fabs(rig->worldPosition().y) < 1e-5f && rig->worldPosition().z < -1.9f, "아래를 봐도 수평으로 걸음");
        head->setLocalRotation(glm::quat(1.f, 0.f, 0.f, 0.f));

        // 스냅 회전: 머리 자리를 지키며 45° 씩, 스틱을 돌려놓아야 다시 돎.
        // 머리를 리그 중심에서 비켜 둡니다(놀이 공간 가장자리에 선 경우). 리그 중심으로 돌리면 머리가 옮겨집니다.
        head->setLocalPosition({ 0.3f, 1.7f, 0.2f });
        s.hands[0].thumbstick = { 0.f, 0.f };
        s.hands[1].thumbstick = { 1.f, 0.f };
        in.setXrInput(s);
        const glm::vec3 headBefore = head->worldPosition();
        loco->tick(0.016f);
        const glm::vec3 fwd = head->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
        check(glm::length(head->worldPosition() - headBefore) < 1e-4f, "스냅 회전은 머리 자리에서 돎");
        check(std::fabs(glm::degrees(std::atan2(fwd.x, -fwd.z)) - 45.f) < 0.1f, "오른쪽으로 45°");
        loco->tick(0.016f);
        loco->tick(0.016f);
        const glm::vec3 fwd2 = head->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
        check(glm::length(fwd2 - fwd) < 1e-5f, "스틱을 그대로 두면 한 번만 돎");
        s.hands[1].thumbstick = { 0.f, 0.f };
        in.setXrInput(s);
        loco->tick(0.016f);
        s.hands[1].thumbstick = { -1.f, 0.f };
        in.setXrInput(s);
        loco->tick(0.016f);
        const glm::vec3 fwd3 = head->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
        check(std::fabs(glm::degrees(std::atan2(fwd3.x, -fwd3.z))) < 0.1f, "돌려놓은 뒤 왼쪽으로 다시 45°");

        // 데스크톱: 오른쪽 버튼 끌기 100px → 15° 회전, 아래로 끌면 머리를 숙임
        in.setXrInput({});
        in.setLookDelta(100.f, 0.f);
        loco->tick(0.016f);
        const glm::vec3 f4 = head->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
        check(std::fabs(glm::degrees(std::atan2(f4.x, -f4.z)) - 15.f) < 0.1f, "데스크톱 둘러보기: 100px × 0.15° = 15°");
        in.setLookDelta(0.f, 100.f);
        loco->tick(0.016f);
        const glm::vec3 f5 = head->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
        check(f5.y < -0.2f, "아래로 끌면 머리를 숙임");
        in.setLookDelta(0.f, 0.f);
        in.clearBindings();
    }

    // ── 캐릭터 컨트롤러 · 텔레포트 (2026-11-17) ─────────────────────────────────────
    {
        std::printf("\n-- 캐릭터 컨트롤러(Jolt CharacterVirtual) --\n");
        World w;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w);
        check(PhysicsSystem::current() == &ps, "startup 뒤 current() 가 이 시스템");
        auto staticBox = [&](const char* name, const glm::vec3& pos, const glm::vec3& size) {
            Actor* a = w.spawnActor(name);
            a->setLocalPosition(pos);
            a->setLocalScale(size);
            a->addComponent<RigidBodyComponent>()->motionType = EMotionType::Static;
            return a;
        };
        staticBox("Floor", { 0.f, 0.f, 0.f }, { 20.f, 1.f, 20.f });          // 윗면 y = 0.5
        Actor* wall = staticBox("Wall", { 2.f, 1.5f, 0.f }, { 0.2f, 2.f, 6.f });   // 앞면 x = 1.9
        staticBox("LowStep",  { -3.f, 0.6f, 0.f },  { 2.f, 0.2f, 2.f });     // 윗면 0.7(턱 0.2 m)
        staticBox("HighStep", { 0.f, 0.8f, -3.f },  { 2.f, 0.6f, 2.f });     // 윗면 1.1(턱 0.6 m)
        run(ps, 1);

        PhysicsSystem::CharacterDesc d;
        const uint32_t ch = ps.createCharacter(d, { 0.f, 0.5f, 0.f });
        check(ch != 0, "캐릭터를 만듦");
        // 걷기: 수평 속도 + 떨어지는 속도(호출자가 중력을 넣음, RigLocomotion 과 같은 식)
        auto walk = [&](glm::vec3 feet, const glm::vec3& v, int frames, PhysicsSystem::CharacterMove& m,
                        std::span<Actor* const> ignore = {}) {
            float fall = 0.f;
            for (int i = 0; i < frames; ++i) {
                ps.moveCharacter(ch, feet, { v.x, fall, v.z }, kDt, m, ignore);
                feet = m.position;
                fall = m.onGround ? 0.f : fall - 9.81f * kDt;
            }
            return feet;
        };
        PhysicsSystem::CharacterMove m;
        glm::vec3 p = walk({ 0.f, 0.5f, 0.f }, { 3.f, 0.f, 0.f }, 60, m);
        std::printf("  (참고) 벽으로 1 초: 발 = (%.3f, %.3f, %.3f), 바닥 %d\n", p.x, p.y, p.z, int(m.onGround));
        check(std::fabs(p.x - (1.9f - d.radius)) < 0.05f, "벽 앞(반지름만큼 떨어진 곳)에서 막힘");
        check(std::fabs(p.y - 0.5f) < 0.03f && m.onGround, "바닥 위에 서 있음");
        p = walk(p, { 3.f, 0.f, 2.f }, 30, m);
        check(p.x < 1.9f - d.radius + 0.05f && p.z > 0.8f, "벽에 비스듬히 밀면 벽을 따라 미끄러짐");
        p = walk({ 0.f, 3.f, 1.f }, { 0.f, 0.f, 0.f }, 90, m);
        check(std::fabs(p.y - 0.5f) < 0.03f && m.onGround, "공중에서 떨어져 바닥에 섬");
        p = walk({ 0.f, 0.5f, 0.f }, { -3.f, 0.f, 0.f }, 60, m);
        std::printf("  (참고) 낮은 턱 쪽으로 1 초: 발 = (%.3f, %.3f, %.3f)\n", p.x, p.y, p.z);
        check(p.x < -2.2f && std::fabs(p.y - 0.7f) < 0.05f, "0.2 m 턱은 올라섬");
        p = walk({ 0.f, 0.5f, 0.f }, { 0.f, 0.f, -3.f }, 60, m);
        check(p.z > -2.f + 0.05f && std::fabs(p.y - 0.5f) < 0.05f, "0.6 m 턱은 막힘");
        Actor* ign[] = { wall };
        p = walk({ 0.f, 0.5f, 0.f }, { 3.f, 0.f, 0.f }, 60, m, ign);
        check(p.x > 2.5f, "ignore 에 넣은 액터는 통과");

        // 리그 이동(collide): 스틱 앞으로 → 높은 턱(−Z) 앞에서 막힘
        std::printf("\n-- 리그 충돌 이동 · 텔레포트 --\n");
        HInputSystem& in = HInputSystem::get();
        in.clearBindings();
        std::string err;
        for (const auto& [k, v] : HInputSystem::defaultBindings()) in.applyBinding(k, v, err);
        Actor* rig  = w.spawnActor("Rig");
        rig->setLocalPosition({ 0.f, 0.5f, 0.f });
        auto*  loco = rig->addComponent<RigLocomotionComponent>();
        loco->collide = true;
        Actor* head = w.spawnActor("Head");
        head->attachTo(rig);
        head->setLocalPosition({ 0.f, 1.6f, 0.f });
        head->addComponent<TrackedPoseComponent>()->source = ETrackedSource::Head;
        HXrInputState s;
        s.focused = true;                // 머리 자세는 TrackedPose 를 틱하지 않으므로 그대로
        s.hands[0].thumbstick = { 0.f, 1.f };
        in.setXrInput(s);
        in.update(0.f, [](int) { return false; });
        for (int i = 0; i < 90; ++i) loco->tick(kDt);
        std::printf("  (참고) 리그 = (%.3f, %.3f, %.3f)\n", rig->worldPosition().x, rig->worldPosition().y, rig->worldPosition().z);
        check(rig->worldPosition().z > -2.f && rig->worldPosition().z < -1.5f, "충돌 이동이 높은 턱 앞에서 멈춤");
        check(std::fabs(rig->worldPosition().y - 0.5f) < 0.05f, "리그가 바닥 높이를 지킴");
        loco->collide = false;
        for (int i = 0; i < 30; ++i) loco->tick(kDt);
        check(rig->worldPosition().z < -2.2f, "collide 를 끄면 예전처럼 통과");
        s.hands[0].thumbstick = { 0.f, 0.f };
        in.setXrInput(s);
        in.update(0.f, [](int) { return false; });

        ps.shutdown();
        check(PhysicsSystem::current() == nullptr, "shutdown 뒤 current() 는 null");
        ps.destroyCharacter(ch);   // 지운 뒤 불러도 안전
    }
    {
        std::printf("\n-- 텔레포트 포물선(순수 함수) --\n");
        // 바닥 y = 0 평면만 있는 광선
        const hxr::ArcRay ground = [](const glm::vec3& a, const glm::vec3& b, glm::vec3& pt, glm::vec3& n) {
            if (a.y < 0.f || b.y >= 0.f) return false;
            const float t = a.y / (a.y - b.y);
            pt = a + (b - a) * t;
            n  = { 0.f, 1.f, 0.f };
            return true;
        };
        const hxr::TeleportArc arc = hxr::teleportArc({ 0.f, 1.5f, 0.f }, { 0.f, 0.f, -1.f }, 8.f, 9.81f, 2.f, 0.04f, ground);
        const float expect = 8.f * std::sqrt(2.f * 1.5f / 9.81f);
        std::printf("  (참고) 수평 8 m/s, 높이 1.5 m: 착지 z = %.3f (식 %.3f), 점 %zu개\n", -arc.point.z, expect, arc.points.size());
        check(arc.hit && std::fabs(-arc.point.z - expect) < 0.02f, "수평으로 던지면 식대로 v·sqrt(2h/g) 앞에 떨어짐");
        check(arc.points.back() == arc.point, "꺾은선이 맞은 점에서 끝남");
        check(hxr::teleportValid(arc, 40.f), "평평한 바닥은 내릴 수 있음");
        hxr::TeleportArc wallArc = arc;
        wallArc.normal = { 1.f, 0.f, 0.f };
        check(!hxr::teleportValid(wallArc, 40.f), "벽(법선 수평)은 내릴 수 없음");
        wallArc.normal = glm::vec3(std::sin(glm::radians(30.f)), std::cos(glm::radians(30.f)), 0.f);
        check(hxr::teleportValid(wallArc, 40.f), "30° 비탈은 내릴 수 있음(한계 40°)");
        const hxr::TeleportArc up = hxr::teleportArc({ 0.f, 1.5f, 0.f }, { 0.f, 1.f, 0.f }, 8.f, 9.81f, 0.5f, 0.04f, ground);
        check(!up.hit && !hxr::teleportValid(up, 40.f), "maxTime 안에 맞지 않으면 내릴 곳 없음");
        const glm::vec3 r = hxr::teleportRigPosition({ 1.f, 0.f, 1.f }, { 1.3f, 1.6f, 0.8f }, { 5.f, 2.f, 5.f });
        check(glm::length(r - glm::vec3(4.7f, 2.f, 5.2f)) < 1e-5f, "텔레포트 뒤 머리 수평 위치가 대상 위, 리그 바닥 = 대상 높이");
    }

    // ── 보이지 않는 리그 · 리그 높이 (2026-10-16) ─────────────────────────────────
    {
        std::printf("\n-- 보이지 않는 리그: 카메라 아래 바닥 찾기, 원점 높이, 리그 높이 값 --\n");
        World w;
        boundsBox(w, "Ground", { 0.f, -0.1f, 0.f }, { 20.f, 0.2f, 20.f });   // 윗면 y = 0
        Actor* cam = w.spawnActor("Camera");
        cam->setLocalPosition({ 0.f, 0.3f, 2.5f });
        float y = -99.f;
        check(hxr::floorBelow(w, cam->worldPosition(), cam, y) && std::fabs(y) < 1e-5f, "카메라(0.3m) 아래 바닥 윗면 y=0");

        // 가까운 윗면이 먼저: 카메라 아래 탁자(윗면 0.8)
        Actor* table = boundsBox(w, "Table", { 0.f, 0.4f, 2.5f }, { 1.f, 0.8f, 1.f });
        cam->setLocalPosition({ 0.f, 1.5f, 2.5f });
        check(hxr::floorBelow(w, cam->worldPosition(), cam, y) && std::fabs(y - 0.8f) < 1e-5f, "더 가까운 윗면(탁자 0.8m)을 고름");
        // 시작점을 감싼 바운드(방 · 큰 물체 안)는 건너뜀
        cam->setLocalPosition({ 0.f, 0.5f, 2.5f });   // 탁자 안
        check(hxr::floorBelow(w, cam->worldPosition(), cam, y) && std::fabs(y) < 1e-5f, "카메라를 감싼 바운드는 건너뛰고 그 아래 바닥");
        // 끈 액터는 보지 않음
        table->setActive(false);
        cam->setLocalPosition({ 0.f, 1.5f, 2.5f });
        check(hxr::floorBelow(w, cam->worldPosition(), cam, y) && std::fabs(y) < 1e-5f, "끈 액터(탁자)는 바닥이 아님");
        // 카메라 자신과 그 자식은 보지 않음
        Actor* body = boundsBox(w, "CamBody", { 0.f, -0.5f, 0.f }, { 0.3f, 0.3f, 0.3f });
        body->attachTo(cam);
        check(hxr::floorBelow(w, cam->worldPosition(), cam, y) && std::fabs(y) < 1e-5f, "카메라의 자식은 바닥이 아님");
        // 밟는 면이 아니라고 알린 바운드(눕힌 3D UI 패널)는 보지 않음, 같은 액터의 다른 바운드는 봄
        Actor* panel = w.spawnActor("LyingPanel");
        panel->setLocalPosition({ 0.f, 1.0f, 2.5f });
        panel->setLocalScale({ 1.f, 0.02f, 1.f });
        panel->addComponent<NotFloor>();
        check(hxr::floorBelow(w, cam->worldPosition(), cam, y) && std::fabs(y) < 1e-5f, "blocksFloor=false 바운드(눕힌 패널)는 바닥이 아님");
        panel->addComponent<BoundsOnly>();
        check(hxr::floorBelow(w, cam->worldPosition(), cam, y) && std::fabs(y - 1.01f) < 1e-4f, "같은 액터의 메시 바운드는 바닥 후보");
        panel->setActive(false);   // 아래 시험에 끼지 않게
        // 아래에 아무것도 없음(바닥 밖 · 바닥보다 아래)
        check(!hxr::floorBelow(w, { 50.f, 1.f, 0.f }, nullptr, y), "바닥 밖이면 찾지 못함");
        check(!hxr::floorBelow(w, { 0.f, -5.f, 0.f }, nullptr, y), "바닥보다 아래면 찾지 못함");

        // 카메라 원점: 보이지 않는 리그는 카메라에서 drop 아래 + lift, 수평 위치 · 요는 카메라
        auto* cc = cam->addComponent<CameraComponent>();
        cam->setLocalPosition({ 1.f, 0.3f, 2.f });
        cam->setLocalRotation(glm::angleAxis(glm::radians(30.f), glm::vec3(0.f, 1.f, 0.f)));
        cc->setXrImplicitRig(0.3f, 0.f);
        glm::mat4 o = cc->xrOrigin();
        check(glm::length(glm::vec3(o[3]) - glm::vec3(1.f, 0.f, 2.f)) < 1e-5f, "바닥 기준 공간: 원점은 카메라 아래 바닥");
        const glm::vec3 of = glm::mat3(o) * glm::vec3(0.f, 0.f, -1.f);
        check(std::fabs(glm::degrees(std::atan2(-of.x, -of.z)) - 30.f) < 0.01f, "원점의 요는 카메라를 따름");
        cc->setXrImplicitRig(0.3f, 1.2f);
        check(std::fabs(cc->xrOrigin()[3].y - 1.2f) < 1e-5f, "고정 눈높이 1.2m: 원점(머리 기준 공간)은 바닥 + 1.2");
        cc->setXrRig(nullptr, 0.f);
        check(std::fabs(cc->xrOrigin()[3].y - 0.3f) < 1e-5f && !cc->hasImplicitXrRig(), "데스크톱으로 돌아오면 카메라 위치");

        // 리그 높이 값: 직렬화와 범위
        XrRigComponent rig;
        check(rig.trackedHeight && rig.eyeHeight == 1.7f, "리그 기본값: 실제 키, 눈높이 1.7m");
        rig.readField("height", "fixed");
        rig.readField("eyeHeight", "1.3");
        ComponentFieldWriter fw;
        rig.writeFields(fw);
        std::string fields;
        for (const auto& [k, v] : fw.fields()) fields += k + "=" + v + ";";
        check(!rig.trackedHeight && rig.eyeHeight == 1.3f && fields == "height=fixed;eyeHeight=1.3;", "height · eyeHeight 읽고 쓰기 (" + fields + ")");
        rig.readField("height", "standing");
        rig.readField("eyeHeight", "4");
        check(!rig.trackedHeight && rig.eyeHeight == 1.3f, "모르는 모드 · 범위 밖 값은 무시");
        for (auto& pd : rig.buildProps())
            if (std::string(pd.label) == "Eye Height" && pd.setF) pd.setF(9.f);
        check(rig.eyeHeight == 2.5f, "디테일 패널 값은 2.5m 로 자름");
    }

    if (g_failed) {
        std::printf("\n%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
