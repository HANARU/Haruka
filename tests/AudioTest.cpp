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
#include "../src/audio/AudioOcclusion.hpp"
#include "../src/physics/PhysicsSystem.hpp"
#include "../src/components/RigidBodyComponent.hpp"
#include "../src/components/ReverbZoneComponent.hpp"
#include "../src/audio/AudioReverb.hpp"
#include "../src/components/AudioComponent.hpp"
#include "../src/components/CameraComponent.hpp"
#include "../src/core/World.hpp"
#include "../src/core/Actor.hpp"
#include "../src/core/ComponentFields.hpp"
#include "../src/core/TextUtf.hpp"
#include "../src/vfs/PakReader.hpp"
#include "../src/vfs/PakWriter.hpp"
#include "../src/vfs/Vfs.hpp"

#include "miniaudio.h"   // 14절의 비교용 엔진(구현은 MiniaudioImpl.cpp)

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
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
        check(ac->voice_ != nullptr, "사운드가 만들어진다");
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

    // 2-1b. 들리지 않는 소리의 가상화(수학 보강 S3, 2026-11-17): 예상 게인이 −60 dB 아래면 믹싱을 멈추고 위치만 세다가,
    // 들리게 되면 그 위치에서 이어 냅니다.
    std::printf("\n== 2-1b. 들리지 않는 소리의 가상화(S3) ==\n");
    {
        AudioAttenuationSettings at;   // Inverse, min 1, max 100, rolloff 1
        check(near(hvirt::distanceGain(at, 0.5f), 1.0, 1e-6) && near(hvirt::distanceGain(at, 10.f), 0.1, 1e-6) &&
              near(hvirt::distanceGain(at, 1000.f), 0.01, 1e-6),
              "Inverse: 최소 거리 안 1, 10m 0.1, 최대 거리 밖은 최대 거리 값 0.01(miniaudio 와 같음)");
        at.model = EAudioAttenuation::Linear;
        check(hvirt::distanceGain(at, 100.f) == 0.f && near(hvirt::distanceGain(at, 50.5f), 0.5, 1e-6),
              "Linear: 최대 거리에서 0, 가운데 0.5");
        at.model = EAudioAttenuation::Exponential;
        check(near(hvirt::distanceGain(at, 4.f), 0.25, 1e-6), "Exponential: (4/1)^-1 = 0.25");
        check(hvirt::nextVirtual(false, 0.0009f) && !hvirt::nextVirtual(false, 0.0011f) &&
              hvirt::nextVirtual(true, 0.0015f) && !hvirt::nextVirtual(true, 0.0021f),
              "−60 dB 아래로 가상, −54 dB 위로 복귀(그 사이는 지금 상태 유지)");
        bool ended = false;
        check(near(hvirt::advanceCursor(900.0, 300.0, 1000, true, ended), 200.0, 1e-9) && !ended, "반복이면 길이로 접음");
        check(hvirt::advanceCursor(900.0, 300.0, 1000, false, ended) == 1000.0 && ended, "반복이 아니면 끝에서 멈추고 끝남");

        // 램프(값 = 프레임 × 1e-5)로 이어 낸 위치를 잽니다. Linear · 최대 10m 라 20m 에서는 게인 0.
        AudioComponent* ac = spawnSound(world, "Far", rampFile, { 0.f, 0.f, -20.f }, false, true);
        ac->attenuation.model       = EAudioAttenuation::Linear;
        ac->attenuation.maxDistance = 10.f;
        sys.tick(kDt);
        check(ac->voice_ && sys.voiceVirtual(ac->voice_) && sys.isVoicePlaying(ac->voice_),
              "들리지 않는 자리에서 자동 재생하면 가상으로 시작(재생 중으로 보임)");
        for (int i = 0; i < 30; ++i) { sys.tick(kDt); render(sys, 800); }   // 0.5 초
        check(sys.voiceVirtual(ac->voice_), "들리지 않는 동안 가상 유지");
        ac->owner->setLocalPosition({ 0.f, 0.f, -1.f });   // 최소 거리: 게인 1
        sys.tick(kDt);
        check(!sys.voiceVirtual(ac->voice_), "들리는 자리로 오면 이어 냄");
        render(sys, 480);   // 게인 평활(360 프레임)이 끝나게 흘림
        const Block b = render(sys, 16);
        // 정면 음원의 채널 게인은 0.5(2-2 절). 31틱(24,800 프레임) + 흘린 480 프레임 근처여야 합니다(처음부터였다면 약 500).
        const double frames = b.L[0] / (0.5 * kStep);
        check(frames > 24000.0 && frames < 26500.0,
              "가상 동안 센 위치에서 이어 냄(" + std::to_string((int)frames) + " 프레임, 기대 약 25,280)");
        sys.stopVoice(ac->voice_);
        check(!sys.voiceVirtual(ac->voice_) && !sys.isVoicePlaying(ac->voice_), "stop 은 가상 상태도 풂");
        world.destroyActor(ac->owner);

        // 반복이 아닌 짧은 소리가 들리지 않는 채로 끝나면 재생이 끝난 것으로 봅니다.
        AudioComponent* shortFar = spawnSound(world, "ShortFar", shortFile, { 0.f, 0.f, -20.f }, false, true);
        shortFar->attenuation.model       = EAudioAttenuation::Linear;
        shortFar->attenuation.maxDistance = 10.f;
        sys.tick(kDt);
        const bool startedVirtual = sys.voiceVirtual(shortFar->voice_);
        for (int i = 0; i < 12; ++i) { sys.tick(kDt); render(sys, 800); }   // 0.2 초 > 0.1 초
        check(startedVirtual && !sys.voiceVirtual(shortFar->voice_) && !sys.isVoicePlaying(shortFar->voice_),
              "들리지 않는 채로 끝까지 가면 재생이 끝남");
        world.destroyActor(shortFar->owner);

        // 2D 소리는 거리와 무관해 가상화하지 않습니다.
        AudioComponent* flat = spawnSound(world, "Flat2D", toneFile, { 0.f, 0.f, -500.f }, true, false);
        sys.tick(kDt);
        check(flat->voice_ && !sys.voiceVirtual(flat->voice_) && sys.isVoicePlaying(flat->voice_), "2D 소리는 가상화하지 않음");
        world.destroyActor(flat->owner);
        sys.tick(kDt);

        // 최대 거리 밖 무음(silentBeyondMax): Inverse min 1 · max 10 이면 g(5) = 0.2, g_max = 0.1 → (0.2 − 0.1)/0.9 = 0.111.
        AudioAttenuationSettings sa;
        sa.maxDistance     = 10.f;
        sa.silentBeyondMax = true;
        check(near(hvirt::distanceGain(sa, 5.f), 0.1 / 0.9, 1e-6) && hvirt::distanceGain(sa, 10.f) == 0.f &&
              hvirt::distanceGain(sa, 50.f) == 0.f && hvirt::distanceGain(sa, 0.5f) == 1.f,
              "무음 곡선: 5m 0.111, 최대 거리에서 0, 밖도 0, 최소 거리 안 1");
        check(near(hvirt::silenceFactor(sa, 5.f), (0.1 / 0.9) / 0.2, 1e-6), "miniaudio 게인에 곱할 배율 = 0.556");
        sa.rolloff = 0.f;
        check(hvirt::distanceGain(sa, 9.f) == 1.f && hvirt::distanceGain(sa, 11.f) == 0.f, "rolloff 0 이면 최대 거리 안 1, 밖 0");

        AudioComponent* loud = spawnSound(world, "Silent", toneFile, { 0.f, 0.f, -5.f }, true, true);
        loud->attenuation.maxDistance = 10.f;
        const Level plain = measure(sys);
        loud->attenuation.silentBeyondMax = true;
        const Level quiet = measure(sys);
        check(near(quiet.avg() / plain.avg(), (0.1 / 0.9) / 0.2, 0.01),
              "켜면 5m 소리가 배율(0.556)만큼 작아짐 (" + std::to_string(quiet.avg() / plain.avg()) + ")");
        loud->owner->setLocalPosition({ 0.f, 0.f, -40.f });   // 끄면 −20 dB(0.1)에 머무는 거리
        const Level beyond = measure(sys);
        check(beyond.avg() < 1e-6 && sys.voiceVirtual(loud->voice_), "최대 거리 밖에서는 무음이고 가상화됨");
        ComponentFieldWriter fw;
        loud->writeFields(fw);
        AudioComponent back;
        for (const auto& kv : fw.fields()) back.readField(kv.first, kv.second);
        check(back.attenuation.silentBeyondMax && back.attenuation.maxDistance == 10.f, "silentBeyondMax 가 씬 파일로 왕복");
        world.destroyActor(loud->owner);
        sys.tick(kDt);
    }

    // 2-2. 스테레오 패닝 값(DEF-166, 2026-11-06): miniaudio 는 스테레오 출력의 스피커를 정확히 좌우(±X)에 둡니다
    // (ma_get_default_channel_map_for_spatializer). 채널 게인은 음원 방향과 스피커 방향의 내적을 0~1 로 옮긴 값(최소 0.2)이라,
    // 정면 · 뒤 · 바로 위 음원은 두 채널 모두 0.5 이고 앞뒤 · 위아래를 구분하지 못합니다. 예전 문서는 스피커를 앞쪽 ±45° 로 보고
    // 정면 음원을 0.854 라고 적었습니다. 이 절의 값은 HRTF 가 꺼진 기본 상태(Auto, XR 세션 없음)의 패닝이고, HRTF 는 17절에서 봅니다.
    std::printf("\n== 2-2. 스테레오 패닝 값(DEF-166) ==\n");
    {
        const double flat = 0.5 / std::sqrt(2.0);   // 공간화 없는 진폭 0.5 사인의 채널 RMS
        AudioComponent* ac = spawnSound(world, "Pan", toneFile, {0.f, 0.f, -2.f}, true, true);
        ac->attenuation.rolloff = 0.f;   // 거리 감쇠 없이 패닝 게인만 봅니다
        Actor* a = ac->owner;
        auto at = [&](glm::vec3 p) {
            a->setLocalPosition(p);
            const Level l = measure(sys);
            return Level{ l.L / flat, l.R / flat };
        };
        const Level front = at({ 0.f, 0.f, -2.f }), back = at({ 0.f, 0.f, 2.f });
        const Level right = at({ 2.f, 0.f, 0.f }),  left = at({ -2.f, 0.f, 0.f }), up = at({ 0.f, 2.f, 0.f });
        std::printf("    정면 %.3f/%.3f, 뒤 %.3f/%.3f, 오른쪽 %.3f/%.3f, 왼쪽 %.3f/%.3f, 위 %.3f/%.3f (왼쪽/오른쪽)\n",
                    front.L, front.R, back.L, back.R, right.L, right.R, left.L, left.R, up.L, up.R);
        check(near(front.L, 0.5, 0.01) && near(front.R, 0.5, 0.01), "정면 음원은 두 채널 모두 0.5(예전 문서의 0.854 아님)");
        check(near(back.L, front.L, 0.005) && near(back.R, front.R, 0.005), "뒤 음원도 정면과 같다(앞뒤 구분 없음)");
        check(near(right.R, 1.0, 0.01) && near(right.L, 0.2, 0.01), "오른쪽 음원은 오른쪽 1 · 왼쪽 0.2(최소 게인)");
        check(near(left.L, 1.0, 0.01) && near(left.R, 0.2, 0.01), "왼쪽 음원은 왼쪽 1 · 오른쪽 0.2");
        check(near(up.L, 0.5, 0.01) && near(up.R, 0.5, 0.01), "바로 위 음원도 0.5(위아래 구분 없음)");
        world.destroyActor(a);
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
        check(missing->loadFailed_ && !missing->voice_, "없는 파일은 실패로 표시하고 사운드를 만들지 않는다");
        check(garbage->loadFailed_ && !garbage->voice_, "오디오가 아닌 파일도 실패로 표시한다");

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
            check(ac->voice_ != nullptr && level > 0.01,
                  std::string(name) + " 를 불러와 소리가 난다(RMS " + std::to_string(level).substr(0, 6) + ")");
            world.destroyActor(ac->owner);
        }
    }

    // 9-1. 묶음 파일(2026-10-24): 게임 실행 파일은 소리를 game.hpak 에서 읽습니다. 같은 음원을 메모리 묶음에 넣고 논리
    // 경로로 불러, miniaudio 의 파일 콜백(가상 파일 시스템)을 거쳐 디코딩되는지 봅니다.
    std::printf("\n== 9-1. 묶음 안 음원 ==\n");
    {
        const fs::path dir = fs::path(HARUKA_ASSET_DIR) / "audio";
        std::vector<hpak::PakInput> inputs;
        for (const char* name : { "Test_Chime.wav", "Test_NoiseLoop.ogg" }) {
            hpak::PakInput in;
            in.path = std::string("assets/audio/") + name;
            std::ifstream f(dir / name, std::ios::binary);
            in.data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            in.codec = hpak::Codec::Stored;
            inputs.push_back(std::move(in));
        }
        std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
        std::string err;
        const bool built = hpak::writePak(std::move(inputs), ss, err);
        const std::string s = ss.str();
        auto reader = std::make_unique<hpak::PakReader>();
        const bool opened = built && reader->open(hpak::memorySource(std::vector<uint8_t>(s.begin(), s.end())), err);
        hvfs::reset();
        if (opened) hvfs::mountPack(std::move(reader), "audio_test.hpak", 0);
        check(opened, "음원 둘을 담은 묶음을 마운트 " + err);
        for (const char* name : { "Test_Chime.wav", "Test_NoiseLoop.ogg" }) {
            AudioComponent* ac = spawnSound(world, name, fs::path("assets/audio") / name, {}, true, false);
            sys.tick(kDt);
            render(sys, 2400);
            const double level = rms(render(sys, 24000).L);
            check(ac->voice_ != nullptr && level > 0.01,
                  std::string("묶음 안 ") + name + " 를 논리 경로로 불러와 소리가 난다(RMS " + std::to_string(level).substr(0, 6) + ")");
            world.destroyActor(ac->owner);
        }
        hvfs::reset();
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
        check(extra->voice_ != nullptr, "종료 전에 사운드가 살아 있다");
        sys.shutdown();
        bool allClear = true;
        for (auto& ap : world.getActors())
            if (auto* ac = ap->getComponent<AudioComponent>())
                allClear = allClear && !ac->voice_ && !ac->audioSys_;
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

    // 14. 음원별 노드 사슬(2026-11-06): 사운드 뒤에 Haruka 노드를 끼워도(차폐 필터 · 잔향 센드 · HRTF 자리), 효과가 꺼진
    // 동안의 출력은 예전처럼 사운드를 월드 그룹에 바로 붙인 출력과 비트 단위로 같아야 합니다. 같은 장면을 miniaudio 엔진
    // 하나에 예전 방식으로 직접 만들어 나란히 렌더하고 바이트를 비교합니다(고정 해시값은 CPU 의 SIMD 경로에 따라 달라질 수
    // 있어 쓰지 않습니다). 음원이 움직이고 청취자가 도는 동안, 블록 크기를 노드 캐시(480 프레임)와 어긋나게 잡습니다.
    std::printf("\n== 14. 음원별 노드 사슬: 효과가 꺼지면 예전 출력과 비트 단위로 같음 ==\n");
    {
        World       w2;
        AudioSystem s2(true);
        s2.setWorld(&w2);
        s2.startup();
        Actor* cam2 = w2.spawnActor("Camera");
        auto*  camc = cam2->addComponent<CameraComponent>();
        s2.onBeginPlay();

        struct Src {
            const fs::path* file; glm::vec3 pos, vel; bool loop, spatial;
            EAudioAttenuation model; float vol, minD, maxD, rolloff;
        };
        const Src srcs[] = {
            { &toneFile,  {  3.f,  0.f, -2.f }, { -0.5f, 0.f,  0.1f }, true,  true,  EAudioAttenuation::Inverse,     1.0f, 1.f,  100.f, 1.f  },
            { &rampFile,  {  0.f,  0.f,  0.f }, {  0.f,  0.f,  0.f  }, true,  false, EAudioAttenuation::Inverse,     0.7f, 1.f,  100.f, 1.f  },
            { &toneFile,  { -2.f,  1.f,  4.f }, {  0.3f, 0.f, -0.6f }, true,  true,  EAudioAttenuation::Linear,      0.8f, 0.5f, 12.f,  1.f  },
            { &shortFile, {  1.f, -1.f, -6.f }, {  0.f,  0.f,  0.f  }, false, true,  EAudioAttenuation::Exponential, 1.2f, 1.f,  50.f,  1.5f },
        };
        std::vector<AudioComponent*> comps;
        for (const Src& s : srcs) {
            AudioComponent* ac = spawnSound(w2, "Src", *s.file, s.pos, s.loop, s.spatial);
            ac->volume                  = s.vol;
            ac->attenuation.model       = s.model;
            ac->attenuation.minDistance = s.minD;
            ac->attenuation.maxDistance = s.maxD;
            ac->attenuation.rolloff     = s.rolloff;
            comps.push_back(ac);
        }

        // 예전 방식: 피치를 끈 월드 그룹 하나, 사운드는 그 그룹에 바로 붙습니다(AudioSystem 의 2026-11-06 전 구조).
        ma_engine        ref{};
        ma_engine_config rc = ma_engine_config_init();
        rc.channels   = AudioSystem::kChannels;
        rc.sampleRate = kRate;
        rc.noDevice   = MA_TRUE;
        const bool engineOk = ma_engine_init(&rc, &ref) == MA_SUCCESS;
        ma_sound_group refGroup{};
        const bool groupOk = engineOk && ma_sound_group_init(&ref, MA_SOUND_FLAG_NO_PITCH, nullptr, &refGroup) == MA_SUCCESS;
        bool refOk = groupOk;
        std::vector<std::unique_ptr<ma_sound>> refSounds;
        for (size_t i = 0; refOk && i < std::size(srcs); ++i) {
            auto snd = std::make_unique<ma_sound>();
            refOk = ma_sound_init_from_file(&ref, srcs[i].file->string().c_str(), MA_SOUND_FLAG_ASYNC, &refGroup, nullptr,
                                            snd.get()) == MA_SUCCESS;
            if (refOk) {
                ma_sound_set_looping(snd.get(), srcs[i].loop ? MA_TRUE : MA_FALSE);
                refSounds.push_back(std::move(snd));
            }
        }
        check(refOk, "비교용 엔진(예전 방식)을 만든다");

        // AudioSystem::tick 과 같은 순서로 청취자 · 위치 · 설정을 넣습니다(첫 틱에서 시작).
        auto refTick = [&](bool first) {
            const glm::vec3 p = cam2->worldPosition(), f = camc->forward(), u = camc->up();
            ma_engine_listener_set_position(&ref, 0, p.x, p.y, p.z);
            ma_engine_listener_set_direction(&ref, 0, f.x, f.y, f.z);
            ma_engine_listener_set_world_up(&ref, 0, u.x, u.y, u.z);
            for (size_t i = 0; i < refSounds.size(); ++i) {
                ma_sound* s = refSounds[i].get();
                const AudioComponent& ac = *comps[i];
                const glm::vec3 pos = ac.owner->worldPosition();
                ma_sound_set_position(s, pos.x, pos.y, pos.z);
                ma_sound_set_volume(s, ac.volume);
                ma_sound_set_looping(s, ac.loop ? MA_TRUE : MA_FALSE);
                ma_sound_set_spatialization_enabled(s, ac.attenuation.spatialize ? MA_TRUE : MA_FALSE);
                ma_sound_set_attenuation_model(s, (ma_attenuation_model)ac.attenuation.model);
                ma_sound_set_min_distance(s, ac.attenuation.minDistance);
                ma_sound_set_max_distance(s, ac.attenuation.maxDistance);
                ma_sound_set_rolloff(s, ac.attenuation.rolloff);
                if (first) ma_sound_start(s);
            }
        };

        bool   same      = refOk;
        size_t firstDiff = 0, compared = 0;
        double energy    = 0.0;
        constexpr uint32_t kBlock = 1500;   // 노드 캐시 480 의 배수가 아님
        std::vector<float> a(kBlock * 2), b(kBlock * 2);
        for (int it = 0; refOk && it < 16; ++it) {
            for (size_t i = 0; i < comps.size(); ++i)
                comps[i]->owner->setLocalPosition(srcs[i].pos + srcs[i].vel * (float)it);
            cam2->setLocalRotation(CameraComponent::yawPitchRotation(0.3f * (float)it, 0.1f * (float)(it % 3)));
            s2.tick(kDt);
            refTick(it == 0);
            s2.renderHeadless(a.data(), kBlock);
            ma_uint64 read = 0;
            ma_engine_read_pcm_frames(&ref, b.data(), kBlock, &read);
            for (size_t k = 0; k < a.size(); ++k) energy += (double)a[k] * a[k];
            if (same && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) != 0) {
                same = false;
                size_t k = 0;
                while (k < a.size() && std::memcmp(&a[k], &b[k], sizeof(float)) == 0) ++k;
                firstDiff = compared + k;
                std::printf("    처음 다른 샘플 %zu: %.9g 대 %.9g\n", firstDiff, (double)a[k], (double)b[k]);
            }
            compared += a.size();
        }
        check(same, "음원 4개(3D 셋 · 2D 하나)가 움직이고 청취자가 도는 16블록 동안 출력이 바이트 단위로 같다");
        check(energy / (double)std::max<size_t>(compared, 1) > 1e-4, "비교한 출력이 무음이 아니다");

        for (auto& s : refSounds) ma_sound_uninit(s.get());
        if (groupOk) ma_sound_group_uninit(&refGroup);
        if (engineOk) ma_engine_uninit(&ref);
        s2.shutdown();
    }

    // 15. 차폐(2026-11-06, Audio_Design.md 9-4): 청취자와 음원 사이를 물리 충돌체가 막으면 볼륨을 줄이고 저역 통과를 겁니다.
    // 실제 물리(Jolt)로 정적 상자 벽을 세우고, 200 Hz · 8 kHz 음의 크기 비율을 1차 필터 두 단의 해석값과 비교합니다.
    std::printf("\n== 15. 차폐 ==\n");
    {
        using O = AudioOcclusionSettings;
        // 순수 계산
        {
            O s;   // lowPass 1200, volume 0.4
            check(hocc::gain(0.f, s) == 1.f && near(hocc::gain(1.f, s), 0.4, 1e-6) && near(hocc::gain(0.5f, s), 0.7, 1e-6),
                  "게인: 열림 1, 막힘 0.4, 절반 0.7");
            check(near(hocc::cutoffHz(0.f, s), 20000.0, 0.5) && near(hocc::cutoffHz(1.f, s), 1200.0, 0.05)
                  && near(hocc::cutoffHz(0.5f, s), std::sqrt(20000.0 * 1200.0), 0.5), "차단 주파수는 로그 축(절반 = 기하 평균 4899 Hz)");
            check(hocc::smooth(0.7f, 0.f, kDt, 0.f) == 0.f, "시간 상수 0 이면 바로 목표");
            check(near(hocc::smooth(1.f, 0.f, 0.1f, 0.1f), std::exp(-1.0), 1e-5), "시간 상수만큼 지나면 차이가 1/e");
            check(hocc::smooth(0.0005f, 0.f, kDt, 0.1f) == 0.f, "목표에 1e-3 안이면 목표로 맞춤(필터를 건너뛸 수 있게)");
            std::vector<glm::vec3> pts;
            hocc::samplePoints({ 0.f, 0.f, 0.f }, { 0.f, 0.f, -6.f }, 0.f, pts);
            check(pts.size() == 1, "반지름 0 이면 표본은 음원 하나");
            hocc::samplePoints({ 1.f, 2.f, 3.f }, { 4.f, -2.f, 9.f }, 0.5f, pts);
            bool ring = pts.size() == 9;
            const glm::vec3 axis = glm::normalize(glm::vec3(3.f, -4.f, 6.f));
            for (size_t k = 1; ring && k < pts.size(); ++k) {
                const glm::vec3 off = pts[k] - glm::vec3(4.f, -2.f, 9.f);
                ring = near(glm::length(off), 0.5, 1e-5) && std::fabs(glm::dot(off, axis)) < 1e-5f;
            }
            check(ring, "반지름이 있으면 음원 + 축에 수직인 반지름 원판 둘레 8개");
            hocc::samplePoints({ 0.f, 0.f, 0.f }, { 0.f, 5.f, 0.f }, 0.5f, pts);   // 축이 +Y(기저를 다른 축으로)
            check(pts.size() == 9 && std::fabs(glm::dot(pts[3] - glm::vec3(0.f, 5.f, 0.f), glm::vec3(0.f, 1.f, 0.f))) < 1e-5f,
                  "축이 위(+Y)여도 수직 원판");
        }

        // 설정 텍스트
        {
            check(O{}.toText() == "0 1200 0.4 0.1 0", "기본값은 \"0 1200 0.4 0.1 0\"(끔)");
            O s; s.enabled = true; s.lowPassHz = 850.5f; s.volume = 0.25f; s.smoothTime = 0.35f; s.radius = 1.25f;
            O back;
            check(O::fromText(s.toText(), back) && back == s, "모든 값이 왕복(" + s.toText() + ")");
            const char* bad[] = { "1 1200 0.4 0.1", "1 1200 0.4 0.1 0 0", "2 1200 0.4 0.1 0", "1 10 0.4 0.1 0", "1 30000 0.4 0.1 0",
                                  "1 1200 1.5 0.1 0", "1 1200 -0.1 0.1 0", "1 1200 0.4 -1 0", "1 1200 0.4 11 0", "1 1200 0.4 0.1 -1",
                                  "1 1200 0.4 0.1 11", "1 nan 0.4 0.1 0", "1 1200 0.4x 0.1 0", "" };
            bool allRejected = true;
            for (const char* t : bad) {
                O keep; keep.volume = 0.123f;
                if (O::fromText(t, keep) || keep.volume != 0.123f) { allRejected = false; std::printf("    통과함: \"%s\"\n", t); }
            }
            check(allRejected, "깨진 값 14종을 거부하고, 실패하면 out 을 건드리지 않는다");
            AudioComponent src;
            src.occlusion = s;
            ComponentFieldWriter w;
            src.writeFields(w);
            AudioComponent dst;
            for (const auto& kv : w.fields()) dst.readField(kv.first, kv.second);
            check(dst.occlusion == s, "컴포넌트 직렬화로 차폐 설정이 왕복(키 occlusion)");
            AudioComponent badc;
            check(badc.readField("occlusion", "nonsense") && badc.occlusion == O{}, "깨진 차폐 설정은 기본값 유지");
        }

        const fs::path lowFile  = tmp / "tone_200.wav";    // 200 Hz(9600 프레임 = 40주기)
        const fs::path highFile = tmp / "tone_8k.wav";     // 8 kHz
        writeWavF32(lowFile,  sine(200.f, 1.f, 0.5f));
        writeWavF32(highFile, sine(8000.f, 1.f, 0.5f));

        World         w3;
        PhysicsSystem ps;
        ps.startup();
        ps.setWorld(&w3);
        AudioSystem s3(true);
        s3.setWorld(&w3);
        s3.setPhysics(&ps);
        s3.startup();
        Actor* cam3 = w3.spawnActor("Camera");
        cam3->addComponent<CameraComponent>();   // 원점, 정면 -Z
        s3.onBeginPlay();

        auto step  = [&]() { ps.tick(kDt); s3.tick(kDt); };   // 바디를 먼저 만들고 판정
        // 재생 중인 소리는 0.1 s(kRetraceSec) 간격으로만 다시 판정합니다. 벽을 세우거나 음원을 옮긴 뒤에는 그보다 길게 흘립니다.
        auto settle = [&]() { for (int i = 0; i < 8; ++i) { step(); render(s3, 800); } };
        auto meas3 = [&]() {
            step();
            render(s3, 2400);
            const Block b = render(s3, 9600);
            return Level{ rms(b.L), rms(b.R) };
        };
        auto box = [&](const char* name, glm::vec3 pos, HalfExt he) {
            Actor* a = w3.spawnActor(name);
            a->setLocalPosition(pos);
            auto* rb = a->addComponent<RigidBodyComponent>();
            rb->motionType = EMotionType::Static;
            rb->halfExtent = he;
            return a;
        };
        // 1차 저역 통과 두 단의 진폭 응답(해석값): |H| = (1 − a) / |1 − a·e^(−jω)|, 두 단이라 제곱
        auto lpf2 = [](double hz, double cutoff) {
            const double a = std::exp(-2.0 * 3.14159265358979 * cutoff / kRate);
            const double w = 2.0 * 3.14159265358979 * hz / kRate;
            const double h = (1.0 - a) / std::sqrt(1.0 - 2.0 * a * std::cos(w) + a * a);
            return h * h;
        };
        auto makeSrc = [&](const fs::path& f, glm::vec3 pos, bool occl) {
            AudioComponent* ac = spawnSound(w3, "Src", f, pos, true, true);
            ac->attenuation.rolloff  = 0.f;   // 거리 감쇠 없이 차폐만 봅니다
            ac->occlusion.enabled    = occl;
            ac->occlusion.smoothTime = 0.f;   // 정상 상태 비교는 평활 없이
            return ac;
        };
        const glm::vec3 srcPos{ 0.f, 0.f, -6.f };

        // 열림: 벽이 없으면 차폐를 켜도 필터를 건너뛰어, 끈 소리와 크기가 같습니다.
        AudioComponent* plain = makeSrc(lowFile, srcPos, false);
        const Level openPlain = meas3();
        w3.destroyActor(plain->owner);
        AudioComponent* lo = makeSrc(lowFile, srcPos, true);
        const Level openLo = meas3();
        check(lo->occlusionAmount() == 0.f && near(openLo.avg(), openPlain.avg(), 1e-6 * openPlain.avg()),
              "벽이 없으면 차폐 0, 차폐를 끈 소리와 크기가 같다(필터를 건너뜀)");

        // 벽: 청취자와 음원 사이 z = -3 에 4 m × 4 m 판
        Actor* wall = box("Wall", { 0.f, 0.f, -3.f }, { 2.f, 2.f, 0.1f });
        settle();
        const Level shutLo = meas3();
        const double expLo = 0.4 * lpf2(200.0, 1200.0);
        std::printf("    200 Hz: 막힘 / 열림 = %.4f (해석값 %.4f)\n", shutLo.avg() / openLo.avg(), expLo);
        check(lo->occlusionAmount() == 1.f, "벽 뒤 음원은 차폐 1");
        check(near(shutLo.avg() / openLo.avg(), expLo, 0.02 * expLo), "200 Hz 는 볼륨 0.4 × 필터 통과분만 줄어든다(해석값 ±2%)");
        w3.destroyActor(lo->owner);

        AudioComponent* hiOpen = makeSrc(highFile, srcPos, false);
        const Level openHi = meas3();
        w3.destroyActor(hiOpen->owner);
        AudioComponent* hi = makeSrc(highFile, srcPos, true);
        const Level shutHi = meas3();
        const double expHi = 0.4 * lpf2(8000.0, 1200.0);
        std::printf("    8 kHz: 막힘 / 열림 = %.5f (해석값 %.5f)\n", shutHi.avg() / openHi.avg(), expHi);
        check(near(shutHi.avg() / openHi.avg(), expHi, 0.05 * expHi) && shutHi.avg() < 0.03 * openHi.avg(),
              "8 kHz 는 크게 깎인다(1200 Hz 저역 통과 두 단, 해석값 ±5%)");
        w3.destroyActor(hi->owner);

        // 첫 블록: 벽 뒤에서 새로 만든 소리는 첫 블록부터 막혀 있어야 합니다(UE 의 첫 트레이스 전 튐이 없어야 함).
        auto firstBlock = [&](bool occl) {
            AudioComponent* ac = makeSrc(lowFile, srcPos, occl);
            ac->occlusion.smoothTime = 0.1f;
            step();
            const Block b = render(s3, 480);
            w3.destroyActor(ac->owner);
            return 0.5 * (rms(b.L) + rms(b.R));
        };
        const double firstOpen = firstBlock(false), firstShut = firstBlock(true);
        check(firstOpen > 0.0 && near(firstShut / firstOpen, expLo, 0.05 * expLo), "새 소리는 첫 블록부터 막힌 크기(튐 없음)");

        // 평활: 벽을 치우면 0.1 s 안에 다시 판정하고, 시간 상수 0.1 s 로 서서히 열리다가 정확히 0 이 됩니다.
        AudioComponent* sm = makeSrc(lowFile, srcPos, true);
        sm->occlusion.smoothTime = 0.1f;
        step();
        check(sm->occlusionAmount() == 1.f, "평활 시험: 처음에는 막힘");
        w3.destroyActor(wall);
        bool monotonic = true, sawMiddle = false;
        float prev = 1.f;
        int   openedAt = -1;
        for (int i = 1; i <= 90 && openedAt < 0; ++i) {
            step();
            render(s3, 800);
            const float o = sm->occlusionAmount();
            monotonic = monotonic && o <= prev;
            sawMiddle = sawMiddle || (o > 0.05f && o < 0.95f);
            prev = o;
            if (o == 0.f) openedAt = i;
        }
        std::printf("    벽을 치운 뒤 %d 틱(%.2f s)에 완전히 열림\n", openedAt, openedAt * kDt);
        check(monotonic && sawMiddle, "벽을 치우면 차폐가 줄어들기만 하고 중간 값을 거친다(서서히 열림)");
        check(openedAt > 0 && openedAt * kDt < 1.0f, "1초 안에 정확히 0(필터를 다시 건너뜀)");
        const Level reopened = meas3();
        check(near(reopened.avg(), openLo.avg(), 1e-5 * openLo.avg()), "다 열린 뒤 크기는 처음 열림과 같다");

        // 멈췄다 다시 재생: 멈춘 동안 벽이 생기면 다시 재생한 다음 틱에 바로 다시 판정해 막힌 값으로 시작합니다.
        sm->stop();
        wall = box("Wall", { 0.f, 0.f, -3.f }, { 2.f, 2.f, 0.1f });
        sm->play();
        step();
        check(sm->occlusionAmount() == 1.f, "멈춘 동안 벽이 생기면 다시 재생한 다음 틱에 바로 막힘(평활을 기다리지 않음)");
        w3.destroyActor(sm->owner);

        // 자기 충돌체 · 부모 충돌체 · 청취자 충돌체는 막지 않습니다(레이에서 뺌). 2D 소리는 차폐가 걸리지 않습니다.
        w3.destroyActor(wall);
        AudioComponent* self = makeSrc(lowFile, srcPos, true);
        auto* srb = self->owner->addComponent<RigidBodyComponent>();
        srb->motionType = EMotionType::Static;
        srb->halfExtent = { 0.3f, 0.3f, 0.3f };
        meas3();
        check(self->occlusionAmount() == 0.f, "음원 자신의 충돌체 안에서도 막힘이 아니다");
        Actor* holder = box("Holder", srcPos, { 0.4f, 0.4f, 0.4f });
        self->owner->attachTo(holder);
        settle();
        check(self->occlusionAmount() == 0.f, "음원을 감싼 부모의 충돌체도 막힘이 아니다");
        auto* crb = cam3->addComponent<RigidBodyComponent>();
        crb->motionType = EMotionType::Static;
        crb->shape      = EPhysicsShape::Capsule;
        crb->halfExtent = { 0.3f, 0.9f, 0.3f };
        settle();
        check(self->occlusionAmount() == 0.f, "청취자(카메라 액터)의 충돌체 안에서도 막힘이 아니다");
        w3.destroyActor(holder);   // 자식인 음원도 함께
        cam3->removeComponent(crb);

        wall = box("Wall", { 0.f, 0.f, -3.f }, { 2.f, 2.f, 0.1f });
        AudioComponent* flat = makeSrc(lowFile, srcPos, true);
        flat->attenuation.spatialize = false;
        meas3();
        check(flat->occlusionAmount() == 0.f, "2D 소리(Spatialize 끔)에는 차폐가 걸리지 않는다");
        w3.destroyActor(flat->owner);
        w3.destroyActor(wall);

        // 일부 차폐(반지름): 벽이 x ≤ -0.05 만 가리면 음원 반지름 0.5 m 원판 표본 9개 중 왼쪽 셋만 막힙니다.
        // (표본 x_s 의 레이는 벽 앞면 z = -2.9 를 x = 0.483·x_s 에서 지나므로 x_s ≤ -0.104, 즉 135° · 180° · 225° 셋)
        Actor* half = box("HalfWall", { -5.05f, 0.f, -3.f }, { 5.f, 2.f, 0.1f });
        AudioComponent* edge = makeSrc(lowFile, srcPos, true);
        edge->occlusion.radius = 0.5f;
        meas3();
        std::printf("    모서리 음원의 차폐 = %.4f\n", edge->occlusionAmount());
        check(near(edge->occlusionAmount(), 3.0 / 9.0, 1e-5), "모서리 뒤 음원은 일부만 막힘(3/9, 회절 근사)");
        edge->owner->setLocalPosition({ -2.f, 0.f, -6.f });
        settle();
        check(edge->occlusionAmount() == 1.f, "벽 한가운데 뒤로 옮기면 모두 막힘");
        edge->owner->setLocalPosition({ 2.f, 0.f, -6.f });
        settle();
        check(edge->occlusionAmount() == 0.f, "벽에서 먼 쪽으로 옮기면 모두 열림");
        w3.destroyActor(half);

        // 음원 바로 옆 벽(x 0.25 ~ 0.35, z -6.5 ~ -5.5): +X 쪽 표본(0° · 45° · 315°)은 청취자에게 보여도 소리가 거기까지 가지
        // 못합니다. 표본 → 음원 레이가 이것을 잡습니다(Steam Audio Volumetric 의 두 번째 레이). 0° 표본은 청취자 쪽 레이가 벽의
        // z 범위 밖을 지나므로 이 레이가 없으면 열림으로 셉니다.
        Actor* side = box("SideWall", { 0.3f, 0.f, -6.f }, { 0.05f, 2.f, 0.5f });
        edge->owner->setLocalPosition(srcPos);
        settle();
        check(near(edge->occlusionAmount(), 3.0 / 9.0, 1e-5), "음원 옆 벽 너머 표본은 막힘(표본 → 음원 레이, 3/9)");
        w3.destroyActor(side);
        w3.destroyActor(edge->owner);

        s3.shutdown();
        ps.shutdown();
    }

    // 16. 잔향(2026-11-06, Audio_Design.md 9-5): 공용 리버브 하나 + 음원별 센드 + 청취자 위치의 잔향 구역.
    std::printf("\n== 16. 잔향 ==\n");
    {
        // 순수 계산
        {
            check(near(hrev::kMinDecay, 0.605, 0.005) && near(hrev::kMaxDecay, 10.68, 0.05), "만들 수 있는 잔향 시간 0.61 ~ 10.7 s");
            bool roundTrip = true;
            for (float t : { 0.8f, 1.5f, 3.f, 7.f }) {
                const float g = 0.7f + 0.28f * hrev::roomSizeForDecay(t);
                roundTrip = roundTrip && near(hrev::decayForFeedback(g), t, 1e-3 * t);
            }
            check(roundTrip, "잔향 시간 → 방 크기 → 되먹임 → 잔향 시간이 되돌아온다(0.8 · 1.5 · 3 · 7 s)");
            check(hrev::roomSizeForDecay(0.2f) == 0.f && hrev::roomSizeForDecay(50.f) == 1.f, "범위 밖은 끝값(0 · 1)으로 자름");
            AudioAttenuationSettings a;   // Inverse min 1 max 100
            check(near(hrev::attenuationGain(a, 2.f), 0.5, 1e-6) && hrev::attenuationGain(a, 0.3f) == 1.f
                  && near(hrev::attenuationGain(a, 500.f), 0.01, 1e-6), "거리 감쇠: Inverse 2 m 0.5, min 안 1, max 밖 0.01 유지");
            a.model = EAudioAttenuation::Linear; a.maxDistance = 11.f;
            check(near(hrev::attenuationGain(a, 6.f), 0.5, 1e-6) && hrev::attenuationGain(a, 20.f) == 0.f, "Linear 절반 · max 밖 0");
            a.model = EAudioAttenuation::Exponential; a.maxDistance = 100.f;
            check(near(hrev::attenuationGain(a, 4.f), 0.25, 1e-6), "Exponential 4 m 0.25");
            check(near(hrev::sendGain(0.8f, 0.f, 0.25f), 0.8, 1e-6) && near(hrev::sendGain(0.8f, 1.f, 0.25f), 3.2, 1e-5)
                  && near(hrev::sendGain(1.f, 0.5f, 0.25f), 2.0, 1e-5), "센드 게인 = send × g^(−reach)(0 · 1 · 0.5)");
            check(hrev::sendGain(1.f, 1.f, 0.f) == hrev::kMaxSendGain && hrev::sendGain(0.f, 1.f, 0.5f) == 0.f,
                  "감쇠 0 이면 상한 10, 센드 0 이면 0");
            const glm::vec3 he{ 2.f, 1.f, 3.f };
            check(hrev::boxWeight({ 1.9f, -0.9f, 2.9f }, he, 1.f) == 1.f && near(hrev::boxWeight({ 2.5f, 0.f, 0.f }, he, 1.f), 0.5, 1e-6)
                  && hrev::boxWeight({ 3.5f, 0.f, 0.f }, he, 1.f) == 0.f && hrev::boxWeight({ 2.1f, 0.f, 0.f }, he, 0.f) == 0.f,
                  "상자 가중치: 안 1, 경계 밖 절반 거리 0.5, 그 밖 0, 전환 거리 0 이면 경계 밖 0");
            check(near(hrev::sphereWeight({ 0.f, 0.f, 5.5f }, 5.f, 1.f), 0.5, 1e-6) && hrev::sphereWeight({ 1.f, 1.f, 1.f }, 5.f, 1.f) == 1.f,
                  "구 가중치");

            World wz;
            Actor* za = wz.spawnActor("Zone");
            auto*  zc = za->addComponent<ReverbZoneComponent>();
            zc->halfExtent    = { 4.f, 1.f, 1.f };
            zc->blendDistance = 0.f;
            za->setLocalPosition({ 10.f, 0.f, 0.f });
            za->setLocalRotation(glm::angleAxis(0.5f * kPi, glm::vec3(0.f, 1.f, 0.f)));   // 로컬 X 가 월드 -Z
            check(zc->weightAt({ 10.f, 0.f, -3.5f }) == 1.f && zc->weightAt({ 13.5f, 0.f, 0.f }) == 0.f,
                  "구역은 액터 회전을 따른다(긴 축이 월드 Z 로)");
            za->setLocalScale({ 2.f, 1.f, 1.f });
            check(zc->weightAt({ 10.f, 0.f, -7.5f }) == 1.f && zc->weightAt({ 10.f, 0.f, -8.5f }) == 0.f, "상자 크기에 액터 크기를 곱한다");
            zc->unbounded = true;
            check(zc->weightAt({ 1000.f, 0.f, 0.f }) == 1.f, "unbounded 면 어디서나 1");

            ReverbZoneComponent src;
            src.shape = EReverbZoneShape::Sphere; src.halfExtent = { 1.5f, 2.5f, 3.5f }; src.radius = 7.25f; src.blendDistance = 2.f;
            src.priority = -3; src.unbounded = true; src.decayTime = 2.75f; src.damping = 0.3f; src.width = 0.6f; src.level = 1.5f;
            src.fadeTime = 1.25f;
            ComponentFieldWriter w;
            src.writeFields(w);
            ReverbZoneComponent dst;
            bool known = true;
            for (const auto& kv : w.fields()) known = known && dst.readField(kv.first, kv.second);
            check(known && dst.shape == src.shape && dst.halfExtent == src.halfExtent && dst.radius == src.radius
                  && dst.blendDistance == src.blendDistance && dst.priority == src.priority && dst.unbounded && dst.decayTime == src.decayTime
                  && dst.damping == src.damping && dst.width == src.width && dst.level == src.level && dst.fadeTime == src.fadeTime,
                  "잔향 구역의 모든 값이 이름으로 왕복");
            ReverbZoneComponent bad;
            const std::pair<const char*, const char*> badVals[] = { { "shape", "cone" }, { "damping", "1.5" }, { "level", "-1" },
                                                                    { "halfExtent", "1 0 1" }, { "fadeTime", "abc" }, { "radius", "0" } };
            bool kept = true;
            for (const auto& kv : badVals) kept = kept && bad.readField(kv.first, kv.second);
            check(kept && bad.shape == EReverbZoneShape::Box && bad.damping == 0.5f && bad.level == 1.f && bad.halfExtent == glm::vec3(5.f, 3.f, 5.f)
                  && bad.fadeTime == 0.5f && bad.radius == 5.f, "깨진 값 6종은 기본값 유지");
            AudioComponent as;
            as.reverbSend = 1.75f; as.reverbReach = 0.2f;
            ComponentFieldWriter aw;
            as.writeFields(aw);
            AudioComponent ad;
            for (const auto& kv : aw.fields()) ad.readField(kv.first, kv.second);
            AudioComponent ab;
            ab.readField("reverbSend", "3"); ab.readField("reverbReach", "-0.1");
            check(ad.reverbSend == 1.75f && ad.reverbReach == 0.2f && ab.reverbSend == 1.f && ab.reverbReach == 0.5f,
                  "소리의 센드 · reach 가 왕복하고, 범위 밖 값은 기본값 유지");
        }

        // 잔향 꼬리를 재는 짧은 잡음(10 ms 무음 뒤 20 ms, 결정적 LCG). 공간화 게인 평활(360 프레임)이 끝난 뒤에 소리가 납니다.
        const fs::path burstFile = tmp / "burst.wav";
        {
            std::vector<float> s((size_t)(0.6f * kRate), 0.f);
            uint32_t x = 12345u;
            for (size_t i = 480; i < 1440; ++i) { x = x * 1664525u + 1013904223u; s[i] = 0.5f * ((float)(x >> 8) / 8388608.f - 1.f); }
            writeWavF32(burstFile, s);
        }
        auto windowRms = [](const Block& b, double t0, double t1) {
            const size_t i0 = (size_t)(t0 * kRate), i1 = std::min(b.L.size(), (size_t)(t1 * kRate));
            double acc = 0.0;
            for (size_t i = i0; i < i1; ++i) acc += (double)b.L[i] * b.L[i] + (double)b.R[i] * b.R[i];
            return i1 > i0 ? std::sqrt(acc / (2.0 * (double)(i1 - i0))) : 0.0;
        };
        // 장면 하나: 청취자 원점, unbounded 구역, 음원 하나(반복 없음). 잔향만 남는 1.3 초를 렌더해 돌려줍니다.
        struct TailRun { Block b; bool running = false; };
        auto tailRun = [&](float decay, float damping, glm::vec3 srcAt, float send, float reach, bool spatial) {
            World       w;
            AudioSystem s(true);
            s.setWorld(&w);
            s.startup();
            w.spawnActor("Camera")->addComponent<CameraComponent>();
            s.onBeginPlay();
            auto* z = w.spawnActor("Zone")->addComponent<ReverbZoneComponent>();
            z->unbounded = true; z->decayTime = decay; z->damping = damping; z->fadeTime = 0.f;
            AudioComponent* ac = spawnSound(w, "Burst", burstFile, srcAt, false, spatial);
            ac->reverbSend = send; ac->reverbReach = reach;
            s.tick(kDt);
            TailRun r;
            r.running = s.reverbRunning();
            r.b       = render(s, (uint32_t)(1.3f * kRate));
            s.shutdown();
            return r;
        };
        auto rt60 = [&](const Block& b) {   // 0.2 s 와 1.2 s 창의 크기 차이로 잰 잔향 시간
            const double r1 = windowRms(b, 0.15, 0.25), r3 = windowRms(b, 1.15, 1.25);
            const double slope = 20.0 * std::log10(r1 / std::max(r3, 1e-20));   // dB / 1 s
            return slope > 0.0 ? 60.0 / slope : 1e9;
        };

        const TailRun t15 = tailRun(1.5f, 0.f, { 0.f, 0.f, -1.f }, 1.f, 0.5f, true);
        const TailRun t30 = tailRun(3.0f, 0.f, { 0.f, 0.f, -1.f }, 1.f, 0.5f, true);
        const double est15 = rt60(t15.b), est30 = rt60(t30.b);
        std::printf("    잰 잔향 시간: 1.5 s 설정 → %.2f s, 3.0 s 설정 → %.2f s (꼬리 0.2 s RMS %.5f)\n", est15, est30,
                    windowRms(t15.b, 0.15, 0.25));
        check(t15.running && windowRms(t15.b, 0.15, 0.25) > 1e-4, "구역 안에서는 리버브가 돌고, 소리가 끝난 뒤에도 꼬리가 남는다");
        check(near(est15, 1.5, 0.375) && near(est30, 3.0, 0.75), "꼬리의 감쇠로 잰 잔향 시간이 설정과 맞다(±25%)");
        const TailRun damped = tailRun(1.5f, 1.f, { 0.f, 0.f, -1.f }, 1.f, 0.5f, true);
        check(windowRms(damped.b, 0.45, 0.55) < 0.8 * windowRms(t15.b, 0.45, 0.55), "damping 을 높이면 꼬리가 더 빨리 줄어든다");

        // 거리 규칙(reach): 1 m 와 4 m(Inverse, 감쇠 1 · 0.25)의 꼬리 크기 비율
        const double near0 = windowRms(tailRun(1.5f, 0.f, { 0.f, 0.f, -1.f }, 1.f, 0.f, true).b, 0.15, 0.25);
        const double far0  = windowRms(tailRun(1.5f, 0.f, { 0.f, 0.f, -4.f }, 1.f, 0.f, true).b, 0.15, 0.25);
        const double near1 = windowRms(tailRun(1.5f, 0.f, { 0.f, 0.f, -1.f }, 1.f, 1.f, true).b, 0.15, 0.25);
        const double far1  = windowRms(tailRun(1.5f, 0.f, { 0.f, 0.f, -4.f }, 1.f, 1.f, true).b, 0.15, 0.25);
        std::printf("    4 m / 1 m 꼬리 비율: reach 0 → %.3f, reach 1 → %.3f\n", far0 / near0, far1 / near1);
        check(near(far0 / near0, 0.25, 0.025), "reach 0 이면 잔향도 직접음처럼 거리에 따라 준다(4 m 에서 0.25)");
        check(near(far1 / near1, 1.0, 0.1), "reach 1 이면 잔향 크기가 거리와 무관하다");
        const TailRun flat2d = tailRun(1.5f, 0.f, { 0.f, 0.f, -1.f }, 1.f, 0.5f, false);
        const TailRun zeroSend = tailRun(1.5f, 0.f, { 0.f, 0.f, -1.f }, 0.f, 0.5f, true);
        check(peak(Block{ std::vector<float>(flat2d.b.L.begin() + (size_t)(0.1f * kRate), flat2d.b.L.end()),
                          std::vector<float>(flat2d.b.R.begin() + (size_t)(0.1f * kRate), flat2d.b.R.end()) }) == 0.f,
              "2D 소리는 잔향으로 보내지 않는다(소리가 끝난 뒤 정확히 0)");
        check(peak(Block{ std::vector<float>(zeroSend.b.L.begin() + (size_t)(0.1f * kRate), zeroSend.b.L.end()),
                          std::vector<float>(zeroSend.b.R.begin() + (size_t)(0.1f * kRate), zeroSend.b.R.end()) }) == 0.f,
              "센드 0 이면 꼬리가 정확히 0");

        // 일시정지 · Stop · 구역 전환은 한 장면에서 이어서 봅니다.
        World       w5;
        AudioSystem s5(true);
        s5.setWorld(&w5);
        s5.startup();
        Actor* cam5 = w5.spawnActor("Camera");
        cam5->addComponent<CameraComponent>();
        s5.onBeginPlay();
        auto* room = w5.spawnActor("Room")->addComponent<ReverbZoneComponent>();
        room->halfExtent = { 3.f, 3.f, 3.f }; room->blendDistance = 0.f; room->decayTime = 2.f; room->fadeTime = 0.f;
        AudioComponent* b5 = spawnSound(w5, "Burst", burstFile, { 0.f, 0.f, -1.f }, false, true);
        s5.tick(kDt);
        render(s5, (uint32_t)(0.3f * kRate));
        const Block beforePause = render(s5, 2400);
        s5.onPauseChanged(true);
        check(peak(render(s5, 4800)) == 0.f, "일시정지 중에는 잔향 꼬리도 무음(리버브가 월드 그룹 안)");
        s5.onPauseChanged(false);
        const Block afterPause = render(s5, 2400);
        const double contin = (rms(afterPause.L) + rms(afterPause.R)) / std::max(1e-20, rms(beforePause.L) + rms(beforePause.R));
        check(contin > 0.7 && contin < 1.1, "재개하면 꼬리가 멈춘 자리에서 이어진다(비율 " + std::to_string(contin).substr(0, 5) + ")");
        s5.onEndPlay();
        check(!s5.reverbRunning() && peak(render(s5, 4800)) == 0.f, "Stop(onEndPlay) 하면 리버브가 멈추고 꼬리가 남지 않는다");
        s5.onBeginPlay();
        s5.tick(kDt);
        check(s5.reverbRunning() && peak(render(s5, 4800)) == 0.f, "다음 Play 에서 리버브가 다시 돌아도 지난 꼬리는 비워져 있다");
        w5.destroyActor(b5->owner);

        // 구역 고르기: 우선순위 → 깊이(가중치) → 액터 ID
        auto* big = w5.spawnActor("Big")->addComponent<ReverbZoneComponent>();
        big->halfExtent = { 10.f, 10.f, 10.f }; big->priority = 1; big->decayTime = 4.f; big->level = 0.8f; big->fadeTime = 0.f;
        s5.tick(kDt);
        check(near(s5.reverbParams().decayTime, 4.0, 1e-4) && near(s5.reverbParams().level, 0.8, 1e-6), "겹치면 우선순위가 높은 구역");
        big->setEnabled(false);
        s5.tick(kDt);
        check(near(s5.reverbParams().decayTime, 2.0, 1e-4) && near(s5.reverbParams().level, 1.0, 1e-6), "그 구역을 끄면 남은 구역");
        auto* edgeZone = w5.spawnActor("Edge")->addComponent<ReverbZoneComponent>();
        edgeZone->owner->setLocalPosition({ 5.f, 0.f, 0.f });
        edgeZone->halfExtent = { 4.f, 3.f, 3.f }; edgeZone->blendDistance = 2.f; edgeZone->decayTime = 6.f; edgeZone->fadeTime = 0.f;
        // 원점: room 안(가중치 1), edge 는 경계(x = 1) 밖 1 m · 전환 2 m 라 가중치 0.5. 같은 우선순위면 더 깊이 들어간 room
        s5.tick(kDt);
        check(near(s5.reverbParams().decayTime, 2.0, 1e-4), "같은 우선순위면 더 깊이 들어간(가중치가 큰) 구역");
        auto* twin = w5.spawnActor("Twin")->addComponent<ReverbZoneComponent>();   // room 과 같은 자리 · 크기, ID 만 큼
        twin->halfExtent = { 3.f, 3.f, 3.f }; twin->blendDistance = 0.f; twin->decayTime = 9.f; twin->fadeTime = 0.f;
        s5.tick(kDt);
        check(near(s5.reverbParams().decayTime, 2.0, 1e-4), "우선순위 · 가중치가 같으면 액터 ID 가 작은 구역(순서가 정해짐)");
        w5.destroyActor(twin->owner);
        cam5->setLocalPosition({ 4.f, 0.f, 0.f });   // room 밖 1 m(전환 0 → 0), edge 안
        s5.tick(kDt);
        check(near(s5.reverbParams().decayTime, 6.0, 1e-4) && near(s5.reverbParams().level, 1.0, 1e-6), "청취자가 있는 구역으로 바뀜");
        cam5->setLocalPosition({ 10.f, 0.f, 0.f });  // edge 경계(x = 9) 밖 1 m, 전환 거리 2 m → 가중치 0.5
        s5.tick(kDt);
        check(near(s5.reverbParams().level, 0.5, 1e-5), "경계 밖 전환 거리의 절반이면 크기 절반");

        // 구역을 벗어나면 마지막 구역의 fadeTime 으로 서서히 줄다가 0 이 되면 리버브를 멈춥니다.
        edgeZone->fadeTime = 0.2f;
        s5.tick(kDt);   // 아직 edge 안: 전환 시간 0.2 s 를 받아 둠
        cam5->setLocalPosition({ 30.f, 0.f, 0.f });
        bool decreasing = true, stopped = false;
        float prevLevel = s5.reverbParams().level;
        int stopTick = -1;
        for (int i = 1; i <= 180 && !stopped; ++i) {
            s5.tick(kDt);
            render(s5, 800);
            const float l = s5.reverbParams().level;
            decreasing = decreasing && l <= prevLevel;
            prevLevel = l;
            stopped = !s5.reverbRunning();
            if (stopped) stopTick = i;
        }
        std::printf("    구역을 벗어난 뒤 %d 틱(%.2f s)에 리버브 멈춤\n", stopTick, stopTick * kDt);
        check(decreasing && stopped && stopTick > 10, "구역 밖으로 나가면 잔향이 서서히 줄고(fadeTime 0.2 s) 다 줄면 리버브가 멈춘다");
        s5.shutdown();
    }

    // 17. HRTF(2026-11-06, Audio_Design.md 9-6): HRTF 가 켜지면 공간화 소리마다 HRTF 노드를 사슬에 넣고 miniaudio 의 패닝을 끕니다.
    // 노드의 FIFO · 꼬리 · 일시정지 · 되감기 · 켜고 끄기는 결과가 정해진 가짜 구현으로 샘플 값까지 보고, 실제 Steam Audio 는 방향
    // 단서(귀 사이 시간차 · 크기 차 · 앞뒤 스펙트럼)와 크기 맞춤을 봅니다. 거리 감쇠는 끄고(rolloff 0) HRTF 만 봅니다.
    std::printf("\n== 17. HRTF ==\n");
    {
        constexpr uint32_t F = hhrtf::kFrameSize;
        const double flat = 0.5 / std::sqrt(2.0);   // 공간화 없는 진폭 0.5 사인의 채널 RMS
        auto sameDir = [](glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-5f; };

        // 17-1. 청취자 기준 방향 · 공간화 정도 · 모드 이름
        {
            const glm::vec3 fwd(0.f, 0.f, -1.f), up(0.f, 1.f, 0.f);
            check(sameDir(hhrtf::listenerLocalDirection({ 2.f, 0.f, 0.f }, fwd, up), { 1.f, 0.f, 0.f }) &&
                  sameDir(hhrtf::listenerLocalDirection({ -3.f, 0.f, 0.f }, fwd, up), { -1.f, 0.f, 0.f }) &&
                  sameDir(hhrtf::listenerLocalDirection({ 0.f, 0.f, -5.f }, fwd, up), { 0.f, 0.f, -1.f }) &&
                  sameDir(hhrtf::listenerLocalDirection({ 0.f, 0.f, 5.f }, fwd, up), { 0.f, 0.f, 1.f }) &&
                  sameDir(hhrtf::listenerLocalDirection({ 0.f, 4.f, 0.f }, fwd, up), { 0.f, 1.f, 0.f }),
                  "정면 -Z 청취자: 오른쪽 +X · 왼쪽 -X · 앞 -Z · 뒤 +Z · 위 +Y(청취자 → 음원, Steam Audio 규약)");
            check(sameDir(hhrtf::listenerLocalDirection({ 0.f, 0.f, -3.f }, { 1.f, 0.f, 0.f }, up), { -1.f, 0.f, 0.f }),
                  "+X 를 보면 월드 -Z 의 음원은 왼쪽");
            check(sameDir(hhrtf::listenerLocalDirection({ 0.f, 3.f, 0.f }, fwd, { 1.f, 0.f, 0.f }), { -1.f, 0.f, 0.f }),
                  "머리를 오른쪽으로 기울이면(위 = +X) 위쪽 음원이 왼쪽");
            check(sameDir(hhrtf::listenerLocalDirection({ 0.f, 0.f, 0.f }, fwd, up), { 0.f, 0.f, -1.f }) &&
                  sameDir(hhrtf::listenerLocalDirection({ 1.f, 0.f, 0.f }, fwd, fwd), { 1.f, 0.f, 0.f }),
                  "길이 0 이면 정면, 위 벡터가 정면과 나란해도 단위 방향");
            check(hhrtf::spatialBlend(0.f) == 0.f && near(hhrtf::spatialBlend(0.05f), 0.5, 1e-6) && hhrtf::spatialBlend(2.f) == 1.f,
                  "0.1 m 안에서만 방향 단서를 줄인다(0 m 에서 0)");
            EHrtfMode m{};
            const bool names = AudioSystem::hrtfModeFromText("on", m) && m == EHrtfMode::On &&
                               AudioSystem::hrtfModeFromText("auto", m) && m == EHrtfMode::Auto &&
                               AudioSystem::hrtfModeFromText("off", m) && m == EHrtfMode::Off &&
                               !AudioSystem::hrtfModeFromText("On", m) && m == EHrtfMode::Off &&
                               std::string(AudioSystem::hrtfModeName(EHrtfMode::Auto)) == "auto" &&
                               std::string(AudioSystem::hrtfModeName(EHrtfMode::On)) == "on" &&
                               std::string(AudioSystem::hrtfModeName(EHrtfMode::Off)) == "off";
            check(names, "모드 이름(auto · on · off)이 왕복하고, 모르는 값은 거부하며 out 을 건드리지 않는다");
        }

        // 가짜 구현: 왼쪽 = 입력 그대로, 오른쪽 = 한 블록 전 입력(꼬리가 정확히 한 블록인 필터). 방향 · 공간화 정도 · 호출 수를 기록합니다.
        struct FakeStats { int resets = 0; int blocks = 0; int tails = 0; glm::vec3 dir{ 0.f }; float blend = -1.f; };
        struct FakeRenderer final : hhrtf::Renderer {
            FakeStats& s;
            float      hist[hhrtf::kFrameSize] = {};
            explicit FakeRenderer(FakeStats& st) : s(st) {}
            void reset() override { std::fill(std::begin(hist), std::end(hist), 0.f); ++s.resets; }
            bool process(const float* mono, float* L, float* R, const glm::vec3& dir, float blend) override {
                bool any = false;
                for (uint32_t i = 0; i < hhrtf::kFrameSize; ++i) {
                    L[i] = mono[i]; R[i] = hist[i]; hist[i] = mono[i];
                    any = any || mono[i] != 0.f;
                }
                s.dir = dir; s.blend = blend; ++s.blocks;
                return any;   // 이번 입력이 다음 블록의 오른쪽으로 나옴
            }
            bool tail(float* L, float* R) override {   // 무음 블록: 왼쪽 0, 오른쪽은 남은 한 블록. 그 뒤로는 꼬리가 없음
                for (uint32_t i = 0; i < hhrtf::kFrameSize; ++i) { L[i] = 0.f; R[i] = hist[i]; hist[i] = 0.f; }
                ++s.tails;
                return false;
            }
        };
        struct FakeBackend final : hhrtf::Backend {
            FakeStats& s;
            explicit FakeBackend(FakeStats& st) : s(st) {}
            std::string name() const override { return "fake"; }
            std::unique_ptr<hhrtf::Renderer> createRenderer() override { return std::make_unique<FakeRenderer>(s); }
            float outputGain() const override { return 1.f; }
        };
        // 장면 하나(청취자 원점 · 정면 -Z). fake 면 가짜 구현, 아니면 실행 폴더의 Steam Audio 를 엽니다. 둘 다 모드 On.
        struct HrtfScene { FakeStats st; World w; AudioSystem s{ true }; Actor* cam = nullptr; };
        auto makeScene = [](bool fake) {
            auto sc = std::make_unique<HrtfScene>();
            sc->s.setWorld(&sc->w);
            sc->s.startup();
            sc->cam = sc->w.spawnActor("Camera");
            sc->cam->addComponent<CameraComponent>();
            sc->s.onBeginPlay();
            if (fake) sc->s.setHrtfBackend(std::make_unique<FakeBackend>(sc->st));
            sc->s.setHrtfMode(EHrtfMode::On);
            return sc;
        };
        auto silentRange = [](const std::vector<float>& v, size_t i0, size_t i1) {
            for (size_t i = i0; i < i1 && i < v.size(); ++i)
                if (v[i] != 0.f) return false;
            return true;
        };
        auto rmsRange = [](const std::vector<float>& v, size_t i0, size_t i1) {
            return rms(std::vector<float>(v.begin() + (ptrdiff_t)i0, v.begin() + (ptrdiff_t)std::min(i1, v.size())));
        };

        // 17-2. 지연 · 방향 · 공간화 정도(가짜 구현)
        {
            auto sc = makeScene(true);
            AudioSystem& s = sc->s;
            check(s.hrtfActive() && s.hrtfAvailable() && s.hrtfStatus() == "on (fake)", "구현을 넣고 On 이면 HRTF 가 켜진다");
            AudioComponent* rp = spawnSound(sc->w, "Ramp", rampFile, { 0.f, 0.f, -2.f }, false, true);
            rp->attenuation.rolloff = 0.f;
            AudioComponent* twoD = spawnSound(sc->w, "Flat", toneFile, { 0.f, 0.f, 0.f }, true, false);
            twoD->volume = 0.f;   // 사슬만 보고 출력에는 섞이지 않게
            s.tick(kDt);
            check(s.voiceHrtfRouted(rp->voice_) && !s.voiceHrtfRouted(twoD->voice_), "공간화 소리만 HRTF 노드를 거친다(2D 는 그대로)");
            const Block b = render(s, 3 * F);
            check(silentRange(b.L, 0, F) && near(b.L[F + 100] / kStep, 100.0, 2.0),
                  "직접음이 정확히 한 블록(" + std::to_string(F) + " 프레임) 늦다(FIFO, 램프 값 " + std::to_string(b.L[F + 100] / kStep) + ")");
            bool lagged = true;
            for (uint32_t k = 2 * F; k < 3 * F; ++k) lagged = lagged && b.R[k] == b.L[k - F];
            check(silentRange(b.R, 0, 2 * F) && lagged, "가짜 구현의 오른쪽(한 블록 꼬리)이 왼쪽보다 정확히 한 블록 뒤(블록 경계가 맞음)");
            check(sc->st.blocks == 3 && sc->st.resets == 0, "3블록 분량을 렌더하면 정확히 세 번 처리한다");
            check(sameDir(sc->st.dir, { 0.f, 0.f, -1.f }) && sc->st.blend == 1.f, "정면 음원의 방향(0, 0, -1)과 공간화 정도 1 이 넘어간다");
            rp->owner->setLocalPosition({ 2.f, 0.f, 0.f });
            s.tick(kDt);
            render(s, F);
            check(sameDir(sc->st.dir, { 1.f, 0.f, 0.f }), "음원을 오른쪽으로 옮기면 다음 블록부터 오른쪽 방향");
            sc->cam->setLocalRotation(CameraComponent::yawPitchRotation(kPi, 0.f));   // +Z 를 봄
            s.tick(kDt);
            render(s, F);
            check(sameDir(sc->st.dir, { -1.f, 0.f, 0.f }), "뒤로 돌면 같은 음원이 왼쪽 방향");
            sc->cam->setLocalRotation(CameraComponent::yawPitchRotation(0.f, 0.f));
            rp->owner->setLocalPosition({ 0.f, 0.f, -0.05f });
            s.tick(kDt);
            render(s, F);
            check(near(sc->st.blend, 0.5, 1e-5), "청취자에게 5 cm 면 공간화 정도 0.5(머리 한가운데로 모음)");
            s.shutdown();
        }

        // 17-3. 부르는 블록 크기와 무관한 출력: 같은 장면을 한 번에, 또 들쭉날쭉한 크기로 나눠 렌더해 바이트를 비교합니다.
        auto renderChunks = [&](bool fake, const std::vector<uint32_t>& chunks) {
            auto sc = makeScene(fake);
            AudioComponent* t = spawnSound(sc->w, "Tone", toneFile, { 1.f, 0.5f, -2.f }, true, true);
            t->attenuation.rolloff = 0.f;   // 거리 게인 평활은 부르는 크기마다 다시 시작해서 블록 크기에 따라 달라집니다
            sc->s.tick(kDt);
            std::vector<float> all;
            for (uint32_t c : chunks) {
                std::vector<float> buf((size_t)c * 2, 0.f);
                sc->s.renderHeadless(buf.data(), c);
                all.insert(all.end(), buf.begin(), buf.end());
            }
            sc->s.shutdown();
            return all;
        };
        const std::vector<uint32_t> oneChunk{ 4800 }, oddChunks{ 77, 1500, 13, 480, 255, 257, 2218 };
        {
            const std::vector<float> a = renderChunks(true, oneChunk), b = renderChunks(true, oddChunks);
            check(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0 && rms(a) > 0.1,
                  "부르는 블록 크기가 달라도(77 · 1500 · 13 · 480 · 255 · 257 · 2218) 출력이 바이트 단위로 같다");
        }

        // 17-4. 소리가 끝난 뒤의 꼬리(가짜 구현): 노드를 건너뛰면 FIFO 와 꼬리가 소리 끝에서 잘려 튑니다.
        {
            auto sc = makeScene(true);
            AudioComponent* sh = spawnSound(sc->w, "Short", shortFile, { 0.f, 0.f, -2.f }, false, true);
            sh->attenuation.rolloff = 0.f;
            sc->s.tick(kDt);
            const Block b = render(sc->s, 9600);
            const size_t end = (size_t)(0.1f * kRate);   // 소리가 끝나는 프레임(리샘플러 한 프레임 안팎)
            check(rmsRange(b.L, end + 8, end + F - 8) > 0.3 && rmsRange(b.R, end + F + 8, end + 2 * F - 8) > 0.3,
                  "소리가 끝난 뒤에도 FIFO(왼쪽)와 필터 꼬리(오른쪽 한 블록)를 끝까지 낸다");
            check(silentRange(b.L, end + F + 8, b.L.size()) && silentRange(b.R, end + 2 * F + 8, b.R.size()), "꼬리를 다 낸 뒤에는 정확히 0");
            const uint64_t n1 = sc->s.voiceHrtfBlocks(sh->voice_);
            render(sc->s, 4800);
            check(n1 > 0 && sc->st.tails >= 1 && sc->s.voiceHrtfBlocks(sh->voice_) == n1,
                  "무음 블록은 꼬리 함수로 꺼내고, 꼬리가 끝나면 노드가 구현을 더 부르지 않는다(멈춘 소리는 HRTF 계산 0)");
            sc->s.shutdown();
        }

        // 17-4 (이어서). 블록 중간에서 끝난 소리: 렌더 경계가 어디에 떨어져도 노드가 남은 출력을 다 내기 전에 쉬면 안 됩니다. 소리(0.5 상수
        // 100 프레임)가 셋째 블록의 40 번째 프레임부터라 그 블록을 처리하는 3F 부터 왼쪽이, 다음 블록을 처리하는 4F 부터 오른쪽(가짜 구현의
        // 한 블록 꼬리)이 나옵니다. 둘째 렌더(8 프레임)는 들어온 소리가 없고 소리가 FIFO 입력에만 남은 때에, 셋째 렌더(F+2)는 처리를 마쳐
        // FIFO 출력은 비었고 구현의 꼬리만 남은 때에 끝납니다.
        {
            const fs::path edgeFile = tmp / "edge.wav";
            {
                std::vector<float> s(2 * F + 140, 0.f);
                std::fill(s.begin() + 2 * F + 40, s.end(), 0.5f);
                writeWavF32(edgeFile, s);
            }
            auto renderEdge = [&](const std::vector<uint32_t>& chunks) {
                auto sc = makeScene(true);
                AudioComponent* e = spawnSound(sc->w, "Edge", edgeFile, { 0.f, 0.f, -2.f }, false, true);
                e->attenuation.rolloff = 0.f;
                sc->s.tick(kDt);
                Block all;
                for (uint32_t c : chunks) {
                    const Block b = render(sc->s, c);
                    all.L.insert(all.L.end(), b.L.begin(), b.L.end());
                    all.R.insert(all.R.end(), b.R.begin(), b.R.end());
                }
                sc->s.shutdown();
                return all;
            };
            const std::vector<uint32_t> edgeChunks{ 2 * F + 150, 8, F + 2, 4800 };
            const Block a = renderEdge({ 3 * F + 160 + 4800 }), b = renderEdge(edgeChunks);
            check(a.L.size() == b.L.size() && std::memcmp(a.L.data(), b.L.data(), a.L.size() * sizeof(float)) == 0 &&
                  std::memcmp(a.R.data(), b.R.data(), a.R.size() * sizeof(float)) == 0,
                  "블록 중간에서 끝난 소리도 렌더 경계(소리 뒤 10 · 8 · F+2 프레임)와 무관하게 바이트 단위로 같다");
            check(silentRange(b.L, 0, 3 * F + 30) && rmsRange(b.L, 3 * F + 48, 3 * F + 130) > 0.45 &&
                  silentRange(b.R, 0, 4 * F + 30) && rmsRange(b.R, 4 * F + 48, 4 * F + 130) > 0.45 &&
                  silentRange(b.L, 3 * F + 150, b.L.size()) && silentRange(b.R, 4 * F + 150, b.R.size()),
                  "그 렌더에서도 왼쪽은 3F, 오른쪽은 4F 부터 소리가 나오고(남은 입력 · 꼬리를 잃지 않음) 그 뒤는 정확히 0 (왼 RMS " +
                      std::to_string(rmsRange(b.L, 3 * F + 48, 3 * F + 130)) + ", 오른 RMS " +
                      std::to_string(rmsRange(b.R, 4 * F + 48, 4 * F + 130)) + ")");
        }

        // 17-5. 일시정지 · Stop · 모드 바꾸기 · 구현 빼기(가짜 구현)
        {
            auto sc = makeScene(true);
            AudioSystem& s = sc->s;
            AudioComponent* lp = spawnSound(sc->w, "Loop", toneFile, { 0.f, 0.f, -2.f }, true, true);
            lp->attenuation.rolloff = 0.f;
            s.tick(kDt);
            render(s, 4800);
            const uint64_t before = s.voiceHrtfBlocks(lp->voice_);
            s.onPauseChanged(true);
            check(peak(render(s, 4800)) == 0.f && s.voiceHrtfBlocks(lp->voice_) == before, "일시정지 중에는 무음이고 HRTF 노드도 처리하지 않는다");
            s.onPauseChanged(false);
            const Block resumed = render(s, 4800);
            check(rmsRange(resumed.L, 0, F) > 0.3, "재개하면 FIFO 에 있던 소리부터 이어진다(첫 블록이 무음이 아님)");

            lp->stop();
            lp->play();
            const Block re = render(s, 2 * F);
            check(silentRange(re.L, 0, F) && rmsRange(re.L, F, 2 * F) > 0.3 && sc->st.resets == 1,
                  "Stop 뒤 Play 는 FIFO · 필터를 비우고 처음부터(첫 블록 무음, 지난 소리가 남지 않음)");

            s.setHrtfMode(EHrtfMode::Off);
            lp->owner->setLocalPosition({ 2.f, 0.f, 0.f });
            const Level off = measure(s);
            check(!s.hrtfActive() && !s.voiceHrtfRouted(lp->voice_) && near(off.L / flat, 0.2, 0.01) && near(off.R / flat, 1.0, 0.01),
                  "Off 면 노드를 사슬에서 빼고 패닝으로 돌아간다(오른쪽 음원 0.2 / 1)");
            const uint64_t idle = s.voiceHrtfBlocks(lp->voice_);
            render(s, 4800);
            check(s.voiceHrtfBlocks(lp->voice_) == idle, "사슬에서 뺀 노드는 남은 꼬리를 낸 뒤 처리를 멈춘다");
            s.setHrtfMode(EHrtfMode::On);
            const Level on = measure(s);
            check(s.voiceHrtfRouted(lp->voice_) && near(on.L / flat, 1.0, 0.01) && near(on.R / flat, 1.0, 0.01),
                  "다시 On 이면 노드로 돌아가고 패닝 계수 0(채널 게인 1, 가짜 구현은 좌우 모두 원래 크기)");
            lp->attenuation.spatialize = false;
            s.tick(kDt);
            check(!s.voiceHrtfRouted(lp->voice_), "Spatialize 를 끄면 HRTF 를 거치지 않는다");
            lp->attenuation.spatialize = true;

            s.setHrtfMode(EHrtfMode::Auto);
            const bool autoOff = !s.hrtfActive();
            s.setXrActive(true);
            const bool xrOn = s.hrtfActive();
            s.setXrActive(false);
            check(autoOff && xrOn && !s.hrtfActive() && s.hrtfStatus() == "off", "Auto 는 XR 세션 동안만 켜진다");

            s.setHrtfMode(EHrtfMode::On);
            s.tick(kDt);
            const bool routedBefore = s.voiceHrtfRouted(lp->voice_);
            s.setHrtfBackend(nullptr);
            const Level none = measure(s);
            check(routedBefore && !s.hrtfActive() && !s.hrtfAvailable() && !s.voiceHrtfRouted(lp->voice_) &&
                  near(none.L / flat, 0.2, 0.01) && near(none.R / flat, 1.0, 0.01),
                  "구현을 빼면 HRTF 노드를 지우고 패닝으로 돌아간다(구현이 없을 때와 같음)");
            s.shutdown();
        }

#if defined(HARUKA_EXPECT_STEAM_AUDIO)
        // 17-6. Steam Audio(실행 폴더의 phonon.dll): 방향 단서와 크기 맞춤. 백색 잡음(결정적 LCG, ±0.5, 1 초 반복)을 2 m 에 둡니다.
        {
            const fs::path noiseFile = tmp / "noise.wav";
            {
                std::vector<float> n(kRate);
                uint32_t x = 777u;
                for (float& v : n) { x = x * 1664525u + 1013904223u; v = 0.5f * ((float)(x >> 8) / 8388608.f - 1.f); }
                writeWavF32(noiseFile, n);
            }
            auto sc = makeScene(false);
            AudioSystem& s = sc->s;
            check(s.hrtfActive() && s.hrtfStatus() == "on (Steam Audio 4.8.1)", "실행 폴더의 Steam Audio 를 연다: " + s.hrtfStatus());
            AudioComponent* nz = spawnSound(sc->w, "Noise", noiseFile, { 0.f, 0.f, -2.f }, true, true);
            nz->attenuation.rolloff = 0.f;
            auto capture = [&](glm::vec3 p) {
                nz->owner->setLocalPosition(p);
                s.tick(kDt);
                render(s, 4800);   // 보간 · FIFO 가 새 방향에 자리 잡을 때까지
                return render(s, 24000);
            };
            const Block front = capture({ 0.f, 0.f, -2.f }), back = capture({ 0.f, 0.f, 2.f });
            const Block right = capture({ 2.f, 0.f, 0.f }),  left = capture({ -2.f, 0.f, 0.f });
            const Block above = capture({ 0.f, 2.f, 0.f }),  below = capture({ 0.f, -2.f, 0.f });
            const double ref = 0.5 * 0.5 / std::sqrt(3.0);   // 패닝일 때 정면 음원의 채널 RMS(균등 잡음 RMS × 0.5)
            auto dB = [](double a, double b) { return 20.0 * std::log10(a / b); };
            std::printf("    채널 크기(패닝 정면 대비 dB, 왼/오): 정면 %+.2f/%+.2f, 뒤 %+.2f/%+.2f, 오른쪽 %+.2f/%+.2f, 왼쪽 %+.2f/%+.2f, "
                        "위 %+.2f/%+.2f, 아래 %+.2f/%+.2f\n",
                        dB(rms(front.L), ref), dB(rms(front.R), ref), dB(rms(back.L), ref), dB(rms(back.R), ref),
                        dB(rms(right.L), ref), dB(rms(right.R), ref), dB(rms(left.L), ref), dB(rms(left.R), ref),
                        dB(rms(above.L), ref), dB(rms(above.R), ref), dB(rms(below.L), ref), dB(rms(below.R), ref));
            // 기본 HRTF 는 좌우 대칭이 아니라(정면에서 왼쪽이 약 2 dB 작음) 두 귀의 거듭제곱 평균을 패닝 크기에 맞췄습니다.
            const double frontPow = std::sqrt(0.5 * (rms(front.L) * rms(front.L) + rms(front.R) * rms(front.R)));
            check(std::fabs(dB(frontPow, ref)) < 0.5 && std::fabs(dB(rms(front.L), ref)) < 1.5 && std::fabs(dB(rms(front.R), ref)) < 1.5,
                  "정면 음원 크기가 패닝(채널마다 0.5)과 같다(두 귀 평균 ±0.5 dB, 귀마다 ±1.5 dB, 출력 게인 맞춤)");
            check(dB(rms(right.R), rms(right.L)) > 6.0 && dB(rms(left.L), rms(left.R)) > 6.0,
                  "옆 음원은 가까운 귀가 6 dB 넘게 크다(귀 사이 크기 차, 좌우 부호가 맞음)");
            // 귀 사이 시간차: L[n] ≈ R[n - lag] 를 가장 잘 맞추는 lag(±1 ms). 오른쪽 음원이면 오른쪽 귀가 먼저라 lag > 0 입니다.
            auto itd = [](const Block& b) {
                int best = 0;
                double bestC = -1e300;
                for (int lag = -48; lag <= 48; ++lag) {
                    double c = 0.0;
                    for (size_t n = 64; n + 64 < b.L.size(); ++n) c += (double)b.L[n] * b.R[(size_t)((ptrdiff_t)n - lag)];
                    if (c > bestC) { bestC = c; best = lag; }
                }
                return best;
            };
            const int itdF = itd(front), itdR = itd(right), itdL = itd(left);
            std::printf("    귀 사이 시간차(프레임, 48 kHz): 정면 %d, 오른쪽 %d, 왼쪽 %d\n", itdF, itdR, itdL);
            check(itdR >= 15 && itdR <= 45 && itdL <= -15 && itdL >= -45 && std::abs(itdF) <= 2,
                  "귀 사이 시간차: 오른쪽 음원은 오른쪽 귀가 0.3 ~ 0.9 ms 먼저, 왼쪽은 반대, 정면은 0");
            // 앞뒤 · 위아래: 좌우 단서가 같아도 귓바퀴가 만드는 스펙트럼이 다릅니다. 고역 비율(1차 차분 에너지 / 에너지)로 봅니다.
            auto hf = [](const Block& b) {
                double e = 0.0, d = 0.0;
                for (size_t n = 1; n < b.L.size(); ++n) {
                    e += (double)b.L[n] * b.L[n] + (double)b.R[n] * b.R[n];
                    const double dl = (double)b.L[n] - b.L[n - 1], dr = (double)b.R[n] - b.R[n - 1];
                    d += dl * dl + dr * dr;
                }
                return d / e;
            };
            const double hfF = hf(front), hfB = hf(back), hfU = hf(above), hfD = hf(below);
            std::printf("    고역 비율: 정면 %.3f, 뒤 %.3f, 위 %.3f, 아래 %.3f (백색 잡음 원본 2.0)\n", hfF, hfB, hfU, hfD);
            check(std::fabs(hfF - hfB) > 0.1 * std::max(hfF, hfB) && std::fabs(hfU - hfD) > 0.1 * std::max(hfU, hfD),
                  "앞 · 뒤, 위 · 아래 음원의 스펙트럼이 10% 넘게 다르다(패닝은 같았음, DEF-166)");

            // 소리가 끝나면 Steam Audio 가 꼬리가 끝났다고 알려 노드가 멈춥니다.
            nz->stop();
            AudioComponent* sh = spawnSound(sc->w, "Short", shortFile, { 1.f, 0.f, -2.f }, false, true);
            sh->attenuation.rolloff = 0.f;
            s.tick(kDt);
            render(s, (uint32_t)(0.5f * kRate));
            const uint64_t n1 = s.voiceHrtfBlocks(sh->voice_), m1 = s.voiceHrtfBlocks(nz->voice_);
            const Block later = render(s, 9600);
            std::printf("    소리가 끝난 뒤 처리 블록: 짧은 소리 %llu → %llu, 멈춘 잡음 %llu → %llu, 출력 최댓값 %g\n",
                        (unsigned long long)n1, (unsigned long long)s.voiceHrtfBlocks(sh->voice_), (unsigned long long)m1,
                        (unsigned long long)s.voiceHrtfBlocks(nz->voice_), (double)peak(later));
            check(n1 > 0 && s.voiceHrtfBlocks(sh->voice_) == n1 && s.voiceHrtfBlocks(nz->voice_) == m1 && peak(later) == 0.f,
                  "소리가 끝나거나 멈추면 Steam Audio 꼬리까지 낸 뒤 처리를 멈춘다");
            s.shutdown();
        }
        {
            const std::vector<float> a = renderChunks(false, oneChunk), b = renderChunks(false, oddChunks);
            check(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0 && rms(a) > 0.05,
                  "Steam Audio 도 부르는 블록 크기와 무관하게 바이트 단위로 같은 출력");
        }
        // 비용(이 PC 의 한 코어, 참고값): 방향을 계속 바꾸며 블록 2000개를 처리합니다. Quest 는 실기에서 잽니다.
        {
            std::string err;
            std::unique_ptr<hhrtf::Backend>  be = hhrtf::loadSteamAudio(err);
            std::unique_ptr<hhrtf::Renderer> r  = be ? be->createRenderer() : nullptr;
            check(r != nullptr, "Steam Audio 구현을 따로 만들어 블록을 처리할 수 있다" + (err.empty() ? std::string() : ": " + err));
            if (r) {
                std::vector<float> in(F), L(F), R(F);
                uint32_t x = 99u;
                for (float& v : in) { x = x * 1664525u + 1013904223u; v = 0.5f * ((float)(x >> 8) / 8388608.f - 1.f); }
                constexpr int kBlocks = 2000;
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < kBlocks; ++i) {
                    const float a = 0.01f * (float)i;
                    r->process(in.data(), L.data(), R.data(), { std::sin(a), 0.f, -std::cos(a) }, 1.f);
                }
                const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / kBlocks;
                std::printf("    Steam Audio 블록(%u 프레임, 쌍선형) 처리 %.1f us = 실시간의 %.2f%% (소리 하나, 이 PC 한 코어)\n",
                            F, us, 100.0 * us / ((double)F * 1e6 / kRate));
            }
        }
