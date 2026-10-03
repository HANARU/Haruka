#pragma once
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
//   [구독] handle = HInputSystem::get().subscribe("Jump", EInputEvent::Pressed, fn);
//   [해제] HInputSystem::get().unsubscribe(handle);
//   [루프] HInputSystem::get().update(dt, win);
//
// 컴포넌트는 키 코드를 직접 참조하지 않는다.

class HInputSystem {
public:
    static HInputSystem& get() { static HInputSystem inst; return inst; }

    HInputSystem(const HInputSystem&)            = delete;
    HInputSystem& operator=(const HInputSystem&) = delete;

    using Handle = uint32_t;
    using Fn     = std::function<void(float dt)>;

    // ── 바인딩 (프로젝트 설정 레이어, 컴포넌트 금지) ──────────────────────────
    void bind  (const std::string& action, int keyCode);
    void unbind(const std::string& action);

    // ── 구독 (컴포넌트 레이어) ─────────────────────────────────────────────────
    Handle subscribe  (const std::string& action, EInputEvent when, Fn fn);
    void   unsubscribe(Handle h);

    // ── 프레임 갱신 (메인 루프에서 1회 호출) ──────────────────────────────────
    // keyboardCaptured 가 참이면(에디터 텍스트 입력 중) 모든 키를 뗀 것으로 봅니다. 그러지
    // 않으면 이름을 타이핑하는 동안 같은 키에 바인딩된 게임 액션이 함께 발화합니다(DEF-67).
    // 눌려 있던 액션은 Released 를 한 번 받습니다.
    void update(float dt, IWindow& win, bool keyboardCaptured = false);

private:
    HInputSystem() = default;

    struct Binding  { std::vector<int> keys; bool prevHeld = false; };
    struct Listener { std::string action; EInputEvent when; Fn fn;  };

    std::unordered_map<std::string, Binding>  bindings_;
    std::unordered_map<Handle,      Listener> listeners_;
    Handle next_ = 0;
};
