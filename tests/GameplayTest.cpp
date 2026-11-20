// 게임플레이 기본 도구 · CVar · 콘솔 시험(2026-11-20, 정본 Docs/Gameplay_Tools.md 3절 T1 ~ T3).
// 게임 시간(배율) · 타이머 · 물리 뒤 틱 · 태그(색인 · 씬 파일) · 이벤트 버스 · CVar(등록 · 범위 · 보관 · 콜백) · 콘솔 명령을 창 · GPU 없이
// 검산합니다.

#include "../src/assets/AssetManager.hpp"
#include "../src/core/CVar.hpp"
#include "../src/scene/Component.hpp"
#include "../src/scene/World.hpp"
#include "../src/sceneio/SceneSerializer.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed;
    else ++g_failed;
}

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// 틱을 기록하는 컴포넌트.
struct Probe : Component {
    std::vector<float>* log = nullptr;
    int starts = 0;
    void onStart() override { ++starts; }
    void tick(float dt) override { if (log) log->push_back(dt); }
    const char* typeName() const override { return "Probe"; }
};

// ── T1 게임 시간 · 타이머 · 틱 ─────────────────────────────────────────────────
void testTime() {
    std::printf("\n== T1-1 game time ==\n");
    World w;
    std::vector<float> dts;
    Actor* a = w.spawnActor("A");
    auto* p = a->addComponent<Probe>();
    p->log = &dts;
    w.tick(0.016f);
    check(dts.size() == 1 && dts[0] == 0.016f && w.lastDt() == 0.016f, "배율 1: 받은 dt 그대로(비트 같음)");
    w.setTimeScale(0.5f);
    w.tick(0.016f);
    check(dts[1] == 0.008f && near(w.gameTime(), 0.024, 1e-7), "배율 0.5: 반, 게임 시간 누적");
    w.setTimeScale(0.f);
    w.tick(0.016f);
    check(dts[2] == 0.f && near(w.gameTime(), 0.024, 1e-7), "배율 0: dt 0(틱은 돎)");
    w.setTimeScale(-3.f);
    check(w.timeScale() == 0.f, "음수는 0");
    w.setTimeScale(1000.f);
    check(w.timeScale() == 100.f, "100 이 상한");
    w.setTimeScale(std::nanf(""));
    check(w.timeScale() == 1.f, "NaN 은 1");
    World w2;
    Actor* b = w2.spawnActor("B");
    auto* q = b->addComponent<Probe>();
    w2.setTimeScale(0.f);
    w2.tick(0.1f);
    check(q->starts == 1, "배율 0 이어도 onStart 는 불림");
}

