// 에디터 편집 기본기(2026-10-15)를 화면 없이 검산합니다.
//
// 이 테스트가 존재하는 이유: 커맨드가 액터를 포인터 대신 ID 로 기억하게 바꾸고(액터 삭제를 되돌려도 앞선 편집이
// 이어짐), 되돌릴 수 없던 편집(리네임 · 켜기/끄기 · 부모 바꾸기 · 컴포넌트 추가/제거 · 토글 · 열거 속성)을 커맨드로
// 만들었습니다. 부모를 바꿔도 월드 자세가 그대로여야 하고, 여러 액터를 기즈모로 함께 움직이면 주 선택의 피벗을
// 중심으로 돌아야 하며, 복사한 글은 다른 프로그램의 글과 구별되어야 합니다.
// 실행: build/Release/HarukaEditCmdTest.exe
#include "../src/editor/EditorCommands.hpp"
#include "../src/editor/EditTools.hpp"
#include "../src/core/World.hpp"
#include "../src/core/ComponentRegistry.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }
bool nearQ(const glm::quat& a, const glm::quat& b, float eps = 1e-4f) { return std::abs(std::abs(glm::dot(a, b)) - 1.f) < eps; }

// 숫자 · 토글 · 열거 속성과 직렬화 값을 가진 시험용 컴포넌트
class TestComp : public Component {
public:
    float speed = 1.f;
    bool  loud  = false;
    int   mode  = 0;
    int   committed = 0;
    const char* typeName() const override { return "TestComp"; }
    std::vector<PropDesc> buildProps() override {
        auto* p = this;
        return { makePropFloat("Speed", PropColor::Neutral, 0.1f, [p] { return p->speed; }, [p](float v) { p->speed = v; }),
                 makePropToggle("Loud", [p] { return p->loud; }, [p](bool v) { p->loud = v; }),
                 makePropEnum("Mode", { "A", "B", "C" }, [p] { return p->mode; }, [p](int v) { p->mode = v; }) };
    }
    void onPropsCommitted() override { ++committed; }
    void writeFields(ComponentFieldWriter& w) const override { w.f("speed", speed); w.b("loud", loud); w.i("mode", mode); }
    bool readField(const std::string& k, const std::string& v) override {
        if (k == "speed") return fieldparse::toFloat(v, speed);
        if (k == "loud")  return fieldparse::toBool(v, loud);
        if (k == "mode")  return fieldparse::toInt(v, mode);
        return false;
    }
};
class OtherComp : public Component {
public:
    const char* typeName() const override { return "OtherComp"; }
};

} // namespace

