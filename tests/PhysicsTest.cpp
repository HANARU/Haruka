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
//
// 실행: build/Release/HarukaPhysicsTest.exe

#include "../src/core/World.hpp"
#include "../src/core/Actor.hpp"
#include "../src/components/RigidBodyComponent.hpp"
#include "../src/physics/PhysicsSystem.hpp"

#include <cmath>
#include <cstdio>
#include <string>

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

    if (g_failed) {
        std::printf("\n%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
