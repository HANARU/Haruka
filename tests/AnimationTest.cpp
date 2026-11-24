// 애니메이션 체계(클립 · 샘플링 · 섞기 · glTF 가져오기 · 캐시 · 재생기 · 상태 기계)를 화면 없이 검산합니다
// (2026-11-20, Docs/Animation_System.md 3절 체크리스트).
//
// 이 시험이 존재하는 이유: 보간 공식(특히 CUBICSPLINE 의 접선 x 키 간격), 쿼터니언 반구, 반복 경계의 이벤트 · 루트 모션은
// 한 줄만 어긋나도 화면에서는 "조금 튀는" 정도로만 보여 원인을 찾기 어렵습니다. 그래서 손으로 계산한 값과 맞대어 봅니다.
//   1. 트랙 샘플링(STEP · LINEAR · 회전 slerp · CUBICSPLINE), 범위 밖 · 키 하나, 검사(validate)
//   2. 커서 탐색 = 처음부터 찾기, wrapTime, 자세 섞기 · 가산, TRS 전역 행렬
//   3. 시험 모델 TestArm.glb 가져오기(클립 셋, 관절 아닌 노드 채널 무시, 영향 8개 → 큰 4개), 캐시 v10 왕복
//   4. AnimationComponent: 자동 재생 · 반복 · 한 번 재생 · 거꾸로 · 크로스페이드 · 가산 · 이벤트 · 루트 모션 · 손 자세와 충돌 · 저장 왕복
//   5. 상태 기계: bool · float · 트리거 · 클립 끝 · 아무 상태, 컴포넌트에 붙여 전환
//   6. 모프 타깃: 스킨 메시의 타깃 · 무게 클립, 스킨 없는 모프 메시(관절 하나짜리 스킨), 캐시 왕복
//   7. 트윈: 위치 · 회전 · 배율 · float, 이징, 지연, 되풀이 · 왕복, 끝 콜백, 액터 삭제 · 같은 속성 대신 · 취소
// 실행: build/Release/HarukaAnimTest.exe

#include "../src/assets/AnimStateMachine.hpp"
#include "../src/assets/Animation.hpp"
#include "../src/assets/AssetManager.hpp"
#include "../src/assets/GassetCache.hpp"
#include "../src/assets/GltfLoader.hpp"
#include "../src/assets/Skin.hpp"
#include "../src/components/AnimationComponent.hpp"
#include "../src/components/SkinnedMeshComponent.hpp"
#include "../src/geometry/MeshSource.hpp"
#include "../src/scene/Actor.hpp"
#include "../src/scene/ComponentFields.hpp"
#include "../src/scene/World.hpp"
#include "../src/vfs/Vfs.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// SkinnedMeshComponent::onFieldsLoaded 가 부르는 자리만 채웁니다(이 시험은 setSkinData 로 데이터를 직접 넣음).
std::vector<AssetPair> AssetManager::loadAll(const std::string&) { return {}; }

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) ++g_failed;
    std::printf("  [%s] %s\n", cond ? " OK " : "FAIL", what.c_str());
}

bool nearF(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
bool near3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
    return nearF(a.x, b.x, eps) && nearF(a.y, b.y, eps) && nearF(a.z, b.z, eps);
}
bool nearQ(const glm::quat& a, const glm::quat& b, float eps = 1e-5f) {
    return std::fabs(std::fabs(glm::dot(a, b)) - 1.f) <= eps;   // q 와 -q 는 같은 회전
}
glm::quat axisDeg(const glm::vec3& axis, float deg) { return glm::angleAxis(glm::radians(deg), axis); }
std::vector<float> qv(const glm::quat& q) { return { q.x, q.y, q.z, q.w }; }

const char* kArm = "assets/models/anim/TestArm.glb";

