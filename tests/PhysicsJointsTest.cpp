// 물리 조인트 · 충돌 이벤트 · 레이어 시험(2026-11-20, 정본 Docs/Physics_Joints_And_Events.md 3절 J1 ~ J5).
// 레이어 표 해석 · 레이어로 무시한 쌍 · 트리거 들어감 · 나감 · 접촉 시작 · 유지 · 끝 · 알림 중 삭제 · 조인트 네 종류 · 상대 삭제 ·
// 셰이프 캐스트 · 씬 파일 왕복 · 확인 장면(PhysicsJointProbe.hscene)을 창 · GPU 없이 검산합니다.

#include "../src/assets/AssetManager.hpp"
#include "../src/physics/JointComponent.hpp"
#include "../src/physics/PhysicsLayers.hpp"
#include "../src/physics/PhysicsSystem.hpp"
#include "../src/physics/RigidBodyComponent.hpp"
#include "../src/core/ProjectSettings.hpp"
#include "../src/scene/Actor.hpp"
#include "../src/scene/ComponentFields.hpp"
#include "../src/scene/World.hpp"
#include "../src/sceneio/SceneSerializer.hpp"
#include "../src/components/CoreComponents.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
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

constexpr float kDt = 1.f / 60.f;

void run(PhysicsSystem& ps, int frames) {
    for (int i = 0; i < frames; ++i) ps.tick(kDt);
}

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

// 월드 + 물리 한 벌. 물리를 먼저 끝내 월드의 몸 · 조인트를 지웁니다(월드 소멸은 onDestroy 를 부르지 않음).
struct Scene {
    World         w;
    PhysicsSystem ps;
    explicit Scene(const PhysicsLayerTable* layers = nullptr) {
        if (layers) ps.setLayers(*layers);
        ps.startup();
        ps.setWorld(&w);
    }
    ~Scene() { ps.shutdown(); }

    Actor* box(const char* name, glm::vec3 pos, glm::vec3 scale, EMotionType m, int layer = 0) {
        Actor* a = w.spawnActor(name);
        a->setPosition(pos);
        a->setScale(scale);
        auto* rb = a->addComponent<RigidBodyComponent>();
        rb->motionType = m;
        rb->layer      = layer;
        return a;
    }
    // 바닥은 튀지 않게 둡니다(Jolt 는 반발 계수의 큰 쪽을 씀).
    Actor* ground() {
        Actor* g = box("Ground", { 0.f, -0.1f, 0.f }, { 20.f, 0.2f, 20.f }, EMotionType::Static);
        g->getComponent<RigidBodyComponent>()->restitution = 0.f;
        return g;
    }
};

RigidBodyComponent* rbOf(Actor* a) { return a->getComponent<RigidBodyComponent>(); }

// ── J1 레이어 표 ───────────────────────────────────────────────────────────────
void testLayerTable() {
    std::printf("\n== J1 layer table ==\n");
    PhysicsLayerTable def;
    bool all = true;
    for (int a = 0; a < PhysicsLayerTable::kCount; ++a)
        for (int b = 0; b < PhysicsLayerTable::kCount; ++b) all = all && def.collides(a, b);
    check(all && def.names[0] == "Default" && def.names[15] == "Layer15", "기본 표: 16 슬롯 모두 부딪힘, 0 = Default");

    std::vector<std::string> msgs;
    const std::string ini =
        "physics.layer.1 = Debris\n"
        "physics.layer.2 = Player\n"
        "physics.ignore = Debris, Debris\n"
        "physics.ignore = Player , Debris\n"
        "physics.layer.0 = Nope\n"
        "physics.layer.3 = bad name!\n"
        "physics.layer.4 = Player\n"
        "physics.ignore = Debris\n"
        "physics.ignore = Debris, Ghost\n"
        "physics.layer.1.android = X\n";
    const ProjectSettings ps = project_settings::parse(ini, msgs);
    check(ps.physics.size() == 9 && msgs.size() == 1, "project.ini 의 physics. 줄을 모음(접미사 줄은 거부) " + std::to_string(ps.physics.size()));
    msgs.clear();
    const PhysicsLayerTable t = PhysicsLayerTable::parse(ps.physics, msgs);
    check(t.names[1] == "Debris" && t.names[2] == "Player" && t.find("Player") == 2 && t.find("3") == 3, "이름 · 번호로 찾기");
    check(!t.collides(1, 1) && !t.collides(1, 2) && !t.collides(2, 1) && t.collides(2, 2) && t.collides(0, 1),
          "ignore 줄은 대칭으로 뺌, 나머지는 그대로");
    check(msgs.size() == 5, "잘못된 줄 5개(슬롯 0 · 이름 글자 · 이름 중복 · 쉼표 없음 · 모르는 레이어) 알림 " + std::to_string(msgs.size()));
    check(PhysicsLayerTable::bit(3) == 8 && PhysicsLayerTable::bit(16) == 0, "레이어 비트");
}

