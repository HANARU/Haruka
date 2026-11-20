// 파티클 · 데칼 시험(2026-11-20, 정본 Docs/Particles_And_Decals.md 3절).
// 이미터 시뮬레이션(결정적 시드 · 수명 · 상한 · 비율 · 터뜨리기 · 반복 · 곡선 · 공간 · 중력 · 감쇠), 월드 시간 배율 · 일시정지,
// 그리기 순서(이미터끼리 · 이미터 안 정렬 · 상한 · 드로 합치기), 모양 아틀라스, 씬 파일 왕복, 데칼 상자 · 모양 · 각도 · 추출,
// 확인 장면(ParticleProbe.hscene)을 창 · GPU 없이 검산합니다.

#include "../src/assets/AssetManager.hpp"
#include "../src/components/CoreComponents.hpp"
#include "../src/components/DecalComponent.hpp"
#include "../src/components/ParticleSystemComponent.hpp"
#include "../src/components/RenderExtract.hpp"
#include "../src/renderer/ParticleOrder.hpp"
#include "../src/scene/Actor.hpp"
#include "../src/scene/ComponentFields.hpp"
#include "../src/scene/World.hpp"
#include "../src/sceneio/SceneSerializer.hpp"
#include "../src/vfx/ParticleSim.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

vfx::EmitterDesc fountain(uint32_t seed) {
    vfx::EmitterDesc d;
    d.rate = 200.f; d.burst = 30; d.seed = seed;
    d.lifeMin = 0.5f; d.lifeMax = 1.5f;
    d.speedMin = 1.f; d.speedMax = 3.f;
    d.coneAngle = 30.f; d.radius = 0.1f;
    d.gravity = { 0.f, -9.8f, 0.f }; d.drag = 0.5f;
    d.spinMin = -90.f; d.spinMax = 90.f;
    d.sizeCurve  = { { 0.f, 0.5f }, { 1.f, 1.5f } };
    d.colorCurve = { { 0.f, { 1.f, 0.8f, 0.3f, 1.f } }, { 1.f, { 0.3f, 0.1f, 0.f, 0.f } } };
    d.maxParticles = 5000;
    return d;
}

std::vector<vfx::ParticleInstance> runSim(const vfx::EmitterDesc& d, int steps, const glm::mat4& m = glm::mat4(1.f)) {
    vfx::ParticleSim sim;
    sim.reset(d);
    for (int i = 0; i < steps; ++i) sim.step(d, kDt, m);
    std::vector<vfx::ParticleInstance> out;
    sim.buildInstances(d, m, out);
    return out;
}

bool sameBits(const std::vector<vfx::ParticleInstance>& a, const std::vector<vfx::ParticleInstance>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0);
}

// ── P1 결정적 시드 ────────────────────────────────────────────────────────────
void testDeterminism() {
    std::printf("\n== P1 결정적 시드 ==\n");
    const auto a = runSim(fountain(7), 120);
    const auto b = runSim(fountain(7), 120);
    const auto c = runSim(fountain(8), 120);
    check(!a.empty() && sameBits(a, b), "같은 시드 + 같은 dt 수열 = 비트 단위로 같은 인스턴스 (" + std::to_string(a.size()) + "개)");
    check(!sameBits(a, c), "시드가 다르면 다름");
    // reset 하면 처음부터 같은 수열
    vfx::ParticleSim sim;
    const vfx::EmitterDesc d = fountain(7);
    sim.reset(d);
    for (int i = 0; i < 50; ++i) sim.step(d, kDt, glm::mat4(1.f));
    sim.reset(d);
    for (int i = 0; i < 120; ++i) sim.step(d, kDt, glm::mat4(1.f));
    std::vector<vfx::ParticleInstance> r;
    sim.buildInstances(d, glm::mat4(1.f), r);
    check(sameBits(a, r), "reset 뒤 다시 돌리면 같음");
    // 구 모양도 결정적
    vfx::EmitterDesc s = fountain(3);
    s.shape = vfx::Shape::Sphere;
    check(sameBits(runSim(s, 60), runSim(s, 60)), "구 모양도 같은 결과");
}