// ── 1. 트랙 샘플링 ─────────────────────────────────────────────────────────────
void testSampling() {
    std::printf("== 1. 트랙 샘플링 ==\n");
    hanim::Track t;
    t.path = hanim::Path::Translation; t.comps = 3; t.interp = hanim::Interp::Linear;
    t.times = { 0.f, 1.f, 3.f };
    t.values = { 0, 0, 0,  1, 2, 3,  1, 2, 7 };
    float o[4];
    hanim::sampleTrack(t, 0.5f, o);   check(near3({ o[0], o[1], o[2] }, { 0.5f, 1.f, 1.5f }), "LINEAR 이동: 첫 구간 가운데");
    hanim::sampleTrack(t, 2.f, o);    check(near3({ o[0], o[1], o[2] }, { 1.f, 2.f, 5.f }), "LINEAR 이동: 둘째 구간 가운데(키 간격이 다름)");
    hanim::sampleTrack(t, -1.f, o);   check(near3({ o[0], o[1], o[2] }, { 0, 0, 0 }), "첫 키 앞은 첫 값");
    hanim::sampleTrack(t, 9.f, o);    check(near3({ o[0], o[1], o[2] }, { 1, 2, 7 }), "마지막 키 뒤는 마지막 값");
    hanim::sampleTrack(t, 1.f, o);    check(near3({ o[0], o[1], o[2] }, { 1, 2, 3 }), "키 시간 그 자리는 그 키 값");

    t.interp = hanim::Interp::Step;
    hanim::sampleTrack(t, 0.99f, o);  check(near3({ o[0], o[1], o[2] }, { 0, 0, 0 }), "STEP: 다음 키 전까지 앞 값");
    hanim::sampleTrack(t, 1.0f, o);   check(near3({ o[0], o[1], o[2] }, { 1, 2, 3 }), "STEP: 키 시간부터 그 값");

    hanim::Track one = t;
    one.times = { 0.5f }; one.values = { 4, 5, 6 };
    hanim::sampleTrack(one, 0.f, o);  check(near3({ o[0], o[1], o[2] }, { 4, 5, 6 }), "키 하나짜리 트랙은 언제나 그 값");

    hanim::Track r;
    r.path = hanim::Path::Rotation; r.comps = 4; r.interp = hanim::Interp::Linear;
    r.times = { 0.f, 1.f };
    const glm::quat q0 = axisDeg({ 0, 0, 1 }, 0.f), q1 = axisDeg({ 0, 0, 1 }, 90.f);
    r.values = qv(q0);
    auto add = qv(q1); r.values.insert(r.values.end(), add.begin(), add.end());
    hanim::sampleTrack(r, 0.5f, o);
    check(nearQ(glm::quat(o[3], o[0], o[1], o[2]), axisDeg({ 0, 0, 1 }, 45.f)), "LINEAR 회전은 slerp(가운데 = 45°)");
    hanim::sampleTrack(r, 0.25f, o);
    check(nearQ(glm::quat(o[3], o[0], o[1], o[2]), axisDeg({ 0, 0, 1 }, 22.5f)), "slerp 는 각도에 선형(1/4 = 22.5°, nlerp 와 다름)");
    // 반대 반구로 저장한 키: 짧은 쪽으로 돎
    r.values = qv(q0);
    add = qv(-axisDeg({ 0, 0, 1 }, 90.f)); r.values.insert(r.values.end(), add.begin(), add.end());
    hanim::sampleTrack(r, 0.5f, o);
    check(nearQ(glm::quat(o[3], o[0], o[1], o[2]), axisDeg({ 0, 0, 1 }, 45.f)), "반대 반구 키도 짧은 쪽으로 보간");

    // CUBICSPLINE: 키마다 (in, value, out). p = h00 v0 + h10 td b0 + h01 v1 + h11 td a1
    hanim::Track c;
    c.path = hanim::Path::Translation; c.comps = 3; c.interp = hanim::Interp::CubicSpline;
    c.times = { 1.f, 3.f };   // td = 2
    c.values = { 9, 9, 9,  0, 0, 0,  1, 0, 0,     // 키 0: in(무시) · 값 0 · out (1,0,0)
                 0, 2, 0,  4, 0, 0,  9, 9, 9 };   // 키 1: in (0,2,0) · 값 (4,0,0) · out(무시)
    const float u = 0.25f, td = 2.f;
    const float h00 = 2 * u * u * u - 3 * u * u + 1, h10 = u * u * u - 2 * u * u + u, h01 = -2 * u * u * u + 3 * u * u, h11 = u * u * u - u * u;
    const glm::vec3 want = h00 * glm::vec3(0) + h10 * td * glm::vec3(1, 0, 0) + h01 * glm::vec3(4, 0, 0) + h11 * td * glm::vec3(0, 2, 0);
    hanim::sampleTrack(c, 1.5f, o);
    check(near3({ o[0], o[1], o[2] }, want), "CUBICSPLINE: 접선에 키 간격을 곱한 Hermite 값");
    hanim::sampleTrack(c, 3.f, o);
    check(near3({ o[0], o[1], o[2] }, { 4, 0, 0 }), "CUBICSPLINE: 마지막 키는 가운데 값(접선이 아님)");

    // 검사
    hanim::Clip clip; clip.name = "x";
    clip.tracks.push_back(t);
    clip.tracks[0].target = 0;
    std::string err;
    check(hanim::validate(clip, 1, 0, &err), "올바른 클립은 통과");
    hanim::Clip bad = clip; bad.tracks[0].times = { 0.f, 2.f, 1.f };
    check(!hanim::validate(bad, 1, 0, &err) && err.find("ascending") != std::string::npos, "시간이 내림차순이면 거절");
    bad = clip; bad.tracks[0].values.pop_back();
    check(!hanim::validate(bad, 1, 0, &err), "값 개수가 키와 안 맞으면 거절");
    bad = clip; bad.tracks[0].target = 3;
    check(!hanim::validate(bad, 1, 0, &err), "관절 번호가 범위 밖이면 거절");
    bad = clip; bad.tracks[0].values[1] = NAN;
    check(!hanim::validate(bad, 1, 0, &err), "NaN 값은 거절");
    check(nearF(hanim::computeDuration(clip), 3.f), "길이 = 가장 늦은 키");
    hanim::Clip empty;
    hanim::Pose p(2);
    hanim::sample(empty, 1.f, p);
    check(near3(p[0].t, glm::vec3(0)) && nearQ(p[1].r, glm::quat(1, 0, 0, 0)), "빈 클립은 자세를 바꾸지 않음");
}

