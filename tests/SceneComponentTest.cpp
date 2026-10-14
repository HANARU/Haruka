// 컴포넌트 직렬화(DEF-22)의 검산입니다. 렌더러 · 에셋 · 창이 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 씬 파일은 사용자 데이터라, 저장 · 불러오기에서 값 하나가 조용히 바뀌거나
// 컴포넌트가 사라지면 화면에서 알아채기 어렵고 되돌릴 수도 없습니다. 이름 붙은 값의 왕복, 순서 무관,
// 모르는 이름 · 깨진 값 처리, 등록되지 않은 타입의 원문 보존, 옛 이름(별칭)을 직접 확인합니다.
//
// 실행: build/Release/HarukaSceneTest.exe

#include "../src/assets/ComponentSerializer.hpp"
#include "../src/components/CameraComponent.hpp"
#include "../src/components/LightComponent.hpp"
#include "../src/core/Actor.hpp"
#include "../src/core/ComponentFields.hpp"
#include "../src/core/ComponentRegistry.hpp"
#include "../src/core/UnknownComponent.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

bool near3(const glm::vec3& a, const glm::vec3& b) {
    return std::fabs(a.x - b.x) < 1e-6f && std::fabs(a.y - b.y) < 1e-6f && std::fabs(a.z - b.z) < 1e-6f;
}

// Layer 2 컴포넌트를 흉내 냅니다. 엔진 코드를 고치지 않고 등록만으로 저장 · 불러오기가 되는지 봅니다.
class OrbitComponent : public Component {
public:
    float     speed  = 1.f;
    glm::vec3 axis   { 0.f, 1.f, 0.f };
    bool      clockwise = false;
    const char* typeName() const override { return "OrbitComponent"; }
    void writeFields(ComponentFieldWriter& w) const override {
        w.f("speed", speed); w.v3("axis", axis); w.b("clockwise", clockwise);
    }
    bool readField(const std::string& key, const std::string& value) override {
        bool ok = true;
        if      (key == "speed")     ok = fieldparse::toFloat(value, speed);
        else if (key == "axis")      ok = fieldparse::toVec3 (value, axis);
        else if (key == "clockwise") ok = fieldparse::toBool (value, clockwise);
        else return false;
        if (!ok) reportBadField(key, value);
        return true;
    }
};

// 등록하지 않는 컴포넌트(저장되지 않아야 함).
class ScratchComponent : public Component {
public:
    const char* typeName() const override { return "ScratchComponent"; }
};

std::vector<ComponentSerializer::Block> parse(const std::string& text) {
    ComponentSerializer::BlockParser p;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) p.feed(line, "test");
    return p.blocks();
}

std::string writeOf(const Actor& a) {
    std::ostringstream os;
    ComponentSerializer::write(os, a);
    return os.str();
}

} // namespace

