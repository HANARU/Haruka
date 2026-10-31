#pragma once
#include <string>
#include <vector>

class Actor;
class World;
class AssetManager;

// Saves and spawns actor templates from .hprefab files.
// v3 (2026-10-15): the actor and its whole subtree, as .hscene actor blocks (first block = root).
// v1 / v2 (one actor, NAME: line) are still read.
class PrefabManager {
public:
    static constexpr const char* kDir = "assets/prefabs";
    static constexpr const char* kExt = ".hprefab";

    // Serialize the actor and its subtree to path (logical path, must end in .hprefab).
    static void   save(const Actor* root, const std::string& path);

    // Deserialize and spawn. Returns the root (children are attached), nullptr on failure.
    static Actor* spawn(const std::string& path, World& world, AssetManager& assets);

    // List .hprefab stem names found in kDir.
    static std::vector<std::string> scan();
};
