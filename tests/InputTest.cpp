// VR 입력(DEF-20) 검산입니다. XR 모듈이 넣는 입력 상태를 HInputSystem 이 아날로그 축과 디지털 액션으로
// 내주는지 확인합니다. OpenXR 도 창도 필요 없습니다(키 상태는 함수로 넣음).
//
// 이 테스트가 존재하는 이유: 예전 입력은 키보드 디지털뿐이라 VR 컨트롤러의 스틱 · 트리거 · 버튼을 받을 수
// 없었습니다. 헤드셋 없이도 바인딩 · 눌림 엣지 · 축 값 규칙을 확인할 수 있어야 합니다.
//
// 실행: build/Release/HarukaInputTest.exe

#include "../src/input/HInputSystem.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

bool noKeys(int) { return false; }

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

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
