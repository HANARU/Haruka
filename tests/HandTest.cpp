// 손 쥐기 A 단계(스키닝 · 손 뼈대 · 시험 장갑)를 화면 없이 검산합니다(2026-11-07, Docs/Hand_Grasp_Design.md 4-1 · 6).
//
// 이 시험이 존재하는 이유: 스키닝은 행렬 곱의 순서 · 쉬는 자세와 바인드 자세 · 관절 축 규약이 하나만 어긋나도 손이
// 뒤틀리거나 손가락이 손등 쪽으로 굽습니다. 화면으로는 무엇이 틀렸는지 알기 어려우므로 수치로 확인합니다.
//   1. 손 뼈대 표(이름 · 부모 · 굽힘 관절 사슬)가 OpenXR 26관절과 맞는지
//   2. CPU 스키닝 수식(관절 하나를 돌린 위치 · 노멀, 무게 섞기, 평가 순서 · 순환 거부)
//   3. 시험 장갑 glb(assets/models/hands)를 로더가 스킨과 함께 읽는지, 쉬는 자세의 스키닝 행렬이 단위 행렬인지
//   4. 캐시(버전 7)가 스킨을 그대로 왕복하는지
//   5. HandPoseComponent 가 굽힘 값으로 손가락을 손바닥 쪽으로 굽히는지, 축 규약 위반을 알아채는지
//   6. 컨트롤러 입력(트리거 · 그립 · 정전식 터치) → 손가락 굽힘 목표와 부드럽게 따라가기(B 단계, 4-2)
//   7. HandPoseComponent 가 자기 손의 입력만 읽는지, 입력이 없으면 값을 지키는지, 저장 왕복
// 실행: build/Release/HarukaHandTest.exe

#include "../src/assets/AssetManager.hpp"
#include "../src/assets/GassetCache.hpp"
#include "../src/assets/GltfLoader.hpp"
#include "../src/assets/MeshSource.hpp"
#include "../src/assets/Skin.hpp"
#include "../src/components/HandPoseComponent.hpp"
#include "../src/components/SkinnedMeshComponent.hpp"
#include "../src/core/Actor.hpp"
#include "../src/core/ComponentFields.hpp"
#include "../src/core/World.hpp"
#include "../src/input/HInputSystem.hpp"
#include "../src/vfs/Vfs.hpp"
#include "../src/xr/HandInputMap.hpp"
#include "../src/xr/HandSkeleton.hpp"
#include "../src/xr/GraspSolver.hpp"
#include "../src/components/XrComponents.hpp"
#include "../src/components/RigidBodyComponent.hpp"
#include "../src/physics/PhysicsSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
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
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) <= eps; }

const char* kGloveR = "assets/models/hands/TestGlove_R.glb";
const char* kGloveL = "assets/models/hands/TestGlove_L.glb";

// ── 1. 손 뼈대 표 ──────────────────────────────────────────────────────────────
void testSkeletonTables() {
    std::printf("== 1. 손 뼈대 표 ==\n");
    std::set<std::string> names;
    for (int i = 0; i < hhand::kJointCount; ++i) names.insert(hhand::kHandJointNames[i]);
    check((int)names.size() == hhand::kJointCount, "관절 이름 26개가 서로 다름");
    bool roundTrip = true;
    for (int i = 0; i < hhand::kJointCount; ++i) roundTrip &= hhand::jointIndex(hhand::kHandJointNames[i]) == i;
    check(roundTrip, "이름 → 번호가 OpenXR 번호와 같음");
    check(hhand::jointIndex("Palm") == 0 && hhand::jointIndex("Wrist") == 1 && hhand::jointIndex("LittleTip") == 25,
          "손바닥 0 · 손목 1 · 소지 끝 25 (XrHandJointEXT 와 같음)");
    check(hhand::jointIndex("nope") == -1, "없는 이름은 -1");
    int roots = 0;
    bool parentsOk = true;
    for (int i = 0; i < hhand::kJointCount; ++i) {
        const int p = hhand::kHandJointParent[i];
        if (p < 0) ++roots;
        else parentsOk &= p < hhand::kJointCount && p != i;
    }
    check(roots == 1 && hhand::kHandJointParent[(int)hhand::EJoint::Wrist] == -1, "뿌리는 손목 하나");
    check(parentsOk, "부모 번호가 범위 안");
    bool chains = true;
    for (int f = 0; f < hhand::kFingerCount; ++f) {
        const auto* fl = hhand::kFingerFlexJoints[f];
        chains &= hhand::kHandJointParent[(int)fl[1]] == (int)fl[0];
        chains &= hhand::kHandJointParent[(int)fl[2]] == (int)fl[1];
        chains &= hhand::kHandJointParent[(int)hhand::kFingerTip[f]] == (int)fl[2];
    }
    check(chains, "손가락마다 굽힘 관절 세 개와 끝이 부모 사슬로 이어짐");
}

