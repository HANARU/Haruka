#pragma once
#include "HXrInput.hpp"
#include "../platform/Gamepad.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

enum class EInputEvent { Pressed, Released, Held };

class IWindow;
class UserSettings;

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
//   [컨텍스트] pushContext("Menu", 10) · popContext("Menu")   (Docs/Input_Extensions.md D5)
//   [재지정] rebind(ctx.settings, "input.action.Jump", "key:space", "key:J")   (D6)
//
// 컴포넌트는 키 코드도 OpenXR 도 직접 참조하지 않는다. VR 입력은 XR 모듈이 매 프레임
// setXrInput 으로 넣는다(DEF-20). 예전에는 키보드 디지털 입력만 있었다. 데스크톱 게임패드는 창(플랫폼 층)이 읽어
// update(dt, IWindow&) 가 setGamepad 로 넣는다(2026-11-21).

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

// 축 수정자(D4). input.mod.<축> = deadzone:0.2, axial, curve:2, sens:1.5, invert
struct HAxisMods {
    float deadzone = -1.f;    // < 0 = 소스 기본값(패드 0.15, XR 0). 0 ~ 0.95
    bool  axial    = false;   // 거짓 = 원형(스틱 벡터 길이), 참 = 축별
    float curve    = 1.f;     // 응답 곡선 지수(부호 × |x|^curve), 0.1 ~ 10
    float sens     = 1.f;     // 감도(축 전체 값에 곱함), 0.01 ~ 100
    bool  invert   = false;   // 축 전체 값의 부호를 바꿈
};

// 재지정 결과(D6).
struct HRebindResult {
    bool                     ok = false;
    std::string              error;
    std::string              line;        // 바꾼 뒤 바인딩 글("none" = 비움)
    std::vector<std::string> conflicts;   // 같은 컨텍스트에서 같은 소스를 쓰는 다른 바인딩 키
};

class HInputSystem {
public:
    static HInputSystem& get() { static HInputSystem inst; return inst; }
    HInputSystem(const HInputSystem&)            = delete;
    HInputSystem& operator=(const HInputSystem&) = delete;

    using Handle = uint32_t;
    using Fn     = std::function<void(float dt)>;
    using Lines  = std::vector<std::pair<std::string, std::string>>;

    static constexpr const char* kDefaultContext = "default";

    // ── 바인딩 (프로젝트 설정 레이어, 컴포넌트 금지). 아래 bind* 는 default 컨텍스트에 더합니다 ─────────────
    void bind        (const std::string& action, int keyCode);
    void bindXrButton(const std::string& action, HXrButton button);
    void bindPad     (const std::string& action, HPadButton button);
    void unbind      (const std::string& action);   // 키 · 버튼 바인딩 모두
    void bindAxis    (const std::string& axis, HXrAxis source);
    void bindPadAxis (const std::string& axis, HPadAxis source);
    // 키 두 개를 축으로: pos 를 누르면 +1, neg 를 누르면 -1, 둘 다면 0 (2026-10-15).
    void bindKeyAxis (const std::string& axis, int posKey, int negKey);
    // 마우스 시선(setLookDelta 로 넣은 이번 프레임 이동량) × scale 을 축으로(2026-10-15).
    void bindMouseAxis(const std::string& axis, HMouseAxis which, float scale = 1.f);
    void unbindAxis  (const std::string& axis);     // XR · 패드 · 키 · 마우스 모두
    void setAxisMods (const std::string& axis, const HAxisMods& mods);
    // 모든 컨텍스트의 바인딩 · 수정자와 적용한 사용자 덮어쓰기 기록을 지웁니다. 켜 둔 컨텍스트 스택 · 구독은 둡니다.
    void clearBindings();

    // 프로젝트 설정의 한 줄을 바인딩으로 적용합니다(2026-10-15). key 는 "input.action.이름" · "input.axis.이름" · "input.mod.이름",
    // 또는 컨텍스트 안의 "input.context.컨텍스트.action|axis|mod.이름"(D5). value 는 쉼표로 나눈 소스 목록입니다.
    // 액션: key:W · pad:a · xr:left.grip · xr:right.a 등. 축: keys:W/S · pad:left.x · xr:left.stick.y · mouse:x · mouse:y*0.5 등.
    // 수정자: deadzone:0.2 · radial · axial · curve:2 · sens:1.5 · invert. 이미 있던 같은 이름의 바인딩은 바꿉니다.
    // 하나라도 읽지 못하면 false 와 이유(있던 바인딩은 그대로).
    bool applyBinding(const std::string& key, const std::string& value, std::string& error);
    // 기본 바인딩(이동 · 회전 · 시선 · 잡기). 프로젝트 설정에 input. 줄이 하나도 없으면 엔진이 이것을 씁니다.
    static Lines defaultBindings();
    // 사용자 덮어쓰기 · 재지정이 기준으로 삼는 프로젝트 줄(D6 · D7). 엔진이 시작할 때 넣습니다. 바인딩은 바꾸지 않습니다.
    void setProjectLines(Lines lines) { projectLines_ = std::move(lines); }
    // 바인딩 키인지(input.action. · input.axis. · input.mod. · input.context.).
    static bool isBindingKey(const std::string& key);

