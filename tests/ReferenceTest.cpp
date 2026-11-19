// 참조 체계(Docs/Object_References.md)를 검산합니다: 액터 GUID · 액터 참조 · GUID 정책(Keep · Fresh) · 디테일 칸 ·
// 에셋 참조 색인 · 검사 · 옮기기. 창 · GPU 가 필요 없습니다(씬 글에는 메시가 없어 AssetManager 가 렌더러를 부르지 않음).
//
// 실행: build/Release/HarukaRefTest.exe

#include "../src/core/AssetRefs.hpp"
#include "../src/core/Guid.hpp"
#include "../src/editor/ActorPicker.hpp"
#include "../src/editor/EditorCommands.hpp"
#include "../src/scene/ActorRef.hpp"
#include "../src/scene/ComponentRegistry.hpp"
#include "../src/scene/UnknownComponent.hpp"
#include "../src/sceneio/GuidRemap.hpp"
#include "../src/sceneio/SceneSerializer.hpp"
#include "../src/assets/AssetManager.hpp"
#include "../src/vfs/PathUtf8.hpp"
#include "../Projects/Samples/Source/LookAtComponent.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed; else ++g_failed;
}

// 시험용 컴포넌트: 다른 액터 하나를 가리킵니다.
class RefProbeComponent : public Component {
public:
    HActorRef target;
    bool      flag = false;
    const char* typeName() const override { return "RefProbeComponent"; }
    std::vector<PropDesc> buildProps() override {
        auto* p = this;
        return { XPROP_ActorRef(p, "Target", target), XPROP_Toggle(p, "Flag", flag) };
    }
    void writeFields(ComponentFieldWriter& w) const override {
        w.s("target", target.toText());
        w.b("flag", flag);
    }
    bool readField(const std::string& key, const std::string& value) override {
        if (key == "target") { if (!HActorRef::fromText(value, target)) reportBadField(key, value); return true; }
        if (key == "flag")   { fieldparse::toBool(value, flag); return true; }
        return false;
    }
};

Actor* byName(World& w, const std::string& name, int nth = 0) {
    for (const auto& a : w.getActors())
        if (a->name == name && nth-- == 0) return a.get();
    return nullptr;
}

std::string readBytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
void writeBytes(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
}

// ── 1. GUID ─────────────────────────────────────────────────────────────────────
void testGuid() {
    std::printf("\n== 1. guid ==\n");
    std::set<uint64_t> seen;
    bool allValid = true;
    for (int i = 0; i < 10000; ++i) {
        const HGuid g = HGuid::make();
        allValid = allValid && g.valid();
        seen.insert(g.value);
    }
    check(allValid && seen.size() == 10000, "1만 개 모두 0 아님 · 겹침 없음");
    HGuid g{ 0x00ab00cd00ef0012ull };
    check(g.toString() == "00ab00cd00ef0012", "16자리 소문자, 앞의 0 포함");
    HGuid back;
    check(HGuid::parse(g.toString(), back) && back == g, "왕복");
    check(HGuid::parse("00AB00CD00EF0012", back) && back == g, "대문자도 읽음");
    check(!HGuid::parse("", back) && !HGuid::parse("00ab00cd00ef001", back) && !HGuid::parse("00ab00cd00ef00123", back),
          "길이가 16이 아니면 거절");
    check(!HGuid::parse("00ab00cd00ef001g", back) && !HGuid::parse("0000000000000000", back), "16진수 아닌 글자 · 0 거절");
}

