#pragma once
#include "HXrInput.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class EInputEvent { Pressed, Released, Held };

class IWindow;

// ── HInputSystem ──────────────────────────────────────────────────────────────
// 관찰자 기반 입력 시스템. 키 바인딩과 게임 로직을 분리한다.
//
// 사용법:
//   [설정] HInputSystem::get().bind("Jump", HInput::KEY_SPACE);
//          HInputSystem::get().bindXrButton("Jump", HXrButton::RightPrimary);
//          HInputSystem::get().bindAxis("MoveX", HXrAxis::LeftStickX);
//          HInputSystem::get().bindKeyAxis("MoveY", HInput::KEY_W, HInput::KEY_S);
//          HInputSystem::get().bindMouseAxis("LookX", HMouseAxis::X, 1.f);
//          또는 프로젝트 설정(assets/project.ini)의 input. 줄 — applyBinding · defaultBindings
//   [구독] handle = HInputSystem::get().subscribe("Jump", EInputEvent::Pressed, fn);
//   [해제] HInputSystem::get().unsubscribe(handle);
//   [읽기] float x = HInputSystem::get().axis("MoveX");  bool g = held("GrabLeft");  손 자세는 xr().hands[i]
//   [루프] HInputSystem::get().update(dt, win);
//
// 컴포넌트는 키 코드도 OpenXR 도 직접 참조하지 않는다. VR 입력은 XR 모듈이 매 프레임
// setXrInput 으로 넣는다(DEF-20). 예전에는 키보드 디지털 입력만 있었다.

// 디지털 액션에 묶을 수 있는 컨트롤러 버튼. 트리거 · 그립은 절반 넘게 당기면 눌림입니다.
enum class HXrButton {
    LeftPrimary, LeftSecondary, LeftStickClick, LeftMenu,    // 왼손 X · Y · 스틱 · 메뉴
    RightPrimary, RightSecondary, RightStickClick,           // 오른손 A · B · 스틱
    LeftTrigger, LeftGrip, RightTrigger, RightGrip,          // 아날로그를 0.5 에서 끊음(2026-10-15)
};

// 이름에 묶을 수 있는 아날로그 축. 트리거 · 그립은 0..1, 스틱은 -1..1.
enum class HXrAxis {
    LeftTrigger, RightTrigger, LeftSqueeze, RightSqueeze,
    LeftStickX, LeftStickY, RightStickX, RightStickY,
};

// 마우스 시선 축. 값은 이번 프레임의 이동량(픽셀) × 배율입니다.
enum class HMouseAxis { X, Y };

class HInputSystem {
public:
    static HInputSystem& get() { static HInputSystem inst; return inst; }
    HInputSystem(const HInputSystem&)            = delete;
    HInputSystem& operator=(const HInputSystem&) = delete;

    using Handle = uint32_t;
    using Fn     = std::function<void(float dt)>;

    // ── 바인딩 (프로젝트 설정 레이어, 컴포넌트 금지) ──────────────────────────
    void bind        (const std::string& action, int keyCode);
    void bindXrButton(const std::string& action, HXrButton button);
    void unbind      (const std::string& action);   // 키 · 버튼 바인딩 모두
    void bindAxis    (const std::string& axis, HXrAxis source);
    // 키 두 개를 축으로: pos 를 누르면 +1, neg 를 누르면 -1, 둘 다면 0 (2026-10-15).
    void bindKeyAxis (const std::string& axis, int posKey, int negKey);
    // 마우스 시선(setLookDelta 로 넣은 이번 프레임 이동량) × scale 을 축으로(2026-10-15).
    void bindMouseAxis(const std::string& axis, HMouseAxis which, float scale = 1.f);
    void unbindAxis  (const std::string& axis);     // XR · 키 · 마우스 모두
    void clearBindings();