    // ── 입력 컨텍스트(D5) ─────────────────────────────────────────────────────────
    // 이름 있는 바인딩 묶음을 스택에 켭니다. 우선순위가 높을수록 위, 같으면 나중에 민 것이 위. 이미 켜져 있으면 우선순위만 바꿉니다.
    // 위 컨텍스트가 매핑한 소스(키 · 버튼 · 축)는 아래 컨텍스트에 가지 않습니다. blockLower 면 그 아래 컨텍스트를 모두 막습니다.
    // default 컨텍스트는 언제나 맨 아래에 켜져 있어 밀고 뺄 수 없습니다.
    void pushContext(const std::string& name, int priority = 0, bool blockLower = false);
    bool popContext (const std::string& name);   // 켜져 있었으면 true
    // 켜진 컨텍스트 이름, 위에서 아래로(default 포함).
    std::vector<std::string> activeContexts() const;

    // ── 사용자 재지정(D6 · D7) ────────────────────────────────────────────────────
    // 지금 바인딩을 정규 글로("key:W, pad:a"). 없으면 "none". 수정자 키면 수정자 글.
    std::string bindingLine(const std::string& key) const;
    // 바인딩 키(액션 · 축)의 소스 from 을 to 로 바꾸고(from 이 비면 더함, to 가 비면 뺌) 바로 적용한 뒤 settings 에 씁니다.
    // 결과가 프로젝트 줄과 같으면 덮어쓰기를 지웁니다(바뀐 것만 저장). 같은 컨텍스트에서 to 를 쓰는 다른 바인딩은 conflicts 에.
    HRebindResult rebind(UserSettings& settings, const std::string& key, const std::string& from, const std::string& to);
    // 덮어쓰기를 지워 프로젝트 줄로 돌립니다(없으면 비움). 덮어쓰기가 있었으면 true.
    bool resetBinding(UserSettings& settings, const std::string& key);
    int  resetAllBindings(UserSettings& settings);   // 지운 수
    // 같은 컨텍스트에서 source 를 쓰는 바인딩 키(exceptKey 제외). source 를 읽지 못하면 빈 목록.
    std::vector<std::string> conflictsFor(const std::string& source, const std::string& context = kDefaultContext,
                                          const std::string& exceptKey = {}) const;
    // 사용자 설정의 input.* 덮어쓰기를 프로젝트 줄 위에 적용합니다(예전 Engine::applyUserInput, D7). 지난번과 같으면 아무것도 하지
    // 않고 false. 지운 덮어쓰기는 프로젝트 줄로 돌리고, "none" 은 비우며, 틀린 글은 warnings 에 남기고 프로젝트 줄을 둡니다.
    struct OverrideResult { bool changed = false; int applied = 0; };
    OverrideResult applyUserOverrides(const UserSettings& settings, std::vector<std::string>& warnings);

    // ── 구독 (컴포넌트 레이어) ─────────────────────────────────────────────────
    Handle subscribe  (const std::string& action, EInputEvent when, Fn fn);
    void   unsubscribe(Handle h);
    // 액션이 지금 눌려 있는지(마지막 update 기준). 잡기처럼 누르는 동안 이어지는 동작에 씁니다.
    bool   held(const std::string& action) const;

    // ── VR 입력 (XR 모듈이 매 프레임 넣음) ────────────────────────────────────
    void                 setXrInput(const HXrInputState& s) { xr_ = s; }
    const HXrInputState& xr() const { return xr_; }

    // ── 게임패드(D1). update(dt, IWindow&) 가 창에서 읽어 넣습니다. 시험은 가짜 상태를 직접 넣습니다 ───────────
    void                 setGamepad(const HGamepadState& s) { pad_ = s; }
    const HGamepadState& gamepad() const { return pad_; }

    // ── 마우스 시선(에디터가 Play 중 오른쪽 버튼 끌기 양을 넣고, 엔진이 프레임 끝에 비움) ─────────
    void setLookDelta(float dx, float dy) { lookDx_ = dx; lookDy_ = dy; }

    // ── 햅틱(컨트롤러 진동). XR 모듈이 받는 곳을 정해 둡니다. hand: 0 = 왼손, 1 = 오른손 ──────────
    using HapticSink = std::function<void(int hand, float amplitude, float seconds)>;
    void setHapticSink(HapticSink s) { haptic_ = std::move(s); }
    void haptic(int hand, float amplitude, float seconds) const { if (haptic_) haptic_(hand, amplitude, seconds); }

