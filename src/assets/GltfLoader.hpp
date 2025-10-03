#pragma once
#include "MeshSource.hpp"
#include <vector>
#include <string>

class GltfLoader {
public:
    // Load all mesh primitives from a .glb or .gltf file.
    // Node transforms are baked into vertex positions/normals.
    // Tangents are generated per-triangle when the TANGENT attribute is absent.
    // Returns one MeshSourceData per mesh-primitive/material combination.
    static std::vector<MeshSourceData> loadAll(const std::string& path);

    // Convenience: returns loadAll()[0], or defaultCube() on any failure.
    static MeshSourceData load(const std::string& path);

    // Built-in 24-vertex cube with checkerboard texture — used as fallback.
    static MeshSourceData defaultCube();
};