// ── 2. World ────────────────────────────────────────────────────────────────────
void testWorld() {
    std::printf("\n== 2. world ==\n");
    World w;
    Actor* a = w.spawnActor("A");
    Actor* b = w.spawnActor("B");
    check(a->guid().valid() && b->guid().valid() && a->guid() != b->guid(), "spawnActor 가 서로 다른 GUID 를 줌");
    check(w.findByGuid(a->guid()) == a && w.findByGuid(HGuid{}) == nullptr, "findByGuid");
    const HGuid fixed{ 0x1234 };
    check(w.assignGuid(a, fixed) && a->guid() == fixed && w.findByGuid(fixed) == a, "assignGuid 로 바꿈");
    check(!w.assignGuid(b, fixed) && b->guid() != fixed, "이미 쓰는 값이면 거절");
    check(!w.assignGuid(b, HGuid{}), "0 은 거절");
    const HGuid bg = b->guid();
    const uint64_t bid = b->id;
    w.reassignId(b, bid + 100);
    check(b->guid() == bg, "reassignId 는 GUID 를 바꾸지 않음");
    w.destroyActor(a);
    check(w.findByGuid(fixed) == nullptr, "지우면 색인에서 빠짐");
    Actor* c = w.spawnActor("C");
    check(w.assignGuid(c, fixed) && w.findByGuid(fixed) == c, "지운 GUID 는 다시 쓸 수 있음");
}

// ── 3. HActorRef ────────────────────────────────────────────────────────────────
void testRef() {
    std::printf("\n== 3. ref ==\n");
    World w;
    Actor* a = w.spawnActor("A");
    Actor* holder = w.spawnActor("Holder");
    auto* probe = holder->addComponent<RefProbeComponent>();
    probe->target.set(a);
    check(probe->target.resolve(w) == a && probe->target.resolveFrom(holder) == a, "대상을 찾음");
    check(probe->target.resolve(w) == a, "두 번째도 같은 대상(캐시)");
    const HGuid ag = a->guid();
    w.destroyActor(a);
    check(probe->target.resolve(w) == nullptr && probe->target.isSet(), "대상을 지우면 nullptr, 값은 남음");
    Actor* again = w.spawnActor("A again");
    w.assignGuid(again, ag);
    check(probe->target.resolve(w) == again, "같은 GUID 가 돌아오면 다시 찾음");
    HActorRef r;
    check(HActorRef::fromText("", r) && !r.isSet(), "빈 값 = 없음");
    check(HActorRef::fromText("none", r) && !r.isSet(), "none = 없음");
    check(HActorRef::fromText("actor:00000000000000aa", r) && r.guid().value == 0xaa, "actor:<16진수>");
    check(r.toText() == "actor:00000000000000aa" && HActorRef().toText().empty(), "toText");
    check(!HActorRef::fromText("00000000000000aa", r) && !r.isSet(), "접두 없으면 거절");
    check(!HActorRef::fromText("actor:xyz", r), "깨진 값 거절");
}

// ── 4. 씬 파일 ────────────────────────────────────────────────────────────────────
std::string sceneOf(World& w) { return SceneSerializer::writeScene(w); }

