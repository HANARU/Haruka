#pragma once
#include <string>
#include <vector>

class Actor;
class World;
class AssetManager;

// Saves and spawns actor templates from .hprefab files.
// v3 (2026-10-15): the actor and its whole subtree, as .hscene actor blocks (first block = root).
// v6 (2026-11-21): nested prefab instances are written as instance blocks + overrides (Docs/Linked_Prefabs.md).
// v1 / v2 (one actor, NAME: line) are still read.
class PrefabManager {
public:
    static constexpr const char* kDir = "/Game/prefabs";
    static constexpr const char* kExt = ".hprefab";

    // Serialize the actor and its subtree to path (logical path, must end in .hprefab). Refuses (false) when an instance
    // inside the subtree depends on path (cycle). If the root itself is an instance of path, it is written unpacked.
    // The written text is returned in *written.
    static bool save(const Actor* root, const std::string& path, std::string* written = nullptr);

    // Spawns a linked instance (v3 and later) or a plain copy (v1 / v2). Returns the root, nullptr on failure.
    static Actor* spawn(const std::string& path, World& world, AssetManager& assets);

    // Rebuilds every instance in the world that depends on path (nested included) from newText, keeping overrides,
    // actor ids and added actors. Updates the world's PrefabSourceCache. Returns the number of instances rebuilt.
    static int refreshInstances(World& world, AssetManager& assets, const std::string& path, const std::string& newText);

    // .hprefab names under kDir: path relative to kDir without the extension (subfolders included, UTF-8).
    static std::vector<std::string> scan();
};