// ── 2. CPU 스키닝 수식 ─────────────────────────────────────────────────────────
// 관절 두 개: 뿌리(원점) → 자식(로컬 (0,0,-1)). 정점 0 = (0,0,-2) 자식 100%, 정점 1 = (0,0,-1.5) 반반, 정점 2 = 원점 뿌리 100%.
SkinSourceData twoJointSkin(bool childFirst) {
    SkinSourceData s;
    SkinJoint root, child;
    root.name = "root";
    child.name = "child";
    child.restT = glm::vec3(0.f, 0.f, -1.f);
    root.inverseBind  = glm::mat4(1.f);
    child.inverseBind = glm::inverse(glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, -1.f)));
    if (childFirst) { child.parent = 1; s.joints = { child, root }; }
    else            { child.parent = 0; s.joints = { root, child }; }
    const uint16_t ci = childFirst ? 0 : 1, ri = childFirst ? 1 : 0;
    s.jointIndex  = { ci, 0, 0, 0,   ci, ri, 0, 0,   ri, 0, 0, 0 };
    s.jointWeight = { 1.f, 0, 0, 0,  0.5f, 0.5f, 0, 0, 1.f, 0, 0, 0 };
    return s;
}

std::vector<MeshSourceVertex> twoJointVerts() {
    std::vector<MeshSourceVertex> v(3);
    const float z[3] = { -2.f, -1.5f, 0.f };
    for (int i = 0; i < 3; ++i) {
        v[i] = MeshSourceVertex{ { 0.f, 0.f, z[i] }, { 0.f, 1.f, 0.f }, { 0.f, 0.f }, { 1.f, 0.f, 0.f, 1.f } };
    }
    return v;
}

void testSkinMath() {
    std::printf("== 2. CPU 스키닝 수식 ==\n");
    for (int variant = 0; variant < 2; ++variant) {
        const bool childFirst = variant == 1;
        SkinSourceData s = twoJointSkin(childFirst);
        check(hskin::buildOrder(s), childFirst ? "평가 순서: 자식이 먼저 나와도 만듦" : "평가 순서 만듦");
        const int root = childFirst ? 1 : 0, child = childFirst ? 0 : 1;
        check(s.order.size() == 2 && s.order[0] == root, "평가 순서에서 부모가 먼저");
        check(hskin::findJoint(s, "child") == child && hskin::findJoint(s, "x") == -1, "이름으로 관절 찾기");

        const auto bind = twoJointVerts();
        std::vector<glm::mat4> g, pal;
        hskin::computeGlobals(s, nullptr, g);
        hskin::computePalette(s, g, pal);
        std::vector<MeshSourceVertex> out(3);
        glm::vec3 bmin, bmax;
        hskin::skinVertices(bind.data(), bind.size(), s, pal.data(), out.data(), bmin, bmax);
        check(near3(glm::make_vec3(out[0].pos), { 0, 0, -2 }) && near3(glm::make_vec3(out[1].pos), { 0, 0, -1.5f }),
              "쉬는 자세 = 바인드 자세면 정점이 그대로");

        // 자식을 로컬 X 둘레로 -90°: (0,0,-1) 방향 → (0,-1,0). 정점 0 은 (0,-1,-1), 노멀 (0,1,0) → (0,0,-1).
        std::vector<glm::quat> pose = { s.joints[0].restR, s.joints[1].restR };
        pose[child] = glm::angleAxis(glm::radians(-90.f), glm::vec3(1, 0, 0));
        hskin::computeGlobals(s, pose.data(), g);
        hskin::computePalette(s, g, pal);
        hskin::skinVertices(bind.data(), bind.size(), s, pal.data(), out.data(), bmin, bmax);
        check(near3(glm::make_vec3(out[0].pos), { 0, -1, -1 }), "자식 -90°: 자식 100% 정점이 관절 둘레로 돎");
        check(near3(glm::make_vec3(out[0].normal), { 0, 0, -1 }), "노멀도 같은 회전");
        check(near3(glm::make_vec3(out[0].tangent), { 1, 0, 0 }) && out[0].tangent[3] == 1.f, "X 축 탄젠트 · 부호 유지");
        check(near3(glm::make_vec3(out[1].pos), 0.5f * (glm::vec3(0, -0.5f, -1) + glm::vec3(0, 0, -1.5f))),
              "반반 정점은 두 변환의 평균");
        check(near3(glm::make_vec3(out[2].pos), { 0, 0, 0 }), "뿌리 100% 정점은 그대로");
        check(near3(bmin, { 0, -1, -1.25f }) && near3(bmax, { 0, 0, 0 }), "AABB 가 변형한 정점을 감쌈");
    }
    SkinSourceData cyc;
    cyc.joints.resize(2);
    cyc.joints[0].parent = 1;
    cyc.joints[1].parent = 0;
    check(!hskin::buildOrder(cyc), "순환하는 부모는 거부");
    SkinSourceData bad;
    bad.joints.resize(1);
    bad.joints[0].parent = 5;
    check(!hskin::buildOrder(bad), "범위 밖 부모는 거부");
}

