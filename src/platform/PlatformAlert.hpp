#pragma once

// 플랫폼별 치명적 오류 알림(E1a, 2026-11-23).
// Engine.cpp 가 <windows.h> 를 직접 include 하지 않도록 플랫폼 층으로 뗍니다.
namespace hplat {

void showFatalError(const char* title, const char* message);

} // namespace hplat
