// 오디오 시스템 테스트입니다. 출력 장치 없는 miniaudio 엔진(noDevice)에서 출력 샘플을 직접
// 당겨 숫자로 검산합니다. Vulkan·창·에셋 관리자 없이 World·Actor·AudioComponent·
// CameraComponent 만 씁니다.
//
// 이 테스트가 존재하는 이유: 소리는 화면으로 확인할 수 없고, 청취자 방향이나 일시정지 같은
// 결함은 "들리는 방식"으로만 드러나서 귀로 확인해도 놓치기 쉽습니다(좌우 부호가 뒤집힌 것은
// 특히 그렇습니다). 출력 버퍼의 좌우 에너지와 재생 위치를 재면 같은 결함을 결정적으로 잡습니다.
//
// 재생 위치는 램프 신호로 읽습니다. 프레임 i 의 값이 i·kStep 인 파일을 공간화 없이 틀면 출력
// 값이 곧 재생 위치입니다(miniaudio 의 리샘플러가 한 프레임 늦을 수 있어 몇 프레임 안쪽을 봅니다).
//
// 실행: build/Release/HarukaAudioTest.exe
// 없는 파일·깨진 파일을 불러오는 절에서 [Audio] 오류가 두 줄 나오는 것은 정상입니다.

#include "../src/audio/AudioSystem.hpp"
#include "../src/audio/AudioAttenuation.hpp"
#include "../src/components/AudioComponent.hpp"
#include "../src/components/CameraComponent.hpp"
#include "../src/core/World.hpp"
#include "../src/core/Actor.hpp"
#include "../src/core/ComponentFields.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

constexpr uint32_t kRate = AudioSystem::kSampleRate;
constexpr float    kPi   = 3.14159265358979f;
constexpr float    kStep = 1e-5f;     // 램프의 프레임당 증가분
constexpr float    kDt   = 1.f / 60.f;

// 모노 32비트 float WAV 를 씁니다. 16비트면 램프의 한 프레임 증가분(1e-5)이 양자화
// 간격(약 3e-5)보다 작아 재생 위치를 읽을 수 없습니다.
void writeWavF32(const fs::path& path, const std::vector<float>& s) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = (uint32_t)(s.size() * sizeof(float));
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(3 /* IEEE float */); u16(1); u32(kRate); u32(kRate * 4); u16(4); u16(32);
    f.write("data", 4); u32(bytes);
    f.write(reinterpret_cast<const char*>(s.data()), bytes);
}

std::vector<float> sine(float hz, float seconds, float amp) {
    std::vector<float> s((size_t)(seconds * kRate));
    for (size_t i = 0; i < s.size(); ++i) s[i] = amp * std::sin(2.f * kPi * hz * (float)i / kRate);
    return s;
}

std::vector<float> ramp(float seconds) {
    std::vector<float> s((size_t)(seconds * kRate));
    for (size_t i = 0; i < s.size(); ++i) s[i] = (float)i * kStep;
    return s;
}

struct Block { std::vector<float> L, R; };

Block render(AudioSystem& sys, uint32_t frames) {
    std::vector<float> buf((size_t)frames * 2, 0.f);
    sys.renderHeadless(buf.data(), frames);
    Block b;
    b.L.resize(frames);
    b.R.resize(frames);
    for (uint32_t i = 0; i < frames; ++i) { b.L[i] = buf[i * 2]; b.R[i] = buf[i * 2 + 1]; }
    return b;
}

double rms(const std::vector<float>& v) {
    double acc = 0.0;
    for (float x : v) acc += (double)x * x;
    return v.empty() ? 0.0 : std::sqrt(acc / v.size());
}

float peak(const Block& b) {
    float p = 0.f;
    for (float x : b.L) p = std::max(p, std::fabs(x));
    for (float x : b.R) p = std::max(p, std::fabs(x));
    return p;
}

struct Level { double L, R; double avg() const { return 0.5 * (L + R); } };

// 설정을 반영(tick)한 뒤, 게인 스무딩(360 프레임)이 끝나도록 조금 흘려보내고 잽니다.
Level measure(AudioSystem& sys) {
    sys.tick(kDt);
    render(sys, 2400);
    const Block b = render(sys, 9600);
    return { rms(b.L), rms(b.R) };
}

