// 에셋 핫리로드 · 가져오기 설정(Docs/Asset_Hot_Reload.md) 검산. 창 · GPU 가 필요 없습니다.
//
// 이 시험이 존재하는 이유: 핫리로드는 "파일이 바뀜 → 알림 → 모으기 → 다시 가져오기 → 컴포넌트가 새 핸들을 봄 → 옛 자원 해제" 의
// 사슬이고, 고리 하나가 끊기면 화면에는 옛 모델이 그대로 남아 눈치채기 어렵습니다. 특히 .gltf 옆의 텍스처만 바꾸면 캐시가 그대로
// 쓰이던 결함(DEF-174)과 가져오기 설정이 캐시 키에 들어가지 않는 실수는 결과 화면만으로는 드러나지 않습니다.
//
// 실행: build/Release/HarukaHotReloadTest.exe

#include "../src/assets/AssetChangeQueue.hpp"
#include "../src/assets/AssetManager.hpp"
#include "../src/assets/AssetReload.hpp"
#include "../src/assets/AssetStreamer.hpp"
#include "../src/assets/GassetCache.hpp"
#include "../src/assets/GltfLoader.hpp"
#include "../src/assets/ImportSettings.hpp"
#include "../src/assets/Skin.hpp"
#include "../src/components/AssetRebind.hpp"
#include "../src/components/SkinnedMeshComponent.hpp"
#include "../src/components/StaticMeshComponent.hpp"
#include "../src/editor/AssetWatcher.hpp"
#include "../src/geometry/CollisionMesh.hpp"
#include "../src/renderer/MeshHandle.hpp"
#include "../src/runtime/EngineOptions.hpp"
#include "../src/scene/Actor.hpp"
#include "../src/scene/World.hpp"
#include "../src/vfs/Vfs.hpp"

#include "stb_image_write.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed; else ++g_failed;
}

using Bytes = std::vector<uint8_t>;

void writeFile(const fs::path& p, const Bytes& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), (std::streamsize)b.size());
}
void writeText(const fs::path& p, const std::string& s) { writeFile(p, Bytes(s.begin(), s.end())); }

// n x n 회색(값 v) RGBA PNG.
Bytes greyPng(int n, uint8_t v) {
    std::vector<uint8_t> px((size_t)n * n * 4, v);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    Bytes out;
    stbi_write_png_to_func([](void* ctx, void* data, int size) {
        auto* o = static_cast<Bytes*>(ctx);
        o->insert(o->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    }, &out, n, n, 4, px.data(), n * 4);
    return out;
}

// 격자 판(n x n 칸, 삼각형 2n², LOD 를 만들 만큼 큼): 위치 · UV · u32 인덱스를 한 버퍼에.
struct Grid { Bytes bin; size_t posBytes = 0, uvBytes = 0, idxBytes = 0; uint32_t verts = 0, indices = 0; };
Grid makeGrid(int n) {
    Grid g;
    std::vector<float> pos, uv;
    for (int y = 0; y <= n; ++y)
        for (int x = 0; x <= n; ++x) {
            pos.insert(pos.end(), { float(x) / n, float(y) / n, 0.05f * std::sin(float(x * y)) });
            uv.insert(uv.end(), { float(x) / n, float(y) / n });
        }
    std::vector<uint32_t> idx;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const uint32_t a = uint32_t(y * (n + 1) + x), b = a + 1, c = a + uint32_t(n + 1), d = c + 1;
            idx.insert(idx.end(), { a, b, d, a, d, c });
        }
    g.verts = uint32_t(pos.size() / 3);
    g.indices = uint32_t(idx.size());
    g.posBytes = pos.size() * 4; g.uvBytes = uv.size() * 4; g.idxBytes = idx.size() * 4;
    g.bin.resize(g.posBytes + g.uvBytes + g.idxBytes);
    std::memcpy(g.bin.data(), pos.data(), g.posBytes);
    std::memcpy(g.bin.data() + g.posBytes, uv.data(), g.uvBytes);
    std::memcpy(g.bin.data() + g.posBytes + g.uvBytes, idx.data(), g.idxBytes);
    return g;
}