void testFile(AssetManager& assets) {
    std::printf("\n== 4. file ==\n");
    World w;
    Actor* a = w.spawnActor("Target");
    Actor* b = w.spawnActor("Holder");
    b->addComponent<RefProbeComponent>()->target.set(a);
    const std::string text = sceneOf(w);
    check(text.rfind(SceneSerializer::kSceneHeader, 0) == 0, "머리 줄 v6");
    check(text.find("GUID:" + a->guid().toString()) != std::string::npos && text.find("GUID:" + b->guid().toString()) != std::string::npos,
          "액터마다 GUID: 줄");
    check(text.find("target=actor:" + a->guid().toString()) != std::string::npos, "참조 값 actor:<16진수>");

    World w2;
    const auto slots = SceneSerializer::readScene(text, w2, assets);
    check(slots.size() == 2 && slots[0] && slots[1], "두 액터를 읽음");
    check(slots[0]->guid() == a->guid() && slots[1]->guid() == b->guid(), "Keep: 파일의 GUID 를 지킴");
    auto* p2 = slots[1]->getComponent<RefProbeComponent>();
    check(p2 && p2->target.resolve(w2) == slots[0], "읽은 참조가 읽은 대상을 가리킴");
    check(sceneOf(w2) == text, "다시 쓰면 같은 글");

    // GUID 없는 옛 v5 파일
    const std::string v5 = "# Haruka Scene v5\nACTOR:Old1\nPOS:0 0 0\nEND_ACTOR\nACTOR:Old2\nPOS:1 0 0\nEND_ACTOR\n";
    World w3;
    const auto old = SceneSerializer::readScene(v5, w3, assets);
    check(old.size() == 2 && old[0]->guid().valid() && old[1]->guid().valid() && old[0]->guid() != old[1]->guid(),
          "옛 v5: 새 GUID 를 받음");
    check(sceneOf(w3).find("GUID:" + old[0]->guid().toString()) != std::string::npos, "옛 파일도 다시 쓰면 GUID 가 남음");

    // 같은 GUID 가 둘
    const std::string dup = "# Haruka Scene v6\nACTOR:D1\nGUID:00000000000000d1\nEND_ACTOR\nACTOR:D2\nGUID:00000000000000d1\nEND_ACTOR\n"
                            "ACTOR:R\nCOMPONENT:RefProbeComponent\ntarget=actor:00000000000000d1\nEND_COMPONENT\nEND_ACTOR\n";
    World w4;
    const auto d = SceneSerializer::readScene(dup, w4, assets);
    check(d.size() == 3 && d[0]->guid().value == 0xd1 && d[1]->guid().value != 0xd1 && d[1]->guid().valid(),
          "같은 GUID 가 둘이면 뒤의 것에 새 값");
    check(d[2]->getComponent<RefProbeComponent>()->target.resolve(w4) == d[0], "그 GUID 의 참조는 먼저 읽은 액터");
    World w5;
    const auto bad = SceneSerializer::readScene("# Haruka Scene v6\nACTOR:B\nGUID:zz\nEND_ACTOR\n", w5, assets);
    check(bad.size() == 1 && bad[0] && bad[0]->guid().valid(), "깨진 GUID 는 경고 + 새 값");
}