// ── 2. 커서 · 감기 · 섞기 · TRS ────────────────────────────────────────────────
void testPoseMath() {
    std::printf("== 2. 커서 · 감기 · 섞기 · TRS 전역 행렬 ==\n");
    hanim::Track t;
    t.path = hanim::Path::Translation; t.comps = 3; t.interp = hanim::Interp::Linear;
    for (int k = 0; k < 200; ++k) { t.times.push_back(k * 0.05f + (k % 3) * 0.001f); t.values.insert(t.values.end(), { (float)k, (float)(k * k % 7), 0.f }); }
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> ud(-1.f, 11.f);
    uint32_t cur = 0;
    bool same = true;
    float a[3], b[3];
    float tm = 0.f;
    for (int i = 0; i < 2000; ++i) {
        tm = (i % 500 < 400) ? tm + 0.013f : ud(rng);   // 대부분 앞으로, 가끔 아무 곳
        if (tm > 11.f) tm = 0.f;
        hanim::sampleTrack(t, tm, a, &cur);
        hanim::sampleTrack(t, tm, b, nullptr);
        same = same && a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
    }
    check(same, "커서로 찾은 값 = 처음부터 찾은 값(앞으로 · 되감기 2,000번)");

    check(nearF(hanim::wrapTime(2.5f, 1.f, true), 0.5f) && nearF(hanim::wrapTime(-0.25f, 1.f, true), 0.75f), "반복 감기(양수 · 음수)");
    check(nearF(hanim::wrapTime(2.5f, 1.f, false), 1.f) && nearF(hanim::wrapTime(-1.f, 1.f, false), 0.f), "한 번 재생은 자름");
    check(hanim::wrapTime(1.f, 1.f, true) == 0.f && hanim::wrapTime(5.f, 0.f, true) == 0.f, "끝 = 처음, 길이 0 이면 0");

    hanim::Pose pa(1), pb(1), out;
    pa[0] = { { 0, 0, 0 }, axisDeg({ 0, 1, 0 }, 0), { 1, 1, 1 } };
    pb[0] = { { 2, 4, 0 }, -axisDeg({ 0, 1, 0 }, 90), { 3, 3, 3 } };   // 반대 반구
    hanim::blend(pa, pb, 0.f, out); check(near3(out[0].t, pa[0].t) && nearQ(out[0].r, pa[0].r), "섞기 w = 0 은 a");
    hanim::blend(pa, pb, 1.f, out); check(near3(out[0].t, pb[0].t) && nearQ(out[0].r, pb[0].r) && near3(out[0].s, { 3, 3, 3 }), "섞기 w = 1 은 b");
    hanim::blend(pa, pb, 0.5f, out);
    check(near3(out[0].t, { 1, 2, 0 }) && nearQ(out[0].r, axisDeg({ 0, 1, 0 }, 45)) && near3(out[0].s, { 2, 2, 2 }),
          "섞기 w = 0.5: 이동 · 배율 가운데, 반대 반구 회전도 45°");
    hanim::blend(pa, pb, 0.5f, pa);
    check(near3(pa[0].t, { 1, 2, 0 }), "out 이 입력과 같은 자세여도 됨");

    hanim::Pose base(1), ref(1), addp(1);
    base[0] = { { 1, 0, 0 }, axisDeg({ 0, 0, 1 }, 10), { 2, 2, 2 } };
    ref[0]  = { { 0, 1, 0 }, axisDeg({ 1, 0, 0 }, 30), { 1, 1, 1 } };
    hanim::Pose b2 = base;
    hanim::addAdditive(b2, ref, ref, 1.f);
    check(near3(b2[0].t, base[0].t) && nearQ(b2[0].r, base[0].r) && near3(b2[0].s, base[0].s), "가산: add = ref 이면 그대로");
    addp[0] = { { 0, 3, 0 }, ref[0].r * axisDeg({ 0, 1, 0 }, 40), { 2, 1, 1 } };
    b2 = base;
    hanim::addAdditive(b2, addp, ref, 0.5f);
    check(near3(b2[0].t, { 1, 1, 0 }) && nearQ(b2[0].r, base[0].r * axisDeg({ 0, 1, 0 }, 20)) && near3(b2[0].s, { 3, 2, 2 }),
          "가산 w = 0.5: 차이의 절반(이동 +1 · 회전 20° · 배율 1.5배)");
    b2 = base;
    hanim::addAdditive(b2, addp, ref, 0.f);
    check(near3(b2[0].t, base[0].t) && nearQ(b2[0].r, base[0].r), "가산 w = 0 이면 그대로");

    SkinSourceData s;
    s.joints.resize(2);
    s.joints[0].restT = { 0, 1, 0 }; s.joints[0].restR = axisDeg({ 0, 1, 0 }, 30);
    s.joints[1].parent = 0; s.joints[1].restT = { 0, 0.5f, 0 }; s.joints[1].restR = axisDeg({ 1, 0, 0 }, 20);
    hskin::buildOrder(s);
    std::vector<glm::mat4> g1, g2;
    hskin::computeGlobals(s, nullptr, g1);
    hanim::Pose rest;
    hskin::restPose(s, rest);
    hskin::computeGlobals(s, rest.data(), g2);
    float err = 0.f;
    for (int j = 0; j < 2; ++j) for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) err = std::max(err, std::fabs(g1[j][c][r] - g2[j][c][r]));
    check(err < 1e-6f, "TRS 판(쉬는 자세) = 회전만 판");
    rest[1].t = { 0, 2, 0 };
    rest[1].s = { 2, 2, 2 };
    hskin::computeGlobals(s, rest.data(), g2);
    const glm::vec3 p1 = glm::vec3(g2[1][3]);
    const glm::vec3 want = glm::vec3(0, 1, 0) + axisDeg({ 0, 1, 0 }, 30) * glm::vec3(0, 2, 0);
    check(near3(p1, want) && nearF(glm::length(glm::vec3(g2[1][0])), 2.f), "TRS 판은 관절 이동 · 배율을 씀");
}