// ── P2 수명 · 상한 · 생성 규칙 ────────────────────────────────────────────────
void testLifetimeAndCap() {
    std::printf("\n== P2 수명 · 상한 · 생성 ==\n");
    vfx::EmitterDesc d;
    d.rate = 0.f; d.burst = 10; d.lifeMin = d.lifeMax = 1.f; d.speedMin = d.speedMax = 0.f;
    vfx::ParticleSim sim;
    sim.reset(d);
    for (int i = 0; i < 30; ++i) sim.step(d, kDt, glm::mat4(1.f));
    check(sim.count() == 10, "터뜨리기 10개가 0.5 초에 살아 있음");
    for (int i = 0; i < 32; ++i) sim.step(d, kDt, glm::mat4(1.f));   // 누적 오차로 정확히 1 초에 걸치지 않게 한 틱 더
    check(sim.count() == 0, "수명 1 초가 지나면 모두 빠짐 (" + std::to_string(sim.count()) + ")");

    vfx::EmitterDesc c;
    c.rate = 0.f; c.burst = 100; c.maxParticles = 40; c.lifeMin = c.lifeMax = 5.f;
    sim.reset(c);
    sim.step(c, kDt, glm::mat4(1.f));
    check(sim.count() == 40 && sim.dropped() == 60, "상한 40 에서 나머지 60 은 버림");

    vfx::EmitterDesc r;
    r.rate = 30.f; r.lifeMin = r.lifeMax = 10.f;
    sim.reset(r);
    for (int i = 0; i < 60; ++i) sim.step(r, kDt, glm::mat4(1.f));
    check(sim.count() == 30, "비율 30/초 × 1 초 = 30 (" + std::to_string(sim.count()) + ")");

    vfx::EmitterDesc once;
    once.rate = 20.f; once.loop = false; once.duration = 0.5f; once.lifeMin = once.lifeMax = 0.4f;
    sim.reset(once);
    for (int i = 0; i < 60; ++i) sim.step(once, kDt, glm::mat4(1.f));
    check(sim.spawnedTotal() >= 9 && sim.spawnedTotal() <= 10, "반복 안 함: 0.5 초 동안만 생성 (" + std::to_string(sim.spawnedTotal()) + ")");
    check(sim.count() == 0 && sim.finished(once), "반복 안 함: 수명 뒤 끝남");

    vfx::EmitterDesc loop;
    loop.rate = 0.f; loop.burst = 5; loop.duration = 1.f; loop.lifeMin = loop.lifeMax = 10.f;
    sim.reset(loop);
    for (int i = 0; i < 150; ++i) sim.step(loop, kDt, glm::mat4(1.f));
    check(sim.count() == 15, "반복: 1 초마다 터뜨리기 5 (0 · 1 · 2 초) = 15 (" + std::to_string(sim.count()) + ")");

    sim.reset(r);
    for (int i = 0; i < 10; ++i) sim.step(r, kDt, glm::mat4(1.f), false);
    check(sim.count() == 0, "생성 중이 아니면 내지 않음");
    sim.reset(r);
    sim.step(r, 5.f, glm::mat4(1.f));
    check(sim.count() == 3, "한 틱 dt 는 0.1 초로 자름 (30/초 → 3)");
}

