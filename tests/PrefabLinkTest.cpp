// 링크된 프리팹(Docs/Linked_Prefabs.md)을 검산합니다: 인스턴스 블록 펼치기 · 결정적 GUID · 비교로 얻는 덮어쓰기 · 다시 펼침 ·
// 중첩 2단 왕복 · 순환 거부 · 추가 · 삭제 · 링크 끊기 · 되돌리기 · 원본에 적용 · 옛 씬 · 프리팹 읽기. 창 · GPU 가 필요 없습니다
// (메시 없는 액터만 써서 AssetManager 가 렌더러를 부르지 않음). 프리팹 글은 월드의 PrefabSourceCache 에 넣어 파일을 쓰지 않습니다.
//
// 실행: build/Release/HarukaPrefabTest.exe

#include "../src/assets/AssetManager.hpp"
#include "../src/core/AssetRefs.hpp"
#include "../src/core/Guid.hpp"
#include "../src/editor/ActorSnapshot.hpp"
#include "../src/editor/EditorCommands.hpp"
#include "../src/editor/PrefabCommands.hpp"
#include "../src/scene/ActorRef.hpp"
#include "../src/scene/ComponentRegistry.hpp"
#include "../src/sceneio/PrefabLink.hpp"
#include "../src/sceneio/PrefabManager.hpp"
#include "../src/sceneio/PrefabRules.hpp"
#include "../src/sceneio/SceneSerializer.hpp"
#include "../src/core/FieldParse.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed; else ++g_failed;
}

// 시험용 컴포넌트: 숫자 하나와 다른 액터 참조 하나.
class ProbeComponent : public Component {
public:
    float     value = 0.f;
    HActorRef target;
    const char* typeName() const override { return "ProbeComponent"; }
    std::vector<PropDesc> buildProps() override {
        auto* p = this;
        return { XPROP_Float(p, "Value", PropColor::Neutral, 0.1f, value), XPROP_ActorRef(p, "Target", target) };
    }
    void writeFields(ComponentFieldWriter& w) const override {
        w.f("value", value);
        w.s("target", target.toText());
    }
    bool readField(const std::string& key, const std::string& v) override {
        if (key == "value")  { fieldparse::toFloat(v, value); return true; }
        if (key == "target") { HActorRef::fromText(v, target); return true; }
        return false;
    }
};

const std::string kLamp = "/Game/prefabs/Lamp.hprefab";
const std::string kRoom = "/Game/prefabs/Room.hprefab";

Actor* byName(World& w, const std::string& name, int nth = 0) {
    for (const auto& ap : w.getActors())
        if (ap->name == name && nth-- == 0) return ap.get();
    return nullptr;
}
ProbeComponent* probe(Actor* a, int k = 0) {
    if (!a) return nullptr;
    int seen = 0;
    for (const auto& c : a->getComponents())
        if (auto* p = dynamic_cast<ProbeComponent*>(c.get()))
            if (seen++ == k) return p;
    return nullptr;
}
int count(const std::string& s, const std::string& sub) {
    int n = 0;
    for (size_t at = s.find(sub); at != std::string::npos; at = s.find(sub, at + 1)) ++n;
    return n;
}

// 액터 서브트리를 프리팹 글로(PrefabManager::save 와 같은 글, 파일은 쓰지 않음).
std::string prefabText(const Actor* root, const std::string& path = {}) {
    std::vector<const Actor*> set;
    std::vector<int>          parents;
    std::function<void(const Actor*, int)> walk = [&](const Actor* a, int parent) {
        const int me = (int)set.size();
        set.push_back(a);
        parents.push_back(parent);
        for (const Actor* c : a->getChildren()) walk(c, me);
    };
    walk(root, -1);
    return std::string(hprefab::kPrefabHeaderV6) + "\n"
         + hprefablink::writeCompact(set, parents, hprefablink::loaderFor(root->world()), path);
}

