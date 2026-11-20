// VR 입력(DEF-20) 검산입니다. XR 모듈이 넣는 입력 상태를 HInputSystem 이 아날로그 축과 디지털 액션으로
// 내주는지 확인합니다. OpenXR 도 창도 필요 없습니다(키 상태는 함수로 넣음).
//
// 이 테스트가 존재하는 이유: 예전 입력은 키보드 디지털뿐이라 VR 컨트롤러의 스틱 · 트리거 · 버튼을 받을 수
// 없었습니다. 헤드셋 없이도 바인딩 · 눌림 엣지 · 축 값 규칙을 확인할 수 있어야 합니다.
//
// 실행: build/Release/HarukaInputTest.exe

#include "../src/input/HInputSystem.hpp"
#include "../src/core/UserSettings.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

bool noKeys(int) { return false; }

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

std::function<bool(int)> keysDown(std::vector<int> down) {
    return [down](int k) { for (int d : down) if (d == k) return true; return false; };
}

// ── 게임패드(Docs/Input_Extensions.md D1 · D2 · D3) ─────────────────────────────────────────
void testGamepad(HInputSystem& in) {
    std::printf("\n== 게임패드 ==\n");
    {
        bool isPad[hgamepad::kMaxPads] = {};
        check(hgamepad::pick(-1, isPad) == -1, "패드가 없으면 -1");
        isPad[2] = isPad[5] = true;
        check(hgamepad::pick(-1, isPad) == 2, "처음에는 번호가 가장 작은 패드");
        check(hgamepad::pick(5, isPad) == 5, "쓰던 패드가 붙어 있으면 그대로(두 번째 패드를 꽂아도 바뀌지 않음)");
        isPad[5] = false;
        check(hgamepad::pick(5, isPad) == 2, "쓰던 패드가 빠지면 남은 패드 중 첫 번째");
    }

    in.clearBindings();
    in.setXrInput({});
    std::string err;
    check(in.applyBinding("input.action.Jump", "key:Space, pad:a", err), "pad:a 동작 줄");
    check(in.applyBinding("input.action.Fire", "pad:rt", err), "pad:rt(트리거를 버튼으로)");
    check(in.applyBinding("input.axis.MoveX", "keys:D/A, pad:left.x", err), "pad:left.x 축 줄");
    check(in.applyBinding("input.axis.Throttle", "pad:lt", err), "pad:lt 축 줄");
    check(!in.applyBinding("input.action.Jump", "pad:z", err), "모르는 패드 버튼은 거부");
    check(!in.applyBinding("input.axis.MoveX", "pad:a", err), "버튼 이름은 축 소스가 아님");

    int pressed = 0, released = 0;
    const auto h1 = in.subscribe("Jump", EInputEvent::Pressed,  [&](float) { ++pressed; });
    const auto h2 = in.subscribe("Jump", EInputEvent::Released, [&](float) { ++released; });

    HGamepadState pad;
    pad.connected = true;
    pad.buttons[size_t(HPadButton::A)] = true;
    in.setGamepad(pad);
    in.update(0.016f, noKeys);
    in.update(0.016f, noKeys, /*keyboardCaptured=*/true);
    check(pressed == 1 && in.held("Jump"), "가짜 패드 A → Jump 눌림(키보드 캡처와 상관없음)");

    pad.axes[size_t(HPadAxis::RightTrigger)] = 0.7f;
    in.setGamepad(pad);
    in.update(0.016f, noKeys);
    check(in.held("Fire"), "오른쪽 트리거를 절반 넘게 당기면 Fire 눌림");

    pad.axes[size_t(HPadAxis::LeftX)]       = 0.6f;
    pad.axes[size_t(HPadAxis::LeftTrigger)] = 0.4f;
    in.setGamepad(pad);
    check(near(in.axis("MoveX"), (0.6f - 0.15f) / 0.85f), "스틱은 기본 원형 데드존 0.15 뒤 0 ~ 1 로 다시 펼침");
    check(near(in.axis("Throttle"), (0.4f - 0.15f) / 0.85f), "트리거 축도 기본 데드존");
    pad.axes[size_t(HPadAxis::LeftX)] = 0.1f;
    in.setGamepad(pad);
    check(in.axis("MoveX") == 0.f, "데드존 안의 흔들림은 0");
    pad.axes[size_t(HPadAxis::LeftX)] = -1.f;
    in.setGamepad(pad);
    in.update(0.016f, keysDown({ 'D' }));
    check(near(in.axis("MoveX"), 0.f), "패드 -1 과 키 +1 은 더함");

    // 해제: 상태가 모두 0 이 되어 눌려 있던 동작은 Released 를 한 번 받습니다.
    in.setGamepad({});
    in.update(0.016f, noKeys);
    in.update(0.016f, noKeys);
    check(released == 1 && !in.held("Jump") && !in.held("Fire"), "패드가 빠지면 Released 한 번, 눌림 풀림");
    check(in.axis("MoveX") == 0.f && in.axis("Throttle") == 0.f, "빠진 패드의 축은 0");

    in.unsubscribe(h1); in.unsubscribe(h2);
    in.clearBindings();
}

