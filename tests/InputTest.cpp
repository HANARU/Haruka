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

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
