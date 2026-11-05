// GLTF 바이너리 캐시(GassetCache)의 유효성 판정을 화면 없이 검산합니다(DEF-105).
//
// 이 테스트가 존재하는 이유: 빌드가 에셋을 실행 폴더로 복사할 때마다 파일의 수정 시각이 바뀝니다. 캐시가 수정
// 시각만 비교하면 내용이 그대로여도 빌드 직후 첫 실행마다 GLTF 를 다시 읽고, 그 실행에서만 에셋 경고가 나와
// 경고 개수가 실행마다 달라 보였습니다. 반대로 내용이 바뀌었는데 캐시를 쓰면 옛 메시가 보입니다.
//
// 2026-10-24 부터 원본은 가상 파일 시스템(vfs)으로 찾고 캐시 키는 논리 경로입니다. 임시 폴더를 낱개 폴더로 마운트하고,
// 같은 원본을 묶음 파일(.hpak)에 넣어 묶음 안 원본의 판정(목차의 내용 해시)도 봅니다.
// 실행: build/Release/HarukaAssetCacheTest.exe

#include "../src/assets/GassetCache.hpp"
#include "../src/core/ProjectPaths.hpp"
#include "../src/vfs/PakReader.hpp"
#include "../src/vfs/PakWriter.hpp"
#include "../src/vfs/Vfs.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << bytes;
}

// 복사처럼 내용은 두고 수정 시각만 바꿉니다.
void touch(const fs::path& path, int secondsAhead) {
    fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(secondsAhead));
}

// 파일 하나를 담은 메모리 묶음을 마운트합니다(낱개 폴더보다 위).
void mountPackWith(const std::string& logical, const std::string& bytes, int priority) {
    std::vector<hpak::PakInput> in(1);
    in[0].path = logical;
    in[0].data.assign(bytes.begin(), bytes.end());
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    std::string err;
    if (!hpak::writePak(std::move(in), ss, err)) { std::printf("  writePak failed: %s\n", err.c_str()); return; }
    const std::string s = ss.str();
    auto reader = std::make_unique<hpak::PakReader>();
    if (!reader->open(hpak::memorySource(std::vector<uint8_t>(s.begin(), s.end())), err)) {
        std::printf("  pack open failed: %s\n", err.c_str());
        return;
    }
    hvfs::mountPack(std::move(reader), "test.hpak", priority);
}

uint64_t storedMtime(const std::string& src) {
    std::ifstream f(GassetCache::cachePath(src), std::ios::binary);
    f.seekg(8);
    uint64_t v = 0;
    f.read(reinterpret_cast<char*>(&v), 8);
    return v;
}

} // namespace

