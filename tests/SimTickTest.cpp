#include "../src/core/SimTick.hpp"
#include "../src/core/ProjectSettings.hpp"
#include "../src/input/HInputSystem.hpp"
#include "../src/scene/World.hpp"
#include "../src/scene/Actor.hpp"
#include "../src/scene/Component.hpp"
#include "../src/components/CameraComponent.hpp"
#include "../src/components/StaticMeshComponent.hpp"
#include "../src/components/XrComponents.hpp"
#include "../src/components/RenderExtract.hpp"
#include "../src/audio/AudioSystem.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

class DisplayProbe : public Component {
public:
    DisplayProbe() { tickGroup = ETickGroup::Display; }
    std::vector<float>* log = nullptr;
    int starts = 0;
    void onStart() override { ++starts; }
    void tick(float dt) override { if (log) log->push_back(dt); }
    const char* typeName() const override { return "DisplayProbe"; }
};

class SimProbe : public Component {
public:
    std::vector<float>* log = nullptr;
    void tick(float dt) override { if (log) log->push_back(dt); }
    const char* typeName() const override { return "SimProbe"; }
};

void testSimAccumulator() {
    std::printf("\n== D2a-1 SimAccumulator ==\n");
    constexpr float kStep = 1.f / 60.f;
    hsim::SimAccumulator acc;

    // 1. 1/60s 정확히 입력 시 1:1 틱
    auto r = acc.advance(kStep, kStep, 3);
    check(r.ticks == 1 && !r.dropped && std::fabs(r.alpha) < 1e-5f, "1/60s -> 1 tick, alpha ~ 0");

    // 2. 144Hz (1/144s) dt 열: 0틱 프레임과 1틱 프레임이 섞여 총 시간만큼 틱이 돎
    acc.reset();
    int total144 = 0;
    for (int f = 0; f < 144; ++f) total144 += acc.advance(1.f / 144.f, kStep, 3).ticks;
    // 144 * (1/144) = 1.0s -> 59 또는 60틱 (float 누적 오차 감안)
    check(total144 >= 59 && total144 <= 60, "144Hz 1초 -> 59~60 ticks (" + std::to_string(total144) + ")");

    // 3. 30Hz (1/30s) dt 열: 프레임마다 2틱
    acc.reset();
    r = acc.advance(1.f / 30.f, kStep, 3);
    check(r.ticks == 2 && !r.dropped, "1/30s -> 2 ticks");

    // 4. 상한 초과 (maxTicks = 3 인데 0.2s = 12틱 분량 입력): 3틱만 돌고 나머지 시간 버림
    acc.reset();
    r = acc.advance(0.2f, kStep, 3);
    check(r.ticks == 3 && r.dropped && acc.droppedFrames == 1 && acc.accum == 0.f,
          "상한 초과 시 3 ticks 후 남은 시간 버림 + droppedFrames 증가");
    auto rNext = acc.advance(kStep * 0.5f, kStep, 3);
    check(rNext.ticks == 0 && !rNext.dropped, "버린 뒤 다음 반 틱은 0 ticks (스파이럴 없음)");
}

void testActionEdgeLatch() {
    std::printf("\n== D2a-2 ActionEdgeLatch ==\n");
    hsim::ActionEdgeLatch l;

    // 1. 0틱 프레임에서 눌렸다 뗀 입력이 다음 시뮬레이션 틱에 1회 보존되는지
    l.recordFrame(true);   // 프레임 1 (0틱): 누름
    l.recordFrame(false);  // 프레임 2 (0틱): 뗌
    check(l.tickPressed() && l.tickReleased() && l.tickHeld(),
          "0틱 프레임 사이 짧은 눌림/뗌이 다음 시뮬레이션 틱까지 보존됨");
    l.consumeTick();
    check(!l.tickPressed() && !l.tickReleased() && !l.tickHeld(),
          "틱 소비 후 눌림/뗌 상태가 비워짐");

    // 2. 2틱 프레임에서 눌림이 첫 틱에만 한 번 소비되는지
    l.reset();
    l.recordFrame(true);   // 프레임 1 (2틱): 누름
    check(l.tickPressed() && l.tickHeld(), "2틱 프레임의 첫 번째 틱: pressed=true, held=true");
    l.consumeTick();
    check(!l.tickPressed() && l.tickHeld(), "2틱 프레임의 두 번째 틱: pressed=false (중복 소비 안 됨), held=true");
    l.consumeTick();
}