// ── 3. glTF 가져오기 · 캐시 ────────────────────────────────────────────────────
std::vector<MeshSourceData> testImport() {
    std::printf("== 3. TestArm.glb 가져오기 ==\n");
    std::vector<MeshSourceData> meshes;
    try { meshes = GltfLoader::loadAll(kArm); } catch (const std::exception& e) { check(false, std::string("읽기 실패: ") + e.what()); return {}; }
    check(meshes.size() == 1 && meshes[0].skin, "스킨 메시 하나");
    if (meshes.empty() || !meshes[0].skin) return meshes;
    const SkinSourceData& s = *meshes[0].skin;
    check(s.joints.size() == 4 && hskin::findJoint(s, "Hand") == 3, "관절 넷(Root · Upper · Lower · Hand)");
    check(s.clips.size() == 4 && s.clips[0].name == "Wave" && s.clips[1].name == "Bounce" && s.clips[2].name == "Walk" &&
          s.clips[3].name == "Flex", "클립 넷, 파일 순서");
    if (s.clips.size() != 4) return meshes;
    const hanim::Clip& wave = s.clips[0];
    check(wave.tracks.size() == 2, "Wave: 관절이 아닌 Marker 노드 채널은 빠짐(트랙 둘)");
    check(wave.tracks[0].interp == hanim::Interp::Linear && wave.tracks[1].interp == hanim::Interp::Step, "보간 형식 LINEAR · STEP");
    check(s.clips[1].tracks[0].interp == hanim::Interp::CubicSpline && s.clips[1].tracks[0].values.size() == 18, "CUBICSPLINE: 키마다 3 x 3 값");
    check(nearF(wave.duration, 1.f) && nearF(s.clips[1].duration, 1.f), "길이 1 초");
    hanim::Pose p;
    hskin::restPose(s, p);
    hanim::sample(wave, 0.25f, p);
    check(nearQ(p[1].r, axisDeg({ 0, 0, 1 }, 22.5f)) && nearQ(p[2].r, glm::quat(1, 0, 0, 0)), "Wave 0.25 초: Upper 22.5°, Lower(STEP) 아직 0°");
    hanim::sample(wave, 0.6f, p);
    check(nearQ(p[2].r, axisDeg({ 0, 0, 1 }, 30.f)), "Wave 0.6 초: Lower 가 STEP 으로 30°");
    hskin::restPose(s, p);
    hanim::sample(s.clips[1], 0.5f, p);
    const float u = 0.5f;
    const float yWant = (u * u * u - 2 * u * u + u) * 1.f + (-2 * u * u * u + 3 * u * u) * 0.5f + (u * u * u - u * u) * 1.f;
    check(near3(p[0].t, { 0, yWant, 0 }) && near3(p[3].s, { 1.5f, 1.5f, 1.5f }), "Bounce 0.5 초: Root 이동 CUBICSPLINE · Hand 배율 1.5");

    // 팔꿈치 고리(정점 8 ~ 11)는 영향 8개 중 큰 4개: Lower .50 · Upper .40 · Upper .04 · Lower .03 → 합 .97 로 나눔
    bool trimmed = true;
    for (int v = 8; v < 12; ++v) {
        const uint16_t* ji = &s.jointIndex[(size_t)v * 4];
        const float* w = &s.jointWeight[(size_t)v * 4];
        trimmed = trimmed && ji[0] == 2 && ji[1] == 1 && ji[2] == 1 && ji[3] == 2 &&
                  nearF(w[0], 0.50f / 0.97f) && nearF(w[1], 0.40f / 0.97f) && nearF(w[2], 0.04f / 0.97f) && nearF(w[3], 0.03f / 0.97f);
    }
    check(trimmed, "JOINTS_1 · WEIGHTS_1: 8개 중 큰 4개를 남기고 다시 정규화");
    bool plain = true;
    for (int v = 0; v < 8; ++v) plain = plain && nearF(s.jointWeight[(size_t)v * 4], 1.f);
    check(plain, "영향이 하나인 정점은 그대로");
    return meshes;
}

