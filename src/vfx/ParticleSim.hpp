#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../math/Random.hpp"

// ── CPU 파티클 시뮬레이션 (파티클 · 데칼, 2026-11-20, Docs/Particles_And_Decals.md) ────────────────────────────
// 장면 · 렌더러와 무관한 순수 계산입니다(D1). 컴포넌트(components/ParticleSystemComponent)가 틱마다 step 을 부르고, 렌더러는
// buildInstances 가 만든 ParticleInstance 와 sortBackToFront 만 씁니다. 같은 시드 + 같은 dt 수열이면 결과가 비트 단위로 같습니다(D3).

namespace vfx {

enum class Blend : uint8_t { Additive = 0, Alpha = 1 };
enum class Space : uint8_t { World = 0, Local = 1 };
enum class Shape : uint8_t { Cone = 0, Sphere = 1 };
// 엔진 모양 아틀라스(2 × 2 칸)의 칸 번호(D10).
enum class Sprite : uint8_t { SoftCircle = 0, Circle = 1, Ring = 2, Star = 3 };
constexpr uint32_t kSpriteCount = 4;

constexpr uint32_t kMaxParticles = 65536;   // 이미터 하나 · 한 프레임 전체의 상한(ParticlePass 의 인스턴스 버퍼 크기와 같음)
constexpr uint32_t kMaxCurveKeys = 8;
constexpr float    kMaxStepDt    = 0.1f;    // 한 틱 dt 상한(D5)

// GPU 인스턴스 하나(48 B, ParticlePass 의 정점 입력 · particle.vert 와 같은 배치).
struct ParticleInstance {
    glm::vec4 posSize{ 0.f };    // xyz = 월드 위치, w = 한 변 길이(m)
    glm::vec4 color{ 1.f };      // 선형 HDR 색 · 알파(텍스처에 곱함)
    glm::vec4 params{ 0.f };     // x = 회전(라디안), y = 아틀라스 칸, z w = 예약(0)
};
static_assert(sizeof(ParticleInstance) == 48, "ParticleInstance must match particle.vert");

// 수명 비율(0 ~ 1) 곡선. 키는 t 오름차순이고 그 사이는 선형, 범위 밖은 끝 키 값입니다. 키가 없으면 기본값(1 · 흰색).
struct FloatKey { float t = 0.f; float v = 1.f; };
struct ColorKey { float t = 0.f; glm::vec4 c{ 1.f }; };
float     sampleCurve(const std::vector<FloatKey>& keys, float t);
glm::vec4 sampleCurve(const std::vector<ColorKey>& keys, float t);
// 씬 파일 글자. "t v; t v" · "t r g b a; ..." 형식. 읽기는 키를 t 순으로 정렬하고 kMaxCurveKeys 개까지, 틀리면 false(out 그대로).
std::string curveToText(const std::vector<FloatKey>& keys);
std::string curveToText(const std::vector<ColorKey>& keys);
bool        curveFromText(const std::string& s, std::vector<FloatKey>& out);
bool        curveFromText(const std::string& s, std::vector<ColorKey>& out);

struct EmitterDesc {
    // 생성
    float    rate       = 20.f;    // 개/초(0 이면 비율 생성 없음)
    uint32_t burst      = 0;       // 시작(또는 반복할 때마다) 한 번에 낼 개수
    float    duration   = 2.f;     // 한 주기 길이(초). loop 면 이 주기마다 burst 를 다시 냄
    bool     loop       = true;    // 거짓이면 duration 뒤 생성을 멈춤(남은 파티클은 수명대로)
    uint32_t maxParticles = 1000;  // 1 ~ kMaxParticles
    uint32_t seed       = 0;
    Space    space      = Space::World;
    // 초기값
    float     lifeMin    = 1.f,  lifeMax  = 2.f;     // 초
    float     speedMin   = 1.f,  speedMax = 2.f;     // m/s
    Shape     shape      = Shape::Cone;
    float     coneAngle  = 25.f;                     // 반각(도). Cone 의 축은 로컬 +Y
    float     radius     = 0.f;                      // 생성 위치 반경(m, Cone 은 XZ 원판 · Sphere 는 구 안)
    float     size       = 0.1f;                     // 한 변 길이(m)
    float     spinMin    = 0.f,  spinMax  = 0.f;     // 회전 속도(도/초)
    // 갱신
    glm::vec3 gravity{ 0.f, 0.f, 0.f };              // m/s², 월드 방향
    float     drag       = 0.f;                      // 1/s, v *= exp(-drag dt)
    std::vector<FloatKey> sizeCurve;                 // 크기 배율
    std::vector<ColorKey> colorCurve;                // 선형 색 · 알파
    // 그리기
    Blend  blend  = Blend::Additive;
    Sprite sprite = Sprite::SoftCircle;