// Lamp 프리팹: 루트 Lamp(value 1, 대상 = Bulb) 아래 Bulb(value 2).
std::string makeLamp(World& w0, float lampValue = 1.f, float bulbValue = 2.f) {
    Actor* lamp = w0.spawnActor("Lamp");
    w0.assignGuid(lamp, HGuid{ 0xa1 });
    Actor* bulb = w0.spawnActor("Bulb");
    w0.assignGuid(bulb, HGuid{ 0xa2 });
    bulb->attachTo(lamp);
    bulb->setLocalPosition({ 0.f, 1.f, 0.f });
    auto* pl = lamp->addComponent<ProbeComponent>();
    pl->value = lampValue;
    pl->target = HActorRef(bulb->guid());
    bulb->addComponent<ProbeComponent>()->value = bulbValue;
    const std::string t = prefabText(lamp);
    w0.destroyActor(lamp);
    return t;
}

std::string instanceLine(const std::string& path, uint64_t guid) {
    return "PREFAB:" + path + "\nGUID:" + HGuid{ guid }.toString() + "\nEND_PREFAB\n";
}

struct Fixture {
    World                          w;
    hprefablink::PrefabSourceCache cache;
    Fixture() { w.setService<hprefablink::PrefabSourceCache>(&cache); }
    ~Fixture() { w.setService<hprefablink::PrefabSourceCache>(nullptr); }
};

void testDerive() {
    std::printf("[derive]\n");
    const HGuid a = hprefablink::derive(HGuid{ 1 }, HGuid{ 2 });
    check(a == hprefablink::derive(HGuid{ 1 }, HGuid{ 2 }), "같은 (루트, 원본) → 같은 값");
    check(a != hprefablink::derive(HGuid{ 2 }, HGuid{ 1 }), "순서가 다르면 다른 값");
    check(a != hprefablink::derive(HGuid{ 1 }, HGuid{ 3 }), "원본이 다르면 다른 값");
    check(hprefablink::derive(HGuid{ 0 }, HGuid{ 0 }).valid(), "0 이 되지 않음");
}

void testInstance(AssetManager& assets) {
    std::printf("[instance]\n");
    Fixture f;
    World w0;
    const std::string lampText = makeLamp(w0);
    check(lampText.rfind("# Haruka Prefab v6\n", 0) == 0 && lampText.find("PREFAB:") == std::string::npos, "평범한 프리팹은 v6 머리 줄 + 액터 블록");
    f.cache.set(kLamp, lampText);

    const std::string scene = std::string(SceneSerializer::kSceneHeader) + "\n" + instanceLine(kLamp, 0xb1);
    const auto slots = SceneSerializer::readScene(scene, f.w, assets);
    check(slots.size() == 2 && slots[0] && slots[1], "인스턴스 블록 하나 → 펼친 액터 2개");
    Actor* lamp = byName(f.w, "Lamp");
    Actor* bulb = byName(f.w, "Bulb");
    check(lamp && lamp->guid() == HGuid{ 0xb1 } && lamp->prefabLink.source == kLamp && lamp->prefabLink.pair == HGuid{ 0xa1 },
          "루트: 씬의 GUID · 원본 경로 · 짝 = 원본 루트 GUID");
    check(bulb && bulb->getParent() == lamp && bulb->guid() == hprefablink::derive(HGuid{ 0xb1 }, HGuid{ 0xa2 }) &&
              bulb->prefabLink.pair == HGuid{ 0xa2 },
          "자식: 계층 · 파생 GUID · 짝");
    check(probe(lamp) && probe(lamp)->target.guid() == bulb->guid(), "원본 안 참조는 인스턴스 안을 가리킴");
    check(hprefablink::instanceRootOf(bulb) == lamp && hprefablink::instanceRootOf(lamp) == lamp, "소속 → 루트");

    const std::string saved = SceneSerializer::writeScene(f.w);
    check(saved == scene, "바꾸지 않은 인스턴스는 인스턴스 블록만(덮어쓰기 없음)");

    // 덮어쓰기.
    lamp->setLocalPosition({ 3.f, 0.f, 0.f });
    probe(bulb)->value = 5.f;
    const std::string s2 = SceneSerializer::writeScene(f.w);
    check(count(s2, "OVERRIDE:") == 2, "바꾼 속성 두 개만 덮어쓰기");
    check(s2.find("OVERRIDE:00000000000000a1 POS=3 0 0\n") != std::string::npos, "루트 위치 = (원본 루트 짝 · POS)");
    check(s2.find("OVERRIDE:00000000000000a2 ProbeComponent[0].value=5\n") != std::string::npos, "컴포넌트 값 = (짝 · 형[순번].칸)");

    // 다시 읽기 → 같은 값 · 같은 글.
    Fixture g;
    g.cache.set(kLamp, lampText);
    SceneSerializer::readScene(s2, g.w, assets);
    check(probe(byName(g.w, "Bulb")) && probe(byName(g.w, "Bulb"))->value == 5.f && byName(g.w, "Lamp")->transform().position().x == 3.f,
          "다시 읽으면 덮어쓴 값");
    check(SceneSerializer::writeScene(g.w) == s2, "다시 쓰면 같은 글");
    check(hprefablink::overridePathsOf(*byName(g.w, "Bulb"), hprefablink::loaderFor(&g.w)) ==
              std::vector<std::string>{ "ProbeComponent[0].value" },
          "디테일 표시용 덮어쓴 경로");

    // 옛 씬(v6, 인스턴스 블록 없음)은 링크 없는 복사로 읽힘.
    Fixture o;
    const std::string v6 = "# Haruka Scene v6\nACTOR:Old\nGUID:00000000000000c1\nEND_ACTOR\n";
    SceneSerializer::readScene(v6, o.w, assets);
    check(byName(o.w, "Old") && byName(o.w, "Old")->prefabLink.empty(), "옛 씬 v6 → 링크 없는 액터");
}