std::string gridGltf(const Grid& g, const std::string& binUri, const std::string& imgUri) {
    const auto num = [](size_t v) { return std::to_string(v); };
    return std::string("{\"asset\":{\"version\":\"2.0\"},") +
        "\"buffers\":[{\"uri\":\"" + binUri + "\",\"byteLength\":" + num(g.bin.size()) + "}]," +
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" + num(g.posBytes) + "}," +
        "{\"buffer\":0,\"byteOffset\":" + num(g.posBytes) + ",\"byteLength\":" + num(g.uvBytes) + "}," +
        "{\"buffer\":0,\"byteOffset\":" + num(g.posBytes + g.uvBytes) + ",\"byteLength\":" + num(g.idxBytes) + "}]," +
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":" + num(g.verts) +
        ",\"type\":\"VEC3\",\"min\":[0,0,-0.05],\"max\":[1,1,0.05]}," +
        "{\"bufferView\":1,\"componentType\":5126,\"count\":" + num(g.verts) + ",\"type\":\"VEC2\"}," +
        "{\"bufferView\":2,\"componentType\":5125,\"count\":" + num(g.indices) + ",\"type\":\"SCALAR\"}]," +
        "\"images\":[{\"uri\":\"" + imgUri + "\"}],\"textures\":[{\"source\":0}]," +
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},\"metallicFactor\":0}}]," +
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":2,\"material\":0}]}]," +
        "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
}