// ── 수정자(D4) ───────────────────────────────────────────────────────────────────────
void testModifiers(HInputSystem& in) {
    std::printf("\n== 수정자 ==\n");
    std::string err;
    {
        HAxisMods m;
        check(near(HInputSystem::shapeAnalog(0.3f, 0.3f, true, m, 0.4f),
                   0.3f * ((std::sqrt(0.18f) - 0.4f) / 0.6f) / std::sqrt(0.18f)), "원형 데드존: 대각선 (0.3, 0.3) 은 길이로 판정해 살아남음");
        m.axial = true;
        check(HInputSystem::shapeAnalog(0.3f, 0.3f, true, m, 0.4f) == 0.f, "축별 데드존: 같은 입력이 성분마다 잘려 0");
        m = {};
        m.curve = 2.f;
        check(near(HInputSystem::shapeAnalog(0.5f, 0.f, false, m, 0.f), 0.25f)
              && near(HInputSystem::shapeAnalog(-0.5f, 0.f, false, m, 0.f), -0.25f), "곡선 2: ±0.5 → ±0.25(부호 유지)");
        check(near(HInputSystem::shapeAnalog(1.f, 0.f, false, m, 0.2f), 1.f), "끝까지 밀면 데드존 · 곡선 뒤에도 1");
    }

    in.clearBindings();
    HXrInputState xr;
    xr.focused = true;
    xr.hands[0].thumbstick = { 0.f, 0.05f };
    in.setXrInput(xr);
    check(in.applyBinding("input.axis.MoveY", "keys:W/S, xr:left.stick.y, pad:left.y", err), "축 줄");
    check(near(in.axis("MoveY"), 0.05f), "수정자가 없으면 XR 값은 그대로(기존 동작)");

    check(in.applyBinding("input.mod.MoveY", "Deadzone:0.2, axial, curve:2, sens:1.5, invert", err), "수정자 줄을 읽음");
    check(in.bindingLine("input.mod.MoveY") == "deadzone:0.2, axial, curve:2, sens:1.5, invert", "수정자 정규 글");
    check(in.axis("MoveY") == 0.f, "지정한 데드존은 XR 소스에도 적용");
    xr.hands[0].thumbstick = { 0.f, 0.6f };
    in.setXrInput(xr);
    check(near(in.axis("MoveY"), -std::pow(0.5f, 2.f) * 1.5f), "XR 0.6 → 데드존 0.5 → 곡선 0.25 → 감도 0.375 → 반전");
    xr.hands[0].thumbstick = { 0.f, 0.f };
    in.setXrInput(xr);
    in.update(0.f, keysDown({ 'W' }));
    check(near(in.axis("MoveY"), -1.5f), "감도 · 반전은 키 축에도 적용");

    check(!in.applyBinding("input.mod.MoveY", "deadzone:2", err), "범위 밖 데드존은 거부");
    check(!in.applyBinding("input.mod.MoveY", "wobble", err), "모르는 수정자는 거부");
    in.update(0.f, keysDown({ 'W' }));
    check(near(in.axis("MoveY"), -1.5f), "틀린 수정자 줄은 있던 수정자를 바꾸지 않음");
    check(in.applyBinding("input.mod.MoveY", "none", err) && in.bindingLine("input.mod.MoveY") == "none", "none 은 기본값으로");
    check(near(in.axis("MoveY"), 1.f), "기본값이면 W 는 다시 +1");

    // 사용자 설정에서 지정(시선 반전 같은 플레이어 값).
    in.applyBinding("input.axis.LookY", "mouse:y", err);
    UserSettings us;
    check(us.set("input.mod.LookY", "invert, sens:2"), "사용자 설정이 input.mod. 를 받음");
    std::vector<std::string> warn;
    in.applyUserOverrides(us, warn);
    in.setLookDelta(0.f, 3.f);
    check(warn.empty() && near(in.axis("LookY"), -6.f), "사용자 설정의 수정자가 적용됨(마우스 3 × 2, 반전)");
    in.setLookDelta(0.f, 0.f);
    in.setXrInput({});
    in.clearBindings();
}