void testTimers() {
    std::printf("\n== T1-2 ~ T1-4 timers ==\n");
    World w;
    int fired = 0;
    const TimerHandle h = w.timers().after(0.05f, [&] { ++fired; });
    check(h != 0 && w.timers().active(h) && near(w.timers().remaining(h), 0.05, 1e-6), "after: 핸들 · active · remaining");
    for (int i = 0; i < 2; ++i) w.tick(0.02f);   // 0.04
    check(fired == 0, "0.04 초에는 아직");
    w.tick(0.02f);   // 0.06
    check(fired == 1 && !w.timers().active(h) && w.timers().remaining(h) < 0.f, "0.06 초에 한 번, 그 뒤 비활성");
    w.tick(1.f);
    check(fired == 1, "한 번만");

    // 배율 0.5 면 두 배 늦게.
    World ws;
    ws.setTimeScale(0.5f);
    int f2 = 0;
    ws.timers().after(0.1f, [&] { ++f2; });
    for (int i = 0; i < 9; ++i) ws.tick(0.02f);   // 게임 시간 0.09
    check(f2 == 0, "배율 0.5: 실제 0.18 초에는 아직");
    ws.tick(0.02f);
    ws.tick(0.02f);   // 0.11
    check(f2 == 1, "배율 0.5: 실제 0.22 초에 부름");
    // 틱하지 않으면(일시정지) 부르지 않음.
    World wp;
    int f3 = 0;
    wp.timers().after(0.01f, [&] { ++f3; });
    check(f3 == 0 && wp.timers().count() == 1, "틱하지 않으면(일시정지) 부르지 않음");

    // every: 큰 dt 에서 지난 횟수만큼, 밀리지 않음, 상한.
    World we;
    int n = 0;
    const TimerHandle e = we.timers().every(0.1f, [&] { ++n; });
    we.tick(0.35f);
    check(n == 3 && near(we.timers().remaining(e), 0.05, 1e-5), "every 0.1: 0.35 초에 세 번, 다음은 0.4(밀리지 않음)");
    we.tick(0.06f);
    check(n == 4, "0.41 초에 네 번째");
    we.tick(5.f);
    check(n == 4 + TimerManager::kMaxCallsPerTick && near(we.timers().remaining(e), 0.1, 1e-5),
          "한 틱 상한 16 번, 넘은 몫은 버리고 지금부터 다시 셈");
    int m = 0;
    we.timers().every(1.f, [&] { ++m; }, nullptr, 0.f);
    we.tick(0.001f);
    check(m == 1, "첫 지연 0 이면 다음 틱에 바로");

    // 주인 액터를 지우면 취소.
    World wo;
    Actor* owner = wo.spawnActor("Owner");
    int fo = 0;
    const TimerHandle ho = wo.timers().after(0.1f, [&] { ++fo; }, owner);
    wo.timers().every(0.1f, [&] { ++fo; }, owner);
    wo.destroyActor(owner);
    wo.tick(1.f);
    check(fo == 0 && !wo.timers().active(ho) && wo.timers().count() == 0, "주인 액터를 지우면 함께 취소");

    // 콜백 안에서 자기 취소 · 새 타이머 · 액터 삭제.
    World wc;
    Actor* victim = wc.spawnActor("Victim");
    int self = 0, inner = 0;
    TimerHandle hs = 0;
    hs = wc.timers().every(0.01f, [&] {
        ++self;
        wc.timers().cancel(hs);
        wc.timers().after(0.f, [&] { ++inner; });
        wc.destroyActor(victim);
    });
    wc.tick(0.05f);
    check(self == 1 && inner == 0 && wc.getActors().empty(), "콜백 안 자기 취소 · 삭제(틱 끝에 지움) · 새 타이머는 다음 틱");
    wc.tick(0.01f);
    check(inner == 1, "콜백 안에서 더한 타이머는 다음 틱에 부름");
    int cc = 0;
    const TimerHandle hc = wc.timers().after(1.f, [&] { ++cc; });
    check(wc.timers().cancel(hc) && !wc.timers().cancel(hc) && !wc.timers().cancel(0), "cancel: 처음만 true");
    wc.timers().after(0.f, [&] { ++cc; });
    wc.timers().after(0.f, [&] { wc.timers().clear(); ++cc; });
    wc.timers().after(0.f, [&] { ++cc; });
    wc.tick(0.01f);
    check(cc == 2 && wc.timers().count() == 0, "콜백 안 clear: 뒤의 타이머는 부르지 않음");
    check(wc.timers().after(1.f, nullptr) == 0, "빈 함수는 0");
}

void testPostPhysics() {
    std::printf("\n== T1-5 post physics ==\n");
    World w;
    std::vector<float> pre, post;
    Actor* a = w.spawnActor("A");
    a->addComponent<Probe>()->log = &pre;
    w.tick(0.01f);
    w.tickPostPhysics();
    check(pre.size() == 1 && post.empty(), "PostPhysics 컴포넌트가 없으면 두 번째 훑기에서 아무것도 안 함");
    Actor* b = w.spawnActor("B");
    auto* lp = b->addComponent<Probe>();
    lp->tickGroup = ETickGroup::PostPhysics;
    lp->log = &post;
    w.setTimeScale(2.f);
    w.tick(0.01f);
    check(pre.size() == 2 && post.empty(), "PostPhysics 는 tick 에서 틱하지 않음");
    w.tickPostPhysics();
    check(post.size() == 1 && post[0] == 0.02f && lp->starts == 1, "tickPostPhysics 에서 같은 dt(배율 적용), onStart 한 번");
    w.tick(0.01f);
    w.tickPostPhysics();
    check(post.size() == 2 && lp->starts == 1, "다음 프레임도 한 번씩");
    b->setActive(false);
    w.tick(0.01f);
    w.tickPostPhysics();
    check(post.size() == 2, "비활성 액터는 틱하지 않음");
}