void testRefresh(AssetManager& assets) {
    std::printf("[refresh]\n");
    Fixture f;
    World w0;
    const std::string lampText = makeLamp(w0);
    f.cache.set(kLamp, lampText);
    SceneSerializer::readScene(std::string(SceneSerializer::kSceneHeader) + "\n" + instanceLine(kLamp, 0xb1) + instanceLine(kLamp, 0xb2),
                               f.w, assets);
    Actor* l1 = byName(f.w, "Lamp", 0);
    Actor* l2 = byName(f.w, "Lamp", 1);
    probe(l1->getChildren()[0])->value = 5.f;   // 인스턴스 1 만 Bulb 를 덮어씀
    l2->setLocalPosition({ -2.f, 0.f, 0.f });
    const uint64_t id1 = l1->id, bulbId1 = l1->getChildren()[0]->id;

    // 원본이 바뀜: Lamp 1 → 7, Bulb 2 → 9.
    World w1;
    const std::string lamp2 = makeLamp(w1, 7.f, 9.f);
    const int n = PrefabManager::refreshInstances(f.w, assets, kLamp, lamp2);
    check(n == 2, "기대는 인스턴스 둘을 다시 펼침");
    Actor* r1 = f.w.findByGuid(HGuid{ 0xb1 });
    Actor* r2 = f.w.findByGuid(HGuid{ 0xb2 });
    check(r1 && r2 && probe(r1)->value == 7.f && probe(r2)->value == 7.f, "덮어쓰지 않은 Lamp 값은 둘 다 원본을 따름");
    check(probe(r1->getChildren()[0])->value == 5.f, "덮어쓴 Bulb 값은 유지");
    check(probe(r2->getChildren()[0])->value == 9.f, "덮어쓰지 않은 인스턴스의 Bulb 는 새 값");
    check(r2->transform().position().x == -2.f, "루트 위치 덮어쓰기 유지");
    check(r1->id == id1 && r1->getChildren()[0]->id == bulbId1, "실행 번호(Undo 커맨드가 쓰는 ID)를 지킴");
    std::string cached;
    check(f.cache.peek(kLamp, &cached) && cached == lamp2, "캐시가 새 글로 바뀜");
}

