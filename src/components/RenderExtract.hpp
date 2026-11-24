#pragma once
#include "../renderer/RenderSceneInput.hpp"

class World;

// 한 프레임의 렌더 입력 스냅샷(카메라 · 조명 · 안개)을 월드에서 추출합니다(S5b, 2026-11-18).
// displayAlpha < 1 이면 시뮬레이션 틱 사이를 보간한 표시 자세로 추출합니다(D2b).
void extractRenderScene(const World& world, RenderSceneInput& out, float displayAlpha = 1.f);
