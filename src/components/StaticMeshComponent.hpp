#pragma once
#include "../core/Component.hpp"
#include <string>
#include <vector>

struct MeshHandle;
struct MaterialHandle;

#define SM_PROPS(p) \
    XPROP_Toggle(p, "Visible",      visible),    \
    XPROP_Toggle(p, "Transparent",  transparent)

class StaticMeshComponent : public Component {
public:
    MeshHandle*     mesh        = nullptr;
    MaterialHandle* material    = nullptr;
    bool            visible     = true;
    bool            transparent = false;
    std::string     modelPath;
    int             meshIndex   = 0;  // index into AssetManager::loadAll() result
    int             vertCount   = 0;
    int             triCount    = 0;

    // LOD levels: lodMeshes[0]=same as mesh (highest detail), [1]=next, etc.
    // Switch at lodDistances[i] squared (to avoid sqrt per frame).
    std::vector<MeshHandle*> lodMeshes;
    std::vector<float>       lodDistancesSq;

    // Returns the mesh appropriate for a given squared camera distance.
    MeshHandle* getMeshForDistanceSq(float distSq) const;

    bool localBounds(glm::vec3& outMin, glm::vec3& outMax) const override;

    const char*           typeName()     const override { return "StaticMeshComponent"; }
    std::vector<PropDesc> buildProps()         override;
    std::string           readOnlyInfo() const override;
};
