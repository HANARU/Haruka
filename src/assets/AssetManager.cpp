#include "AssetManager.hpp"
#include "AssetLog.hpp"
#include "../geometry/PrimitiveLog.hpp"
#include "GltfLoader.hpp"
#include "GassetCache.hpp"
#include "../renderer/Renderer.hpp"
#include "../renderer/Vertex.hpp"
#include "../geometry/PrimitiveBuilder.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

std::vector<AssetPair> AssetManager::loadAll(const std::string& path) {
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;

    std::vector<MeshSourceData> meshes;
    try {
        if (GassetCache::isValid(path)) {
            try {
                meshes = GassetCache::load(path);
            } catch (const std::exception& cacheEx) {
                HLOG(LogAsset, Warning) << "Cache corrupted (" << path << "): "
                                        << cacheEx.what() << " -> reloading from source";
                meshes = GltfLoader::loadAll(path);
                GassetCache::save(path, meshes);
            }
        } else {
            meshes = GltfLoader::loadAll(path);
            GassetCache::save(path, meshes);
        }
    } catch (const std::exception& e) {
        HLOG(LogAsset, Error) << "Load failed (" << path << "): "
                              << e.what() << " -> using defaultCube";
        meshes = { GltfLoader::defaultCube() };
    }

    std::vector<AssetPair> pairs;
    pairs.reserve(meshes.size());

    for (auto& data : meshes) {
        std::vector<Vertex> verts;
        verts.reserve(data.vertices.size());
        for (const auto& gv : data.vertices) {
            Vertex v{};
            std::memcpy(v.pos,     gv.pos,     sizeof(v.pos));
            std::memcpy(v.normal,  gv.normal,  sizeof(v.normal));
            std::memcpy(v.uv,      gv.uv,      sizeof(v.uv));
            std::memcpy(v.tangent, gv.tangent, sizeof(v.tangent));
            verts.push_back(v);
        }

        AssetPair p;
        p.mesh      = renderer->uploadMesh(verts, data.indices);
        p.material  = renderer->uploadMaterial(
            data.albedo, data.normalMap, data.metalRough, data.ao, data.emissive);
        p.name      = std::move(data.name);
        p.isBlend   = data.isBlend;
        p.vertCount = (int)data.vertices.size();
        p.triCount  = (int)(data.indices.size() / 3);
        pairs.push_back(std::move(p));
    }

    cache[path] = pairs;
    return pairs;
}

AssetPair AssetManager::load(const std::string& path) {
    auto all = loadAll(path);
    return all.empty() ? AssetPair{} : all[0];
}


// ── 절차 도형 ─────────────────────────────────────────────────────────────────

// rgba8 은 0xRRGGBBAA 이며 'sRGB 로 인코딩된 표시값'입니다. 즉 색 선택기에서
// 고르는 값 그대로입니다. albedo 텍스처가 VK_FORMAT_R8G8B8A8_SRGB 로 올라가므로
// 샘플링 시점에 하드웨어가 선형화합니다. 여기서 또 변환하면 이중 인코딩입니다.

AssetPair AssetManager::acquirePrimitive(const PrimitiveDesc& descIn) {
    PrimitiveDesc desc = descIn;
    desc.canonicalize();

    const std::string key = "@prim/" + desc.toKey();
    auto it = cache.find(key);
    if (it != cache.end() && !it->second.empty()) return it->second[0];

    // 업로드 전에 예산을 검사합니다. uploadMesh 의 throw 를 밟지 않는 것이
    // 목적이며, 그래서 생성과 업로드를 분리해 둔 것입니다.
    const uint32_t evc = PrimitiveBuilder::estimateVertexCount(desc);
    // 로그는 영문으로 남깁니다. Output Log 폰트가 ASCII 전용이라 한글은 물음표로 보입니다.
    if (evc == 0) {
        HLOG(LogAsset, Error) << "primitive '" << key << "' is not implemented; the actor gets no mesh";
        return AssetPair{};
    }

    MeshSourceData src = PrimitiveBuilder::build(desc);
    if (src.vertices.empty() || src.indices.empty()) {
        HLOG(LogAsset, Error) << "primitive '" << key << "' generated an empty mesh";
        return AssetPair{};
    }

#ifndef NDEBUG
    // 규칙 T3. 와인딩, 탄젠트 handedness, 바운드, 예측 개수를 대조합니다.
    // defaultCube 가 여섯 면 전부 handedness 를 틀린 채로 오래 남아 있었던
    // 이유가 이런 검사가 없었기 때문입니다 (DEF-39).
    std::string report;
    if (!PrimitiveBuilder::validate(desc, src, &report))
        HLOG(LogPrimitive, Error) << "'" << key << "' violates mesh conventions:\n" << report;
#endif

    std::vector<Vertex> verts;
    verts.reserve(src.vertices.size());
    for (const auto& sv : src.vertices) {
        Vertex v{};
        std::memcpy(v.pos,     sv.pos,     sizeof(v.pos));
        std::memcpy(v.normal,  sv.normal,  sizeof(v.normal));
        std::memcpy(v.uv,      sv.uv,      sizeof(v.uv));
        std::memcpy(v.tangent, sv.tangent, sizeof(v.tangent));
        verts.push_back(v);
    }

    const MeshBounds mb = PrimitiveBuilder::analyticBounds(desc);

    AssetPair p;
    p.mesh      = renderer->uploadMesh(verts, src.indices, &mb);
    p.material  = defaultPrimitiveMaterial();
    p.name      = key;
    p.isBlend   = false;
    p.vertCount = (int)verts.size();
    p.triCount  = (int)(src.indices.size() / 3);

    cache[key] = { p };
    return p;
}