void testAddRemove(AssetManager& assets) {
    std::printf("[add/remove]\n");
    Fixture f;
    World w0;
    f.cache.set(kLamp, makeLamp(w0));
    SceneSerializer::readScene(instanceLine(kLamp, 0xb1), f.w, assets);
    Actor* lamp = byName(f.w, "Lamp");
    Actor* bulb = byName(f.w, "Bulb");

    // 원본에 없는 컴포넌트 · 지운 컴포넌트 · 더한 액터.
    lamp->addComponent<ProbeComponent>()->value = 4.f;
    bulb->removeComponent(probe(bulb));
    Actor* extra = f.w.spawnActor("Extra");
    extra->attachTo(bulb);
    const std::string s = SceneSerializer::writeScene(f.w);
    check(s.find("OVERRIDE:00000000000000a1 ProbeComponent[1].value=4\n") != std::string::npos, "더한 컴포넌트 = 순번이 원본 수 이상인 덮어쓰기");
    check(s.find("REMOVE:00000000000000a2 ProbeComponent[0]\n") != std::string::npos, "지운 컴포넌트 = REMOVE 짝 형[순번]");
    check(s.find("PARENT_GUID:" + bulb->guid().toString() + "\n") != std::string::npos, "인스턴스 안 액터 아래 더한 액터 = PARENT_GUID");

    Fixture g;
    g.cache = f.cache;
    SceneSerializer::readScene(s, g.w, assets);
    Actor* gl = byName(g.w, "Lamp");
    Actor* gb = byName(g.w, "Bulb");
    Actor* ge = byName(g.w, "Extra");
    check(gl && probe(gl, 1) && probe(gl, 1)->value == 4.f, "다시 읽으면 더한 컴포넌트");
    check(gb && !probe(gb), "다시 읽으면 지운 컴포넌트 없음");
    check(ge && ge->getParent() == gb && ge->prefabLink.empty(), "더한 액터는 인스턴스 안 부모에 붙은 보통 액터");
    check(SceneSerializer::writeScene(g.w) == s, "추가 · 삭제 왕복 글 동일");

    // 인스턴스 안 액터를 지움 → REMOVE:짝. 더한 액터는 부모가 사라져 루트로.
    g.w.destroyActor(gb);
    const std::string s2 = SceneSerializer::writeScene(g.w);
    check(s2.find("REMOVE:00000000000000a2\n") != std::string::npos, "지운 액터 = REMOVE 짝");
    Fixture h;
    h.cache = f.cache;
    SceneSerializer::readScene(s2, h.w, assets);
    check(!byName(h.w, "Bulb") && byName(h.w, "Lamp"), "다시 읽으면 지운 액터 없음");

    // 인스턴스 안 액터를 인스턴스 밖으로 옮기면 빠져 보통 액터.
    Fixture m;
    m.cache = f.cache;
    SceneSerializer::readScene(instanceLine(kLamp, 0xb3), m.w, assets);
    Actor* mb = byName(m.w, "Bulb");
    mb->attachTo(nullptr);
    check(hprefablink::instanceRootOf(mb) == nullptr, "밖으로 옮긴 액터는 소속이 아님");
    const std::string s3 = SceneSerializer::writeScene(m.w);
    check(s3.find("REMOVE:00000000000000a2\n") != std::string::npos && s3.find("ACTOR:Bulb\n") != std::string::npos &&
              s3.find("PREFAB_PAIR") == std::string::npos,
          "옮긴 액터 = 인스턴스에서 REMOVE + 링크 없는 보통 블록");

    // 인스턴스 안에서 부모만 바꿈 → @PARENT.
    Fixture q;
    World wq;
    {
        Actor* a = wq.spawnActor("A"); wq.assignGuid(a, HGuid{ 0xd1 });
        Actor* b = wq.spawnActor("B"); wq.assignGuid(b, HGuid{ 0xd2 }); b->attachTo(a);
        Actor* c = wq.spawnActor("C"); wq.assignGuid(c, HGuid{ 0xd3 }); c->attachTo(a);
        q.cache.set("/Game/prefabs/Tri.hprefab", prefabText(a));
    }
    SceneSerializer::readScene(instanceLine("/Game/prefabs/Tri.hprefab", 0xe1), q.w, assets);
    byName(q.w, "C")->attachTo(byName(q.w, "B"));
    const std::string s4 = SceneSerializer::writeScene(q.w);
    check(s4.find("OVERRIDE:00000000000000d3 @PARENT=00000000000000d2\n") != std::string::npos, "안에서 부모 바꿈 = @PARENT 덮어쓰기");
    Fixture q2;
    q2.cache = q.cache;
    SceneSerializer::readScene(s4, q2.w, assets);
    check(byName(q2.w, "C")->getParent() == byName(q2.w, "B"), "다시 읽으면 바뀐 부모");
}

