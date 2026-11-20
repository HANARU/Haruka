#include "AssetManager.hpp"
#include "AssetLog.hpp"
#include "../core/Profiler.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../geometry/PrimitiveLog.hpp"
#include "GltfLoader.hpp"
#include "GassetCache.hpp"
#include "AssetStreamer.hpp"
#include "AssetReload.hpp"
#include "Skin.hpp"
#include "../renderer/Renderer.hpp"
#include "../renderer/Vertex.hpp"
#include "../geometry/PrimitiveBuilder.hpp"
#include "../geometry/CollisionMesh.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

// 충돌용 CPU 형상(P2): 위치와 인덱스만 남깁니다.
static std::shared_ptr<const CollisionMesh> makeCollisionMesh(const std::vector<MeshSourceVertex>& v,
                                                              const std::vector<uint32_t>& indices) {
    auto cm = std::make_shared<CollisionMesh>();
    cm->positions.reserve(v.size());
    for (const auto& sv : v) cm->positions.emplace_back(sv.pos[0], sv.pos[1], sv.pos[2]);
    cm->indices = indices;
    return cm;
}

AssetManager::AssetManager(Renderer* r) : renderer(r) {}

AssetManager::~AssetManager() {
    streamer_.reset();   // 워커를 먼저 거둡니다(읽는 중 작업은 끝까지)
    clear();
}

std::vector<AssetPair> AssetManager::loadAll(const std::string& path) {
    const std::string key = hvfs::toCanonical(path);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    // 원본은 정식 논리 경로(/Game/...)로 가상 파일 시스템에서 열고, 캐시 표 열쇠도 정식 경로로 통일합니다.
    // 백그라운드에서 같은 경로를 읽는 중이면 그 결과를 기다려 씁니다(두 번 읽지 않음, 비동기 로딩 D2).
    std::vector<MeshSourceData> meshes;
    std::vector<std::string>    deps;
    AssetStreamer::Result r;
    bool ok = false;
    std::string err;
    const ImportSettings settings = importSettingsFor(key);
    if (streamer_ && streamer_->waitFor(key, r)) {
        ok = r.ok;
        meshes = std::move(r.meshes);
        deps   = std::move(r.deps);
        err = r.error;
    } else {
        ok = AssetStreamer::decode(key, settings, meshes, err, &deps);
    }
    if (!ok) {
        HLOG(LogAsset, Error) << "Load failed (" << path << "): " << err << " -> using defaultCube";
        meshes = { GltfLoader::defaultCube() };
    }
    noteModel(key, settings.hash(), std::move(deps));
    return uploadModel(key, path, meshes);
}

bool AssetManager::requestLoad(const std::string& path) {
    const std::string key = hvfs::toCanonical(path);
    if (cache.count(key)) return true;
    if (streamWorkers_ <= 0) { loadAll(key); return true; }
    if (!streamer_) streamer_ = std::make_unique<AssetStreamer>(streamWorkers_);
    streamer_->request(key, importSettingsFor(key));
    return false;
}

bool AssetManager::isReady(const std::string& path) const {
    return cache.count(hvfs::toCanonical(path)) != 0;
}

size_t AssetManager::pendingLoads() const {
    return streamer_ ? streamer_->pending() : 0;
}

float AssetManager::pump(float budgetMs) {
    if (!streamer_) return 0.f;
    HPROF_SCOPE("Asset.Pump");
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    float used = 0.f;
    while (used < budgetMs) {
        auto results = streamer_->takeFinished(1);
        if (results.empty()) break;
        AssetStreamer::Result& r = results.front();
        if (!cache.count(r.path)) {
            if (!r.ok) {
                HLOG(LogAsset, Error) << "Load failed (" << r.path << "): " << r.error << " -> using defaultCube";
                r.meshes = { GltfLoader::defaultCube() };
            }
            // 설정 해시는 요청할 때의 표로 정했습니다. 그 사이 표가 바뀌었으면 다음 reloadForSettingsChange 가 맞춥니다.
            noteModel(r.path, importSettingsFor(r.path).hash(), std::move(r.deps));
            uploadModel(r.path, r.path, r.meshes);
        }
        used = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
    }
    return used;
}

size_t AssetManager::releaseUnused(const std::unordered_set<std::string>& keep) {
    size_t n = 0;
    for (auto it = cache.begin(); it != cache.end();) {
        const bool isPrim = it->first.rfind("@prim/", 0) == 0;
        if (isPrim || keep.count(it->first)) { ++it; continue; }
        freePairs(it->second, false);
        modelInfo_.erase(it->first);
        it = cache.erase(it);
        ++n;
    }
    return n;
}