// ── 3. 시험 장갑 읽기 ───────────────────────────────────────────────────────────
void testGloveLoad(const char* path, std::vector<MeshSourceData>& keep) {
    std::printf("== 3. 장갑 읽기: %s ==\n", path);
    std::vector<MeshSourceData> meshes;
    try { meshes = GltfLoader::loadAll(path); } catch (const std::exception& e) {
        check(false, std::string("읽기 실패: ") + e.what());
        return;
    }
    check(meshes.size() == 1, "프리미티브 하나");
    if (meshes.empty()) return;
    const MeshSourceData& m = meshes[0];
    check(m.skin != nullptr, "스킨이 있음");
    if (!m.skin) return;
    const SkinSourceData& s = *m.skin;
    check((int)s.joints.size() == hhand::kJointCount, "관절 26개");
    bool allNames = true;
    for (int i = 0; i < hhand::kJointCount; ++i) allNames &= hskin::findJoint(s, hhand::kHandJointNames[i]) >= 0;
    check(allNames, "OpenXR 관절 이름이 모두 있음");
    bool parentsMatch = true;
    for (int i = 0; i < hhand::kJointCount; ++i) {
        const int j  = hskin::findJoint(s, hhand::kHandJointNames[i]);
        const int ep = hhand::kHandJointParent[i];
        const int p  = s.joints[j].parent;
        parentsMatch &= ep < 0 ? p < 0 : (p >= 0 && s.joints[p].name == hhand::kHandJointNames[ep]);
    }
    check(parentsMatch, "모델의 부모 관계가 엔진 손 뼈대와 같음");
    check(s.jointIndex.size() == m.vertices.size() * 4 && s.jointWeight.size() == m.vertices.size() * 4,
          "정점마다 관절 4개 · 무게 4개");
    bool weightsOk = true, indexOk = true;
    for (size_t v = 0; v < m.vertices.size(); ++v) {
        float sum = 0.f;
        for (int k = 0; k < 4; ++k) { sum += s.jointWeight[v * 4 + k]; indexOk &= s.jointIndex[v * 4 + k] < s.joints.size(); }
        weightsOk &= near(sum, 1.f, 1e-4f);
    }
    check(weightsOk, "무게 합이 1");
    check(indexOk, "관절 번호가 범위 안");
    std::vector<glm::mat4> g, pal;
    hskin::computeGlobals(s, nullptr, g);
    hskin::computePalette(s, g, pal);
    float err = 0.f;
    for (const auto& pm : pal)
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) err = std::max(err, std::fabs(pm[c][r] - (c == r ? 1.f : 0.f)));
    check(err < 1e-4f, "쉬는 자세의 스키닝 행렬이 단위 행렬(오차 " + std::to_string(err) + ")");
    keep = std::move(meshes);
}

// ── 4. 캐시 왕복 ────────────────────────────────────────────────────────────────
void testCache(const std::vector<MeshSourceData>& meshes, const fs::path& tmp) {
    std::printf("== 4. 캐시 왕복(버전 7) ==\n");
    if (meshes.empty() || !meshes[0].skin) { check(false, "장갑이 없어 건너뜀"); return; }
    GassetCache::setDirectory((tmp / "cache").string());
    GassetCache::save(kGloveR, meshes);
    check(GassetCache::isValid(kGloveR), "저장한 캐시가 유효");
    std::vector<MeshSourceData> back;
    try { back = GassetCache::load(kGloveR); } catch (const std::exception& e) { check(false, e.what()); return; }
    check(back.size() == 1 && back[0].skin != nullptr, "스킨이 함께 돌아옴");
    if (back.empty() || !back[0].skin) return;
    const SkinSourceData& a = *meshes[0].skin;
    const SkinSourceData& b = *back[0].skin;
    bool same = a.joints.size() == b.joints.size() && a.jointIndex == b.jointIndex && a.jointWeight == b.jointWeight;
    for (size_t j = 0; same && j < a.joints.size(); ++j) {
        same &= a.joints[j].name == b.joints[j].name && a.joints[j].parent == b.joints[j].parent;
        same &= a.joints[j].restT == b.joints[j].restT && a.joints[j].restS == b.joints[j].restS;
        same &= a.joints[j].restR == b.joints[j].restR && a.joints[j].inverseBind == b.joints[j].inverseBind;
        same &= a.joints[j].rootParent == b.joints[j].rootParent;
    }
    check(same, "관절 · 무게 · 행렬이 바이트 그대로");
    check(b.order.size() == b.joints.size(), "불러온 스킨의 평가 순서를 다시 만듦");
}