void testCache(const std::vector<MeshSourceData>& arm, const fs::path& tmp) {
    std::printf("== 3b. 캐시 v10 클립 왕복 ==\n");
    if (arm.empty() || !arm[0].skin) { check(false, "모델이 없어 건너뜀"); return; }
    GassetCache::setDirectory((tmp / "cache").string());
    GassetCache::save(kArm, arm);
    check(GassetCache::isValid(kArm), "저장한 캐시가 유효");
    std::vector<MeshSourceData> back;
    try { back = GassetCache::load(kArm); } catch (const std::exception& e) { check(false, e.what()); return; }
    const auto& a = arm[0].skin->clips;
    const auto& b = back.empty() || !back[0].skin ? a : back[0].skin->clips;
    bool same = !back.empty() && back[0].skin && a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); ++i) {
        same = a[i].name == b[i].name && a[i].duration == b[i].duration && a[i].tracks.size() == b[i].tracks.size();
        for (size_t k = 0; same && k < a[i].tracks.size(); ++k) {
            const auto& x = a[i].tracks[k];
            const auto& y = b[i].tracks[k];
            same = x.target == y.target && x.path == y.path && x.interp == y.interp && x.comps == y.comps && x.times == y.times && x.values == y.values;
        }
    }
    check(same, "클립 이름 · 길이 · 트랙(관절 · 경로 · 보간 · 시간 · 값)이 그대로");
    const bool morphSame = !back.empty() && back[0].skin && back[0].skin->morphCount == arm[0].skin->morphCount &&
                           back[0].skin->morphPos == arm[0].skin->morphPos && back[0].skin->morphNrm == arm[0].skin->morphNrm &&
                           back[0].skin->defaultWeights == arm[0].skin->defaultWeights;
    check(morphSame, "모프 타깃(수 · 위치 · 노멀 차이 · 기본 무게)이 그대로");
}

// ── 4. 재생기 ─────────────────────────────────────────────────────────────────
struct FakeHandPose : Component {
    const char* typeName() const override { return "HandPoseComponent"; }
};

struct Rig {
    World world;
    Actor* actor = nullptr;
    SkinnedMeshComponent* mesh = nullptr;
    AnimationComponent* anim = nullptr;
    explicit Rig(const std::vector<MeshSourceData>& arm, bool withAnim = true) {
        actor = world.spawnActor("Arm");
        mesh = actor->addComponent<SkinnedMeshComponent>();
        mesh->setSkinData(arm[0].skin, std::make_shared<const std::vector<MeshSourceVertex>>(arm[0].vertices));
        if (withAnim) anim = actor->addComponent<AnimationComponent>();
    }
};