size_t AssetManager::loadedModelCount() const {
    size_t n = 0;
    for (const auto& [k, v] : cache) n += k.rfind("@prim/", 0) == 0 ? 0 : 1;
    return n;
}

void AssetManager::freePairs(std::vector<AssetPair>& pairs, bool isPrim) {
    for (auto& p : pairs) {
        for (MeshHandle* lm : p.lods) renderer->freeMesh(lm);
        renderer->freeMesh(p.mesh);
        if (!isPrim) renderer->freeMaterial(p.material);
    }
    pairs.clear();
}

std::vector<AssetPair> AssetManager::uploadModel(const std::string& key, const std::string& path,
                                                 std::vector<MeshSourceData>& meshes) {
    std::vector<AssetPair> pairs;
    pairs.reserve(meshes.size());
    int   maskMeshes = 0;      // glTF alphaMode MASK 를 쓰는 메시 수(아래 경고용)
    float firstCutoff = 0.5f;

    using UpClock = std::chrono::steady_clock;
    const auto upStart = UpClock::now();
    float msMesh = 0.f, msLod = 0.f;
    auto since = [](UpClock::time_point t) { return std::chrono::duration<float, std::milli>(UpClock::now() - t).count(); };
    for (auto& data : meshes) {
        // 스킨 메시는 쉬는 자세로 한 번 스키닝해 올립니다(바인드 자세와 쉬는 자세가 다른 모델도 제자리에 보이게).
        // 바인드 정점과 스킨은 SkinnedMeshComponent 가 매 프레임 쓰도록 남깁니다(2026-11-07).
        std::vector<MeshSourceVertex> restPose;
        const std::vector<MeshSourceVertex>* src = &data.vertices;
        if (data.skin) {
            std::vector<glm::mat4> globals, palette;
            hskin::computeGlobals(*data.skin, nullptr, globals);
            hskin::computePalette(*data.skin, globals, palette);
            restPose.resize(data.vertices.size());
            glm::vec3 bmin, bmax;
            hskin::skinVertices(data.vertices.data(), data.vertices.size(), *data.skin, palette.data(),
                                restPose.data(), bmin, bmax);
            src = &restPose;
        }
        std::vector<Vertex> verts;
        verts.reserve(src->size());
        for (const auto& gv : *src) {
            Vertex v{};
            std::memcpy(v.pos,     gv.pos,     sizeof(v.pos));
            std::memcpy(v.normal,  gv.normal,  sizeof(v.normal));
            std::memcpy(v.uv,      gv.uv,      sizeof(v.uv));
            std::memcpy(v.tangent, gv.tangent, sizeof(v.tangent));
            verts.push_back(v);
        }

        AssetPair p;
        if (data.skin) {
            p.skin         = data.skin;
            p.bindVertices = std::make_shared<const std::vector<MeshSourceVertex>>(data.vertices);
        }
        auto tm = UpClock::now();
        p.mesh      = renderer->uploadMesh(verts, data.indices, nullptr, data.cacheOrdered);
        msMesh += since(tm);
        tm = UpClock::now();
        if (autoLod_ && !data.skin && p.mesh) {
            for (const auto& lod : data.lods) {
                if (MeshHandle* lm = renderer->uploadLodIndices(*p.mesh, lod.indices, data.cacheOrdered)) {
                    p.lods.push_back(lm);
                    p.lodScreenSizes.push_back(lod.screenSize);
                }
            }
        }
        msLod += since(tm);
        p.collision = makeCollisionMesh(*src, data.indices);
        p.material  = renderer->uploadMaterial(
            data.albedo, data.normalMap, data.metalRough, data.ao, data.emissive);
        p.name      = std::move(data.name);
        p.isBlend   = data.isBlend;
        p.doubleSided = data.doubleSided;
        p.alphaMask   = data.alphaMask;
        p.alphaCutoff = data.alphaCutoff;
        if (data.alphaMask && maskMeshes++ == 0) firstCutoff = data.alphaCutoff;
        p.vertCount = (int)data.vertices.size();
        p.triCount  = (int)(data.indices.size() / 3);
        pairs.push_back(std::move(p));
    }

    // MASK(컷아웃)는 아직 그리지 못합니다. 잎 · 철망을 들인 사용자가 왜 불투명 판으로 보이는지 알 수 있게 파일마다 한 번
    // 알립니다(이름은 위 루프에서 옮겨져 path 를 씀). 컷아웃 프리패스 · 그림자 소비자가 생기면 이 문구도 고칩니다.
    if (maskMeshes > 0) {
        char cut[32];
        std::snprintf(cut, sizeof(cut), "%.3g", firstCutoff);
        HLOG(LogAsset, Warning) << path << ": " << maskMeshes << " mesh(es) use glTF alphaMode MASK (first cutoff "
                                << cut << "); cutout is not supported yet, drawn opaque with an opaque shadow";
    }

    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%.2f ms (mesh %.2f, LOD %.2f)", since(upStart), msMesh, msLod);
        HLOG(LogAsset, Verbose) << "Uploaded " << key << " in " << buf;   // 측정용(비동기 로딩), 기본 로그에는 남기지 않음
    }
    cache[key] = pairs;
    return pairs;
}