// ── T2 태그 · 이벤트 버스 ─────────────────────────────────────────────────────────
void testTags(AssetManager& assets) {
    std::printf("\n== T2 tags ==\n");
    World w;
    Actor* a = w.spawnActor("A");
    Actor* b = w.spawnActor("B");
    Actor* c = w.spawnActor("C");
    check(a->addTag("enemy") && !a->addTag("enemy") && a->tags().size() == 1, "addTag 중복 무시");
    check(!a->addTag("") && !a->addTag("has space") && !a->addTag("a,b") && !a->addTag(std::string(65, 'x')), "틀린 이름 거절");
    check(a->addTag("boss.level-1") && a->hasTag("boss.level-1") && !a->hasTag("Enemy"), "점 · 하이픈 됨, 대소문자 가림");
    c->addTag("enemy");
    const auto& e1 = w.actorsWithTag("enemy");
    check(e1.size() == 2 && e1[0] == a && e1[1] == c, "actorsWithTag: 액터 순서");
    const auto* p1 = &w.actorsWithTag("enemy");
    check(p1 == &e1 && w.actorsWithTag("none").empty() && w.firstWithTag("enemy") == a && !w.firstWithTag("none"),
          "같은 버전이면 같은 목록(캐시), firstWithTag");
    b->addTag("enemy");
    check(w.actorsWithTag("enemy").size() == 3, "태그를 더하면 다시 만듦");
    check(a->removeTag("enemy") && !a->removeTag("enemy") && w.actorsWithTag("enemy").size() == 2, "removeTag");
    w.destroyActor(c);
    check(w.actorsWithTag("enemy").size() == 1 && w.actorsWithTag("enemy")[0] == b, "액터를 지우면 다시 만듦");
    w.spawnActor("D")->addTag("enemy");
    check(w.actorsWithTag("enemy").size() == 2, "새 액터");

    // 씬 파일 왕복.
    World plain;
    plain.spawnActor("NoTags");
    const std::string plainText = SceneSerializer::writeScene(plain);
    check(plainText.find("TAGS:") == std::string::npos, "태그 없는 씬은 TAGS 줄이 없음(예전과 같은 글)");
    const std::string text = SceneSerializer::writeScene(w);
    check(text.find("TAGS:boss.level-1\n") != std::string::npos && text.find("TAGS:enemy\n") != std::string::npos, "TAGS: 줄");
    World r;
    SceneSerializer::readScene(text, r, assets);
    check(r.actorsWithTag("enemy").size() == 2 && r.actorsWithTag("boss.level-1").size() == 1, "다시 읽은 태그");
    check(SceneSerializer::writeScene(r) == text, "다시 쓰면 같은 글");
    World bad;
    SceneSerializer::readScene("# Haruka Scene v6\nACTOR:X\nTAGS: a , bad tag,b\nEND_ACTOR\n", bad, assets);
    check(bad.getActors().size() == 1 && bad.getActors()[0]->tags() == std::vector<std::string>{ "a", "b" }, "공백 정리 · 틀린 태그는 건너뜀");
}

struct Hit { int damage; };
struct Heal { int amount; };