// ── 5. 손 자세 ──────────────────────────────────────────────────────────────────
glm::vec3 jointPos(const SkinnedMeshComponent& sk, const char* name) {
    const int j = sk.findJoint(name);
    return j >= 0 ? glm::vec3(sk.jointGlobals()[j][3]) : glm::vec3(NAN);
}

void testHandPose(const std::vector<MeshSourceData>& glove, const char* label) {
    std::printf("== 5. 손 자세: %s ==\n", label);
    if (glove.empty() || !glove[0].skin) { check(false, "장갑이 없어 건너뜀"); return; }
    World world;
    Actor* a = world.spawnActor("Glove");
    auto* sk = a->addComponent<SkinnedMeshComponent>();
    auto* hp = a->addComponent<HandPoseComponent>();
    auto bind = std::make_shared<const std::vector<MeshSourceVertex>>(glove[0].vertices);
    sk->setSkinData(glove[0].skin, bind);
    check(sk->hasSkin() && !sk->loaded() && sk->jointCount() == 26,
          "스킨 데이터를 넣으면 자세 계산이 되고, GPU 메시가 없으니 그리지는 않음");

    const glm::vec3 tip0  = jointPos(*sk, "IndexTip");
    const glm::vec3 mtip0 = jointPos(*sk, "MiddleTip");
    std::vector<MeshSourceVertex> out(bind->size());
    glm::vec3 bmin, bmax;
    check(hp->apply(), "굽힘 0 적용");
    check(hp->mappedJoints() == 26 && hp->axisViolations().empty(), "관절 26개 대응 · 축 규약 위반 없음");
    sk->skinInto(out.data(), bmin, bmax);
    float restErr = 0.f;
    for (size_t i = 0; i < out.size(); ++i)
        restErr = std::max(restErr, glm::length(glm::make_vec3(out[i].pos) - glm::make_vec3((*bind)[i].pos)));
    check(restErr < 1e-5f, "굽힘 0 이면 바인드 정점 그대로");

    hp->curl[(int)hhand::EFinger::Index] = 1.f;
    hp->apply();
    const glm::vec3 tip1  = jointPos(*sk, "IndexTip");
    const glm::vec3 mtip1 = jointPos(*sk, "MiddleTip");
    // 완전히 쥐면 세 마디가 합쳐 240° 말려 끝이 다시 올라오므로(약 2.5cm 아래) 2cm 를 기준으로 봅니다.
    check(tip1.y < tip0.y - 0.02f, "검지를 쥐면 끝이 손바닥 쪽(-Y)으로 2cm 넘게 내려감");
    check(glm::length(tip1 - jointPos(*sk, "IndexProximal")) < glm::length(tip0 - jointPos(*sk, "IndexProximal")),
          "검지 끝이 뿌리 마디에 가까워짐(말림)");
    check(near3(mtip1, mtip0), "다른 손가락(중지)은 그대로");
    sk->skinInto(out.data(), bmin, bmax);
    bool inside = true;
    for (const auto& v : out) {
        const glm::vec3 p = glm::make_vec3(v.pos);
        inside &= glm::all(glm::greaterThanEqual(p, bmin - 1e-6f)) && glm::all(glm::lessThanEqual(p, bmax + 1e-6f));
    }
    check(inside, "스키닝 AABB 가 모든 정점을 감쌈");

    for (float& c : hp->curl) c = 1.f;
    hp->apply();
    check(jointPos(*sk, "ThumbTip").y < tip0.y, "엄지도 손바닥 쪽으로 굽음");
    hp->curl[0] = hp->curl[1] = hp->curl[2] = hp->curl[3] = hp->curl[4] = 0.f;
    hp->apply();
    check(near3(jointPos(*sk, "IndexTip"), tip0), "다시 펴면 쉬는 자세로");

    // 축 규약 위반: 검지 intermediate 를 로컬 +Z 쪽에 둔 복사본
    auto broken = std::make_shared<SkinSourceData>(*glove[0].skin);
    const int inter = hskin::findJoint(*broken, "IndexIntermediate");
    broken->joints[inter].restT = glm::vec3(0.f, 0.f, 0.04f);
    Actor* b = world.spawnActor("Broken");
    auto* sk2 = b->addComponent<SkinnedMeshComponent>();
    auto* hp2 = b->addComponent<HandPoseComponent>();
    sk2->setSkinData(broken, bind);
    hp2->apply();
    bool flagged = false;
    for (const auto& n : hp2->axisViolations()) flagged |= (n == "IndexProximal");
    check(flagged, "자식 관절이 로컬 -Z 쪽에 없으면 그 관절을 위반으로 알림");
}

