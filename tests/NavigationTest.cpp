// 내비게이션 · AI 시험(2026-11-21, Docs/Navigation_AI.md).
//
// N1 시험 지형(바닥 · 벽 · 완만한 경사 · 가파른 경사 · 섬)을 굽고 경로 길이 · 도달 · 부분 경로 · 없음 · 투영 · 설정 효과 · 생성 시간 ·
//    결정성 · .hnav 왕복 · 손상 거부를 봅니다(월드 없이).
// N2 월드: Static 충돌체만 입력(Dynamic · 트리거 제외), NavSystem 굽기 · 질의, 에이전트(표면 · 캐릭터) 이동 · 이벤트 · 벽 통과 없음,
//    경로 이탈 다시 찾기, 도달 불가, 마주 오는 두 에이전트의 회피.
// N3 AI 상태 기계: 순찰 → 대기 → 다음 순찰점, 대상 감지 → 추적, 놓침 → 대기 → 순찰.
// N4 확인 장면 NavProbe.hscene + 구운 NavProbe.hnav: 파일이 지금 장면과 같은 바이트(결정성 · 최신), 600 프레임 뒤 에이전트가 목표 근처.
//
// HarukaNavTest --write-probe <Content 폴더> 는 확인 장면과 .hnav 를 다시 씁니다(장면을 고친 뒤).
#include "../src/assets/AssetManager.hpp"
#include "../src/components/CoreComponents.hpp"
#include "../src/nav/AIBrainComponent.hpp"
#include "../src/nav/NavAgentComponent.hpp"
#include "../src/nav/NavEvents.hpp"
#include "../src/nav/NavGeometry.hpp"
#include "../src/nav/NavMeshBuild.hpp"
#include "../src/nav/NavMeshVolumeComponent.hpp"
#include "../src/nav/NavSystem.hpp"
#include "../src/physics/PhysicsSystem.hpp"
#include "../src/physics/RigidBodyComponent.hpp"
#include "../src/scene/Actor.hpp"
#include "../src/scene/World.hpp"
#include "../src/sceneio/SceneSerializer.hpp"
#include "../src/vfs/Vfs.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_passed = 0, g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed;
    else ++g_failed;
}

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}
std::string fmt(const glm::vec3& v) { return "(" + fmt(v.x) + ", " + fmt(v.y) + ", " + fmt(v.z) + ")"; }

constexpr float kDt = 1.f / 60.f;
constexpr float kRampDeg  = 18.434949f;   // atan(1/3): 3 m 에 1 m 오름
constexpr float kSteepDeg = 60.f;

