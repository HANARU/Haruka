// Actor 활성 상태(activeSelf / activeInHierarchy)와 컴포넌트 생명주기 연동 테스트입니다.
// Vulkan 도, 디바이스도, 창도, World 도 필요 없습니다. Actor 와 Component 만 컴파일합니다.
//
// 이 테스트가 존재하는 이유: A-2 의 핵심은 "실효 활성(enabled_ && 액터 activeInHierarchy)
// 이 뒤집힐 때 컴포넌트 onEnable/onDisable 이 정확히 한 번 발화한다" 입니다. 이 엣지 로직은
// 화면으로 확인하기 어렵고, 계층 전파·멱등성·attachTo 재부착 같은 경우가 얽혀 검산이 필요합니다.
//
// 실행: build/Release/HarukaActorTest.exe

#include "../src/core/Actor.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

// onEnable/onDisable/onStart/tick 발화 횟수를 세는 관찰용 컴포넌트.
struct Probe : Component {
    int enables = 0, disables = 0, starts = 0, ticks = 0;
    void onEnable()  override { ++enables; }
    void onDisable() override { ++disables; }
    void onStart()   override { ++starts;  }
    void tick(float) override { ++ticks;   }
    const char* typeName() const override { return "Probe"; }
};

} // namespace

int main() {
    std::printf("Haruka Actor 활성·생명주기 테스트\n");

    // 1. 활성 액터에 컴포넌트 추가 → onEnable 즉시 1회
    std::printf("\n== 1. 부착 시 onEnable ==\n");
    {
        Actor a;
        auto* p = a.addComponent<Probe>();
        check(p->enables == 1 && p->disables == 0, "활성 액터 부착 시 onEnable 1회");
        check(p->isLive(), "컴포넌트가 live");
        check(a.activeSelf() && a.activeInHierarchy(), "기본 액터는 activeSelf/activeInHierarchy 참");
    }

    // 2. setActive 토글이 onDisable/onEnable 발화, 같은 상태는 멱등
    std::printf("\n== 2. setActive 토글 ==\n");
    {
        Actor a;
        auto* p = a.addComponent<Probe>();
        a.setActive(false);
        check(p->disables == 1 && !p->isLive(), "setActive(false) → onDisable, live 해제");
        check(!a.activeInHierarchy() && !a.activeSelf(), "activeSelf/activeInHierarchy 거짓");
        a.setActive(true);
        check(p->enables == 2 && p->isLive(), "setActive(true) → onEnable 재발화");
        int e = p->enables, d = p->disables;
        a.setActive(true);
        check(p->enables == e && p->disables == d, "같은 상태 setActive 는 무발화(멱등)");
    }

    // 3. 계층 전파: 부모 비활성이 자식 컴포넌트를 끔
    std::printf("\n== 3. 부모→자식 전파 ==\n");
    {
        Actor parent, child;
        auto* cp = child.addComponent<Probe>();
        child.attachTo(&parent);
        check(cp->enables == 1 && cp->isLive(), "활성 부모에 붙은 자식은 live 유지");
        parent.setActive(false);
        check(child.activeSelf() && !child.activeInHierarchy(),
              "부모 비활성: 자식 activeSelf=참, activeInHierarchy=거짓");
        check(cp->disables == 1 && !cp->isLive(), "부모 비활성이 자식 컴포넌트 onDisable");
        parent.setActive(true);
        check(cp->enables == 2 && cp->isLive(), "부모 재활성이 자식 컴포넌트 onEnable");
    }

    // 4. 비활성 부모에 attachTo → onDisable, detach → onEnable
    std::printf("\n== 4. attachTo / detach 로 조상 활성 변화 ==\n");
    {
        Actor parent, child;
        parent.setActive(false);
        auto* cp = child.addComponent<Probe>();
        check(cp->isLive(), "부모 붙기 전 활성 자식은 live");
        child.attachTo(&parent);
        check(cp->disables == 1 && !cp->isLive(), "비활성 부모에 attachTo → 자식 onDisable");
        child.detach();
        check(cp->enables == 2 && cp->isLive(), "detach 로 조상 비활성 해제 → onEnable");
    }

    // 5. setEnabled 와 액터 활성의 상호작용
    std::printf("\n== 5. setEnabled × 액터 활성 ==\n");
    {
        Actor a;
        auto* p = a.addComponent<Probe>();   // enables=1
        a.setActive(false);                  // disables=1
        p->setEnabled(false);                // 이미 비-live → 무발화
        check(p->enables == 1 && p->disables == 1, "비활성 액터에서 setEnabled(false) 무발화");
        a.setActive(true);                   // 컴포넌트 disabled → 여전히 비-live
        check(p->enables == 1 && !p->isLive(), "컴포넌트 disabled 면 액터 재활성해도 onEnable 안 함");
        p->setEnabled(true);                 // 이제 활성 && enabled → live
        check(p->enables == 2 && p->isLive(), "액터 활성 상태에서 setEnabled(true) → onEnable");
    }

    // 6. tickIfReady 는 live 일 때만 tick, onStart 는 첫 틱 1회
    std::printf("\n== 6. tick 게이트 ==\n");
    {
        Actor a;
        auto* p = a.addComponent<Probe>();
        a.tick(0.1f);
        check(p->starts == 1 && p->ticks == 1, "live 컴포넌트는 onStart 후 tick");
        a.setActive(false);
        a.tick(0.1f);
        check(p->ticks == 1, "비활성 액터의 컴포넌트는 tick 안 함");
    }

    // 7. 3대 계층: 조부 비활성이 손자까지 전파
    std::printf("\n== 7. 3대 계층 전파 ==\n");
    {
        Actor gp, parent, child;
        auto* cp = child.addComponent<Probe>();
        parent.attachTo(&gp);
        child.attachTo(&parent);
        check(cp->isLive(), "3대 모두 활성이면 손자 live");
        gp.setActive(false);
        check(!child.activeInHierarchy(), "조부 비활성 → 손자 activeInHierarchy 거짓");
        check(cp->disables == 1 && !cp->isLive(), "조부 비활성이 손자 컴포넌트 onDisable");
        gp.setActive(true);
        check(cp->isLive(), "조부 재활성 → 손자 live 복귀");
    }

    // 8. 사이클 가드: 자기 자신·자손 아래 attachTo 거부(무한 재귀·크래시 없음)
    //    아래에서 attachTo 가 거부될 때 stderr 경고가 한 줄씩 나오는 것은 정상입니다.
    std::printf("\n== 8. 사이클 가드 ==\n");
    {
        Actor a, b;
        a.attachTo(&b);                 // a 는 b 의 자식(정상)
        b.attachTo(&a);                 // b 를 자기 자손 a 아래에 → 거부되어야 함
        check(a.getParent() == &b, "정상 attachTo 는 유지");
        check(b.getParent() == nullptr, "사이클 유발 attachTo 거부 → b 는 루트 유지");

        Actor c;
        c.attachTo(&c);                 // 자기 자신 → 거부
        check(c.getParent() == nullptr, "자기 자신에 attachTo 거부");

        // 사이클이 없으므로 activeInHierarchy 는 무한 재귀 없이 반환합니다.
        check(a.activeInHierarchy() && b.activeInHierarchy(),
              "사이클 없이 activeInHierarchy 정상 반환");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