// ── 5. Fresh 정책 ──────────────────────────────────────────────────────────────────
void testRemap(AssetManager& assets) {
    std::printf("\n== 5. remap ==\n");
    const std::string set =
        "ACTOR:actor:00000000000000a1\n"            // 이름이 토큰처럼 생겨도 바뀌지 않음
        "GUID:00000000000000a1\n"
        "END_ACTOR\n"
        "ACTOR:B\n"
        "GUID:00000000000000b1\n"
        "PARENT:0\n"
        "COMPONENT:RefProbeComponent\n"
        "target=actor:00000000000000a1\n"
        "END_COMPONENT\n"
        "COMPONENT:NotRegisteredComponent\n"
        "other=actor:00000000000000b1\n"
        "far=actor:00000000000000ee\n"
        "END_COMPONENT\n"
        "END_ACTOR\n";
    int n = 0;
    const std::string out = hguid::remapFresh(set, {}, &n);
    check(n == 2, "세트 안 GUID 2개를 바꿈");
    check(out.find("GUID:00000000000000a1") == std::string::npos && out.find("GUID:00000000000000b1") == std::string::npos,
          "GUID: 줄이 새 값");
    check(out.find("ACTOR:actor:00000000000000a1") != std::string::npos, "이름 줄은 그대로");
    check(out.find("far=actor:00000000000000ee") != std::string::npos, "세트 밖 참조는 그대로");
    check(out.find("target=actor:00000000000000a1") == std::string::npos && out.find("other=actor:00000000000000b1") == std::string::npos,
          "세트 안 참조(등록되지 않은 컴포넌트 포함)는 새 값");
    const std::string crlf = hguid::remapFresh("ACTOR:X\r\nGUID:00000000000000c1\r\nEND_ACTOR\r\n", {});
    check(crlf.find("GUID:00000000000000c1") == std::string::npos && crlf.find("\r\nEND_ACTOR\r\n") != std::string::npos,
          "CRLF 줄 끝을 지킴");

    // 같은 세트를 두 번 꺼내면 각자 자기 안을 가리킴(프리팹 두 번 꺼내기)
    World w;
    Actor* outside = w.spawnActor("Outside");
    w.assignGuid(outside, HGuid{ 0xee });
    const auto s1 = SceneSerializer::readScene(set, w, assets, SceneSerializer::GuidPolicy::Fresh);
    const auto s2 = SceneSerializer::readScene(set, w, assets, SceneSerializer::GuidPolicy::Fresh);
    check(s1.size() == 2 && s2.size() == 2 && s1[0] && s2[0], "두 번 꺼냄");
    auto* p1 = s1[1]->getComponent<RefProbeComponent>();
    auto* p2 = s2[1]->getComponent<RefProbeComponent>();
    check(p1 && p2 && p1->target.resolve(w) == s1[0] && p2->target.resolve(w) == s2[0], "각자 자기 사본 안을 가리킴");
    check(s1[0]->guid() != s2[0]->guid() && s1[0]->guid().value != 0xa1, "GUID 가 모두 다름");
    auto* u = dynamic_cast<UnknownComponent*>(s1[1]->getComponents().back().get());
    bool farKept = false;
    if (u) for (const auto& kv : u->rawFields) farKept = farKept || (kv.first == "far" && kv.second == "actor:00000000000000ee");
    check(u && farKept, "등록되지 않은 컴포넌트의 밖 참조는 그대로 남음");

    // 복제: 원본과 사본이 함께 있을 때 사본은 사본을, 밖 참조는 원래 대상을
    World w2;
    Actor* tgt = w2.spawnActor("T");
    Actor* hold = w2.spawnActor("H");
    Actor* ext = w2.spawnActor("Ext");
    auto* hp = hold->addComponent<RefProbeComponent>();
    hp->target.set(tgt);
    Actor* hold2 = w2.spawnActor("H2");
    hold2->addComponent<RefProbeComponent>()->target.set(ext);
    const std::string clip = SceneSerializer::writeActorSet({ tgt, hold, hold2 }, { -1, -1, -1 });
    const auto copy = SceneSerializer::readScene(clip, w2, assets, SceneSerializer::GuidPolicy::Fresh);
    check(copy.size() == 3 && copy[1]->getComponent<RefProbeComponent>()->target.resolve(w2) == copy[0], "복제한 쌍은 서로를 가리킴");
    check(copy[2]->getComponent<RefProbeComponent>()->target.resolve(w2) == ext, "세트 밖을 가리키던 값은 원래 대상");
    check(hp->target.resolve(w2) == tgt, "원본은 그대로");
}

// ── 6. Undo · Play ─────────────────────────────────────────────────────────────────
void testUndoPlay(AssetManager& assets) {
    std::printf("\n== 6. undo · play ==\n");
    World w;
    Actor* a = w.spawnActor("A");
    Actor* h = w.spawnActor("H");
    auto* p = h->addComponent<RefProbeComponent>();
    p->target.set(a);
    SnapshotWriter write = [](const std::vector<const Actor*>& o, const std::vector<int>& pi) { return SceneSerializer::writeActorSet(o, pi); };
    SnapshotReader read  = [&](const std::string& t) { return SceneSerializer::readScene(t, w, assets); };
    const ActorSnapshot snap = hsnap::capture(a, write);
    const HGuid ag = a->guid();
    w.destroyActor(a);
    check(p->target.resolve(w) == nullptr, "지우면 끊김");
    Actor* back = hsnap::restore(snap, w, read);
    check(back && back->guid() == ag && p->target.resolve(w) == back, "Undo 로 되살리면 다시 이어짐");

    // Play: 쓰기 → 모두 지우기 → 읽기(Keep)
    const std::string before = SceneSerializer::writeScene(w);
    std::vector<Actor*> all;
    for (const auto& x : w.getActors()) all.push_back(x.get());
    for (Actor* x : all) w.destroyActor(x);
    SceneSerializer::readScene(before, w, assets);
    check(SceneSerializer::writeScene(w) == before, "Play → Stop 뒤 같은 글(같은 GUID)");
    Actor* h2 = byName(w, "H");
    check(h2 && h2->getComponent<RefProbeComponent>()->target.resolve(w) == byName(w, "A"), "되돌린 뒤에도 참조가 이어짐");
}

