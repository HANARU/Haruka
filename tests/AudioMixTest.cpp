// 오디오 버스 그래프 · 스냅숏 · 더킹 · 리미터 · 사운드 큐 테스트입니다(2026-11-21, Docs/Audio_Design.md 12절).
// 출력 장치 없는 소리 엔진(AudioSystem headless)에서 출력을 직접 당겨 숫자로 검산합니다. 창 · GPU 가 필요 없습니다.
//
//   1. 순수 계산(hmix): 더킹 포락선 · 스냅숏 섞기 · 리미터 · 큐 글 형식 · 가중치 랜덤 · 시드
//   2. 버스 그래프: 표준 5버스 · 버스 더하기 · 부모 · 음소거 · 자기 버스로 가는 소리 · 버스 미터
//   3. 스냅숏: 페이드 인 · 아웃과 우선순위 덮어쓰기를 렌더 RMS 로
//   4. 더킹: 원천 버스에 소리가 나면 대상 버스가 공격 · 유지 · 해제 곡선으로 내려갔다 올라옴(렌더 RMS 와 계산 곡선)
//   5. 리미터: 출력 피크 ≤ 한도(마스터 볼륨 포함), 한도 아래 소리는 그대로
//   6. 사운드 큐: 같은 시드 = 같은 변형 순서, 상한에서 가장 오래된 것이 꺼짐, rejectNew, 재시작 간격, 피치 · 볼륨, 큐의 버스
//   7. CVar · 콘솔 명령 · 월드 서비스

#include "audio/AudioSystem.hpp"
#include "audio/AudioComponent.hpp"
#include "audio/AudioMix.hpp"
#include "components/CameraComponent.hpp"
#include "core/CVar.hpp"
#include "scene/Actor.hpp"
#include "scene/World.hpp"

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

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed; else ++g_failed;
}

constexpr uint32_t kRate  = AudioSystem::kSampleRate;
constexpr float    kPi    = 3.14159265358979f;
constexpr float    kDt    = 1.f / 60.f;
constexpr uint32_t kBlock = 800;   // 한 틱(1/60 초)의 프레임

void writeWavF32(const fs::path& path, const std::vector<float>& s) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = (uint32_t)(s.size() * sizeof(float));
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(3); u16(1); u32(kRate); u32(kRate * 4); u16(4); u16(32);
    f.write("data", 4); u32(bytes);
    f.write(reinterpret_cast<const char*>(s.data()), bytes);
}

