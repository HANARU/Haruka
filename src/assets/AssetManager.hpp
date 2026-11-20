#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../geometry/PrimitiveDesc.hpp"

struct MeshHandle;
struct MaterialHandle;
struct MeshSourceVertex;
struct SkinSourceData;
struct CollisionMesh;
struct MeshSourceData;
class  Renderer;
class  AssetStreamer;

struct AssetPair {
    MeshHandle*     mesh      = nullptr;
    MaterialHandle* material  = nullptr;
    std::string     name;
    bool            isBlend   = false;
    bool            doubleSided = false;   // glTF material.doubleSided(소비자는 아직 없음)
    bool            alphaMask   = false;   // glTF alphaMode MASK(소비자는 아직 없음)
    float           alphaCutoff = 0.5f;    // glTF alphaCutoff(MASK 일 때만 의미)
    int             vertCount = 0;
    int             triCount  = 0;
    // 스킨 메시(2026-11-07)일 때만: 바인드 정점(CPU 에 남김)과 스킨. mesh 에는 쉬는 자세로 스키닝한 정점이 올라가 있어
    // 스킨 없이 그려도(StaticMeshComponent) 쉬는 자세로 보입니다. 매 프레임 자세는 SkinnedMeshComponent 가 씁니다.
    std::shared_ptr<const std::vector<MeshSourceVertex>> bindVertices;
    std::shared_ptr<const SkinSourceData>                skin;
    // 충돌용 CPU 형상(위치 · 인덱스, 로컬 공간, 2026-11-17 P2). 볼록 껍질 · 메시 충돌체가 씁니다. 스킨 메시는 쉬는 자세입니다.
    std::shared_ptr<const CollisionMesh>                 collision;
    // 자동 생성 LOD (O13, 2026-11-18). 거친 단계 순이며, --no-auto-lod 이면 비웁니다.
    std::vector<MeshHandle*>                             lods;
    std::vector<float>                                   lodScreenSizes;
};

class AssetManager {
public:
    explicit AssetManager(Renderer* r);
    ~AssetManager();

    void setAutoLod(bool on) { autoLod_ = on; }
    bool autoLod() const     { return autoLod_; }

    // Load all mesh primitives from a glTF/glb file (cached by path).
    // Falls back to a single defaultCube entry on failure.
    std::vector<AssetPair> loadAll(const std::string& path);

    // Convenience: returns loadAll()[0], or an empty AssetPair on failure.
    AssetPair load(const std::string& path);

    // ── 비동기 읽기 (2026-11-20, Docs/Async_Loading.md D1 · D2) ─────────────────────────────
    // 워커 스레드 수를 정합니다(처음 requestLoad 전에, 기본 2 · Android 1). 0 이면 비동기 요청도 동기로 읽습니다.
    void   setStreamWorkers(int n) { streamWorkers_ = n; }
    // 백그라운드 읽기를 요청합니다. 이미 올라가 있으면 true(바로 쓸 수 있음), 아니면 false(요청함 · 진행 중).
    bool   requestLoad(const std::string& path);
    bool   isReady(const std::string& path) const;
    size_t pendingLoads() const;   // 요청했지만 아직 올리지 않은 것(읽는 중 + 올리기 대기)
    // 메인 스레드: 끝난 결과를 budgetMs 안에서 올립니다(모델 단위라 하나는 넘을 수 있음). 쓴 시간(ms)을 돌려줍니다.
    float  pump(float budgetMs);
    // 이 경로들(정식 논리 경로)이 아닌 파일 모델을 해제합니다(절차 도형 · 그 머티리얼은 남김). 해제한 모델 수.
    // 해제한 핸들을 쓰는 컴포넌트가 없어야 합니다(씬을 바꾼 뒤). GPU 자원은 렌더러가 프레임이 끝난 뒤 지웁니다.
    size_t releaseUnused(const std::unordered_set<std::string>& keep);
    size_t loadedModelCount() const;

    // ── 절차 도형 ────────────────────────────────────────────────────────────
    // 같은 desc 는 같은 핸들입니다. 무조건 생성하는 API 는 공개하지 않습니다.
    // 메가버퍼가 해제 없는 범프 할당자이므로, 재생성을 부를 수 있는 문을
    // 아예 만들지 않는 것이 유일하게 확실한 방어입니다.
    //
    // 캐시는 파일 경로용 맵을 그대로 재사용하고 "@prim/" 접두사로 키 공간만
    // 분리합니다. clear() 가 맵 하나만 순회하면 되므로 정리 경로가 갈라지지
    // 않습니다. 미구현 셰이프이면 빈 AssetPair 를 반환합니다.
    AssetPair acquirePrimitive(const PrimitiveDesc& desc);

    // 세그먼트만 줄인 같은 도형입니다 (규칙 R4). 높은 LOD 부터 순서대로입니다.
    std::vector<MeshHandle*> acquirePrimitiveLods(const PrimitiveDesc& desc, int levels = 3);

    // 절차 도형의 기본 머티리얼입니다. 팩터를 명시적으로 채워서 만듭니다.
    // 비어 있는 값을 엔진이 뒤에서 채우는 경로는 만들지 않습니다 (머티리얼 정책).
    MaterialHandle* primitiveMaterial(uint32_t rgba8, float metallic = 0.f, float roughness = 0.6f);
    MaterialHandle* defaultPrimitiveMaterial() { return primitiveMaterial(0xCCCCCCFFu); }

    // 검증용 체커입니다. UV 왜곡과 스트레치를 즉시 드러냅니다.
    MaterialHandle* primitiveCheckerMaterial();

    // 머티리얼 정체성으로 머티리얼을 얻습니다. 위의 두 함수로 나뉘는 유일한 분기점이라,
    // 씬 파일에서 읽은 값과 코드에서 만든 값이 같은 핸들을 받습니다.
    MaterialHandle* acquirePrimitiveMaterial(const PrimitiveMaterialDesc& desc);

    void clear();

private:
    std::vector<AssetPair> uploadModel(const std::string& key, const std::string& path, std::vector<MeshSourceData>& meshes);
    void freePairs(std::vector<AssetPair>& pairs, bool isPrim);

    Renderer* renderer;
    int       streamWorkers_ = 2;
    std::unique_ptr<AssetStreamer> streamer_;
    bool      autoLod_ = true;
    std::unordered_map<std::string, std::vector<AssetPair>> cache;
    std::unordered_map<std::string, MaterialHandle*>        primMaterials;
};