void testNested(AssetManager& assets) {
    std::printf("[nested]\n");
    Fixture f;
    World w0;
    const std::string lampText = makeLamp(w0);
    f.cache.set(kLamp, lampText);

    // Room 프리팹 = Room 액터 아래 Lamp 인스턴스(Bulb 값 3 으로 덮어씀). 2단 중첩.
    World wr;
    hprefablink::PrefabSourceCache rc;
    rc.set(kLamp, lampText);
    wr.setService<hprefablink::PrefabSourceCache>(&rc);
    Actor* room = wr.spawnActor("Room");
    wr.assignGuid(room, HGuid{ 0xf1 });
    SceneSerializer::readScene(instanceLine(kLamp, 0xf2), wr, assets);
    Actor* innerLamp = wr.findByGuid(HGuid{ 0xf2 });
    innerLamp->attachTo(room);
    probe(innerLamp->getChildren()[0])->value = 3.f;
    const std::string roomText = prefabText(room);
    wr.setService<hprefablink::PrefabSourceCache>(nullptr);
    check(roomText.find("PREFAB:" + kLamp + "\nGUID:00000000000000f2\nPARENT:0\nOVERRIDE:00000000000000a2 ProbeComponent[0].value=3\n") !=
              std::string::npos,
          "프리팹 안 인스턴스 = 인스턴스 블록 + 덮어쓰기");
    f.cache.set(kRoom, roomText);

    // 씬: Room 인스턴스 → Room · Lamp · Bulb.
    const std::string scene = std::string(SceneSerializer::kSceneHeader) + "\n" + instanceLine(kRoom, 0x91);
    SceneSerializer::readScene(scene, f.w, assets);
    Actor* r = f.w.findByGuid(HGuid{ 0x91 });
    check(r && r->getChildren().size() == 1 && r->getChildren()[0]->name == "Lamp", "중첩 펼침: Room 아래 Lamp");
    Actor* lamp = r->getChildren()[0];
    Actor* bulb = lamp->getChildren().empty() ? nullptr : lamp->getChildren()[0];
    check(bulb && probe(bulb)->value == 3.f, "안쪽 덮어쓰기가 들어감");
    check(lamp->guid() == hprefablink::derive(HGuid{ 0x91 }, HGuid{ 0xf2 }) &&
              bulb->guid() == hprefablink::derive(HGuid{ 0x91 }, hprefablink::derive(HGuid{ 0xf2 }, HGuid{ 0xa2 })),
          "중첩 GUID = derive(바깥 루트, derive(안쪽 루트, 원본))");
    check(hprefablink::instanceRootOf(bulb) == r && lamp->prefabLink.source.empty(), "씬에서는 바깥 인스턴스 하나의 소속");
    check(probe(lamp)->target.guid() == bulb->guid(), "중첩 안 참조도 이번 인스턴스 안을 가리킴");

    // 바깥에서 한 번 더 덮어씀 → 저장 → 읽기 → 글자 동일.
    probe(bulb)->value = 8.f;
    const std::string s1 = SceneSerializer::writeScene(f.w);
    check(count(s1, "OVERRIDE:") == 1 && s1.find(" ProbeComponent[0].value=8\n") != std::string::npos, "바깥 덮어쓰기 하나");
    Fixture g;
    g.cache = f.cache;
    SceneSerializer::readScene(s1, g.w, assets);
    const std::string s2 = SceneSerializer::writeScene(g.w);
    check(s2 == s1, "중첩 2단 저장 → 읽기 → 저장 글자 동일");

    // 가장 안쪽 원본(Lamp)이 바뀌면 Room 인스턴스도 다시 펼침. 덮어쓴 Bulb 는 유지, Lamp 값은 새 값.
    World w1;
    const int n = PrefabManager::refreshInstances(g.w, assets, kLamp, makeLamp(w1, 6.f, 1.f));
    Actor* gr = g.w.findByGuid(HGuid{ 0x91 });
    check(n == 1 && gr && probe(gr->getChildren()[0])->value == 6.f && probe(gr->getChildren()[0]->getChildren()[0])->value == 8.f,
          "중첩 원본이 바뀌면 바깥 인스턴스도 갱신, 덮어쓴 값 유지");

    // 원본에 적용(D9): 바깥 인스턴스의 Bulb 덮어쓰기를 Room 글에 넣으면 안쪽 인스턴스 블록의 덮어쓰기가 됩니다.
    std::string rt = roomText;
    const auto lines = hprefablink::overrideLinesOf(*gr->getChildren()[0]->getChildren()[0], hprefablink::loaderFor(&g.w));
    const int applied = hprefablink::applyToSourceText(rt, kRoom, lines, hprefablink::loaderFor(&g.w));
    check(applied == 1 && rt.find("OVERRIDE:00000000000000a2 ProbeComponent[0].value=8\n") != std::string::npos &&
              rt.find("value=3\n") == std::string::npos,
          "중첩 액터를 원본에 적용 = 안쪽 인스턴스 블록의 덮어쓰기를 바꿈");
    std::string lt = lampText;
    const int a2 = hprefablink::applyToSourceText(lt, kLamp, { "OVERRIDE:00000000000000a2 ProbeComponent[0].value=4" },
                                                  hprefablink::loaderFor(&g.w));
    check(a2 == 1 && lt.find("value=4\n") != std::string::npos, "보통 블록에 적용 = 그 블록 값을 바꿈");

    // 순환: Lamp 가 Room 을 품고 Room 이 Lamp 를 품음.
    Fixture c;
    c.cache.set(kRoom, roomText);
    c.cache.set(kLamp, "# Haruka Prefab v6\nACTOR:Lamp\nGUID:00000000000000a1\nEND_ACTOR\nPREFAB:" + kRoom +
                           "\nGUID:00000000000000a9\nPARENT:0\nEND_PREFAB\n");
    std::vector<std::string> errs;
    std::vector<hprefablink::TBlock> out;
    const bool ok = hprefablink::expandSource(kRoom, HGuid{ 0x11 }, hprefablink::loaderFor(&c.w), out, &errs);
    bool cycleSeen = false;
    for (const auto& e : errs) cycleSeen = cycleSeen || e.find("cycle") != std::string::npos;
    check(!ok && cycleSeen, "안쪽 어디서든 순환을 찾으면 그 원본을 통째로 거부");
    check(hprefablink::dependsOn(kRoom, kLamp, hprefablink::loaderFor(&c.w)) &&
              hprefablink::dependsOn(kLamp, kLamp, hprefablink::loaderFor(&c.w)),
          "의존 판정이 순환에서 멈춤");
    // 순환 블록을 펼치지 못한 루트는 줄을 기억했다가 그대로 다시 씁니다(D8).
    Fixture d;
    d.cache.set(kLamp, "# Haruka Prefab v6\nPREFAB:" + kLamp + "\nGUID:00000000000000a7\nEND_PREFAB\n");
    const std::string selfScene = std::string(SceneSerializer::kSceneHeader) + "\nPREFAB:" + kLamp +
                                  "\nGUID:0000000000000071\nOVERRIDE:00000000000000a1 POS=1 2 3\nEND_PREFAB\n";
    SceneSerializer::readScene(selfScene, d.w, assets);
    Actor* ph = d.w.findByGuid(HGuid{ 0x71 });
    check(ph && ph->prefabLink.raw.size() == 1, "펼치지 못한 인스턴스는 빈 루트 + 원래 줄");
    check(SceneSerializer::writeScene(d.w) == selfScene, "펼치지 못한 인스턴스를 다시 저장하면 글 그대로");

    // 저장 거부: Room(안에 Lamp 인스턴스)을 Lamp 로 저장하려 하면 순환.
    check(!PrefabManager::save(gr, kLamp), "자신을 품게 되는 프리팹 저장을 거부");
}