std::vector<float> sine(float hz, float seconds, float amp) {
    std::vector<float> s((size_t)(seconds * kRate));
    for (size_t i = 0; i < s.size(); ++i) s[i] = amp * std::sin(2.f * kPi * hz * (float)i / kRate);
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

// 설정을 반영(tick)한 뒤 게인 보간이 끝나도록 흘려보내고 잽니다(왼쪽 채널 RMS).
double level(AudioSystem& sys) {
    sys.tick(kDt);
    render(sys, 2400);
    return rms(render(sys, 9600).L);
}

AudioComponent* spawn2D(World& w, const char* name, const fs::path& file, bool loop, EAudioBus bus = EAudioBus::Sfx) {
    Actor* a = w.spawnActor(name);
    a->setLocalPosition({ 0.f, 0.f, -1.f });
    auto* ac = a->addComponent<AudioComponent>();
    ac->filePath               = file.string();
    ac->loop                   = loop;
    ac->attenuation.spatialize = false;
    ac->bus                    = bus;
    return ac;
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

void clearWorld(World& w, AudioSystem& sys, Actor* keep) {
    std::vector<Actor*> gone;
    for (auto& a : w.getActors())
        if (a.get() != keep) gone.push_back(a.get());
    for (Actor* a : gone) w.destroyActor(a);
    sys.tick(kDt);
    render(sys, kBlock);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // [Audio] 경고(stderr)가 검사 줄 사이에 끼어 [PASS] 를 깨뜨리지 않게
    std::printf("Haruka 오디오 버스 그래프 · 스냅숏 · 더킹 · 리미터 · 사운드 큐 테스트\n");

    // ── 1. 순수 계산 ─────────────────────────────────────────────────────────────────
    std::printf("\n== 1. 순수 계산(hmix) ==\n");
    {
        // 더킹 포락선: 공격 0.1 · 유지 0.2 · 해제 0.5, −12 dB
        hmix::DuckRule r;
        r.amountDb = -12.f; r.attack = 0.1f; r.hold = 0.2f; r.release = 0.5f; r.thresholdDb = -40.f;
        hmix::DuckState st;
        check(hmix::stepDuck(st, r, 0.001f, 0.05f) == 1.f && st.env == 0.f, "더킹: 문턱(−40 dB = 0.01) 아래면 그대로 1");
        hmix::stepDuck(st, r, 0.5f, 0.05f);
        check(near(st.env, 0.5, 1e-6) && near(hmix::duckGain(st, r), std::pow(10.0, -6.0 / 20.0), 1e-5), "더킹: 공격 절반 → 포락선 0.5 = −6 dB");
        const float g1 = hmix::stepDuck(st, r, 0.5f, 0.05f);
        check(st.env == 1.f && near(g1, std::pow(10.0, -12.0 / 20.0), 1e-5), "더킹: 공격 끝 → −12 dB(0.251)");
        for (int i = 0; i < 3; ++i) hmix::stepDuck(st, r, 0.f, 0.05f);
        check(st.env == 1.f && st.holdLeft > 0.f, "더킹: 소리가 끊겨도 유지 시간(0.2 s) 동안 그대로");
        hmix::stepDuck(st, r, 0.f, 0.05f);   // 유지 끝
        for (int i = 0; i < 5; ++i) hmix::stepDuck(st, r, 0.f, 0.05f);
        check(near(st.env, 0.5, 1e-5), "더킹: 해제 0.25 s 뒤 포락선 0.5");
        for (int i = 0; i < 6; ++i) hmix::stepDuck(st, r, 0.f, 0.05f);
        check(st.env == 0.f && hmix::duckGain(st, r) == 1.f, "더킹: 해제가 끝나면 정확히 1");

        // 스냅숏 섞기: A(우선순위 0) bus1 0.5 · bus2 0.8, B(우선순위 1) bus1 0.2 무게 0.5
        std::vector<hmix::Snapshot> defs(2);
        defs[0].name = "A"; defs[0].priority = 0; defs[0].volumes = { { 1, 0.5f }, { 2, 0.8f } };
        defs[1].name = "B"; defs[1].priority = 1; defs[1].volumes = { { 1, 0.2f } };
        std::vector<hmix::ActiveSnapshot> act = { { 1, 0.5f, true, 2 }, { 0, 1.f, true, 1 } };
        std::vector<float> out(5);
        hmix::blendSnapshots(defs, act, out);
        check(near(out[1], 0.35, 1e-6) && near(out[2], 0.8, 1e-6) && out[0] == 1.f && out[3] == 1.f,
              "스냅숏: 높은 우선순위가 같은 버스를 덮어 섞음(0.5 → 0.2 를 무게 0.5 로 = 0.35), 다른 버스는 그대로");
        std::swap(act[0], act[1]);
        std::vector<float> out2(5);
        hmix::blendSnapshots(defs, act, out2);
        check(out == out2, "스냅숏: 목록 순서와 무관(우선순위 순서로 적용)");
        defs[1].priority = 0;   // 같은 우선순위 → 켠 순서(B 가 나중)
        hmix::blendSnapshots(defs, act, out2);
        check(near(out2[1], 0.35, 1e-6), "스냅숏: 같은 우선순위면 나중에 켠 것이 위");
        hmix::ActiveSnapshot fade{ 0, 0.f, true, 1 };
        defs[0].fadeIn = 0.5f; defs[0].fadeOut = 1.f;
        hmix::stepSnapshot(fade, defs[0], 0.1f);
        check(near(fade.weight, 0.2, 1e-6), "스냅숏: 페이드 인 0.5 s 중 0.1 s → 무게 0.2");
        fade.on = false;
        const bool gone1 = hmix::stepSnapshot(fade, defs[0], 0.1f);    // 0.2 → 0.1
        const bool gone2 = hmix::stepSnapshot(fade, defs[0], 0.15f);   // 0.1 → 0
        check(!gone1 && gone2 && fade.weight == 0.f, "스냅숏: 페이드 아웃이 0 에 닿으면 빼라고 알림");

        // 리미터
        std::vector<float> sig(2 * 4800);
        for (size_t i = 0; i < 4800; ++i) {
            sig[2 * i]     = 2.f * std::sin(2.f * kPi * 440.f * (float)i / kRate);
            sig[2 * i + 1] = 1.5f * std::sin(2.f * kPi * 660.f * (float)i / kRate);
        }
        hmix::LimiterState ls;
        hmix::limitBlock(sig.data(), 4800, 2, 0.5f, hmix::limiterReleaseCoef(0.1f, kRate), ls);
        float pk = 0.f;
        for (float x : sig) pk = std::max(pk, std::fabs(x));
        check(pk <= 0.5f && pk > 0.49f, "리미터: 피크 2.0 신호 → 출력 피크 ≤ 한도 0.5 (" + std::to_string(pk) + ")");
        std::vector<float> quiet(2 * 480), quietRef;
        for (size_t i = 0; i < quiet.size(); ++i) quiet[i] = 0.3f * std::sin((float)i * 0.01f);
        quietRef = quiet;
        hmix::LimiterState q;
        hmix::limitBlock(quiet.data(), 480, 2, 0.5f, hmix::limiterReleaseCoef(0.1f, kRate), q);
        check(quiet == quietRef && q.gain == 1.f, "리미터: 한도 아래 소리는 비트 단위로 그대로");
        check(near(hmix::limiterReleaseCoef(0.1f, kRate), 1.0 - std::exp(-1.0 / 4800.0), 1e-7) &&
              hmix::limiterReleaseCoef(0.f, kRate) == 1.f, "리미터: 해제 계수 = 1 − e^(−1/(시간 × 샘플률))");

        // 랜덤 · 해시
        check(hmix::fnv1a64("") == 0xCBF29CE484222325ull && hmix::fnv1a64("a") == 0xAF63DC4C8601EC8Cull, "FNV-1a 64 알려진 값");
        uint64_t s1 = 7, s2 = 7;
        bool same = true;
        for (int i = 0; i < 100; ++i) same = same && hmix::splitmix64(s1) == hmix::splitmix64(s2);
        float lo = 1.f, hi = 0.f;
        for (int i = 0; i < 10000; ++i) { const float u = hmix::uniform01(s1); lo = std::min(lo, u); hi = std::max(hi, u); }
        check(same && lo >= 0.f && hi < 1.f && lo < 0.01f && hi > 0.99f, "splitmix64: 같은 상태 = 같은 수열, uniform01 은 [0, 1)");

        // 큐 글 형식
        const std::string text =
            "# 발소리\nHCUE 1\nvariant = step_a.wav 1\nvariant = /Game/audio/step_b.wav 3\nvariant = step_c.wav 0\n"
            "bus = music\nvolume = 0.5 0.9\npitch = 0.9 1.1\nmaxInstances = 4\noverflow = rejectNew\nminInterval = 0.05\nseed = 12345\n";
        hmix::SoundCue c;
        std::vector<std::string> msgs;
        const bool okCue = hmix::parseCue(text, "/Game/sfx", c, msgs);
        check(okCue && msgs.empty() && c.variants.size() == 3 && c.variants[0].path == "/Game/sfx/step_a.wav" &&
              c.variants[1].path == "/Game/audio/step_b.wav" && c.variants[1].weight == 3.f && c.variants[2].weight == 0.f,
              "큐: 변형 셋(상대 경로는 큐 폴더 기준, 가중치)");
        check(c.bus == "music" && c.volumeMin == 0.5f && c.volumeMax == 0.9f && c.pitchMin == 0.9f && c.pitchMax == 1.1f &&
              c.maxInstances == 4 && c.overflow == hmix::ECueOverflow::RejectNew && c.minInterval == 0.05f && c.hasSeed &&
              c.seed == 12345u, "큐: 버스 · 볼륨 · 피치 범위 · 상한 · 넘을 때 규칙 · 간격 · 시드");
        hmix::SoundCue back;
        std::vector<std::string> m2;
        check(hmix::parseCue(hmix::cueToText(c), "", back, m2) && m2.empty() && hmix::cueToText(back) == hmix::cueToText(c),
              "큐: 글로 쓰고 다시 읽으면 같음");
        hmix::SoundCue bad;
        std::vector<std::string> m3;
        const bool okBad = hmix::parseCue("HCUE 1\nvariant = a.wav\npitch = 9\nvolume = 0.8 0.2\ncolor = red\noverflow = loudest\n", "", bad, m3);
        check(okBad && m3.size() == 4 && bad.pitchMin == 1.f && bad.volumeMax == 1.f && bad.overflow == hmix::ECueOverflow::StopOldest,
              "큐: 깨진 값 · 모르는 키는 경고하고 기본값 유지(" + std::to_string(m3.size()) + "개)");
        std::vector<std::string> m4;
        check(!hmix::parseCue("HCUE 1\nbus = sfx\n", "", bad, m4) && !m4.empty(), "큐: 변형이 없으면 실패");

        // 가중치 랜덤 · 시드
        uint64_t ra = 99, rb = 99, rc = 100;
        bool sameSeq = true, diffSeq = false;
        for (int i = 0; i < 32; ++i) {
            const hmix::CuePick a = hmix::pickCue(c, ra), b = hmix::pickCue(c, rb), d = hmix::pickCue(c, rc);
            sameSeq = sameSeq && a.variant == b.variant && a.volume == b.volume && a.pitch == b.pitch;
            diffSeq = diffSeq || a.variant != d.variant || a.pitch != d.pitch;
        }
        check(sameSeq && diffSeq, "큐: 같은 시드 = 같은 변형 · 볼륨 · 피치 순서, 다른 시드는 다름");
        int counts[3] = { 0, 0, 0 };
        bool inRange = true;
        uint64_t rs = 1;
        for (int i = 0; i < 20000; ++i) {
            const hmix::CuePick p = hmix::pickCue(c, rs);
            if (p.variant >= 0) ++counts[p.variant];
            inRange = inRange && p.volume >= 0.5f && p.volume <= 0.9f && p.pitch >= 0.9f && p.pitch <= 1.1f;
        }
        check(near(counts[1] / 20000.0, 0.75, 0.015) && counts[2] == 0, "큐: 가중치 1 : 3 : 0 → 0.25 · 0.75 · 0 (" +
              std::to_string(counts[0]) + " · " + std::to_string(counts[1]) + " · " + std::to_string(counts[2]) + ")");
        check(inRange, "큐: 볼륨 · 피치는 범위 안");
    }

    // ── 엔진 준비 ────────────────────────────────────────────────────────────────────
    const fs::path tmp = fs::temp_directory_path() / "HarukaAudioMixTest";
    fs::create_directories(tmp);
    const fs::path toneFile  = tmp / "tone_1k.wav";    // 1 kHz, 1 초, 진폭 0.5(정수 주기라 이음매 없이 반복)
    const fs::path tone2File = tmp / "tone_500.wav";   // 500 Hz
    const fs::path burstFile = tmp / "burst_3k.wav";   // 3 kHz, 0.3 초(더킹 원천)
    const fs::path shortFile = tmp / "tone_short.wav"; // 0.1 초
    writeWavF32(toneFile,  sine(1000.f, 1.f, 0.5f));
    writeWavF32(tone2File, sine(500.f, 1.f, 0.5f));
    writeWavF32(burstFile, sine(3000.f, 0.3f, 0.5f));
    writeWavF32(shortFile, sine(1000.f, 0.1f, 0.5f));

    World       world;
    AudioSystem sys(true);
    sys.setWorld(&world);
    sys.setListenerSelector(CameraComponent::activeOwnerIn);
    sys.startup();
    Actor* cam = world.spawnActor("Camera");
    cam->addComponent<CameraComponent>();
    sys.onBeginPlay();

    // ── 2. 버스 그래프 ───────────────────────────────────────────────────────────────
    std::printf("\n== 2. 버스 그래프 ==\n");
    {
        bool names = sys.busCount() == kAudioBusCount;
        for (int b = 0; b < kAudioBusCount; ++b) names = names && sys.busName(b) == audioBusName((EAudioBus)b) && sys.busParent(b) == -1;
        check(names && sys.findBus("music") == 1 && sys.findBus("nope") == -1, "표준 5버스(이름 · id = EAudioBus, 부모 없음)");
        const int dlg = sys.addBus("dialog_fx", "voice");
        check(dlg == kAudioBusCount && sys.busParent(dlg) == (int)EAudioBus::Voice, "버스 더하기: 부모 voice 아래 dialog_fx");
        check(sys.addBus("dialog_fx") == -1 && sys.addBus("Bad Name") == -1 && sys.addBus("orphan", "nope") == -1,
              "버스 더하기 거절: 같은 이름 · 틀린 이름 · 없는 부모");

        AudioComponent* mus = spawn2D(world, "Music", toneFile, true, EAudioBus::Music);
        const double base = level(sys);
        sys.tick(kDt);   // 미터는 지난 틱 뒤의 렌더 피크
        check(base > 0.3 && sys.busPeak(1) > 0.4f && sys.busPeak(0) == 0.f, "음악 소리는 music 버스로(미터: music 피크 " +
              std::to_string(sys.busPeak(1)) + ", sfx 0, RMS " + std::to_string(base) + ")");
        sys.setBusMuted(1, true);
        check(level(sys) < 1e-7 && sys.busMuted(1), "music 버스 음소거 → 무음");
        sys.setBusMuted(1, false);
        sys.setBusVolume(EAudioBus::Sfx, 0.f);
        check(near(level(sys) / base, 1.0, 1e-3), "다른 버스(sfx 0)는 영향 없음");
        sys.setBusVolume(EAudioBus::Sfx, 1.f);
        mus->bus = EAudioBus::Ambient;   // 플레이 중에 버스를 바꾸면 따라감
        sys.setBusVolume(EAudioBus::Ambient, 0.5f);
        const double amb = level(sys);
        sys.tick(kDt);
        check(near(amb / base, 0.5, 0.01) && sys.busPeak(3) > 0.f && sys.busPeak(1) == 0.f, "플레이 중 버스 바꾸기 → ambient 0.5 를 따름");
        sys.setBusVolume(EAudioBus::Ambient, 1.f);
        world.destroyActor(mus->owner);
        sys.tick(kDt);

        // 부모 버스: dialog_fx(부모 voice) 로 가는 큐 소리. voice 0.5 × dialog_fx 0.5 = 0.25
        const fs::path dlgCue = tmp / "dialog.hcue";
        { std::ofstream f(dlgCue); f << "HCUE 1\nvariant = tone_1k.wav\nbus = dialog_fx\n"; }
        AudioComponent* d = spawn2D(world, "Dialog", dlgCue, true);
        d->filePath = dlgCue.string();
        const double dBase = level(sys);
        sys.setBusVolume(EAudioBus::Voice, 0.5f);
        const double half = level(sys);
        sys.setBusVolume(dlg, 0.5f);
        const double quarter = level(sys);
        check(near(dBase, base, base * 1e-3) && near(half / dBase, 0.5, 0.01) && near(quarter / dBase, 0.25, 0.01),
              "부모 버스 볼륨이 자식 버스 소리에 곱해짐(0.5 → 0.5 × 0.5 = 0.25)");
        check(near(sys.busEffectiveGain(dlg), 0.25, 1e-6) && sys.busGain(dlg) == 0.5f, "부모까지 곱한 게인 0.25");
        sys.setBusVolume(EAudioBus::Voice, 1.f);
        sys.setBusVolume(dlg, 1.f);
        world.destroyActor(d->owner);
        sys.tick(kDt);
    }

    // ── 3. 스냅숏 ────────────────────────────────────────────────────────────────────
    std::printf("\n== 3. 스냅숏 ==\n");
    {
        AudioComponent* mus = spawn2D(world, "Music", toneFile, true, EAudioBus::Music);
        const double base = level(sys);
        hmix::Snapshot menu;
        menu.name = "menu"; menu.priority = 0; menu.fadeIn = 0.5f; menu.fadeOut = 0.f;
        menu.volumes = { { (int)EAudioBus::Music, 0.25f } };
        check(sys.defineSnapshot(menu), "스냅숏 정의");
        hmix::Snapshot badSnap = menu;
        badSnap.volumes = { { 99, 0.5f } };
        check(!sys.defineSnapshot(badSnap) && !sys.setSnapshotActive("nope", true), "틀린 버스 · 없는 이름은 거절");
        check(sys.setSnapshotActive("menu", true) && sys.snapshotActive("menu"), "스냅숏 켜기");
        for (int i = 0; i < 15; ++i) { sys.tick(kDt); render(sys, kBlock); }   // 0.25 초 = 페이드 절반
        check(near(sys.snapshotWeight("menu"), 0.5, 0.01) && near(sys.busSnapshotGain(1), 0.625, 0.01), "페이드 절반: 무게 0.5 → 1 + (0.25 − 1) × 0.5 = 0.625");
        for (int i = 0; i < 20; ++i) { sys.tick(kDt); render(sys, kBlock); }
        check(near(level(sys) / base, 0.25, 0.01), "페이드가 끝나면 music 0.25 (RMS 비율)");

        hmix::Snapshot cut;
        cut.name = "cutscene"; cut.priority = 5; cut.fadeIn = 0.f; cut.fadeOut = 0.f;
        cut.volumes = { { (int)EAudioBus::Music, 0.8f }, { (int)EAudioBus::Sfx, 0.f } };
        sys.defineSnapshot(cut);
        sys.setSnapshotActive("cutscene", true);
        check(near(sys.busSnapshotGain(1), 0.8, 1e-6) && sys.busSnapshotGain(0) == 0.f, "우선순위 높은 스냅숏이 덮어씀(페이드 0 은 바로)");
        check(near(level(sys) / base, 0.8, 0.01), "덮어쓴 값이 소리에(RMS 비율 0.8)");
        sys.setSnapshotActive("cutscene", false);
        check(near(sys.busSnapshotGain(1), 0.25, 1e-6) && sys.busSnapshotGain(0) == 1.f, "높은 것을 끄면 아래 스냅숏 값으로");
        sys.setSnapshotActive("menu", false);
        check(!sys.snapshotActive("menu") && sys.busSnapshotGain(1) == 1.f && near(level(sys) / base, 1.0, 1e-3),
              "모두 끄면 원래 크기");
        world.destroyActor(mus->owner);
        sys.tick(kDt);
    }

    // ── 4. 더킹 ──────────────────────────────────────────────────────────────────────
    std::printf("\n== 4. 더킹 ==\n");
    {
        AudioComponent* mus = spawn2D(world, "Music", toneFile, true, EAudioBus::Music);
        AudioComponent* shot = spawn2D(world, "Shot", burstFile, false, EAudioBus::Sfx);
        shot->autoPlay = false;
        shot->volume   = 0.02f;   // 원천은 아주 작게(음악 RMS 를 재는 데 섞이는 양 < 0.3 %)
        hmix::DuckRule rule;
        rule.source = (int)EAudioBus::Sfx; rule.target = (int)EAudioBus::Music;
        rule.amountDb = -12.f; rule.attack = 0.1f; rule.hold = 0.2f; rule.release = 0.5f; rule.thresholdDb = -50.f;
        check(sys.addDuck(rule) == 0 && sys.addDuck({ 1, 1 }) == -1, "더킹 규칙(같은 버스끼리는 거절)");
        double base = 0.0;
        for (int i = 0; i < 6; ++i) { sys.tick(kDt); const double r = rms(render(sys, kBlock).L); if (i >= 2) base += r / 4.0; }
        constexpr int kTicks = 110, kShotAt = 5;
        std::vector<float>  gains(kTicks);
        std::vector<double> lv(kTicks);
        for (int k = 0; k < kTicks; ++k) {
            if (k == kShotAt) shot->play();
            sys.tick(kDt);
            gains[k] = sys.busDuckGain((int)EAudioBus::Music);
            lv[k]    = rms(render(sys, kBlock).L);
        }
        const float minG = *std::min_element(gains.begin(), gains.end());
        int a0 = -1, a1 = -1, r0 = -1, r1 = -1;
        for (int k = 0; k < kTicks; ++k) {
            if (a0 < 0 && gains[k] < 1.f) a0 = k;
            if (a1 < 0 && gains[k] <= minG) a1 = k;
            if (a1 >= 0 && r0 < 0 && k > a1 && gains[k] > minG) r0 = k;
            if (r0 >= 0 && r1 < 0 && gains[k] == 1.f) r1 = k;
        }
        check(a0 == kShotAt + 1, "원천 소리가 난 다음 틱부터 내려감(" + std::to_string(a0) + "틱)");
        check(near(minG, std::pow(10.0, -12.0 / 20.0), 1e-4), "가장 깊은 곳 −12 dB (" + std::to_string(minG) + ")");
        check(a1 - a0 >= 5 && a1 - a0 <= 6, "공격 0.1 s ≈ 6틱 (" + std::to_string(a1 - a0 + 1) + "틱째에 바닥)");
        bool attackCurve = a0 > 0;
        for (int j = 0; a0 > 0 && j < a1 - a0; ++j)
            attackCurve = attackCurve && near(gains[a0 + j], std::pow(10.0, -12.0 * std::min(1.0, (j + 1) * (double)kDt / 0.1) / 20.0), 1e-3);
        check(attackCurve, "공격 곡선 = −12 dB × (경과 / 공격)");
        // 원천 0.3 s = 18틱(마지막 블록까지 피크가 남음) + 유지 0.2 s = 12틱 동안 바닥
        check(r0 - a0 >= 18 + 12 - 1 && r0 - a0 <= 18 + 12 + 2, "원천이 끝나고 유지 0.2 s 뒤에 해제 시작(" + std::to_string(r0 - a0) + "틱)");
        bool releaseCurve = r0 > 0;
        for (int j = 0; r0 > 0 && r0 + j < kTicks && gains[r0 + j] < 1.f; ++j)
            releaseCurve = releaseCurve && near(gains[r0 + j], std::pow(10.0, -12.0 * (1.0 - (j + 1) * (double)kDt / 0.5) / 20.0), 1e-3);
        check(releaseCurve && r1 - r0 >= 29 && r1 - r0 <= 30, "해제 곡선 = −12 dB × (1 − 경과 / 해제), 0.5 s ≈ 30틱 (" + std::to_string(r1 - r0) + ")");
        // 렌더 RMS 가 게인을 따름: 블록 안에서 게인이 a → b 로 선형이면 RMS = √((a² + ab + b²) / 3)
        int checked = 0, okBlocks = 0;
        double worst = 0.0;
        for (int k = 1; k < kTicks; ++k) {
            const double a = gains[k - 1], b = gains[k];
            if (std::fabs(a - b) > 0.06 * b) continue;   // 공격 블록(한 틱에 2 dB)은 보간 위치에 민감해 뺌
            const double want = std::sqrt((a * a + a * b + b * b) / 3.0);
            const double err  = std::fabs(lv[k] / base - want) / want;
            worst = std::max(worst, err);
            ++checked;
            if (err < 0.03) ++okBlocks;
        }
        check(checked > 90 && okBlocks == checked, "렌더 RMS 가 더킹 게인을 따름(" + std::to_string(okBlocks) + "/" +
              std::to_string(checked) + " 블록, 최대 오차 " + std::to_string(worst * 100.0) + " %)");
        check(near(lv[a1 + 3] / base, minG, 0.01) && near(lv[kTicks - 1] / base, 1.0, 0.01), "바닥에서 RMS 0.251, 해제 뒤 1");
        sys.clearDucks();
        world.destroyActor(mus->owner);
        world.destroyActor(shot->owner);
        sys.tick(kDt);
    }

    // ── 5. 리미터 ────────────────────────────────────────────────────────────────────
    std::printf("\n== 5. 리미터 ==\n");
    {
        AudioComponent* a = spawn2D(world, "LoudA", toneFile, true);
        AudioComponent* b = spawn2D(world, "LoudB", toneFile, true);
        a->volume = 2.f; b->volume = 2.f;   // 0.5 × 2 + 0.5 × 2 = 피크 2.0
        sys.tick(kDt);
        render(sys, 4800);
        const float off = peak(render(sys, 9600));
        check(!sys.limiterEnabled() && off > 1.9f, "리미터 기본 끔: 피크 2.0 그대로 (" + std::to_string(off) + ")");
        sys.setLimiter(true, -6.f, 0.1f);
        sys.tick(kDt);
        const float lim = hdb::dbToGain(-6.f);
        const float on = peak(render(sys, 9600));
        check(on <= lim * 1.000001f && on > lim * 0.95f, "리미터 −6 dBFS: 출력 피크 ≤ 0.501 (" + std::to_string(on) + ")");
        sys.setMasterVolume(0.5f);
        sys.tick(kDt);
        render(sys, 2400);
        const float onM = peak(render(sys, 9600));
        check(onM <= lim * 1.000001f && onM > lim * 0.9f, "마스터 0.5 에서도 최종 피크 ≤ 한도 (" + std::to_string(onM) + ")");
        sys.setMasterVolume(1.f);
        a->volume = 0.4f; b->volume = 0.4f;   // 피크 0.4 < 0.501
        sys.tick(kDt);
        render(sys, 48000);   // 해제(0.1 s 시간 상수)가 끝나게
        const float quiet = peak(render(sys, 9600));
        check(near(quiet, 0.4, 0.002), "한도 아래 소리는 줄이지 않음 (" + std::to_string(quiet) + ")");
        sys.setLimiter(false);
        world.destroyActor(a->owner);
        world.destroyActor(b->owner);
        sys.tick(kDt);
    }

    // ── 6. 사운드 큐 ─────────────────────────────────────────────────────────────────
    std::printf("\n== 6. 사운드 큐 ==\n");
    {
        auto writeCue = [&](const char* name, const std::string& body) {
            const fs::path p = tmp / name;
            std::ofstream f(p);
            f << "HCUE 1\n" << body;
            return p.string();
        };
        const std::string seqCue = writeCue("seq.hcue", "variant = tone_1k.wav 1\nvariant = tone_500.wav 1\nvariant = tone_short.wav 2\n"
                                                        "pitch = 0.9 1.1\nvolume = 0.6 1\n");
        // 같은 시드 = 같은 변형 순서(컴포넌트가 큐를 가리킴)
        auto sequence = [&](uint64_t seed) {
            sys.setCueSeed(seqCue, seed);
            std::vector<int> v;
            for (int i = 0; i < 8; ++i) {
                AudioComponent* ac = spawn2D(world, "Seq", seqCue, false);
                ac->autoPlay = false;
                sys.tick(kDt);
                v.push_back(sys.voiceCueVariant(ac->voice_));
                const float p = sys.voiceCuePitch(ac->voice_), vol = sys.voiceCueVolume(ac->voice_);
                if (p < 0.9f || p > 1.1f || vol < 0.6f || vol > 1.f) v.push_back(-99);
                world.destroyActor(ac->owner);
            }
            sys.tick(kDt);
            return v;
        };
        const std::vector<int> s42 = sequence(42), s42b = sequence(42), s7 = sequence(7);
        std::string seqText;
        for (int x : s42) seqText += std::to_string(x);
        check(s42 == s42b && s42 != s7 && std::find(s42.begin(), s42.end(), -1) == s42.end() &&
              std::find(s42.begin(), s42.end(), -99) == s42.end(),
              "같은 시드(42) = 같은 변형 순서 " + seqText + ", 다른 시드(7)는 다름, 피치 · 볼륨은 범위 안");
        uint64_t mirror = 42;
        bool matches = true;
        for (int x : s42) matches = matches && hmix::pickCue(*sys.cue(seqCue), mirror).variant == x;
        check(matches, "엔진의 순서 = 같은 시드로 hmix::pickCue 를 부른 순서");

        // 상한 2 · stopOldest: 한 번 재생 셋 → 가장 먼저 낸 것이 꺼짐
        const std::string capCue = writeCue("cap.hcue", "variant = tone_1k.wav\nmaxInstances = 2\noverflow = stopOldest\n");
        AudioAttenuationSettings flat;
        flat.spatialize = false;
        const uint32_t i1 = sys.playCue(capCue, {}, 1.f, flat);
        sys.tick(kDt); render(sys, kBlock);
        const uint32_t i2 = sys.playCue(capCue, {}, 1.f, flat);
        sys.tick(kDt); render(sys, kBlock);
        const uint32_t i3 = sys.playCue(capCue, {}, 1.f, flat);
        check(i1 && i2 && i3 && !sys.oneShotPlaying(i1) && sys.oneShotPlaying(i2) && sys.oneShotPlaying(i3) &&
              sys.cueInstanceCount(capCue) == 2, "상한 2 · stopOldest: 셋째를 내면 가장 오래된 첫째가 꺼짐");
        const uint32_t i4 = sys.playCue(capCue, {}, 1.f, flat);
        check(i4 && !sys.oneShotPlaying(i2) && sys.oneShotPlaying(i3) && sys.oneShotPlaying(i4), "넷째 → 둘째가 꺼짐(시작 순서)");
        // 컴포넌트 인스턴스도 같은 규칙
        AudioComponent* c1 = spawn2D(world, "Cap1", capCue, true);
        c1->filePath = capCue;
        sys.tick(kDt); render(sys, kBlock);
        check(c1->isPlaying() && !sys.oneShotPlaying(i3) && sys.cueInstanceCount(capCue) == 2, "컴포넌트가 큐를 내면 가장 오래된 한 번 재생(셋째)이 꺼짐");
        sys.tick(kDt); render(sys, kBlock);
        sys.playCue(capCue, {}, 1.f, flat);
        sys.tick(kDt); render(sys, kBlock);
        check(!sys.oneShotPlaying(i4) && c1->isPlaying(), "그다음 한 번 재생은 넷째를 끔(컴포넌트가 더 나중)");
        sys.playCue(capCue, {}, 1.f, flat);
        check(!c1->isPlaying() && sys.cueInstanceCount(capCue) == 2, "그다음에는 가장 오래된 컴포넌트가 멈춤");
        world.destroyActor(c1->owner);
        sys.onEndPlay();
        sys.onBeginPlay();
        check(sys.cueInstanceCount(capCue) == 0, "플레이가 끝나면 인스턴스 0");

        // rejectNew
        const std::string rejCue = writeCue("reject.hcue", "variant = tone_1k.wav\nmaxInstances = 1\noverflow = rejectNew\n");
        const uint32_t r1 = sys.playCue(rejCue, {}, 1.f, flat);
        const uint32_t r2 = sys.playCue(rejCue, {}, 1.f, flat);
        check(r1 && !r2 && sys.oneShotPlaying(r1), "상한 1 · rejectNew: 둘째는 내지 않고 첫째는 그대로");
        // 재시작 최소 간격 0.1 s
        const std::string gapCue = writeCue("gap.hcue", "variant = tone_short.wav\nminInterval = 0.1\n");
        const uint32_t g1 = sys.playCue(gapCue, {}, 1.f, flat);
        const uint32_t g2 = sys.playCue(gapCue, {}, 1.f, flat);
        for (int i = 0; i < 4; ++i) { sys.tick(kDt); render(sys, kBlock); }   // 0.067 s
        const uint32_t g3 = sys.playCue(gapCue, {}, 1.f, flat);
        for (int i = 0; i < 3; ++i) { sys.tick(kDt); render(sys, kBlock); }   // 0.117 s
        const uint32_t g4 = sys.playCue(gapCue, {}, 1.f, flat);
        check(g1 && !g2 && !g3 && g4, "재시작 최소 간격 0.1 s: 바로 · 0.067 s 뒤는 거절, 0.117 s 뒤는 냄");
        sys.onEndPlay();
        sys.onBeginPlay();

        // 큐의 볼륨 · 피치 · 버스가 소리에
        AudioComponent* plain = spawn2D(world, "Plain", toneFile, true);
        const double base = level(sys);
        world.destroyActor(plain->owner);
        sys.tick(kDt);
        const std::string volCue = writeCue("vol.hcue", "variant = tone_1k.wav\nvolume = 0.5\nbus = music\n");
        AudioComponent* vc = spawn2D(world, "Vol", volCue, true);
        vc->filePath = volCue;
        check(near(level(sys) / base, 0.5, 0.01), "큐 볼륨 0.5 → RMS 비율 0.5");
        sys.setBusMuted((int)EAudioBus::Music, true);
        check(level(sys) < 1e-7, "큐의 bus = music: music 음소거 → 무음(컴포넌트 bus 는 기본 sfx)");
        sys.setBusMuted((int)EAudioBus::Music, false);
        vc->bus = EAudioBus::Ambient;
        sys.setBusMuted((int)EAudioBus::Music, true);
        check(near(level(sys) / base, 0.5, 0.01), "컴포넌트가 bus 를 고르면 그것이 우선(ambient)");
        sys.setBusMuted((int)EAudioBus::Music, false);
        world.destroyActor(vc->owner);
        sys.tick(kDt);

        // 다시 재생하면 변형을 다시 뽑음
        const std::string reCue = writeCue("re.hcue", "variant = tone_short.wav\nvariant = tone_short.wav\nvariant = tone_short.wav\nseed = 5\n");
        AudioComponent* rc = spawn2D(world, "Re", reCue, false);
        rc->filePath = reCue;
        sys.tick(kDt);
        const AudioVoice* firstVoice = rc->voice_;
        const int firstVar = sys.voiceCueVariant(firstVoice);
        for (int i = 0; i < 12; ++i) { sys.tick(kDt); render(sys, kBlock); }   // 0.2 s > 0.1 s
        check(!rc->isPlaying(), "짧은 큐 소리가 끝남");
        rc->play();
        check(rc->isPlaying(), "play() 직후 재생 중으로 보임(다음 틱에 새 변형)");
        sys.tick(kDt);
        uint64_t m = 5;
        const int v1 = hmix::pickCue(*sys.cue(reCue), m).variant;
        const int v2 = hmix::pickCue(*sys.cue(reCue), m).variant;
        check(firstVar == v1 && sys.voiceCueVariant(rc->voice_) == v2 && rc->isPlaying(), "다시 재생 = 다음 변형(시드 5 의 둘째)");
        world.destroyActor(rc->owner);
        sys.tick(kDt);

        // 깨진 큐
        const std::string badCue = writeCue("bad.hcue", "bus = sfx\n");
        AudioComponent* bc = spawn2D(world, "Bad", badCue, true);
        bc->filePath = badCue;
        sys.tick(kDt);
        check(!bc->voice_ && bc->loadFailed_ && sys.playCue(badCue, {}) == 0 && !sys.cue(badCue), "변형 없는 큐는 소리를 만들지 않음");
        check(sys.playCue((tmp / "missing.hcue").string(), {}) == 0, "없는 큐 파일은 0");
        world.destroyActor(bc->owner);
        sys.tick(kDt);
        check(AudioSystem::isCuePath("/Game/a/b.HCUE") && !AudioSystem::isCuePath("a.wav") && !AudioSystem::isCuePath("hcue"), "큐 경로 판별");
    }

    // ── 7. CVar · 콘솔 · 월드 서비스 ─────────────────────────────────────────────────
    std::printf("\n== 7. CVar · 콘솔 · 월드 서비스 ==\n");
    {
        CVarRegistry reg;
        sys.registerCVars(reg);
        check(reg.find("audio.bus.music.volume") && reg.find("audio.bus.ui.mute") && reg.find("audio.limiter") &&
              reg.find("audio.limiter.ceiling") && reg.find("audio.bus.dialog_fx.volume"), "버스마다 volume · mute CVar, 리미터 CVar");
        std::string why;
        reg.set("audio.bus.music.volume", "0.5", CVarRegistry::Source::Console, &why);
        reg.dispatchChanges();
        check(near(sys.busGain(1), 0.5, 1e-6) && sys.busVolume(EAudioBus::Music) == 1.f, "audio.bus.music.volume 0.5 → 게인 0.5(사용자 볼륨은 1 그대로)");
        reg.set("audio.bus.sfx.mute", "true", CVarRegistry::Source::Console, &why);
        reg.dispatchChanges();
        check(sys.busGain(0) == 0.f && sys.busMuted(0), "audio.bus.sfx.mute → 게인 0");
        reg.reset("audio.bus.sfx.mute");
        reg.reset("audio.bus.music.volume");
        reg.dispatchChanges();
        check(sys.busGain(0) == 1.f && sys.busGain(1) == 1.f, "reset → 1");
        reg.executeLogged("audio.limiter 1");
        reg.dispatchChanges();
        check(sys.limiterEnabled(), "콘솔 audio.limiter 1 → 리미터 켬");
        reg.executeLogged("audio.limiter 0");
        reg.dispatchChanges();
        const std::string onText = reg.execute("audio.snapshot menu on");
        check(sys.snapshotActive("menu") && onText.find("on") != std::string::npos, "콘솔 audio.snapshot menu on");
        reg.execute("audio.snapshot menu off");
        check(!sys.snapshotActive("menu") && reg.execute("audio.snapshot nope on").find("unknown") != std::string::npos,
              "audio.snapshot off · 없는 이름 알림");
        const std::string rep = reg.execute("audio.buses");
        check(rep.find("music") != std::string::npos && rep.find("dialog_fx") != std::string::npos && rep.find("limiter") != std::string::npos,
              "audio.buses 표");
        const int late = sys.addBus("late_bus");
        check(late > 0 && reg.find("audio.bus.late_bus.volume") != nullptr, "등록 뒤에 더한 버스도 CVar 가 생김");
        const std::string cueText = reg.execute("audio.cue " + (tmp / "seq.hcue").string());
        check(cueText.find("played") != std::string::npos, "콘솔 audio.cue 가 큐를 냄");
        check(world.service<AudioSystem>() == &sys, "월드 서비스 world.service<AudioSystem>()");
    }

    clearWorld(world, sys, nullptr);
    sys.shutdown();
    check(world.service<AudioSystem>() == nullptr, "shutdown 이 월드 서비스를 뺌");
    std::error_code ec;
    fs::remove_all(tmp, ec);

    std::printf("\n(%d passed, %d failed)\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