// ── 입력 컨텍스트(D5) ────────────────────────────────────────────────────────────────────
void testContexts(HInputSystem& in) {
    std::printf("\n== 입력 컨텍스트 ==\n");
    std::string err;
    in.clearBindings();
    check(in.applyBinding("input.action.Jump", "key:Space, pad:a", err), "default 의 Jump");
    check(in.applyBinding("input.axis.MoveY", "keys:W/S", err), "default 의 MoveY");
    check(in.applyBinding("input.context.Menu.action.Confirm", "key:Space", err), "Menu 컨텍스트의 Confirm");
    check(in.applyBinding("input.context.Menu.action.Up", "key:W", err), "Menu 컨텍스트의 Up");
    check(!in.applyBinding("input.context..action.X", "key:Space", err), "이름 없는 컨텍스트는 거부");
    check(!in.applyBinding("input.context.Menu.button.X", "key:Space", err), "action · axis · mod 가 아니면 거부");

    int jumpPressed = 0, jumpReleased = 0, confirm = 0;
    const auto h1 = in.subscribe("Jump", EInputEvent::Pressed,  [&](float) { ++jumpPressed; });
    const auto h2 = in.subscribe("Jump", EInputEvent::Released, [&](float) { ++jumpReleased; });
    const auto h3 = in.subscribe("Confirm", EInputEvent::Pressed, [&](float) { ++confirm; });

    in.update(0.f, keysDown({ ' ' }));
    check(jumpPressed == 1 && confirm == 0, "Menu 를 켜기 전: Space 는 Jump 만");

    in.pushContext("Menu", 10);
    in.update(0.f, keysDown({ ' ' }));
    check(confirm == 1 && !in.held("Jump") && jumpReleased == 1, "Menu 를 밀면 Space 는 Confirm, 눌려 있던 Jump 는 Released 한 번");
    in.update(0.f, keysDown({ 'W' }));
    check(in.axis("MoveY") == 0.f && in.held("Up"), "위 컨텍스트가 가져간 W 는 아래 MoveY 에 가지 않음");
    in.update(0.f, keysDown({ 'S' }));
    check(in.axis("MoveY") == -1.f, "가져가지 않은 S 는 그대로");

    HGamepadState pad;
    pad.connected = true;
    pad.buttons[size_t(HPadButton::A)] = true;
    in.setGamepad(pad);
    in.update(0.f, noKeys);
    check(in.held("Jump"), "Menu 가 쓰지 않는 패드 A 로는 Jump 가 됨");

    in.pushContext("Modal", 20, /*blockLower=*/true);
    in.update(0.f, keysDown({ 'S' }));
    check(!in.held("Jump") && in.axis("MoveY") == 0.f && !in.held("Up"), "blockLower 컨텍스트는 아래를 모두 막음");
    check((in.activeContexts() == std::vector<std::string>{ "Modal", "Menu", "default" }), "활성 순서: 우선순위 높은 것이 위");
    in.pushContext("Hud", 10);
    check((in.activeContexts() == std::vector<std::string>{ "Modal", "Hud", "Menu", "default" }), "같은 우선순위는 나중에 민 것이 위");
    check(in.popContext("Modal") && in.popContext("Hud") && !in.popContext("Modal"), "빼기(없는 것은 false)");

    check(in.popContext("Menu"), "Menu 빼기");
    in.setGamepad({});
    in.update(0.f, keysDown({ 'W' }));
    check(in.axis("MoveY") == 1.f && !in.held("Up"), "Menu 를 빼면 W 는 다시 MoveY");
    in.pushContext("default");   // default 는 밀 수 없음
    check(in.activeContexts().size() == 1, "default 는 언제나 맨 아래 하나");

    in.unsubscribe(h1); in.unsubscribe(h2); in.unsubscribe(h3);
    in.update(0.f, noKeys);
    in.clearBindings();
}

