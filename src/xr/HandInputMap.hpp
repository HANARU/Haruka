#pragma once
// 컨트롤러 입력 → 손가락 굽힘(2026-11-07, Docs/Hand_Grasp_Design.md 4-2, B 단계).
//
// Meta Capsense 와 같은 발상입니다. 손가락마다 굽힘 목표 0(편 손) ~ 1(쥔 손)을 정하고, 갑자기 튀지 않도록 짧은 시간 상수로
// 따라가게 합니다. GPU · 컴포넌트 없이 시험할 수 있도록 순수 함수로 둡니다.
//
//   검지          터치 입력이 있으면: 트리거에 닿지 않으면 0(가리키기), 닿으면 kIndexTouchCurl + 나머지 x 트리거 값
//                 터치 입력이 없으면(단순 컨트롤러): 트리거 값 그대로
//   중지 ~ 소지   그립 값 그대로
//   엄지          터치 입력이 있으면: 스틱 · 버튼 · 엄지 받침에 닿으면 kThumbDownCurl, 아니면 0(들기)
//                 터치 입력이 없으면: 0

#include "HandSkeleton.hpp"
#include "../input/HXrInput.hpp"

#include <algorithm>
#include <cmath>

namespace hhand {

constexpr float kIndexTouchCurl = 0.3f;    // 검지가 트리거에 닿기만 했을 때
constexpr float kThumbDownCurl  = 0.6f;    // 엄지가 스틱 · 버튼에 닿았을 때
constexpr float kCurlSmoothTau  = 0.03f;   // 초. 목표와의 차이가 약 50 ms 에 80% 줄어듦

inline void curlTargets(const HHandInput& h, float out[kFingerCount]) {
    const float trigger = std::clamp(h.trigger, 0.f, 1.f);
    const float squeeze = std::clamp(h.squeeze, 0.f, 1.f);
    if (h.hasTouch) {
        // 트리거를 당기고 있으면 닿은 것으로 봅니다(터치 센서가 늦게 오는 프레임 대비).
        const bool onTrigger = h.triggerTouch || trigger > 0.01f;
        out[(int)EFinger::Index] = onTrigger ? kIndexTouchCurl + (1.f - kIndexTouchCurl) * trigger : 0.f;
        out[(int)EFinger::Thumb] = h.thumbTouch ? kThumbDownCurl : 0.f;
    } else {
        out[(int)EFinger::Index] = trigger;
        out[(int)EFinger::Thumb] = 0.f;
    }
    out[(int)EFinger::Middle] = squeeze;
    out[(int)EFinger::Ring]   = squeeze;
    out[(int)EFinger::Little] = squeeze;
}

// cur 를 target 쪽으로 dt 초만큼 옮깁니다(지수 감쇠). dt <= 0 이면 그대로, tau <= 0 이면 바로 target.
inline float smoothCurl(float cur, float target, float dt, float tau = kCurlSmoothTau) {
    if (dt <= 0.f) return cur;
    if (tau <= 0.f) return target;
    const float a = 1.f - std::exp(-dt / tau);
    return cur + (target - cur) * a;
}

} // namespace hhand
