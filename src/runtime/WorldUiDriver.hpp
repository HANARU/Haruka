#pragma once

// ── 월드 UI 패널의 프레임 처리 (2026-11-18 Engine 에서 떼어 냄, 엔진 구조 안정화 S2) ─────────────────────────
// 머리를 따라오는 패널(3D UI 구간 7)을 옮기고, 마우스 · XR 두 손으로 패널을 가리키고 누릅니다(구간 5 · 6). 로직은 예전
// Engine::updateFollowPanels · updateWorldUiPointers 그대로입니다.

#include <memory>

class IEngineFrontend;
class PhysicsSystem;
class Renderer;
class UIPanelPointer;
class World;

class WorldUiDriver {
public:
    WorldUiDriver();
    ~WorldUiDriver();

    // 머리를 따라오는 패널(placement = FollowHead)을 옮깁니다. Play 중 월드 틱 뒤에 부릅니다. 머리 자세는 XR 세션이 입력을
    // 받는 동안 헤드셋(XR 원점 × 머리), 아니면 카메라 액터입니다(청취자와 같은 기준).
    void updateFollowPanels(World& world, bool xrSessionLive, float dt);

    // 패널을 마우스 · XR 두 손으로 가리키고 누릅니다. 꺼진 쪽은 붙잡은 패널을 놓습니다. 앞단에 "마우스가 패널 위" 를 알려 같은
    // 클릭으로 액터를 고르지 않게 하고, XR 손의 레이저 · 커서를 렌더러에 넣습니다.
    struct PointerInput {
        bool  mouseActive = false, xrActive = false;
        float mx = 0.f, my = 0.f;
        bool  lmb = false;
        int   winW = 0, winH = 0;
        float dt = 0.f;
    };
    void updatePointers(IEngineFrontend& front, World& world, PhysicsSystem* physics, Renderer& renderer,
                        const PointerInput& in);

private:
    // 마우스 포인터(구간 5). 패널 주소를 붙잡지만 매 프레임 목록으로 확인하므로 패널보다 오래 살아도 됩니다.
    std::unique_ptr<UIPanelPointer> mouse_;
    bool                            mouseLive_ = false;
    std::unique_ptr<UIPanelPointer> hands_[2];             // 0 = 왼손, 1 = 오른손
    bool                            handLive_[2] = { false, false };
    bool                            linesShown_ = false;   // 지난 프레임에 레이저를 넣었음(지울 때 빈 목록 한 번)
};