// ── P3 곡선 ───────────────────────────────────────────────────────────────────
void testCurves() {
    std::printf("\n== P3 수명 비율 곡선 ==\n");
    const std::vector<vfx::FloatKey> f = { { 0.f, 1.f }, { 0.5f, 3.f }, { 1.f, 0.f } };
    check(near(vfx::sampleCurve(f, 0.25f), 2.f, 1e-6f) && near(vfx::sampleCurve(f, 0.75f), 1.5f, 1e-6f), "선형 보간");
    check(vfx::sampleCurve(f, -1.f) == 1.f && vfx::sampleCurve(f, 2.f) == 0.f, "범위 밖은 끝 키");
    check(vfx::sampleCurve(std::vector<vfx::FloatKey>{}, 0.3f) == 1.f, "키가 없으면 1");
    const std::vector<vfx::ColorKey> c = { { 0.f, { 1.f, 0.f, 0.f, 1.f } }, { 1.f, { 0.f, 0.f, 1.f, 0.f } } };
    const glm::vec4 m = vfx::sampleCurve(c, 0.5f);
    check(near(m.r, 0.5f, 1e-6f) && near(m.b, 0.5f, 1e-6f) && near(m.a, 0.5f, 1e-6f), "색 곡선 가운데");

    std::vector<vfx::FloatKey> back;
    check(vfx::curveFromText(vfx::curveToText(f), back) && back.size() == 3 && back[1].t == 0.5f && back[1].v == 3.f, "글자 왕복");
    std::vector<vfx::ColorKey> cb;
    check(vfx::curveFromText("1 0 0 1 0; 0 1 1 1 1", cb) && cb.size() == 2 && cb[0].t == 0.f, "읽을 때 t 순으로 정렬");
    check(!vfx::curveFromText("0 1 2", back) && back.size() == 3, "개수가 틀리면 거절(값은 그대로)");
    check(!vfx::curveFromText("0 1 1 1 2", cb), "알파 > 1 은 거절");

    // 인스턴스에 적용
    vfx::EmitterDesc d;
    d.rate = 0.f; d.burst = 1; d.lifeMin = d.lifeMax = 1.f; d.size = 0.2f; d.speedMin = d.speedMax = 0.f;
    d.sizeCurve  = { { 0.f, 1.f }, { 1.f, 0.f } };
    d.colorCurve = { { 0.f, { 1.f, 1.f, 1.f, 1.f } }, { 1.f, { 1.f, 1.f, 1.f, 0.f } } };
    d.sprite = vfx::Sprite::Ring;
    const auto inst = runSim(d, 31);   // 생성 뒤 30 번 갱신 = 0.5 초
    check(inst.size() == 1 && near(inst[0].posSize.w, 0.1f, 1e-3f) && near(inst[0].color.a, 0.5f, 1e-3f),
          "수명 절반: 크기 0.2 × 0.5 · 알파 0.5");
    check(inst.size() == 1 && inst[0].params.y == 2.f, "아틀라스 칸 = Ring(2)");
}

// ── P4 공간 · 힘 ──────────────────────────────────────────────────────────────
void testSpaceAndForces() {
    std::printf("\n== P4 공간 · 중력 · 감쇠 ==\n");
    vfx::EmitterDesc g;
    g.rate = 0.f; g.burst = 1; g.lifeMin = g.lifeMax = 5.f; g.speedMin = g.speedMax = 0.f; g.gravity = { 0.f, -10.f, 0.f };
    const auto a = runSim(g, 60);
    // 반암시적 오일러: 생성한 틱 뒤 59 번 갱신 → y = -g dt² n(n+1)/2, n = 59
    const float expect = -10.f * kDt * kDt * 59.f * 60.f / 2.f;
    check(a.size() == 1 && near(a[0].posSize.y, expect, 1e-3f), "중력 1 초 낙하 (" + std::to_string(a.empty() ? 0.f : a[0].posSize.y) + ")");

    vfx::EmitterDesc dr;
    dr.rate = 0.f; dr.burst = 1; dr.lifeMin = dr.lifeMax = 5.f; dr.speedMin = dr.speedMax = 1.f; dr.coneAngle = 0.f; dr.drag = 1.f;
    vfx::ParticleSim sim;
    sim.reset(dr);
    sim.step(dr, kDt, glm::mat4(1.f));   // 생성
    for (int i = 0; i < 60; ++i) sim.step(dr, kDt, glm::mat4(1.f));
    check(sim.count() == 1 && near(sim.particles()[0].vel.y, std::exp(-1.f), 1e-4f), "감쇠 1/s: 1 초 뒤 속도 e^-1");
    check(sim.count() == 1 && near(sim.particles()[0].vel.x, 0.f, 1e-6f), "원뿔 0도는 +Y 로만");

    // 월드 공간: 생성 뒤 이미터가 움직여도 파티클은 그대로. 로컬: 따라감.
    vfx::EmitterDesc w;
    w.rate = 0.f; w.burst = 1; w.lifeMin = w.lifeMax = 5.f; w.speedMin = w.speedMax = 0.f;
    const glm::mat4 m0 = glm::translate(glm::mat4(1.f), glm::vec3(1, 0, 0));
    const glm::mat4 m1 = glm::translate(glm::mat4(1.f), glm::vec3(5, 0, 0));
    sim.reset(w);
    sim.step(w, kDt, m0);
    sim.step(w, kDt, m1);
    std::vector<vfx::ParticleInstance> out;
    sim.buildInstances(w, m1, out);
    check(out.size() == 1 && near(out[0].posSize.x, 1.f, 1e-6f), "World: 생성 자리에 남음");
    vfx::EmitterDesc l = w;
    l.space = vfx::Space::Local;
    sim.reset(l);
    sim.step(l, kDt, m0);
    sim.step(l, kDt, m1);
    sim.buildInstances(l, m1, out);
    check(out.size() == 1 && near(out[0].posSize.x, 5.f, 1e-6f), "Local: 이미터를 따라감");

    // 회전한 이미터: World 원뿔 축은 이미터 +Y 의 월드 방향
    vfx::EmitterDesc t = dr;
    t.drag = 0.f;
    const glm::mat4 rz = glm::rotate(glm::mat4(1.f), glm::radians(-90.f), glm::vec3(0, 0, 1));   // +Y → +X
    sim.reset(t);
    sim.step(t, kDt, rz);
    check(sim.count() == 1 && near(sim.particles()[0].vel.x, 1.f, 1e-5f), "회전한 이미터의 원뿔 축");
}

