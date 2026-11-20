#pragma once
// 다시 읽은 모델을 컴포넌트에 바꿔 끼우기(2026-11-20, Docs/Asset_Hot_Reload.md D6). AssetManager::reloadModel 의 다시 묶기 함수로 씁니다.
// modelPath 가 같은(정식 경로로 비교) StaticMeshComponent · SkinnedMeshComponent 의 핸들을 새 AssetPair 로 바꿉니다. meshIndex 가 새 목록
// 밖이면 마지막 것으로 줄이고 경고합니다. transparent 는 사용자가 정한 값을 그대로 둡니다. 물리 몸의 충돌 형상은 다시 만들지 않습니다.

#include <string>
#include <vector>

class World;
struct AssetPair;

namespace hreload {

// 바꿔 끼운 컴포넌트 수를 돌려줍니다.
size_t rebindModel(World& world, const std::string& canonicalPath, const std::vector<AssetPair>& fresh);

} // namespace hreload
