#pragma once
#include "../scene/Component.hpp"
#include "../geometry/PrimitiveDesc.hpp"
#include "../geometry/CollisionMesh.hpp"   // ICollisionMeshSource
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct MeshHandle;
struct MaterialHandle;
class AssetManager;
class World;

#define SM_PROPS(p) \
    XPROP_Toggle(p, "Visible",      visible),     \
    XPROP_Toggle(p, "Transparent",  transparent), \
    XPROP_Toggle(p, "Auto LOD",     autoLod)

class StaticMeshComponent : public Component, public ICollisionMeshSource {
public:
    MeshHandle*     mesh        = nullptr;
    // 충돌용 CPU 형상(P2): 메시를 배정할 때 AssetPair::collision 을 함께 넣습니다. RigidBody 의 ConvexHull · Mesh 셰이프가 씁니다.
    std::shared_ptr<const CollisionMesh> collision;
    MaterialHandle* material    = nullptr;
    bool            visible     = true;
    bool            transparent = false;
    uint8_t         lightChannels = 1;   // 이 메시를 비출 라이트 채널(LightChannels.hpp, 2026-11-17)
    std::string     modelPath;
    int             meshIndex   = 0;  // index into AssetManager::loadAll() result

    // 절차 도형이면 그 정체성이고, 파일 메시면 비어 있습니다. modelPath 와 배타입니다.
    // 메시·머티리얼 핸들만으로는 무엇을 만들었는지 알 수 없어서, 씬에 저장할 때 이 두 값을
    // 씁니다. 비워 두면 Play→Stop 이나 저장·불러오기에서 도형이 사라집니다.
    std::optional<PrimitiveDesc> primitive;
    PrimitiveMaterialDesc        primitiveMaterial;   // primitive 가 있을 때만 의미가 있습니다
    int             vertCount   = 0;
    int             triCount    = 0;

    // LOD(수학 보강 O6, 2026-11-17): lodMeshes[i] 는 mesh 다음으로 거친 단계들이고, lodScreenSizes[i] 는 단계 i(0 = mesh)에서
    // lodMeshes[i] 로 넘어가는 화면 크기(구의 투영 지름 ÷ 화면 높이, 내림차순)입니다. 경계에는 이력 띠가 있습니다
    // (renderer/ScreenSize.hpp). 예전 거리² 문턱은 시야각 · 해상도와 무관했습니다.
    std::vector<MeshHandle*> lodMeshes;
    std::vector<float>       lodScreenSizes;
    bool                     autoLod = true;   // 꺼져 있으면 늘 가장 고운 메시를 씀(O13)

    // AssetPair 의 메시 · 충돌 · LOD 를 한 경로로 채웁니다(StaticMeshComponent · SceneSerializer 공용).
    void assignMeshFromPair(const struct AssetPair& pair);

    bool localBounds(glm::vec3& outMin, glm::vec3& outMax) const override;
    const CollisionMesh* collisionMesh() const override { return collision.get(); }

    const char*           typeName()     const override { return "StaticMeshComponent"; }
    std::vector<PropDesc> buildProps()         override;
    std::string           readOnlyInfo() const override;

    // 직렬화 (DEF-22). 메시는 절차 도형(prim · primMaterial)과 파일 메시(model · meshIndex) 중 하나로만
    // 씁니다. transparent 도 씁니다(DEF-101: 예전에는 저장되지 않아 불러올 때 에셋 값으로 돌아감).
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    void onFieldsLoaded(const ComponentLoadContext& ctx) override;   // 메시 · 머티리얼을 불러옴

    // 머티리얼 편집(2026-11-17): 절차 도형이면 디테일 패널에 Checker · R · G · B · Metallic · Roughness 칸이 나오고, 값을
    // 확정하면(onPropsCommitted) 그 값의 머티리얼을 다시 받습니다(AssetManager 가 값마다 하나를 캐시하므로 같은 값은 공유).
    // 파일 메시의 머티리얼은 텍스처만 있어(인자 없음) 아직 편집하지 않습니다.
    void bindAssets(AssetManager* a) { assets_ = a; }

    // 그릴 메시(예전 World::getVisibleMeshes, 2026-11-18 S3): 활성 · 켜짐 · visible · 메시 있음, 액터 순서. 머티리얼은 거르지
    // 않습니다. 없으면 렌더러가 오류 머티리얼로 그립니다(DEF-28).
    static std::vector<StaticMeshComponent*> visibleIn(const World& world);
    void onPropsCommitted() override;

private:
    AssetManager* assets_ = nullptr;   // 머티리얼을 다시 받을 곳(불러올 때 정해짐)
    bool transparentLoaded_ = false;   // 파일에 transparent 가 있었는지(없으면 에셋 값을 씀)
};