// ── P5 월드 시간 배율 · 일시정지 ──────────────────────────────────────────────
void testWorldTime() {
    std::printf("\n== P5 월드 시간 배율 ==\n");
    auto make = [](World& w) {
        Actor* a = w.spawnActor("Emitter");
        auto* ps = a->addComponent<ParticleSystemComponent>();
        ps->desc.rate = 60.f;
        ps->desc.lifeMin = ps->desc.lifeMax = 10.f;
        return ps;
    };
    auto run = [](World& w, int frames) {
        for (int i = 0; i < frames; ++i) { w.tick(kDt); w.tickPostPhysics(); }
    };
    World w1;
    ParticleSystemComponent* p1 = make(w1);
    run(w1, 60);
    check(p1->sim().count() == 60 && p1->instances().size() == 60, "배율 1: 1 초에 60 (" + std::to_string(p1->sim().count()) + ")");
    World w2;
    ParticleSystemComponent* p2 = make(w2);
    w2.setTimeScale(0.5f);
    run(w2, 60);
    check(p2->sim().count() == 30, "배율 0.5: 30 (" + std::to_string(p2->sim().count()) + ")");
    World w3;
    ParticleSystemComponent* p3 = make(w3);
    w3.setTimeScale(0.f);
    run(w3, 60);
    check(p3->sim().count() == 0, "배율 0: 생성 없음");
    // 일시정지 = 틱하지 않음: 상태가 그대로
    const size_t before = p1->sim().count();
    check(p1->sim().count() == before && p1->instances().size() == before, "틱이 없으면 그대로");
    // 정지 · 다시
    p1->stop();
    run(w1, 10);
    check(p1->sim().count() == before, "stop: 새 생성 없음");
    p1->play();
    run(w1, 1);
    check(p1->sim().count() == 1, "play: 처음부터 다시");
    World w4;
    ParticleSystemComponent* p4 = make(w4);
    p4->autoPlay = false;
    run(w4, 30);
    check(p4->sim().count() == 0, "autoPlay 끔: 생성 없음");
}

// ── P6 그리기 순서 ────────────────────────────────────────────────────────────
vfx::ParticleInstance at(float z) {
    vfx::ParticleInstance p;
    p.posSize = glm::vec4(0.f, 0.f, z, 1.f);
    return p;
}

