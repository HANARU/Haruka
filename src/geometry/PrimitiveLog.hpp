#pragma once
#include "../core/HLog.hpp"

// 절차 도형의 규약 위반·미구현 알림. 생성기(geometry)와 AssetManager 가 함께 씁니다.
inline constinit HLogCategory LogPrimitive{ "Primitive", ELogVerbosity::Log };
