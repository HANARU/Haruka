#pragma once
#include "../IWindow.hpp"
#include "../WindowPlacement.hpp"
#include <memory>
#include <string>
#include <vector>

class GlfwWindow;

// GLFW 라이브러리 수명과 이벤트 펌프(다중 OS 창 H0, 2026-11-21, Docs/Editor_MultiWindow_Design.md).
//
// 이 클래스가 있는 이유: 예전에는 창 객체가 glfwInit · glfwTerminate 를 직접 불러, 창을 하나 더 만들었다가 닫으면 GLFW 가
// 통째로 끝났습니다. 또 창마다 pollEvents 가 자기 걸쇠만 비우고 전역 glfwPollEvents() 를 불러, 창이 둘이면 한 창의 폴링이
// 다른 창의 이벤트를 받아 가거나 걸쇠를 이벤트가 들어온 뒤에 비웠습니다(DEF-82 재발). GLFW 상태는 라이브러리 전체에
// 하나이므로 그 수명과 펌프도 창 밖 한곳에서 맡습니다.
//
// 상태(참조 수 · 살아 있는 창 목록)는 GlfwPlatform.cpp 안에만 있고 바깥에 접근자를 열지 않습니다. 시스템이 아니라 GLFW 의
// 전역 상태를 그대로 비춘 것이라 EngineContext 에 넣지 않습니다. 메인 스레드에서만 부릅니다(GLFW 규칙과 같음).
class GlfwPlatform {
public:
    // 참조 수를 하나 늘리고, 처음이면 glfwInit 을 부릅니다. 초기화에 실패하면(창 시스템 없음) false 이고 참조 수는 그대로입니다.
    static bool acquire();
    // 참조 수를 하나 줄이고, 마지막이면 glfwTerminate 를 부릅니다. acquire 가 성공한 횟수만큼만 부릅니다.
    static void release();

    // 등록된 모든 창의 걸쇠(keyLatch_ · buttonLatch_ · keyPresses_)를 먼저 비우고, 그 다음 glfwPollEvents()(wait 이면
    // glfwWaitEvents())를 딱 한 번 부릅니다. 이 순서여야 한 프레임 안에 눌렀다 뗀 입력이 그 프레임의 걸쇠에 남습니다(DEF-82).
    // 프레임마다 한 번만 부릅니다. GlfwWindow::pollEvents · waitEvents 가 이것을 부르므로, 엔진 루프가 메인 창의 pollEvents 를
    // 부르면 보조 창의 이벤트도 함께 받습니다. 보조 창의 pollEvents 를 따로 부르면 펌프가 두 번 돌아 앞 펌프의 걸쇠가 지워집니다.
    static void pumpEvents(bool wait);

    static bool isInitialized();   // glfwInit 이 성공했고 아직 끝나지 않았는지
    static int  windowCount();     // 지금 등록된(init 이 끝나고 cleanup 전인) GlfwWindow 수
    // 연결된 모니터마다 작업 영역(작업 표시줄을 뺀 화면 좌표 사각형, M3a). GLFW 가 초기화되지 않았거나 모니터가 없으면 빈 목록이고,
    // 그때 부른 쪽은 창 자리를 끼우지 않습니다(hwinplace::clampToWorkAreas 가 빈 목록이면 그대로 돌려줌).
    static std::vector<hwinplace::Rect> monitorWorkAreas();

private:
    friend class GlfwWindow;
    static void addWindow(GlfwWindow* w);
    static void removeWindow(GlfwWindow* w);
};

// 보조 창(3D 뷰포트 없이 UI 만 그리는 창)을 만듭니다. init 까지 마친 창을 돌려주고, 만들지 못하면(창 시스템 없음 등)
// 경고를 남기고 nullptr 입니다. hidden 이면 메인 창의 --hidden-window 와 같은 규칙(보이지 않음 · 마우스 창 밖 고정 · 키 떼어 둠)을
// 따릅니다. 보조 창은 카메라 모드와 게임패드를 쓰지 않습니다. 메인 창보다 먼저 없애야 하는 것은 아닙니다(참조 수가 맡음).
std::unique_ptr<IWindow> createAuxWindow(int width, int height, const std::string& title, bool hidden);