void testEditor(AssetManager& assets) {
    std::printf("[editor]\n");
    Fixture f;
    World w0;
    f.cache.set(kLamp, makeLamp(w0));
    SceneSerializer::readScene(instanceLine(kLamp, 0xb1), f.w, assets);
    Actor* lamp = byName(f.w, "Lamp");
    Actor* bulb = byName(f.w, "Bulb");
    probe(bulb)->value = 5.f;
    const std::string withOverride = SceneSerializer::writeScene(f.w);

    // 되돌리기: 원본 값으로(Undo 가능).
    std::string base;
    check(hprefablink::baselineBlock(*bulb, hprefablink::loaderFor(&f.w), base), "원본 값 블록");
    HCommandStack stack;
    {
        auto cmd = std::make_unique<ActorStateCmd>(f.w, *bulb, hprefablink::actorBlock(*bulb), base, ComponentLoadContext{ &assets }, nullptr);
        cmd->redo();
        stack.push(std::move(cmd));
    }
    check(probe(byName(f.w, "Bulb"))->value == 2.f && SceneSerializer::writeScene(f.w).find("OVERRIDE:") == std::string::npos,
          "되돌리면 덮어쓰기가 사라짐");
    stack.undo();
    check(probe(byName(f.w, "Bulb"))->value == 5.f && SceneSerializer::writeScene(f.w) == withOverride, "되돌리기 Undo");

    // 링크 끊기(Undo 가능).
    {
        auto cmd = std::make_unique<PrefabUnlinkCmd>(f.w, *lamp, nullptr);
        cmd->redo();
        stack.push(std::move(cmd));
    }
    const std::string unlinked = SceneSerializer::writeScene(f.w);
    check(unlinked.find("PREFAB:") == std::string::npos && count(unlinked, "ACTOR:") == 2 && lamp->prefabLink.empty() &&
              bulb->prefabLink.empty(),
          "링크 끊기 = 보통 액터 두 블록");
    stack.undo();
    check(SceneSerializer::writeScene(f.w) == withOverride, "링크 끊기 Undo");

    // 지우기 → 되살리기(전체 형식 스냅숏)에도 링크가 따라옴.
    SnapshotWriter write = [](const std::vector<const Actor*>& o, const std::vector<int>& pi) { return SceneSerializer::writeActorSet(o, pi); };
    SnapshotReader read  = [&](const std::string& t) { return SceneSerializer::readScene(t, f.w, assets); };
    const ActorSnapshot snap = hsnap::capture(lamp, write);
    check(snap.text.find("PREFAB_SOURCE:" + kLamp) != std::string::npos && snap.text.find("PREFAB_PAIR:00000000000000a2") != std::string::npos,
          "스냅숏은 전체 형식(링크 줄 포함)");
    f.w.destroyActor(lamp);
    check(hsnap::restore(snap, f.w, read) != nullptr && SceneSerializer::writeScene(f.w) == withOverride, "되살리면 같은 인스턴스");

    // 붙여넣기(Fresh): 새 루트 GUID, 소속은 그 값에서 파생(MEMBER_GUID 없음).
    Actor* l = byName(f.w, "Lamp");
    const std::string clip = SceneSerializer::writeActorSet({ l, l->getChildren()[0] }, { -1, 0 });
    const auto pasted = SceneSerializer::readScene(clip, f.w, assets, SceneSerializer::GuidPolicy::Fresh);
    check(pasted.size() == 2 && pasted[0]->guid() != l->guid() &&
              pasted[1]->guid() == hprefablink::derive(pasted[0]->guid(), HGuid{ 0xa2 }),
          "붙여넣은 인스턴스: 새 루트 + 파생 GUID");
    check(probe(pasted[0])->target.guid() == pasted[1]->guid(), "붙여넣은 인스턴스 안 참조도 새 값");
    const std::string s = SceneSerializer::writeScene(f.w);
    check(count(s, "PREFAB:") == 2 && s.find("MEMBER_GUID") == std::string::npos, "붙여넣은 인스턴스도 인스턴스 블록, MEMBER_GUID 없음");

    // 파생 값과 다른 GUID(손으로 바꿈) → MEMBER_GUID 로 지킴.
    Fixture m;
    m.cache = f.cache;
    SceneSerializer::readScene(instanceLine(kLamp, 0xb5), m.w, assets);
    m.w.assignGuid(byName(m.w, "Bulb"), HGuid{ 0x777 });
    const std::string ms = SceneSerializer::writeScene(m.w);
    check(ms.find("MEMBER_GUID:00000000000000a2 0000000000000777\n") != std::string::npos, "다른 GUID = MEMBER_GUID");
    Fixture m2;
    m2.cache = f.cache;
    SceneSerializer::readScene(ms, m2.w, assets);
    check(byName(m2.w, "Bulb")->guid() == HGuid{ 0x777 } && SceneSerializer::writeScene(m2.w) == ms, "MEMBER_GUID 왕복");
}