// ── 6. 컨트롤러 입력 → 손가락(B 단계) ──────────────────────────────────────────
void testInputMapping() {
    std::printf("== 6. 컨트롤러 입력 → 손가락 굽힘 ==\n");
    using hhand::EFinger;
    float t[hhand::kFingerCount];
    HHandInput h;
    h.active = true;
    h.hasTouch = true;
    hhand::curlTargets(h, t);
    check(t[(int)EFinger::Index] == 0.f && t[(int)EFinger::Thumb] == 0.f && t[(int)EFinger::Middle] == 0.f,
          "터치 컨트롤러 · 아무것도 닿지 않음 → 모두 0(검지 가리키기 · 엄지 들기)");
    h.triggerTouch = true;
    hhand::curlTargets(h, t);
    check(near(t[(int)EFinger::Index], hhand::kIndexTouchCurl), "트리거에 닿기만 함 → 검지 0.3");
    h.trigger = 0.5f;
    hhand::curlTargets(h, t);
    check(near(t[(int)EFinger::Index], 0.65f), "트리거 절반 → 검지 0.3 + 0.7 x 0.5");
    h.triggerTouch = false;
    hhand::curlTargets(h, t);
    check(near(t[(int)EFinger::Index], 0.65f), "당기는 중에 터치 신호가 빠져도 닿은 것으로 봄");
    h.trigger = 1.f;
    h.squeeze = 0.7f;
    h.thumbTouch = true;
    hhand::curlTargets(h, t);
    check(near(t[(int)EFinger::Index], 1.f) && near(t[(int)EFinger::Middle], 0.7f) && near(t[(int)EFinger::Ring], 0.7f) &&
          near(t[(int)EFinger::Little], 0.7f) && near(t[(int)EFinger::Thumb], hhand::kThumbDownCurl),
          "트리거 끝 · 그립 0.7 · 엄지 터치 → 1 · 0.7 x3 · 0.6");
    h.squeeze = 2.f;
    h.trigger = -1.f;
    hhand::curlTargets(h, t);
    check(t[(int)EFinger::Middle] == 1.f && t[(int)EFinger::Index] >= 0.f, "범위 밖 입력은 0 ~ 1 로 자름");
    HHandInput simple;
    simple.active = true;
    simple.trigger = 0.4f;
    simple.thumbTouch = true;   // 단순 컨트롤러에는 의미 없음
    hhand::curlTargets(simple, t);
    check(near(t[(int)EFinger::Index], 0.4f) && t[(int)EFinger::Thumb] == 0.f, "터치가 없는 컨트롤러 → 검지 = 트리거, 엄지 들기");

    check(hhand::smoothCurl(0.2f, 1.f, 0.f) == 0.2f, "dt 0 이면 그대로");
    check(hhand::smoothCurl(0.f, 1.f, 0.016f, 0.f) == 1.f, "시간 상수 0 이면 바로 목표");
    const float after50ms = hhand::smoothCurl(0.f, 1.f, 0.05f);
    check(after50ms > 0.75f && after50ms < 0.85f, "50 ms 에 차이의 약 80% 를 따라감(" + std::to_string(after50ms) + ")");
    float c = 0.f, prev = 0.f;
    bool monotonic = true;
    for (int i = 0; i < 120; ++i) { c = hhand::smoothCurl(c, 1.f, 1.f / 120.f); monotonic &= c >= prev && c <= 1.f; prev = c; }
    check(monotonic && c > 0.99f, "목표를 넘지 않고 1 초 안에 다다름");
}

