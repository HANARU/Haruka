// miniaudio 구현부는 MiniaudioImpl.cpp 에 있습니다. 여기서는 선언만 씁니다.
#include "miniaudio.h"

#include "AudioSystem.hpp"
#include "AudioOcclusion.hpp"
#include "AudioPropagation.hpp"
#include "AudioReverb.hpp"
#include "Verblib.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../components/AudioComponent.hpp"
#include "../components/ReverbZoneComponent.hpp"
#include "../scene/World.hpp"
#include "../vfs/Vfs.hpp"
#include "../scene/Actor.hpp"
#include "../components/CameraComponent.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

#include "../core/HLog.hpp"
#include "../core/TextUtf.hpp"

static constinit HLogCategory LogAudio{ "Audio", ELogVerbosity::Log };

// EAudioAttenuation 은 ma_attenuation_model 값을 그대로 씁니다(applySettings 가 캐스트만 합니다).
static_assert((int)EAudioAttenuation::Inverse     == (int)ma_attenuation_model_inverse);
static_assert((int)EAudioAttenuation::Linear      == (int)ma_attenuation_model_linear);
static_assert((int)EAudioAttenuation::Exponential == (int)ma_attenuation_model_exponential);

static ma_engine*      asEngine(void* p) { return static_cast<ma_engine*>(p); }
static ma_sound_group* asGroup (void* p) { return static_cast<ma_sound_group*>(p); }

// ── 음원별 노드 사슬 (2026-11-06) ────────────────────────────────────────────────
// 소리 하나를 보이스로 감쌉니다. ma_sound 가 디코딩 · 피치 · 거리 감쇠 · 패닝까지 하고, 그 뒤에 Haruka 노드를 이어 월드
// 그룹에 붙입니다.
//
//     ma_sound ──▶ 보이스 필터 ──┬─(출력 0)──▶ [HRTF 노드] ──▶ 월드 그룹 ──▶ 엔진 출력
//                               └─(출력 1)──▶ 공용 리버브 ──▶ 월드 그룹
//
// ma_sound 에는 저역 통과 필터가 없어서(엔진 노드는 fader · resampler · spatializer · panner · gainer 뿐) 차폐는 이 자리의 보이스
// 필터가 맡습니다(9-4). 차폐가 없으면(o = 0) 필터는 입력을 그대로 내보내므로, 효과가 꺼진 동안의 출력은 사운드를 그룹에 바로
// 붙이던 예전 구조와 비트 단위로 같습니다(오디오 테스트 14절이 예전 구조의 엔진과 나란히 렌더해 비교). 출력 1 은 잔향 센드(9-5)이고,
// HRTF 노드(9-6)는 HRTF 가 켜진 공간화 소리에만 사이에 들어갑니다(꺼지면 보이스 필터를 그룹에 다시 바로 붙임).
namespace {

// 보이스 필터: 1차 저역 통과 두 단(12 dB/옥타브) + 게인. 게임 스레드가 원자 값으로 목표를 넘기고, 오디오 스레드는 블록 시작에
// 한 번 읽습니다. 값마다 따로 원자적이라 한 블록 동안 옛 게인과 새 계수가 섞여 보일 수 있지만 들리지 않습니다. 게인은 블록
// 안에서 선형으로 옮겨 계단 잡음을 막고, 계수는 블록마다 바꿉니다(차폐는 0.1 s 시간 상수로 천천히 변함). 할당 · 잠금 없음.
struct VoiceFilterNode {
    ma_node_base base;   // 첫 멤버여야 ma_node* 로 다룰 수 있습니다

    // 게임 스레드 → 오디오 스레드
    std::atomic<bool>  active{ false };   // false = 그대로 통과(차폐 없음). 예전 출력과 비트 단위로 같음
    std::atomic<float> gain{ 1.f };       // 목표 게인
    std::atomic<float> coef{ 0.f };       // 1차 저역 통과 계수 a(hocc::onePoleCoef)
    std::atomic<bool>  snap{ false };     // 다음 블록에서 게인을 보간 없이 목표로(처음 판정한 값으로 시작)
    std::atomic<bool>  reset{ false };    // 다음 블록에서 필터 상태를 비움(멈춤 · 되감기)
    std::atomic<float> send{ 0.f };       // 출력 1(잔향 센드)의 목표 게인(9-5). 리버브가 멈춰 있으면 0

    // 오디오 스레드 전용
    float curGain   = 1.f;
    float curSend   = 0.f;
    bool  wasActive = false;
    float z1[2]     = { 0.f, 0.f };   // 첫째 단 상태(채널별)
    float z2[2]     = { 0.f, 0.f };   // 둘째 단 상태
};
static_assert(AudioSystem::kChannels == 2, "보이스 필터의 상태 배열은 스테레오 기준입니다");

// 입력 하나 · 출력 둘(0 = 직접음 → 월드 그룹, 1 = 잔향 센드 → 리버브), 채널 수가 같고 처리 속도도 같은 노드라 miniaudio 는
// frameCountIn == frameCountOut 으로 부릅니다. 출력이 둘이어도 입력이 있는 노드는 출력이 하나일 때와 같은 캐시 경로(480 프레임
// 단위)로 처리되고, 한쪽 출력이 읽히지 않아도(리버브가 멈춤) 다음 주기에 정상으로 다시 처리합니다(miniaudio 의 출력별 읽음 표시).
void voiceFilterProcess(ma_node* node, const float** in, ma_uint32* /*frameCountIn*/, float** out, ma_uint32* frameCountOut) {
    auto* f = static_cast<VoiceFilterNode*>(node);
    const ma_uint32 ch = ma_node_get_output_channels(node, 0);
    const ma_uint32 n  = *frameCountOut;
    if (f->reset.exchange(false, std::memory_order_acq_rel)) {
        f->z1[0] = f->z1[1] = f->z2[0] = f->z2[1] = 0.f;
        f->wasActive = false;
    }
    const bool snapNow = f->snap.exchange(false, std::memory_order_acq_rel);
    if (!f->active.load(std::memory_order_relaxed) || ch > 2 || n == 0) {
        std::memcpy(out[0], in[0], (size_t)n * ch * sizeof(float));
        f->curGain   = 1.f;
        f->wasActive = false;
    } else {
        const float target = f->gain.load(std::memory_order_relaxed);
        const float a      = f->coef.load(std::memory_order_relaxed);
        const float b      = 1.f - a;
        if (!f->wasActive) {
            // 통과하던 소리에 필터가 걸리기 시작하면 상태를 첫 입력으로 맞춰 시작 튐을 막습니다.
            for (ma_uint32 c = 0; c < ch; ++c) f->z1[c] = f->z2[c] = in[0][c];
            f->wasActive = true;
        }
        if (snapNow) f->curGain = target;
        const float g0   = f->curGain;
        const float step = (target - g0) / (float)n;
        for (ma_uint32 i = 0; i < n; ++i) {
            const float g = g0 + step * (float)(i + 1);
            for (ma_uint32 c = 0; c < ch; ++c) {
                const size_t k = (size_t)i * ch + c;
                f->z1[c] = b * in[0][k] + a * f->z1[c];
                f->z2[c] = b * f->z1[c] + a * f->z2[c];
                out[0][k] = f->z2[c] * g;
            }
        }
        f->curGain = target;
    }

    // 잔향 센드: 차폐 필터 뒤의 소리(= 거리 감쇠 · 패닝 · 차폐를 받은 소리)에 센드 게인을 곱합니다. 게인은 블록 안에서 선형으로 옮깁니다.
    const float s1 = f->send.load(std::memory_order_relaxed);
    const float s0 = f->curSend;
    if (s0 == 0.f && s1 == 0.f) {
        std::memset(out[1], 0, (size_t)n * ch * sizeof(float));
    } else {
        const float step = (s1 - s0) / (float)(n ? n : 1);
        for (ma_uint32 i = 0; i < n; ++i) {
            const float g = s0 + step * (float)(i + 1);
            for (ma_uint32 c = 0; c < ch; ++c) out[1][(size_t)i * ch + c] = out[0][(size_t)i * ch + c] * g;
        }
    }
    f->curSend = s1;
}

ma_node_vtable g_voiceFilterVtable = { voiceFilterProcess, nullptr, 1, 2, 0 };

// ── 공용 리버브 (2026-11-06, Docs/Audio_Design.md 9-5) ──────────────────────────────────
// verblib(Freeverb) 하나를 감싼 노드입니다. 입력 = 모든 보이스의 센드 출력(입력 버스 하나에 여럿이 붙으면 miniaudio 가 섞음),
// 출력 = 잔향만(wet 1 · dry 0) → 월드 그룹. **월드 그룹 안쪽**에 두어, 일시정지(그룹 정지) 동안 리버브도 당겨지지 않아 꼬리가 그대로
// 얼었다가 재개하면 이어집니다(밖에 두면 일시정지 중에도 꼬리가 1 ~ 2 초 울림).
// 매개변수는 게임 스레드가 원자 값으로 넘기고, 오디오 스레드가 블록 시작에 바뀐 것만 verblib 에 넣습니다(verblib_set_* 는 잠금이 없어
// 게임 스레드에서 부르면 처리 중인 오디오 스레드와 겹침). 잔향 크기는 블록 안에서 선형으로 옮깁니다. 비우기는 verblib_initialize 를
// 다시 부르는 것입니다(verblib_mute 는 static). 입력이 없어도 꼬리를 처리하도록 CONTINUOUS_PROCESSING(입력 없으면 무음이 들어옴).
struct ReverbNode {
    ma_node_base base;   // 첫 멤버

    std::atomic<float> roomSize{ 0.5f };
    std::atomic<float> damping{ 0.5f };
    std::atomic<float> width{ 1.f };
    std::atomic<float> level{ 0.f };
    std::atomic<bool>  reset{ true };