void testOldPrefab(AssetManager& assets) {
    std::printf("[old prefab]\n");
    // v4 프리팹(GUID 없음)도 링크된 인스턴스로 꺼낼 수 있고, 짝은 경로 + 순번으로 정해져 왕복이 같습니다.
    Fixture f;
    const std::string path = "/Game/prefabs/OldPair.hprefab";
    f.cache.set(path, "# Haruka Prefab v4\nACTOR:P\nPOS:0 0 0\nEND_ACTOR\nACTOR:Q\nPOS:0 0 0\nPARENT:0\nEND_ACTOR\n");
    SceneSerializer::readScene(instanceLine(path, 0x51), f.w, assets);
    Actor* q = byName(f.w, "Q");
    check(q && q->getParent() == byName(f.w, "P") && q->prefabLink.pair.valid(), "GUID 없는 옛 프리팹: 짝을 만듦");
    const std::string s = SceneSerializer::writeScene(f.w);
    Fixture g;
    g.cache = f.cache;
    SceneSerializer::readScene(s, g.w, assets);
    check(byName(g.w, "Q") && byName(g.w, "Q")->guid() == q->guid() && SceneSerializer::writeScene(g.w) == s, "옛 프리팹 인스턴스 왕복");
    std::string probeText = "# Haruka Prefab v5\nACTOR:X\nEND_ACTOR\n";
    check(hprefab::stripBomAndDetectV3(probeText), "v5 머리 줄도 액터 블록 형식");
    std::string v6 = "# Haruka Prefab v6\nPREFAB:/Game/prefabs/A.hprefab\nEND_PREFAB\n";
    check(hprefab::stripBomAndDetectV3(v6), "v6 머리 줄을 알아봄");
}