std::vector<MeshHandle*> AssetManager::acquirePrimitiveLods(const PrimitiveDesc& desc, int levels) {
    std::vector<MeshHandle*> out;
    PrimitiveDesc prev;
    bool          hasPrev = false;
    for (int i = 0; i < std::max(levels, 1); ++i) {
        const PrimitiveDesc d = primitiveLodDesc(desc, i);
        // 이미 최소 세그먼트에 닿았으면 같은 메시를 또 넣지 않습니다.
        if (hasPrev && d == prev) break;
        AssetPair p = acquirePrimitive(d);
        if (!p.mesh) break;
        out.push_back(p.mesh);
        prev = d; hasPrev = true;
    }
    return out;
}

MaterialHandle* AssetManager::primitiveMaterial(uint32_t rgba8, float metallic, float roughness) {
    char keyBuf[64];
    std::snprintf(keyBuf, sizeof(keyBuf), "%08x|%.3f|%.3f", rgba8, (double)metallic, (double)roughness);
    const std::string key(keyBuf);

    auto it = primMaterials.find(key);
    if (it != primMaterials.end()) return it->second;

    // 팩터를 명시적으로 채웁니다. glTF 기본값(metallic=1, roughness=1)을 그대로
    // 두면 확산광이 0 이 되어 음영 없는 평평한 표면이 됩니다. 파일에서 온
    // 머티리얼은 규약대로 두지만(엔진이 값을 대체하지 않습니다), 엔진이
    // 직접 만드는 머티리얼은 처음부터 의도한 값으로 채웁니다.
    TexSourceData albedo;
    albedo.w = albedo.h = 1;
    albedo.hasData = true;
    albedo.data = { (uint8_t)((rgba8 >> 24) & 0xFF),
                    (uint8_t)((rgba8 >> 16) & 0xFF),
                    (uint8_t)((rgba8 >>  8) & 0xFF),
                    (uint8_t)( rgba8        & 0xFF) };

    TexSourceData metalRough;
    metalRough.w = metalRough.h = 1;
    metalRough.hasData = true;
    metalRough.data = { 0,
                        (uint8_t)std::lround(std::clamp(roughness, 0.f, 1.f) * 255.f),  // G
                        (uint8_t)std::lround(std::clamp(metallic,  0.f, 1.f) * 255.f),  // B
                        255 };

    TexSourceData none;   // normal / ao / emissive 는 uploadMaterial 의 기본값을 씁니다
    MaterialHandle* m = renderer->uploadMaterial(albedo, none, metalRough, none, none);
    primMaterials[key] = m;
    return m;
}

void AssetManager::clear() {
    for (auto& [key, pairs] : cache) {
        // 절차 도형의 머티리얼은 primMaterials 가 소유하며 여러 도형이 공유합니다.
        // 여기서 같이 풀면 이중 해제입니다.
        const bool isPrim = key.rfind("@prim/", 0) == 0;
        for (auto& p : pairs) {
            renderer->freeMesh(p.mesh);
            if (!isPrim) renderer->freeMaterial(p.material);
        }
    }
    cache.clear();

    for (auto& [key, m] : primMaterials) renderer->freeMaterial(m);
    primMaterials.clear();
}

MaterialHandle* AssetManager::acquirePrimitiveMaterial(const PrimitiveMaterialDesc& desc) {
    return desc.checker ? primitiveCheckerMaterial()
                        : primitiveMaterial(desc.rgba8, desc.metallic, desc.roughness);
}

MaterialHandle* AssetManager::primitiveCheckerMaterial() {
    const std::string key = "@checker";
    auto it = primMaterials.find(key);
    if (it != primMaterials.end()) return it->second;

    // 체커 한 칸의 종횡비가 도형 전체에서 일정한지가 UV 검증 기준입니다.
    constexpr int W = 256, H = 256, CELL = 32;
    TexSourceData albedo;
    albedo.w = W; albedo.h = H; albedo.hasData = true;
    albedo.data.resize((size_t)W * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint8_t c = (((x / CELL) + (y / CELL)) % 2 == 0) ? 230 : 60;
            uint8_t* p = albedo.data.data() + ((size_t)y * W + x) * 4;
            p[0] = c; p[1] = c; p[2] = c; p[3] = 255;
        }

    TexSourceData metalRough;
    metalRough.w = metalRough.h = 1;
    metalRough.hasData = true;
    metalRough.data = { 0, 153, 0, 255 };   // G=roughness 0.6, B=metallic 0

    TexSourceData none;
    MaterialHandle* m = renderer->uploadMaterial(albedo, none, metalRough, none, none);
    primMaterials[key] = m;
    return m;
}
