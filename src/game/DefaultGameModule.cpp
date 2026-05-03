#include "DefaultGameModule.hpp"
#include "../core/World.hpp"
#include "../core/Actor.hpp"
#include "../components/LightComponent.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../assets/AssetManager.hpp"
#include "../geometry/PrimitiveDesc.hpp"
#include <iostream>

// ── 라이팅 확인 씬 ────────────────────────────────────────────────────────────
// Primitive_And_Lighting_Design.md 9절의 Lighting_Probe 배치입니다.
//
// 임시로 여기에 둡니다. 절차 도형 직렬화(PRIM: 키)가 붙으면 .hscene 파일로
// 옮깁니다. 씬 자체가 직렬화 왕복의 테스트 케이스가 되기 때문이며, onStart 에
// 계속 두면 게임 모듈이 특정 디버그 씬에 결합됩니다.
//
// 모든 액터가 원점에서 20 m 안에 있습니다. 섀도맵 ortho 범위 안이라는 뜻이며
// 그것이 의도한 배치입니다. FarSphere 만 일부러 밖에 둡니다.

namespace {

Actor* spawnPrimitive(EngineContext& ctx, const char* name,
                      const PrimitiveDesc& desc,
                      const glm::vec3& pos, const glm::vec3& scale,
                      MaterialHandle* material) {
    AssetPair pair = ctx.assets.acquirePrimitive(desc);
    if (!pair.mesh) {
        std::cerr << "[Scene] '" << name << "' 생성 실패. 액터를 만들지 않습니다.\n";
        return nullptr;
    }

    Actor* a = ctx.world.spawnActor(name);
    a->setPosition(pos);
    a->setScale(scale);

    auto* mc     = a->addComponent<StaticMeshComponent>();
    mc->mesh     = pair.mesh;
    mc->material = material ? material : pair.material;
    mc->vertCount = pair.vertCount;
    mc->triCount  = pair.triCount;
    return a;
}

} // namespace

void DefaultGameModule::onStart(EngineContext& ctx) {
    ctx.world.spawnActor("Sun")->addComponent<LightComponent>();

    MaterialHandle* checker = ctx.assets.primitiveCheckerMaterial();
    MaterialHandle* gray    = ctx.assets.primitiveMaterial(0xB4B4B4FFu);

    PrimitiveDesc box;    box.shape    = EPrimitiveShape::Box;
    PrimitiveDesc sphere; sphere.shape = EPrimitiveShape::Sphere;
    sphere.segU = 32; sphere.segV = 16;
    PrimitiveDesc plane;  plane.shape  = EPrimitiveShape::Plane;
    plane.segU = 1; plane.segV = 1;

    // 두께 있는 슬래브입니다. ShadowPass 가 앞면을 컬링하므로 한 겹짜리 평면은
    // 라이트를 마주할 때 섀도맵에 아무것도 기록하지 못합니다 (규칙 W4).
    // 상면이 정확히 y=0 에 오도록 두께의 절반만큼 내립니다.
    spawnPrimitive(ctx, "Ground", box, { 0.f, -0.1f, 0.f }, { 20.f, 0.2f, 20.f }, gray);

    // 곡면의 명암 그라데이션과 그림자를 봅니다.
    spawnPrimitive(ctx, "ProbeSphere", sphere, { 0.f, 1.f, 0.f }, glm::vec3(2.f), checker);

    // 하드 엣지 노멀 분할을 봅니다. 각 면이 균일하게 칠해지고 코너에
    // 그라데이션이 없어야 합니다.
    spawnPrimitive(ctx, "ProbeBox", box, { -3.f, 0.5f, 0.f }, glm::vec3(1.f), checker);

    // Plane 은 단면이라 섀도 캐스팅이 라이트 방향에 의존합니다. 정상입니다.
    spawnPrimitive(ctx, "ProbePlane", plane, { 3.f, 0.02f, 0.f }, { 2.f, 1.f, 2.f }, checker);

    // 섀도맵 UV 범위 밖입니다. 섀도 경로가 조기 반환하는 구간을 확인합니다.
    spawnPrimitive(ctx, "FarSphere", sphere, { 35.f, 1.f, 0.f }, glm::vec3(2.f), gray);

    // ── Tier 1 / Tier 2 ──────────────────────────────────────────────────────
    // Capsule 은 반구와 원통 경계가 C1 연속이라 정점을 공유합니다. 경계에 링이
    // 보이면 그것이 곧 규칙 N2 위반입니다 (검증 항목 V8).
    PrimitiveDesc capsule; capsule.shape = EPrimitiveShape::Capsule;
    capsule.segU = 32; capsule.segV = 8; capsule.ratio = 0.5f;
    spawnPrimitive(ctx, "ProbeCapsule", capsule, { 3.f, 1.f, -2.5f }, glm::vec3(2.f), checker);

    PrimitiveDesc cylinder; cylinder.shape = EPrimitiveShape::Cylinder; cylinder.segU = 32;
    spawnPrimitive(ctx, "ProbeCylinder", cylinder, { -3.f, 0.75f, -2.5f }, { 1.2f, 1.5f, 1.2f }, checker);

    PrimitiveDesc cone; cone.shape = EPrimitiveShape::Cone; cone.segU = 32;
    spawnPrimitive(ctx, "ProbeCone", cone, { 0.f, 0.75f, -2.5f }, { 1.2f, 1.5f, 1.2f }, checker);

    PrimitiveDesc torus; torus.shape = EPrimitiveShape::Torus;
    torus.segU = 32; torus.segV = 16; torus.ratio = 0.4f;
    spawnPrimitive(ctx, "ProbeTorus", torus, { -6.f, 1.f, 0.f }, glm::vec3(2.f), checker);

    // UV 파라미터화가 없어 체커를 붙일 수 없습니다. 노멀맵도 금지입니다.
    // 균일한 삼각형 분포가 필요할 때만 쓰는 도형입니다.
    PrimitiveDesc ico; ico.shape = EPrimitiveShape::Icosphere; ico.segU = 2;
    spawnPrimitive(ctx, "ProbeIcosphere", ico, { 6.f, 1.f, 0.f }, glm::vec3(2.f), gray);

    // Arrow 는 유일한 예외 카테고리입니다 (규칙 P2). 원점이 시작점이고 전방이 -Z 이며
    // 길이가 1 m 입니다. 축 세 개를 만들려면 회전만 달리해 세 번 스폰하면 됩니다.
    PrimitiveDesc arrow; arrow.shape = EPrimitiveShape::Arrow;
    arrow.segU = 16; arrow.ratio = 0.3f;
    spawnPrimitive(ctx, "ProbeArrow", arrow, { 0.f, 2.5f, 0.f }, glm::vec3(2.f),
                   ctx.assets.primitiveMaterial(0xE04030FFu));

    try {
        AssetPair pair = ctx.assets.load("assets/models/model.glb");
        Actor*    key  = ctx.world.spawnActor("Key");
        key->setPosition({ 0.f, 0.6f, 3.f });
        auto*     mc   = key->addComponent<StaticMeshComponent>();
        mc->mesh       = pair.mesh;
        mc->material   = pair.material;
        mc->modelPath  = "assets/models/model.glb";
        mc->vertCount  = pair.vertCount;
        mc->triCount   = pair.triCount;
    } catch (const std::exception& ex) {
        std::cerr << "[DefaultGameModule] Mesh load failed: " << ex.what() << "\n";
    }
}