void testHandPoseInput() {
    std::printf("== 7. 손 자세가 입력을 읽음 ==\n");
    World world;
    auto* left  = world.spawnActor("L")->addComponent<HandPoseComponent>();
    auto* right = world.spawnActor("R")->addComponent<HandPoseComponent>();
    auto* none  = world.spawnActor("N")->addComponent<HandPoseComponent>();
    left->hand  = EHandSide::Left;
    right->hand = EHandSide::Right;
    none->curl[2] = 0.25f;

    HXrInputState xr;
    xr.focused = true;
    xr.hands[1].active   = true;
    xr.hands[1].hasTouch = true;
    xr.hands[1].squeeze  = 1.f;
    xr.hands[1].thumbTouch = true;
    HInputSystem::get().setXrInput(xr);
    bool updated = false;
    for (int i = 0; i < 60; ++i) updated = right->updateFromInput(1.f / 60.f);
    check(updated && right->curl[(int)hhand::EFinger::Middle] > 0.99f && near(right->curl[0], hhand::kThumbDownCurl, 1e-3f),
          "오른손: 그립을 1 초 쥐면 중지 1 · 엄지 0.6 에 다다름");
    check(!left->updateFromInput(1.f / 60.f) && left->curl[2] == 0.f, "왼손 컨트롤러가 꺼져 있으면 값 그대로");
    check(!none->updateFromInput(1.f / 60.f) && none->curl[2] == 0.25f, "hand = None 이면 입력을 읽지 않음");
    xr.focused = false;
    HInputSystem::get().setXrInput(xr);
    right->curl[2] = 0.5f;
    check(!right->updateFromInput(1.f / 60.f) && right->curl[2] == 0.5f, "XR 세션이 입력을 받지 않으면 값 그대로");
    HInputSystem::get().setXrInput(HXrInputState{});

    ComponentFieldWriter w;
    right->curl[1] = 0.75f;
    right->maxFlexDeg[1][1] = 90.f;
    right->writeFields(w);
    auto* back = world.spawnActor("B")->addComponent<HandPoseComponent>();
    for (const auto& [k, v] : w.fields()) back->readField(k, v);
    check(back->hand == EHandSide::Right && near(back->curl[1], 0.75f) && near(back->maxFlexDeg[1][1], 90.f),
          "저장 왕복: 손 · 굽힘 값 · 최대 각");
}

// ── 6. 자동 맞춤 쥐기: 순수 계산(합성 손) ─────────────────────────────────────────
// 손가락 다섯이 x 로 나란히, 손바닥(원점)에서 -Z 로 뻗은 마디 4 cm 셋. 굽힘은 관절마다 로컬 X 둘레로 최대 90° 아래(-Y)로.
static void synthPose(const hgrasp::Flex& flex, hgrasp::JointPos& out) {
    for (auto& p : out) p = glm::vec3(0.f);
    out[(int)hhand::EJoint::Palm]  = glm::vec3(0.f);
    out[(int)hhand::EJoint::Wrist] = glm::vec3(0.f, 0.f, 0.06f);
    for (int f = 0; f < hhand::kFingerCount; ++f) {
        glm::vec3 p(-0.04f + 0.02f * (float)f, 0.f, -0.04f);
        out[(int)hhand::EJoint::MiddleProximal] = glm::vec3(0.f, 0.f, -0.04f);
        float ang = 0.f;
        for (int k = 0; k < 3; ++k) {
            int a, b;
            hgrasp::boneJoints(f, k, a, b);
            out[a] = p;
            ang += glm::radians(90.f) * flex[f][k];
            const glm::vec3 dir(0.f, -std::sin(ang), -std::cos(ang));
            p += 0.04f * dir;
            out[b] = p;
        }
    }
}