    // 묶인 축 값입니다. 아날로그 소스(XR · 패드)는 데드존 · 곡선을 거친 뒤 절댓값이 가장 큰 것 하나, 거기에 키 축과 마우스 축을
    // 더하고 감도 · 반전을 곱합니다(D4). 여러 컨텍스트에 있으면 합. 묶이지 않았으면 0.
    float axis(const std::string& axis) const;
    static float axisValue(const HXrInputState& s, HXrAxis a);
    static bool  buttonDown(const HXrInputState& s, HXrButton b);
    static bool  padDown(const HGamepadState& s, HPadButton b);
    // 데드존 · 곡선(D4). other 는 같은 스틱의 다른 성분(트리거는 0, stick 거짓).
    static float shapeAnalog(float v, float other, bool stick, const HAxisMods& m, float deadzone);

    // ── 프레임 갱신 (메인 루프에서 1회 호출) ──────────────────────────────────
    // keyboardCaptured 가 참이면(에디터 텍스트 입력 중) 모든 키를 뗀 것으로 봅니다. 그러지
    // 않으면 이름을 타이핑하는 동안 같은 키에 바인딩된 게임 액션이 함께 발화합니다(DEF-67).
    // 눌려 있던 액션은 Released 를 한 번 받습니다. 컨트롤러 버튼 · 게임패드는 키보드 캡처와 상관없습니다.
    void update(float dt, IWindow& win, bool keyboardCaptured = false);
    // 키 상태를 함수로 받는 판(창 없이 시험할 때). 게임패드는 setGamepad 로 넣은 상태를 씁니다.
    void update(float dt, const std::function<bool(int)>& isKeyDown, bool keyboardCaptured = false);

private:
    HInputSystem() = default;

    struct Binding  {
        std::vector<int> keys; std::vector<HXrButton> buttons; std::vector<HPadButton> pads;
        std::vector<std::string> sources;   // 정규 소스 글(bindingLine · 재지정)
    };
    struct Listener { std::string action; EInputEvent when; Fn fn;  };
    struct KeyAxis  { int pos = 0, neg = 0; };
    struct MouseAxis { HMouseAxis which = HMouseAxis::X; float scale = 1.f; };
    struct AxisBinding {
        std::vector<HXrAxis>   xr;
        std::vector<HPadAxis>  pads;
        std::vector<KeyAxis>   keys;
        std::vector<MouseAxis> mouse;
        std::vector<std::string> sources;
    };
    struct Context {
        std::unordered_map<std::string, Binding>     actions;
        std::unordered_map<std::string, AxisBinding> axes;
        std::unordered_map<std::string, HAxisMods>   mods;
    };
    struct Active { std::string name; int priority = 0; bool blockLower = false; uint64_t seq = 0; };
    // 켜진 컨텍스트 하나와 그보다 위 컨텍스트가 가져간 소스(D5). 스택 · 바인딩이 바뀔 때만 다시 만듭니다.
    struct Layer { std::string name; const Context* ctx = nullptr; std::unordered_set<int> hidden; };

    enum class KeyKind { Action, Axis, Mod };
    static bool parseKey(const std::string& key, std::string& ctx, KeyKind& kind, std::string& name, std::string& error);
    static bool parseActionLine(const std::string& value, Binding& out, std::string& error);
    static bool parseAxisLine(const std::string& value, AxisBinding& out, std::string& error);
    static bool parseModLine(const std::string& value, HAxisMods& out, std::string& error);
    static std::string modText(const HAxisMods& m);
    static std::string makeKey(const std::string& ctx, KeyKind kind, const std::string& name);
    const std::string* projectLine(const std::string& key) const;
    void clearTarget(const std::string& key);       // 그 키의 바인딩 · 수정자를 지움
    void restoreProject(const std::string& key);    // 프로젝트 줄로(없으면 지움)
    static void claims(const Binding& b, std::unordered_set<int>& out);
    static void claims(const AxisBinding& b, std::unordered_set<int>& out);
    const std::vector<Layer>& layers() const;
    void touch() { layersDirty_ = true; }

    std::map<std::string, Context>   contexts_;    // default + 줄 · API 로 정의한 컨텍스트
    std::vector<Active>              stack_;       // 켠 컨텍스트(default 제외)
    uint64_t                         pushSeq_ = 0;
    mutable std::vector<Layer>       layers_;
    mutable bool                     layersDirty_ = true;
    std::unordered_map<std::string, bool> actionHeld_;   // 이름별 마지막 update 의 눌림
    std::unordered_set<int>          keysDown_;          // 마지막 update 에서 눌린(바인딩된) 키
    std::unordered_map<Handle, Listener> listeners_;
    Lines                            projectLines_;
    std::map<std::string, std::string> overridesApplied_;   // 지금 적용한 사용자 덮어쓰기
    HXrInputState xr_;
    HGamepadState pad_;
    float         lookDx_ = 0.f, lookDy_ = 0.f;
    HapticSink    haptic_;
    Handle next_ = 0;
};