void AssetManager::noteModel(const std::string& key, uint64_t settingsHash, std::vector<std::string> deps) {
    ModelInfo& mi   = modelInfo_[key];
    mi.settingsHash = settingsHash;
    mi.deps         = std::move(deps);
}

bool AssetManager::reloadModel(const std::string& path, const RebindFn& rebind, ReloadStats* stats) {
    const std::string key = hvfs::toCanonical(path);
    if (key.rfind("@prim/", 0) == 0) return false;
    auto it = cache.find(key);
    if (it == cache.end()) return false;
    HPROF_SCOPE("Asset.Reload");

    const ImportSettings settings = importSettingsFor(key);
    std::vector<MeshSourceData> meshes;
    std::vector<std::string>    deps;
    std::string err;
    if (!AssetStreamer::decode(key, settings, meshes, err, &deps)) {
        // 제작 도구가 쓰는 도중이거나 깨진 파일: 옛 모델을 그대로 둡니다(다음 저장에 다시 알림이 옴).
        HLOG(LogAsset, Error) << "Reload failed (" << key << "): " << err << " -> keeping the previous version";
        return false;
    }
    std::vector<AssetPair> oldPairs = std::move(it->second);
    cache.erase(it);
    noteModel(key, settings.hash(), std::move(deps));
    std::vector<AssetPair> fresh = uploadModel(key, key, meshes);
    if (rebind) rebind(key, fresh);

    const hreload::RetireList rl = hreload::retireList(oldPairs, fresh);
    for (MeshHandle* m : rl.meshes)        renderer->freeMesh(m);
    for (MaterialHandle* m : rl.materials) renderer->freeMaterial(m);
    if (stats) {
        ++stats->models;
        stats->meshesRetired    += rl.meshes.size();
        stats->materialsRetired += rl.materials.size();
    }
    return true;
}

std::vector<std::string> AssetManager::modelsUsing(const std::string& path) const {
    const std::string canon = hvfs::toCanonical(path);
    std::vector<std::string> out;
    for (const auto& [key, info] : modelInfo_) {
        if (!cache.count(key)) continue;
        if (key == canon || std::find(info.deps.begin(), info.deps.end(), canon) != info.deps.end()) out.push_back(key);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> AssetManager::reloadForSettingsChange(const RebindFn& rebind, ReloadStats* stats) {
    std::vector<std::string> keys;
    for (const auto& [key, info] : modelInfo_)
        if (cache.count(key) && importSettingsFor(key).hash() != info.settingsHash) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    std::vector<std::string> done;
    for (const auto& k : keys)
        if (reloadModel(k, rebind, stats)) done.push_back(k);
    return done;
}

std::vector<std::string> AssetManager::reloadStale(const RebindFn& rebind, ReloadStats* stats) {
    std::vector<std::string> keys;
    for (const auto& [key, info] : modelInfo_)
        if (cache.count(key) && !GassetCache::isValid(key, importSettingsFor(key).hash(), nullptr)) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    std::vector<std::string> done;
    for (const auto& k : keys)
        if (reloadModel(k, rebind, stats)) done.push_back(k);
    return done;
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
    p.collision = makeCollisionMesh(src.vertices, src.indices);
    p.material  = defaultPrimitiveMaterial();
    p.name      = key;
    p.isBlend   = false;
    p.doubleSided = false;   // Plane 의 그림자 양면 여부는 소비자(shadow_bias-4)가 정함
    p.alphaMask   = false;
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
        freePairs(pairs, key.rfind("@prim/", 0) == 0);
    }
    cache.clear();
    modelInfo_.clear();

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