// ── 사용자 재지정 · 저장 · 다시 읽기(D6 · D7) ─────────────────────────────────────────────────
void testRebind(HInputSystem& in) {
    std::printf("\n== 사용자 재지정 ==\n");
    const HInputSystem::Lines project = {
        { "input.action.Jump",                 "key:Space, pad:a" },
        { "input.action.Fire",                 "key:F" },
        { "input.axis.MoveY",                  "keys:W/S, pad:left.y" },
        { "input.context.Menu.action.Confirm", "key:Enter" },
    };
    auto startup = [&](const UserSettings& s) {   // 엔진 시작과 같은 순서: 프로젝트 줄 → 기준 → 사용자 덮어쓰기
        in.clearBindings();
        std::string e;
        for (const auto& [k, v] : project) in.applyBinding(k, v, e);
        in.setProjectLines(project);
        std::vector<std::string> warn;
        in.applyUserOverrides(s, warn);
        return warn;
    };
    UserSettings us;
    startup(us);
    check(in.bindingLine("input.action.Jump") == "key:space, pad:a", "지금 바인딩을 정규 글로");

    HRebindResult r = in.rebind(us, "input.action.Jump", "key:SPACE", "key:j");
    check(r.ok && r.line == "key:J, pad:a" && r.conflicts.empty(), "Jump 의 Space 를 J 로");
    check(us.getString("input.action.Jump") == "key:J, pad:a", "사용자 설정에 바뀐 줄을 저장");
    check(!us.has("input.action.Fire") && !us.has("input.axis.MoveY"), "바꾸지 않은 바인딩은 저장하지 않음");
    in.update(0.f, keysDown({ 'J' }));
    check(in.held("Jump"), "바꾼 키가 바로 동작");
    in.update(0.f, keysDown({ ' ' }));
    check(!in.held("Jump"), "예전 키는 더 이상 동작하지 않음");

    r = in.rebind(us, "input.action.Fire", "", "key:J");
    check(r.ok && r.line == "key:F, key:J", "from 이 비면 더함");
    check((r.conflicts == std::vector<std::string>{ "input.action.Jump" }), "같은 키를 쓰는 다른 동작을 충돌로 알림");
    check((in.conflictsFor("pad:a") == std::vector<std::string>{ "input.action.Jump" }), "conflictsFor: pad:a 는 Jump");
    check(in.conflictsFor("key:Enter").empty() && in.conflictsFor("key:Enter", "Menu").size() == 1, "충돌은 같은 컨텍스트 안에서만");

    r = in.rebind(us, "input.axis.MoveY", "keys:W/S", "keys:Up/Down");
    check(r.ok && r.line == "keys:up/down, pad:left.y", "축의 키 쌍도 바꿈");
    r = in.rebind(us, "input.context.Menu.action.Confirm", "", "pad:a");
    check(r.ok && us.getString("input.context.Menu.action.Confirm") == "key:enter, pad:a", "컨텍스트 안의 동작도 재지정 · 저장");

    check(!in.rebind(us, "input.action.Fire", "key:Q", "key:R").ok, "묶이지 않은 소스는 바꾸지 못함");
    check(!in.rebind(us, "input.action.Fire", "", "key:NotAKey").ok, "틀린 소스는 거부");
    check(!in.rebind(us, "input.mod.MoveY", "", "invert").ok, "수정자는 재지정 대상이 아님");

    // 저장 → 다시 시작(새 설정 객체로 파일 글을 읽고, 바인딩을 처음부터 다시 만듦).
    const std::string saved = us.serialize();
    UserSettings us2;
    std::vector<std::string> msgs;
    us2.parse(saved, msgs);
    const std::vector<std::string> warn = startup(us2);
    check(warn.empty() && msgs.empty(), "저장한 글을 경고 없이 다시 읽음");
    check(in.bindingLine("input.action.Jump") == "key:J, pad:a" && in.bindingLine("input.action.Fire") == "key:F, key:J"
          && in.bindingLine("input.axis.MoveY") == "keys:up/down, pad:left.y"
          && in.bindingLine("input.context.Menu.action.Confirm") == "key:enter, pad:a", "다시 시작한 뒤 바꾼 바인딩이 그대로 적용됨");
    in.update(0.f, keysDown({ 265 }));   // GLFW Up
    check(in.axis("MoveY") == 1.f, "다시 시작한 뒤 바꾼 축 키가 동작");

    // 프로젝트 줄과 같아지면 덮어쓰기를 지움.
    r = in.rebind(us2, "input.action.Fire", "key:J", "");
    check(r.ok && r.line == "key:F" && !us2.has("input.action.Fire"), "프로젝트 줄로 돌아오면 덮어쓰기를 지움(바뀐 것만 저장)");
    check(in.resetBinding(us2, "input.action.Jump") && !us2.has("input.action.Jump")
          && in.bindingLine("input.action.Jump") == "key:space, pad:a", "기본값 되돌리기(동작 하나)");
    check(in.resetAllBindings(us2) == 2 && in.bindingLine("input.axis.MoveY") == "keys:W/S, pad:left.y"
          && in.bindingLine("input.context.Menu.action.Confirm") == "key:enter", "기본값 되돌리기(모두)");

    r = in.rebind(us2, "input.action.Fire", "key:F", "");
    check(r.ok && r.line == "none" && us2.getString("input.action.Fire") == "none" && in.bindingLine("input.action.Fire") == "none",
          "소스를 모두 빼면 none(비움)으로 저장");
    startup(us2);
    check(in.bindingLine("input.action.Fire") == "none", "다시 시작해도 비운 바인딩은 비어 있음");

    UserSettings bad;
    bad.set("input.action.Jump", "key:Nope");
    const std::vector<std::string> w2 = startup(bad);
    check(w2.size() == 1 && in.bindingLine("input.action.Jump") == "key:space, pad:a", "틀린 덮어쓰기는 알리고 프로젝트 줄 유지");
    in.setProjectLines({});
    in.clearBindings();
}

} // namespace

