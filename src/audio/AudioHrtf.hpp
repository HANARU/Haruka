#pragma once
// ── HRTF(머리 전달 함수) 공간화 (2026-11-06, Docs/Audio_Design.md 9-6) ─────────────────────────────────────
// 소리마다 청취자 기준 방향으로 좌 · 우 귀 필터를 걸어 앞뒤 · 위아래 단서를 냅니다. 패닝(좌우 크기 차이만)은 앞뒤 · 위아래를
// 구분하지 못합니다(DEF-166). UE5 · Unity · Godot 4 는 엔진 안에 HRTF 가 없고 플러그인 자리만 두므로, Haruka 도 노드는 엔진이
// 갖고 계산은 뒤의 구현(Backend)에 맡깁니다. 지금 구현은 Steam Audio 4.8.1 의 바이노럴 효과 하나이고(SteamAudioHrtf.cpp),
// 같은 인터페이스로 공개 HRTF 데이터 + 직접 컨볼루션 같은 다른 구현을 바꿔 끼울 수 있습니다.
//
// 거리 감쇠 · 콘 · 도플러는 그대로 miniaudio 가 하고(패닝 계수만 0), 차폐 필터 · 잔향 센드도 HRTF 앞(보이스 필터)에 있습니다.
// HRTF 는 헤드폰 · 헤드셋용이라 켜는 때는 AudioSystem 의 모드(Auto = XR Play 동안, On, Off)가 정합니다.

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace hhrtf {

// 한 번에 처리하는 프레임 수입니다. Steam Audio 는 만들 때 정한 크기로만 처리하고, miniaudio 노드는 부를 때마다 프레임 수가
// 달라질 수 있어(사운드 끝 · 캐시 경계 · 렌더 크기) HRTF 노드가 FIFO 로 모아 이 크기마다 처리합니다. 그만큼(48 kHz 에서 5.3 ms)
// 직접음이 늦습니다. 작을수록 지연이 짧고 호출 비용이 늘어납니다(Steam Audio 문서의 예는 512 · 1024, VR 이라 줄임).
constexpr uint32_t kFrameSize = 256;

// 청취자에게 이만큼(m) 가까워지면 방향 단서를 줄여 머리 한가운데로 모읍니다(거리 0 에서는 방향이 정해지지 않음). 청취자 액터에
// 붙은 소리(숨소리 · 발소리 등)가 정면이나 한쪽으로 튀지 않게 합니다. 사람 머리 반지름(약 9 cm)과 같은 자릿수입니다.
constexpr float kNearDistance = 0.1f;

// 청취자 → 음원 벡터를 청취자 기준 단위 방향으로 바꿉니다(+X 오른쪽, +Y 위, -Z 정면; Steam Audio 와 miniaudio 의 규약).
// 오른쪽은 정면 × 위입니다(miniaudio 청취자와 같음). 길이가 거의 0 이면 정면을 돌려줍니다.
inline glm::vec3 listenerLocalDirection(const glm::vec3& toSource, const glm::vec3& forward, const glm::vec3& up) {
    const float fl = glm::length(forward);
    const glm::vec3 f = fl > 1e-6f ? forward / fl : glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 r = glm::cross(f, up);
    float rl = glm::length(r);
    if (rl < 1e-6f) {   // 위 벡터가 정면과 나란함: 월드 +Y(그것도 나란하면 +Z)로 오른쪽을 정합니다
        r  = glm::cross(f, std::abs(f.y) < 0.99f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(0.f, 0.f, 1.f));
        rl = glm::length(r);
    }
    r /= rl;
    const glm::vec3 u = glm::cross(r, f);
    const glm::vec3 d(glm::dot(toSource, r), glm::dot(toSource, u), -glm::dot(toSource, f));
    const float dl = glm::length(d);
    return dl > 1e-6f ? d / dl : glm::vec3(0.f, 0.f, -1.f);
}

// 공간화 정도(0 = 방향 없음, 1 = 완전한 HRTF). kNearDistance 안에서만 1 보다 작습니다.
inline float spatialBlend(float distance) {
    return std::clamp(distance / kNearDistance, 0.f, 1.f);
}

// 소리 하나의 HRTF 상태(필터 꼬리 · 보간 상태). 게임 스레드가 만들고 지우며, process · reset 은 오디오 스레드만 부릅니다.
class Renderer {
public:
    virtual ~Renderer() = default;
    // 내부 상태를 비웁니다(멈춤 · 되감기 뒤 지난 소리의 꼬리가 남지 않게).
    virtual void reset() = 0;
    // kFrameSize 프레임의 모노 입력을 좌 · 우 귀 출력으로 바꿉니다. dir 은 청취자 기준 단위 방향, blend 는 0 ~ 1.
    // 출력은 Backend::outputGain 을 곱하기 전 값입니다. 할당 · 잠금이 없어야 합니다.
    // 돌려주는 값: 필터 꼬리가 남았는지. 확실하지 않으면 true 를 돌려줍니다(Steam Audio 의 처리 함수는 언제나 true).
    virtual bool process(const float* mono, float* outL, float* outR, const glm::vec3& dir, float blend) = 0;
    // 입력 블록이 모두 0 일 때 process 대신 부릅니다. 새 입력 없이 남은 꼬리 kFrameSize 프레임을 내고, 꼬리가 더 남았는지 돌려줍니다
    // (Steam Audio 의 GetTail: 입력이 끝나면 이것으로 꼬리를 끝까지 꺼내야 끝을 알 수 있음). HRTF 노드는 소리가 끝난 뒤 이 값이
    // false 가 되고 남은 출력을 다 내보내면 구현을 더 부르지 않습니다(멈춘 소리는 HRTF 계산 0).
    virtual bool tail(float* outL, float* outR) = 0;
};

// HRTF 구현 하나. 게임 스레드에서 만들고, 모든 Renderer 를 지운 뒤에 지웁니다.
class Backend {
public:
    virtual ~Backend() = default;
    virtual std::string name() const = 0;   // 로그용(예: "Steam Audio 4.8.1")
    // 소리 하나의 상태를 만듭니다(게임 스레드, 할당). 실패하면 nullptr.
    virtual std::unique_ptr<Renderer> createRenderer() = 0;
    // 출력에 곱할 게인. 정면 음원이 패닝일 때(채널마다 0.5)와 같은 크기로 들리게 HRTF 데이터마다 맞춥니다(오디오 테스트 17절).
    virtual float outputGain() const = 0;
};

// Steam Audio(phonon)를 실행 파일이 있는 폴더에서 열어 Backend 를 만듭니다. 라이브러리가 없거나(빌드 옵션
// HARUKA_STEAM_AUDIO 꺼짐 · 배포물에서 빠짐) 버전이 맞지 않으면 nullptr 를 돌려주고 error 에 이유를 남깁니다.
std::unique_ptr<Backend> loadSteamAudio(std::string& error);

} // namespace hhrtf
