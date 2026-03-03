#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../geometry/PrimitiveDesc.hpp"

struct MeshHandle;
struct MaterialHandle;
class  Renderer;

struct AssetPair {
    MeshHandle*     mesh      = nullptr;
    MaterialHandle* material  = nullptr;
    std::string     name;
    bool            isBlend   = false;
    int             vertCount = 0;
    int             triCount  = 0;
};

class AssetManager {
public:
    explicit AssetManager(Renderer* r) : renderer(r) {}
    ~AssetManager() { clear(); }

    // Load all mesh primitives from a glTF/glb file (cached by path).
    // Falls back to a single defaultCube entry on failure.
    std::vector<AssetPair> loadAll(const std::string& path);

    // Convenience: returns loadAll()[0], or an empty AssetPair on failure.
    AssetPair load(const std::string& path);

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

    void clear();

private:
    Renderer* renderer;
    std::unordered_map<std::string, std::vector<AssetPair>> cache;
    std::unordered_map<std::string, MaterialHandle*>        primMaterials;
};