// ── 7. 디테일 칸 · 고르기 목록 ──────────────────────────────────────────────────────
void testProps() {
    std::printf("\n== 7. props · picker ==\n");
    World w;
    Actor* a = w.spawnActor("Cube");
    Actor* b = w.spawnActor("Cube");
    Actor* c = w.spawnActor("Cube #2");
    Actor* cam = w.spawnActor("EditorCam");
    Actor* child = w.spawnActor("Child");
    child->attachTo(a);
    Actor* named = w.spawnActor("None");
    Actor* h = w.spawnActor("H");
    auto* p = h->addComponent<RefProbeComponent>();

    hcmd::PropValues before = hcmd::captureProps(*p);
    check(before.count(PropDesc::ActorRef) == 1 && before.v[0].g == 0, "capture: ActorRef 칸 값 0");
    p->target.set(b);
    hcmd::PropValues after = hcmd::captureProps(*p);
    check(after != before && after.v[0].g == b->guid().value, "capture: 대상 GUID");
    hcmd::applyProps(*p, before);
    check(!p->target.isSet(), "apply: 되돌림");
    hcmd::PropValue e; e.label = "Target"; e.kind = PropDesc::ActorRef; e.g = c->guid().value;
    check(hcmd::setProp(*p, e) && p->target.resolve(w) == c, "setProp: 여러 선택에 같은 값");

    const auto list = hpick::buildList(w, cam, "None");
    std::vector<std::string> labels;
    for (const auto& x : list) labels.push_back(x.label);
    const std::vector<std::string> want = { "None", "Cube", "Child", "Cube #3", "Cube #2", "None #2", "H" };
    check(labels == want, "목록: None + 아웃라이너 순서, 같은 이름 구분, 카메라 뺌");
    std::set<std::string> uniq(labels.begin(), labels.end());
    check(uniq.size() == labels.size(), "목록 글이 겹치지 않음");
    check(list[0].guid == 0 && list[3].guid == b->guid().value && list[5].guid == named->guid().value, "목록 항목의 GUID");
    check(hpick::labelFor(w, 0, "None", "(missing)") == "None" && hpick::labelFor(w, c->guid().value, "None", "(missing)") == "Cube #2" &&
          hpick::labelFor(w, 0xdead, "None", "(missing)") == "(missing)", "칸 글: None · 이름 · (missing)");
}

// ── 8. 에셋 참조 ───────────────────────────────────────────────────────────────────
struct TempProject {
    fs::path root;
    std::vector<hrefs::Root> roots;
    explicit TempProject(const std::string& name) {
        root = fs::temp_directory_path() / ("haruka_ref_test_" + name);
        std::error_code ec;
        fs::remove_all(root, ec);
        roots = { { "/Game", root / "Content" }, { "/Config", root / "Config" } };
        writeBytes(root / "Content/scenes/A.hscene",
                   "# Haruka Scene v6\r\nACTOR:Ship\r\nGUID:0000000000000001\r\nCOMPONENT:StaticMeshComponent\r\nmodel=/Game/models/m.glb\r\n"
                   "meshIndex=0\r\nEND_COMPONENT\r\nEND_ACTOR\r\nACTOR:Old\r\nMODEL:assets/models/m.glb\r\nEND_ACTOR\r\n"
                   "ACTOR:Gone\r\nCOMPONENT:StaticMeshComponent\r\nmodel=/Game/models/none.glb\r\nEND_COMPONENT\r\nEND_ACTOR\r\n");
        writeBytes(root / "Content/prefabs/P.hprefab",
                   "# Haruka Prefab v5\nACTOR:Beeper\nCOMPONENT:AudioComponent\nfile=/Game/audio/s.wav\nEND_COMPONENT\nEND_ACTOR\n");
        writeBytes(root / "Config/project.ini", "; comment /Game/scenes/A.hscene\nstartScene = /Game/scenes/A.hscene\n");
        writeBytes(root / "Content/models/m.glb", "glb");
        writeBytes(root / "Content/audio/s.wav", "wav");
        writeBytes(root / "Content/models/g/x.gltf", "{ \"buffers\": [ { \"uri\" : \"x.bin\" } ], \"images\": [ { \"uri\": \"tex/a%20b.png\" },"
                                                     " { \"uri\": \"data:image/png;base64,AAAA\" } ] }");
        writeBytes(root / "Content/models/g/x.bin", "bin");
        writeBytes(root / "Content/models/g/tex/a b.png", "png");
        writeBytes(root / "Source/Game.cpp", "// spawn\nconst char* kBeep = \"/Game/audio/s.wav\";\n");
    }
    ~TempProject() { std::error_code ec; fs::remove_all(root, ec); }
    hrefs::Index scan() const { return hrefs::scan(roots); }
};

