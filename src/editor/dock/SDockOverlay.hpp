#pragma once
#include "../EditorTheme.hpp"
#include "../../ui/slate/HWidget.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

// ── SDockOverlay ──────────────────────────────────────────────────────────────
// 드래그 재도킹 중 **드롭 존**을 강조하는 최상위 오버레이입니다. 레이아웃과 무관하게
// 절대 rect(프레임버퍼 픽셀)를 그립니다. EditorApp 이 드래그 동안 active/rect 를 갱신하고,
// root SOverlay 의 맨 위 슬롯으로 얹어 다른 패널 위에 그려지게 합니다. 입력은 통과시킵니다.
class SDockOverlay : public HWidget {
public:
    bool      active = false;
    HGeometry rect;                                   // 프레임버퍼 픽셀
    glm::vec4 fill   = EditorTheme::dockFill;
    glm::vec4 border = EditorTheme::dockBorder;

    glm::vec2 computeDesiredSize() override { return { 0.f, 0.f }; }

    // 입력을 통과시키므로 히트 테스트에서도 빠집니다(H1a-1). 끌기 중 아래 패널이 그대로 걸립니다.
    bool hitTestVisible() const override { return false; }

    void paint(HPaintContext& ctx, const HGeometry& /*geo*/) override {
        if (!active || rect.size.x < 1.f || rect.size.y < 1.f) return;
        ctx.drawRect(rect, fill);
        ctx.drawBorder(rect, border, 2.f);
    }
};