// ── J1 레이어로 무시한 쌍: 통과 · 접촉 없음 ───────────────────────────────────────
void testLayerIgnore() {
    std::printf("\n== J1 layer ignore ==\n");
    PhysicsLayerTable t;
    t.names[1] = "Debris";
    t.setCollides(1, 0, false);   // Debris 는 Default(바닥)와 부딪히지 않음
    Scene s(&t);
    s.ground();
    Actor* debris = s.box("Debris", { -1.f, 1.f, 0.f }, glm::vec3(0.3f), EMotionType::Dynamic, 1);
    Actor* normal = s.box("Normal", {  1.f, 1.f, 0.f }, glm::vec3(0.3f), EMotionType::Dynamic, 0);
    int debrisEvents = 0, normalBegin = 0, normalStay = 0, normalEnd = 0;
    rbOf(debris)->contactEvents = true;
    rbOf(normal)->contactEvents = true;
    rbOf(normal)->restitution   = 0.f;   // 튀지 않게(튀면 시작 · 끝이 여러 번)
    rbOf(debris)->onContactBegin = [&](const PhysicsContact&) { ++debrisEvents; };
    rbOf(normal)->onContactBegin = [&](const PhysicsContact& c) { ++normalBegin; (void)c; };
    rbOf(normal)->onContactStay  = [&](const PhysicsContact&) { ++normalStay; };
    rbOf(normal)->onContactEnd   = [&](const PhysicsContact&) { ++normalEnd; };
    run(s.ps, 90);
    check(debris->worldPosition().y < -1.f, "Debris 레이어 상자는 바닥을 통과 (y " + fmt(debris->worldPosition().y) + ")");
    check(debrisEvents == 0, "통과한 쌍은 접촉 알림 없음");
    check(std::fabs(normal->worldPosition().y - 0.15f) < 0.03f, "Default 상자는 바닥 위에 섬 (y " + fmt(normal->worldPosition().y) + ")");
    check(normalBegin == 1 && normalEnd == 0 && normalStay > 0,
          "바닥 접촉: 시작 1 · 유지 " + std::to_string(normalStay) + " · 끝 0 (시작 " + std::to_string(normalBegin) + ")");
    // 잠든 뒤에도 끝이 오지 않고(잠듦은 끝이 아님), 쿼리 마스크도 레이어를 따름
    run(s.ps, 240);
    PhysicsSystem::BodyInfo bi;
    s.ps.queryBody(rbOf(normal)->bodyId_, bi);
    check(!bi.active && normalEnd == 0, "잠든 상자: 끝 알림 없음 (active " + std::to_string(bi.active) + ")");
    PhysicsSystem::RaycastHit hit;
    const bool hitAll  = s.ps.raycast({ 1.f, 3.f, 0.f }, { 0.f, -1.f, 0.f }, 10.f, hit);
    const bool hitMask = s.ps.raycast({ 1.f, 3.f, 0.f }, { 0.f, -1.f, 0.f }, 10.f, hit, {}, PhysicsLayerTable::bit(1));
    check(hitAll && !hitMask, "레이캐스트 layerMask: Debris 만 보면 상자 · 바닥을 맞추지 않음");
    // 깨워서 들어 올리면 끝
    rbOf(normal)->setLinearVelocity({ 0.f, 6.f, 0.f });
    run(s.ps, 20);
    check(normalEnd == 1, "들어 올리면 끝 1 (" + std::to_string(normalEnd) + ")");
}

