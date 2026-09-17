#pragma once
#include "../core/HLog.hpp"

// 렌더러 계층(Renderer·GpuScene·스왑체인)이 함께 쓰는 로그 카테고리.
// Vulkan 검증 레이어 메시지는 따로 끌 수 있도록 Renderer.cpp 의 Vulkan 카테고리로 분리합니다.
inline constinit HLogCategory LogRenderer{ "Renderer", ELogVerbosity::Log };