int main() {
    std::printf("Haruka 편집 기본기 검산\n");
    ComponentRegistry::get().registerType<TestComp>("TestComp", "Test");
    ComponentRegistry::get().registerType<OtherComp>("OtherComp", "Other");

    std::printf("\n== 1. 액터: 자리 지정 붙이기 · 포인터로 컴포넌트 지우기 · 옮기기 ==\n");
    {
        World w;
        Actor* P = w.spawnActor("P");
        Actor* a = w.spawnActor("a");
        Actor* b = w.spawnActor("b");
        Actor* c = w.spawnActor("c");
        a->attachTo(P); b->attachTo(P); c->attachTo(P);
        check(c->siblingIndex() == 2, "끝에 붙음");
        c->attachTo(P, 0);
        check(c->siblingIndex() == 0 && a->siblingIndex() == 1, "같은 부모 안에서 자리만 옮김");
        a->attachTo(nullptr, 0);
        check(!a->getParent() && a->siblingIndex() == -1, "null 부모는 떼기");
        a->attachTo(P, 1);
        check(P->getChildren().size() == 3 && P->getChildren()[1] == a, "다른 부모에서 자리 지정해 붙임");
        P->attachTo(a, 0);
        check(!P->getParent(), "자손 아래로는 거부(사이클)");

        Actor* h = w.spawnActor("h");
        auto* t1 = h->addComponent<TestComp>();
        auto* o  = h->addComponent<OtherComp>();
        auto* t2 = h->addComponent<TestComp>();
        check(h->getComponent<TestComp>() == t1, "같은 타입이 둘이면 먼저 붙은 것을 찾음");
        check(h->removeComponent(t1) && h->getComponent<TestComp>() == t2, "먼저 붙은 것을 지우면 다음 것을 찾음");
        check(h->componentIndex(o) == 0 && h->componentIndex(t2) == 1, "붙은 순서 안의 자리");
        h->moveComponent(t2, 0);
        check(h->componentIndex(t2) == 0 && h->componentIndex(o) == 1, "자리를 옮김");
        check(!h->removeComponent(t1), "이미 지운 포인터는 false");
    }

    std::printf("\n== 2. 이름 · 켜기/끄기 · 부모 바꾸기 커맨드(월드 자세 유지) ==\n");
    {
        World w;
        HCommandStack st;
        Actor* P = w.spawnActor("P");
        P->setLocalPosition({ 10.f, 0.f, 0.f });
        P->setLocalRotation(glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f)));
        P->setLocalScale({ 2.f, 2.f, 2.f });
        Actor* Q = w.spawnActor("Q");
        Actor* x = w.spawnActor("x");
        Actor* y = w.spawnActor("y");
        x->attachTo(Q); y->attachTo(Q);
        x->setLocalPosition({ 1.f, 2.f, 3.f });
        x->setLocalRotation(glm::angleAxis(glm::radians(30.f), glm::vec3(1.f, 0.f, 0.f)));
        const glm::vec3 wp = x->worldPosition();
        const glm::quat wr = x->worldRotation();
        const glm::vec3 ws = x->worldScale();
        const Transform oldLocal = x->transform();

        st.push(std::make_unique<RenameActorCmd>(w, x, "x", "renamed"));
        x->name = "renamed";
        st.push(std::make_unique<SetActiveCmd>(w, x, true, false));
        x->setActive(false);

        auto rp = std::make_unique<ReparentCmd>(w, x->id, Q->id, x->siblingIndex(), oldLocal, P->id, -1, hxform::localUnder(*x, P));
        rp->redo();
        st.push(std::move(rp));
        check(x->getParent() == P, "새 부모 아래");
        check(near(x->worldPosition(), wp) && nearQ(x->worldRotation(), wr) && near(x->worldScale(), ws),
              "부모를 바꿔도 월드 위치 · 회전 · 크기가 그대로");
        st.undo();
        check(x->getParent() == Q && x->siblingIndex() == 0 && near(x->transform().position(), oldLocal.position()),
              "되돌리면 원래 부모 · 원래 형제 자리 · 원래 로컬");
        st.undo();
        check(x->activeSelf(), "켜기/끄기 되돌림");
        st.undo();
        check(x->name == "x", "이름 되돌림");
        st.redo(); st.redo(); st.redo();
        check(x->name == "renamed" && !x->activeSelf() && x->getParent() == P, "다시 하기");

        // 부모가 사라졌으면 부모 바꾸기를 하지 않습니다.
        st.undo();
        const uint64_t qid = Q->id;
        w.destroyActor(Q);
        check(!w.findById(qid), "(준비) 원래 부모를 지움");
        Actor* xx = w.findById(x->id);
        check(!xx, "(준비) 자식도 함께 지워짐 — 커맨드는 없는 액터를 건너뜀");
        st.redo();   // x 가 없으니 아무 일도 없음
        check(true, "없는 액터를 가리키는 커맨드도 실행해도 안전함");
    }

    std::printf("\n== 3. 컴포넌트 켜기/끄기 · 속성(숫자 · 토글 · 열거) ==\n");
    {
        World w;
        HCommandStack st;
        Actor* a = w.spawnActor("a");
        a->addComponent<OtherComp>();
        auto* t = a->addComponent<TestComp>();
        st.push(std::make_unique<ComponentEnabledCmd>(w, a, t, true, false));
        t->setEnabled(false);
        hcmd::PropValues before = hcmd::captureProps(*t);
        t->speed = 4.f; t->loud = true; t->mode = 2;
        hcmd::PropValues after = hcmd::captureProps(*t);
        check(before != after && before.f.size() == 1 && before.b.size() == 1 && before.i.size() == 1, "종류별로 값을 뜸");
        st.push(std::make_unique<ComponentPropsCmd>(w, a, t, before, after));
        st.undo();
        check(t->speed == 1.f && !t->loud && t->mode == 0 && t->committed == 1, "숫자 · 토글 · 열거를 되돌리고 확정 훅을 부름");
        st.undo();
        check(t->isEnabled(), "컴포넌트 켜기/끄기 되돌림");
        st.redo(); st.redo();
        check(!t->isEnabled() && t->speed == 4.f && t->loud && t->mode == 2, "다시 하기");

        // 같은 타입이 둘이면 순번으로 구별합니다(씬 파일에서는 둘이 붙을 수 있음).
        auto* t2 = a->addComponent<TestComp>();
        hcmd::PropValues b2 = hcmd::captureProps(*t2);
        t2->speed = 8.f;
        st.push(std::make_unique<ComponentPropsCmd>(w, a, t2, b2, hcmd::captureProps(*t2)));
        st.undo();
        check(t2->speed == 1.f && t->speed == 4.f, "두 번째 컴포넌트의 편집은 두 번째에만 되돌림");
    }

    std::printf("\n== 4. 컴포넌트 추가 · 제거(값 · 켜짐 · 순서 보존) ==\n");
    {
        World w;
        HCommandStack st;
        Actor* a = w.spawnActor("a");
        a->addComponent<OtherComp>();
        auto* t = a->addComponent<TestComp>();
        a->addComponent<OtherComp>();
        t->speed = 7.5f; t->loud = true; t->mode = 1;
        t->setEnabled(false);
        int changed = 0;
        auto rm = std::make_unique<ComponentPresenceCmd>(ComponentPresenceCmd::Kind::Remove, w, a, t, ComponentLoadContext{},
                                                         [&] { ++changed; });
        rm->redo();
        st.push(std::move(rm));
        check(!a->getComponent<TestComp>() && a->getComponents().size() == 2, "지움");
        st.undo();
        auto* back = a->getComponent<TestComp>();
        check(back && back->speed == 7.5f && back->loud && back->mode == 1 && !back->isEnabled(), "값과 꺼짐 상태로 되살림");
        check(back && a->componentIndex(back) == 1, "원래 자리(가운데)로 되살림");
        st.redo();
        check(!a->getComponent<TestComp>() && changed == 3, "다시 지움(바뀔 때마다 알림)");

        // 붙임 커맨드: 이미 붙인 뒤 쌓고, Undo 는 지우고 Redo 는 그때 값으로 되살림
        auto* n = a->addComponent<TestComp>();
        n->speed = 3.f;
        st.push(std::make_unique<ComponentPresenceCmd>(ComponentPresenceCmd::Kind::Add, w, a, n, ComponentLoadContext{}, nullptr));
        st.undo();
        check(!a->getComponent<TestComp>(), "붙임을 되돌리면 지움");
        st.redo();
        check(a->getComponent<TestComp>() && a->getComponent<TestComp>()->speed == 3.f, "다시 하면 값과 함께 되살림");

        // 속성 커맨드는 컴포넌트를 포인터가 아니라 타입 · 순번으로 찾으므로, 되살린 컴포넌트에도 이어집니다.
        auto* cur = a->getComponent<TestComp>();
        hcmd::PropValues b0 = hcmd::captureProps(*cur);
        cur->speed = 9.f;
        st.push(std::make_unique<ComponentPropsCmd>(w, a, cur, b0, hcmd::captureProps(*cur)));
        auto rm2 = std::make_unique<ComponentPresenceCmd>(ComponentPresenceCmd::Kind::Remove, w, a, cur, ComponentLoadContext{}, nullptr);
        rm2->redo();
        st.push(std::move(rm2));
        st.undo();   // 되살림(새 객체)
        st.undo();   // 속성 되돌림 → 새 객체에 적용
        check(a->getComponent<TestComp>() && a->getComponent<TestComp>()->speed == 3.f, "되살린 컴포넌트에도 앞선 속성 Undo 가 이어짐");
    }

    std::printf("\n== 5. 묶음 커맨드 ==\n");
    {
        World w;
        Actor* a = w.spawnActor("a");
        auto comp = std::make_unique<CompositeCmd>();
        Transform t0 = a->transform(), t1 = t0, t2 = t0;
        t1.setPosition({ 1.f, 0.f, 0.f });
        t2.setPosition({ 2.f, 0.f, 0.f });
        comp->add(std::make_unique<SetTransformCmd>(w, a, t0, t1));
        comp->add(std::make_unique<SetTransformCmd>(w, a, t1, t2));
        comp->redo();
        check(a->transform().position().x == 2.f, "순서대로 다시 하기");
        comp->undo();
        check(a->transform().position().x == 0.f, "거꾸로 되돌리기");
    }

    std::printf("\n== 6. 여러 액터 함께 움직이기(주 선택 피벗) ==\n");
    {
        hxform::WorldPose ps{ { 0.f, 0.f, 0.f }, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f } };
        hxform::WorldPose os{ { 2.f, 0.f, 0.f }, glm::quat(1.f, 0.f, 0.f, 0.f), { 1.f, 1.f, 1.f } };
        hxform::WorldPose pn = ps;
        pn.pos = { 0.f, 0.f, 5.f };
        check(near(hxform::followPivot(ps, pn, os).pos, { 2.f, 0.f, 5.f }), "이동은 같은 만큼");
        pn = ps;
        pn.rot = glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f));
        const hxform::WorldPose r = hxform::followPivot(ps, pn, os);
        check(near(r.pos, { 0.f, 0.f, -2.f }) && nearQ(r.rot, pn.rot), "회전은 주 선택 위치를 중심으로");
        pn = ps;
        pn.scale = { 2.f, 2.f, 2.f };
        const hxform::WorldPose sc = hxform::followPivot(ps, pn, os);
        check(near(sc.pos, { 4.f, 0.f, 0.f }) && near(sc.scale, { 2.f, 2.f, 2.f }), "크기는 피벗에서 거리와 크기를 함께 늘림");
    }

    std::printf("\n== 7. 클립보드 글 · 복사본 이름 · 화면 맞추기 거리 ==\n");
    {
        const std::string set = "ACTOR:Cube\nPOS:0 0 0\nEND_ACTOR\n";
        const std::string clip = hclip::wrap(set, { 0, 42 });
        std::string out;
        std::vector<uint64_t> parents;
        check(hclip::unwrap(clip, out, parents) && out == set && parents == std::vector<uint64_t>{ 0, 42 }, "감싸고 풀면 그대로");
        std::string crlf = clip;
        for (size_t i = 0; (i = crlf.find('\n', i)) != std::string::npos; i += 2) crlf.insert(i, "\r");
        check(hclip::unwrap(crlf, out, parents) && out == set, "CRLF 로 바뀌어도 읽음");
        check(!hclip::unwrap("hello world", out, parents), "다른 글은 거부");
        check(!hclip::unwrap(std::string(hclip::kHeader) + "\n#ROOT_PARENTS x\nACTOR:a\n", out, parents), "깨진 부모 목록은 거부");
        check(!hclip::unwrap(std::string(hclip::kHeader) + "\n#ROOT_PARENTS 0\n", out, parents), "액터가 없으면 거부");

        std::set<std::string> names{ "Cube", "Cube (1)", "Lamp (3)" };
        check(hclip::nextCopyName("Cube", names) == "Cube (2)", "이미 있는 번호는 건너뜀");
        check(hclip::nextCopyName("Cube (1)", names) == "Cube (2)", "번호가 붙은 이름은 번호를 바꿈");
        check(hclip::nextCopyName("Lamp (3)", names) == "Lamp (1)", "가장 작은 빈 번호");
        check(hclip::nextCopyName("Odd (x)", names) == "Odd (x) (1)", "숫자가 아닌 괄호는 이름의 일부");

        const float d = hframe::fitDistance(1.f, glm::radians(90.f), 1.f);
        check(std::abs(d - 1.f / std::sin(glm::radians(45.f))) < 1e-4f, "반지름 1, 시야 90° → 1/sin45°");
        check(hframe::fitDistance(1.f, glm::radians(90.f), 0.5f) > d, "세로로 긴 화면은 좁은 가로 시야에 맞춰 더 멀리");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