AudioComponent* spawnSound(World& w, const char* name, const fs::path& file, glm::vec3 pos,
                           bool loop, bool spatialize) {
    Actor* a = w.spawnActor(name);
    a->setLocalPosition(pos);
    auto* ac = a->addComponent<AudioComponent>();
    ac->filePath               = file.string();
    ac->loop                   = loop;
    ac->attenuation.spatialize = spatialize;
    return ac;
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

} // namespace

int main() {
    std::printf("Haruka 오디오 시스템 테스트\n");

    const fs::path tmp = fs::temp_directory_path() / "HarukaAudioTest";
    fs::create_directories(tmp);
    const fs::path toneFile  = tmp / "tone_1k.wav";      // 1 kHz, 1 초(정수 주기라 이음매 없이 반복)
    const fs::path rampFile  = tmp / "ramp.wav";         // 2 초 램프
    const fs::path shortFile = tmp / "tone_short.wav";   // 0.1 초
    const fs::path badFile   = tmp / "garbage.wav";
    writeWavF32(toneFile,  sine(1000.f, 1.f, 0.5f));
    writeWavF32(rampFile,  ramp(2.f));
    writeWavF32(shortFile, sine(1000.f, 0.1f, 0.5f));
    { std::ofstream f(badFile, std::ios::binary); f << "this is not audio at all, just text bytes"; }

    World       world;
    AudioSystem sys(true);
    sys.setWorld(&world);
    sys.startup();

    Actor* camActor = world.spawnActor("Camera");
    camActor->addComponent<CameraComponent>();   // 회전이 없으면 정면 -Z, 오른쪽 +X
    // 카메라 방향은 액터 회전이 정합니다(DEF-21). yaw 는 Y 축 회전입니다.
    auto faceYaw = [camActor](float yaw) {
        camActor->setLocalRotation(CameraComponent::yawPitchRotation(yaw, 0.f));
    };
    sys.onBeginPlay();

    // 1. 장치 없는 엔진
    std::printf("\n== 1. 장치 없는 엔진 ==\n");
    {
        std::vector<float> buf(256 * 2);
        check(sys.renderHeadless(buf.data(), 256), "noDevice 엔진이 만들어지고 출력을 당길 수 있다");
    }

    // 2. 청취자 방향: 예전에는 위치만 넘겨서 카메라를 돌려도 좌우가 그대로였습니다.
    std::printf("\n== 2. 청취자 방향(좌우) ==\n");
    {
        AudioComponent* ac = spawnSound(world, "Right", toneFile, {3.f, 0.f, 0.f}, true, true);
        Level front = measure(sys);
        check(ac->sound_ != nullptr, "사운드가 만들어진다");
        check(front.R > 2.0 * front.L, "정면이 -Z 일 때 +X 음원은 오른쪽 채널이 크다");

        faceYaw(kPi);   // +Z 를 봅니다
        Level back = measure(sys);
        check(back.L > 2.0 * back.R, "뒤로 돌면(+Z) 같은 음원이 왼쪽 채널에서 크다");

        faceYaw(0.5f * kPi);   // +X 를 봅니다(음원이 정면)
        Level ahead = measure(sys);
        check(std::fabs(ahead.L - ahead.R) < 0.05 * (ahead.L + ahead.R), "음원 쪽(+X)을 보면 좌우가 같다");

        // 롤: 머리를 오른쪽으로 90° 기울이면 +Y 음원이 왼쪽 귀 쪽입니다. 예전에는 청취자 위 벡터를
        // 늘 월드 +Y 로 넘겨 기울기가 소리에 반영되지 않았습니다(DEF-21 과 함께 고침).
        world.destroyActor(ac->owner);
        AudioComponent* up = spawnSound(world, "Up", toneFile, {0.f, 3.f, 0.f}, true, true);
        // Z 축으로 -90° 돌리면 위 벡터가 +X 로 갑니다(정면은 그대로 -Z, 오른쪽 귀는 -Y).
        camActor->setLocalRotation(glm::angleAxis(-0.5f * kPi, glm::vec3(0.f, 0.f, 1.f)));
        Level tilted = measure(sys);
        check(tilted.L > 2.0 * tilted.R, "머리를 오른쪽으로 기울이면 위쪽 음원이 왼쪽 채널에서 크다");
        ac = up;

        faceYaw(0.f);
        world.destroyActor(ac->owner);
    }

    // 2-1. XR 머리 자세: 헤드셋을 쓰면 카메라는 XR 원점일 뿐이라, 청취자는 Engine 이 넘기는 머리 자세를 따라야
    // 합니다. 예전에는 늘 카메라를 따라 고개를 돌려도 소리의 좌우가 그대로였습니다(2026-10-15).
    std::printf("\n== 2-1. XR 머리 자세 청취자 ==\n");
    {
        AudioComponent* ac = spawnSound(world, "Right", toneFile, {3.f, 0.f, 0.f}, true, true);
        Level cam = measure(sys);
        check(cam.R > 2.0 * cam.L, "자세를 넘기지 않으면 카메라(정면 -Z)를 따라 +X 음원이 오른쪽");

        sys.setListenerPose(glm::mat4_cast(CameraComponent::yawPitchRotation(kPi, 0.f)));   // 머리만 +Z 로 돌림
        Level turned = measure(sys);
        check(turned.L > 2.0 * turned.R, "머리를 뒤로 돌리면 카메라가 그대로여도 같은 음원이 왼쪽");

        sys.setListenerPose(glm::translate(glm::mat4(1.f), glm::vec3(6.f, 0.f, 0.f)));   // 머리 위치만 +X 6m
        Level moved = measure(sys);
        check(moved.L > 2.0 * moved.R, "머리 위치도 따른다(음원보다 오른쪽에 서면 음원이 왼쪽)");

        sys.clearListenerPose();
        Level back = measure(sys);
        check(back.R > 2.0 * back.L, "자세를 지우면 다시 카메라를 따른다");
        world.destroyActor(ac->owner);
    }

    // 3. 거리 감쇠: 음원을 정면(-Z)에 두면 패닝이 좌우 대칭이라 비율이 곧 감쇠 비율입니다.
    std::printf("\n== 3. 거리 감쇠 ==\n");
    {
        AudioComponent* ac = spawnSound(world, "Front", toneFile, {0.f, 0.f, -1.f}, true, true);
        Actor* a = ac->owner;
        auto at = [&](float d) { a->setLocalPosition({0.f, 0.f, -d}); return measure(sys).avg(); };

        ac->attenuation = AudioAttenuationSettings{};   // Inverse, min 1, max 100, rolloff 1
        const double inv1 = at(1.f), inv2 = at(2.f), inv8 = at(8.f);
        check(near(inv1 / inv2, 2.0, 0.1), "Inverse: 2 m 는 1 m 의 1/2");
        check(near(inv1 / inv8, 8.0, 0.4), "Inverse: 8 m 는 1 m 의 1/8");

        ac->attenuation.maxDistance = 4.f;
        check(near(at(4.f) / at(8.f), 1.0, 0.03), "Max Distance 밖에서는 더 줄지 않는다(Inverse)");

        ac->attenuation.model       = EAudioAttenuation::Linear;
        ac->attenuation.maxDistance = 11.f;
        const double lin1 = at(1.f), lin6 = at(6.f), lin11 = at(11.f);
        check(near(lin1 / lin6, 2.0, 0.1), "Linear: min 1, max 11 에서 6 m 는 절반");
        check(lin11 < 1e-4, "Linear: Max Distance 에서 무음");

        ac->attenuation.model       = EAudioAttenuation::Exponential;
        ac->attenuation.maxDistance = 100.f;
        check(near(at(1.f) / at(4.f), 4.0, 0.2), "Exponential: rolloff 1 에서 4 m 는 1/4");

        ac->attenuation.model   = EAudioAttenuation::Inverse;
        ac->attenuation.rolloff = 0.f;
        check(near(at(1.f) / at(8.f), 1.0, 0.03), "Rolloff 0 이면 거리 감쇠가 없다");

        ac->attenuation = AudioAttenuationSettings{};
        ac->attenuation.spatialize = false;
        a->setLocalPosition({5.f, 0.f, -8.f});   // 오른쪽 앞 먼 곳
        Level flat = measure(sys);
        check(near(flat.avg(), 0.5 / std::sqrt(2.0), 0.01), "Spatialize 를 끄면 거리와 무관하게 원래 크기");
        check(near(flat.L, flat.R, 1e-6), "Spatialize 를 끄면 좌우가 같다");

        world.destroyActor(a);
    }

    // 4. 일시정지·재개: 월드 그룹을 멈추면 출력이 무음이고, 재생 위치가 그대로 멈춰 있어야 합니다.
    std::printf("\n== 4. 일시정지·재개 ==\n");
    {
        AudioComponent* ac = spawnSound(world, "Ramp", rampFile, {}, false, false);
        sys.tick(kDt);
        const Block before = render(sys, 4800);
        const float last   = before.L.back();
        check(last > 4000 * kStep, "재생 중에는 램프가 흘러간다");

        sys.onPauseChanged(true);
        check(sys.isPaused(), "isPaused 가 켜진다");
        const Block during = render(sys, 4800);
        check(peak(during) == 0.f, "일시정지 중에는 무음");
        check(ac->isPlaying(), "일시정지는 재생 상태를 지우지 않는다");

        sys.onPauseChanged(false);
        const Block after = render(sys, 64);
        const float delta = after.L[4] - last;   // 4 프레임 뒤라면 약 5·kStep
        check(near(delta, 5 * kStep, 3 * kStep), "재개하면 멈춘 자리에서 이어진다");

        world.destroyActor(ac->owner);
    }

    // 5. 비활성화: 예전에는 플레이 중 setActive(false) 를 해도 소리가 계속 났습니다.
    std::printf("\n== 5. 비활성화와 재활성화 ==\n");
    {
        AudioComponent* ac = spawnSound(world, "Ramp", rampFile, {}, false, false);
        Actor* a = ac->owner;
        sys.tick(kDt);
        render(sys, 4800);

        a->setActive(false);
        check(!ac->isPlaying(), "액터를 끄면 재생이 멈춘다");
        sys.tick(kDt);
        // 첫 샘플까지 0 이어야 합니다. 월드 그룹의 피치(리샘플러)가 켜져 있으면 멈추기 직전
        // 샘플 하나가 다음 블록 첫머리에 남습니다(AudioSystem::startup 의 NO_PITCH).
        check(peak(render(sys, 4800)) == 0.f, "끈 뒤에는 첫 샘플부터 무음");

        a->setActive(true);
        check(ac->isPlaying(), "다시 켜면 autoPlay 사운드가 재생된다");
        const Block b = render(sys, 64);
        check(b.L[10] < 50 * kStep, "다시 켜면 처음부터 재생한다(되감김)");

        ac->setEnabled(false);
        check(!ac->isPlaying(), "컴포넌트를 꺼도 멈춘다");
        ac->setEnabled(true);

        Actor* parent = world.spawnActor("Parent");
        a->attachTo(parent);
        render(sys, 4800);
        parent->setActive(false);
        check(!ac->isPlaying(), "부모를 끄면 자식의 소리도 멈춘다");

        world.destroyActor(parent);   // 자식(a)도 함께 파괴됩니다
    }

    // 6. stop() 은 되감기: ma_sound_stop 만 부르면 다음 play() 가 멈춘 자리에서 이어졌습니다.
    std::printf("\n== 6. stop 과 play ==\n");
    {
        AudioComponent* ac = spawnSound(world, "Ramp", rampFile, {}, false, false);
        sys.tick(kDt);
        render(sys, 9600);
        ac->stop();
        check(!ac->isPlaying(), "stop 하면 재생이 멈춘다");
        ac->play();
        const Block b = render(sys, 64);
        check(b.L[10] < 50 * kStep, "stop 뒤 play 는 처음부터");
        world.destroyActor(ac->owner);
    }

    // 7. 플레이 중 Loop 변경: 예전에는 사운드를 만들 때만 적용해 디테일 패널 변경이 무시됐습니다.
    std::printf("\n== 7. 플레이 중 Loop 변경 ==\n");
    {
        AudioComponent* ac = spawnSound(world, "Short", shortFile, {}, false, false);
        sys.tick(kDt);          // Loop 꺼진 채로 만들고 재생 시작
        ac->loop = true;
        sys.tick(kDt);          // 다음 틱에 반영
        render(sys, 24000);     // 0.1 초 소리의 다섯 배
        check(rms(render(sys, 4800).L) > 0.1, "Loop 를 켜면 이미 만든 사운드도 반복한다");

        ac->loop = false;
        sys.tick(kDt);
        render(sys, 9600);
        check(peak(render(sys, 4800)) == 0.f, "Loop 를 끄면 끝에서 멈춘다");
        world.destroyActor(ac->owner);
    }

    // 8. 불러오기 실패: 없는 파일과 오디오가 아닌 파일은 만들 때 바로 실패로 표시됩니다.
    std::printf("\n== 8. 불러오기 실패 ==\n");
    {
        AudioComponent* missing = spawnSound(world, "Missing", tmp / "no_such_file.wav", {}, false, false);
        AudioComponent* garbage = spawnSound(world, "Garbage", badFile, {}, false, false);
        sys.tick(kDt);
        check(missing->loadFailed_ && !missing->sound_, "없는 파일은 실패로 표시하고 사운드를 만들지 않는다");
        check(garbage->loadFailed_ && !garbage->sound_, "오디오가 아닌 파일도 실패로 표시한다");

        sys.onBeginPlay();
        check(!missing->loadFailed_, "다음 플레이 시작에서 다시 시도하도록 표시를 푼다");
        world.destroyActor(missing->owner);
        world.destroyActor(garbage->owner);
    }

    // 9. 저장소의 시험용 음원: OGG 는 stb_vorbis 를 붙이기 전까지 항상 실패했습니다.
    std::printf("\n== 9. 시험용 음원(assets/audio) ==\n");
    {
        const fs::path dir = fs::path(HARUKA_ASSET_DIR) / "audio";
        for (const char* name : { "Test_BeepLoop.wav", "Test_Chime.wav", "Test_NoiseLoop.ogg" }) {
            AudioComponent* ac = spawnSound(world, name, dir / name, {}, true, false);
            sys.tick(kDt);
            render(sys, 2400);
            const double level = rms(render(sys, 24000).L);
            check(ac->sound_ != nullptr && level > 0.01,
                  std::string(name) + " 를 불러와 소리가 난다(RMS " + std::to_string(level).substr(0, 6) + ")");
            world.destroyActor(ac->owner);
        }
    }

    // 10. Pause 상태에서 Stop: 그룹이 멈춘 채 남으면 다음 플레이가 통째로 무음이 됩니다.
    std::printf("\n== 10. 일시정지 중 플레이 종료 ==\n");
    {
        AudioComponent* ac = spawnSound(world, "Tone", toneFile, {}, true, false);
        sys.tick(kDt);
        render(sys, 2400);
        sys.onPauseChanged(true);
        sys.onEndPlay();
        check(!sys.isPaused(), "onEndPlay 가 일시정지를 푼다");
        check(!ac->isPlaying(), "onEndPlay 가 남은 소리를 멈춘다");
        world.destroyActor(ac->owner);

        sys.onBeginPlay();
        spawnSound(world, "Tone2", toneFile, {}, true, false);
        sys.tick(kDt);
        render(sys, 2400);
        check(rms(render(sys, 9600).L) > 0.3, "다음 플레이의 소리가 들린다");
    }

    // 11. 종료 정리: World 는 Engine 소멸 때 onDestroy 없이 해제되므로 shutdown 이 사운드를 정리합니다.
    std::printf("\n== 11. 종료 정리 ==\n");
    {
        AudioComponent* extra = spawnSound(world, "Extra", toneFile, {}, true, true);
        sys.tick(kDt);
        check(extra->sound_ != nullptr, "종료 전에 사운드가 살아 있다");
        sys.shutdown();
        bool allClear = true;
        for (auto& ap : world.getActors())
            if (auto* ac = ap->getComponent<AudioComponent>())
                allClear = allClear && !ac->sound_ && !ac->audioSys_;
        check(allClear, "shutdown 이 모든 컴포넌트의 사운드와 시스템 포인터를 비운다");

        std::vector<Actor*> all;
        for (auto& ap : world.getActors()) all.push_back(ap.get());
        for (Actor* a : all) world.destroyActor(a);
        check(world.getActors().empty(), "종료 뒤 액터 파괴가 해제된 엔진을 건드리지 않는다");
    }

    // 12. AUDIO_3D 텍스트: 씬 파일의 3D 설정 줄입니다.
    std::printf("\n== 12. AUDIO_3D 텍스트 ==\n");
    {
        using S = AudioAttenuationSettings;
        check(S{}.toText() == "1 inverse 1 100 1", "기본값은 \"1 inverse 1 100 1\"");

        S s;
        s.spatialize = false; s.model = EAudioAttenuation::Exponential;
        s.minDistance = 0.5f; s.maxDistance = 37.25f; s.rolloff = 2.5f;
        S back;
        check(S::fromText(s.toText(), back) && back == s, "모든 값이 왕복한다(" + s.toText() + ")");

        S p; p.minDistance = 1.2345678f; p.maxDistance = 98.7654321f;
        check(S::fromText(p.toText(), back) && back == p, "실수가 정밀도를 잃지 않는다");

        check(S::fromText("  0   linear  2 20  0.5 ", back) && !back.spatialize
              && back.model == EAudioAttenuation::Linear && back.maxDistance == 20.f,
              "공백이 여러 개여도 읽는다");

        const char* bad[] = {
            "1 inverse 1 100",       "1 inverse 1 100 1 9",  "2 inverse 1 100 1",
            "1 log 1 100 1",         "1 inverse 5 5 1",      "1 inverse 0 100 1",
            "1 inverse 1 100 -1",    "1 inverse nan 100 1",  "1 inverse 1 inf 1",
            "1 inverse 1x 100 1",    "",
        };
        bool allRejected = true;
        for (const char* t : bad) {
            S keep; keep.rolloff = 7.f;
            if (S::fromText(t, keep) || keep.rolloff != 7.f) {
                allRejected = false;
                std::printf("    거부해야 하는 값이 통과함: \"%s\"\n", t);
            }
        }
        check(allRejected, "깨진 값 11종을 거부하고, 실패하면 out 을 건드리지 않는다");

        const char* names[] = { "inverse", "linear", "exponential" };
        bool namesOk = true;
        for (const char* n : names) {
            EAudioAttenuation m;
            namesOk = namesOk && S::modelFromName(n, m) && std::string(S::modelName(m)) == n;
        }
        check(namesOk, "모델 이름이 왕복한다");
    }

    // ── 직렬화(DEF-22): 이름 붙은 값의 왕복 ─────────────────────────────────────
    {
        std::printf("\n== DEF-22: AudioComponent 값이 이름으로 왕복 ==\n");
        AudioComponent src;
        src.filePath = "assets/audio/chime.wav"; src.volume = 0.35f; src.loop = true; src.autoPlay = false;
        src.attenuation.spatialize = false;
        src.attenuation.minDistance = 2.f; src.attenuation.maxDistance = 40.f;
        ComponentFieldWriter w;
        src.writeFields(w);
        AudioComponent dst;
        bool allKnown = true;
        for (const auto& kv : w.fields()) allKnown = allKnown && dst.readField(kv.first, kv.second);
        check(allKnown, "쓴 이름을 모두 다시 읽음");
        check(dst.filePath == src.filePath && dst.volume == 0.35f && dst.loop && !dst.autoPlay,
              "파일 · 볼륨 · 반복 · 자동 재생이 그대로");
        check(dst.attenuation.toText() == src.attenuation.toText(), "3D 설정이 그대로");
        AudioComponent bad;
        check(bad.readField("attenuation", "nonsense") && bad.attenuation.toText() == AudioAttenuationSettings{}.toText(),
              "깨진 3D 설정은 기본값 유지");
    }

    std::error_code ec;
    fs::remove_all(tmp, ec);

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
