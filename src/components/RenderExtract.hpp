#pragma once
#include "../renderer/RenderSceneInput.hpp"

class World;

// 한 프레임의 렌더 입력 스냅샷(카메라 · 조명 · 안개)을 월드에서 추출합니다(S5b, 2026-11-18).
void extractRenderScene(const World& world, RenderSceneInput& out);
