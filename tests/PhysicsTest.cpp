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
#include "../src/core/ComponentFields.hpp"
#include "../src/components/XrComponents.hpp"
#include "../src/components/CameraComponent.hpp"
#include "../src/input/HInputSystem.hpp"
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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