void testPlayer(const std::vector<MeshSourceData>& arm) {
    std::printf("== 4. AnimationComponent 재생 ==\n");
    if (arm.empty() || !arm[0].skin || arm[0].skin->clips.size() != 4) { check(false, "모델이 없어 건너뜀"); return; }
    {
        Rig r(arm);
        r.world.tickDisplay(0.25f);
        check(r.anim->currentClip() == "Wave" && nearF(r.anim->time(), 0.25f), "자동 재생: 첫 클립 Wave, 0.25 초");
        check(nearQ(r.mesh->pose()[1].r, axisDeg({ 0, 0, 1 }, 22.5f)), "자세가 메시에 들어감(Upper 22.5°)");
        r.world.tickDisplay(1.0f);
        check(nearF(r.anim->time(), 0.25f, 1e-4f) && r.anim->isPlaying(), "반복: 1.25 초 → 0.25 초");
        std::vector<MeshSourceVertex> outv(arm[0].vertices.size());
        glm::vec3 smin, smax, bmin, bmax;
        r.mesh->skinInto(outv.data(), smin, smax);
        check(r.mesh->localBounds(bmin, bmax) && near3(bmin, smin) && near3(bmax, smax), "경계 = 마지막으로 스키닝한 자세의 경계(A6-3)");
    }
    {
        Rig r(arm);
        r.anim->clip = "Bounce";
        r.anim->loop = false;
        int finished = 0;
        std::string finName;
        r.anim->onFinished.add([&](const std::string& n) { ++finished; finName = n; });
        r.world.tickDisplay(0.6f);
        r.world.tickDisplay(0.6f);
        r.world.tickDisplay(0.6f);
        check(!r.anim->isPlaying() && nearF(r.anim->time(), 1.f) && finished == 1 && finName == "Bounce", "한 번 재생: 끝에서 멈추고 끝 알림 한 번");
        check(near3(r.mesh->pose()[3].s, { 2, 2, 2 }), "멈춘 자세 = 마지막 키(Hand 배율 2)");
    }
    {
        Rig r(arm);
        r.anim->speed = -1.f;
        r.world.tickDisplay(0.25f);
        check(nearF(r.anim->time(), 0.75f), "거꾸로 재생: 끝에서 시작해 0.75 초");
    }
    {
        Rig r(arm);
        r.world.tickDisplay(0.25f);   // Wave 0.25
        r.anim->play("Bounce", 0.5f);
        r.world.tickDisplay(0.25f);   // 페이드 반: Wave 0.5 와 Bounce 0.25 를 반씩
        const SkinSourceData& s = *arm[0].skin;
        hanim::Pose a, b, want;
        hskin::restPose(s, a); hanim::sample(s.clips[0], 0.5f, a);
        hskin::restPose(s, b); hanim::sample(s.clips[1], 0.25f, b);
        hanim::blend(a, b, 0.5f, want);
        bool same = true;
        for (size_t j = 0; j < 4; ++j) same = same && near3(r.mesh->pose()[j].t, want[j].t) && nearQ(r.mesh->pose()[j].r, want[j].r) && near3(r.mesh->pose()[j].s, want[j].s);
        check(same, "크로스페이드 중간 = 두 클립 자세의 반반");
        r.world.tickDisplay(0.3f);
        hskin::restPose(s, b); hanim::sample(s.clips[1], 0.55f, b);
        check(near3(r.mesh->pose()[0].t, b[0].t) && near3(r.mesh->pose()[3].s, b[3].s), "페이드가 끝나면 새 클립만");
        check(!r.anim->play("Nope"), "없는 클립 이름은 false");
    }
    {
        Rig r(arm);
        r.world.tickDisplay(0.25f);
        const hanim::Pose before = r.mesh->pose();
        r.anim->playAdditive("Bounce", 0.f);
        r.anim->setTime(0.25f);
        r.anim->applyPose();
        bool same = true;
        for (size_t j = 0; j < 4; ++j) same = same && near3(r.mesh->pose()[j].t, before[j].t) && near3(r.mesh->pose()[j].s, before[j].s);
        check(same, "가산 층 무게 0 이면 바닥 자세 그대로");
        r.anim->setAdditiveWeight(1.f);
        r.world.tickDisplay(0.5f);   // 가산 Bounce 0.5 초: Hand 배율 1.5 배
        check(nearF(r.mesh->pose()[3].s.x, 1.5f, 1e-4f), "가산 무게 1: Hand 배율이 1.5 배");
    }
    {
        Rig r(arm);
        std::vector<std::string> got;
        r.anim->onNotify.add([&](const std::string& n) { got.push_back(n); });
        r.anim->addNotify("Wave", 0.f, "start");
        r.anim->addNotify("Wave", 0.5f, "mid");
        r.anim->addNotify("Bounce", 0.5f, "other");
        r.world.tickDisplay(0.4f);
        check(got.size() == 1 && got[0] == "start", "0 초 이벤트는 재생 첫 진행에 한 번");
        r.world.tickDisplay(0.2f);
        check(got.size() == 2 && got[1] == "mid", "0.5 초를 지나면 mid");
        r.world.tickDisplay(0.5f);   // 0.6 → 1.1: 감으면서 start
        check(got.size() == 3 && got[2] == "start", "반복 경계를 넘으면 start 가 다시");
        got.clear();
        r.world.tickDisplay(3.0f);   // 0.1 → 3.1: mid 3번, start 3번, 지난 순서
        int mids = 0, starts = 0;
        for (const auto& n : got) { mids += n == "mid"; starts += n == "start"; }
        check(mids == 3 && starts == 3 && got.size() == 6 && got[0] == "mid" && got[1] == "start", "큰 dt 도 빠짐없이 지난 순서로(3 바퀴)");
        check(std::find(got.begin(), got.end(), "other") == got.end(), "다른 클립의 이벤트는 알리지 않음");
    }
    {
        Rig r(arm);
        r.anim->clip = "Walk";
        r.anim->rootMotion = true;
        r.actor->setLocalRotation(axisDeg({ 0, 1, 0 }, 90.f));   // 액터가 +X 를 보게: 로컬 +Z 이동 → 월드 +X
        for (int i = 0; i < 25; ++i) r.world.tickDisplay(0.1f);       // 2.5 초 = 2.5 바퀴
        const glm::vec3 pos = r.actor->transform().position();
        check(near3(pos, { 2.5f, 0, 0 }, 1e-3f), "루트 모션: 2.5 바퀴 = 액터가 2.5 m(액터 회전 반영)");
        check(near3(r.mesh->pose()[0].t, { 0, 0, 0 }), "뿌리 관절은 제자리");
    }
    {
        Rig r(arm);
        r.actor->addComponent<FakeHandPose>();
        const hanim::Pose before = r.mesh->pose();
        r.world.tickDisplay(0.25f);
        check(nearQ(r.mesh->pose()[1].r, before[1].r), "같은 액터에 손 자세가 있으면 자세를 쓰지 않음");
    }
    {
        AnimationComponent c;
        c.clip = "Walk"; c.autoPlay = false; c.loop = false; c.speed = 0.5f; c.rootMotion = true;
        ComponentFieldWriter w;
        c.writeFields(w);
        AnimationComponent d;
        for (const auto& [k, v] : w.fields()) d.readField(k, v);
        check(d.clip == "Walk" && !d.autoPlay && !d.loop && nearF(d.speed, 0.5f) && d.rootMotion, "저장 왕복(clip · autoPlay · loop · speed · rootMotion)");
    }
    {
        Rig r(arm);
        r.anim->clip = "Wave";
        r.anim->previewTime = 0.25f;
        r.anim->applyPreview();
        check(nearQ(r.mesh->pose()[1].r, axisDeg({ 0, 0, 1 }, 22.5f)), "에디터 미리보기: 그 시간의 자세(틱 없이)");
        const auto props = r.anim->buildProps();
        check(props.size() == 6 && props[0].options.size() == 4, "디테일: 클립 목록 넷 + 칸 다섯");
    }
}

