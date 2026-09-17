#pragma once
#include "../core/HLog.hpp"

// 에셋 계층(AssetManager·glTF 로더·gasset 캐시·프리팹)이 함께 쓰는 로그 카테고리.
// 캐시 적중과 머티리얼 덤프처럼 로드마다 반복되는 로그는 Verbose 로 남깁니다.
inline constinit HLogCategory LogAsset{ "Asset", ELogVerbosity::Log };
