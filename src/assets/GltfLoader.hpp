#pragma once
#include "../geometry/MeshSource.hpp"
#include "ImportSettings.hpp"
#include <vector>
#include <string>

class GltfLoader {
public:
    // Load all mesh primitives from a .glb or .gltf file.
    // Node transforms are baked into vertex positions/normals.
    // Tangents are generated per-triangle when the TANGENT attribute is absent.
    // Returns one MeshSourceData per mesh-primitive/material combination.
    static std::vector<MeshSourceData> loadAll(const std::string& path);

    // 가져오기 설정(배율 · 자동 LOD · sRGB)을 적용해 읽습니다(2026-11-20, Docs/Asset_Hot_Reload.md D8). dependencies 가 있으면
    // 읽은 바깥 파일(.gltf 옆의 .bin · 이미지, 정식 논리 경로)을 채웁니다(.gasset 캐시 판정 · 핫리로드 D4).
    static std::vector<MeshSourceData> loadAll(const std::string& path, const ImportSettings& settings,
                                               std::vector<std::string>* dependencies);

    // Convenience: returns loadAll()[0], or defaultCube() on any failure.
    static MeshSourceData load(const std::string& path);

    // Built-in 24-vertex cube with checkerboard texture — used as fallback.
    static MeshSourceData defaultCube();
};
