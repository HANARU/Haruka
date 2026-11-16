#pragma once
// ── verblib(Freeverb 기반 알고리즘 리버브) 설정 (2026-11-06, Docs/Audio_Design.md 9-5) ──────────────────
// miniaudio 저장소 extras/nodes/ma_reverb_node/verblib.h 를 씁니다(MIT No Attribution 또는 퍼블릭 도메인, miniaudio 태그와 함께 고정).
// extras 의 ma_reverb_node 는 설정 값을 적용하지 않고(초기화가 verblib_initialize 만 부름) 매개변수 변경이 오디오 스레드와 겹쳐서,
// 노드는 AudioSystem.cpp 에서 직접 감쌉니다.
//
// 구조체 크기가 이 매크로로 정해지므로, verblib.h 는 반드시 이 헤더를 거쳐 포함합니다(구현부는 MiniaudioImpl.cpp).
// 엔진은 48 kHz 고정이라 44100 × 2 까지면 되고, 기본값 4 의 절반인 약 199 KiB 로 줄어듭니다.
#define verblib_max_sample_rate_multiplier 2

#if defined(_MSC_VER)
    #pragma warning(push, 0)
    #pragma warning(disable: 4244 4305)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Weverything"
#endif
#include "extras/nodes/ma_reverb_node/verblib.h"
#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#endif