// ── J2 트리거 ─────────────────────────────────────────────────────────────────
void testTrigger() {
    std::printf("\n== J2 trigger ==\n");
    Scene s;
    s.ground();
    Actor* zone = s.box("Zone", { 0.f, 2.f, 0.f }, { 2.f, 1.f, 2.f }, EMotionType::Static);
    rbOf(zone)->trigger = true;
    // 1) 공이 트리거를 지나 바닥으로: 들어감 1 · 나감 1(양쪽 함수 칸 · 버스)
    Actor* ball = s.box("Ball", { 0.f, 4.f, 0.f }, glm::vec3(0.3f), EMotionType::Dynamic);
    rbOf(ball)->shape = EPhysicsShape::Sphere;
    int zoneEnter = 0, zoneExit = 0, ballEnter = 0, ballExit = 0, busBegin = 0, busEnd = 0;
    Actor* seenOther = nullptr;
    rbOf(zone)->onTriggerEnter = [&](const PhysicsContact& c) { ++zoneEnter; seenOther = c.other; };
    rbOf(zone)->onTriggerExit  = [&](const PhysicsContact&) { ++zoneExit; };
    rbOf(ball)->onTriggerEnter = [&](const PhysicsContact&) { ++ballEnter; };
    rbOf(ball)->onTriggerExit  = [&](const PhysicsContact&) { ++ballExit; };
    EventSubscription sub = s.w.events().subscribe<PhysicsContactEvent>([&](const PhysicsContactEvent& e) {
        if (!e.trigger) return;
        if (e.phase == EContactPhase::Begin) ++busBegin;
        if (e.phase == EContactPhase::End) ++busEnd;
    });
    run(s.ps, 120);
    check(zoneEnter == 1 && zoneExit == 1, "공이 지나감: 트리거 들어감 1 · 나감 1 (" + std::to_string(zoneEnter) + ", " + std::to_string(zoneExit) + ")");
    check(ballEnter == 1 && ballExit == 1 && seenOther == ball, "상대 쪽 함수 칸도 1 · 1, other = 공");
    check(busBegin == 1 && busEnd == 1, "이벤트 버스 PhysicsContactEvent 시작 1 · 끝 1");
    check(ball->worldPosition().y < 0.5f, "트리거는 막지 않음 (공 y " + fmt(ball->worldPosition().y) + ")");

    // 2) 트리거 안에서 잠든 상자: 들어감 1 · 나감 0, 지우면 나감 1(other = nullptr)
    Actor* zone2 = s.box("Zone2", { 5.f, 0.5f, 0.f }, { 2.f, 1.f, 2.f }, EMotionType::Static);
    rbOf(zone2)->trigger = true;
    Actor* crate = s.box("Crate", { 5.f, 2.f, 0.f }, glm::vec3(0.3f), EMotionType::Dynamic);
    int in2 = 0, out2 = 0;
    bool exitOtherNull = false;
    rbOf(zone2)->onTriggerEnter = [&](const PhysicsContact&) { ++in2; };
    rbOf(zone2)->onTriggerExit  = [&](const PhysicsContact& c) { ++out2; exitOtherNull = c.other == nullptr; };
    run(s.ps, 400);
    PhysicsSystem::BodyInfo bi;
    s.ps.queryBody(rbOf(crate)->bodyId_, bi);
    check(in2 == 1 && out2 == 0 && !bi.active, "트리거 안에서 잠든 상자: 들어감 1 · 나감 0 (" + std::to_string(in2) + ", " + std::to_string(out2)
          + ", active " + std::to_string(bi.active) + ")");
    s.w.destroyActor(crate);
    run(s.ps, 2);
    check(out2 == 1 && exitOtherNull, "상자를 지우면 나감 1, other = nullptr");

    // 2-b) 이미 잠든 상자 둘레에 트리거가 새로 생겨도 들어감(정적 센서는 잠든 몸을 못 보므로 깨어 있는 키네마틱 센서로 만듦, D3)
    Actor* sleeper = s.box("Sleeper", { 8.f, 0.15f, 0.f }, glm::vec3(0.3f), EMotionType::Dynamic);
    run(s.ps, 200);
    s.ps.queryBody(rbOf(sleeper)->bodyId_, bi);
    Actor* zone3 = s.box("Zone3", { 8.f, 0.5f, 0.f }, { 2.f, 1.f, 2.f }, EMotionType::Static);
    rbOf(zone3)->trigger = true;
    int in3 = 0;
    rbOf(zone3)->onTriggerEnter = [&](const PhysicsContact&) { ++in3; };
    run(s.ps, 5);
    check(!bi.active && in3 == 1, "잠든 상자 둘레에 생긴 트리거도 들어감 1 (" + std::to_string(in3) + ")");

    // 3) 질의는 트리거를 맞추지 않음
    PhysicsSystem::RaycastHit hit;
    s.ps.raycast({ 5.f, 3.f, 0.f }, { 0.f, -1.f, 0.f }, 10.f, hit);
    check(hit.actor && hit.actor->name == "Ground", "레이캐스트는 트리거를 지나 바닥을 맞춤");
    std::vector<Actor*> found;
    s.ps.overlapSphere({ 5.f, 0.5f, 0.f }, 0.2f, found);
    check(found.empty(), "구 겹침은 트리거를 빼냄");
}