void testAssetIndex() {
    std::printf("\n== 8. asset index ==\n");
    TempProject p("index");
    const hrefs::Index idx = p.scan();
    check(idx.assets.size() == 8, "루트 아래 파일 8개(Source 는 루트가 아님)");
    const auto mRefs = hrefs::referencesTo(idx, "/Game/models/m.glb");
    check(mRefs.size() == 2, "m.glb 참조 2개(새 형식 + 옛 assets/ 형식)");
    check(!mRefs.empty() && mRefs[0].file == "/Game/scenes/A.hscene" && mRefs[0].line == 5 && mRefs[0].key == "model", "파일 · 줄 · 키");
    check(hrefs::referencesTo(idx, "/Game/scenes/A.hscene").size() == 1, "project.ini 의 startScene(주석 줄은 뺌)");
    check(hrefs::referencesTo(idx, "/game/AUDIO/s.wav").size() == 1, "대소문자 무시로 찾음");
    check(hrefs::referencesTo(idx, "/Game/models", true).size() == 5, "폴더 아래 전부(glTF uri 포함)");
    const auto miss = hrefs::missing(idx, p.roots);
    check(miss.size() == 1 && miss[0].target == "/Game/models/none.glb", "없는 파일을 가리키는 참조 1개");
    const auto g = hrefs::referencesTo(idx, "/Game/models/g/tex/a b.png");
    check(g.size() == 1 && g[0].readOnly && g[0].file == "/Game/models/g/x.gltf", "glTF uri(퍼센트 풀기, data: 뺌)");
    check(hrefs::referencesTo(idx, "/Game/models/g/x.bin").size() == 1, "glTF 버퍼 uri");
}