void testInputSystemSimTick() {
    std::printf("\n== D2a-3 HInputSystem sim tick latch ==\n");
    HInputSystem& in = HInputSystem::get();
    in.clearBindings();
    in.bind("Jump", 'J');
    bool down = false;
    auto probe = [&](int k) { return k == 'J' && down; };

    // 초기 떼어진 상태
    in.update(0.016f, probe, false);
    check(!in.held("Jump") && !in.pressed("Jump") && !in.released("Jump"), "초기 상태 모두 false");

    // 프레임 1 (0틱): 누름
    down = true;
    in.update(0.005f, probe, false);
    check(in.pressed("Jump") && in.held("Jump"), "프레임 문맥: pressed=true, held=true");

    // 프레임 2 (0틱): 뗌 (예전 구조에서는 시뮬레이션 틱이 이 눌림을 영영 못 봄)
    down = false;
    in.update(0.005f, probe, false);
    check(!in.held("Jump") && in.released("Jump"), "프레임 문맥: 뗀 프레임은 held=false, released=true");

    // 이제 시뮬레이션 틱 1회 실행 -> 보존된 눌림/뗌을 1회 소비
    in.beginSimTick();
    check(in.pressed("Jump") && in.released("Jump") && in.held("Jump"),
          "시뮬레이션 틱 문맥: 0틱 프레임의 눌림/뗌을 소비함");
    in.endSimTick();

    // 다음 시뮬레이션 틱(같은 프레임 내 2번째 틱) -> 이미 소비됨
    in.beginSimTick();
    check(!in.pressed("Jump") && !in.released("Jump") && !in.held("Jump"),
          "두 번째 시뮬레이션 틱: 이미 소비되어 모두 false");
    in.endSimTick();
    in.clearBindings();
}

void testDisplayTickGroup() {
    std::printf("\n== D2a-4 ETickGroup::Display ==\n");
    World w;
    std::vector<float> simLog, dispLog;
    Actor* a = w.spawnActor("Sim");
    a->addComponent<SimProbe>()->log = &simLog;
    Actor* b = w.spawnActor("Disp");
    auto* dp = b->addComponent<DisplayProbe>();
    dp->log = &dispLog;

    w.setTimeScale(0.5f);
    w.tick(1.f / 60.f);
    w.tickPostPhysics();
    check(simLog.size() == 1 && dispLog.empty(), "World::tick 은 Display 컴포넌트를 틱하지 않음");

    w.tickDisplay(0.01f);
    check(dispLog.size() == 1 && std::fabs(dispLog[0] - 0.005f) < 1e-6f && dp->starts == 1,
          "World::tickDisplay 는 Display 컴포넌트만 timeScale 적용 dt 로 틱하고 onStart 1회");

    b->setActive(false);
    w.tickDisplay(0.01f);
    check(dispLog.size() == 1, "비활성 액터의 Display 컴포넌트는 틱하지 않음");
}

void testProjectSettingsTickRate() {
    std::printf("\n== D2a-5 ProjectSettings sim.tickRate ==\n");
    std::vector<std::string> msgs;
    const ProjectSettings d = project_settings::parse("", msgs);
    check(d.simTickRate == 72.f && msgs.empty(), "기본 sim.tickRate = 72 Hz");

    msgs.clear();
    const ProjectSettings s90 = project_settings::parse("sim.tickRate = 90\n", msgs);
    check(s90.simTickRate == 90.f && msgs.empty(), "sim.tickRate = 90 파싱");

    msgs.clear();
    const ProjectSettings s60 = project_settings::parse("sim.tickRate = 60\n", msgs);
    check(s60.simTickRate == 72.f && msgs.size() == 1, "72 미만 sim.tickRate 는 72 로 올리고 경고 1줄");

    msgs.clear();
    const ProjectSettings bad = project_settings::parse("sim.tickRate = 999\n", msgs);
    check(bad.simTickRate == 72.f && !msgs.empty(), "범위 밖 sim.tickRate 는 경고 후 기본값 유지");
}