// ── J2 접촉 알림 안에서 액터 삭제 ──────────────────────────────────────────────
void testDeleteInCallback() {
    std::printf("\n== J2 delete inside a callback ==\n");
    Scene s;
    Actor* ground = s.ground();
    rbOf(ground)->contactEvents = true;
    std::vector<Actor*> boxes;
    for (int i = 0; i < 3; ++i) boxes.push_back(s.box("B", { (float)i, 1.f, 0.f }, glm::vec3(0.3f), EMotionType::Dynamic));
    int begins = 0, ends = 0;
    rbOf(ground)->onContactBegin = [&](const PhysicsContact& c) {
        ++begins;
        if (c.other) s.w.destroyActor(c.other);   // 부딪힌 상자를 바로 지움
    };
    rbOf(ground)->onContactEnd = [&](const PhysicsContact& c) { ++ends; (void)c; };
    run(s.ps, 90);
    int left = 0;
    for (const auto& a : s.w.getActors()) left += a->name == "B" ? 1 : 0;
    check(begins == 3 && ends == 3 && left == 0, "시작 알림에서 지움: 시작 3 · 끝 3 · 남은 상자 0 (" + std::to_string(begins) + ", "
          + std::to_string(ends) + ", " + std::to_string(left) + ")");
}

// ── J3 조인트 ─────────────────────────────────────────────────────────────────
void testHingeDoor() {
    std::printf("\n== J3 hinge door ==\n");
    Scene s;
    s.ground();
    Actor* frame = s.box("Frame", { -0.6f, 1.f, 0.f }, { 0.1f, 2.f, 0.1f }, EMotionType::Static);
    Actor* door  = s.box("Door",  {  0.f,  1.f, 0.f }, { 1.f, 1.8f, 0.05f }, EMotionType::Dynamic);
    auto* j = door->addComponent<JointComponent>();
    j->type   = EJointType::Hinge;
    j->target = HActorRef(frame);
    j->anchor = { -0.5f, 0.f, 0.f };   // 문 왼쪽 끝(로컬, 크기를 곱하면 −0.5 m)
    j->axis   = { 0.f, 1.f, 0.f };
    j->limits = true;
    j->limitMin = -10.f;
    j->limitMax = 80.f;
    run(s.ps, 1);
    check(j->hasJoint() && s.ps.jointCount() == 1, "물리 틱이 경첩을 만듦");
    // 바깥쪽 끝을 세게 밀어 열기(+Y 둘레 + 방향)
    float maxAngle = -999.f;
    for (int i = 0; i < 180; ++i) {
        if (i == 0) rbOf(door)->setAngularVelocity({ 0.f, 4.f, 0.f });   // 한 번 세게 밂
        s.ps.tick(kDt);
        maxAngle = std::max(maxAngle, j->currentValue());
    }
    const float a = j->currentValue();
    check(maxAngle < 82.f && a > 76.f && a < 81.f, "밀어 연 문은 80도 한계에서 멈춤 (최대 " + fmt(maxAngle) + ", 지금 " + fmt(a) + ")");
    const glm::vec3 hingeW = door->worldPosition() + door->worldRotation() * glm::vec3(-0.5f, 0.f, 0.f);
    check(glm::length(hingeW - glm::vec3(-0.5f, 1.f, 0.f)) < 0.02f, "경첩 점은 제자리 (" + fmt(glm::length(hingeW - glm::vec3(-0.5f, 1.f, 0.f))) + " m)");
    float minAngle = 999.f;
    for (int i = 0; i < 180; ++i) {
        if (i == 0) rbOf(door)->setAngularVelocity({ 0.f, -4.f, 0.f });
        s.ps.tick(kDt);
        minAngle = std::min(minAngle, j->currentValue());
    }
    check(minAngle > -12.f && j->currentValue() < -7.f, "반대로 밀면 −10도에서 멈춤 (최소 " + fmt(minAngle) + ", 지금 " + fmt(j->currentValue()) + ")");

    // 상대(문틀)를 지우면 조인트도 정리, 크래시 없음
    s.w.destroyActor(frame);
    run(s.ps, 30);
    check(!j->hasJoint() && s.ps.jointCount() == 0 && door->worldPosition().y < 1.f, "문틀을 지우면 조인트가 사라지고 문은 떨어짐 (y " + fmt(door->worldPosition().y) + ")");
    // 조인트를 든 쪽을 지워도 안전
    Actor* post = s.box("Post", { 4.f, 3.f, 0.f }, glm::vec3(0.2f), EMotionType::Static);
    Actor* lamp = s.box("Lamp", { 4.f, 2.f, 0.f }, glm::vec3(0.2f), EMotionType::Dynamic);
    auto* j2 = lamp->addComponent<JointComponent>();
    j2->type = EJointType::Distance;
    j2->target = HActorRef(post);
    run(s.ps, 10);
    check(j2->hasJoint() && s.ps.jointCount() == 1, "거리 조인트 생김");
    s.w.destroyActor(lamp);
    run(s.ps, 10);
    check(s.ps.jointCount() == 0, "조인트를 든 액터를 지우면 조인트도 지워짐");
    (void)post;
}