void testAssetMove() {
    std::printf("\n== 9. asset move ==\n");
    {
        TempProject p("move");
        const std::string before = readBytes(p.root / "Content/scenes/A.hscene");
        hrefs::Index idx = p.scan();
        const auto plan = hrefs::planMove(idx, p.roots, "/Game/models/m.glb", "/Game/models/sub/Ship.glb", { p.root / "Source" });
        check(plan.ok() && plan.edits.size() == 1 && plan.valueCount() == 2, "계획: 파일 하나에 값 둘");
        std::string err;
        check(hrefs::applyMove(plan, p.roots, &err), "옮기기 성공 " + err);
        check(fs::exists(p.root / "Content/models/sub/Ship.glb") && !fs::exists(p.root / "Content/models/m.glb"), "파일이 옮겨짐");
        std::string want = before;
        want.replace(want.find("/Game/models/m.glb"), 18, "/Game/models/sub/Ship.glb");
        want.replace(want.find("assets/models/m.glb"), 19, "/Game/models/sub/Ship.glb");
        check(readBytes(p.root / "Content/scenes/A.hscene") == want, "값 글자만 바뀜(CRLF · 나머지 바이트 그대로)");
        idx = p.scan();
        check(hrefs::referencesTo(idx, "/Game/models/sub/Ship.glb").size() == 2 && hrefs::missing(idx, p.roots).size() == 1,
              "다시 훑으면 새 경로 2개, 없는 참조는 처음 그대로 1개");

        const auto s = hrefs::planMove(idx, p.roots, "/Game/audio/s.wav", "/Game/audio/beep.wav", { p.root / "Source" });
        check(s.ok() && s.codeWarnings.size() == 1 && s.codeWarnings[0].find("Game.cpp:2") != std::string::npos, "C++ 소스의 같은 문자열을 보고");

        const auto sc = hrefs::planMove(idx, p.roots, "/Game/scenes/A.hscene", "/Game/scenes/한글 씬.hscene");
        check(sc.ok() && hrefs::applyMove(sc, p.roots, &err), "씬 이름 바꾸기(한글)");
        check(readBytes(p.root / "Config/project.ini").find("startScene = /Game/scenes/한글 씬.hscene") != std::string::npos,
              "project.ini 의 시작 씬도 고침(주석은 그대로)");
        check(readBytes(p.root / "Config/project.ini").find("; comment /Game/scenes/A.hscene") != std::string::npos, "주석 줄은 그대로");
    }
    {
        TempProject p("folder");
        hrefs::Index idx = p.scan();
        auto f = hrefs::planMove(idx, p.roots, "/Game/models/g", "/Game/ships/g");
        check(f.ok() && f.folder, "폴더 옮기기 허용(glTF 와 그 파일이 함께 감)");
        check(hrefs::mapPath(f, "/Game/models/g/tex/a b.png") == "/Game/ships/g/tex/a b.png" && hrefs::mapPath(f, "/Game/models/gx") == "/Game/models/gx",
              "mapPath: 폴더 아래만");
        check(!hrefs::planMove(idx, p.roots, "/Game/models/g/x.bin", "/Game/x.bin").ok(), "glTF 가 따로 가리키는 파일은 거절");
        check(!hrefs::planMove(idx, p.roots, "/Game/models/g/x.gltf", "/Game/x.gltf").ok(), "glTF 만 다른 폴더로는 거절");
        check(hrefs::planMove(idx, p.roots, "/Game/models/g/x.gltf", "/Game/models/g/y.gltf").ok(), "같은 폴더 안 이름 바꾸기는 허용");
        check(!hrefs::planMove(idx, p.roots, "/Game/models/m.glb", "/Game/audio/s.wav").ok(), "확장자 바꾸기 · 이미 있는 대상 거절");
        check(!hrefs::planMove(idx, p.roots, "/Game/models/m.glb", "/Game/models/m.fbx").ok(), "확장자 바꾸기 거절");
        check(!hrefs::planMove(idx, p.roots, "/Game/models/m.glb", "/Game/models/a:b.glb").ok(), "금지 글자 거절");
        check(!hrefs::planMove(idx, p.roots, "/Config/project.ini", "/Config/x.ini").ok(), "/Game 밖은 거절");
        check(!hrefs::planMove(idx, p.roots, "/Game/models", "/Game/models/inner").ok(), "폴더를 자기 안으로 거절");
        check(!hrefs::planMove(idx, p.roots, "/Game/models/zzz.glb", "/Game/models/y.glb").ok(), "없는 원본 거절");

        // 옮기기가 실패하면 고친 파일을 되돌림: 대상 폴더를 계획 뒤에 만들어 rename 이 실패하게 함
        const std::string scene = readBytes(p.root / "Content/scenes/A.hscene");
        auto fm = hrefs::planMove(idx, p.roots, "/Game/models", "/Game/meshes");
        writeBytes(p.root / "Content/meshes/blocker.txt", "x");
        std::string err;
        check(fm.ok() && !hrefs::applyMove(fm, p.roots, &err) && readBytes(p.root / "Content/scenes/A.hscene") == scene,
              "옮기기 실패 → 참조 파일 되돌림");
        // 훑은 뒤 파일이 바뀌면 멈춤
        auto mv = hrefs::planMove(idx, p.roots, "/Game/models/m.glb", "/Game/models/m2.glb");
        writeBytes(p.root / "Content/scenes/A.hscene", "# changed\n" + scene);
        check(mv.ok() && !hrefs::applyMove(mv, p.roots, &err) && fs::exists(p.root / "Content/models/m.glb"), "훑은 뒤 바뀐 파일이면 멈춤");
    }
}