// ── 5. 상태 기계 ──────────────────────────────────────────────────────────────
void testStateMachine(const std::vector<MeshSourceData>& arm) {
    std::printf("== 5. 상태 기계 ==\n");
    using SM = hanim::StateMachine;
    SM sm;
    const int idle = sm.addState("Idle", "Wave");
    const int walk = sm.addState("Walk", "Walk");
    const int jump = sm.addState("Jump", "Bounce", false);
    sm.addTransition(idle, walk, 0.2f, { { SM::Condition::FloatGreater, "speed", 0.1f } });
    sm.addTransition(walk, idle, 0.2f, { { SM::Condition::FloatLess, "speed", 0.1f } });
    sm.addTransition(-1, jump, 0.1f, { { SM::Condition::Trigger, "jump", 0.f } });
    sm.addTransition(jump, idle, 0.3f, { { SM::Condition::ClipEnd, "", 0.f } });
    auto st = sm.update(false);
    check(st.to == idle && st.blend == 0.f, "첫 update 는 시작 상태(블렌드 0)");
    check(sm.update(false).to == -1, "조건이 없으면 그대로");
    sm.setFloat("speed", 1.f);
    st = sm.update(false);
    check(st.to == walk && nearF(st.blend, 0.2f), "float 조건으로 Walk");
    sm.setTrigger("jump");
    check(sm.update(false).to == jump && !sm.triggerSet("jump"), "아무 상태 → Jump(트리거), 쓴 트리거는 지움");
    check(sm.update(false).to == -1, "클립이 끝나기 전에는 Jump 유지");
    check(sm.update(true).to == idle, "클립 끝 → Idle");
    sm.setFloat("speed", 0.f);
    check(sm.update(false).to == -1, "speed 0: Idle 유지");
    SM two;
    const int s0 = two.addState("A", "Wave"), s1 = two.addState("B", "Walk"), s2 = two.addState("C", "Bounce");
    two.addTransition(s0, s1, 0.f, { { SM::Condition::BoolTrue, "go", 0.f } });
    two.addTransition(s0, s2, 0.f, { { SM::Condition::BoolTrue, "go", 0.f } });
    two.update(false);
    two.setBool("go", true);
    check(two.update(false).to == s1, "같은 프레임에 둘이 맞으면 먼저 등록한 전환");

    if (arm.empty() || !arm[0].skin) return;
    Rig r(arm);
    auto machine = std::make_shared<SM>();
    machine->addState("Idle", "Wave");
    machine->addState("Walk", "Walk");
    machine->addTransition(0, 1, 0.f, { { SM::Condition::BoolTrue, "moving", 0.f } });
    r.anim->setStateMachine(machine);
    r.world.tickDisplay(0.1f);
    check(r.anim->currentClip() == "Wave", "컴포넌트에 붙이면 시작 상태 클립");
    machine->setBool("moving", true);
    r.world.tickDisplay(0.1f);
    check(r.anim->currentClip() == "Walk", "파라미터를 바꾸면 다음 틱에 전환");
}

// ── 6. 모프 타깃 ──────────────────────────────────────────────────────────────
void testMorph(const std::vector<MeshSourceData>& arm) {
    std::printf("== 6. 모프 타깃 ==\n");
    if (arm.empty() || !arm[0].skin) { check(false, "모델이 없어 건너뜀"); return; }
    const SkinSourceData& s = *arm[0].skin;
    check(s.morphCount == 1 && s.defaultWeights.size() == 1 && s.defaultWeights[0] == 0.f &&
          s.morphPos.size() == arm[0].vertices.size() * 3 && s.morphNrm.size() == s.morphPos.size(), "스킨 메시의 타깃 하나 · 기본 무게 0");
    check(s.clips.size() == 4 && s.clips[3].tracks.size() == 1 && s.clips[3].tracks[0].path == hanim::Path::Weights &&
          s.clips[3].tracks[0].comps == 1, "Flex: 무게 트랙(성분 = 타깃 수)");
    {
        Rig r(arm);
        std::vector<MeshSourceVertex> out(arm[0].vertices.size());
        glm::vec3 mn, mx;
        r.mesh->skinInto(out.data(), mn, mx);
        check(near3(glm::make_vec3(out[8].pos), glm::make_vec3(arm[0].vertices[8].pos)), "무게 0: 정점 = 바인드");
        r.anim->clip = "Flex";
        r.anim->loop = false;
        r.world.tickDisplay(2.f);   // 끝 = 무게 1
        check(r.mesh->morphWeights().size() == 1 && nearF(r.mesh->morphWeights()[0], 1.f), "클립의 무게 트랙이 메시 무게를 정함");
        r.mesh->skinInto(out.data(), mn, mx);
        const glm::vec3 b8 = glm::make_vec3(arm[0].vertices[8].pos);
        check(near3(glm::make_vec3(out[8].pos), b8 + glm::vec3(b8.x, 0, b8.z)) && near3(glm::make_vec3(out[0].pos), glm::make_vec3(arm[0].vertices[0].pos)),
              "무게 1: 팔꿈치 고리만 바깥으로 차이만큼(바인드 + 차이)");
        check(nearF(mx.x, 0.1f), "부푼 정점이 경계에 들어감");
    }
    std::vector<MeshSourceData> quad;
    try { quad = GltfLoader::loadAll("assets/models/anim/TestMorph.glb"); } catch (const std::exception& e) { check(false, e.what()); return; }
    check(quad.size() == 1 && quad[0].skin && quad[0].skin->joints.size() == 1 && quad[0].skin->morphCount == 1,
          "스킨 없는 모프 메시: 관절 하나짜리 스킨으로 읽음");
    if (quad.empty() || !quad[0].skin) return;
    const SkinSourceData& q = *quad[0].skin;
    check(near3(glm::vec3(q.joints[0].rootParent[3]), { 1, 0, 0 }) && near3(glm::make_vec3(quad[0].vertices[2].pos), { 1, 1, 0 }),
          "노드 이동은 rootParent 에 두고 정점은 메시 로컬 그대로");
    check(nearF(q.defaultWeights[0], 0.25f) && q.clips.size() == 1 && q.clips[0].name == "Lift", "기본 무게 0.25 · 클립 Lift");
    Rig r(quad);
    r.anim->loop = false;   // 0.5 초 클립을 0.6 초 돌리므로 반복이면 0.1 초로 감김
    std::vector<MeshSourceVertex> out(4);
    glm::vec3 mn, mx;
    r.mesh->skinInto(out.data(), mn, mx);
    check(near3(glm::make_vec3(out[2].pos), { 2, 1.125f, 0 }), "기본 무게 0.25: 위 정점 +0.125, 노드 이동 +1");
    r.world.tickDisplay(0.6f);
    r.mesh->skinInto(out.data(), mn, mx);
    check(near3(glm::make_vec3(out[2].pos), { 2, 1.5f, 0 }), "STEP 무게 1(0.5 초 뒤): +0.5");
}

