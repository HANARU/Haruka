// 컴포넌트 직렬화(DEF-22)의 검산입니다. 렌더러 · 에셋 · 창이 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 씬 파일은 사용자 데이터라, 저장 · 불러오기에서 값 하나가 조용히 바뀌거나
// 컴포넌트가 사라지면 화면에서 알아채기 어렵고 되돌릴 수도 없습니다. 이름 붙은 값의 왕복, 순서 무관,
// 모르는 이름 · 깨진 값 처리, 등록되지 않은 타입의 원문 보존, 옛 이름(별칭)을 직접 확인합니다.
// 회전은 ROT(오일러 각)만으로는 왕복할 때마다 마지막 자리가 흔들려, 정확한 QUAT 를 함께 씁니다(DEF-108).
//
// 실행: build/Release/HarukaSceneTest.exe

#include "../src/assets/ComponentSerializer.hpp"
#include "../src/assets/TransformText.hpp"
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

    std::printf("\n== 8. 회전 왕복(DEF-108) ==\n");
    {
        // 씬 저장과 같은 순서로 글자를 만들고 다시 읽습니다: ROT 는 floatToText, QUAT 는 quatText.
        struct Text { std::string rot, quat; };
        auto write = [](const glm::quat& q) {
            const glm::vec3 e = transform_text::quatToEulerDeg(q);
            return Text{ floatToText(e.x) + " " + floatToText(e.y) + " " + floatToText(e.z),
                         transform_text::quatText(q) };
        };
        auto read = [](const Text& t, bool useQuat) {
            std::istringstream ss(t.rot);
            glm::vec3 e{};
            ss >> e.x >> e.y >> e.z;
            glm::quat q{ 1.f, 0.f, 0.f, 0.f };
            const bool has = useQuat && transform_text::parseQuat(t.quat, q);
            return transform_text::resolve(e, has, q);
        };
        auto same = [](const glm::quat& a, const glm::quat& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w &&
                   std::signbit(a.x) == std::signbit(b.x) && std::signbit(a.w) == std::signbit(b.w);
        };

        // 무작위 회전 2000개(고정 씨앗). QUAT 를 쓰면 다섯 번 왕복해도 비트까지 같아야 합니다.
        uint32_t seed = 12345u;
        auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.f * 2.f - 1.f; };
        int exact = 0, legacyDrift = 0;
        const int kCount = 2000;
        for (int n = 0; n < kCount; ++n) {
            const glm::quat q0 = glm::normalize(glm::quat(rnd(), rnd(), rnd(), rnd()));
            glm::quat q = q0;
            for (int k = 0; k < 5; ++k) q = read(write(q), true);
            if (same(q, q0)) ++exact;
            if (!same(read(write(q0), false), q0)) ++legacyDrift;
        }
        check(exact == kCount, "QUAT 가 있으면 다섯 번 왕복해도 그대로 (" + std::to_string(exact) + "/2000)");
        check(legacyDrift > 0, "ROT 만으로는 값이 바뀌는 회전이 있음(이 검사의 전제, " +
                               std::to_string(legacyDrift) + "/2000)");

        // 이번 점검에서 본 예: X 축 -10°. 예전에는 Play → Stop 마다 -9.999999 → -9.999998.
        const glm::quat cam = glm::quat(glm::radians(glm::vec3(-10.f, 0.f, 0.f)));
        Text t1 = write(cam);
        Text t2 = write(read(t1, true));
        Text t3 = write(read(t2, true));
        check(t1.rot == t2.rot && t2.rot == t3.rot && t1.quat == t3.quat, "-10° 회전의 글자가 왕복마다 같음 (" + t1.rot + ")");

        // 사람이 ROT 만 고치면 ROT 를 따릅니다. QUAT 가 남아 있어도 무시되지 않습니다.
        Text edited = t1;
        edited.rot = "0 45 0";
        const glm::quat want = glm::quat(glm::radians(glm::vec3(0.f, 45.f, 0.f)));
        check(same(read(edited, true), want), "ROT 를 손으로 고치면 ROT 가 이김");
        {
            std::istringstream ss(t1.rot);
            glm::vec3 e{};
            ss >> e.x >> e.y >> e.z;
            check(same(read(t1, false), glm::quat(glm::radians(e))), "QUAT 가 없으면 ROT 로 읽음(옛 파일)");
        }
        glm::quat junk{ 1.f, 0.f, 0.f, 0.f };
        check(!transform_text::parseQuat("0.1 0.2 x 1", junk) && junk.w == 1.f && !transform_text::parseQuat("0 0 0", junk),
              "깨진 QUAT 는 읽지 않음(ROT 로 대체)");
        check(transform_text::quatText(glm::quat(1.f, -0.f, 0.f, 0.5f)) == "-0 0 0.5 1", "-0 성분은 -0 으로 씀");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