void testGraspSolver() {
    std::printf("== 6. 자동 맞춤 쥐기(순수 계산) ==\n");
    hgrasp::Settings st;
    // 손가락 아래 4 cm 의 바닥(y = -0.04). 침투 = 반지름 - (가장 낮은 점의 높이 - 바닥).
    const float floorY = -0.04f;
    auto floorOverlap = [&](const glm::vec3& a, const glm::vec3& b, float r) { return r - (std::min(a.y, b.y) - floorY); };
    const hgrasp::Result r = hgrasp::solve(synthPose, floorOverlap, st);
    bool allContact = true, partial = true, noDeep = true;
    hgrasp::JointPos jp;
    synthPose(r.flex, jp);
    for (int f = 0; f < hhand::kFingerCount; ++f) {
        allContact = allContact && r.fingerContact[f];
        partial    = partial && r.flex[f][0] < 1.f;
        for (int k = 0; k < 3; ++k) {
            int a, b;
            hgrasp::boneJoints(f, k, a, b);
            noDeep = noDeep && floorOverlap(jp[a], jp[b], st.boneRadius) <= st.tolerance + 1e-6f;
        }
    }
    std::printf("    검지 굽힘 %.3f %.3f %.3f, 판정 %d 번\n", r.flex[1][0], r.flex[1][1], r.flex[1][2], r.queries);
    check(allContact && partial, "바닥에 닿을 때까지 굽히다 멈춤(모든 손가락 닿음, 뿌리 마디 < 1)");
    check(noDeep, "맞춘 자세의 어느 마디도 허용 침투(2 mm)보다 깊이 박히지 않음");
    check(r.fitted, "엄지와 다른 손가락이 닿으면 맞춤 잡기");
    check(r.queries < 600, "판정 횟수가 잡을 때 수백 번 안쪽");

    const hgrasp::Result none = hgrasp::solve(synthPose, [](const glm::vec3&, const glm::vec3&, float) { return -1.f; }, st);
    bool fist = true;
    for (int f = 0; f < hhand::kFingerCount; ++f)
        for (int k = 0; k < 3; ++k) fist = fist && none.flex[f][k] == 1.f;
    check(fist && !none.fitted && !none.palmContact, "닿는 것이 없으면 주먹을 쥐고 맞춤 아님(예전 잡기로)");

    // 손바닥만 닿음(손바닥 캡슐 = 원점 → 중지 뿌리)
    auto palmOnly = [](const glm::vec3& a, const glm::vec3& b, float r) {
        return (a == glm::vec3(0.f) && b == glm::vec3(0.f, 0.f, -0.04f)) ? r : -1.f;
    };
    const hgrasp::Result palm = hgrasp::solve(synthPose, palmOnly, st);
    check(palm.palmContact && palm.fitted, "손바닥이 닿으면 맞춤 잡기");

    // 첫 마디부터 박힌 물체: 그 손가락은 굽히지 않음
    auto inside = [&](const glm::vec3& a, const glm::vec3& b, float r) { return r - (std::min(a.y, b.y) + 0.001f); };
    const hgrasp::Result in = hgrasp::solve(synthPose, inside, st);
    check(in.fingerContact[1] && in.flex[1][0] == 0.f && in.flex[1][2] == 0.f, "편 손에서 이미 박힌 손가락은 그대로 둠");
}

