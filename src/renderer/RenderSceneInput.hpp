#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../vfx/ParticleSim.hpp"

struct MeshHandle;
struct MaterialHandle;
struct MeshSourceVertex;
class  HSlateContext;

// ── 렌더 입력 스냅샷 (엔진 구조 안정화 S5b, 2026-11-18) ────────────────────────────
// 추출 단계(components/RenderExtract)가 장면(World · Actor · 컴포넌트)에서 값을 뽑아
// 이 형에 담아 넘기며, 렌더러는 장면 모듈을 보지 않고 이 값만 소비합니다.

struct RenderCamera {
    bool      present    = false;   // 활성 카메라(CameraComponent::activeIn). 없으면 false
    glm::mat4 view{ 1.f };          // viewMatrix()
    glm::mat4 xrOrigin{ 1.f };      // xrOrigin()
    float     fovY       = 45.f;
    float     zNear      = 0.1f;
    float     zFar       = 100.f;
    float     exposureEv = 0.f;
};

enum class RenderLightKind : uint8_t { Directional, Point, Spot };

struct RenderLight {                // LightComponent::enabledIn 순서 그대로
    RenderLightKind kind = RenderLightKind::Directional;
    glm::vec3       position{ 0.f };        // owner ? worldMatrix()[3] : 0
    glm::vec3       emitDirection{ 0.f, 0.f, -1.f };   // emitDirection()
    glm::vec3       color{ 1.f };           // effectiveColor() * intensity
    float           radius      = 10.f;     // max(attenuationRadius, kMinAttenuationRadius)
    float           innerCone   = 15.f;     // innerConeAngle (도)
    float           outerCone   = 30.f;     // outerConeAngle (도)
    float           contactShadowLength = 0.f;
    uint32_t        channels    = 0x1u;     // lightChannels
    bool            castShadows = false;
};

struct RenderFog {                  // 켜져 있고 활성인 첫 HeightFogComponent. 없으면 present = false
    bool      present       = false;
    glm::vec3 color{ 0.f };
    float     maxOpacity    = 0.f;
    float     density       = 0.f;
    float     heightFalloff = 0.f;
    float     height        = 0.f;
    float     startDistance = 0.f;
};

struct RenderMesh {                        // StaticMeshComponent::visibleIn 순서 그대로
    uint64_t              key               = 0;        // LOD 상태 표의 키(컴포넌트 주소)
    uint64_t              actorId           = 0;        // 재질 없음 경고를 액터마다 한 번
    const char*           debugName         = "?";      // 액터 이름(이 프레임 동안 유효)
    const MeshHandle*     mesh              = nullptr;  // 가장 고운 메시
    MeshHandle* const*    lodMeshes         = nullptr;  // 컴포넌트 벡터 포인터(이 프레임 동안 유효)
    size_t                lodMeshCount      = 0;
    const float*          lodScreenSizes    = nullptr;
    size_t                lodScreenSizeCount = 0;
    bool                  autoLod           = true;
    const MaterialHandle* material          = nullptr;  // null 이면 렌더러가 오류 재질로
    glm::mat4             model{ 1.f };                 // owner->worldMatrix()
    bool                  transparent       = false;
    uint8_t               lightChannels     = 1;
};

struct IRenderSkinSource {                // 스킨 메시가 구현. 렌더러는 이것만 앎
    virtual bool skinInto(MeshSourceVertex* out, glm::vec3& bmin, glm::vec3& bmax) const = 0;
protected:
    ~IRenderSkinSource() = default;
};

struct RenderSkinnedMesh {                // componentsOf<SkinnedMeshComponent> 순서 그대로
    const IRenderSkinSource* source        = nullptr;
    const MeshHandle*        baseMesh      = nullptr;
    uint32_t                 vertCount     = 0;
    uint64_t                 actorId       = 0;
    const char*              debugName     = "?";
    const MaterialHandle*    material      = nullptr;
    glm::mat4                model{ 1.f };
    bool                     transparent   = false;
    uint8_t                  lightChannels = 1;
};

struct RenderReflectionCapture {          // 켜진 첫 ReflectionCaptureComponent. 없으면 present = false
    bool        present    = false;
    uint64_t    actorId    = 0;
    glm::vec3   pos{ 0.f };               // owner->worldPosition()
    float       radius     = 0.f;
    float       intensity  = 1.f;
    uint32_t    version    = 0;
    const void* worldToken = nullptr;     // &world (월드가 바뀌면 다시 찍기 위한 식별값)
};

struct IRenderPanelSource {               // UIPanelComponent 가 구현. 렌더러는 이것만 앎
    virtual HSlateContext& slate()     = 0;
    virtual bool           takeDirty() = 0;
    virtual void           markDirty() = 0;
protected:
    ~IRenderPanelSource() = default;
};

struct RenderUIPanel {                    // componentsOf<UIPanelComponent> 순서 그대로
    uint64_t            key         = 0;  // 패널 GPU 상태 표의 키(컴포넌트 주소)
    IRenderPanelSource* source      = nullptr;
    glm::mat4           model{ 1.f };     // owner->worldMatrix() * scale(width, height, 1)
    glm::uvec2          textureSize{ 0, 0 };
    bool                twoSided    = false;
    float               brightness  = 1.f;
};

// 파티클 이미터 하나(2026-11-20, Docs/Particles_And_Decals.md). 인스턴스는 월드 공간이고 컴포넌트가 가진 목록을 가리킵니다
// (이 프레임 동안 유효). 정렬은 렌더러가 두 눈 가운데를 안 뒤에 합니다(D9).
struct RenderParticleBatch {               // componentsOf<ParticleSystemComponent> 순서 그대로, 빈 이미터는 뺌
    const vfx::ParticleInstance* data   = nullptr;
    uint32_t                     count  = 0;
    vfx::Blend                   blend  = vfx::Blend::Additive;
    glm::vec3                    center{ 0.f };   // 이미터 월드 위치(이미터끼리 정렬)
};

// 상자 투영 데칼 하나(D11). 렌더러가 눈 가운데에서 가까운 것부터 HARUKA_MAX_DECALS 개를 FrameUBO 에 싣습니다.
struct RenderDecal {                       // componentsOf<DecalComponent> 순서 그대로
    glm::mat4 worldToBox{ 1.f };           // inverse(액터 월드 × scale(size))
    glm::vec3 axis{ 0.f, 1.f, 0.f };       // 상자 +Y 의 월드 방향(단위)
    glm::vec3 center{ 0.f };
    glm::vec3 color{ 1.f };                // 선형 알베도
    float     opacity   = 1.f;
    float     roughness = -1.f;            // 음수 = 거칠기 그대로
    int32_t   blend     = 0;               // 0 Replace · 1 Multiply
    int32_t   shape     = 0;               // 0 원 · 1 부드러운 원 · 2 고리 · 3 사각
};

struct RenderSceneInput {
    RenderCamera                   camera;
    std::vector<RenderLight>       lights;
    RenderFog                      fog;
    std::vector<RenderMesh>        meshes;
    std::vector<RenderSkinnedMesh> skinned;
    RenderReflectionCapture        capture;
    std::vector<RenderUIPanel>     panels;
    std::vector<RenderParticleBatch> particles;
    std::vector<RenderDecal>       decals;
};