void testOtherJoints() {
    std::printf("\n== J3 fixed · slider · distance ==\n");
    Scene s;
    s.ground();
    // 슬라이더: X 축, 0 ~ 1 m
    Actor* rail = s.box("Rail", { 0.f, 1.f, 0.f }, glm::vec3(0.2f), EMotionType::Dynamic);
    auto* sj = rail->addComponent<JointComponent>();
    sj->type = EJointType::Slider;
    sj->axis = { 1.f, 0.f, 0.f };
    sj->limits = true;
    sj->limitMin = 0.f;
    sj->limitMax = 1.f;
    // 거리: 월드 점 (3, 3, 0) 에 1 m 줄
    Actor* bob = s.box("Bob", { 4.f, 3.f, 0.f }, glm::vec3(0.2f), EMotionType::Dynamic);
    rbOf(bob)->shape = EPhysicsShape::Sphere;
    auto* dj = bob->addComponent<JointComponent>();
    dj->type = EJointType::Distance;
    dj->connectedAnchor = { 3.f, 3.f, 0.f };
    // 고정: 두 상자가 붙어 함께 떨어짐
    Actor* f1 = s.box("F1", { -3.f, 2.f, 0.f }, glm::vec3(0.3f), EMotionType::Dynamic);
    Actor* f2 = s.box("F2", { -2.5f, 2.f, 0.f }, glm::vec3(0.3f), EMotionType::Dynamic);
    auto* fj = f2->addComponent<JointComponent>();
    fj->type = EJointType::Fixed;
    fj->target = HActorRef(f1);
    run(s.ps, 1);
    for (int i = 0; i < 60; ++i) {
        rbOf(rail)->setLinearVelocity({ 4.f, 0.f, 0.f });
        s.ps.tick(kDt);
    }
    const glm::vec3 rp = rail->worldPosition();
    check(sj->hasJoint() && rp.x > 0.95f && rp.x < 1.03f && std::fabs(rp.y - 1.f) < 0.02f,
          "슬라이더는 X 로만, 1 m 한계에서 멈춤 (" + fmt(rp.x) + ", " + fmt(rp.y) + ")");
    run(s.ps, 120);
    const float len = glm::length(bob->worldPosition() - glm::vec3(3.f, 3.f, 0.f));
    check(dj->hasJoint() && std::fabs(len - 1.f) < 0.02f && std::fabs(dj->currentValue() - 1.f) < 0.02f && bob->worldPosition().y < 3.f,
          "거리 조인트: 1 m 줄에 매달려 흔들림 (" + fmt(len) + ", 값 " + fmt(dj->currentValue()) + ")");
    const float gap = glm::length(f2->worldPosition() - f1->worldPosition());
    check(fj->hasJoint() && std::fabs(gap - 0.5f) < 0.02f && f1->worldPosition().y < 0.3f,
          "고정 조인트: 떨어진 뒤에도 0.5 m 간격 (" + fmt(gap) + ")");
}