    // 오디오 스레드 전용
    verblib rev{};          // 약 199 KiB(Verblib.hpp 의 배율 2)
    float   appliedRoom  = -1.f;
    float   appliedDamp  = -1.f;
    float   appliedWidth = -1.f;
    float   curLevel     = 0.f;
};

void reverbProcess(ma_node* node, const float** in, ma_uint32* /*frameCountIn*/, float** out, ma_uint32* frameCountOut) {
    auto* r = static_cast<ReverbNode*>(node);
    const ma_uint32 n = *frameCountOut;
    if (r->reset.exchange(false, std::memory_order_acq_rel)) {
        verblib_initialize(&r->rev, AudioSystem::kSampleRate, AudioSystem::kChannels);   // 버퍼를 비우고 기본값(wet 1 · dry 0)
        r->appliedRoom = r->appliedDamp = r->appliedWidth = -1.f;
        r->curLevel    = 0.f;
    }
    const float room  = r->roomSize.load(std::memory_order_relaxed);
    const float damp  = r->damping.load(std::memory_order_relaxed);
    const float width = r->width.load(std::memory_order_relaxed);
    if (room  != r->appliedRoom)  { verblib_set_room_size(&r->rev, room); r->appliedRoom  = room;  }
    if (damp  != r->appliedDamp)  { verblib_set_damping(&r->rev, damp);   r->appliedDamp  = damp;  }
    if (width != r->appliedWidth) { verblib_set_width(&r->rev, width);    r->appliedWidth = width; }
    verblib_process(&r->rev, in[0], out[0], n);
    const float l1   = r->level.load(std::memory_order_relaxed);
    const float l0   = r->curLevel;
    const float step = (l1 - l0) / (float)(n ? n : 1);
    for (ma_uint32 i = 0; i < n; ++i) {
        const float g = l0 + step * (float)(i + 1);
        out[0][(size_t)i * 2]     *= g;
        out[0][(size_t)i * 2 + 1] *= g;
    }
    r->curLevel = l1;
}

ma_node_vtable g_reverbVtable = { reverbProcess, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING };

ReverbNode* asReverb(void* p) { return static_cast<ReverbNode*>(p); }

// ── HRTF 노드 (2026-11-06, Docs/Audio_Design.md 9-6) ──────────────────────────────────
// 보이스 필터의 직접음(출력 0)을 받아 HRTF 로 왼 · 오른 귀 소리를 만들어 월드 그룹에 넣습니다. HRTF 가 켜진 공간화 소리에만 사슬에
// 들어갑니다.
//  - 입력은 패닝을 끈 공간화기 출력이라 모노 음원이면 두 채널이 같습니다. 평균을 내 모노로 넣습니다(스테레오 음원은 한 점으로 모음,
//    Steam Audio 도 2채널 입력을 같은 위치로 처리).
//  - 구현은 kFrameSize 단위로만 처리하므로 FIFO 로 모읍니다. 입력 한 프레임을 넣을 때마다 지난 블록의 결과 한 프레임을 내보내고,
//    kFrameSize 가 차면 처리합니다. 지연은 정확히 kFrameSize 이고, miniaudio 가 부르는 크기와 상관없이 출력이 같습니다.
//  - 소리가 끝나거나 멈추면 입력이 끊깁니다(보이스 필터가 0 프레임을 냄). 보통 노드처럼 그때 처리를 건너뛰면 FIFO 에 남은
//    kFrameSize 와 필터 꼬리가 잘려 끝에서 튀므로, CONTINUOUS_PROCESSING · ALLOW_NULL_INPUT 으로 입력이 없어도 불리게 합니다. 남은
//    출력(FIFO · 구현이 알려 준 꼬리)을 다 낸 뒤에는 구현을 부르지 않고 무음만 내므로(사슬에서 뺀 노드는 0 프레임), 멈춘 소리는
//    HRTF 계산을 하지 않습니다.
//  - 일시정지(그룹 정지) 동안은 읽히지 않아 FIFO 가 그대로 있다가 재개하면 이어집니다. Stop(되감기)은 reset 으로 비웁니다.
struct HrtfNode {
    ma_node_base base;   // 첫 멤버

    // 게임 스레드 → 오디오 스레드. 방향은 성분마다 따로 원자적이라 처리할 때 다시 정규화합니다(섞여 보여도 이웃한 두 틱 사이 방향).
    std::atomic<float>    dirX{ 0.f }, dirY{ 0.f }, dirZ{ -1.f };
    std::atomic<float>    blend{ 1.f };
    std::atomic<bool>     reset{ false };
    std::atomic<uint64_t> blocks{ 0 };   // 처리한 블록 수(시험 · 진단)
    hhrtf::Renderer*      renderer = nullptr;   // AudioVoice 가 소유하고, 이 노드를 정리한 뒤에 지웁니다
    float                 gain     = 1.f;       // Backend::outputGain

    // 오디오 스레드 전용
    uint32_t pos  = 0;       // FIFO 위치
    bool     live = false;   // 아직 0 이 아닌 출력이 나올 수 있는지
    bool     tail = false;   // 마지막 처리에서 구현이 꼬리가 남았다고 했는지
    float    in  [hhrtf::kFrameSize] = {};
    float    outL[hhrtf::kFrameSize] = {};
    float    outR[hhrtf::kFrameSize] = {};
};

bool anyNonZero(const float* p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i)
        if (p[i] != 0.f) return true;
    return false;
}