// ── 7. 자동 맞춤 쥐기: 시험 장갑 + Jolt ────────────────────────────────────────────
void testGraspGlove(const std::vector<MeshSourceData>& glove) {
    std::printf("== 7. 자동 맞춤 쥐기(시험 장갑 + 물리) ==\n");
    if (glove.empty() || !glove[0].skin) { check(false, "장갑이 없어 건너뜀"); return; }
    World w;
    PhysicsSystem ps;
    ps.startup();
    ps.setWorld(&w);
    ps.setGravity({ 0.f, 0.f, 0.f });
    Actor* handA = w.spawnActor("RightHand");
    auto*  ia    = handA->addComponent<GrabInteractorComponent>();
    Actor* gloveA = w.spawnActor("RightGlove");
    gloveA->attachTo(handA);
    auto* sk = gloveA->addComponent<SkinnedMeshComponent>();
    auto* hp = gloveA->addComponent<HandPoseComponent>();
    sk->setSkinData(glove[0].skin, std::make_shared<const std::vector<MeshSourceVertex>>(glove[0].vertices));
    glm::mat4 palmM;
    check(hp->jointWorld(hhand::EJoint::Palm, palmM), "손바닥 관절의 월드 자세");
    const glm::vec3 palm   = glm::vec3(palmM[3]);
    const glm::vec3 normal = -glm::normalize(glm::vec3(palmM[1]));
    // 지름 7 cm 공을 손바닥 앞 2 cm 띄워 둠(공 표면 ~ 손바닥 = 두께 1.5 cm + 2 cm)
    const float R = 0.035f;
    Actor* ball = w.spawnActor("Ball");
    ball->setLocalPosition(palm + normal * (GrabInteractorComponent::kPalmThickness + 0.02f + R));
    ball->setLocalScale(glm::vec3(2.f * R));
    auto* rb = ball->addComponent<RigidBodyComponent>();
    rb->shape = EPhysicsShape::Sphere;
    rb->motionType = EMotionType::Dynamic;
    auto* g = ball->addComponent<GrabbableComponent>();
    ps.tick(1.f / 90.f);
    check(rb->hasBody(), "(준비) 공 바디");
    PhysicsSystem::RaycastHit hit;
    check(ps.raycastBody(rb->bodyId_, palm, normal, 0.2f, hit) && std::fabs(hit.distance - 0.035f) < 1e-3f,
          "바디 하나 대상 광선: 손바닥에서 공 표면까지 3.5 cm");
    check(ps.capsulePenetration(rb->bodyId_, ball->worldPosition() - glm::vec3(0.1f, 0, 0), ball->worldPosition() + glm::vec3(0.1f, 0, 0), 0.01f) > R,
          "바디 하나 대상 캡슐: 공을 꿰뚫으면 침투가 반지름보다 깊음");
    check(ps.capsulePenetration(rb->bodyId_, ball->worldPosition() + glm::vec3(0, 0.2f, 0), ball->worldPosition() + glm::vec3(0, 0.3f, 0), 0.01f) < 0.f,
          "떨어진 캡슐은 음수(닿지 않음)");

    const glm::vec3 before = ball->worldPosition();
    ia->grab(g, handA->worldPosition(), handA->worldRotation());
    check(ia->lastFitted(), "공을 잡으면 맞춤 잡기");
    for (int i = 0; i < 18; ++i) {   // 0.2 초: 전환(0.1 초)이 끝남
        ia->follow(handA->worldPosition(), handA->worldRotation(), 1.f / 90.f);
        hp->tick(1.f / 90.f);
        ps.tick(1.f / 90.f);
    }
    const float moved = glm::dot(before - ball->worldPosition(), normal);
    std::printf("    공이 손바닥 쪽으로 %.4f m, 손가락 비중 %.2f\n", moved, hp->graspWeight());
    check(std::fabs(moved - 0.02f) < 0.002f, "손바닥에 맞춰 공을 2 cm 당김(표면이 손바닥 두께 자리)");
    check(std::fabs(hp->graspWeight() - 1.f) < 1e-5f, "0.1 초 전환 뒤 손가락은 맞춘 자세");
    // 맞춘 자세의 마디가 공에 깊이 박히지 않음(지금 바디 자리 기준)
    float deepest = -1.f;
    int touching = 0;
    for (int f = 0; f < hhand::kFingerCount; ++f) {
        bool t = false;
        for (int k = 0; k < 3; ++k) {
            int a, b;
            hgrasp::boneJoints(f, k, a, b);
            glm::mat4 ma, mb;
            hp->jointWorld((hhand::EJoint)a, ma);
            hp->jointWorld((hhand::EJoint)b, mb);
            const float d = ps.capsulePenetration(rb->bodyId_, glm::vec3(ma[3]), glm::vec3(mb[3]), 0.011f);
            deepest = std::max(deepest, d);
            t = t || d > -0.004f;
        }
        touching += t;
    }
    std::printf("    가장 깊은 마디 침투 %.4f m, 공에 닿은 손가락 %d\n", deepest, touching);
    check(deepest < 0.0035f, "맞춘 손가락 마디가 공에 박히지 않음(허용 2 mm + 판정 오차)");
    check(touching >= 2, "손가락 둘 이상이 공 표면에 닿음");
    ia->release(glm::vec3(0.f), glm::vec3(0.f), false);
    for (int i = 0; i < 18; ++i) hp->tick(1.f / 90.f);
    check(hp->graspWeight() < 1e-5f && !ia->lastFitted(), "놓으면 0.1 초 동안 입력 자세로 돌아감");

    // 멀리 있는 물체는 맞추지 못하고 예전 잡기. 놓은 바디는 다음 물리 틱부터 순간 이동을 판정하므로 한 틱 흘린 뒤 옮깁니다.
    ps.tick(1.f / 90.f);
    ball->setLocalPosition(palm + normal * 0.5f);
    ps.tick(1.f / 90.f);
    ia->grab(g, handA->worldPosition(), handA->worldRotation());
    check(!ia->lastFitted(), "손이 닿지 않는 물체는 맞춤 없이 잡음(잡기는 실패하지 않음)");
    ia->release(glm::vec3(0.f), glm::vec3(0.f), false);
    ps.shutdown();
}

} // namespace

int main() {
    std::printf("HarukaHandTest\n");
    const fs::path root = HARUKA_SOURCE_DIR;
    hvfs::reset();
    hvfs::mountDirectory(root, "", 0);
    const fs::path tmp = fs::temp_directory_path() / "haruka_hand_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    testSkeletonTables();
    testSkinMath();
    std::vector<MeshSourceData> right, left;
    testGloveLoad(kGloveR, right);
    testGloveLoad(kGloveL, left);
    testCache(right, tmp);
    testHandPose(right, "오른손");
    testHandPose(left, "왼손");
    testInputMapping();
    testHandPoseInput();
    testGraspSolver();
    testGraspGlove(right);

    hvfs::reset();
    fs::remove_all(tmp, ec);
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