float maxAbsPos(const std::vector<MeshSourceData>& m) {
    float r = 0.f;
    for (const auto& d : m)
        for (const auto& v : d.vertices) r = std::max(r, std::max(std::fabs(v.pos[0]), std::fabs(v.pos[1])));
    return r;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// ── 1. 가져오기 설정 표 ──────────────────────────────────────────────────────────
void testSettingsTable() {
    std::printf("\n== 1. 가져오기 설정 표(import.ini) ==\n");
    using himport::globMatch;
    check(globMatch("/Game/models/*.glb", "/Game/models/a.glb"), "'*' 는 한 폴더 안의 이름");
    check(!globMatch("/Game/models/*.glb", "/Game/models/sub/a.glb"), "'*' 는 '/' 를 넘지 않음");
    check(globMatch("/Game/models/**", "/Game/models/sub/deep/a.gltf"), "'**' 는 '/' 를 넘음");
    check(globMatch("/game/MODELS/a?.GLB", "/Game/models/ab.glb"), "대소문자 무시 · '?' 한 글자");
    check(!globMatch("/Game/models/a?.glb", "/Game/models/a.glb"), "'?' 는 꼭 한 글자");
    check(globMatch("/Game/**/props/*.glb", "/Game/levels/a/props/box.glb") && !globMatch("/Game/**/props/*.glb", "/Game/props.glb"),
          "가운데 '**'");

    const std::string text =
        "# 주석\n"
        "[/Game/models/**]\n"
        "scale = 0.01   # 센티미터 원본\n"
        "srgb = false\n"
        "\n"
        "[/Game/models/hero/*.glb]\n"
        "scale = 2\n"
        "autoLod = off\n"
        "bogus = 1\n"
        "scale = -3\n"
        "noequals\n"
        "[]\n"
        "orphan = 1\n";
    std::vector<std::string> msgs;
    const ImportSettingsTable t = ImportSettingsTable::parse(text, msgs);
    check(t.rules().size() == 2, "절 두 개를 읽음(빈 [] 는 버림)");
    check(msgs.size() == 5, "잘못된 줄 5개(모르는 키 · 범위 밖 배율 · '=' 없음 · 빈 절 · 절 밖 키)를 알림, 받은 수 " +
                                std::to_string(msgs.size()));
    check(!msgs.empty() && msgs[0].rfind("line 9:", 0) == 0, "알림에 줄 번호(첫 알림 = 9번 줄의 bogus)");

    const ImportSettings def = t.resolve("/Game/scenes/a.hscene");
    check(def == ImportSettings{}, "맞는 절이 없으면 기본값");
    const ImportSettings prop = t.resolve("assets/models/props/crate.glb");
    check(prop.scale == 0.01f && !prop.srgb && prop.autoLod, "첫 절만 맞음(논리 경로 assets/ 도 정식 경로로 맞춤)");
    const ImportSettings hero = t.resolve("/Game/models/hero/knight.glb");
    check(hero.scale == 2.f && !hero.srgb && !hero.autoLod, "두 절이 맞으면 겹쳐 적용하고 뒤 절이 이김(srgb 는 앞 절 값 유지)");

    ImportSettings a, b;
    check(a.hash() == b.hash() && a.hash() != 0, "같은 설정은 같은 해시");
    b.scale = 2.f;
    ImportSettings c; c.autoLod = false;
    ImportSettings d; d.srgb = false;
    check(a.hash() != b.hash() && a.hash() != c.hash() && a.hash() != d.hash() && c.hash() != d.hash(),
          "배율 · 자동 LOD · sRGB 가 하나라도 다르면 다른 해시(캐시 키)");
}

// ── 2. 모으기 · 분류 ──────────────────────────────────────────────────────────────
void testQueue() {
    std::printf("\n== 2. 바뀐 파일 모으기(디바운스) · 분류 ==\n");
    hreload::AssetChangeQueue q(300);
    q.notify("/Game/models/a.glb", 0);
    q.notify("/Game/models/b.glb", 50);
    q.notify("/Game/models/a.glb", 100);   // 같은 파일을 또 씀 → 조용한 시간이 다시 시작
    check(q.pending() == 2, "같은 경로의 알림은 하나로 합침");
    check(q.takeReady(299).empty(), "300 ms 가 지나기 전에는 내보내지 않음");
    auto r = q.takeReady(350);
    check(r.size() == 1 && r[0] == "/Game/models/b.glb", "b 만 조용해짐(a 는 100 ms 에 다시 알림)");
    r = q.takeReady(400);
    check(r.size() == 1 && r[0] == "/Game/models/a.glb" && q.pending() == 0, "a 는 마지막 알림 뒤 300 ms 에 한 번");
    check(q.takeReady(10000).empty(), "내보낸 경로는 다시 나오지 않음");
    bool all = false;
    q.notifyOverflow(1000);
    q.takeReady(1100, &all);
    check(!all, "넘침도 조용해질 때까지 기다림");
    q.takeReady(1300, &all);
    check(all && q.pending() == 0, "넘침 → 한 번 '모두 다시 확인'");

    using hreload::ChangeKind;
    check(hreload::classify("/Game/models/a.GLB") == ChangeKind::Model && hreload::classify("/Game/m/b.gltf") == ChangeKind::Model,
          "glb · gltf = 모델");
    check(hreload::classify("/Game/scenes/x.hscene") == ChangeKind::Scene, "hscene = 씬");
    check(hreload::classify("/Config/import.ini") == ChangeKind::ImportSettings &&
              hreload::classify("/Config/project.ini") == ChangeKind::Other,
          "/Config/import.ini 만 가져오기 설정");
    check(hreload::classify("/Game/textures/a.png") == ChangeKind::Other, "이미지는 Other(올라간 모델의 의존 파일인지 따로 물음)");
    check(hreload::logicalFromProjectRelative("Content/models/a.glb") == "/Game/models/a.glb" &&
              hreload::logicalFromProjectRelative("Config/import.ini") == "/Config/import.ini" &&
              hreload::logicalFromProjectRelative("Saved/Logs/x.log").empty() &&
              hreload::logicalFromProjectRelative("Config/project.ini").empty() &&
              hreload::logicalFromProjectRelative("Content/").empty(),
          "프로젝트 상대 경로 → 정식 논리 경로(Content · Config/import.ini 만)");
}

// ── 3 · 4. 의존 파일 · 캐시 키 · 설정 적용 ─────────────────────────────────────────
void testCacheAndImport(const fs::path& tmp) {
    std::printf("\n== 3. 의존 파일 기록 · 캐시 판정(DEF-174) ==\n");
    const fs::path content = tmp / "project/Content";
    const Grid g = makeGrid(24);   // 삼각형 1,152 개
    writeFile(content / "models/grid.bin", g.bin);
    writeFile(content / "textures/grey.png", greyPng(2, 128));
    writeText(content / "models/grid.gltf", gridGltf(g, "grid.bin", "../textures/grey.png"));
    hvfs::reset();
    hvfs::mountDirectoryAs(content, "/Game/", 10);
    GassetCache::setDirectory((tmp / "cache").string());
    const std::string model = "/Game/models/grid.gltf";

    std::vector<std::string> deps;
    std::vector<MeshSourceData> m;
    try { m = GltfLoader::loadAll(model, ImportSettings{}, &deps); } catch (const std::exception& e) { check(false, e.what()); return; }
    check(m.size() == 1 && m[0].albedo.w == 2, "격자 · 텍스처를 읽음");
    check(deps.size() == 2 && contains(deps, "/Game/models/grid.bin") && contains(deps, "/Game/textures/grey.png"),
          "바깥 파일 두 개(.bin · 이미지)를 정식 논리 경로로 기록, 받은 수 " + std::to_string(deps.size()));
    check(m.size() == 1 && m[0].albedo.w == 2,
          "/Game/ 정식 경로로 연 .gltf 가 '../' 바깥 파일을 찾음(DEF-175: 예전에는 맨 앞 '/' 를 버려 못 찾음)");
    // 마운트 이름 위로 올라가 다른 마운트(/Config/)로 넘어가는 uri 는 거부합니다.
    writeFile(tmp / "project/Config/x.png", greyPng(2, 50));
    hvfs::mountDirectoryAs(tmp / "project/Config", "/Config/", 10);
    writeText(content / "models/escape.gltf", gridGltf(g, "grid.bin", "../../Config/x.png"));
    try {
        std::vector<std::string> escDeps;
        const auto esc = GltfLoader::loadAll("/Game/models/escape.gltf", ImportSettings{}, &escDeps);
        check(esc.size() == 1 && esc[0].albedo.w == 1 && !contains(escDeps, "/Config/x.png"),
              "'/Game/../Config/...' 로 넘어가는 uri 는 읽지 않음(모델은 열고 텍스처는 팩터 1x1)");
    } catch (const std::exception& e) { check(false, std::string("escape.gltf: ") + e.what()); }

    const uint64_t hDef = ImportSettings{}.hash();
    ImportSettings big; big.scale = 2.f;
    GassetCache::save(model, m, hDef, deps);
    std::vector<std::string> back;
    check(GassetCache::isValid(model, hDef, &back) && back == deps, "저장한 캐시가 유효하고 의존 목록을 돌려줌");
    check(!GassetCache::isValid(model, big.hash(), nullptr), "가져오기 설정 해시가 다르면 무효(설정이 캐시 키에 들어감)");
    check(GassetCache::isValid(model), "기본 설정 판정(인자 하나)도 같은 결과");

    // 텍스처만 바꿈(.gltf 는 그대로). 예전에는 원본만 봐서 캐시를 그대로 썼습니다.
    writeFile(content / "textures/grey.png", greyPng(4, 200));
    check(!GassetCache::isValid(model, hDef, nullptr), "의존 이미지가 바뀌면 무효(DEF-174)");
    std::vector<MeshSourceData> m2;
    std::string err;
    std::vector<std::string> deps2;
    check(AssetStreamer::decode(model, ImportSettings{}, m2, err, &deps2) && m2.size() == 1 && m2[0].albedo.w == 4,
          "다시 읽으면 새 이미지(4x4)를 가져옴");
    check(GassetCache::isValid(model, hDef, nullptr) && deps2.size() == 2, "다시 가져온 뒤 캐시가 다시 유효");
    // 의존 파일의 시각만 바뀜(내용 같음): 내용 해시로 그대로 씀.
    fs::last_write_time(content / "textures/grey.png", fs::file_time_type::clock::now() + std::chrono::seconds(5));
    check(GassetCache::isValid(model, hDef, nullptr), "의존 파일의 시각만 바뀌면 내용 해시로 판정해 그대로 씀");

    std::printf("\n== 4. 가져오기 설정 적용(배율 · 자동 LOD · sRGB) ==\n");
    ImportSettings noLod; noLod.autoLod = false;
    ImportSettings lin;   lin.srgb = false;
    std::vector<MeshSourceData> a, b, c, d;
    try {
        a = GltfLoader::loadAll(model, ImportSettings{}, nullptr);
        b = GltfLoader::loadAll(model, big, nullptr);
        c = GltfLoader::loadAll(model, noLod, nullptr);
        d = GltfLoader::loadAll(model, lin, nullptr);
    } catch (const std::exception& e) { check(false, e.what()); return; }
    check(std::fabs(maxAbsPos(a) - 1.f) < 1e-4f && std::fabs(maxAbsPos(b) - 2.f) < 1e-4f, "scale = 2 면 정점이 두 배");
    check(!a.empty() && !a[0].lods.empty(), "기본은 자동 LOD 를 만듦(삼각형 1,152 개)");
    check(!c.empty() && c[0].lods.empty() && c[0].indices.size() == a[0].indices.size(), "autoLod = false 면 LOD 없음(본 메시는 그대로)");
    // 200/255 = 0.784(선형) → sRGB 0.898 → 229. 기본(sRGB 로 저장됨)이면 그대로 200.
    check(!a.empty() && a[0].albedo.data.size() >= 4 && a[0].albedo.data[0] == 200, "srgb = true(기본): 이미지 바이트 그대로");
    check(!d.empty() && d[0].albedo.data.size() >= 4 && d[0].albedo.data[0] == 229 && d[0].albedo.data[3] == 255,
          "srgb = false: 선형 값을 sRGB 로 인코딩(200 → 229, 알파 그대로), 받은 값 " +
              std::to_string(d.empty() || d[0].albedo.data.empty() ? -1 : d[0].albedo.data[0]));
    hvfs::reset();
}

// ── 5. 다시 묶기 ──────────────────────────────────────────────────────────────────
void testRebind() {
    std::printf("\n== 5. 다시 묶기(컴포넌트가 새 핸들을 봄) ==\n");
    MeshHandle oldMesh, newMesh0, newMesh1, newLod;
    newMesh1.aabbMin = glm::vec3(-1.f); newMesh1.aabbMax = glm::vec3(1.f);
    auto* oldMat = reinterpret_cast<MaterialHandle*>(0x1000);
    auto* newMat0 = reinterpret_cast<MaterialHandle*>(0x2000);
    auto* newMat1 = reinterpret_cast<MaterialHandle*>(0x3000);
    auto coll = std::make_shared<CollisionMesh>();

    World world;
    Actor* a1 = world.spawnActor("A1");
    auto* s1 = a1->addComponent<StaticMeshComponent>();
    s1->modelPath = "assets/models/m.glb";   // 옛 형식 경로도 정식 경로로 비교
    s1->meshIndex = 1; s1->mesh = &oldMesh; s1->material = oldMat; s1->transparent = true;
    Actor* a2 = world.spawnActor("A2");
    auto* s2 = a2->addComponent<StaticMeshComponent>();
    s2->modelPath = "/Game/models/m.glb"; s2->meshIndex = 7; s2->mesh = &oldMesh;
    Actor* a3 = world.spawnActor("A3");
    auto* s3 = a3->addComponent<StaticMeshComponent>();
    s3->modelPath = "/Game/models/other.glb"; s3->mesh = &oldMesh;
    Actor* a4 = world.spawnActor("A4");
    auto* sk = a4->addComponent<SkinnedMeshComponent>();
    sk->modelPath = "/Game/models/m.glb"; sk->meshIndex = 0; sk->baseMesh = &oldMesh;

    std::vector<AssetPair> fresh(2);
    fresh[0].mesh = &newMesh0; fresh[0].material = newMat0; fresh[0].triCount = 10;
    auto skin = std::make_shared<SkinSourceData>();
    auto bind = std::make_shared<const std::vector<MeshSourceVertex>>(std::vector<MeshSourceVertex>(5));
    fresh[0].skin = skin; fresh[0].bindVertices = bind;
    fresh[1].mesh = &newMesh1; fresh[1].material = newMat1; fresh[1].lods = { &newLod }; fresh[1].lodScreenSizes = { 0.3f };
    fresh[1].collision = coll; fresh[1].vertCount = 4; fresh[1].triCount = 2;

    const size_t n = hreload::rebindModel(world, "/Game/models/m.glb", fresh);
    check(n == 3, "같은 모델을 쓰는 컴포넌트 3개(정적 2 · 스킨 1)를 바꿈, 다른 모델은 그대로, 받은 수 " + std::to_string(n));
    check(s1->mesh == &newMesh1 && s1->material == newMat1 && s1->lodMeshes.size() == 1 && s1->lodMeshes[0] == &newLod &&
              s1->collision == coll && s1->vertCount == 4,
          "정적 메시: meshIndex 1 의 메시 · 머티리얼 · LOD · 충돌 · 정점 수");
    check(s1->transparent, "사용자가 정한 transparent 는 그대로");
    check(s2->meshIndex == 1 && s2->mesh == &newMesh1, "meshIndex 7 은 새 목록 끝(1)으로 줄임");
    check(s3->mesh == &oldMesh, "다른 모델의 컴포넌트는 그대로");
    check(sk->baseMesh == &newMesh0 && sk->material == newMat0 && sk->skin() == skin.get() && sk->vertCount == 5,
          "스킨 메시: 기본 메시 · 머티리얼 · 스킨 · 바인드 정점");
    check(hreload::rebindModel(world, "/Game/models/m.glb", {}) == 0 && s1->mesh == &newMesh1, "빈 목록이면 바꾸지 않음");
}

// ── 6. 옛 자원 은퇴 목록 ───────────────────────────────────────────────────────────
void testRetire() {
    std::printf("\n== 6. 다시 읽은 모델의 옛 자원(은퇴 목록) ==\n");
    MeshHandle m0, m1, l0, l1, shared, n0;
    auto* mat0 = reinterpret_cast<MaterialHandle*>(0x10);
    auto* mat1 = reinterpret_cast<MaterialHandle*>(0x20);
    auto* nmat = reinterpret_cast<MaterialHandle*>(0x30);
    std::vector<AssetPair> oldPairs(2);
    oldPairs[0].mesh = &m0; oldPairs[0].material = mat0; oldPairs[0].lods = { &l0, &l1 };
    oldPairs[1].mesh = &m1; oldPairs[1].material = mat1; oldPairs[1].lods = { &l1 };   // 같은 LOD 핸들이 두 번(중복 해제 금지)
    std::vector<AssetPair> fresh(1);
    fresh[0].mesh = &n0; fresh[0].material = nmat;
    hreload::RetireList r = hreload::retireList(oldPairs, fresh);
    check(r.meshes.size() == 4 && r.materials.size() == 2, "메시 2 + LOD 2(중복 하나 뺌) · 머티리얼 2, 받은 수 " +
                                                               std::to_string(r.meshes.size()) + " · " +
                                                               std::to_string(r.materials.size()));
    // 새 목록이 옛 핸들을 다시 쓰면(같은 머티리얼 등) 그것은 은퇴시키지 않음.
    fresh[0].material = mat1;
    fresh[0].lods = { &shared, &l0 };
    r = hreload::retireList(oldPairs, fresh);
    check(r.meshes.size() == 3 && r.materials.size() == 1 && std::find(r.meshes.begin(), r.meshes.end(), &l0) == r.meshes.end(),
          "새 목록에도 있는 핸들은 빼고 셈(LOD l0 · 머티리얼 mat1)");
    oldPairs[0].mesh = nullptr;
    r = hreload::retireList(oldPairs, {});
    check(std::find(r.meshes.begin(), r.meshes.end(), nullptr) == r.meshes.end() && r.meshes.size() == 3, "null 핸들은 넣지 않음");
}

// ── 7. 감시자(임시 폴더) ─────────────────────────────────────────────────────────
void testWatcher(const fs::path& tmp) {
    std::printf("\n== 7. 감시자(임시 프로젝트 폴더 → 정식 논리 경로) ==\n");
#ifdef _WIN32
    const fs::path root = tmp / "watched";
    fs::create_directories(root / "Content/models");
    fs::create_directories(root / "Config");
    fs::create_directories(root / "Saved");
    AssetWatcher w;
    check(w.start(root), "감시 시작");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    writeText(root / "Content/models/a.glb", "x");
    writeText(root / "Saved/log.txt", "x");
    writeText(root / "Config/import.ini", "# x\n");
    writeText(root / "Config/project.ini", "# x\n");
    hreload::AssetChangeQueue q(300);
    std::vector<std::string> ready;
    const auto t0 = std::chrono::steady_clock::now();
    int64_t now = 0;
    while (ready.size() < 2 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
        now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        for (const auto& n : w.drain()) q.notify(n, now);
        for (auto& r : q.takeReady(now)) ready.push_back(std::move(r));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::sort(ready.begin(), ready.end());
    check(ready.size() == 2 && ready[0] == "/Config/import.ini" && ready[1] == "/Game/models/a.glb",
          "Content 의 모델 · Config/import.ini 만 나옴(Saved · project.ini 는 버림), 받은 수 " + std::to_string(ready.size()));
    w.stop();
    check(!w.running(), "멈춤");
#else
    check(true, "Windows 밖에서는 감시자가 없음");
#endif
}

void testOptions() {
    std::printf("\n== 8. 명령줄 ==\n");
    std::vector<std::string> unknown;
    const EngineOptions o = hopt::parse({ "--touch-asset-at=5:/Game/models/model.glb", "--touch-asset-at=9:/Game/scenes/a.hscene",
                                          "--auto-reload-scene", "--touch-asset-at=x:/Game/a.glb" }, unknown);
    check(o.touchAssetAt.size() == 2 && o.touchAssetAt[0].frame == 5 && o.touchAssetAt[0].command == "/Game/models/model.glb" &&
              o.touchAssetAt[1].frame == 9,
          "--touch-asset-at=<프레임>:<경로> 여러 번");
    check(o.autoReloadScene, "--auto-reload-scene");
    check(unknown.size() == 1, "프레임이 숫자가 아니면 거부");
}

} // namespace

int main() {
    std::printf("Haruka 에셋 핫리로드 · 가져오기 설정 검산\n");
    const fs::path tmp = fs::temp_directory_path() / "haruka_hot_reload_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    testSettingsTable();
    testQueue();
    testCacheAndImport(tmp);
    testRebind();
    testRetire();
    testWatcher(tmp);
    testOptions();

    fs::remove_all(tmp, ec);
    std::printf("\n%s (%d passed, %d failed)\n", g_failed == 0 ? "ALL PASS" : "FAILURES", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