#else
        std::printf("    (Steam Audio 없이 빌드됨: 실제 HRTF 절은 건너뜀)\n");
#endif
    }

    std::error_code ec;
    fs::remove_all(tmp, ec);

    // 13. 출력 장치 설정(2026-10-16): 장치 없는 엔진은 값만 기억합니다. 장치 ID · 이름 변환 도우미도 여기서 봅니다.
    std::printf("\n== 13. 출력 장치 설정 ==\n");
    {
        AudioSystem hs(true);
        hs.setOutputDevice("{0.0.0.00000000}.{abc}");
        hs.startup();
        check(hs.outputDevice() == "{0.0.0.00000000}.{abc}" && hs.outputOverride().empty(), "고른 장치를 기억(시작 전에 골라도)");
        check(hs.outputDevices().empty() && hs.activeOutputName().empty(), "장치 없는 엔진은 장치 목록 · 이름이 비어 있음");
        hs.setOutputOverride("{0.0.0.00000000}.{headset}");
        check(hs.outputOverride() == "{0.0.0.00000000}.{headset}" && hs.outputDevice() == "{0.0.0.00000000}.{abc}",
              "잠시 쓰는 장치(헤드셋)는 고른 장치를 바꾸지 않음");
        hs.setOutputOverride("");
        hs.setOutputDevice("");
        check(hs.outputOverride().empty() && hs.outputDevice().empty(), "비우면 시스템 기본");
        check(!hs.overrideActive(), "장치 없는 엔진은 헤드셋 장치가 열린 것으로 보지 않음(DEF-147 표시)");
        hs.tick(5.f);   // 장치가 없으니 다시 열기 점검도 아무것도 하지 않아야 함
        hs.shutdown();

        // 열어 볼 순서(DEF-146 · 147): 헤드셋 → 고른 장치 → 시스템 기본. 헤드셋이 없다고 고른 장치를 건너뛰지 않습니다.
        using V = std::vector<std::string>;
        check(AudioSystem::outputCandidates("{h}", "{a}") == V{ "{h}", "{a}", "" }, "헤드셋 다음은 고른 장치, 그다음 시스템 기본");
        check(AudioSystem::outputCandidates("", "{a}") == V{ "{a}", "" }, "고른 장치가 열리지 않으면 시스템 기본");
        check(AudioSystem::outputCandidates("", "") == V{ "" }, "고른 장치가 없으면 시스템 기본만");
        check(AudioSystem::outputCandidates("{H}", "{h}") == V{ "{H}", "" }, "같은 장치(대소문자 무시)는 한 번만");
        check(AudioSystem::outputCandidates("{h}", "") == V{ "{h}", "" }, "고른 장치가 시스템 기본이면 헤드셋 다음 시스템 기본");

        const wchar_t endpoint[] = L"{0.0.0.00000000}.{BAFA3D4B-1578-4319-AE32-C07C0494152E}";
        check(hutf::fromUtf16(endpoint) == "{0.0.0.00000000}.{BAFA3D4B-1578-4319-AE32-C07C0494152E}", "WASAPI 끝점 ID 를 UTF-8 로");
        const char16_t korean[] = { 0xD5E4, 0xB4DC, 0xD3F0, 0 };   // 헤드폰
        check(hutf::fromUtf16(korean) == "\xED\x97\xA4\xEB\x93\x9C\xED\x8F\xB0", "한글(3바이트 UTF-8)");
        const char16_t pair[] = { 0xD83C, 0xDFA7, 0 };              // U+1F3A7(헤드폰 그림 문자)
        check(hutf::fromUtf16(pair) == "\xF0\x9F\x8E\xA7", "서로게이트 쌍(4바이트 UTF-8)");
        const char16_t lone[] = { 0x41, 0xD800, 0x42, 0 };
        check(hutf::fromUtf16(lone) == "A\xEF\xBF\xBD" "B", "짝 없는 서로게이트는 U+FFFD");
        const char16_t loneLow[] = { 0xDC00, 0x43, 0 };
        check(hutf::fromUtf16(loneLow) == "\xEF\xBF\xBD" "C", "앞짝 없는 뒤 서로게이트도 U+FFFD");
        check(hutf::equalsIgnoreCase("{0.0.0.00000000}.{bafa3d4b}", "{0.0.0.00000000}.{BAFA3D4B}") &&
              hutf::equalsIgnoreCase("{0.0.0.00000000}.{BAFA3D4B}", "{0.0.0.00000000}.{bafa3d4b}") &&
              !hutf::equalsIgnoreCase("{a}", "{b}"), "장치 ID 는 대소문자를 무시하고 비교(어느 쪽이 대문자여도)");

        // 한국어 Windows 의 장치 이름: UI 는 ASCII 만 그리므로 한글을 빼고, 남은 글을 감싼 괄호를 벗깁니다.
        const std::string headphones = hutf::fromUtf16(u"헤드폰(Oculus Virtual Audio Device)");
        check(hutf::asciiLabel(headphones, "?") == "Oculus Virtual Audio Device", "\"헤드폰(Oculus Virtual Audio Device)\"");
        const std::string speaker = hutf::fromUtf16(u"스피커(Realtek(R) Audio)");
        check(hutf::asciiLabel(speaker, "?") == "Realtek(R) Audio", "안에 괄호가 있어도 바깥 괄호만 벗김");
        check(hutf::asciiLabel("(A) (B)", "?") == "(A) (B)", "괄호가 둘로 나뉘면 그대로");
        check(hutf::asciiLabel("Generic  Wireless Headset", "?") == "Generic Wireless Headset", "ASCII 이름은 공백만 정리");
        check(hutf::asciiLabel(hutf::fromUtf16(u"스피커"), "Device 3") == "Device 3", "남는 글이 없으면 대신 쓸 이름");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