void testOrdering() {
    std::printf("\n== P6 정렬 · 그리기 순서 ==\n");
    std::vector<vfx::ParticleInstance> v = { at(-1.f), at(-5.f), at(-3.f), at(-5.f) };
    v[1].params.z = 1.f; v[3].params.z = 3.f;   // 같은 거리 둘을 구분
    vfx::SortScratch ss;
    vfx::sortBackToFront(v.data(), v.size(), glm::vec3(0.f), ss);
    check(v[0].posSize.z == -5.f && v[1].posSize.z == -5.f && v[2].posSize.z == -3.f && v[3].posSize.z == -1.f, "먼 것부터");
    check(v[0].params.z == 1.f && v[1].params.z == 3.f, "같은 거리는 들어온 순서");
    // 많은 수 · 여러 바이트에 걸친 거리: 비교 정렬과 같은 순서
    {
        hmath::Pcg32 rng(5);
        std::vector<vfx::ParticleInstance> big(5000);
        for (size_t i = 0; i < big.size(); ++i) {
            big[i].posSize = glm::vec4(rng.range(-50.f, 50.f), rng.range(0.f, 3.f), rng.range(-50.f, 50.f), 1.f);
            if (i % 7 == 0) big[i].posSize = big[i / 2].posSize;   // 같은 거리 섞기
            big[i].params.z = (float)i;
        }
        std::vector<vfx::ParticleInstance> ref = big;
        std::stable_sort(ref.begin(), ref.end(), [](const vfx::ParticleInstance& a, const vfx::ParticleInstance& b) {
            return glm::dot(glm::vec3(a.posSize), glm::vec3(a.posSize)) > glm::dot(glm::vec3(b.posSize), glm::vec3(b.posSize));
        });
        vfx::sortBackToFront(big.data(), big.size(), glm::vec3(0.f), ss);
        bool same = true;
        for (size_t i = 0; i < big.size(); ++i) same = same && big[i].params.z == ref[i].params.z;
        check(same, "5,000 개: 안정 비교 정렬과 같은 순서");
    }

    const std::vector<vfx::ParticleInstance> near3 = { at(-1.f), at(-2.f), at(-1.5f) };
    const std::vector<vfx::ParticleInstance> far2  = { at(-10.f), at(-9.f) };
    const std::vector<vfx::ParticleInstance> mid2  = { at(-5.f), at(-6.f) };
    std::vector<RenderParticleBatch> b(3);
    b[0] = { near3.data(), 3, vfx::Blend::Alpha,    glm::vec3(0, 0, -1.5f) };
    b[1] = { far2.data(),  2, vfx::Blend::Additive, glm::vec3(0, 0, -9.5f) };
    b[2] = { mid2.data(),  2, vfx::Blend::Additive, glm::vec3(0, 0, -5.5f) };
    std::vector<vfx::ParticleInstance> out;
    std::vector<ParticleRange> ranges;
    ParticleOrderScratch s;
    buildParticleStream(b, glm::vec3(0.f), 100, out, ranges, s);
    check(out.size() == 7 && out[0].posSize.z == -10.f && out[2].posSize.z == -5.f, "이미터끼리 먼 것부터(가산은 안을 정렬하지 않음)");
    check(out.size() == 7 && out[4].posSize.z == -2.f && out[5].posSize.z == -1.5f && out[6].posSize.z == -1.f, "알파 이미터 안은 먼 것부터");
    check(ranges.size() == 2 && ranges[0].additive && ranges[0].count == 4 && !ranges[1].additive && ranges[1].first == 4,
          "이웃한 가산 구간을 드로 하나로 합침");
    buildParticleStream(b, glm::vec3(0.f), 4, out, ranges, s);
    check(out.size() == 4 && out[0].posSize.z == -5.f && out[1].posSize.z == -2.f, "상한을 넘으면 먼 이미터부터 잘림");
    buildParticleStream({}, glm::vec3(0.f), 100, out, ranges, s);
    check(out.empty() && ranges.empty(), "이미터가 없으면 비움");
}