// ── 10. 샘플 프로젝트 ──────────────────────────────────────────────────────────────
void testSamples() {
    std::printf("\n== 10. samples ==\n");
    const fs::path proj = HARUKA_SAMPLES_DIR;
    const std::vector<hrefs::Root> roots = { { "/Game", proj / "Content" }, { "/Config", proj / "Config" } };
    const hrefs::Index idx = hrefs::scan(roots);
    const auto miss = hrefs::missing(idx, roots);
    for (const auto& r : miss) std::printf("    missing: %s:%d %s\n", r.file.c_str(), r.line, r.target.c_str());
    check(!idx.refs.empty() && miss.empty(), "샘플 프로젝트에 끊긴 참조 없음(참조 " + std::to_string(idx.refs.size()) + "개)");
}

// ── 11. 샘플의 쓰는 예(LookAtComponent, R-8) ────────────────────────────────────────
// 이 시험은 핵심 컴포넌트를 등록하지 않으므로 씬의 메시 · 라이트 블록은 UnknownComponent 로 남고 에셋을 읽지 않습니다.
void testLookAt(AssetManager& assets) {
    std::printf("\n== 11. sample LookAtComponent ==\n");
    ComponentRegistry::get().registerType<LookAtComponent>("LookAtComponent", "Look At");
    const std::string text = readBytes(fs::path(HARUKA_SAMPLES_DIR) / "Content/scenes/ReferenceProbe.hscene");
    World w;
    SceneSerializer::readScene(text, w, assets);
    Actor* target  = byName(w, "Target");
    Actor* watcher = byName(w, "Watcher");
    Actor* orphan  = byName(w, "Orphan");
    auto* la = watcher ? watcher->getComponent<LookAtComponent>() : nullptr;
    auto* lo = orphan ? orphan->getComponent<LookAtComponent>() : nullptr;
    check(target && la && lo, "ReferenceProbe 를 읽음");
    if (!target || !la || !lo) return;
    check(la->target.resolve(w) == target && lo->target.isSet() && lo->target.resolve(w) == nullptr,
          "Watcher → Target, Orphan 은 없는 액터를 가리킴");
    const std::string once = SceneSerializer::writeScene(w);
    World w2;
    SceneSerializer::readScene(once, w2, assets);
    check(SceneSerializer::writeScene(w2) == once && once.find("target=actor:5a1e0c7d00000003") != std::string::npos, "씬 왕복");
    w.tick(1.f / 72.f);
    glm::vec3 want = target->worldPosition() - watcher->worldPosition();
    want.y = 0.f;
    const glm::vec3 fwd = watcher->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
    check(glm::dot(glm::normalize(want), fwd) > 0.9999f, "Play 틱: -Z 가 대상(세로축만)을 향함");
    const glm::quat oq = orphan->worldRotation();
    check(std::abs(oq.w) > 0.9999f, "대상을 못 찾으면 돌지 않음");
}

} // namespace

int main() {
    ComponentRegistry::get().registerType<RefProbeComponent>("RefProbeComponent");
    AssetManager assets(nullptr);   // 메시가 없는 씬만 읽으므로 렌더러를 부르지 않습니다
    testGuid();
    testWorld();
    testRef();
    testFile(assets);
    testRemap(assets);
    testUndoPlay(assets);
    testProps();
    testAssetIndex();
    testAssetMove();
    testSamples();
    testLookAt(assets);
    std::printf("\n%s (%d passed, %d failed)\n", g_failed ? "FAILED" : "ALL PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
