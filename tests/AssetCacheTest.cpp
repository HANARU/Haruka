// GLTF 바이너리 캐시(GassetCache)의 유효성 판정을 화면 없이 검산합니다(DEF-105).
//
// 이 테스트가 존재하는 이유: 빌드가 에셋을 실행 폴더로 복사할 때마다 파일의 수정 시각이 바뀝니다. 캐시가 수정
// 시각만 비교하면 내용이 그대로여도 빌드 직후 첫 실행마다 GLTF 를 다시 읽고, 그 실행에서만 에셋 경고가 나와
// 경고 개수가 실행마다 달라 보였습니다. 반대로 내용이 바뀌었는데 캐시를 쓰면 옛 메시가 보입니다.
//
// 임시 폴더를 만들어 그 안에서 실행합니다(캐시 경로가 작업 폴더 기준 assets/cache 입니다).
// 실행: build/Release/HarukaAssetCacheTest.exe

#include "../src/assets/GassetCache.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

void writeBytes(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << bytes;
}

// 복사처럼 내용은 두고 수정 시각만 바꿉니다.
void touch(const std::string& path, int secondsAhead) {
    fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(secondsAhead));
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
    fs::create_directories(root);
    const fs::path oldCwd = fs::current_path();
    fs::current_path(root);

    const std::string src = "model.glb";
    writeBytes(src, std::string(4096, 'a') + "mesh");

    MeshSourceData m;
    m.name = "Cube";
    m.vertices.resize(3);
    for (int i = 0; i < 3; ++i) m.vertices[i] = { { (float)i, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f }, { 1.f, 0.f, 0.f, 1.f } };
    m.indices = { 0, 1, 2 };
    GassetCache::save(src, { m });

    std::printf("\n== 1. 그대로 ==\n");
    check(GassetCache::isValid(src), "만든 직후에는 유효");
    const auto loaded = GassetCache::load(src);
    check(loaded.size() == 1 && loaded[0].name == "Cube" && loaded[0].indices.size() == 3 &&
          loaded[0].vertices[2].pos[0] == 2.f, "읽은 메시가 저장한 것과 같음");

    std::printf("\n== 2. 수정 시각만 바뀜(빌드의 에셋 복사) ==\n");
    const uint64_t before = storedMtime(src);
    touch(src, 30);
    check(GassetCache::isValid(src), "내용이 같으면 수정 시각이 달라도 유효");
    check(storedMtime(src) != before, "기록한 수정 시각을 새 값으로 고침(다음부터 해시 생략)");
    check(GassetCache::isValid(src), "고친 뒤에도 유효");

    std::printf("\n== 3. 내용이 바뀜 ==\n");
    writeBytes(src, std::string(4096, 'a') + "MESH");   // 크기는 같고 내용만 다름
    touch(src, 60);
    check(!GassetCache::isValid(src), "크기가 같아도 내용이 다르면 무효");
    writeBytes(src, std::string(5000, 'a'));
    check(!GassetCache::isValid(src), "크기가 다르면 무효");

    std::printf("\n== 4. 옛 형식 ==\n");
    writeBytes(src, std::string(4096, 'a') + "mesh");
    GassetCache::save(src, { m });
    {
        std::fstream f(GassetCache::cachePath(src), std::ios::binary | std::ios::in | std::ios::out);
        const uint32_t v1 = 1;
        f.seekp(4);
        f.write(reinterpret_cast<const char*>(&v1), 4);
    }
    check(!GassetCache::isValid(src), "버전 1 캐시는 다시 만듦");

    fs::current_path(oldCwd);
    fs::remove_all(root, ec);

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