// ── P7 아틀라스 · 씬 파일 ─────────────────────────────────────────────────────
void testAtlasAndFields() {
    std::printf("\n== P7 모양 아틀라스 · 씬 파일 ==\n");
    std::vector<uint8_t> px;
    vfx::buildSpriteAtlas(px);
    auto alphaAt = [&](int x, int y) { return px[((size_t)y * vfx::kAtlasSize + (size_t)x) * 4 + 3]; };
    const int h = vfx::kAtlasSize / 4;   // 칸 가운데
    check(px.size() == (size_t)vfx::kAtlasSize * vfx::kAtlasSize * 4, "128² RGBA");
    check(alphaAt(h, h) > 250 && alphaAt(3 * h, h) > 250 && alphaAt(3 * h, 3 * h) > 250, "원 · 단단한 원 · 별 가운데는 불투명");
    check(alphaAt(h, 3 * h) == 0, "고리 가운데는 투명");
    check(alphaAt(0, 0) == 0 && alphaAt(vfx::kAtlasSize / 2, h) == 0, "칸 가장자리는 투명(번짐 방지)");

    ParticleSystemComponent a;
    a.desc = fountain(42);
    a.desc.space = vfx::Space::Local; a.desc.shape = vfx::Shape::Sphere; a.desc.blend = vfx::Blend::Alpha;
    a.desc.sprite = vfx::Sprite::Star; a.desc.loop = false; a.autoPlay = false;
    ComponentFieldWriter w;
    a.writeFields(w);
    ParticleSystemComponent b;
    for (const auto& [k, v] : w.fields()) check(b.readField(k, v), "읽음: " + k);
    ComponentFieldWriter w2;
    b.writeFields(w2);
    check(w.fields() == w2.fields(), "ParticleSystemComponent 왕복");
    ParticleSystemComponent c;
    c.readField("blend", "glow");
    c.readField("maxParticles", "999999");
    check(c.desc.blend == vfx::Blend::Additive && c.desc.maxParticles == 1000, "틀린 값은 무시");

    DecalComponent d;
    d.size = { 2.f, 1.f, 3.f }; d.color = { 0.1f, 0.2f, 0.3f }; d.opacity = 0.5f; d.roughness = 0.9f;
    d.blend = DecalComponent::Blend::Multiply; d.shape = DecalComponent::Shape::Ring;
    ComponentFieldWriter dw;
    d.writeFields(dw);
    DecalComponent e;
    for (const auto& [k, v] : dw.fields()) e.readField(k, v);
    ComponentFieldWriter dw2;
    e.writeFields(dw2);
    check(dw.fields() == dw2.fields() && e.shape == DecalComponent::Shape::Ring, "DecalComponent 왕복");
}