// ── J4 셰이프 캐스트 ──────────────────────────────────────────────────────────
void testShapeCast() {
    std::printf("\n== J4 shape cast ==\n");
    PhysicsLayerTable t;
    t.names[2] = "Wall";
    Scene s(&t);
    s.ground();   // 윗면 y = 0
    Actor* wall = s.box("Wall", { 0.f, 1.f, -5.f }, { 4.f, 2.f, 0.2f }, EMotionType::Static, 2);   // 앞면 z = −4.9
    Actor* zone = s.box("Zone", { 0.f, 2.5f, 0.f }, glm::vec3(1.f), EMotionType::Static);
    rbOf(zone)->trigger = true;
    run(s.ps, 1);
    using SS = PhysicsSystem::SweepShape;
    PhysicsSystem::RaycastHit h;
    const glm::quat id(1.f, 0.f, 0.f, 0.f);
    bool ok = s.ps.shapeCast(SS::sphere(0.5f), { 0.f, 5.f, 0.f }, id, { 0.f, -1.f, 0.f }, 10.f, h);
    check(ok && std::fabs(h.distance - 4.5f) < 0.01f && h.actor && h.actor->name == "Ground" && h.normal.y > 0.99f,
          "구 스윕: 바닥까지 4.5 m, 법선 +Y, 트리거는 지나침 (" + fmt(h.distance) + ")");
    ok = s.ps.shapeCast(SS::box({ 0.5f, 0.25f, 0.5f }), { 0.f, 5.f, 0.f }, id, { 0.f, -2.f, 0.f }, 10.f, h);
    check(ok && std::fabs(h.distance - 4.75f) < 0.01f, "상자 스윕: 4.75 m (" + fmt(h.distance) + ")");
    ok = s.ps.shapeCast(SS::capsule(0.2f, 0.5f), { 0.f, 5.f, 0.f }, id, { 0.f, -1.f, 0.f }, 10.f, h);
    check(ok && std::fabs(h.distance - 4.3f) < 0.01f, "캡슐 스윕(Y 축): 4.3 m (" + fmt(h.distance) + ")");
    ok = s.ps.shapeCast(SS::sphere(0.3f), { 0.f, 1.f, 0.f }, id, { 0.f, 0.f, -1.f }, 10.f, h);
    check(ok && std::fabs(h.distance - 4.6f) < 0.01f && h.actor == wall && h.normal.z > 0.99f, "구 스윕: 벽까지 4.6 m, 법선 +Z (" + fmt(h.distance) + ")");
    ok = s.ps.shapeCast(SS::sphere(0.3f), { 0.f, 1.f, 0.f }, id, { 0.f, 0.f, -1.f }, 10.f, h, (uint16_t)~PhysicsLayerTable::bit(2));
    check(!ok, "layerMask 로 Wall 레이어를 빼면 맞추지 않음");
    std::vector<Actor*> ignore{ wall };
    ok = s.ps.shapeCast(SS::sphere(0.3f), { 0.f, 1.f, 0.f }, id, { 0.f, 0.f, -1.f }, 10.f, h, PhysicsLayerTable::kAllMask, ignore);
    check(!ok, "ignore 액터는 건너뜀");
    ok = s.ps.shapeCast(SS::sphere(0.5f), { 0.f, 0.2f, 0.f }, id, { 1.f, 0.f, 0.f }, 3.f, h);
    check(ok && h.distance < 1e-4f, "시작부터 겹치면 거리 0 (" + std::to_string(ok) + ", " + fmt(h.distance) + ")");
    ok = s.ps.shapeCast(SS::box({ 0.5f, 0.1f, 0.5f }), { 0.f, 5.f, 0.f }, glm::angleAxis(glm::radians(90.f), glm::vec3(0.f, 0.f, 1.f)),
                        { 0.f, -1.f, 0.f }, 10.f, h);
    check(ok && std::fabs(h.distance - 4.5f) < 0.01f, "회전한 상자(세운 판) 스윕: 4.5 m (" + fmt(h.distance) + ")");
}

