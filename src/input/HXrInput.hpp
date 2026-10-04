#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// ── VR 입력 상태 (DEF-20) ──────────────────────────────────────────────────────
// 헤드셋과 두 컨트롤러의 한 프레임 입력입니다. XR 모듈(src/xr)이 OpenXR 액션으로 채우고
// HInputSystem 이 보관합니다. 게임 코드는 OpenXR 를 모른 채 HInputSystem 에서 읽습니다.
// 예전 사양은 창(IWindow)에 아날로그 축과 포즈를 넣으라고 했지만, UE · Unity · Godot 모두 XR 입력을
// 창과 분리된 XR 모듈에서 다룹니다. 창은 키보드 · 마우스만 맡습니다.
//
// 자세는 월드가 아니라 **XR 원점 기준**입니다. 원점은 활성 카메라 액터의 위치와 요(yaw)이고,
// 렌더러가 눈 자세에 쓰는 것과 같은 원점입니다. 월드로 옮기려면 원점 변환을 곱합니다.

struct HPose {
    glm::vec3 position{ 0.f };
    glm::quat orientation{ 1.f, 0.f, 0.f, 0.f };
    bool      valid = false;   // 추적 중인지(위치 · 방향 모두)
};

struct HHandInput {
    bool      active = false;           // 컨트롤러가 연결되어 입력을 내는지
    HPose     grip;                     // 쥐는 손의 자세(물체를 붙일 곳)
    HPose     aim;                      // 가리키는 방향(레이 · 메뉴)
    glm::vec3 linearVelocity{ 0.f };    // 원점 기준 m/s (던지기)
    glm::vec3 angularVelocity{ 0.f };   // 원점 기준 rad/s
    float     trigger = 0.f;            // 0..1
    float     squeeze = 0.f;            // 0..1 (그립 버튼)
    glm::vec2 thumbstick{ 0.f };        // -1..1, 위가 +y
    bool      primary         = false;  // 왼손 X · 오른손 A
    bool      secondary       = false;  // 왼손 Y · 오른손 B
    bool      thumbstickClick = false;
    bool      menu            = false;  // 왼손 메뉴 버튼(오른손은 시스템 예약)
};

struct HXrInputState {
    bool       focused = false;   // XR 세션이 입력을 받는 중(FOCUSED). 아니면 나머지는 기본값
    HPose      head;              // 헤드셋(두 눈 사이)
    HHandInput hands[2];          // 0 = 왼손, 1 = 오른손
};