void hrtfProcess(ma_node* node, const float** in, ma_uint32* frameCountIn, float** out, ma_uint32* frameCountOut) {
    constexpr uint32_t F = hhrtf::kFrameSize;
    auto* h = static_cast<HrtfNode*>(node);
    if (h->reset.exchange(false, std::memory_order_acq_rel)) {
        h->pos  = 0;
        h->live = h->tail = false;
        std::memset(h->in,   0, sizeof h->in);
        std::memset(h->outL, 0, sizeof h->outL);
        std::memset(h->outR, 0, sizeof h->outR);
        h->renderer->reset();
    }
    // 입력: 붙은 노드가 없으면(사슬에서 뺌) miniaudio 가 NULL 을 줍니다(ALLOW_NULL_INPUT). 붙어 있으면 그 노드가 아무것도 내지 않아도
    // (소리가 끝남 · 멈춤) 0 으로 채운 버퍼를 줍니다. 둘 다 "들어온 소리 없음" 입니다.
    const ma_uint32 n   = *frameCountOut;
    const float*    src = in ? in[0] : nullptr;
    const bool      silentIn = !src || !anyNonZero(src, n * 2);
    if (silentIn && !h->live) {
        // 남은 출력도 없음: 구현을 부르지 않습니다(FIFO · 꼬리가 모두 0 이라 처리해도 0). NULL 이면 0 프레임을 돌려주고(둘 다 0 이어야
        // miniaudio 의 처리 고리가 끝남, 섞이는 것 없음), 입력이 붙어 있으면 그 입력을 쓴 것으로 하고 무음을 냅니다(0 프레임을 내고
        // 입력만 쓰면 miniaudio 가 같은 주기에 입력을 또 읽어 고리가 끝나지 않음).
        if (!src) {
            *frameCountIn  = 0;
            *frameCountOut = 0;
        } else {
            std::memset(out[0], 0, (size_t)n * 2 * sizeof(float));
        }
        return;
    }
    float* o = out[0];
    for (ma_uint32 i = 0; i < n;) {
        const ma_uint32 k = std::min<ma_uint32>(n - i, F - h->pos);
        for (ma_uint32 j = 0; j < k; ++j) {
            const size_t f = (size_t)(i + j) * 2;
            h->in[h->pos + j] = src ? 0.5f * (src[f] + src[f + 1]) : 0.f;
            o[f]     = h->outL[h->pos + j] * h->gain;
            o[f + 1] = h->outR[h->pos + j] * h->gain;
        }
        h->pos += k;
        i      += k;
        if (h->pos == F) {
            glm::vec3 d(h->dirX.load(std::memory_order_relaxed), h->dirY.load(std::memory_order_relaxed),
                        h->dirZ.load(std::memory_order_relaxed));
            const float len = glm::length(d);
            d = len > 1e-6f ? d / len : glm::vec3(0.f, 0.f, -1.f);
            const float b = std::clamp(h->blend.load(std::memory_order_relaxed), 0.f, 1.f);
            // 무음 블록은 남은 꼬리만 꺼냅니다(Steam Audio 는 처리 함수로는 꼬리의 끝을 알려 주지 않음). 같은 출력이고, 꼬리가 끝나면
            // false 가 와서 노드가 쉴 수 있습니다.
            h->tail = anyNonZero(h->in, F) ? h->renderer->process(h->in, h->outL, h->outR, d, b)
                                           : h->renderer->tail(h->outL, h->outR);
            h->pos  = 0;
            h->blocks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // 소리가 들어오면 계속 살아 있고, 무음이 들어오면 꼬리 · FIFO 에 남은 값이 모두 0 이 될 때까지만 처리합니다.
    h->live = !silentIn || h->tail || anyNonZero(h->in, h->pos) ||
              anyNonZero(h->outL + h->pos, F - h->pos) || anyNonZero(h->outR + h->pos, F - h->pos);
}

ma_node_vtable g_hrtfVtable = { hrtfProcess, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT };

} // namespace

struct AudioVoice {
    ma_sound        sound{};
    VoiceFilterNode filter{};
    bool            soundOk  = false;
    bool            filterOk = false;

    // HRTF(게임 스레드, 9-6). 노드는 처음 HRTF 가 필요할 때 만들어 보이스가 없어질 때까지 둡니다(끄면 사슬에서만 뺌).
    std::unique_ptr<HrtfNode>        hrtf;
    std::unique_ptr<hhrtf::Renderer> hrtfRenderer;   // 노드가 쓰는 상태. destroyVoice 가 노드를 그래프에서 정리한 뒤에 지웁니다
    bool hrtfRouted = false;   // 보이스 필터의 출력 0 이 HRTF 노드에 붙어 있는지
    bool hrtfFailed = false;   // 만들기에 실패함(이 보이스는 패닝으로 둠)

    // 차폐 상태(게임 스레드 전용, 9-4)
    float occl       = 0.f;     // 지금 차폐 정도(평활 뒤)
    float occlTarget = 0.f;     // 마지막 판정 값
    float retrace    = 0.f;     // 다음 판정까지 남은 시간(초)
    bool  traced     = false;   // 이번 재생에서 판정했는지(멈추면 다시 false: 다음 틱에 바로 판정하고 그 값으로 시작)

    // 가상화(게임 스레드 전용, S3): 들리지 않는 동안 멈춰 두고 재생 위치(원본 샘플률의 프레임)만 셉니다.
    bool   virtualized   = false;
    double virtualCursor = 0.0;

    // 전파(게임 스레드 전용, S1 · S5)
    glm::vec3 prevPos{ 0.f };
    glm::vec3 velocity{ 0.f };
    bool      hasPrev    = false;
    float     pitch      = 1.f;   // 마지막으로 넣은 도플러 배율(1 이면 넣지 않음)
    uint32_t  traceCount = 0;     // 표본 회전 위상
    // 사운드 단계 4
    bool      budgetCulled = false;   // 보이스 예산 밖(지난 틱 순위): 가상화
    bool      coneSet      = false;   // 콘을 한 번이라도 넣었는지(끄면 기본으로 되돌림)
};

// ── 파일 읽기 (2026-10-24) ───────────────────────────────────────────────────────
// miniaudio 의 리소스 관리자가 소리 파일을 열 때 가상 파일 시스템을 거칩니다(에디터는 프로젝트 폴더, 게임은 묶음 파일).
// 소리는 통째로 디코딩하므로(STREAM 플래그 없음) 열 때 파일 전체를 메모리로 읽어 둡니다. 리소스 관리자의 작업 스레드가
// 부를 수 있고 hvfs 읽기는 여러 스레드에서 불러도 됩니다. 파일 하나는 한 스레드만 씁니다(miniaudio 규칙).
namespace {

struct VfsFile {
    std::vector<uint8_t> data;
    size_t               pos = 0;
};

// 이 콜백들은 miniaudio(C 코드)가 리소스 관리자의 잠금을 쥔 채 부릅니다. 예외가 이 경계를 넘으면 잠금이 풀리지 않은 채
// 프로그램이 끝나므로(DEF-123), 여기서 모두 받아 오류 코드로 바꿉니다. hvfs 읽기는 던지지 않지만 할당은 던질 수 있습니다.
ma_result vfsOpen(ma_vfs*, const char* path, ma_uint32 mode, ma_vfs_file* out) {
    if (!out || !path) return MA_INVALID_ARGS;
    *out = nullptr;
    if (mode & MA_OPEN_MODE_WRITE) return MA_ACCESS_DENIED;
    try {
        auto f = std::make_unique<VfsFile>();
        std::string err;
        if (!hvfs::readAll(path, f->data, &err)) {
            HLOG(LogAudio, Warning) << "Sound file not opened: " << err;
            return MA_DOES_NOT_EXIST;
        }
        *out = f.release();
        return MA_SUCCESS;
    } catch (...) {
        return MA_OUT_OF_MEMORY;
    }
}
ma_result vfsOpenW(ma_vfs* vfs, const wchar_t* path, ma_uint32 mode, ma_vfs_file* out) {
#ifdef _WIN32
    try {
        return vfsOpen(vfs, hutf::fromUtf16(path).c_str(), mode, out);   // Windows 의 wchar_t 는 UTF-16
    } catch (...) {
        return MA_OUT_OF_MEMORY;
    }
#else
    (void)vfs; (void)path; (void)mode; (void)out;
    return MA_NOT_IMPLEMENTED;
#endif
}
ma_result vfsClose(ma_vfs*, ma_vfs_file file) {
    delete static_cast<VfsFile*>(file);
    return MA_SUCCESS;
}
ma_result vfsRead(ma_vfs*, ma_vfs_file file, void* dst, size_t bytes, size_t* read) {
    auto* f = static_cast<VfsFile*>(file);
    const size_t n = std::min(bytes, f->data.size() - f->pos);
    if (n) std::memcpy(dst, f->data.data() + f->pos, n);
    f->pos += n;
    if (read) *read = n;
    return (n == 0 && bytes > 0) ? MA_AT_END : MA_SUCCESS;
}
ma_result vfsWrite(ma_vfs*, ma_vfs_file, const void*, size_t, size_t* written) {
    if (written) *written = 0;
    return MA_ACCESS_DENIED;
}
ma_result vfsSeek(ma_vfs*, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin) {
    auto* f = static_cast<VfsFile*>(file);
    const ma_int64 base = origin == ma_seek_origin_start ? 0
                        : origin == ma_seek_origin_current ? (ma_int64)f->pos : (ma_int64)f->data.size();
    const ma_int64 to = base + offset;
    if (to < 0 || to > (ma_int64)f->data.size()) return MA_BAD_SEEK;
    f->pos = (size_t)to;
    return MA_SUCCESS;
}
ma_result vfsTell(ma_vfs*, ma_vfs_file file, ma_int64* cursor) {
    *cursor = (ma_int64)static_cast<VfsFile*>(file)->pos;
    return MA_SUCCESS;
}
ma_result vfsInfo(ma_vfs*, ma_vfs_file file, ma_file_info* info) {
    info->sizeInBytes = static_cast<VfsFile*>(file)->data.size();
    return MA_SUCCESS;
}

ma_vfs_callbacks g_vfs = { vfsOpen, vfsOpenW, vfsClose, vfsRead, vfsWrite, vfsSeek, vfsTell, vfsInfo };

} // namespace

// ── 출력 장치 (2026-10-16) ───────────────────────────────────────────────────────
// 엔진이 장치를 직접 만들게 두지 않고(ma_engine_config::pDevice) 여기서 만든 장치에 연결합니다. 장치를 바꿀 때 엔진과
// 사운드는 그대로 두고 장치만 닫았다 같은 자리에 다시 열면 되기 때문입니다. 장치 형식은 언제나 f32 · 2채널 · 48kHz 로
// 열어(실제 장치 형식과 다르면 miniaudio 가 변환) 엔진이 처음 본 형식이 바뀌지 않게 합니다.
namespace {
struct OutputState {
    ma_context  ctx{};
    ma_device   dev{};
    bool        devOk = false;
    std::string activeId;     // 연 장치(빈 문자열 = 시스템 기본)
    std::string activeName;
};
OutputState* asOut(void* p) { return static_cast<OutputState*>(p); }

// WASAPI 는 끝점 ID(예: "{0.0.0.00000000}.{GUID}")가 장치를 정확히 가리키고, OpenXR 런타임이 알려 주는 헤드셋 장치 ID 와
// 같은 형식입니다. 다른 백엔드는 장치 이름을 씁니다.
std::string deviceIdString(const ma_context& ctx, const ma_device_info& info) {
    if (ctx.backend == ma_backend_wasapi) return hutf::fromUtf16(info.id.wasapi);
    return info.name;
}

void deviceData(ma_device* d, void* out, const void* /*in*/, ma_uint32 frames) {
    ma_engine_read_pcm_frames(static_cast<ma_engine*>(d->pUserData), out, frames, nullptr);
}

// want 가 가리키는 장치 하나를 엽니다(시작은 하지 않음). 비었으면 시스템 기본 장치입니다. 목록에 없으면 열지 않고
// missing 을 참으로, 열기가 실패하면(다른 앱의 독점 모드 등) error 에 이유를 남기고 false 를 돌려줍니다.
bool openDevice(OutputState& o, ma_engine* eng, const std::string& want, bool& missing, std::string& error) {
    missing = false;
    error.clear();
    ma_device_id id{};
    bool haveId = false;
    if (!want.empty()) {
        ma_device_info* infos = nullptr;
        ma_uint32       count = 0;
        if (ma_context_get_devices(&o.ctx, &infos, &count, nullptr, nullptr) == MA_SUCCESS)
            for (ma_uint32 i = 0; i < count && !haveId; ++i)
                if (hutf::equalsIgnoreCase(deviceIdString(o.ctx, infos[i]), want)) { id = infos[i].id; haveId = true; }
        if (!haveId) { missing = true; return false; }
    }
    ma_device_config dc = ma_device_config_init(ma_device_type_playback);
    dc.playback.pDeviceID        = haveId ? &id : nullptr;
    dc.playback.format           = ma_format_f32;
    dc.playback.channels         = AudioSystem::kChannels;
    dc.sampleRate                = AudioSystem::kSampleRate;
    dc.dataCallback              = deviceData;
    dc.pUserData                 = eng;
    dc.noPreSilencedOutputBuffer = MA_TRUE;   // 콜백이 언제나 모든 프레임을 채웁니다(엔진 기본 장치와 같은 설정)
    dc.noClip                    = MA_TRUE;   // 엔진이 직접 자릅니다
    if (const ma_result r = ma_device_init(&o.ctx, &dc, &o.dev); r != MA_SUCCESS) {
        error = ma_result_description(r);
        return false;
    }
    o.devOk      = true;
    o.activeId   = want;
    o.activeName = o.dev.playback.name;
    return true;
}

// 후보를 앞에서부터 열어 처음 열린 것을 씁니다(AudioSystem::outputCandidates 순서). 목록에 없거나 열리지 않은 후보는
// 경고하고 다음 후보로 갑니다. 예전에는 고른 장치가 열리지 않으면 시스템 기본으로 돌아가지 않았고(DEF-146), 헤드셋 장치가
// 목록에 없으면 고른 장치를 건너뛰고 시스템 기본으로 열었습니다(DEF-147). quiet 면 경고를 남기지 않습니다(다시 열기 재시도).
bool openPreferred(OutputState& o, ma_engine* eng, const std::vector<std::string>& candidates, bool quiet) {
    for (size_t k = 0; k < candidates.size(); ++k) {
        bool        missing = false;
        std::string error;
        if (openDevice(o, eng, candidates[k], missing, error)) return true;
        if (quiet) continue;
        const std::string what = candidates[k].empty() ? std::string("system default") : candidates[k];
        if (missing) {
            HLOG(LogAudio, Warning) << "Audio output device not found: " << what;
        } else {
            HLOG(LogAudio, Warning) << "Audio output device could not be opened: " << what << " (" << error << ")";
        }
        if (k + 1 < candidates.size()) {
            HLOG(LogAudio, Warning) << "  trying " << (candidates[k + 1].empty() ? std::string("the system default") : candidates[k + 1]);
        }
    }
    return false;
}
} // namespace

std::vector<std::string> AudioSystem::outputCandidates(const std::string& overrideId, const std::string& requestedId) {
    std::vector<std::string> c;
    if (!overrideId.empty()) c.push_back(overrideId);
    if (!requestedId.empty() && !hutf::equalsIgnoreCase(requestedId, overrideId)) c.push_back(requestedId);
    c.push_back(std::string());   // 시스템 기본
    return c;
}

void AudioSystem::startup() {
    auto* eng = new ma_engine();
    ma_engine_config cfg = ma_engine_config_init();
    cfg.channels   = kChannels;
    cfg.sampleRate = kSampleRate;
    cfg.noDevice   = headless_ ? MA_TRUE : MA_FALSE;
    cfg.pResourceManagerVFS = &g_vfs;   // 소리 파일을 가상 파일 시스템으로 엶(2026-10-24)

    OutputState* out = nullptr;
    if (!headless_) {
        out = new OutputState();
        if (const ma_result r = ma_context_init(nullptr, 0, nullptr, &out->ctx); r != MA_SUCCESS) {
            HLOG(LogAudio, Error) << "ma_context_init failed: " << ma_result_description(r);
            delete out;
            delete eng;
            return;
        }
        if (!openPreferred(*out, eng, outputCandidates(overrideId_, requestedId_), false)) {
            HLOG(LogAudio, Error) << "No audio output (no device could be opened)";
            ma_context_uninit(&out->ctx);
            delete out;
            delete eng;
            return;
        }
        cfg.pDevice = &out->dev;   // 엔진이 이 장치를 시작합니다(noAutoStart 꺼짐)
    }

    if (const ma_result r = ma_engine_init(&cfg, eng); r != MA_SUCCESS) {
        HLOG(LogAudio, Error) << "ma_engine_init failed: " << ma_result_description(r);
        if (out) {
            ma_device_uninit(&out->dev);
            ma_context_uninit(&out->ctx);
            delete out;
        }
        delete eng;
        return;
    }
    engine_ = eng;
    out_    = out;

    // 그룹은 기본으로 재생 상태이고 공간화가 꺼져 있어, 섞인 소리를 다시 감쇠하지 않습니다.
    // 피치도 끕니다(NO_PITCH). 켜 두면 그룹이 섞인 소리 전체를 배율 1 로 리샘플링하느라
    // 연산을 쓰고, 그 리샘플러가 한 프레임 늦게 내보내서 멈춘 소리의 마지막 샘플이 다음
    // 블록 첫머리에 남습니다(HarukaAudioTest 5절이 잡았습니다).
    auto* grp = new ma_sound_group();
    if (const ma_result r = ma_sound_group_init(eng, MA_SOUND_FLAG_NO_PITCH, nullptr, grp); r != MA_SUCCESS) {
        // 그룹이 없으면 사운드는 엔진 출력에 바로 붙습니다. 재생은 되고 일시정지만 듣지 않습니다.
        HLOG(LogAudio, Error) << "world sound group init failed: " << ma_result_description(r)
                              << " (pause will not silence sounds)";
        delete grp;
    } else {
        group_ = grp;
    }

    // 공용 리버브(2026-11-06, 9-5): 월드 그룹 안쪽에 멈춘 채로 만들어 둡니다. 청취자가 잔향 구역에 들어가면 tick 이 시작합니다.
    // 그룹 입력 목록에서 리버브가 맨 앞이지만, 멈춘 노드는 읽히지 않아 잔향이 없는 동안의 출력은 리버브가 없던 때와 같습니다.
    if (group_) {
        auto* rv = new ReverbNode();
        ma_node_config nc = ma_node_config_init();
        nc.vtable       = &g_reverbVtable;
        nc.initialState = ma_node_state_stopped;
        const ma_uint32 ch = kChannels;
        nc.pInputChannels  = &ch;
        nc.pOutputChannels = &ch;
        if (!verblib_initialize(&rv->rev, kSampleRate, kChannels)) {
            HLOG(LogAudio, Error) << "reverb init failed (verblib); reverb zones will be silent";
            delete rv;
        } else if (const ma_result r = ma_node_init(ma_engine_get_node_graph(eng), &nc, nullptr, &rv->base); r != MA_SUCCESS) {
            HLOG(LogAudio, Error) << "reverb node init failed: " << ma_result_description(r) << "; reverb zones will be silent";
            delete rv;
        } else if (const ma_result r2 = ma_node_attach_output_bus(&rv->base, 0, grp, 0); r2 != MA_SUCCESS) {
            HLOG(LogAudio, Error) << "reverb attach failed: " << ma_result_description(r2) << "; reverb zones will be silent";
            ma_node_uninit(&rv->base, nullptr);
            delete rv;
        } else {
            reverb_ = rv;
        }
    }
    HLOG(LogAudio, Log) << "miniaudio initialized (48 kHz stereo" << (headless_ ? ", no device" : "") << ")";
    if (out) HLOG(LogAudio, Log) << "Audio output: " << out->activeName << (out->activeId.empty() ? " (system default)" : "");
}

void AudioSystem::shutdown() {
    if (!engine_) return;
    // 장치 콜백(오디오 스레드)이 엔진을 읽지 않도록 먼저 멈춥니다.
    OutputState* out = asOut(out_);
    if (out && out->devOk) ma_device_stop(&out->dev);
    // 엔진보다 사운드를 먼저 정리합니다. World 는 Engine 이 소멸할 때 컴포넌트 onDestroy 없이
    // 해제되므로, 여기서 정리하지 않으면 해제된 엔진에 붙은 사운드와 사라질 이 객체를 가리키는
    // audioSys_ 가 컴포넌트에 남습니다.
    releaseWorldSounds();
    // 리버브는 모든 보이스(센드가 리버브 입력에 붙음) 뒤, 그룹(리버브 출력이 붙은 곳) 앞에 정리합니다.
    if (reverb_) {
        ma_node_uninit(&asReverb(reverb_)->base, nullptr);
        delete asReverb(reverb_);
        reverb_        = nullptr;
        reverbRunning_ = false;
    }
    if (group_) {
        ma_sound_group_uninit(asGroup(group_));
        delete asGroup(group_);
        group_ = nullptr;
    }
    ma_engine_uninit(asEngine(engine_));   // 장치는 엔진 것이 아니므로 아래에서 직접 닫습니다
    delete asEngine(engine_);
    engine_ = nullptr;
    if (out) {
        if (out->devOk) ma_device_uninit(&out->dev);
        ma_context_uninit(&out->ctx);
        delete out;
        out_ = nullptr;
    }
    paused_ = false;
}

std::vector<AudioOutputDevice> AudioSystem::outputDevices() const {
    std::vector<AudioOutputDevice> list;
    OutputState* out = asOut(out_);
    if (!out) return list;
    ma_device_info* infos = nullptr;
    ma_uint32       count = 0;
    if (ma_context_get_devices(&out->ctx, &infos, &count, nullptr, nullptr) != MA_SUCCESS) return list;
    for (ma_uint32 i = 0; i < count; ++i)
        list.push_back({ deviceIdString(out->ctx, infos[i]), infos[i].name, infos[i].isDefault != 0 });
    return list;
}

void AudioSystem::setOutputDevice(const std::string& id) {
    requestedId_ = id;
    reopenDevice();
}

void AudioSystem::setOutputOverride(const std::string& id) {
    overrideId_ = id;
    reopenDevice();
}

std::string AudioSystem::activeOutputName() const {
    const OutputState* out = asOut(out_);
    return (out && out->devOk) ? out->activeName : std::string();
}

bool AudioSystem::overrideActive() const {
    const OutputState* out = asOut(out_);
    return out && out->devOk && !overrideId_.empty() && hutf::equalsIgnoreCase(out->activeId, overrideId_);
}

bool AudioSystem::reopenDevice(bool quiet) {
    OutputState* out = asOut(out_);
    if (!out || !engine_) return false;   // headless 이거나 시작 전: 값만 기억했다가 시작할 때 엽니다
    const std::string& want = overrideId_.empty() ? requestedId_ : overrideId_;
    // 원하는 장치가 이미 열려 돌고 있으면 그대로 둡니다. 멈춘 장치(실행 중에 뽑힘 등)는 같은 장치라도 다시 엽니다. 예전에는
    // 장치 ID 만 비교해서, 잃은 장치를 대화상자에서 다시 골라도 열지 않았습니다(DEF-145).
    if (out->devOk && hutf::equalsIgnoreCase(out->activeId, want) && ma_device_is_started(&out->dev)) return true;
    // 같은 ma_device 자리를 닫고 다시 엽니다. 엔진은 이 주소를 들고 있고, 형식이 같으므로 엔진 · 사운드는 그대로입니다.
    if (out->devOk) {
        ma_device_uninit(&out->dev);
        out->devOk = false;
    }
    if (!openPreferred(*out, asEngine(engine_), outputCandidates(overrideId_, requestedId_), quiet)) {
        if (!quiet) { HLOG(LogAudio, Error) << "No audio output (no device could be opened)"; }
        return false;
    }
    if (const ma_result r = ma_device_start(&out->dev); r != MA_SUCCESS) {
        if (!quiet) { HLOG(LogAudio, Error) << "Audio output device could not start: " << ma_result_description(r); }
        return false;   // 열린 채 멈춘 장치는 Play 중 tick 이 간격을 두고 다시 엽니다
    }
    HLOG(LogAudio, Log) << "Audio output: " << out->activeName
                        << (out->activeId.empty() ? " (system default)" : "")
                        << (overrideActive() ? " (temporary)" : "");
    return true;
}

// 실행 중에 장치를 잃으면(USB · 블루투스 장치를 뽑음 등) miniaudio 는 장치를 멈춥니다. 시스템 기본 장치는 miniaudio 가
// 새 기본 장치로 옮겨 주지만, 직접 고른 장치는 멈춘 채 남습니다. 간격을 두고 다시 열어, 그 장치가 없으면 다음 후보(고른
// 장치 → 시스템 기본)로 소리를 잇습니다(DEF-145). 장치가 돌고 있으면 목록을 훑지 않으므로 평소 비용은 상태 읽기 하나입니다.
void AudioSystem::checkOutput(float dt) {
    OutputState* out = asOut(out_);
    if (!out || !engine_) return;
    if (out->devOk && ma_device_is_started(&out->dev)) {
        if (outputLost_) {
            outputLost_ = false;
            const std::string& want = overrideId_.empty() ? requestedId_ : overrideId_;
            HLOG(LogAudio, Log) << "Audio output restored: " << out->activeName
                                << (hutf::equalsIgnoreCase(out->activeId, want) ? "" : " (the chosen device is unavailable)");
        }
        outputRetry_ = 0.f;
        return;
    }
    if (!outputLost_) {
        outputLost_  = true;
        outputRetry_ = 0.f;   // 처음 알아챈 프레임에 바로 한 번
        HLOG(LogAudio, Warning) << "Audio output stopped" << (out->devOk ? " (device lost?): " + out->activeName : std::string())
                                << "; reopening every " << kOutputRetrySec << " s";
    }
    outputRetry_ -= dt;
    if (outputRetry_ > 0.f) return;
    outputRetry_ = kOutputRetrySec;
    reopenDevice(/*quiet*/ true);   // 성공하면 다음 프레임에 "restored" 를 남깁니다
}

void AudioSystem::releaseWorldSounds() {
    releaseOneShots();
    if (!world_) return;
    for (auto& ap : world_->getActors())
        if (auto* ac = ap->getComponent<AudioComponent>(); ac && ac->voice_) {
            destroyVoice(ac->voice_);
            ac->voice_    = nullptr;
            ac->audioSys_ = nullptr;
        }
}

void AudioSystem::updateListener() {
    glm::vec3 pos, fwd, up;
    CameraComponent* cam = world_->getActiveCamera();
    // 차폐 레이에서 뺄 청취자 쪽 액터입니다. XR 이면 카메라는 XR 원점(리그)이고, 머리는 그 아래에 있습니다.
    listenerActor_ = cam ? cam->owner : nullptr;
    if (hasListenerPose_) {
        // XR 세션 동안의 머리 자세. 카메라와 같은 규약(-Z 정면, +Y 위)입니다.
        pos = glm::vec3(listenerPose_[3]);
        fwd = -glm::normalize(glm::vec3(listenerPose_[2]));
        up  =  glm::normalize(glm::vec3(listenerPose_[1]));
    } else {
        if (!cam || !cam->owner) return;
        pos = cam->owner->worldPosition();
        fwd = cam->forward();
        up  = cam->up();
    }
    listenerPos_ = pos;
    listenerFwd_ = fwd;   // HRTF 방향의 기준(miniaudio 청취자와 같은 값)
    listenerUp_  = up;
    ma_engine_listener_set_position(asEngine(engine_), 0, pos.x, pos.y, pos.z);
    // 예전에는 위치만 넘겨서 청취자가 늘 -Z 를 보고 있었고, 카메라를 돌려도 소리의 좌우가
    // 바뀌지 않았습니다. miniaudio 는 방향 × 위 벡터로 오른쪽을 정합니다. 카메라 자세가 액터
    // 회전이 된 뒤로(DEF-21) 롤이 있을 수 있으므로 월드 +Y 가 아니라 카메라의 위 벡터를 넘깁니다.
    ma_engine_listener_set_direction(asEngine(engine_), 0, fwd.x, fwd.y, fwd.z);
    ma_engine_listener_set_world_up(asEngine(engine_), 0, up.x, up.y, up.z);
}

void AudioSystem::applySettings(AudioVoice& voice, const AudioComponent& ac) const {
    ma_sound* s = &voice.sound;
    const AudioAttenuationSettings& at = ac.attenuation;
    ma_sound_set_volume(s, ac.volume * busVolume(ac.bus));   // 버스 볼륨(사운드 단계 4, 기본 1)
    ma_sound_set_looping(s, ac.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(s, at.spatialize ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(s, (ma_attenuation_model)at.model);
    ma_sound_set_min_distance(s, at.minDistance);
    ma_sound_set_max_distance(s, at.maxDistance);
    ma_sound_set_rolloff(s, at.rolloff);
}

void AudioSystem::tick(float dt) {
    if (!engine_) return;
    checkOutput(dt);
    if (!world_) return;
    updateListener();
    // 청취자 속도(도플러, S1). 처음 틱은 속도 0.
    if (listenerHasPrev_) listenerVel_ = hprop::trackVelocity(listenerPrevPos_, listenerPos_, dt, listenerVel_);
    listenerPrevPos_ = listenerPos_;
    listenerHasPrev_ = true;
    updateReverb(dt);   // 청취자 위치로 잔향 구역을 고르고 리버브를 따라가게(9-5)

    std::vector<std::pair<AudioVoice*, float>> budget;   // 보이스 예산 순위용(재생 중인 3D 소리와 들리는 크기)
    for (AudioComponent* ac : world_->componentsOf<AudioComponent>()) {
        Actor* a = ac->owner;
        // 실효 활성(컴포넌트 enabled && 액터 activeInHierarchy)일 때만 다룹니다. 렌더·틱과
        // 같은 기준이며, 꺼질 때 나오던 소리는 AudioComponent::onDisable 이 멈춥니다.
        if (!ac->isLive()) continue;

        bool created = false;
        if (!ac->voice_ && !ac->filePath.empty() && !ac->loadFailed_) {
            if (AudioVoice* v = createVoice(ac->filePath.c_str(), ac->loop, ac->stream)) {
                ac->voice_    = v;
                ac->audioSys_ = this;
                created       = true;
            } else {
                ac->loadFailed_ = true;   // 다음 플레이 시작(onBeginPlay)까지 다시 시도하지 않습니다
            }
        }
        if (!ac->voice_) continue;

        const glm::vec3 pos = a->worldPosition();
        ma_sound_set_position(&ac->voice_->sound, pos.x, pos.y, pos.z);
        applySettings(*ac->voice_, *ac);
        // 최대 거리 밖 무음(silentBeyondMax): miniaudio 곡선에 배율을 곱해 최대 거리에서 0 이 되게 합니다.
        if (ac->attenuation.spatialize && ac->attenuation.silentBeyondMax)
            ma_sound_set_volume(&ac->voice_->sound, ac->volume * busVolume(ac->bus) *
                                hvirt::silenceFactor(ac->attenuation, glm::length(pos - listenerPos_)));
        // 지향성 콘(사운드 단계 4): 액터 앞쪽(-Z). 쓰지 않는 소리는 한 번도 부르지 않습니다(예전 출력 그대로).
        if (ac->cone.enabled() || ac->voice_->coneSet) {
            const bool on = ac->cone.enabled();
            ma_sound_set_cone(&ac->voice_->sound, on ? glm::radians(ac->cone.innerDeg) : 6.2831853f,
                              on ? glm::radians(std::max(ac->cone.outerDeg, ac->cone.innerDeg)) : 6.2831853f,
                              on ? ac->cone.outerGain : 0.f);
            const glm::vec3 fwd = a->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
            ma_sound_set_direction(&ac->voice_->sound, fwd.x, fwd.y, fwd.z);
            ac->voice_->coneSet = on;
        }
        updateOcclusion(*ac->voice_, *ac, pos, dt);
        updateDoppler(*ac->voice_, *ac, pos, dt);
        updateSend(*ac->voice_, *ac, pos);
        updateHrtf(*ac->voice_, *ac, pos);
        // 위치 · 설정 · 첫 차폐 판정 · HRTF 사슬을 넣은 뒤에 시작해야 첫 소리가 원점에서 나거나 막힌 소리가 열린 채로 튀지 않습니다
        // (UE 의 비동기 트레이스는 첫 결과가 오기 전에 소리가 바뀌는 문제가 알려져 있음). 처음부터 들리지 않으면 가상으로 시작합니다.
        if (created && ac->autoPlay) {
            if (hvirt::nextVirtual(false, audibleGain(*ac->voice_, *ac, pos))) {
                ac->voice_->virtualized   = true;
                ac->voice_->virtualCursor = 0.0;
            } else {
                ma_sound_start(&ac->voice_->sound);
            }
        } else {
            updateVirtual(*ac->voice_, *ac, pos, dt);
        }
        if (ac->attenuation.spatialize && isVoicePlaying(ac->voice_))
            budget.emplace_back(ac->voice_, audibleGain(*ac->voice_, *ac, pos));
    }
    applyBudget(budget);
    tickOneShots();
}

// ── 사운드 단계 4: 버스 · 마스터 · 보이스 예산 · 한 번 재생 (2026-11-17) ─────────────────────────────
void AudioSystem::setMasterVolume(float v) {
    masterVolume_ = std::clamp(std::isfinite(v) ? v : 1.f, 0.f, 2.f);
    if (engine_) ma_engine_set_volume(asEngine(engine_), masterVolume_);
}

void AudioSystem::setBusVolume(EAudioBus bus, float v) {
    const int i = (int)bus;
    if (i < 0 || i >= kAudioBusCount) return;
    busVolume_[i] = std::clamp(std::isfinite(v) ? v : 1.f, 0.f, 2.f);
}

float AudioSystem::busVolume(EAudioBus bus) const {
    const int i = (int)bus;
    return (i >= 0 && i < kAudioBusCount) ? busVolume_[i] : 1.f;
}

// 들리는 크기 순위로 예산 밖의 보이스를 표시합니다. 다음 틱의 updateVirtual 이 표시된 보이스를 가상화하고, 예산 안으로 돌아오면
// 그 위치에서 이어 냅니다.
void AudioSystem::applyBudget(std::vector<std::pair<AudioVoice*, float>>& audible) {
    std::vector<float> gains;
    gains.reserve(audible.size());
    for (const auto& [v, g] : audible) gains.push_back(g);
    std::vector<bool> culled;
    hprop::rankBudget(gains, voiceBudget_, culled);
    for (size_t i = 0; i < audible.size(); ++i) audible[i].first->budgetCulled = culled[i];
}

bool AudioSystem::playOneShot(const char* filePath, const glm::vec3& position, float volume, EAudioBus bus,
                              const AudioAttenuationSettings& attenuation) {
    if (!engine_ || !filePath || !*filePath || oneShots_.size() >= kMaxOneShots) return false;
    const float g = volume * busVolume(bus) *
                    (attenuation.spatialize ? hvirt::distanceGain(attenuation, glm::length(position - listenerPos_)) : 1.f);
    if (g < hvirt::kVirtualGain) return false;   // 들리지 않을 소리는 만들지 않음
    AudioVoice* v = createVoice(filePath, false, false);
    if (!v) return false;
    ma_sound* s = &v->sound;
    ma_sound_set_position(s, position.x, position.y, position.z);
    ma_sound_set_spatialization_enabled(s, attenuation.spatialize ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(s, (ma_attenuation_model)attenuation.model);
    ma_sound_set_min_distance(s, attenuation.minDistance);
    ma_sound_set_max_distance(s, attenuation.maxDistance);
    ma_sound_set_rolloff(s, attenuation.rolloff);
    ma_sound_set_volume(s, volume * busVolume(bus));
    ma_sound_start(s);
    oneShots_.push_back({ v, position, attenuation, volume, bus });
    return true;
}

void AudioSystem::tickOneShots() {
    for (size_t i = 0; i < oneShots_.size();) {
        OneShot& o = oneShots_[i];
        if (ma_sound_at_end(&o.voice->sound)) {
            destroyVoice(o.voice);
            oneShots_[i] = oneShots_.back();
            oneShots_.pop_back();
            continue;
        }
        ma_sound_set_volume(&o.voice->sound, o.volume * busVolume(o.bus));   // 버스 볼륨을 바꾸면 울리는 중에도 따라감
        ++i;
    }
}

void AudioSystem::releaseOneShots() {
    for (OneShot& o : oneShots_) destroyVoice(o.voice);
    oneShots_.clear();
}

void AudioSystem::onBeginPlay() {
    // 지난 플레이에서 불러오기에 실패한 파일을 이번 플레이에서 다시 시도합니다.
    if (world_)
        for (auto& ap : world_->getActors()) {
            auto* ac = ap->getComponent<AudioComponent>();
            if (ac && ac->voice_ == nullptr) ac->loadFailed_ = false;
        }
    onPauseChanged(false);   // 어떤 경로로든 멈춘 그룹이 남아 있으면 풉니다
    stopReverb();            // 지난 플레이의 잔향을 남기지 않습니다(다시 시작할 때 꼬리를 비움)
}

void AudioSystem::onEndPlay() {
    releaseOneShots();   // 한 번 재생은 플레이에 속함
    // 플레이가 끝나면 남은 소리를 멈춥니다. 에디터는 Stop 에서 월드를 되돌리며 사운드를 이미
    // 정리하므로 보통은 할 일이 없지만, 다른 경로로 끝나도 소리가 남지 않게 합니다.
    if (world_)
        for (auto& ap : world_->getActors())
            if (auto* ac = ap->getComponent<AudioComponent>(); ac && ac->voice_)
                stopVoice(ac->voice_);
    // Pause 상태에서 Stop 하면 그룹이 멈춘 채 남아 다음 플레이가 무음이 됩니다. 여기서 풉니다.
    onPauseChanged(false);
    // 그룹은 재생 상태로 남으므로 리버브를 그대로 두면 꼬리가 1 ~ 2 초 더 울립니다. 멈춥니다(꼬리는 다시 시작할 때 비움, 9-5).
    stopReverb();
}

void AudioSystem::onPauseChanged(bool paused) {
    if (!group_ || paused == paused_) return;
    if (paused) ma_sound_group_stop(asGroup(group_));
    else        ma_sound_group_start(asGroup(group_));
    paused_ = paused;
}

AudioVoice* AudioSystem::createVoice(const char* filePath, bool loop, bool stream) {
    if (!engine_) return nullptr;
    ma_engine* eng = asEngine(engine_);
    auto v = std::make_unique<AudioVoice>();

    // 사슬의 끝부터 만듭니다. 필터를 월드 그룹(그룹이 없으면 엔진 출력)에 붙인 뒤 사운드를 필터에 붙입니다. 필터는 붙이는
    // 순간부터 그룹 입력 목록에 들어가므로, 예전에 사운드가 들어가던 자리 · 순서와 같습니다(섞는 순서가 같아야 합이 같음).
    ma_node_config nc = ma_node_config_init();
    nc.vtable = &g_voiceFilterVtable;
    const ma_uint32 inChannels     = kChannels;                // 사운드 출력은 엔진 채널 수(스테레오)입니다
    const ma_uint32 outChannels[2] = { kChannels, kChannels };  // 0 = 직접음, 1 = 잔향 센드
    nc.pInputChannels  = &inChannels;
    nc.pOutputChannels = outChannels;
    if (const ma_result r = ma_node_init(ma_engine_get_node_graph(eng), &nc, nullptr, &v->filter.base); r != MA_SUCCESS) {
        HLOG(LogAudio, Error) << "voice filter node init failed: " << ma_result_description(r);
        return nullptr;
    }
    v->filterOk = true;
    if (const ma_result r = ma_node_attach_output_bus(&v->filter.base, 0, sinkNode(), 0); r != MA_SUCCESS) {
        HLOG(LogAudio, Error) << "voice filter attach failed: " << ma_result_description(r);
        destroyVoice(v.release());
        return nullptr;
    }
    // 잔향 센드는 리버브 입력에 붙입니다. 리버브가 멈춰 있으면 읽히지 않고, 센드 게인도 0 이라 0 만 씁니다.
    if (reverb_) {
        if (const ma_result r = ma_node_attach_output_bus(&v->filter.base, 1, &asReverb(reverb_)->base, 0); r != MA_SUCCESS) {
            HLOG(LogAudio, Warning) << "reverb send attach failed (this sound will have no reverb): " << ma_result_description(r);
        }
    }

    // ASYNC 여도 miniaudio 는 디코더를 초기화할 때까지 기다렸다가 돌아옵니다(WAIT_INIT 강제).
    // 그래서 없는 파일과 모르는 형식은 여기서 바로 실패하고, 디코딩은 믹싱 중에 합니다.
    // 논리 경로 그대로 넘깁니다. 리소스 관리자가 가상 파일 시스템으로 엽니다(위 g_vfs, 2026-10-24).
    // 설정은 ma_sound_init_from_file 과 같고(ma_sound_config_init_2), 붙는 곳만 그룹 대신 필터입니다.
    ma_sound_config sc = ma_sound_config_init_2(eng);
    sc.pFilePath                      = filePath;
    sc.flags                          = MA_SOUND_FLAG_ASYNC | (stream ? MA_SOUND_FLAG_STREAM : 0u);
    sc.pInitialAttachment             = &v->filter.base;
    sc.initialAttachmentInputBusIndex = 0;
    if (const ma_result r = ma_sound_init_ex(eng, &sc, &v->sound); r != MA_SUCCESS) {
        HLOG(LogAudio, Error) << "failed to load '" << filePath << "': " << ma_result_description(r);
        destroyVoice(v.release());
        return nullptr;
    }
    v->soundOk = true;
    ma_sound_set_looping(&v->sound, loop ? MA_TRUE : MA_FALSE);
    return v.release();
}

void AudioSystem::destroyVoice(AudioVoice* voice) {
    if (!voice) return;
    // 사슬의 앞부터 풉니다. 사운드를 먼저 정리해야 필터 입력에서 떨어지고, 그 뒤 필터가 그룹에서 떨어집니다. 두 정리 모두
    // 오디오 스레드가 읽는 중이면 끝날 때까지 기다립니다(miniaudio 의 연결 잠금).
    if (voice->soundOk)  ma_sound_uninit(&voice->sound);
    if (voice->filterOk) ma_node_uninit(&voice->filter.base, nullptr);
    if (voice->hrtf)     ma_node_uninit(&voice->hrtf->base, nullptr);   // 그 뒤 delete 가 노드와 HRTF 상태를 지움
    delete voice;
}

void AudioSystem::playVoice(AudioVoice* voice) {
    // 가상 보이스는 이미 재생 중입니다(재생 중인 소리에 ma_sound_start 를 부른 것처럼 아무 일도 하지 않음). 들리게 되면
    // updateVirtual 이 이어 냅니다.
    if (voice && !voice->virtualized) ma_sound_start(&voice->sound);
}

// ── 가상화 (S3) ───────────────────────────────────────────────────────────────
float AudioSystem::audibleGain(const AudioVoice& v, const AudioComponent& ac, const glm::vec3& sourcePos) const {
    if (!ac.attenuation.spatialize) return 1.f;   // 2D 는 가상화하지 않음
    float g = ac.volume * busVolume(ac.bus) * masterVolume_ *
              hvirt::distanceGain(ac.attenuation, glm::length(sourcePos - listenerPos_));
    if (ac.occlusion.enabled) g *= hocc::gain(v.occl, ac.occlusion);
    if (ac.cone.enabled() && ac.owner) {
        const glm::vec3 to = listenerPos_ - sourcePos;
        const float len = glm::length(to);
        if (len > 1e-4f) g *= hprop::coneGain(ac.cone, ac.owner->worldRotation() * glm::vec3(0.f, 0.f, -1.f), to / len);
    }
    return g;
}

void AudioSystem::updateVirtual(AudioVoice& v, const AudioComponent& ac, const glm::vec3& sourcePos, float dt) {
    if (!v.soundOk) return;
    ma_sound* s      = &v.sound;
    ma_uint64 length = 0;
    ma_uint32 rate   = 0;
    if (ma_sound_get_length_in_pcm_frames(s, &length) != MA_SUCCESS || length == 0 ||
        ma_sound_get_data_format(s, nullptr, nullptr, &rate, nullptr, 0) != MA_SUCCESS || rate == 0)
        return;   // 길이 · 샘플률을 아직 모름(비동기 디코딩 초기) → 가상화하지 않음
    const bool want = hvirt::nextVirtual(v.virtualized, audibleGain(v, ac, sourcePos)) || v.budgetCulled;
    if (v.virtualized) {
        // 일시정지(월드 그룹 멈춤) 동안은 진짜 보이스처럼 위치가 멈춰 있습니다.
        if (!paused_) {
            bool ended = false;
            v.virtualCursor = hvirt::advanceCursor(v.virtualCursor, (double)dt * rate * ma_sound_get_pitch(s), length,
                                                   ac.loop, ended);
            if (ended) {   // 들리지 않는 채로 끝남: 진짜 보이스가 끝까지 간 것처럼 멈춘 상태로 둠
                v.virtualized = false;
                ma_sound_seek_to_pcm_frame(s, length);
                return;
            }
        }
        if (!want) {
            ma_sound_seek_to_pcm_frame(s, (ma_uint64)v.virtualCursor);
            v.filter.reset.store(true, std::memory_order_release);   // 멈추기 전 필터 상태를 이어 쓰지 않음
            if (v.hrtf) v.hrtf->reset.store(true, std::memory_order_release);
            v.virtualized = false;
            ma_sound_start(s);
        }
    } else if (want && ma_sound_is_playing(s)) {
        ma_uint64 cursor = 0;
        if (ma_sound_get_cursor_in_pcm_frames(s, &cursor) != MA_SUCCESS) return;
        ma_sound_stop(s);
        v.virtualized   = true;
        v.virtualCursor = (double)cursor;
    }
}

bool AudioSystem::voiceVirtual(const AudioVoice* voice) const { return voice && voice->virtualized; }

void AudioSystem::stopVoice(AudioVoice* voice) {
    if (!voice) return;
    // ma_sound_stop 은 재생 위치를 그대로 두는 일시정지입니다. 엔진의 stop 은 처음으로
    // 되감는 것이 규칙이라(Unity·UE 와 같음) 되감기를 함께 겁니다. 되감기는 다음 믹싱에서
    // 적용됩니다.
    ma_sound_stop(&voice->sound);
    ma_sound_seek_to_pcm_frame(&voice->sound, 0);
    voice->virtualized   = false;
    voice->virtualCursor = 0.0;
    // 필터 상태를 비우고, 다음 틱에 차폐를 바로 다시 판정해 그 값으로 시작합니다(멈춘 동안 벽이 생기거나 사라졌을 수 있음).
    voice->filter.reset.store(true, std::memory_order_release);
    if (voice->hrtf) voice->hrtf->reset.store(true, std::memory_order_release);   // 지난 소리의 FIFO · 필터 꼬리를 비움
    voice->traced = false;
}

float AudioSystem::voicePitch(const AudioVoice* voice) const { return voice ? voice->pitch : 1.f; }

float AudioSystem::voiceOcclusion(const AudioVoice* voice) const {
    return voice ? voice->occl : 0.f;
}

// ── 차폐 판정 (2026-11-06, Docs/Audio_Design.md 9-4) ─────────────────────────────

void AudioSystem::updateOcclusion(AudioVoice& v, const AudioComponent& ac, const glm::vec3& sourcePos, float dt) {
    const AudioOcclusionSettings& s = ac.occlusion;
    if (!s.enabled || !ac.attenuation.spatialize || !physics_) {
        // 꺼짐 · 2D · 물리 없음: 바로 열림. 켜고 끄는 순간의 차이는 들려도 괜찮은 편집 동작입니다. 공기 흡수(S2)는 따로 봅니다.
        v.occl = v.occlTarget = 0.f;
        v.traced = false;
        applyFilter(v, ac, sourcePos, false);
        return;
    }
    bool first = false;
    v.retrace -= dt;
    if (!v.traced || v.retrace <= 0.f) {
        v.occlTarget = traceOcclusion(ac, sourcePos);
        if (!v.traced) {
            // 처음 판정한 값으로 바로 시작합니다. 다음 판정 시점은 소리마다 엇갈리게 둬 한 프레임에 몰리지 않게 합니다
            // (황금비 간격: 0.618 씩 밀린 위상).
            first    = true;
            v.occl   = v.occlTarget;
            v.traced = true;
            const float phase = std::fmod((float)(voiceSerial_++) * 0.6180339887f, 1.f);
            v.retrace = hocc::kRetraceSec * (0.25f + 0.75f * phase);
        } else {
            v.retrace += hocc::kRetraceSec;
            if (v.retrace <= 0.f) v.retrace = hocc::kRetraceSec;   // 긴 프레임 뒤에 밀린 판정을 한꺼번에 하지 않음
        }
    }
    if (!first) v.occl = hocc::smooth(v.occl, v.occlTarget, dt, s.smoothTime);
    applyFilter(v, ac, sourcePos, first);
}

// 보이스 필터 = 차폐(게인 + 저역 통과) 와 공기 흡수(저역 통과, S2)를 합친 것. 두 저역 통과는 차단 주파수가 낮은 쪽 하나로 겁니다
// (같은 1차 두 단 필터라 둘을 곱하는 것보다 싸고, 차이는 두 차단 주파수가 가까울 때만 3 dB 안쪽). 둘 다 없으면 필터를 건너뜁니다
// (예전 출력과 비트 단위로 같음).
void AudioSystem::applyFilter(AudioVoice& v, const AudioComponent& ac, const glm::vec3& sourcePos, bool first) {
    VoiceFilterNode& f = v.filter;
    const bool  occluded = ac.occlusion.enabled && v.occl > 0.f;
    const float airHz    = hprop::airCutoffHz(ac.attenuation, ac.propagation, glm::length(sourcePos - listenerPos_));
    const bool  air      = airHz < AudioPropagationSettings::kMaxHz - 1.f;
    if (!occluded && !air) {
        f.active.store(false, std::memory_order_relaxed);
        return;
    }
    const float occHz = occluded ? hocc::cutoffHz(v.occl, ac.occlusion) : AudioPropagationSettings::kMaxHz;
    f.gain.store(occluded ? hocc::gain(v.occl, ac.occlusion) : 1.f, std::memory_order_relaxed);
    f.coef.store(hocc::onePoleCoef(std::min(occHz, airHz), (float)kSampleRate), std::memory_order_relaxed);
    if (first) f.snap.store(true, std::memory_order_relaxed);
    f.active.store(true, std::memory_order_release);
}

void AudioSystem::updateDoppler(AudioVoice& v, const AudioComponent& ac, const glm::vec3& sourcePos, float dt) {
    if (v.hasPrev) v.velocity = hprop::trackVelocity(v.prevPos, sourcePos, dt, v.velocity);
    v.prevPos = sourcePos;
    v.hasPrev = true;
    const float factor = ac.attenuation.spatialize ? ac.propagation.dopplerFactor : 0.f;
    const float pitch  = hprop::dopplerPitch(listenerPos_, listenerVel_, sourcePos, v.velocity, factor);
    if (pitch != v.pitch) {   // 바뀔 때만 넣음(도플러를 쓰지 않는 소리는 한 번도 부르지 않음)
        ma_sound_set_pitch(&v.sound, pitch);
        v.pitch = pitch;
    }
}

// 청취자 → 음원(반지름이 있으면 음원 둘레 표본점마다 청취자 → 표본 → 음원)이 막혔는지 보고 막힌 표본의 비율을 돌려줍니다.
// 표본 → 음원도 보는 것은 Steam Audio Volumetric 과 같은 이유입니다: 음원 옆 벽 너머의 표본은 청취자에게 보여도 소리가 거기까지
// 가지 못합니다.
float AudioSystem::traceOcclusion(const AudioComponent& ac, const glm::vec3& sourcePos) {
    // 레이에서 뺄 액터: 음원 액터와 그 조상 · 자손(음원이 자기 충돌체 안에 있으면 레이가 거리 0 으로 맞음), 청취자 액터와 그 조상.
    ignore_.clear();
    if (ac.owner) {
        ignore_.push_back(ac.owner);
        for (size_t i = 0; i < ignore_.size(); ++i)   // 자손(너비 우선). 자식 목록은 액터의 것이라 push_back 과 무관합니다
            for (Actor* c : ignore_[i]->getChildren()) ignore_.push_back(c);
        for (Actor* p = ac.owner->getParent(); p; p = p->getParent()) ignore_.push_back(p);
    }
    for (Actor* p = listenerActor_; p; p = p->getParent()) ignore_.push_back(p);

    const AudioOcclusionSettings& s = ac.occlusion;
    const float phase = (s.rotateSamples && ac.voice_) ? hprop::ringPhase(ac.voice_->traceCount++) : 0.f;
    hocc::samplePoints(listenerPos_, sourcePos, s.radius, samples_, phase);
    float blocked = 0.f;
    for (size_t k = 0; k < samples_.size(); ++k) {
        const glm::vec3& p = samples_[k];
        if (s.fullThickness > 0.f) {
            // 두께 투과(S4): 청취자 → 표본(→ 음원) 길을 막은 두께의 합으로 일부 막힘을 정합니다.
            float th = segmentThickness(listenerPos_, p);
            if (k > 0) th += segmentThickness(p, sourcePos);
            blocked += hprop::blockFromThickness(th, s.fullThickness);
        } else if (segmentBlocked(listenerPos_, p) || (k > 0 && segmentBlocked(p, sourcePos))) {
            blocked += 1.f;
        }
    }
    return blocked / (float)samples_.size();
}

// ── 잔향 (2026-11-06, Docs/Audio_Design.md 9-5) ────────────────────────────────────────

// 청취자 위치에서 잔향 구역을 고릅니다: 우선순위가 높은 것, 같으면 더 깊이 들어간 것(가중치), 그것도 같으면 액터 ID 가 작은 것
// (정하지 않은 순서를 남기지 않음). 고른 구역의 매개변수와 크기 × 가중치가 목표이고, 그 구역의 fadeTime 으로 따라갑니다. 구역이
// 없으면 크기만 0 으로 줄이고(다른 매개변수는 그대로 두어 꼬리의 성격이 갑자기 바뀌지 않게), 0 이 되면 리버브를 멈춥니다.
void AudioSystem::updateReverb(float dt) {
    ReverbNode* r = asReverb(reverb_);
    if (!r) return;
    const ReverbZoneComponent* best = nullptr;
    float bestW = 0.f;
    for (const ReverbZoneComponent* z : world_->componentsOf<ReverbZoneComponent>()) {
        if (!z->isLive()) continue;
        const float w = z->weightAt(listenerPos_);
        if (!(w > 0.f)) continue;
        const bool better = !best || z->priority > best->priority
            || (z->priority == best->priority && (w > bestW || (w == bestW && z->owner->id < best->owner->id)));
        if (better) { best = z; bestW = w; }
    }
    if (best) {
        reverbTarget_       = best->params();
        reverbTarget_.level = best->level * bestW;
        reverbFade_         = best->fadeTime;
    } else {
        reverbTarget_.level = 0.f;
    }
    reverbTarget_.decayTime = std::clamp(reverbTarget_.decayTime, hrev::kMinDecay, hrev::kMaxDecay);

    if (!reverbRunning_) {
        if (!(reverbTarget_.level > 0.f)) return;   // 구역 밖: 리버브는 멈춘 채(읽히지 않음)
        // 시작: 꼬리를 비우고, 매개변수는 바로 목표로(멈춰 있던 동안의 값에서 옮겨 가지 않음), 크기만 0 에서 올라갑니다.
        reverbCur_       = reverbTarget_;
        reverbCur_.level = 0.f;
        r->reset.store(true, std::memory_order_release);
        reverbRunning_ = true;
    }
    reverbCur_ = hrev::follow(reverbCur_, reverbTarget_, dt, reverbFade_);
    r->roomSize.store(hrev::roomSizeForDecay(reverbCur_.decayTime), std::memory_order_relaxed);
    r->damping.store(reverbCur_.damping, std::memory_order_relaxed);
    r->width.store(reverbCur_.width, std::memory_order_relaxed);
    r->level.store(reverbCur_.level, std::memory_order_relaxed);
    if (reverbCur_.level <= 0.f && reverbTarget_.level <= 0.f) {
        stopReverb();        // 다 줄어듦: 출력이 0 이므로 바로 멈춰도 들리는 차이가 없습니다
        return;
    }
    ma_node_set_state(&r->base, ma_node_state_started);
}

// 멈춘 리버브는 읽히지 않으므로 남은 꼬리는 들리지 않습니다. 꼬리는 다시 시작할 때(updateReverb) 비웁니다.
void AudioSystem::stopReverb() {
    ReverbNode* r = asReverb(reverb_);
    if (!r) return;
    ma_node_set_state(&r->base, ma_node_state_stopped);
    r->level.store(0.f, std::memory_order_relaxed);
    reverbRunning_   = false;
    reverbCur_.level = 0.f;
}

// 센드 게인 = send × g(d)^(−reach)(hrev::sendGain). 리버브가 멈춰 있거나 2D 소리면 0 입니다.
void AudioSystem::updateSend(AudioVoice& v, const AudioComponent& ac, const glm::vec3& sourcePos) {
    float g = 0.f;
    if (reverbRunning_ && ac.attenuation.spatialize && ac.reverbSend > 0.f) {
        const float att = hrev::attenuationGain(ac.attenuation, glm::length(sourcePos - listenerPos_));
        g = hrev::sendGain(ac.reverbSend, ac.reverbReach, att);
    }
    v.filter.send.store(g, std::memory_order_relaxed);
}

// ── HRTF (2026-11-06, Docs/Audio_Design.md 9-6) ────────────────────────────────────────

void* AudioSystem::sinkNode() const {
    if (group_) return static_cast<ma_node*>(asGroup(group_));
    return engine_ ? ma_engine_get_endpoint(asEngine(engine_)) : nullptr;
}

const char* AudioSystem::hrtfModeName(EHrtfMode m) {
    switch (m) {
        case EHrtfMode::On:  return "on";
        case EHrtfMode::Off: return "off";
        default:             return "auto";
    }
}

bool AudioSystem::hrtfModeFromText(const std::string& text, EHrtfMode& out) {
    if (text == "auto") { out = EHrtfMode::Auto; return true; }
    if (text == "on")   { out = EHrtfMode::On;   return true; }
    if (text == "off")  { out = EHrtfMode::Off;  return true; }
    return false;
}

void AudioSystem::setHrtfMode(EHrtfMode m) {
    if (m == hrtfMode_) return;
    hrtfMode_ = m;
    HLOG(LogAudio, Log) << "HRTF mode: " << hrtfModeName(m);
    refreshHrtf();
}

void AudioSystem::setXrActive(bool active) {
    if (active == xrActive_) return;
    xrActive_ = active;
    refreshHrtf();
}

// 켤지 정하고, 처음 켜질 때 Steam Audio 를 엽니다(시작이 느려지지 않게 미룸, 열기 · 기본 HRTF 만들기에 수십 ms). 열지 못하면 한 번만
// 경고하고 패닝으로 둡니다. 소리마다 노드를 넣고 빼는 것은 다음 tick 의 updateHrtf 가 합니다.
void AudioSystem::refreshHrtf() {
    const bool want = hrtfMode_ == EHrtfMode::On || (hrtfMode_ == EHrtfMode::Auto && xrActive_);
    if (want && !hrtf_ && !hrtfTried_) {
        hrtfTried_ = true;
        std::string err;
        hrtf_ = hhrtf::loadSteamAudio(err);
        if (hrtf_) { HLOG(LogAudio, Log) << "HRTF: " << hrtf_->name(); }
        else       { HLOG(LogAudio, Warning) << "HRTF unavailable: " << err << "; 3D sounds keep stereo panning"; }
    }
    const bool active = want && hrtf_ != nullptr;
    if (active != hrtfActive_) {
        HLOG(LogAudio, Log) << "HRTF " << (active ? "on" : "off") << " (mode " << hrtfModeName(hrtfMode_)
                            << (xrActive_ ? ", XR session" : "") << ")";
    }
    hrtfActive_ = active;
}

void AudioSystem::setHrtfBackend(std::unique_ptr<hhrtf::Backend> backend) {
    dropHrtfNodes();   // 지금 구현으로 만든 상태는 그 구현보다 먼저 지워야 합니다
    hrtf_       = std::move(backend);
    hrtfTried_  = true;
    hrtfWarned_ = false;
    hrtfActive_ = false;   // 아래에서 다시 정함(로그가 새 구현 기준으로 남게)
    refreshHrtf();
}

void AudioSystem::dropHrtfNodes() {
    if (!world_) return;
    for (auto& ap : world_->getActors()) {
        auto* ac = ap->getComponent<AudioComponent>();
        if (!ac || !ac->voice_) continue;
        AudioVoice& v = *ac->voice_;
        if (v.hrtf) {
            if (v.hrtfRouted) ma_node_attach_output_bus(&v.filter.base, 0, sinkNode(), 0);
            ma_node_uninit(&v.hrtf->base, nullptr);   // 오디오 스레드가 읽는 중이면 끝날 때까지 기다림
            v.hrtf.reset();
            v.hrtfRenderer.reset();
        }
        v.hrtfRouted = false;
        v.hrtfFailed = false;
        ma_sound_set_directional_attenuation_factor(&v.sound, 1.f);
    }
}

bool AudioSystem::createHrtfNode(AudioVoice& v) {
    auto fail = [&](const std::string& why) {
        if (!hrtfWarned_) { HLOG(LogAudio, Warning) << "HRTF node not created (" << why << "); that sound keeps stereo panning"; }
        hrtfWarned_  = true;
        v.hrtfFailed = true;
        return false;
    };
    if (!hrtf_ || !engine_) return false;
    std::unique_ptr<hhrtf::Renderer> r = hrtf_->createRenderer();
    if (!r) return fail(hrtf_->name() + " could not create an effect");
    auto node = std::make_unique<HrtfNode>();
    node->renderer = r.get();
    node->gain     = hrtf_->outputGain();
    ma_node_config nc = ma_node_config_init();
    nc.vtable = &g_hrtfVtable;
    const ma_uint32 ch = kChannels;
    nc.pInputChannels  = &ch;
    nc.pOutputChannels = &ch;
    if (const ma_result e = ma_node_init(ma_engine_get_node_graph(asEngine(engine_)), &nc, nullptr, &node->base); e != MA_SUCCESS)
        return fail(ma_result_description(e));
    if (const ma_result e = ma_node_attach_output_bus(&node->base, 0, sinkNode(), 0); e != MA_SUCCESS) {
        ma_node_uninit(&node->base, nullptr);
        return fail(ma_result_description(e));
    }
    v.hrtf         = std::move(node);
    v.hrtfRenderer = std::move(r);
    return true;
}

// 소리마다: HRTF 를 쓸지(켜짐 · 공간화) 보고 보이스 필터의 출력 0 을 HRTF 노드나 월드 그룹에 붙이고, 패닝 계수(HRTF 면 0, 아니면
// miniaudio 기본 1)와 청취자 기준 방향을 넘깁니다. 붙이기 · 떼기는 오디오 스레드가 도는 중에도 되고(miniaudio 의 잠금 없는 목록),
// 바뀌는 순간 한 블록 정도 패닝 · HRTF 가 엇갈릴 수 있지만 설정을 바꿀 때뿐입니다.
void AudioSystem::updateHrtf(AudioVoice& v, const AudioComponent& ac, const glm::vec3& sourcePos) {
    const bool want = hrtfActive_ && ac.attenuation.spatialize && v.filterOk;
    if (want && !v.hrtfRouted && !v.hrtfFailed && (v.hrtf || createHrtfNode(v))) {
        if (const ma_result e = ma_node_attach_output_bus(&v.filter.base, 0, &v.hrtf->base, 0); e == MA_SUCCESS) {
            v.hrtfRouted = true;
        } else {
            ma_node_attach_output_bus(&v.filter.base, 0, sinkNode(), 0);   // 떼어진 채 남지 않게 그룹에 다시
            if (!hrtfWarned_) { HLOG(LogAudio, Warning) << "HRTF attach failed: " << ma_result_description(e) << "; that sound keeps stereo panning"; }
            hrtfWarned_  = true;
            v.hrtfFailed = true;
        }
    } else if (!want && v.hrtfRouted) {
        ma_node_attach_output_bus(&v.filter.base, 0, sinkNode(), 0);   // HRTF 노드는 입력이 끊겨 남은 꼬리만 내고 멈춤
        v.hrtfRouted = false;
    }
    ma_sound_set_directional_attenuation_factor(&v.sound, v.hrtfRouted ? 0.f : 1.f);
    if (!v.hrtfRouted) return;
    const glm::vec3 rel = sourcePos - listenerPos_;
    const glm::vec3 d   = hhrtf::listenerLocalDirection(rel, listenerFwd_, listenerUp_);
    v.hrtf->dirX.store(d.x, std::memory_order_relaxed);
    v.hrtf->dirY.store(d.y, std::memory_order_relaxed);
    v.hrtf->dirZ.store(d.z, std::memory_order_relaxed);
    v.hrtf->blend.store(hhrtf::spatialBlend(glm::length(rel)), std::memory_order_relaxed);
}

std::string AudioSystem::hrtfStatus() const {
    if (hrtfActive_) return "on (" + hrtf_->name() + ")";
    if (!hrtf_ && hrtfTried_) return "unavailable (see the log)";
    return "off";
}

bool AudioSystem::voiceHrtfRouted(const AudioVoice* voice) const {
    return voice && voice->hrtfRouted;
}

uint64_t AudioSystem::voiceHrtfBlocks(const AudioVoice* voice) const {
    return (voice && voice->hrtf) ? voice->hrtf->blocks.load(std::memory_order_relaxed) : 0;
}

bool AudioSystem::segmentBlocked(const glm::vec3& from, const glm::vec3& to) const {
    const glm::vec3 d   = to - from;
    const float     len = glm::length(d);
    constexpr float kEnd = 1e-3f;   // 끝점에 딱 닿은 면은 막힘으로 보지 않음
    if (len <= 2.f * kEnd) return false;
    PhysicsSystem::RaycastHit hit;
    return physics_->raycast(from, d, len - kEnd, hit, ignore_);
}

float AudioSystem::segmentThickness(const glm::vec3& from, const glm::vec3& to) const {
    const glm::vec3 d   = to - from;
    const float     len = glm::length(d);
    constexpr float kEnd = 1e-3f;
    if (len <= 2.f * kEnd) return 0.f;
    PhysicsSystem::RaycastHit a, b;
    if (!physics_->raycast(from, d, len - kEnd, a, ignore_)) return 0.f;
    // 반대쪽에서 처음 닿는 곳까지. 반대쪽에서 못 맞추면(끝점이 물체 안) 들어간 곳부터 끝까지가 두께입니다.
    const float back = physics_->raycast(to, -d, len - kEnd, b, ignore_) ? b.distance : 0.f;
    return std::max(0.f, len - a.distance - back);
}

bool AudioSystem::isVoicePlaying(const AudioVoice* voice) const {
    if (!voice) return false;
    return voice->virtualized || ma_sound_is_playing(&voice->sound) == MA_TRUE;   // 가상 보이스도 재생 중
}

bool AudioSystem::renderHeadless(float* outStereo, uint32_t frameCount) {
    if (!engine_ || !headless_) return false;
    ma_uint64 read = 0;
    const ma_result r = ma_engine_read_pcm_frames(asEngine(engine_), outStereo, frameCount, &read);
    return r == MA_SUCCESS && read == frameCount;
}