// ── 7. 트윈 ──────────────────────────────────────────────────────────────────
void testTween() {
    std::printf("== 7. 트윈 ==\n");
    World world;
    Actor* a = world.spawnActor("Box");
    TweenSystem& tw = world.tweens();
    int done = 0;
    TweenSystem::Options lin;
    lin.ease = EEase::Linear;
    lin.onDone = [&] { ++done; };
    const auto h = tw.moveTo(a, { 2, 0, 0 }, 1.f, lin);
    world.tick(0.25f);
    check(near3(a->transform().position(), { 0.5f, 0, 0 }) && tw.active(h), "위치 Linear 1/4");
    world.tick(1.f);
    check(near3(a->transform().position(), { 2, 0, 0 }) && !tw.active(h) && done == 1, "끝: 목표 값 · 끝 콜백 한 번 · 사라짐");

    TweenSystem::Options eo;
    eo.ease = EEase::InOutQuad;
    tw.scaleTo(a, { 3, 3, 3 }, 1.f, eo);
    world.tick(0.25f);
    check(near3(a->transform().scale(), glm::vec3(1.f + 2.f * hmath::ease::inOutQuad(0.25f))), "배율 InOutQuad 이징");
    world.tick(1.f);

    TweenSystem::Options dl;
    dl.ease = EEase::Linear;
    dl.delay = 0.5f;
    tw.rotateTo(a, axisDeg({ 0, 1, 0 }, 90), 1.f, dl);
    world.tick(0.4f);
    check(nearQ(a->transform().rotation(), glm::quat(1, 0, 0, 0)), "지연 동안 그대로");
    world.tick(0.6f);   // 기다림 0.1 넘음 + 0.5 진행
    check(nearQ(a->transform().rotation(), axisDeg({ 0, 1, 0 }, 45)), "지연을 넘은 시간만큼 진행(회전 slerp 45°)");
    world.tick(1.f);

    float value = 10.f;
    TweenSystem::Options yo;
    yo.ease = EEase::Linear;
    yo.repeat = 1;
    yo.yoyo = true;
    const auto hf = tw.floatTo([&] { return value; }, [&](float v) { value = v; }, 20.f, 1.f, yo);
    world.tick(1.5f);
    check(nearF(value, 15.f) && tw.active(hf), "왕복: 1.5 초 = 돌아오는 길 가운데");
    world.tick(0.6f);
    check(nearF(value, 10.f) && !tw.active(hf), "되풀이 한 번 뒤 끝(시작 값으로 돌아옴)");

    TweenSystem::Options inf;
    inf.ease = EEase::Linear;
    inf.repeat = -1;
    const auto hi = tw.floatTo([&] { return value; }, [&](float v) { value = v; }, 11.f, 0.1f, inf);
    for (int i = 0; i < 50; ++i) world.tick(0.033f);
    check(tw.active(hi), "끝없이 되풀이");
    tw.cancel(hi);
    world.tick(0.01f);
    check(!tw.active(hi) && tw.count() == 0, "취소");

    const auto h1 = tw.moveTo(a, { 0, 5, 0 }, 1.f);
    const auto h2 = tw.moveTo(a, { 0, 0, 5 }, 1.f);
    check(!tw.active(h1) && tw.active(h2), "같은 액터의 같은 속성은 새 트윈이 대신함");
    int doneB = 0;
    TweenSystem::Options ob;
    ob.onDone = [&] { ++doneB; };
    Actor* b = world.spawnActor("Other");
    tw.moveTo(b, { 1, 1, 1 }, 1.f, ob);
    world.destroyActor(b);
    world.tick(2.f);
    check(doneB == 0 && tw.count() == 0, "액터를 지우면 그 트윈도 사라짐(끝 콜백 없이)");
}

} // namespace

int main() {
    std::printf("HarukaAnimTest\n");
    const fs::path root = HARUKA_SOURCE_DIR;
    hvfs::reset();
    hvfs::mountDirectoryAs(root / "Projects/Samples/Content", "/Game/", 0);
    const fs::path tmp = fs::temp_directory_path() / "haruka_anim_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    testSampling();
    testPoseMath();
    const auto arm = testImport();
    testCache(arm, tmp);
    testPlayer(arm);
    testStateMachine(arm);
    testMorph(arm);
    testTween();
    fs::remove_all(tmp, ec);
    std::printf("\n결과: %s (검사 %d, 실패 %d)\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
