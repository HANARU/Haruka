// glTF 외부 파일 경로를 화면 없이 검산합니다(2026-10-31, DEF-125).
//
// 이 시험이 존재하는 이유: .gltf 는 버퍼(.bin) · 텍스처를 uri 로 따로 가리키고, 제작 도구는 "../textures/a.png" 처럼
// 다른 폴더를 가리키는 uri 를 흔히 씁니다. 가상 파일 시스템의 논리 경로는 '.' · '..' 를 받지 않으므로, 로더가 이은 경로를
// 접지 않으면 버퍼를 못 찾아 모델 전체가 기본 큐브가 되고 텍스처는 경고만 남긴 채 빠집니다. 낱개 폴더와 묶음 두 곳에서
// 같은 모델이 같은 결과로 열리는지, 프로젝트 · 묶음 밖으로 올라가는 uri 는 거부하는지 확인합니다.
// 실행: build/Release/HarukaGltfTest.exe

#include "../src/assets/GltfLoader.hpp"
#include "../src/vfs/PakReader.hpp"
#include "../src/vfs/PakWriter.hpp"
#include "../src/vfs/PathUtf8.hpp"
#include "../src/vfs/Vfs.hpp"

#include "stb_image_write.h"   // 구현은 GltfLoader.cpp 에 있습니다

#include <cstdio>
#include <cstring>
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

using Bytes = std::vector<uint8_t>;

void writeFile(const fs::path& p, const Bytes& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), (std::streamsize)b.size());
}

Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

// 삼각형 하나: 위치 3개(36B) + UV 3개(24B).
Bytes triangleBin() {
    const float data[] = { 0, 0, 0,  1, 0, 0,  0, 1, 0,   0, 0,  1, 0,  0, 1 };
    Bytes b(sizeof(data));
    std::memcpy(b.data(), data, sizeof(data));
    return b;
}

// 2x2 RGBA PNG.
Bytes tinyPng() {
    const uint8_t px[16] = { 255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,  255, 255, 255, 255 };
    Bytes out;
    stbi_write_png_to_func([](void* ctx, void* data, int size) {
        auto* o = static_cast<Bytes*>(ctx);
        o->insert(o->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    }, &out, 2, 2, 4, px, 8);
    return out;
}

std::string gltfJson(const std::string& bufferUri, const std::string& imageUri) {
    return std::string("{\"asset\":{\"version\":\"2.0\"},") +
        "\"buffers\":[{\"uri\":\"" + bufferUri + "\",\"byteLength\":60}]," +
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":24}]," +
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]}," +
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"}]," +
        "\"images\":[{\"uri\":\"" + imageUri + "\"}],\"textures\":[{\"source\":0}]," +
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}]," +
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"material\":0}]}]," +
        "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
}

// 불러온 결과가 삼각형 하나 + 2x2 텍스처인지.
bool loadsTriangleWithTexture(const std::string& path, std::string& why) {
    try {
        const std::vector<MeshSourceData> all = GltfLoader::loadAll(path);
        if (all.size() != 1) { why = "meshes " + std::to_string(all.size()); return false; }
        if (all[0].vertices.size() != 3) { why = "vertices " + std::to_string(all[0].vertices.size()); return false; }
        // 텍스처가 없으면 로더가 팩터로 1x1 을 만듭니다. 2x2 여야 파일의 텍스처를 읽은 것입니다.
        if (!all[0].albedo.hasData || all[0].albedo.w != 2 || all[0].albedo.h != 2) { why = "texture missing"; return false; }
        return true;
    } catch (const std::exception& e) {
        why = e.what();
        return false;
    }
}

// 모델은 열리지만 파일의 텍스처는 읽지 않았는지(팩터로 만든 1x1).
bool loadsWithoutFileTexture(const std::string& path, std::string& why) {
    try {
        const std::vector<MeshSourceData> all = GltfLoader::loadAll(path);
        if (all.size() != 1 || all[0].vertices.size() != 3) { why = "model not loaded as expected"; return false; }
        if (all[0].albedo.w == 2 && all[0].albedo.h == 2) { why = "texture was read"; return false; }
        return true;
    } catch (const std::exception& e) {
        why = e.what();
        return false;
    }
}