    // 프로젝트 설정의 한 줄을 바인딩으로 적용합니다(2026-10-15). key 는 "input.action.이름" 또는 "input.axis.이름",
    // value 는 쉼표로 나눈 소스 목록입니다. 액션: key:W · xr:left.grip · xr:right.a 등. 축: keys:W/S · xr:left.stick.y ·
    // mouse:x · mouse:y*0.5 등. 이미 있던 같은 이름의 바인딩은 바꿉니다. 하나라도 읽지 못하면 false 와 이유.
    bool applyBinding(const std::string& key, const std::string& value, std::string& error);
    // 기본 바인딩(이동 · 회전 · 시선 · 잡기). 프로젝트 설정에 input. 줄이 하나도 없으면 엔진이 이것을 씁니다.
    static std::vector<std::pair<std::string, std::string>> defaultBindings();

    // ── 구독 (컴포넌트 레이어) ─────────────────────────────────────────────────
    Handle subscribe  (const std::string& action, EInputEvent when, Fn fn);
    void   unsubscribe(Handle h);
    // 액션이 지금 눌려 있는지(마지막 update 기준). 잡기처럼 누르는 동안 이어지는 동작에 씁니다.
    bool   held(const std::string& action) const;

    // ── VR 입력 (XR 모듈이 매 프레임 넣음) ────────────────────────────────────
    void                 setXrInput(const HXrInputState& s) { xr_ = s; }
    const HXrInputState& xr() const { return xr_; }

    // ── 마우스 시선(에디터가 Play 중 오른쪽 버튼 끌기 양을 넣고, 엔진이 프레임 끝에 비움) ─────────
    void setLookDelta(float dx, float dy) { lookDx_ = dx; lookDy_ = dy; }

    // ── 햅틱(컨트롤러 진동). XR 모듈이 받는 곳을 정해 둡니다. hand: 0 = 왼손, 1 = 오른손 ──────────
    using HapticSink = std::function<void(int hand, float amplitude, float seconds)>;
    void setHapticSink(HapticSink s) { haptic_ = std::move(s); }
    void haptic(int hand, float amplitude, float seconds) const { if (haptic_) haptic_(hand, amplitude, seconds); }

    // 묶인 축 값입니다. XR 소스는 절댓값이 가장 큰 것 하나, 거기에 키 축과 마우스 축을 더합니다. 묶이지 않았으면 0.
    float axis(const std::string& axis) const;
    static float axisValue(const HXrInputState& s, HXrAxis a);
    static bool  buttonDown(const HXrInputState& s, HXrButton b);

    // ── 프레임 갱신 (메인 루프에서 1회 호출) ──────────────────────────────────
    // keyboardCaptured 가 참이면(에디터 텍스트 입력 중) 모든 키를 뗀 것으로 봅니다. 그러지
    // 않으면 이름을 타이핑하는 동안 같은 키에 바인딩된 게임 액션이 함께 발화합니다(DEF-67).
    // 눌려 있던 액션은 Released 를 한 번 받습니다. 컨트롤러 버튼은 키보드 캡처와 상관없습니다.
    void update(float dt, IWindow& win, bool keyboardCaptured = false);
    // 키 상태를 함수로 받는 판(창 없이 시험할 때).
    void update(float dt, const std::function<bool(int)>& isKeyDown, bool keyboardCaptured = false);

private:
    HInputSystem() = default;

    struct Binding  { std::vector<int> keys; std::vector<HXrButton> buttons; bool prevHeld = false; };
    struct Listener { std::string action; EInputEvent when; Fn fn;  };
    struct KeyAxis  { int pos = 0, neg = 0; };
    struct MouseAxis { HMouseAxis which = HMouseAxis::X; float scale = 1.f; };
    struct AxisBinding {
        std::vector<HXrAxis>   xr;
        std::vector<KeyAxis>   keys;
        std::vector<MouseAxis> mouse;
        float keyValue = 0.f;   // 마지막 update 의 키 축 값
    };

    std::unordered_map<std::string, Binding>     bindings_;
    std::unordered_map<std::string, AxisBinding> axes_;
    std::unordered_map<Handle,      Listener>    listeners_;
    HXrInputState xr_;
    float         lookDx_ = 0.f, lookDy_ = 0.f;
    HapticSink    haptic_;
    Handle next_ = 0;
};