int main() {
    std::printf("Haruka VR 입력 검산\n");
    HInputSystem& in = HInputSystem::get();

    // 1. 아날로그 축
    std::printf("\n== 1. 아날로그 축 ==\n");
    {
        HXrInputState s;
        s.focused = true;
        s.hands[0].thumbstick = { 0.5f, -0.8f };
        s.hands[1].trigger    = 0.7f;
        s.hands[0].trigger    = 0.2f;
        in.setXrInput(s);
        in.bindAxis("MoveX", HXrAxis::LeftStickX);
        in.bindAxis("MoveY", HXrAxis::LeftStickY);
        in.bindAxis("Fire",  HXrAxis::RightTrigger);
        check(in.axis("MoveX") == 0.5f && in.axis("MoveY") == -0.8f, "왼쪽 스틱 x · y 가 축으로 나옴");
        check(in.axis("Fire") == 0.7f, "오른쪽 트리거가 축으로 나옴");
        check(in.axis("Unbound") == 0.f, "묶지 않은 축은 0");

        in.bindAxis("Throttle", HXrAxis::LeftTrigger);
        in.bindAxis("Throttle", HXrAxis::RightTrigger);
        check(in.axis("Throttle") == 0.7f, "여러 소스를 묶으면 절댓값이 가장 큰 값");

        s.focused = false;   // 헤드셋을 벗음 등
        in.setXrInput(s);
        check(in.axis("MoveX") == 0.f && in.axis("Fire") == 0.f, "XR 입력이 포커스를 잃으면 축은 0");
        in.unbindAxis("MoveX"); in.unbindAxis("MoveY"); in.unbindAxis("Fire"); in.unbindAxis("Throttle");
    }

    // 2. 컨트롤러 버튼 → 디지털 액션(눌림 · 뗌 엣지)
    std::printf("\n== 2. 컨트롤러 버튼 ==\n");
    {
        int pressed = 0, released = 0, held = 0;
        in.bindXrButton("Jump", HXrButton::RightPrimary);
        const auto h1 = in.subscribe("Jump", EInputEvent::Pressed,  [&](float) { ++pressed; });
        const auto h2 = in.subscribe("Jump", EInputEvent::Released, [&](float) { ++released; });
        const auto h3 = in.subscribe("Jump", EInputEvent::Held,     [&](float) { ++held; });

        HXrInputState s;
        s.focused = true;
        s.hands[1].primary = true;   // A 버튼
        in.setXrInput(s);
        in.update(0.016f, noKeys);
        in.update(0.016f, noKeys);
        check(pressed == 1 && held == 2, "누르면 Pressed 한 번, 누르고 있는 동안 Held");
        s.hands[1].primary = false;
        in.setXrInput(s);
        in.update(0.016f, noKeys);
        check(released == 1, "떼면 Released 한 번");

        // 텍스트 입력 중(키보드 캡처)에도 컨트롤러 버튼은 동작합니다.
        s.hands[1].primary = true;
        in.setXrInput(s);
        in.update(0.016f, noKeys, /*keyboardCaptured=*/true);
        check(pressed == 2, "키보드 캡처 중에도 컨트롤러 버튼은 눌림");

        // 같은 액션에 키와 버튼을 함께 묶을 수 있습니다.
        s.hands[1].primary = false;
        in.setXrInput(s);
        in.update(0.016f, noKeys);
        in.bind("Jump", 32);
        in.update(0.016f, [](int k) { return k == 32; });
        check(pressed == 3, "같은 액션의 키도 눌림");

        in.unsubscribe(h1); in.unsubscribe(h2); in.unsubscribe(h3);
        in.unbind("Jump");
        in.setXrInput({});
    }

    // 키 · 마우스 축, 액션 눌림 조회, 설정 줄 바인딩, 햅틱(2026-10-15)
    std::printf("\n== 키 · 마우스 축과 설정 줄 바인딩 ==\n");
    {
        in.clearBindings();
        in.setXrInput({});
        in.setLookDelta(0.f, 0.f);
        in.bindKeyAxis("MoveY", 'W', 'S');
        auto keys = [](std::vector<int> down) {
            return [down](int k) { for (int d : down) if (d == k) return true; return false; };
        };
        in.update(0.f, keys({ 'W' }));
        check(in.axis("MoveY") == 1.f, "W 는 +1");
        in.update(0.f, keys({ 'S' }));
        check(in.axis("MoveY") == -1.f, "S 는 -1");
        in.update(0.f, keys({ 'W', 'S' }));
        check(in.axis("MoveY") == 0.f, "둘 다면 0");
        in.update(0.f, keys({ 'W' }), /*keyboardCaptured=*/true);
        check(in.axis("MoveY") == 0.f, "텍스트 입력 중에는 0");

        in.bindMouseAxis("LookX", HMouseAxis::X, 0.5f);
        in.setLookDelta(10.f, -4.f);
        check(in.axis("LookX") == 5.f, "마우스 이동량 × 배율");
        in.setLookDelta(0.f, 0.f);
        check(in.axis("LookX") == 0.f, "이동이 없으면 0");

        std::string err;
        check(in.applyBinding("input.axis.Walk", "keys:W/S, xr:left.stick.y", err), "축 줄을 읽음");
        HXrInputState s;
        s.focused = true;
        s.hands[0].thumbstick = { 0.f, 0.5f };
        in.setXrInput(s);
        in.update(0.f, keys({ 'W' }));
        check(std::fabs(in.axis("Walk") - 1.5f) < 1e-6f, "XR 축과 키 축은 더함(이동이 길이 1 로 자름)");
        check(in.applyBinding("input.axis.Turn", "mouse:x*0.25", err), "마우스 배율");
        in.setLookDelta(8.f, 0.f);
        check(in.axis("Turn") == 2.f, "mouse:x*0.25");
        in.setLookDelta(0.f, 0.f);

        check(!in.applyBinding("input.axis.Walk", "keys:W", err) && !err.empty(), "슬래시 없는 키 쌍은 거부");
        in.update(0.f, keys({ 'W' }));
        check(std::fabs(in.axis("Walk") - 1.5f) < 1e-6f, "틀린 줄은 있던 바인딩을 바꾸지 않음");
        check(!in.applyBinding("input.action.Jump", "key:NotAKey", err), "모르는 키 이름은 거부");
        check(!in.applyBinding("input.button.Jump", "key:Space", err), "action · axis 가 아니면 거부");
        check(!in.applyBinding("input.axis.Turn", "mouse:x*abc", err), "깨진 배율은 거부");

        check(in.applyBinding("input.action.Grab", "xr:left.grip, key:F5", err), "액션 줄(XR 그립 · F 키)");
        s.hands[0].squeeze = 0.7f;
        in.setXrInput(s);
        in.update(0.f, noKeys);
        check(in.held("Grab"), "그립을 절반 넘게 쥐면 눌림");
        s.hands[0].squeeze = 0.3f;
        in.setXrInput(s);
        in.update(0.f, noKeys);
        check(!in.held("Grab"), "놓으면 뗌");
        in.update(0.f, keys({ 294 }));   // GLFW F5
        check(in.held("Grab"), "F5 키로도 눌림");

        int ok = 0;
        for (const auto& [k, v] : HInputSystem::defaultBindings()) ok += in.applyBinding(k, v, err) ? 1 : 0;
        check(ok == (int)HInputSystem::defaultBindings().size(), "엔진 기본 바인딩은 모두 읽힘");

        int   hHand = -1;
        float hAmp = 0.f, hSec = 0.f;
        in.setHapticSink([&](int hand, float amp, float sec) { hHand = hand; hAmp = amp; hSec = sec; });
        in.haptic(1, 0.6f, 0.04f);
        check(hHand == 1 && hAmp == 0.6f && hSec == 0.04f, "햅틱은 정해 둔 곳으로 보냄");
        in.setHapticSink({});
        in.haptic(0, 1.f, 1.f);
        check(hHand == 1, "받는 곳이 없으면 아무 일도 없음");
        in.clearBindings();
        in.setXrInput({});
    }

    testGamepad(in);
    testModifiers(in);
    testContexts(in);
    testRebind(in);

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