int main() {
    std::printf("Haruka GLTF 캐시 유효성 검산\n");

    const fs::path root = fs::temp_directory_path() / "haruka_asset_cache_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "models");
    hvfs::reset();
    hvfs::mountDirectory(root, "", 0);
    GassetCache::setDirectory((root / "cache").string());

    const std::string src  = "models/model.glb";            // 논리 경로
    const fs::path    file = root / "models" / "model.glb";  // 그 낱개 파일
    writeBytes(file, std::string(4096, 'a') + "mesh");

    MeshSourceData m;
    m.name = "Cube";
    m.vertices.resize(3);
    for (int i = 0; i < 3; ++i) m.vertices[i] = { { (float)i, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f }, { 1.f, 0.f, 0.f, 1.f } };
    m.indices = { 0, 1, 2 };
    m.doubleSided = true;
    m.alphaMask   = true;
    m.alphaCutoff = 0.25f;
    GassetCache::save(src, { m });

    std::printf("\n== 1. 그대로 ==\n");
    check(GassetCache::isValid(src), "만든 직후에는 유효");
    const auto loaded = GassetCache::load(src);
    check(loaded.size() == 1 && loaded[0].name == "Cube" && loaded[0].indices.size() == 3 &&
          loaded[0].vertices[2].pos[0] == 2.f, "읽은 메시가 저장한 것과 같음");
    check(loaded.size() == 1 && loaded[0].doubleSided && !loaded[0].isBlend, "doubleSided 왕복(버전 6, isBlend 와 섞이지 않음)");
    check(loaded.size() == 1 && loaded[0].alphaMask && loaded[0].alphaCutoff == 0.25f, "alphaMask · alphaCutoff 왕복(버전 6)");

    std::printf("\n== 2. 수정 시각만 바뀜(빌드의 에셋 복사) ==\n");
    const uint64_t before = storedMtime(src);
    touch(file, 30);
    check(GassetCache::isValid(src), "내용이 같으면 수정 시각이 달라도 유효");
    check(storedMtime(src) != before, "기록한 수정 시각을 새 값으로 고침(다음부터 해시 생략)");
    check(GassetCache::isValid(src), "고친 뒤에도 유효");

    std::printf("\n== 3. 내용이 바뀜 ==\n");
    writeBytes(file, std::string(4096, 'a') + "MESH");   // 크기는 같고 내용만 다름
    touch(file, 60);
    check(!GassetCache::isValid(src), "크기가 같아도 내용이 다르면 무효");
    writeBytes(file, std::string(5000, 'a'));
    check(!GassetCache::isValid(src), "크기가 다르면 무효");

    std::printf("\n== 4. 옛 형식 ==\n");
    writeBytes(file, std::string(4096, 'a') + "mesh");
    GassetCache::save(src, { m });
    auto setVersion = [&](uint32_t v) {
        std::fstream f(GassetCache::cachePath(src), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(4);
        f.write(reinterpret_cast<const char*>(&v), 4);
    };
    setVersion(1);
    check(!GassetCache::isValid(src), "버전 1 캐시는 다시 만듦");
    setVersion(2);
    check(!GassetCache::isValid(src), "버전 2 캐시(실제 경로 키 · FNV 해시)도 다시 만듦");
    setVersion(3);
    check(!GassetCache::isValid(src), "버전 3 캐시(uri 의 '../' 텍스처를 놓쳤을 수 있음, DEF-125)도 다시 만듦");
    setVersion(4);
    check(!GassetCache::isValid(src), "버전 4 캐시(doubleSided 없음)도 다시 만듦");
    setVersion(5);
    check(!GassetCache::isValid(src), "버전 5 캐시(alphaMask 없음)도 다시 만듦");

    // 캐시 키(2026-10-24): 실제 경로의 std::hash 대신 정규화한 논리 경로라, 표기가 달라도 · 마운트 위치가 달라도 같습니다.
    std::printf("\n== 5. 키 ==\n");
    GassetCache::save(src, { m });
    check(GassetCache::cachePath(src) == GassetCache::cachePath("Models\\MODEL.glb") &&
          GassetCache::cachePath(src) == GassetCache::cachePath("./models/model.glb"),
          "구분자 · 대소문자 · './' 가 달라도 같은 캐시 파일");
    const std::string name = fs::path(GassetCache::cachePath(src)).filename().string();
    check(name.size() == 16 + 7 && name.compare(16, 7, ".gasset") == 0, "파일 이름은 16자리 16진수 + .gasset");
    check(GassetCache::cachePath(src) != GassetCache::cachePath("models/other.glb"), "다른 경로는 다른 캐시 파일");

    // 원본이 묶음 안이면 목차의 크기 · 내용 해시로 판정합니다(파일을 읽지 않음). 같은 원본에서 만든 캐시는 낱개에서
    // 만들었든 묶음에서 만들었든 그대로 씁니다.
    std::printf("\n== 6. 묶음 안 원본 ==\n");
    mountPackWith(src, std::string(4096, 'a') + "mesh", 10);
    hvfs::Stat st;
    check(hvfs::stat(src, st) && st.inPack, "묶음이 낱개 파일보다 위");
    check(GassetCache::isValid(src), "낱개에서 만든 캐시를 같은 내용의 묶음 원본에도 씀");
    fs::remove(file, ec);
    check(GassetCache::isValid(src), "낱개 파일이 없어도 묶음 원본으로 판정");
    mountPackWith(src, std::string(4096, 'a') + "MESH", 20);   // 같은 크기, 다른 내용(패치)
    check(!GassetCache::isValid(src), "위에 올린 묶음의 내용이 다르면 무효");
    GassetCache::save(src, { m });
    check(GassetCache::isValid(src), "묶음 원본으로 다시 만든 캐시는 유효");

    hvfs::reset();
    fs::remove_all(root, ec);

    // 에셋 논리 경로의 해석(2026-10-15). 에디터 빌드는 프로젝트 폴더의 assets/ 를 직접 읽고 쓰지만, 캐시는
    // 파생 데이터라 실행 폴더에 남아야 하고 assets/ 밖의 경로(임시 파일 등)는 건드리지 않아야 합니다.
    std::printf("\n== 에셋 경로 해석(hproj) ==\n");
    hproj::setRoot({});
    check(hproj::resolve("assets/models/a.glb") == "assets/models/a.glb", "루트가 없으면 그대로(실행 폴더 기준)");
    hproj::setRoot("P:/proj");
    check(hproj::resolve("assets/models/a.glb") == "P:/proj/assets/models/a.glb", "루트가 있으면 assets/ 를 프로젝트 폴더로");
    check(hproj::resolve("assets/prefabs") == "P:/proj/assets/prefabs", "폴더 경로도 바꿈(프리팹 저장 · 목록)");
    check(hproj::resolve("assets/cache/0123.gasset") == "assets/cache/0123.gasset", "assets/cache 는 바꾸지 않음");
    check(hproj::resolve("C:/tmp/x.wav") == "C:/tmp/x.wav", "assets/ 밖 경로는 그대로");
    check(hproj::resolve("assetsX/y") == "assetsX/y", "assets/ 로 시작하지 않으면 그대로");
    hproj::setRoot({});

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