bool loadFails(const std::string& path) {
    try {
        GltfLoader::loadAll(path);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

} // namespace

int main() {
    std::printf("Haruka glTF 외부 파일 경로 검산\n");
    const fs::path tmp = fs::temp_directory_path() / "haruka_gltf_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);

    const Bytes bin = triangleBin(), png = tinyPng();
    check(!png.empty(), "시험용 PNG 를 만듦");
    const std::string up = gltfJson("../bin/tri.bin", "./../textures/a.png");
    const std::string same = gltfJson("tri.bin", "a.png");
    const std::string escape = gltfJson("../../../outside.bin", "a.png");

    // ── 1. 낱개 폴더(에디터 · 개발 실행) ─────────────────────────────────────────
    std::printf("\n== 1. 낱개 폴더 ==\n");
    const fs::path root = tmp / "project";
    writeFile(root / "assets/models/up.gltf", bytesOf(up));
    writeFile(root / "assets/bin/tri.bin", bin);
    writeFile(root / "assets/textures/a.png", png);
    writeFile(root / "assets/models/same.gltf", bytesOf(same));
    writeFile(root / "assets/models/tri.bin", bin);
    writeFile(root / "assets/models/a.png", png);
    writeFile(root / "assets/models/escape.gltf", bytesOf(escape));
    // "assets/models" 에서 '..' 세 번은 논리 경로의 맨 위보다 위입니다. 넘는 '..' 를 버리고 "outside.bin" 으로 읽으면
    // 안 되므로, 그 이름의 파일을 마운트 맨 위에 실제로 둡니다.
    writeFile(root / "outside.bin", bin);
    hvfs::reset();
    hvfs::mountDirectory(root, "", 0);
    std::string why;
    check(loadsTriangleWithTexture("assets/models/same.gltf", why), "같은 폴더의 uri 는 그대로 열림 " + why);
    check(loadsTriangleWithTexture("assets/models/up.gltf", why), "uri 의 '../' · './' 를 접어 다른 폴더의 버퍼 · 텍스처를 찾음 " + why);
    check(loadFails("assets/models/escape.gltf"), "논리 경로의 맨 위보다 위로 올라가는 uri 는 거부(모델을 열지 않음)");

    // 드라이브 문자(절대 경로) uri. tinygltf 는 "기준 폴더/uri" 와 "./uri" 두 후보를 시도하는데, 뒤의 것을 접어 절대 경로가 되면
    // 마운트를 우회해 디스크에서 읽습니다. 그런 파일은 묶음에 들어가지 않아 만든 PC 에서만 보이므로, 실제로 있는 파일이어도
    // 읽지 않아야 합니다(2026-10-31 검토).
    writeFile(tmp / "abs/tri.bin", bin);
    writeFile(tmp / "abs/a.png", png);
    const std::string absBin = hpak::utf8Of(tmp / "abs/tri.bin"), absPng = hpak::utf8Of(tmp / "abs/a.png");
    writeFile(root / "assets/models/absimg.gltf", bytesOf(gltfJson("tri.bin", absPng)));
    writeFile(root / "assets/models/absbuf.gltf", bytesOf(gltfJson(absBin, "a.png")));
    check(loadsWithoutFileTexture("assets/models/absimg.gltf", why), "절대 경로 uri 의 텍스처는 디스크에 있어도 읽지 않음 " + why);
    check(loadFails("assets/models/absbuf.gltf"), "절대 경로 uri 의 버퍼는 디스크에 있어도 읽지 않음(모델을 열지 않음)");
    // 논리 경로 맨 위의 glTF 는 기준 폴더가 비어, tinygltf 가 uri 를 그대로 넘깁니다(입력 자체가 "C:/..." 로 시작).
    writeFile(root / "root.gltf", bytesOf(gltfJson("assets/models/tri.bin", absPng)));
    check(loadsWithoutFileTexture("root.gltf", why), "맨 위의 glTF 도 절대 경로 uri 의 텍스처는 읽지 않음 " + why);
    // glTF 자체를 절대 경로로 열면(시험의 임시 파일, 외부 파일) 이은 경로를 운영체제에 맡깁니다(예전과 같음).
    check(loadsTriangleWithTexture(hpak::utf8Of(root / "assets/models/up.gltf"), why),
          "glTF 를 절대 경로로 열면 '../' uri 를 운영체제 경로로 찾음 " + why);

    // ── 2. 묶음(배포 ZIP 의 game.hpak) ──────────────────────────────────────────
    std::printf("\n== 2. 묶음 ==\n");
    {
        std::vector<hpak::PakInput> in;
        auto add = [&](const char* p, const Bytes& d) {
            hpak::PakInput x;
            x.path = p;
            x.data = d;
            in.push_back(std::move(x));
        };
        add("assets/models/up.gltf", bytesOf(up));
        add("assets/bin/tri.bin", bin);
        add("assets/textures/a.png", png);
        std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
        std::string err;
        check(hpak::writePak(std::move(in), ss, err), "묶음을 씀 " + err);
        const std::string s = ss.str();
        auto reader = std::make_unique<hpak::PakReader>();
        check(reader->open(hpak::memorySource(Bytes(s.begin(), s.end())), err), "묶음을 엶 " + err);
        hvfs::reset();
        hvfs::mountPack(std::move(reader), "game.hpak", 20);
        check(loadsTriangleWithTexture("assets/models/up.gltf", why), "묶음 안에서도 '../' uri 의 버퍼 · 텍스처를 찾음 " + why);
    }

    hvfs::reset();
    fs::remove_all(tmp, ec);
    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
