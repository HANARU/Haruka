#pragma once
// 접근성(2026-11-21, Docs/Accessibility.md).
//
// 플레이어가 고르는 접근성 값(access.*)과, 엔진 여러 곳이 함께 보는 실행 중 상태(비네트 세기 · 앉은 자세 보정)를 둡니다.
// 엔진(runtime/Engine)이 AccessibilityState 를 월드 서비스로 두고(world.setService), 이동 컴포넌트는 속도를 알리고, 패널 · 자막은
// 글자 배율을 읽고, 추출(RenderExtract)은 비네트 · 색 보정을 렌더 입력에 싣습니다. 계산은 haccess:: 의 순수 함수라 창 없이 시험합니다.
// 메인 스레드 전용입니다.

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class UserSettings;

// access.* 값(D1). 기본값이면 지금과 같은 화면 · 동작입니다.
struct AccessSettings {
    bool  subtitles          = false;
    float subtitleSize       = 1.f;    // 0.5 ~ 3
    float subtitleBackground = 0.6f;   // 0 ~ 1
    float textScale          = 1.f;    // 0.5 ~ 3
    bool  leftHanded         = false;  // access.hand = left
    int   colorFilter        = 0;      // 0 none · 1 protanopia · 2 deuteranopia · 3 tritanopia

    static AccessSettings from(const UserSettings& s);
    bool operator==(const AccessSettings& o) const {
        return subtitles == o.subtitles && subtitleSize == o.subtitleSize && subtitleBackground == o.subtitleBackground
            && textScale == o.textScale && leftHanded == o.leftHanded && colorFilter == o.colorFilter;
    }
};

namespace haccess {

// ── 비네트(D6) ──────────────────────────────────────────────────────────────
constexpr float kVignetteLinLo  = 0.1f;   // m/s, 이보다 느리면 0
constexpr float kVignetteLinHi  = 2.f;    // m/s, 이보다 빠르면 1
constexpr float kVignetteYawLo  = 5.f;    // °/s
constexpr float kVignetteYawHi  = 60.f;   // °/s
constexpr float kVignetteAttack  = 0.08f; // s, 들어갈 때 시간 상수
constexpr float kVignetteRelease = 0.25f; // s, 나올 때 시간 상수

// 이동 속도(선 m/s · 요 °/s)에서 목표 세기. strength(설정, 0 ~ 1)를 곱합니다.
float vignetteTarget(float linearSpeed, float yawRateDeg, float strength);
// 지금 값에서 목표로 dt 만큼 다가간 값(지수 접근, 올라갈 때 attack · 내려갈 때 release).
float vignetteStep(float current, float target, float dt);
// 화면 가운데에서의 거리 r(가장자리 가운데 1, 모서리 약 1.41)에서 곱할 밝기. post.frag 와 같은 식입니다.
float vignetteMask(float r, float v);

// ── 앉은 자세(D7) ───────────────────────────────────────────────────────────
constexpr float kMaxSeatedLift = 1.2f;   // m
// 선 눈높이 − 앉은 머리 높이(0 ~ kMaxSeatedLift). 앉은 머리가 더 높으면 0.
float seatedLift(float standingEye, float seatedHead);

// ── 색 보정(D9) ─────────────────────────────────────────────────────────────
// Machado(2009) 세기 1 시뮬레이션 행렬(선형 RGB, 열 우선 glm). mode 1 ~ 3, 그 밖에는 단위 행렬.
glm::mat3 colorSimulation(int mode);
// 보정 행렬 I + E(I − S). mode 0 이면 단위 행렬.
glm::mat3 colorCorrection(int mode);

// ── 자막(D3 · D4) ───────────────────────────────────────────────────────────
constexpr float kSubtitleMinSeconds  = 1.5f;
constexpr float kSubtitleMaxSeconds  = 8.f;
constexpr float kSubtitleCharsPerSec = 15.f;
// 자동 표시 시간: 1.5 + 글자 수 / 15 초, [1.5, 8]. 글자 수는 UTF-8 코드 포인트입니다.
float subtitleDuration(std::string_view text);
// 폭 maxWidth 안에 들어가게 공백에서 줄을 나눕니다(공백이 없는 긴 단어는 글자 단위). 최대 maxRows 줄, 넘치면 마지막 줄 끝을 "…" 로.
// measure 는 글 한 줄의 폭입니다. 이미 있는 줄바꿈은 지킵니다.
std::vector<std::string> wrapText(const std::string& text, float maxWidth, int maxRows,
                                  const std::function<float(std::string_view)>& measure);

} // namespace haccess

// 월드 서비스(D2). 엔진이 하나 두고 프레임마다 설정 · 비네트를 갱신합니다.
class AccessibilityState {
public:
    AccessSettings access;          // 엔진이 사용자 설정에서 채움
    float          vignetteStrength = 0.f;   // comfort.vignette(엔진이 채움)

    // 이동 컴포넌트가 이번 틱의 속도를 알립니다(여럿이면 큰 값). 스냅 회전 · 텔레포트는 넣지 않습니다.
    void reportMotion(float linearSpeed, float yawRateDeg);
    // 프레임 끝: 알린 속도로 세기를 갱신하고 알린 값을 비웁니다. 설정이 0 이면 곧바로 0 입니다.
    void tickVignette(float dt);
    float vignette() const { return vignette_; }
    // 세기를 직접 정합니다(모양 확인용 CVar comfort.vignetteDebug). 다음 tickVignette 가 다시 계산합니다.
    void  setVignette(float v) { vignette_ = v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

    // 앉은 자세(D7): 다음 머리 자세로 앉은 머리 높이를 다시 잽니다(게임 · 콘솔 comfort.recenterSeated).
    void requestSeatedRecalibrate() { recalibrate_ = true; }
    // 이번 프레임에 원점에 더할 높이. seated 가 꺼졌거나 바닥 기준이 아니면 0(잰 값도 지워 다시 켜면 새로 잼).
    // headValid · headY 는 바닥 기준 공간의 머리 높이(m)입니다. 처음 · 다시 맞추기 요청 뒤 유효한 머리에서 잽니다.
    float seatedLiftFor(bool seated, bool floorSpace, float standingEye, bool headValid, float headY);
    float seatedHead() const { return seatedHead_; }   // 0 = 아직 재지 않음

private:
    float pendingLin_ = 0.f, pendingYaw_ = 0.f;
    float vignette_   = 0.f;
    bool  recalibrate_ = false;
    float seatedHead_  = 0.f;
};