// ── P8 데칼 계산 · 추출 ───────────────────────────────────────────────────────
void testDecals() {
    std::printf("\n== P8 데칼 ==\n");
    const glm::mat4 box = glm::translate(glm::mat4(1.f), glm::vec3(2, 0, 0)) * glm::scale(glm::mat4(1.f), glm::vec3(2, 1, 2));
    const glm::mat4 inv = glm::inverse(box);
    const glm::vec3 up(0, 1, 0);
    check(near(hdecal::weight(inv, up, { 2, 0, 0 }, up, 0), 1.f, 1e-6f), "상자 가운데 · 윗면 = 1");
    check(hdecal::weight(inv, up, { 3.5f, 0, 0 }, up, 0) == 0.f, "상자 밖 = 0");
    check(hdecal::weight(inv, up, { 2, 0.6f, 0 }, up, 0) == 0.f, "투영 깊이 밖 = 0");
    check(hdecal::weight(inv, up, { 2, 0, 0 }, glm::vec3(1, 0, 0), 0) == 0.f, "옆면(법선 ⟂ 투영) = 0");
    check(near(hdecal::weight(inv, up, { 2, 0, 0 }, glm::normalize(glm::vec3(1, 1, 0)), 0), 1.f, 1e-6f), "45도 면은 다 보임");
    check(hdecal::weight(inv, up, { 2, 0, 0 }, up, 2) == 0.f && hdecal::weight(inv, up, { 2.75f, 0, 0 }, up, 2) > 0.9f,
          "고리: 가운데 0 · 테 1");
    check(hdecal::weight(inv, up, { 2.9f, 0, 0.9f }, up, 3) > 0.9f && hdecal::weight(inv, up, { 2.9f, 0, 0.9f }, up, 0) == 0.f,
          "사각은 모서리까지 · 원은 아님");

    World w;
    Actor* a = w.spawnActor("Decal");
    a->setPosition({ 1, 0, -2 });
    a->setRotation(glm::angleAxis(glm::radians(90.f), glm::vec3(1, 0, 0)));   // +Y → +Z
    auto* dc = a->addComponent<DecalComponent>();
    dc->size = { 1.f, 0.4f, 1.f };
    Actor* off = w.spawnActor("Hidden");
    off->addComponent<DecalComponent>()->opacity = 0.f;
    Actor* em = w.spawnActor("Emitter");
    auto* ps = em->addComponent<ParticleSystemComponent>();
    ps->desc.blend = vfx::Blend::Alpha;
    ps->desc.rate = 60.f;
    for (int i = 0; i < 10; ++i) { w.tick(kDt); w.tickPostPhysics(); }
    RenderSceneInput in;
    extractRenderScene(w, in);
    check(in.decals.size() == 1, "켜진 데칼만 추출(불투명도 0 은 뺌)");
    if (!in.decals.empty()) {
        const RenderDecal& d = in.decals[0];
        const glm::vec3 c = glm::vec3(d.worldToBox * glm::vec4(1, 0, -2, 1));
        const glm::vec3 e = glm::vec3(d.worldToBox * glm::vec4(1, 0, -2 + 0.2f, 1));
        check(glm::length(c) < 1e-5f && near(e.y, 0.5f, 1e-5f), "worldToBox: 액터 위치 = 원점, 투영 깊이 0.4 m");
        check(near(d.axis.z, 1.f, 1e-5f), "axis = 상자 +Y 의 월드 방향");
    }
    check(in.particles.size() == 1 && in.particles[0].count == ps->instances().size() && in.particles[0].count == 10 &&
          in.particles[0].blend == vfx::Blend::Alpha, "파티클 이미터 추출");
    em->setActive(false);
    extractRenderScene(w, in);
    check(in.particles.empty(), "끈 액터의 이미터는 빼");
}

// ── P9 확인 장면 ──────────────────────────────────────────────────────────────
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

std::vector<vfx::ParticleInstance> probeRun(const std::string& text, size_t& emitters, size_t& decals) {
    World w;
    AssetManager assets(nullptr);
    SceneSerializer::readScene(text, w, assets);
    for (int i = 0; i < 90; ++i) { w.tick(kDt); w.tickPostPhysics(); }
    RenderSceneInput in;
    extractRenderScene(w, in);
    emitters = in.particles.size();
    decals   = in.decals.size();
    std::vector<vfx::ParticleInstance> out;
    std::vector<ParticleRange> ranges;
    ParticleOrderScratch s;
    buildParticleStream(in.particles, glm::vec3(0, 1.6f, 3.f), vfx::kMaxParticles, out, ranges, s);
    return out;
}

void testProbeScene() {
    std::printf("\n== P9 ParticleProbe.hscene ==\n");
    std::ifstream f(HARUKA_SOURCE_DIR "/Projects/Samples/Content/scenes/ParticleProbe.hscene", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    check(!ss.str().empty(), "장면 파일을 읽음");
    const std::string text = stripMeshes(ss.str());
    size_t e1 = 0, d1 = 0, e2 = 0, d2 = 0;
    const auto a = probeRun(text, e1, d1);
    const auto b = probeRun(text, e2, d2);
    check(e1 >= 3 && d1 >= 2, "이미터 3개 이상 · 데칼 2개 이상 (" + std::to_string(e1) + " · " + std::to_string(d1) + ")");
    check(!a.empty() && sameBits(a, b), "1.5 초 뒤 그릴 목록이 두 번 같음 (" + std::to_string(a.size()) + "개)");
}

} // namespace

int main() {
    std::printf("Particle / decal test (Docs/Particles_And_Decals.md)\n");
    registerCoreComponents();
    testDeterminism();
    testLifetimeAndCap();
    testCurves();
    testSpaceAndForces();
    testWorldTime();
    testOrdering();
    testAtlasAndFields();
    testDecals();
    testProbeScene();
    std::printf("\n(%d passed, %d failed)\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