float flatDist(const glm::vec3& a, const glm::vec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

// 벽(x ∈ [-0.2, 0.2], z ∈ [-10, 6]) 안을 지나는지(선분을 5 cm 로 나눠 봄).
bool crossesWall(const std::vector<glm::vec3>& pts) {
    for (size_t i = 1; i < pts.size(); ++i) {
        const glm::vec3 a = pts[i - 1], b = pts[i];
        const int n = std::max(1, (int)(glm::length(b - a) / 0.05f));
        for (int k = 0; k <= n; ++k) {
            const glm::vec3 p = a + (b - a) * ((float)k / (float)n);
            if (std::fabs(p.x) < 0.2f && p.z < 6.f && p.z > -10.f) return true;
        }
    }
    return false;
}
float pathLength(const std::vector<glm::vec3>& p) {
    float l = 0.f;
    for (size_t i = 1; i < p.size(); ++i) l += glm::length(p[i] - p[i - 1]);
    return l;
}
float maxZ(const std::vector<glm::vec3>& p) {
    float z = -1e9f;
    for (const auto& v : p) z = std::max(z, v.z);
    return z;
}

// 시험 지형(월드 없이 삼각형으로).
//   바닥 20 × 20(윗면 y 0), 벽 x 0 · z -10 ~ 6(높이 2, 끝 z 6 ~ 10 이 열림),
//   높은 판 A(7.5, ·, -7.5) 윗면 y 1 + 완만한 경사(18.4도), 높은 판 B(-7.5, ·, -7.5) 윗면 y 1 + 가파른 경사(60도), 섬(7.5, 3.1, 7.5).
glm::mat4 trs(const glm::vec3& t, float rotZDeg = 0.f) {
    return glm::rotate(glm::translate(glm::mat4(1.f), t), glm::radians(rotZDeg), glm::vec3(0.f, 0.f, 1.f));
}
void makeTerrain(NavInputGeometry& g) {
    g.clear();
    hnav::addBox(g, trs({ 0.f, -0.1f, 0.f }), glm::vec3(0.f), { 10.f, 0.1f, 10.f });
    hnav::addBox(g, trs({ 0.f, 1.f, -2.f }), glm::vec3(0.f), { 0.2f, 1.f, 8.f });
    hnav::addBox(g, trs({ 7.5f, 0.5f, -7.5f }), glm::vec3(0.f), { 1.5f, 0.5f, 1.5f });
    hnav::addBox(g, trs({ 4.5f, 0.45f, -7.5f }, kRampDeg), glm::vec3(0.f), { 1.6f, 0.05f, 1.f });
    hnav::addBox(g, trs({ -7.5f, 0.5f, -7.5f }), glm::vec3(0.f), { 1.5f, 0.5f, 1.5f });
    hnav::addBox(g, trs({ -5.71f, 0.45f, -7.5f }, -kSteepDeg), glm::vec3(0.f), { 0.6f, 0.05f, 1.f });
    hnav::addBox(g, trs({ 7.5f, 3.f, 7.5f }), glm::vec3(0.f), { 1.5f, 0.1f, 1.5f });
}
const glm::vec3 kBMin{ -10.f, -1.f, -10.f }, kBMax{ 10.f, 4.f, 10.f };
const glm::vec3 kExt{ 1.f, 2.f, 1.f };

NavBuildResult buildTerrain(const NavBuildSettings& s, NavInputGeometry* outGeo = nullptr) {
    NavInputGeometry g;
    makeTerrain(g);
    NavBuildResult r = hnav::build(g, s, kBMin, kBMax);
    if (outGeo) *outGeo = g;
    return r;
}

void testBuildAndQuery() {
    std::printf("\n== N1 굽기 · 경로 · 파일 ==\n");
    NavBuildSettings s;
    NavInputGeometry geo;
    const NavBuildResult r = buildTerrain(s, &geo);
    check(r.ok && !r.tile.empty() && r.polygons > 0,
          "시험 지형을 구움: " + std::to_string(r.polygons) + " 다각형, " + std::to_string(geo.triangleCount()) + " 삼각형, " +
              fmt((float)r.milliseconds) + " ms");
    check(r.milliseconds < 5000.0, "생성 시간 5 초 안(" + fmt((float)r.milliseconds) + " ms)");
    NavMesh m;
    std::string err;
    check(m.load(r.tile, err) && m.valid() && m.polygonCount() == r.polygons, "Detour 메시로 읽음 " + err);

    std::vector<glm::vec3> p;
    // 벽 돌아가기.
    ENavPathResult res = m.findPath({ -5.f, 0.f, 0.f }, { 5.f, 0.f, 0.f }, kExt, p);
    const float lenA = pathLength(p);
    check(res == ENavPathResult::Complete && !p.empty() && flatDist(p.back(), { 5.f, 0.f, 0.f }) < 0.05f,
          "벽 너머 끝점: 완전 경로, 끝이 목표(" + std::to_string(p.size()) + " 점)");
    check(lenA > 15.f && lenA < 18.f, "경로 길이 ≈ 벽 끝 돌아가기 16.1 m (" + fmt(lenA) + ")");
    check(!crossesWall(p), "경로가 벽을 지나지 않음");
    const float cornerZ = maxZ(p);
    check(cornerZ > 6.f && cornerZ < 6.8f, "벽 끝 모서리를 반경만큼 떨어져 돎(z " + fmt(cornerZ) + ")");
    // 완만한 경사 → 판 위.
    res = m.findPath({ 2.f, 0.f, -3.f }, { 7.5f, 1.f, -7.5f }, kExt, p);
    check(res == ENavPathResult::Complete && !p.empty() && std::fabs(p.back().y - 1.f) < 0.15f,
          "18도 경사로 판 위에 닿음(끝 " + fmt(p.empty() ? glm::vec3(0.f) : p.back()) + ")");
    // 가파른 경사(60도) → 부분 경로.
    res = m.findPath({ -3.f, 0.f, -3.f }, { -7.5f, 1.f, -7.5f }, kExt, p);
    check(res == ENavPathResult::Partial && !p.empty() && p.back().y < 0.3f,
          "60도 경사 위 판: 부분 경로, 바닥에서 멈춤(" + std::string(navPathResultName(res)) + ")");
    // 섬(이어지지 않음) → 부분 경로.
    res = m.findPath({ 5.f, 0.f, 3.f }, { 7.5f, 3.1f, 7.5f }, kExt, p);
    check(res == ENavPathResult::Partial, "떨어진 섬: 부분 경로(" + std::string(navPathResultName(res)) + ")");
    // 메시가 없는 곳.
    res = m.findPath({ -5.f, 0.f, 0.f }, { 50.f, 0.f, 50.f }, kExt, p);
    check(res == ENavPathResult::NoPath && p.empty(), "끝점 근처에 메시 없음: 경로 없음");
    res = m.findPath({ 50.f, 0.f, 50.f }, { 0.f, 0.f, 0.f }, kExt, p);
    check(res == ENavPathResult::NoPath, "시작점 근처에 메시 없음: 경로 없음");
    // 투영 · 표면 미끄러짐.
    glm::vec3 q(0.f);
    check(m.projectPoint({ -5.f, 0.7f, 0.f }, kExt, q) && std::fabs(q.y) < 0.1f, "투영: 공중 점 → 바닥 y " + fmt(q.y));
    check(m.moveAlongSurface({ -1.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, kExt, q) && q.x < -0.2f,
          "표면 미끄러짐: 벽에 막혀 x < -0.2 에서 멈춤(x " + fmt(q.x) + ")");

    // 설정 효과: 반경이 크면 모서리에서 더 떨어지고, 경사 한계가 낮으면 18도 경사를 못 오름.
    NavBuildSettings wide = s;
    wide.agentRadius = 0.6f;
    NavMesh mw;
    mw.load(buildTerrain(wide).tile, err);
    mw.findPath({ -5.f, 0.f, 0.f }, { 5.f, 0.f, 0.f }, kExt, p);
    check(maxZ(p) > cornerZ + 0.2f, "반경 0.6 m: 모서리를 더 크게 돎(z " + fmt(maxZ(p)) + " > " + fmt(cornerZ) + " + 0.2)");
    NavBuildSettings flat = s;
    flat.maxSlopeDeg = 15.f;
    NavMesh mf;
    mf.load(buildTerrain(flat).tile, err);
    res = mf.findPath({ 2.f, 0.f, -3.f }, { 7.5f, 1.f, -7.5f }, kExt, p);
    check(res == ENavPathResult::Partial, "최대 경사 15도: 18도 경사를 못 올라 부분 경로");
    NavBuildSettings low = s;
    low.maxClimb = 0.02f;   // 경사 판 두께(0.1 m)만큼의 턱도 못 오름
    NavMesh ml;
    ml.load(buildTerrain(low).tile, err);
    res = ml.findPath({ 2.f, 0.f, -3.f }, { 7.5f, 1.f, -7.5f }, kExt, p);
    check(res == ENavPathResult::Partial, "턱 높이 0.02 m: 경사로 시작 턱을 못 올라 부분 경로");

    // 결정성.
    const NavBuildResult r2 = buildTerrain(s);
    check(r2.tile == r.tile, "같은 입력 두 번 = 같은 바이트(" + std::to_string(r.tile.size()) + " B)");
    check(hnav::inputHash(geo, s, kBMin, kBMax) == hnav::inputHash(geo, s, kBMin, kBMax) &&
              hnav::inputHash(geo, wide, kBMin, kBMax) != hnav::inputHash(geo, s, kBMin, kBMax),
          "입력 해시: 같으면 같고 설정이 다르면 다름");

    // 파일 왕복.
    NavFile f;
    f.settings = s;
    f.inputHash = hnav::inputHash(geo, s, kBMin, kBMax);
    f.boundsMin = kBMin;
    f.boundsMax = kBMax;
    f.polygons = r.polygons;
    f.tile = r.tile;
    const std::vector<uint8_t> bytes = hnav::writeFile(f);
    NavFile g;
    check(hnav::readFile(bytes.data(), bytes.size(), g, err) && g.tile == f.tile && g.inputHash == f.inputHash &&
              g.polygons == f.polygons && g.settings.agentRadius == s.agentRadius && g.boundsMax == kBMax,
          "쓰기 → 읽기 왕복: 같은 내용 " + err);
    NavMesh m2;
    m2.load(g.tile, err);
    m2.findPath({ -5.f, 0.f, 0.f }, { 5.f, 0.f, 0.f }, kExt, p);
    check(std::fabs(pathLength(p) - lenA) < 1e-4f, "읽은 메시도 같은 경로 길이");
    auto rejects = [&](std::vector<uint8_t> b, const char* what) {
        NavFile x;
        std::string e;
        const bool ok = hnav::readFile(b.data(), b.size(), x, e);
        check(!ok && !e.empty(), std::string("깨진 파일 거부: ") + what + " (" + e + ")");
    };
    { auto b = bytes; b[0] ^= 1; rejects(b, "마법 값"); }
    { auto b = bytes; b[4] = 9; rejects(b, "판"); }
    { auto b = bytes; b.resize(b.size() - 10); rejects(b, "잘림"); }
    { auto b = bytes; b[bytes.size() - r.tile.size()] ^= 0xFF; rejects(b, "Detour 머리"); }
    rejects({}, "빈 파일");
    // 빈 입력 = 빈 메시.
    const NavBuildResult empty = hnav::build(NavInputGeometry{}, s, kBMin, kBMax);
    check(empty.ok && empty.tile.empty(), "삼각형 없음: 빈 메시(성공, 타일 없음)");
    check(!hnav::build(geo, s, kBMax, kBMin).ok, "크기 없는 범위는 실패");
}

// ── 월드 ──────────────────────────────────────────────────────────────────────────────────────────

// 월드 + 물리 + 내비. 물리를 먼저 끝냅니다(월드 소멸은 onDestroy 를 부르지 않음).
struct Scene {
    World         w;
    PhysicsSystem ps;
    NavSystem     nav;
    Scene() {
        ps.startup();
        ps.setWorld(&w);
        nav.setWorld(&w);
        w.setService<NavSystem>(&nav);
    }
    ~Scene() { ps.shutdown(); }

    Actor* box(const char* name, glm::vec3 pos, glm::vec3 scale, EMotionType m = EMotionType::Static, bool trigger = false) {
        Actor* a = w.spawnActor(name);
        a->setPosition(pos);
        a->setScale(scale);
        auto* rb = a->addComponent<RigidBodyComponent>();
        rb->motionType = m;
        rb->trigger = trigger;
        return a;
    }
    NavMeshVolumeComponent* volume(glm::vec3 half = { 10.f, 3.f, 10.f }) {
        Actor* a = w.spawnActor("NavVolume");
        auto* v = a->addComponent<NavMeshVolumeComponent>();
        v->halfExtent = half;
        return v;
    }
    void floor(float half = 10.f) { box("Floor", { 0.f, -0.1f, 0.f }, { 2.f * half, 0.2f, 2.f * half }); }
    void wall() { box("Wall", { 0.f, 1.f, -2.f }, { 0.4f, 2.f, 16.f }); }
    void step() {
        w.tick(kDt);
        ps.tick(kDt);
        w.tickPostPhysics();
    }
    Actor* agent(const char* name, glm::vec3 pos, bool character) {
        Actor* a = w.spawnActor(name);
        a->setPosition(pos);
        auto* ag = a->addComponent<NavAgentComponent>();
        ag->useCharacter = character;
        return a;
    }
};

NavAgentComponent* agentOf(Actor* a) { return a->getComponent<NavAgentComponent>(); }

void testWorld() {
    std::printf("\n== N2 월드 · 에이전트 ==\n");
    Scene s;
    s.floor();
    s.wall();
    s.box("Crate", { 5.f, 0.5f, 5.f }, glm::vec3(1.f), EMotionType::Dynamic);
    s.box("Zone", { -5.f, 0.5f, 5.f }, glm::vec3(2.f), EMotionType::Static, true);
    NavMeshVolumeComponent* vol = s.volume();
    glm::vec3 mn, mx;
    vol->worldBounds(mn, mx);
    NavInputGeometry geo;
    const size_t used = hnav::collectStaticGeometry(s.w, mn, mx, geo);
    check(used == 2 && geo.triangleCount() == 24, "입력은 Static 충돌체 둘(바닥 · 벽)만, Dynamic · 트리거 제외 (" + std::to_string(used) + ")");
    const NavSystem::BuildReport rep = s.nav.build(false);
    std::printf("    %s\n", rep.text.c_str());
    check(rep.built == 1 && rep.failed == 0 && vol->loaded(), "NavSystem 굽기: 볼륨 1 개, 메시 읽힘");
    check(s.nav.info().find("up to date") != std::string::npos, "nav.info: 구운 입력이 지금 월드와 같음");
    NavPath path;
    const ENavPathResult wr = s.nav.findPath({ -5.f, 0.f, 0.f }, { 5.f, 0.f, 0.f }, path);
    check(wr == ENavPathResult::Complete && path.length() > 15.f, "월드 서비스 findPath: 완전 경로 " + fmt(path.length()) + " m");
    glm::vec3 proj(0.f);
    check(s.nav.projectPoint({ 3.f, 1.f, 3.f }, proj) && std::fabs(proj.y) < 0.1f, "월드 서비스 projectPoint: 바닥 y " + fmt(proj.y));
    s.w.findById(s.w.getActors()[1]->id)->setPosition({ 0.f, 1.f, -3.f });   // 벽을 옮기면 오래된 데이터
    check(s.nav.info().find("out of date") != std::string::npos, "벽을 옮기면 nav.info 가 오래됨을 알림");
    s.w.getActors()[1]->setPosition({ 0.f, 1.f, -2.f });
    s.ps.tick(kDt);   // 정적 몸 만들기(캐릭터가 설 바닥)

    int events = 0, okEvents = 0;
    EventSubscription sub = s.w.events().subscribe<NavMoveFinished>([&](const NavMoveFinished& e) { ++events; okEvents += e.success ? 1 : 0; });

    for (int mode = 0; mode < 2; ++mode) {
        const bool character = mode == 1;
        const char* label = character ? "캐릭터 이동" : "표면 이동";
        Actor* a = s.agent(character ? "Walker2" : "Walker1", { -5.f, 0.f, 0.f }, character);
        NavAgentComponent* ag = agentOf(a);
        ag->speed = 3.f;
        events = okEvents = 0;
        check(ag->moveTo({ 5.f, 0.f, 0.f }) && ag->status() == ENavMoveStatus::Moving, std::string(label) + ": moveTo 시작");
        bool inWall = false;
        float minY = 1e9f, maxY = -1e9f;
        int frames = 0;
        while (ag->status() == ENavMoveStatus::Moving && frames < 60 * 20) {
            s.step();
            ++frames;
            const glm::vec3 p = a->worldPosition();
            inWall = inWall || (std::fabs(p.x) < 0.2f && p.z < 6.f);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        const glm::vec3 p = a->worldPosition();
        check(ag->status() == ENavMoveStatus::Arrived && flatDist(p, { 5.f, 0.f, 5.f - 5.f }) <= ag->arrivalRadius + 0.05f,
              std::string(label) + ": 도착 " + fmt(p) + ", " + std::to_string(frames) + " 프레임");
        check(events == 1 && okEvents == 1, std::string(label) + ": NavMoveFinished 한 번(성공)");
        check(!inWall, std::string(label) + ": 벽을 지나지 않음");
        check(minY > -0.05f && maxY < 0.15f, std::string(label) + ": 바닥 높이 유지(y " + fmt(minY) + " ~ " + fmt(maxY) + ")");
        const glm::vec3 fwd = a->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
        check(frames > 0 && std::fabs(fwd.y) < 1e-3f, std::string(label) + ": 몸은 세워진 채 진행 방향으로 돎");
        s.w.destroyActor(a);
    }

    // 경로 이탈 → 다시 찾기.
    {
        Actor* a = s.agent("Pushed", { -5.f, 0.f, 0.f }, false);
        NavAgentComponent* ag = agentOf(a);
        ag->moveTo({ 5.f, 0.f, 0.f });
        for (int i = 0; i < 60; ++i) s.step();
        const int before = ag->repathCount();
        // 진행 방향의 직각으로 2 m(경로 선분에서 1 m 넘게 벗어남).
        const glm::vec3 v = ag->velocity();
        const glm::vec3 side = glm::normalize(glm::cross(glm::vec3(v.x, 0.f, v.z), glm::vec3(0.f, 1.f, 0.f)));
        a->setWorldPosition(a->worldPosition() + side * 2.f);
        for (int i = 0; i < 60 * 20 && ag->status() == ENavMoveStatus::Moving; ++i) s.step();
        check(ag->repathCount() > before && ag->status() == ENavMoveStatus::Arrived,
              "옆으로 2 m 밀면 다시 찾고 도착(경로 " + std::to_string(before) + " → " + std::to_string(ag->repathCount()) + ")");
        // 도달 불가.
        events = okEvents = 0;
        check(!ag->moveTo({ 40.f, 0.f, 40.f }) && ag->status() == ENavMoveStatus::Failed && events == 1 && okEvents == 0,
              "메시 밖 목표: moveTo false · Failed · 실패 이벤트");
        s.w.destroyActor(a);
    }
    s.step();
}

void testAvoidance() {
    std::printf("\n== N2 회피 ==\n");
    for (int on = 1; on >= 0; --on) {
        Scene s;
        s.floor();
        s.volume();
        s.nav.build(false);
        Actor* a = s.agent("A", { -4.f, 0.f, 0.f }, false);
        Actor* b = s.agent("B", { 4.f, 0.f, 0.f }, false);
        agentOf(a)->avoidance = agentOf(b)->avoidance = on != 0;
        agentOf(a)->moveTo({ 4.f, 0.f, 0.f });
        agentOf(b)->moveTo({ -4.f, 0.f, 0.f });
        float minD = 1e9f;
        for (int i = 0; i < 60 * 10; ++i) {
            s.step();
            minD = std::min(minD, flatDist(a->worldPosition(), b->worldPosition()));
            if (agentOf(a)->status() != ENavMoveStatus::Moving && agentOf(b)->status() != ENavMoveStatus::Moving) break;
        }
        const bool both = agentOf(a)->status() == ENavMoveStatus::Arrived && agentOf(b)->status() == ENavMoveStatus::Arrived;
        if (on) check(both && minD > 0.45f, "마주 오는 두 에이전트: 비켜 지나가 둘 다 도착, 최소 거리 " + fmt(minD) + " m (반경 합 0.6)");
        else    check(minD < 0.2f, "회피를 끄면 서로 겹침(최소 거리 " + fmt(minD) + " m) — 회피가 실제로 작동함");
    }
}

void testAI() {
    std::printf("\n== N3 AI 상태 기계 ==\n");
    Scene s;
    s.floor();
    s.volume();
    s.nav.build(false);
    Actor* pa = s.w.spawnActor("P_A");
    pa->setPosition({ 5.f, 0.f, 0.f });
    pa->addTag("Patrol");
    Actor* pb = s.w.spawnActor("P_B");
    pb->setPosition({ -5.f, 0.f, 0.f });
    pb->addTag("Patrol");
    Actor* g = s.agent("Guard", { 0.f, 0.f, 0.f }, false);
    agentOf(g)->speed = 3.f;
    auto* brain = g->addComponent<AIBrainComponent>();
    brain->patrolTag = "Patrol";
    brain->targetTag = "Player";
    brain->waitTime = 0.5f;
    brain->sightRadius = 4.f;
    brain->loseRadius = 6.f;
    std::vector<std::string> log;
    EventSubscription sub = s.w.events().subscribe<AIStateChanged>([&](const AIStateChanged& e) {
        if (e.actor == g) log.push_back(std::string(aiStateName(e.from)) + ">" + aiStateName(e.to));
    });
    auto has = [&](const char* t) { return std::find(log.begin(), log.end(), t) != log.end(); };
    s.step();
    check(brain->state() == EAIState::Patrol && brain->patrolIndex() == 0 && flatDist(agentOf(g)->goal(), pa->worldPosition()) < 1e-3f,
          "시작: 이름순 첫 순찰점 P_A 로 순찰");
    for (int i = 0; i < 60 * 3; ++i) s.step();
    check(has("patrol>wait") && has("wait>patrol") && brain->patrolIndex() == 1 && brain->state() == EAIState::Patrol,
          "도착 → 대기(타이머 0.5 초) → 다음 순찰점 P_B");
    Actor* player = s.w.spawnActor("Player");
    player->addTag("Player");
    player->setPosition(g->worldPosition() + glm::vec3(2.f, 0.f, 2.f));
    for (int i = 0; i < 20; ++i) s.step();
    check(brain->state() == EAIState::Chase && brain->target() == player, "대상이 감지 반경(4 m) 안 → 추적");
    for (int i = 0; i < 60 * 2; ++i) s.step();
    check(flatDist(g->worldPosition(), player->worldPosition()) < agentOf(g)->arrivalRadius + 0.6f,
          "추적: 대상 곁에 닿음(" + fmt(flatDist(g->worldPosition(), player->worldPosition())) + " m)");
    player->setPosition({ 9.f, 0.f, 9.f });
    for (int i = 0; i < 20; ++i) s.step();
    check(brain->state() == EAIState::Wait && has("chase>wait"), "대상이 놓침 반경(6 m) 밖 → 대기");
    const size_t lost = log.size();
    for (int i = 0; i < 45 && brain->state() == EAIState::Wait; ++i) s.step();
    check(brain->state() == EAIState::Patrol && log.size() == lost + 1 && log.back() == "wait>patrol", "대기(0.5 초) 뒤 다시 순찰");
    std::string seq;
    for (const auto& l : log) seq += l + " ";
    std::printf("    상태 바뀜: %s\n", seq.c_str());
    g->getComponent<AIBrainComponent>()->setEnabled(false);
    const size_t timersBefore = s.w.timers().count();
    check(agentOf(g)->status() == ENavMoveStatus::Idle && timersBefore == 0, "AI 를 끄면 타이머 · 이동이 멈춤(타이머 " + std::to_string(timersBefore) + ")");
}

// ── 확인 장면 ─────────────────────────────────────────────────────────────────────────────────────

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

// 장면 글(.hscene). 정적 상자 · 경사 · 목표 · 에이전트 · 볼륨.
struct ProbeWriter {
    std::ostringstream os;
    int guid = 1;
    static std::string F(float v) {
        char b[32];
        std::snprintf(b, sizeof b, "%.6g", v);
        return b;
    }
    // rotDeg = (x, y, z) 축 하나만 돌린 각도(도). 둘 이상 돌리면 ROT 줄과 QUAT 가 어긋나므로 하나만 씁니다.
    void begin(const char* name, glm::vec3 pos, glm::vec3 scale, glm::vec3 rotDeg = glm::vec3(0.f), const char* tags = nullptr) {
        char g[32];
        std::snprintf(g, sizeof g, "4e61760000%06x", guid++);
        const glm::vec3 h = glm::radians(rotDeg) * 0.5f;
        const glm::quat q = glm::quat(std::cos(h.x), std::sin(h.x), 0.f, 0.f) * glm::quat(std::cos(h.y), 0.f, std::sin(h.y), 0.f) *
                            glm::quat(std::cos(h.z), 0.f, 0.f, std::sin(h.z));
        os << "ACTOR:" << name << "\nGUID:" << g << "\nPOS:" << F(pos.x) << " " << F(pos.y) << " " << F(pos.z) << "\nROT:"
           << F(rotDeg.x) << " " << F(rotDeg.y) << " " << F(rotDeg.z) << "\nQUAT:" << F(q.x) << " " << F(q.y) << " " << F(q.z) << " "
           << F(q.w) << "\nSCALE:" << F(scale.x) << " " << F(scale.y) << " " << F(scale.z) << "\nACTIVE:1\n";
        if (tags) os << "TAGS:" << tags << "\n";
    }
    void mesh(const char* prim, const char* material) {
        os << "COMPONENT:StaticMeshComponent\nenabled=1\nvisible=1\ntransparent=0\nprim=" << prim << "\nprimMaterial=" << material
           << "\nEND_COMPONENT\n";
    }
    void staticBody() {
        os << "COMPONENT:RigidBodyComponent\nenabled=1\nshape=box\nmotion=static\nfriction=0.6\nrestitution=0\nmass=1\n"
              "halfExtent=0.5 0.5 0.5\nEND_COMPONENT\n";
    }
    void end() { os << "END_ACTOR\n"; }
    void solid(const char* name, glm::vec3 pos, glm::vec3 scale, const char* material, float rotZDeg = 0.f) {
        begin(name, pos, scale, glm::vec3(0.f, 0.f, rotZDeg));
        mesh("box|0x0|0|0.0000", material);
        staticBody();
        end();
    }
};

std::string probeSceneText() {
    ProbeWriter w;
    w.os << "# Haruka Scene v7\n"
            "# Navigation and AI check (2026-11-21, Docs/Navigation_AI.md N5). Press Play: Walker (Nav Agent + AI Brain) patrols the\n"
            "# actors tagged NavGoal in name order. NavGoalA is on the raised platform behind the wall, so Walker walks around the\n"
            "# wall end and up the ramp. The nav mesh is baked into NavProbe.hnav (console: nav.build); nav.debug 1 draws it.\n"
            "# The navigation test loads this scene and checks the baked file and where Walker is after 600 frames.\n";
    w.begin("Sun", glm::vec3(0.f), glm::vec3(1.f));
    w.os << "COMPONENT:LightComponent\nenabled=1\ntype=directional\ncolor=1 1 1\nintensity=3\nattenuationRadius=10\ninnerConeAngle=15\n"
            "outerConeAngle=30\ndirection=0.37139067 0.74278134 0.557086\nuseTemperature=0\ntemperatureK=6500\ncastShadows=1\nEND_COMPONENT\n";
    w.end();
    w.begin("Camera", { 0.f, 11.f, 17.f }, glm::vec3(1.f), { -35.f, 0.f, 0.f });
    w.os << "COMPONENT:CameraComponent\nenabled=1\nfovY=60\nzNear=0.05\nzFar=200\nprimary=1\nEND_COMPONENT\n";
    w.end();
    w.solid("Floor", { 0.f, -0.1f, 0.f }, { 20.f, 0.2f, 20.f }, "checker");
    w.solid("Wall", { 0.f, 1.f, -2.f }, { 0.4f, 2.f, 16.f }, "color 8C6A4AFF 0 0.7");
    w.solid("Platform", { 7.5f, 0.5f, -7.5f }, { 3.f, 1.f, 3.f }, "color B4B4B4FF 0 0.6");
    w.solid("Ramp", { 4.5f, 0.45f, -7.5f }, { 3.2f, 0.1f, 2.f }, "color B4B4B4FF 0 0.6", kRampDeg);
    w.begin("NavVolume", { 0.f, 1.f, 0.f }, glm::vec3(1.f));
    w.os << "COMPONENT:NavMeshVolumeComponent\nenabled=1\nhalfExtent=10 3 10\nagentRadius=0.3\nagentHeight=1.8\nmaxSlope=45\n"
            "maxClimb=0.35\ncellSize=0.1\ncellHeight=0.05\ndata=/Game/scenes/NavProbe.hnav\nEND_COMPONENT\n";
    w.end();
    w.begin("NavGoalA", { 7.5f, 1.f, -7.5f }, { 0.3f, 0.05f, 0.3f }, glm::vec3(0.f), "NavGoal");
    w.mesh("box|0x0|0|0.0000", "color E0B040FF 0 0.5");
    w.end();
    w.begin("NavGoalB", { -5.f, 0.f, 5.f }, { 0.3f, 0.05f, 0.3f }, glm::vec3(0.f), "NavGoal");
    w.mesh("box|0x0|0|0.0000", "color E0B040FF 0 0.5");
    w.end();
    w.begin("Walker", { -3.f, 0.f, 4.f }, glm::vec3(1.f));
    w.os << "COMPONENT:NavAgentComponent\nenabled=1\nspeed=3.5\nangularSpeed=540\narrivalRadius=0.3\nradius=0.3\nheight=1.8\n"
            "useCharacter=1\navoidance=1\nallowPartial=1\nrepathInterval=0.5\nEND_COMPONENT\n"
            "COMPONENT:AIBrainComponent\nenabled=1\npatrolTag=NavGoal\nsightRadius=6\nloseRadius=9\nwaitTime=30\nsenseInterval=0.2\nEND_COMPONENT\n";
    w.end();
    w.begin("WalkerBody", { 0.f, 0.9f, 0.f }, { 0.6f, 1.8f, 0.6f });
    w.os << "PARENT:9\n";   // Walker(파일 안 순번)
    w.mesh("sphere|32x16|0|0.0000", "color 4D73D9FF 0 0.5");
    w.end();
    return w.os.str();
}

// 확인 장면의 볼륨을 구운 .hnav 바이트(파일과 같아야 함).
std::vector<uint8_t> bakeProbe(World& w) {
    NavMeshVolumeComponent* vol = nullptr;
    for (auto* v : w.componentsOf<NavMeshVolumeComponent>()) vol = v;
    if (!vol) return {};
    glm::vec3 mn, mx;
    vol->worldBounds(mn, mx);
    NavInputGeometry geo;
    hnav::collectStaticGeometry(w, mn, mx, geo);
    NavBuildSettings s = vol->settings;
    s.sanitize();
    const NavBuildResult r = hnav::build(geo, s, mn, mx);
    NavFile f;
    f.settings = s;
    f.inputHash = hnav::inputHash(geo, s, mn, mx);
    f.boundsMin = mn;
    f.boundsMax = mx;
    f.polygons = r.polygons;
    f.tile = r.tile;
    return hnav::writeFile(f);
}

int writeProbe(const char* contentDir) {
    registerCoreComponents();
    const std::string text = probeSceneText();
    const std::filesystem::path dir = std::filesystem::path(contentDir) / "scenes";
    { std::ofstream o(dir / "NavProbe.hscene", std::ios::binary); o << text; }
    Scene s;
    AssetManager assets(nullptr);
    SceneSerializer::readScene(stripMeshes(text), s.w, assets);
    const std::vector<uint8_t> bytes = bakeProbe(s.w);
    { std::ofstream o(dir / "NavProbe.hnav", std::ios::binary); o.write((const char*)bytes.data(), (std::streamsize)bytes.size()); }
    std::printf("wrote %s (NavProbe.hscene, NavProbe.hnav %zu bytes)\n", dir.string().c_str(), bytes.size());
    return bytes.empty() ? 1 : 0;
}

void testProbeScene() {
    std::printf("\n== N4 NavProbe.hscene ==\n");
    const std::string content = HARUKA_SOURCE_DIR "/Projects/Samples/Content";
    std::ifstream f(content + "/scenes/NavProbe.hscene", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    check(!text.empty(), "장면 파일을 읽음");
    check(text == probeSceneText(), "장면 파일이 시험의 장면 글과 같음(고쳤으면 --write-probe)");
    hvfs::reset();
    hvfs::mountDirectoryAs(content, "/Game/", 0);
    Scene s;
    AssetManager assets(nullptr);
    SceneSerializer::readScene(stripMeshes(text), s.w, assets);
    Actor* walker = named(s.w, "Walker");
    Actor* goal = named(s.w, "NavGoalA");
    NavMeshVolumeComponent* vol = nullptr;
    for (auto* v : s.w.componentsOf<NavMeshVolumeComponent>()) vol = v;
    check(walker && goal && vol && walker->getComponent<AIBrainComponent>(), "장면의 액터 · 볼륨 · AI 를 찾음");
    if (!walker || !goal || !vol) return;
    check(vol->loaded() && vol->mesh().polygonCount() > 0, "씬을 읽을 때 NavProbe.hnav 를 읽음(" + vol->status() + ")");
    std::vector<uint8_t> disk;
    hvfs::readAll("/Game/scenes/NavProbe.hnav", disk);
    const std::vector<uint8_t> fresh = bakeProbe(s.w);
    check(!disk.empty() && disk == fresh, "저장된 .hnav = 지금 장면을 다시 구운 바이트(" + std::to_string(disk.size()) + " B, 결정성 · 최신)");
    s.ps.tick(kDt);
    for (int i = 0; i < 600; ++i) s.step();
    const float d = flatDist(walker->worldPosition(), goal->worldPosition());
    check(d < 0.5f && std::fabs(walker->worldPosition().y - 1.f) < 0.15f,
          "600 프레임 뒤 Walker 가 판 위 NavGoalA 근처(" + fmt(d) + " m, " + fmt(walker->worldPosition()) + ")");
    check(walker->getComponent<AIBrainComponent>()->state() == EAIState::Wait, "도착 뒤 대기(waitTime 30 초)");
    hvfs::reset();
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--write-probe") == 0) return writeProbe(argv[2]);
    registerCoreComponents();
    testBuildAndQuery();
    testWorld();
    testAvoidance();
    testAI();
    testProbeScene();
    std::printf("\nnavigation: (%d passed, %d failed)\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
