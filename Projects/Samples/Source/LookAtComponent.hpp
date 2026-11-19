#pragma once
// 액터 참조를 쓰는 예(참조 체계 R-8, Docs/Object_References.md).
// Play 중 매 틱 이 액터가 대상 액터를 바라보게 돌립니다(카메라처럼 -Z 가 앞). 대상은 디테일 패널의 "Target" 칸에서 고르고,
// 씬 파일에는 target=actor:<16진수> 로 남습니다. 대상이 지워지면 아무것도 하지 않고, Undo 로 되살아나면 다시 바라봅니다.
//
// 게임 코드에서 다른 액터를 가리킬 때는 Actor* 를 들지 말고 HActorRef 를 드십시오:
//   - 저장 · 불러오기 · 복제 · 프리팹 꺼내기에서 대상이 알맞게 이어집니다.
//   - resolveFrom(owner) 는 대상이 지워졌으면 nullptr 를 돌려주므로 매달린 포인터가 생기지 않습니다.

#include "scene/ActorRef.hpp"
#include "scene/Component.hpp"
#include "scene/ComponentFields.hpp"

#include <glm/gtc/quaternion.hpp>

class LookAtComponent : public Component {
public:
    HActorRef target;
    bool      yawOnly = true;   // 세로축(Y)으로만 돕니다(VR 에서 기울지 않게)

    const char* typeName() const override { return "LookAtComponent"; }

    std::vector<PropDesc> buildProps() override {
        auto* p = this;
        return { XPROP_ActorRef(p, "Target", target), XPROP_Toggle(p, "Yaw Only", yawOnly) };
    }

    void tick(float /*dt*/) override {
        const Actor* t = target.resolveFrom(owner);
        if (!t || t == owner) return;
        glm::vec3 dir = t->worldPosition() - owner->worldPosition();
        if (yawOnly) dir.y = 0.f;
        if (glm::dot(dir, dir) < 1e-8f) return;
        dir = glm::normalize(dir);
        // 위쪽이 dir 과 거의 나란하면(바로 위 · 아래) 다른 위쪽을 씁니다.
        const glm::vec3 up = std::abs(dir.y) > 0.999f ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
        owner->setWorldRotation(glm::quatLookAtRH(dir, up));
    }

    void writeFields(ComponentFieldWriter& w) const override {
        w.s("target",  target.toText());
        w.b("yawOnly", yawOnly);
    }
    bool readField(const std::string& key, const std::string& value) override {
        bool ok = true;
        if (key == "target")       ok = HActorRef::fromText(value, target);
        else if (key == "yawOnly") ok = fieldparse::toBool(value, yawOnly);
        else return false;
        if (!ok) reportBadField(key, value);
        return true;
    }
};
