#pragma once
#include <string>
#include <vector>

class Actor;
class World;
class AssetManager;

// Saves and spawns Actor templates from .hprefab files.
// Format is identical to the single-actor block in .hscene files.
class PrefabManager {
public:
    static constexpr const char* kDir = "assets/prefabs";
    static constexpr const char* kExt = ".hprefab";

    // Serialize actor state to path (full path, must end in .hprefab).
    static void   save(const Actor* actor, const std::string& path);

    // Deserialize and spawn. Returns nullptr on failure.
    static Actor* spawn(const std::string& path, World& world, AssetManager& assets);

    // List .hprefab stem names found in kDir.
    static std::vector<std::string> scan();
};
