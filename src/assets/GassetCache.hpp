#pragma once
#include "MeshSource.hpp"
#include <string>
#include <vector>

// Binary cache for pre-processed GLTF mesh data.
// Eliminates repeated GLTF parsing on startup.
// Format: magic(u32) version(u32) srcMtime(u64) meshCount(u32) [mesh entries...]
// Cache files live in assets/cache/ keyed by hash of source path.

namespace GassetCache {
    bool                      isValid(const std::string& srcPath);
    std::vector<MeshSourceData> load(const std::string& srcPath);
    void                      save(const std::string& srcPath,
                                   const std::vector<MeshSourceData>& meshes);
    std::string               cachePath(const std::string& srcPath);
}
