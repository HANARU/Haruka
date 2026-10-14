#pragma once
#include "../core/Component.hpp"
#include "../geometry/PrimitiveDesc.hpp"
#include <optional>
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

    // 절차 도형이면 그 정체성이고, 파일 메시면 비어 있습니다. modelPath 와 배타입니다.
    // 메시·머티리얼 핸들만으로는 무엇을 만들었는지 알 수 없어서, 씬에 저장할 때 이 두 값을
    // 씁니다. 비워 두면 Play→Stop 이나 저장·불러오기에서 도형이 사라집니다.
    std::optional<PrimitiveDesc> primitive;
    PrimitiveMaterialDesc        primitiveMaterial;   // primitive 가 있을 때만 의미가 있습니다
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

    // 직렬화 (DEF-22). 메시는 절차 도형(prim · primMaterial)과 파일 메시(model · meshIndex) 중 하나로만
    // 씁니다. transparent 도 씁니다(DEF-101: 예전에는 저장되지 않아 불러올 때 에셋 값으로 돌아감).
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    void onFieldsLoaded(const ComponentLoadContext& ctx) override;   // 메시 · 머티리얼을 불러옴

private:
    bool transparentLoaded_ = false;   // 파일에 transparent 가 있었는지(없으면 에셋 값을 씀)
};