void testAssetRefs() {
    std::printf("[asset refs]\n");
    const auto refs = hrefs::refsInText("/Game/scenes/S.hscene",
                                        "# Haruka Scene v7\nPREFAB:/Game/prefabs/Lamp.hprefab\n"
                                        "OVERRIDE:00000000000000a1 AudioComponent[0].file=/Game/audio/beep.wav\nEND_PREFAB\n");
    bool prefab = false, audio = false;
    for (const auto& r : refs) {
        prefab = prefab || r.target == "/Game/prefabs/Lamp.hprefab";
        audio  = audio  || r.target == "/Game/audio/beep.wav";
    }
    check(prefab, "인스턴스 블록의 원본 경로가 에셋 참조");
    check(audio, "덮어쓰기 값의 에셋 경로도 참조(옮기기 때 함께 바뀜)");
}

} // namespace

int main() {
    ComponentRegistry::get().registerType<ProbeComponent>("ProbeComponent");
    AssetManager assets(nullptr);
    testDerive();
    testInstance(assets);
    testRefresh(assets);
    testAddRemove(assets);
    testNested(assets);
    testEditor(assets);
    testOldPrefab(assets);
    testAssetRefs();
    std::printf("\n%s (%d passed, %d failed)\n", g_failed ? "FAILED" : "ALL PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