// ── 씬 파일 왕복 ──────────────────────────────────────────────────────────────
std::string fieldsOf(const Component& c) {
    ComponentFieldWriter w;
    c.writeFields(w);
    std::string out;
    for (const auto& [k, v] : w.fields()) out += k + "=" + v + ";";
    return out;
}

void testSerialization() {
    std::printf("\n== fields ==\n");
    RigidBodyComponent rb;
    const std::string before = fieldsOf(rb);
    check(before.find("layer") == std::string::npos && before.find("trigger") == std::string::npos &&
          before.find("contactEvents") == std::string::npos, "기본값은 쓰지 않음(옛 씬 파일이 바뀌지 않음)");
    rb.readField("layer", "3");
    rb.readField("trigger", "1");
    rb.readField("contactEvents", "1");
    check(rb.layer == 3 && rb.trigger && rb.contactEvents && fieldsOf(rb).find("layer=3;trigger=1;contactEvents=1;") != std::string::npos,
          "layer · trigger · contactEvents 읽고 쓰기");
    rb.readField("layer", "16");
    check(rb.layer == 3, "범위 밖 레이어는 무시");

    JointComponent j;
    j.readField("type", "slider");
    j.readField("target", "actor:00000000000000ab");
    j.readField("anchor", "1 2 3");
    j.readField("axis", "0 0 1");
    j.readField("limits", "1");
    j.readField("limitMin", "-0.5");
    j.readField("limitMax", "2");
    JointComponent j2;
    const std::string f = fieldsOf(j);
    ComponentFieldWriter w;
    j.writeFields(w);
    for (const auto& [k, v] : w.fields()) j2.readField(k, v);
    check(j2.type == EJointType::Slider && j2.target.guid().value == 0xab && j2.anchor == glm::vec3(1, 2, 3) && j2.axis == glm::vec3(0, 0, 1)
          && j2.limits && j2.limitMin == -0.5f && j2.limitMax == 2.f && fieldsOf(j2) == f, "JointComponent 왕복 (" + f + ")");
    JointComponent j3;
    j3.readField("type", "rope");
    check(j3.type == EJointType::Hinge, "모르는 종류는 무시");
    bool hasTarget = false;
    for (auto& pd : j.buildProps()) hasTarget = hasTarget || (std::string(pd.label) == "Target" && pd.kind == PropDesc::ActorRef);
    check(hasTarget, "디테일 패널: Target 은 액터 참조 칸");
    bool hasLayer = false;
    for (auto& pd : rb.buildProps())
        if (std::string(pd.label) == "Layer" && pd.kind == PropDesc::Enum && pd.options.size() == 16) hasLayer = true;
    check(hasLayer, "디테일 패널: Layer 는 16 개 이름 목록");
}