int main() {
    std::printf("Haruka 컴포넌트 직렬화 검산\n");
    auto& reg = ComponentRegistry::get();
    const ComponentLoadContext ctx;   // 에셋이 필요한 컴포넌트(메시)는 이 테스트에 없음

    std::printf("\n== 1. 등록 ==\n");
    check(reg.registerType<LightComponent>("LightComponent", "Light"), "Light 등록");
    check(reg.registerType<CameraComponent>("CameraComponent", "Camera"), "Camera 등록");
    check(reg.registerType<OrbitComponent>("OrbitComponent", "Orbit", { "SpinnerComponent" }),
          "Layer 2 컴포넌트를 옛 이름과 함께 등록");
    check(!reg.registerType<LightComponent>("LightComponent", "Light"), "같은 이름은 거부");
    check(!reg.registerType<CameraComponent>("Camera2", "", { "SpinnerComponent" }), "이미 쓰는 옛 이름도 거부");
    check(reg.find("SpinnerComponent") && reg.find("SpinnerComponent")->name == "OrbitComponent", "옛 이름으로 찾음");
    check(reg.entries().size() == 3 && reg.entries()[0].editorLabel == "Light" && reg.entries()[2].editorLabel == "Orbit",
          "버튼 순서 = 등록 순서");

    std::printf("\n== 2. 쓰기와 왕복 ==\n");
    std::string text;
    {
        Actor src;
        auto* l = src.addComponent<LightComponent>();
        l->lightType = LightType::Point; l->color = { 1.f, 0.5f, 0.25f }; l->intensity = 1.2345678f;
        l->attenuationRadius = 7.5f; l->useTemperature = true; l->temperatureK = 3200.f; l->castShadows = false;
        auto* c = src.addComponent<CameraComponent>();
        c->fovY = 70.f; c->zNear = 0.02f; c->zFar = 500.f; c->primary = false;
        c->setEnabled(false);
        auto* o = src.addComponent<OrbitComponent>();
        o->speed = 2.5f; o->axis = { 1.f, 0.f, 0.f }; o->clockwise = true;
        src.addComponent<ScratchComponent>();
        text = writeOf(src);
    }
    check(text.find("COMPONENT:LightComponent\nenabled=1\ntype=point\n") != std::string::npos,
          "LightComponent 블록: enabled 다음 이름 붙은 값(종류는 이름)");
    check(text.find("COMPONENT:CameraComponent\nenabled=0\n") != std::string::npos, "꺼진 컴포넌트는 enabled=0");
    check(text.find("COMPONENT:OrbitComponent") != std::string::npos, "Layer 2 컴포넌트도 씀");
    check(text.find("Scratch") == std::string::npos, "등록하지 않은 타입은 쓰지 않음(경고)");
    check(text.find("intensity=1.2345678\n") != std::string::npos, "실수는 정밀도를 잃지 않음");
    {
        Actor dst;
        const int made = ComponentSerializer::spawn(dst, parse(text), ctx);
        auto* l = dst.getComponent<LightComponent>();
        auto* c = dst.getComponent<CameraComponent>();
        auto* o = dst.getComponent<OrbitComponent>();
        check(made == 3 && l && c && o, "컴포넌트 셋을 다시 만듦");
        check(l && l->lightType == LightType::Point && near3(l->color, { 1.f, 0.5f, 0.25f }) &&
              l->intensity == 1.2345678f && l->attenuationRadius == 7.5f && l->useTemperature &&
              l->temperatureK == 3200.f && !l->castShadows, "라이트 값이 그대로");
        check(c && c->fovY == 70.f && c->zNear == 0.02f && c->zFar == 500.f && !c->primary && !c->isEnabled(),
              "카메라 값과 꺼진 상태가 그대로");
        check(o && o->speed == 2.5f && near3(o->axis, { 1.f, 0.f, 0.f }) && o->clockwise, "Layer 2 값이 그대로");
        check(writeOf(dst) == text, "다시 쓰면 글자까지 같음");
    }

    std::printf("\n== 3. 순서 · 모르는 이름 · 깨진 값 ==\n");
    {
        Actor a;
        ComponentSerializer::spawn(a, parse(
            "COMPONENT:LightComponent\n"
            "castShadows=0\n"
            "intensity=oops\n"          // 깨진 값 → 기본값 3 유지
            "glow=5\n"                  // 모르는 이름 → 건너뜀
            "type=spot\n"
            "color=0.1 0.2 0.3\n"
            "END_COMPONENT\n"), ctx);
        auto* l = a.getComponent<LightComponent>();
        check(l && l->lightType == LightType::Spot && near3(l->color, { 0.1f, 0.2f, 0.3f }) && !l->castShadows,
              "순서와 상관없이 읽음");
        check(l && l->intensity == 3.f, "깨진 값은 기본값을 둠(경고)");
        check(l && l->isEnabled(), "enabled 가 없으면 켜짐");
    }

    std::printf("\n== 4. 등록되지 않은 타입은 원문 보존 ==\n");
    {
        const std::string block =
            "COMPONENT:DebrisFieldComponent\n"
            "enabled=1\n"
            "count=2000\n"
            "seed=42\n"
            "END_COMPONENT\n";
        Actor a;
        ComponentSerializer::spawn(a, parse(block), ctx);
        auto* u = a.getComponent<UnknownComponent>();
        check(u && u->originalType == "DebrisFieldComponent" && u->rawFields.size() == 3, "자리표시자에 원문을 담음");
        check(writeOf(a) == block, "다시 쓰면 원래 블록과 글자까지 같음");
    }

    std::printf("\n== 5. 옛 이름 · 옛 숫자 · 반경 최솟값 ==\n");
    {
        Actor a;
        ComponentSerializer::spawn(a, parse(
            "COMPONENT:SpinnerComponent\nspeed=4\nEND_COMPONENT\n"
            "COMPONENT:LightComponent\ntype=1\nattenuationRadius=0\nEND_COMPONENT\n"), ctx);
        auto* o = a.getComponent<OrbitComponent>();
        auto* l = a.getComponent<LightComponent>();
        check(o && o->speed == 4.f, "옛 이름(SpinnerComponent)으로 새 타입을 만듦");
        check(writeOf(a).find("COMPONENT:OrbitComponent") != std::string::npos, "다시 쓸 때는 새 이름");
        check(l && l->lightType == LightType::Point, "종류는 옛 숫자(1)도 받음");
        check(l && l->attenuationRadius == LightComponent::kMinAttenuationRadius, "반경 0 은 최솟값으로 고침(DEF-35)");
    }

    std::printf("\n== 6. 깨진 블록 구조 ==\n");
    {
        const auto blocks = parse(
            "COMPONENT:CameraComponent\nfovY=50\n"     // END_COMPONENT 빠짐
            "COMPONENT:OrbitComponent\nspeed=3\nnot a field\nEND_COMPONENT\n");
        check(blocks.size() == 2 && blocks[0].fields.size() == 1 && blocks[1].fields.size() == 1,
              "END_COMPONENT 가 빠져도 다음 블록을 따로 읽고, '=' 없는 줄은 건너뜀");
    }

    std::printf("\n== 7. 값 해석 ==\n");
    {
        float f = 9.f; glm::vec3 v{ 9.f }; bool b = false; int i = 9;
        check(!fieldparse::toFloat("1.5x", f) && f == 9.f, "실수 뒤에 글자가 붙으면 실패하고 값을 건드리지 않음");
        check(fieldparse::toFloat(" 2.5 ", f) && f == 2.5f, "앞뒤 공백은 허용");
        check(!fieldparse::toVec3("1 2", v) && fieldparse::toVec3("1 2 3", v) && near3(v, { 1.f, 2.f, 3.f }),
              "벡터는 정확히 세 값");
        check(fieldparse::toBool("true", b) && b && !fieldparse::toBool("2", b), "참거짓은 0 · 1 · true · false 만");
        check(!fieldparse::toInt("", i) && i == 9, "빈 값은 실패");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