    void clampValues();   // 범위 밖 값을 자름(디테일 · 씬 파일 · 시험이 같은 규칙)
};

struct Particle {
    glm::vec3 pos{ 0.f };    // Space::World 면 월드, Local 이면 이미터 로컬
    float     age  = 0.f;
    glm::vec3 vel{ 0.f };
    float     life = 1.f;
    float     rot  = 0.f;    // 라디안
    float     spin = 0.f;    // 라디안/초
    uint32_t  serial = 0;    // 생성 순번(정렬 동점 처리)
};

class ParticleSim {
public:
    // 상태를 비우고 시드로 난수를 다시 만듭니다(시간 0).
    void reset(const EmitterDesc& d);
    // dt 만큼 진행합니다(dt 는 [0, kMaxStepDt] 로 자름). emitterWorld 는 이번 틱의 이미터 월드 행렬, emitting 이 거짓이면 새로 내지 않음.
    void step(const EmitterDesc& d, float dt, const glm::mat4& emitterWorld, bool emitting = true);
    // 그릴 인스턴스를 만듭니다(월드 공간, 순서는 내부 순서). out 을 비우고 채웁니다.
    void buildInstances(const EmitterDesc& d, const glm::mat4& emitterWorld, std::vector<ParticleInstance>& out) const;

    const std::vector<Particle>& particles() const { return particles_; }
    size_t   count() const       { return particles_.size(); }
    float    time() const        { return time_; }
    bool     finished(const EmitterDesc& d) const { return !d.loop && time_ >= d.duration && particles_.empty(); }
    uint64_t spawnedTotal() const { return serial_; }
    uint64_t dropped() const      { return dropped_; }   // 상한 때문에 버린 생성 수

private:
    void spawn(const EmitterDesc& d, const glm::mat4& emitterWorld, uint32_t n);

    std::vector<Particle> particles_;
    hmath::Pcg32 rng_;
    float    time_     = 0.f;    // 이번 주기 안 시간
    float    total_    = 0.f;    // 시작부터 시간
    float    carry_    = 0.f;    // 비율 생성의 소수 부분
    bool     burstDone_ = false;
    uint32_t serial_   = 0;
    uint64_t dropped_  = 0;
};

// 엔진 모양 아틀라스(D10): kAtlasSize² RGBA8(선형), 2 × 2 칸(칸 c 의 왼쪽 위 = ((c & 1) · 칸, (c >> 1) · 칸)). RGB 는 흰색이고 모양은
// 알파에 있습니다. 칸 가장자리 2 텍셀은 투명이라 이웃 칸이 번지지 않습니다.
constexpr int kAtlasSize = 128;
void buildSpriteAtlas(std::vector<uint8_t>& rgba);

// 인스턴스를 eye 에서 먼 것부터 정렬합니다(D9). 같은 거리는 들어온 순서를 지킵니다. 거리 제곱(0 이상 float)의 비트를 뒤집은
// 32비트 키로 8비트씩 4번 도는 안정 기수 정렬(LSD)이라 비교 정렬보다 빠릅니다(1만 개 측정, 문서 4절). 버퍼는 다시 씁니다.
struct SortScratch {
    std::vector<uint32_t>         k0, k1, i0, i1;
    std::vector<ParticleInstance> tmp;
};
void sortBackToFront(ParticleInstance* inst, size_t n, const glm::vec3& eye, SortScratch& s);

} // namespace vfx