// ── J5 확인 장면 ──────────────────────────────────────────────────────────────
// 장면의 메시 컴포넌트는 GPU 가 필요하므로 빼고 읽습니다(물리는 충돌체 크기만 씀).
std::string stripMeshes(const std::string& text) {
    std::istringstream in(text);
    std::string line, out;
    bool skip = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "COMPONENT:StaticMeshComponent") { skip = true; continue; }
        if (skip) { if (line == "END_COMPONENT") skip = false; continue; }
        out += line + "\n";
    }
    return out;
}

Actor* named(World& w, const char* n) {
    for (const auto& a : w.getActors())
        if (a->name == n) return a.get();
    return nullptr;
}

void testProbeScene() {
    std::printf("\n== J5 PhysicsJointProbe.hscene ==\n");
    std::ifstream f(HARUKA_SOURCE_DIR "/Projects/Samples/Content/scenes/PhysicsJointProbe.hscene", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    check(!ss.str().empty(), "장면 파일을 읽음");
    Scene s;
    AssetManager assets(nullptr);
    SceneSerializer::readScene(stripMeshes(ss.str()), s.w, assets);
    Actor* gate = named(s.w, "Gate");
    Actor* zone = named(s.w, "TriggerZone");
    Actor* anchor = named(s.w, "ChainAnchor");
    Actor* links[4] = { named(s.w, "Link1"), named(s.w, "Link2"), named(s.w, "Link3"), named(s.w, "Link4") };
    check(gate && zone && anchor && links[0] && links[3], "장면의 액터를 찾음");
    if (!gate || !zone || !anchor || !links[0] || !links[3]) return;
    int enter = 0, exit = 0;
    rbOf(zone)->onTriggerEnter = [&](const PhysicsContact&) { ++enter; };
    rbOf(zone)->onTriggerExit  = [&](const PhysicsContact&) { ++exit; };
    run(s.ps, 300);   // 5초
    auto* gj = gate->getComponent<JointComponent>();
    check(gj && gj->hasJoint() && s.ps.jointCount() == 5, "조인트 5개(문 1 · 사슬 4)가 생김 (" + std::to_string(s.ps.jointCount()) + ")");
    const float ga = gj ? gj->currentValue() : 0.f;
    check(ga < -55.f && ga > -62.f, "문(경첩): 중력에 내려가 −60도 한계에서 멈춤 (" + fmt(ga) + ")");
    bool linksOk = true;
    std::string lens;
    Actor* prev = anchor;
    for (Actor* l : links) {
        const float d = glm::length(l->worldPosition() - prev->worldPosition());
        lens += fmt(d) + " ";
        linksOk = linksOk && std::fabs(d - 0.3f) < 0.03f;
        prev = l;
    }
    check(linksOk, "사슬(거리): 고리 간격 0.3 m 유지 (" + lens + ")");
    check(links[3]->worldPosition().y < anchor->worldPosition().y && links[3]->worldPosition().y > 1.f,
          "사슬 끝은 고정점 아래에 매달림 (y " + fmt(links[3]->worldPosition().y) + ")");
    check(enter == 1 && exit == 0, "트리거 상자: 들어감 1 · 나감 0 (" + std::to_string(enter) + ", " + std::to_string(exit) + ")");
}

} // namespace

int main() {
    std::printf("== Physics joints · contact events · layers ==\n");
    registerCoreComponents();
    testLayerTable();
    testLayerIgnore();
    testTrigger();
    testDeleteInCallback();
    testHingeDoor();
    testOtherJoints();
    testShapeCast();
    testSerialization();
    testProbeScene();
    std::printf("\n(%d passed, %d failed)\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