void testEvents() {
    std::printf("\n== T2-4 event bus ==\n");
    World w;
    EventBus& bus = w.events();
    std::vector<std::string> order;
    int heal = 0;
    {
        EventSubscription s1 = bus.subscribe<Hit>([&](const Hit& h) { order.push_back("a" + std::to_string(h.damage)); });
        EventSubscription s2 = bus.subscribe<Hit>([&](const Hit& h) { order.push_back("b" + std::to_string(h.damage)); });
        EventSubscription s3 = bus.subscribe<Heal>([&](const Heal& h) { heal += h.amount; });
        check(bus.publish(Hit{ 5 }) == 2 && order == std::vector<std::string>{ "a5", "b5" } && heal == 0, "형별 전달 · 구독 순서");
        check(bus.publish(Heal{ 3 }) == 1 && heal == 3, "다른 형");
        check(bus.listenerCount<Hit>() == 2, "listenerCount");
    }
    check(bus.listenerCount<Hit>() == 0 && bus.publish(Hit{ 1 }) == 0, "구독 객체를 지우면 해제");

    // 발행 중 해제 · 구독 · 재발행.
    order.clear();
    EventSubscription later;
    EventSubscription second;
    EventSubscription first = bus.subscribe<Hit>([&](const Hit& h) {
        order.push_back("first" + std::to_string(h.damage));
        if (h.damage == 1) {
            second.reset();   // 뒤의 리스너를 발행 중에 해제
            later = bus.subscribe<Hit>([&](const Hit& x) { order.push_back("later" + std::to_string(x.damage)); });
            bus.publish(Hit{ 2 });   // 다시 발행(안쪽)
        }
    });
    second = bus.subscribe<Hit>([&](const Hit& h) { order.push_back("second" + std::to_string(h.damage)); });
    bus.publish(Hit{ 1 });
    check(order == std::vector<std::string>{ "first1", "first2", "later2" }, "발행 중 해제는 바로 빠지고, 더한 구독은 바깥 발행을 받지 않음");
    check(bus.listenerCount<Hit>() == 2, "끝나면 정리(해제한 것 없음)");
    EventSubscription moved = std::move(first);
    check(!first.active() && moved.active() && bus.listenerCount<Hit>() == 2, "구독 객체 이동");
    moved = EventSubscription();
    check(bus.listenerCount<Hit>() == 1, "빈 것을 대입하면 해제");
}

