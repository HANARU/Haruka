#pragma once
// ── 소리 믹스 계산: 스냅숏 섞기 · 더킹 포락선 · 피크 리미터 · 사운드 큐 (2026-11-21, Docs/Audio_Design.md 12절) ──────────────
// AudioSystem 이 쓰는 순수 계산입니다. miniaudio 와 무관해서 시험이 따로 검산합니다.
//
//  스냅숏(D3): 이름 있는 버스 볼륨 묶음. 켜고 끌 때 무게가 페이드 시간 동안 선형으로 움직이고, 우선순위가 낮은 것부터 덮어 섞습니다.
//  더킹(D4):   원천 버스의 피크가 문턱을 넘으면 포락선을 공격 시간 동안 올리고, 내려가면 유지 뒤 해제 시간 동안 내립니다(게인은 dB 축).
//  리미터(D5): 앞보기 없는 피크 리미터. 넘는 프레임은 바로 줄이고(즉시 공격), 그 뒤 해제 시간 상수로 1 을 향해 돌아갑니다.
//  큐(D6 ~ D8): .hcue 글 형식, 가중치 랜덤 변형 · 피치 · 볼륨 범위 · 동시 재생 상한 · 재시작 최소 간격, splitmix64 시드.
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmix {

// ── 스냅숏 ──────────────────────────────────────────────────────────────────────
struct Snapshot {
    std::string name;
    int         priority = 0;      // 클수록 나중에 적용(같은 버스를 덮어씀)
    float       fadeIn   = 0.5f;   // 초. 0 이면 바로
    float       fadeOut  = 0.5f;
    std::vector<std::pair<int, float>> volumes;   // {버스 id, 볼륨 0 ~ 2}
};

// 켜져 있거나 꺼지는 중인 스냅숏 하나.
struct ActiveSnapshot {
    int      snapshot = -1;    // Snapshot 목록의 번호
    float    weight   = 0.f;   // 0 ~ 1
    bool     on       = true;  // false = 꺼지는 중(무게가 0 이 되면 목록에서 빠짐)
    uint64_t order    = 0;     // 켠 순서(같은 우선순위끼리)
};

// 무게를 dt 만큼 옮깁니다. 꺼지는 중이고 0 에 닿았으면 true(목록에서 빼라는 뜻).
bool stepSnapshot(ActiveSnapshot& a, const Snapshot& s, float dt);
// 버스마다 1 에서 시작해 우선순위 · 켠 순서대로 v = v + (값 − v) × 무게. out 크기 = 버스 수.
void blendSnapshots(const std::vector<Snapshot>& defs, const std::vector<ActiveSnapshot>& active, std::vector<float>& out);

// ── 더킹 ────────────────────────────────────────────────────────────────────────
struct DuckRule {
    int   source      = 0;        // 이 버스에 소리가 있으면
    int   target      = 1;        // 이 버스를 낮춤
    float amountDb    = -12.f;    // 다 내려갔을 때(음수)
    float attack      = 0.1f;     // 초
    float hold        = 0.2f;
    float release     = 0.5f;
    float thresholdDb = -50.f;    // 원천 버스 출력 피크가 이것을 넘으면 "소리가 있음"
};
struct DuckState {
    float env      = 0.f;   // 0 ~ 1
    float holdLeft = 0.f;
};
// 한 틱. 돌려주는 값은 대상 버스에 곱할 게인(1 = 그대로).
float stepDuck(DuckState& st, const DuckRule& r, float sourcePeak, float dt);

// ── 피크 리미터 ─────────────────────────────────────────────────────────────────
struct LimiterState {
    float gain = 1.f;
};
// 해제 시간(초) → 프레임당 계수(gain += (1 − gain) × coef).
float limiterReleaseCoef(float releaseSec, uint32_t sampleRate);
// interleaved float 를 제자리에서 줄입니다. 출력 절댓값은 ceiling 을 넘지 않습니다.
void limitBlock(float* io, uint32_t frames, uint32_t channels, float ceiling, float releaseCoef, LimiterState& st);

// ── 사운드 큐 ───────────────────────────────────────────────────────────────────
enum class ECueOverflow : uint8_t { StopOldest = 0, RejectNew = 1 };

struct SoundCue {
    struct Variant { std::string path; float weight = 1.f; };
    std::vector<Variant> variants;
    std::string  bus;                  // 비면 지정 없음
    float        volumeMin = 1.f, volumeMax = 1.f;
    float        pitchMin  = 1.f, pitchMax  = 1.f;
    int          maxInstances = 0;     // 0 = 제한 없음
    ECueOverflow overflow     = ECueOverflow::StopOldest;
    float        minInterval  = 0.f;   // 초
    bool         hasSeed      = false;
    uint64_t     seed         = 0;

    static constexpr float kMaxVolume = 2.f;
    static constexpr float kMinPitch  = 0.25f, kMaxPitch = 4.f;
};

// .hcue 글을 읽습니다. 경로가 '/' 로 시작하지 않으면 cueDir(큐 파일이 있는 가상 폴더, 끝 '/' 없음) 기준입니다.
// 모르는 키 · 깨진 값은 messages 에 남기고 건너뜁니다. 변형이 하나도 없으면 false.
bool parseCue(std::string_view text, const std::string& cueDir, SoundCue& out, std::vector<std::string>& messages);
std::string cueToText(const SoundCue& c);   // 시험 · 도구용(읽으면 같은 값)

struct CuePick {
    int   variant = -1;
    float volume  = 1.f;
    float pitch   = 1.f;
};
// 변형(가중치) → 볼륨 → 피치 순서로 뽑습니다(D7). 변형이 없거나 가중치 합이 0 이면 variant = -1.
CuePick pickCue(const SoundCue& c, uint64_t& rng);

// ── 랜덤 ────────────────────────────────────────────────────────────────────────
uint64_t splitmix64(uint64_t& state);
float    uniform01(uint64_t& state);   // [0, 1)
uint64_t fnv1a64(std::string_view s);

// ── 구현이 짧은 것 ─────────────────────────────────────────────────────────────
inline float duckGain(const DuckState& st, const DuckRule& r) {
    if (st.env <= 0.f) return 1.f;
    return std::pow(10.f, r.amountDb * st.env / 20.f);
}

} // namespace hmix
