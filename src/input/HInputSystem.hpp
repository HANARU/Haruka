#pragma once
#include "HXrInput.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
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
//   [구독] handle = HInputSystem::get().subscribe("Jump", EInputEvent::Pressed, fn);
//   [해제] HInputSystem::get().unsubscribe(handle);
//   [읽기] float x = HInputSystem::get().axis("MoveX");  손 자세는 xr().hands[i]
//   [루프] HInputSystem::get().update(dt, win);
//
// 컴포넌트는 키 코드도 OpenXR 도 직접 참조하지 않는다. VR 입력은 XR 모듈이 매 프레임
// setXrInput 으로 넣는다(DEF-20). 예전에는 키보드 디지털 입력만 있었다.

// 디지털 액션에 묶을 수 있는 컨트롤러 버튼.
enum class HXrButton {
    LeftPrimary, LeftSecondary, LeftStickClick, LeftMenu,    // 왼손 X · Y · 스틱 · 메뉴
    RightPrimary, RightSecondary, RightStickClick,           // 오른손 A · B · 스틱
};

// 이름에 묶을 수 있는 아날로그 축. 트리거 · 그립은 0..1, 스틱은 -1..1.
enum class HXrAxis {
    LeftTrigger, RightTrigger, LeftSqueeze, RightSqueeze,
    LeftStickX, LeftStickY, RightStickX, RightStickY,
};

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
    void unbindAxis  (const std::string& axis);

    // ── 구독 (컴포넌트 레이어) ─────────────────────────────────────────────────
    Handle subscribe  (const std::string& action, EInputEvent when, Fn fn);
    void   unsubscribe(Handle h);

    // ── VR 입력 (XR 모듈이 매 프레임 넣음) ────────────────────────────────────
    void                 setXrInput(const HXrInputState& s) { xr_ = s; }
    const HXrInputState& xr() const { return xr_; }
    // 묶인 축 값입니다. 여러 소스를 묶었으면 절댓값이 가장 큰 값. 묶이지 않았거나 XR 입력이 없으면 0.
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

    std::unordered_map<std::string, Binding>              bindings_;
    std::unordered_map<std::string, std::vector<HXrAxis>> axes_;
    std::unordered_map<Handle,      Listener>             listeners_;
    HXrInputState xr_;
    Handle next_ = 0;
};