// ── T3 CVar · 콘솔 ────────────────────────────────────────────────────────────
void testCVars() {
    std::printf("\n== T3 cvars ==\n");
    CVarRegistry r;
    int changes = 0;
    float seen = 0.f;
    auto* scale = r.addFloat("time.scale", 1.f, 0.f, 100.f, "World time scale", [&](const CVarRegistry::CVar& v) { ++changes; seen = v.asFloat(); });
    auto* lines = r.addBool("debug.lines", true, "Debug lines");
    auto* count = r.addInt("game.count", 3, 0, 10, "Count");
    auto* label = r.addString("game.label", "hello", "Label");
    check(scale && lines && count && label && scale->asFloat() == 1.f && lines->asBool() && count->asInt() == 3 && label->asString() == "hello",
          "등록 · 기본값");
    check(!r.addBool("time.scale", false, "") && !r.addBool("TIME.SCALE", false, "") && !r.addBool("bad name", false, "") && !r.addBool(".x", false, ""),
          "중복(대소문자 무시) · 틀린 이름 거절");
    check(r.find("Time.Scale") == scale, "대소문자 무시 찾기");
    std::string why;
    check(r.set("time.scale", "0.5", CVarRegistry::Source::Code, &why) && scale->asFloat() == 0.5f && scale->source == CVarRegistry::Source::Code,
          "set · 바꾼 곳");
    check(changes == 0, "콜백은 바로 부르지 않음");
    check(r.dispatchChanges() == 1 && changes == 1 && seen == 0.5f, "dispatchChanges 에서 한 번");
    check(r.dispatchChanges() == 0 && changes == 1, "바뀐 것이 없으면 부르지 않음");
    r.set("time.scale", "0.5", CVarRegistry::Source::Console);
    check(r.dispatchChanges() == 0, "같은 값은 콜백 없음");
    why.clear();
    check(r.set("time.scale", "500", CVarRegistry::Source::Console, &why) && scale->asFloat() == 100.f && !why.empty(), "범위 밖은 잘라 알림: " + why);
    check(!r.set("time.scale", "fast", CVarRegistry::Source::Console, &why) && scale->asFloat() == 100.f, "틀린 형 거절: " + why);
    check(r.set("debug.lines", "off", CVarRegistry::Source::Console) && !lines->asBool() && lines->text() == "false", "bool: on/off · true/false · 1/0");
    check(r.set("game.count", "-4", CVarRegistry::Source::Console) && count->asInt() == 0, "int 범위");
    check(!r.set("nobody", "1", CVarRegistry::Source::Console, &why), "콘솔 · 코드에서 모르는 이름은 false: " + why);

    // 등록 전 값(명령줄 · ini) 보관.
    CVarRegistry r2;
    std::vector<std::string> msgs;
    r2.loadIni("# comment\ngame.speed = 2.5\nbroken\ngame.mode=hard\n", msgs);
    check(msgs.size() == 1, "ini: = 없는 줄만 알림(보관한 이름은 시작 끝에 한 번에)");
    check(r2.set("game.speed", "3", CVarRegistry::Source::CommandLine, &why) && !why.empty(), "명령줄도 보관(나중 것이 이김)");
    auto names = r2.pendingNames();
    check(names.size() == 2, "보관 중인 이름 2개");
    int cb = 0;
    auto* speed = r2.addFloat("game.speed", 1.f, 0.f, 10.f, "Speed", [&](const CVarRegistry::CVar&) { ++cb; });
    check(speed->asFloat() == 3.f && speed->source == CVarRegistry::Source::CommandLine && speed->defaultText == "1", "등록할 때 보관한 값 적용 · 출처 · 기본값 글");
    check(r2.dispatchChanges() == 1 && cb == 1, "등록할 때 적용한 값도 콜백 한 번");
    check(r2.pendingNames() == std::vector<std::string>{ "game.mode" }, "끝까지 모르는 이름이 남음(엔진이 알림)");

    // 콘솔 명령.
    std::printf("\n== T3-4 console ==\n");
    std::vector<std::string> gotArgs;
    check(r.addCommand("give", "give <item> [count]", [&](const std::vector<std::string>& a) { gotArgs = a; return std::string("given"); }),
          "명령 등록");
    check(!r.addCommand("help", "", [](const auto&) { return std::string(); }) && !r.addCommand("time.scale", "", [](const auto&) { return std::string(); }),
          "기본 명령 · 변수 이름과 겹치면 거절");
    check(r.execute("give \"iron sword\" 2") == "given" && gotArgs == std::vector<std::string>{ "iron sword", "2" }, "따옴표 인자");
    std::string out = r.execute("time.scale 0.25");
    check(out == "time.scale = 0.25" && scale->asFloat() == 0.25f && scale->source == CVarRegistry::Source::Console, "<변수> <값>: " + out);
    out = r.execute("TIME.SCALE");
    check(out.find("time.scale = 0.25") == 0 && out.find("default 1") != std::string::npos && out.find("console") != std::string::npos,
          "<변수>: 값 · 기본값 · 바꾼 곳 · 도움말: " + out);
    out = r.execute("game.label two words");
    check(label->asString() == "two words", "문자열 값은 나머지 인자를 공백으로 이음");
    out = r.execute("time.scale abc");
    check(out.rfind("error:", 0) == 0, "틀린 값: " + out);
    out = r.execute("reset time.scale");
    check(scale->asFloat() == 1.f && scale->source == CVarRegistry::Source::Default && out.find("(default)") != std::string::npos, "reset: " + out);
    out = r.execute("list game.");
    check(out.find("game.count") != std::string::npos && out.find("game.label") != std::string::npos && out.find("time.scale") == std::string::npos,
          "list 앞부분");
    out = r.execute("help");
    check(out.find("give") != std::string::npos && out.find("4 variables") != std::string::npos, "help 목록");
    check(r.execute("help give").find("give <item>") != std::string::npos && r.execute("help time.scale").find("World time scale") != std::string::npos,
          "help <이름>");
    out = r.execute("frobnicate");
    check(out.find("unknown") != std::string::npos, "모르는 명령: " + out);
    check(r.execute("   ").empty(), "빈 줄은 빈 결과");
    check(CVarRegistry::splitArgs("  a  \"b c\" \"\" d") == std::vector<std::string>{ "a", "b c", "", "d" }, "splitArgs(빈 따옴표 포함)");
}

} // namespace

int main() {
    AssetManager assets(nullptr);   // 메시 없는 씬만 읽음
    testTime();
    testTimers();
    testPostPhysics();
    testTags(assets);
    testEvents();
    testCVars();
    std::printf("\n%s (%d passed, %d failed)\n", g_failed ? "FAILED" : "ALL PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