void testD2bInterpolationAndRig() {
    std::printf("\n== D2b-1 Display interpolation, local rig, held object, audio module ==\n");
    // 1. 공통 보간 + 정적 액터 캐시 유지 + 순간 이동
    World w;
    Actor* st = w.spawnActor("Static");
    st->setLocalPosition({ 1.f, 2.f, 3.f });
    Actor* mv = w.spawnActor("Moving");
    mv->setLocalPosition({ 0.f, 0.f, 0.f });

    w.snapshotTickPoses();
    mv->setLocalPosition({ 10.f, 0.f, 0.f });
    check(glm::length(st->displayWorldPosition(0.5f) - glm::vec3(1.f, 2.f, 3.f)) < 1e-6f,
          "정적 액터는 보간과 무관하게 제자리");
    check(std::fabs(mv->displayWorldPosition(0.25f).x - 2.5f) < 1e-5f,
          "움직인 액터의 표시 위치는 alpha=0.25 에서 2.5");
    check(std::fabs(mv->worldPosition().x - 10.f) < 1e-5f,
          "보간은 시뮬레이션 Transform 을 바꾸지 않음");

    mv->teleportPosition({ 50.f, 0.f, 0.f });
    check(std::fabs(mv->displayWorldPosition(0.25f).x - 50.f) < 1e-5f,
          "teleportPosition 직후에는 보간하지 않고 즉시 50");

    // 2. 로컬 리그(보간) + TrackedPose(최신 추적 값) + 카메라 xrOrigin(alpha)
    Actor* rig = w.spawnActor("Rig");
    rig->setLocalPosition({ 0.f, 0.f, 0.f });
    Actor* head = w.spawnActor("Head");
    head->attachTo(rig);
    auto* tpHead = head->addComponent<TrackedPoseComponent>();
    tpHead->source = ETrackedSource::Head;
    auto* cam = head->addComponent<CameraComponent>();
    cam->setXrRig(rig, 0.f);

    Actor* hand = w.spawnActor("RightHand");
    hand->attachTo(rig);
    auto* tpHand = hand->addComponent<TrackedPoseComponent>();
    tpHand->source = ETrackedSource::RightHand;
    auto* grabber = hand->addComponent<GrabInteractorComponent>();

    // 쥔 물체: 시뮬레이션 틱에서는 손의 이전 위치에 있었지만, 표시 추출에서는 그 프레임 손 표시 자세를 따라가야 함
    Actor* cube = w.spawnActor("Cube");
    cube->setLocalPosition({ 4.f, 1.f, -0.5f });
    auto* grabbable = cube->addComponent<GrabbableComponent>();
    struct DummyMesh {};
    static DummyMesh dummyMesh;
    auto* cubeMc = cube->addComponent<StaticMeshComponent>();
    cubeMc->mesh = reinterpret_cast<MeshHandle*>(&dummyMesh);

    // 틱 스냅샷 후 리그 이동 (0 -> 4)
    w.snapshotTickPoses();
    rig->setLocalPosition({ 4.f, 0.f, 0.f });
    // 시뮬레이션 중(손 로컬 (0,1,0))에 잡기
    hand->setLocalPosition({ 0.f, 1.f, 0.f });
    grabber->grab(grabbable, hand->worldPosition(), hand->worldRotation());

    // 표시 단계에서 헤드셋/컨트롤러 최신 추적 값 반영 (지연 없이 즉시 반영)
    HXrInputState xr{};
    xr.focused = true;
    xr.head.valid = true;
    xr.head.position = { 0.f, 1.7f, 0.f };
    xr.hands[1].active = true;
    xr.hands[1].grip.valid = true;
    xr.hands[1].grip.position = { 0.3f, 1.2f, -0.4f };
    HInputSystem::get().setXrInput(xr);
    w.tickDisplay(1.f / 90.f);

    // alpha=0.5: 보간된 리그(x=2) + 최신 머리(0,1.7,0) -> (2, 1.7, 0)
    const glm::vec3 headDisp = head->displayWorldPosition(0.5f);
    check(std::fabs(headDisp.x - 2.f) < 1e-4f && std::fabs(headDisp.y - 1.7f) < 1e-4f,
          "머리는 보간한 리그 루트(x=2) + 최신 추적 값(y=1.7)으로 그려짐");
    const glm::vec3 originPos = glm::vec3(cam->xrOrigin(0.5f)[3]);
    check(std::fabs(originPos.x - 2.f) < 1e-4f, "CameraComponent::xrOrigin(alpha) 도 보간된 리그 루트를 씀");

    // 렌더 추출에서 쥔 물체는 그 프레임 손 표시 자세(2.3, 1.2, -0.4) + 상대 자세(0, 0, -0.5)
    RenderSceneInput rsi;
    extractRenderScene(w, rsi, 0.5f);
    bool foundCube = false;
    for (const RenderMesh& rm : rsi.meshes) {
        if (rm.debugName && std::string(rm.debugName) == "Cube") {
            foundCube = true;
            const glm::vec3 p(rm.model[3]);
            check(std::fabs(p.x - 2.3f) < 1e-4f && std::fabs(p.y - 1.2f) < 1e-4f && std::fabs(p.z - (-0.9f)) < 1e-4f,
                  "쥔 물체는 보간된 시뮬레이션 자세가 아니라 그 프레임 손 표시 자세 + 상대 자세로 추출됨");
        }
    }
    check(foundCube, "쥔 물체 메시 추출 확인");
    grabber->release(glm::vec3(0.f), glm::vec3(0.f), false);
    HInputSystem::get().setXrInput(HXrInputState{});

    // 3. AudioSystem 은 표시 모듈
    AudioSystem audio(/*headless=*/true);
    check(audio.isDisplayModule(), "AudioSystem::isDisplayModule() == true");
}

} // namespace

int main() {
    testSimAccumulator();
    testActionEdgeLatch();
    testInputSystemSimTick();
    testDisplayTickGroup();
    testProjectSettingsTickRate();
    testD2bInterpolationAndRig();
    std::printf("\nSimTickTest: %s (%d failed)\n", g_failed == 0 ? "ALL PASSED" : "FAILED", g_failed);
    return g_failed == 0 ? 0 : 1;
}
