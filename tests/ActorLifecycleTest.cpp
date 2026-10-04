// Actor 활성 상태(activeSelf / activeInHierarchy)와 컴포넌트 생명주기 연동 테스트입니다.
// Vulkan 도, 디바이스도, 창도, World 도 필요 없습니다. Actor 와 Component 만 컴파일합니다.
//
// 이 테스트가 존재하는 이유: A-2 의 핵심은 "실효 활성(enabled_ && 액터 activeInHierarchy)
// 이 뒤집힐 때 컴포넌트 onEnable/onDisable 이 정확히 한 번 발화한다" 입니다. 이 엣지 로직은
// 화면으로 확인하기 어렵고, 계층 전파·멱등성·attachTo 재부착 같은 경우가 얽혀 검산이 필요합니다.
//
// 실행: build/Release/HarukaActorTest.exe

#include "../src/core/Actor.hpp"
#include "../src/components/LightComponent.hpp"
#include "../src/components/CameraComponent.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
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

    // 9. Attenuation Radius 최솟값(DEF-35). 예전 UI 는 0 까지 허용했고, 반경 0 이면 Point·Spot
    //    라이트가 통째로 꺼졌습니다. 디테일 패널이 쓰는 것과 같은 setter(PropDesc)로 확인합니다.
    std::printf("\n== 9. Attenuation Radius 최솟값 ==\n");
    {
        Actor a;
        auto* lc = a.addComponent<LightComponent>();
        lc->lightType = LightType::Point;
        PropDesc* radius = nullptr;
        auto props = lc->buildProps();
        for (auto& pd : props)
            if (std::strcmp(pd.label, "Attenuation Radius") == 0) radius = &pd;
        check(radius != nullptr, "Point 라이트에 Attenuation Radius 행이 있음");
        if (radius) {
            radius->setF(0.f);
            check(lc->attenuationRadius == LightComponent::kMinAttenuationRadius, "0 을 넣으면 최솟값(0.01m)");
            radius->setF(-5.f);
            check(lc->attenuationRadius == LightComponent::kMinAttenuationRadius, "음수를 넣어도 최솟값");
            radius->setF(3.f);
            check(lc->attenuationRadius == 3.f, "정상 값은 그대로");
        }
    }

    // 10. 카메라 자세(DEF-21). 예전 viewMatrix 는 컴포넌트의 yaw/pitch 와 액터의 **로컬** 위치를 써서
    //     액터 회전 · 롤 · 부모를 무시했습니다. 이제 액터의 월드 회전과 월드 위치에서 만듭니다.
    std::printf("\n== 10. 카메라 자세 ==\n");
    {
        auto near3 = [](const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < 1e-5f; };

        // 에디터의 yaw/pitch 규약이 예전 forward 공식과 같아야 에디터 화면이 그대로입니다.
        bool sameDir = true, sameView = true;
        const float cases[][2] = { {0.f, 0.15f}, {0.7f, 0.3f}, {-2.5f, -1.2f}, {3.f, 1.5f} };
        for (const auto& c : cases) {
            const float yaw = c[0], pitch = c[1];
            const glm::vec3 oldFwd(std::cos(pitch) * std::sin(yaw), -std::sin(pitch),
                                   -std::cos(pitch) * std::cos(yaw));
            Actor a;
            a.setLocalPosition({1.f, 2.f, 3.f});
            a.setLocalRotation(CameraComponent::yawPitchRotation(yaw, pitch));
            auto* cam = a.addComponent<CameraComponent>();
            sameDir = sameDir && near3(cam->forward(), oldFwd);
            const glm::mat4 oldView = glm::lookAt(glm::vec3(1, 2, 3), glm::vec3(1, 2, 3) + oldFwd,
                                                  glm::vec3(0, 1, 0));
            for (int col = 0; col < 4; ++col)
                sameView = sameView && glm::length(cam->viewMatrix()[col] - oldView[col]) < 1e-5f;
        }
        check(sameDir,  "yawPitchRotation 의 정면이 예전 yaw/pitch 공식과 같음");
        check(sameView, "yawPitchRotation 의 뷰 행렬이 예전 lookAt 과 같음(에디터 화면 불변)");

        // 액터 회전이 방향을 정합니다: Y 축 +90° 면 -X 를 봅니다.
        Actor turned;
        turned.setLocalRotation(glm::angleAxis(glm::radians(90.f), glm::vec3(0, 1, 0)));
        auto* cam = turned.addComponent<CameraComponent>();
        check(near3(cam->forward(), {-1.f, 0.f, 0.f}), "액터를 Y 축으로 90° 돌리면 카메라가 -X 를 봄");

        // 롤: Z 축 -90° 면 위가 +X 입니다(예전에는 위가 늘 +Y 였음).
        Actor rolled;
        rolled.setLocalRotation(glm::angleAxis(glm::radians(-90.f), glm::vec3(0, 0, 1)));
        auto* rc = rolled.addComponent<CameraComponent>();
        check(near3(rc->up(), {1.f, 0.f, 0.f}) && near3(rc->forward(), {0.f, 0.f, -1.f}),
              "롤을 주면 위 벡터가 함께 돎");

        // 부모: 부모 (10,0,0) · Y 축 90°, 자식 카메라 로컬 (0,0,5) → 월드 (15,0,0) 에서 -X 를 봄.
        Actor parent, child;
        parent.setLocalPosition({10.f, 0.f, 0.f});
        parent.setLocalRotation(glm::angleAxis(glm::radians(90.f), glm::vec3(0, 1, 0)));
        child.setLocalPosition({0.f, 0.f, 5.f});
        child.attachTo(&parent);
        auto* cc = child.addComponent<CameraComponent>();
        const glm::vec4 eyeInView = cc->viewMatrix() * glm::vec4(15.f, 0.f, 0.f, 1.f);
        check(near3(glm::vec3(eyeInView), glm::vec3(0.f)), "부모 아래 카메라는 월드 위치가 뷰 원점");
        check(near3(cc->forward(), {-1.f, 0.f, 0.f}), "부모 회전이 카메라 방향에 합성됨");

        // 크기는 뷰에 들어가지 않습니다(회전 부분이 직교 정규).
        Actor scaled;
        scaled.setLocalScale({2.f, 3.f, 4.f});
        auto* sc = scaled.addComponent<CameraComponent>();
        const glm::mat4 v = sc->viewMatrix();
        check(std::fabs(glm::length(glm::vec3(v[0])) - 1.f) < 1e-6f &&
              std::fabs(glm::length(glm::vec3(v[2])) - 1.f) < 1e-6f, "액터 크기는 뷰 행렬에 들어가지 않음");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
