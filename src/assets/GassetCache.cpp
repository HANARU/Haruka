#include "GassetCache.hpp"
#include "AssetLog.hpp"
#include "../vfs/PakFormat.hpp"
#include "../vfs/Vfs.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace fs = std::filesystem;

static constexpr uint32_t kMagic   = 0x47415354u; // "GAST"
// 2: 원본의 크기 · 내용 해시를 함께 기록합니다(DEF-105). 버전 1 캐시는 다시 만듭니다.
// 3: 파일 이름이 실제 경로가 아니라 논리 경로에서 나오고, 원본 해시가 묶음 목차와 같은 XXH3-64 입니다(2026-10-24).
//    버전 2 이하 캐시는 다시 만듭니다.
// 4: 내용은 3 과 같습니다. 버전 3 은 uri 에 "../" · "./" 가 든 텍스처를 찾지 못한 채(DEF-125) 텍스처 없이 저장했을 수
//    있어 다시 만듭니다(2026-10-31).
// 5: 메시마다 isBlend 다음에 doubleSided(u8)를 둡니다. 버전 4 이하 캐시는 다시 만듭니다(2026-11-01).
// 6: 5 다음에 alphaMask(u8) · alphaCutoff(f32)를 둡니다. 버전 5 이하 캐시는 다시 만듭니다(2026-11-02).
static constexpr uint32_t kVersion = 6u;
// 머리: magic(4) version(4) mtime(8) size(8) hash(8) meshCount(4)
static constexpr std::streamoff kMtimeOffset = 8;

// 원본 정보는 가상 파일 시스템에서 얻습니다. 묶음 안이면 목차의 크기 · 내용 해시를 그대로 쓰고(파일을 읽지 않음),
// 낱개 파일이면 수정 시각을 함께 씁니다. 해시는 낱개 파일일 때 필요할 때만 계산합니다.
static uint64_t sourceHash(const std::string& srcPath, const hvfs::Stat& st) {
    if (st.inPack) return st.contentHash;
    uint64_t h = 0;
    return hvfs::contentHash(srcPath, h) ? h : 0;
}

static std::string& cacheDirStorage() {
    static std::string dir = "assets/cache";
    return dir;
}

void GassetCache::setDirectory(const std::string& dir) { cacheDirStorage() = dir; }
const std::string& GassetCache::directory() { return cacheDirStorage(); }

// 캐시 파일 이름은 정규화한 논리 경로(대소문자 무시)의 FNV-1a 64비트 해시입니다(2026-10-24). 실행 · 컴파일러 · 기기와
// 상관없이 같은 값이라, 나중에 캐시를 미리 만들어 묶음에 넣을 수 있습니다. 예전에는 실제 경로의 std::hash 라 실행마다
// 달라질 수 있었고 에디터(절대 경로)와 게임의 키가 달랐습니다. 논리 경로가 아닌 경로(절대 경로)는 그 문자열을 씁니다.
std::string GassetCache::cachePath(const std::string& srcPath) {
    std::string norm;
    const std::string key = hpak::normalizePath(srcPath, norm) ? hpak::foldKey(norm) : srcPath;
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : key) { h ^= c; h *= 1099511628211ull; }
    std::ostringstream oss;
    oss << cacheDirStorage() << "/" << std::hex << std::setw(16) << std::setfill('0') << h << ".gasset";
    return oss.str();
}

// 크기가 다르면 무효입니다. 원본이 묶음 안이면 목차의 내용 해시로 판정합니다. 낱개 파일이면 수정 시각이 같을 때 그대로
// 쓰고, 다르면 내용 해시를 비교해 같으면 캐시를 쓰고 기록한 수정 시각을 새 값으로 고쳐 다음부터는 해시를 다시 계산하지
// 않습니다. 예전에는 수정 시각만 비교해, 빌드가 에셋을 실행 폴더로 복사하며 시각을 바꿀 때마다 GLTF 를 다시 읽었습니다
// (DEF-105). Godot 의 가져오기도 수정 시각이 다르면 내용 해시(md5)로 다시 확인합니다.
bool GassetCache::isValid(const std::string& srcPath) {
    std::string cp = cachePath(srcPath);
    if (!fs::exists(cp)) return false;
    uint64_t mtime = 0, size = 0, hash = 0;
    {
        std::ifstream f(cp, std::ios::binary);
        if (!f) return false;
        uint32_t magic = 0, version = 0;
        f.read(reinterpret_cast<char*>(&magic),   4);
        f.read(reinterpret_cast<char*>(&version), 4);
        f.read(reinterpret_cast<char*>(&mtime),   8);
        f.read(reinterpret_cast<char*>(&size),    8);
        f.read(reinterpret_cast<char*>(&hash),    8);
        if (!f || magic != kMagic || version != kVersion) return false;
    }
    hvfs::Stat st;
    if (!hvfs::stat(srcPath, st) || size != st.size) return false;
    if (st.inPack) return hash == st.contentHash;
    if (mtime == st.mtime) return true;
    if (hash != sourceHash(srcPath, st)) return false;

    std::fstream rw(cp, std::ios::binary | std::ios::in | std::ios::out);
    if (rw) {
        rw.seekp(kMtimeOffset);
        rw.write(reinterpret_cast<const char*>(&st.mtime), 8);
    }
    HLOG(LogAsset, Verbose) << "gasset cache kept (same content, new timestamp): " << srcPath;
    return true;
}

// ── write helpers ─────────────────────────────────────────────────────────────

static void wU8 (std::ofstream& f, uint8_t  v) { f.write((const char*)&v, 1); }
static void wU32(std::ofstream& f, uint32_t v) { f.write((const char*)&v, 4); }
static void wU64(std::ofstream& f, uint64_t v) { f.write((const char*)&v, 8); }
static void wF32(std::ofstream& f, float    v) { f.write((const char*)&v, 4); }

static void wStr(std::ofstream& f, const std::string& s) {
    wU32(f, (uint32_t)s.size());
    f.write(s.data(), (std::streamsize)s.size());
}
static void wTex(std::ofstream& f, const TexSourceData& t) {
    wU8(f, t.hasData ? 1u : 0u);
    if (t.hasData) {
        wU32(f, (uint32_t)t.w);
        wU32(f, (uint32_t)t.h);
        wU32(f, (uint32_t)t.data.size());
        f.write((const char*)t.data.data(), (std::streamsize)t.data.size());
    }
}

void GassetCache::save(const std::string& srcPath, const std::vector<MeshSourceData>& meshes) {
    std::error_code dirEc;
    fs::create_directories(cacheDirStorage(), dirEc);
    std::string cp  = cachePath(srcPath);
    std::string tmp = cp + ".tmp";

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { HLOG(LogAsset, Warning) << "gasset cache cannot write: " << tmp; return; }

        hvfs::Stat st;
        hvfs::stat(srcPath, st);
        wU32(f, kMagic);
        wU32(f, kVersion);
        wU64(f, st.mtime);
        wU64(f, st.size);
        wU64(f, sourceHash(srcPath, st));
        wU32(f, (uint32_t)meshes.size());

        for (const auto& m : meshes) {
            wStr(f, m.name);
            wU8 (f, m.isBlend ? 1u : 0u);
            wU8 (f, m.doubleSided ? 1u : 0u);   // 버전 5. load 와 같은 순서
            wU8 (f, m.alphaMask ? 1u : 0u);     // 버전 6. load 와 같은 순서
            wF32(f, m.alphaCutoff);
            wU32(f, (uint32_t)m.vertices.size());
            wU32(f, (uint32_t)m.indices.size());
            wTex(f, m.albedo);
            wTex(f, m.normalMap);
            wTex(f, m.metalRough);
            wTex(f, m.ao);
            wTex(f, m.emissive);
            f.write((const char*)m.vertices.data(),
                    (std::streamsize)(m.vertices.size() * sizeof(MeshSourceVertex)));
            f.write((const char*)m.indices.data(),
                    (std::streamsize)(m.indices.size()  * sizeof(uint32_t)));
        }

        if (!f) {
            HLOG(LogAsset, Warning) << "gasset cache write error: " << tmp;
            std::error_code ec; fs::remove(tmp, ec);
            return;
        }
    } // flush & close before rename

    std::error_code ec;
    fs::rename(tmp, cp, ec);
    if (ec) {
        HLOG(LogAsset, Warning) << "gasset cache rename failed: " << ec.message();
        fs::remove(tmp, ec);
    }
}

// ── read helpers ──────────────────────────────────────────────────────────────

static uint8_t  rU8 (std::ifstream& f) { uint8_t  v; f.read((char*)&v, 1); return v; }
static uint32_t rU32(std::ifstream& f) { uint32_t v; f.read((char*)&v, 4); return v; }
static uint64_t rU64(std::ifstream& f) { uint64_t v; f.read((char*)&v, 8); return v; }
static float    rF32(std::ifstream& f) { float    v; f.read((char*)&v, 4); return v; }

static std::string rStr(std::ifstream& f) {
    uint32_t len = rU32(f);
    std::string s(len, '\0');
    f.read(s.data(), len);
    return s;
}
static TexSourceData rTex(std::ifstream& f) {
    TexSourceData t;
    t.hasData = (rU8(f) != 0);
    if (t.hasData) {
        t.w = (int)rU32(f);
        t.h = (int)rU32(f);
        uint32_t sz = rU32(f);
        t.data.resize(sz);
        f.read((char*)t.data.data(), sz);
    }
    return t;
}

std::vector<MeshSourceData> GassetCache::load(const std::string& srcPath) {
    std::string cp = cachePath(srcPath);
    std::ifstream f(cp, std::ios::binary);
    if (!f) throw std::runtime_error("GassetCache: cannot open " + cp);

    rU32(f); rU32(f); rU64(f); rU64(f); rU64(f);   // skip magic, version, mtime, size, hash
    uint32_t meshCount = rU32(f);

    std::vector<MeshSourceData> result;
    result.reserve(meshCount);

    for (uint32_t i = 0; i < meshCount; ++i) {
        MeshSourceData m;
        m.name    = rStr(f);
        m.isBlend = (rU8(f) != 0);
        m.doubleSided = (rU8(f) != 0);   // 버전 5. save 와 같은 순서
        m.alphaMask   = (rU8(f) != 0);   // 버전 6. save 와 같은 순서
        m.alphaCutoff = rF32(f);
        uint32_t vc = rU32(f);
        uint32_t ic = rU32(f);
        m.albedo     = rTex(f);
        m.normalMap  = rTex(f);
        m.metalRough = rTex(f);
        m.ao         = rTex(f);
        m.emissive   = rTex(f);
        m.vertices.resize(vc);
        m.indices.resize(ic);
        f.read((char*)m.vertices.data(), (std::streamsize)(vc * sizeof(MeshSourceVertex)));
        f.read((char*)m.indices.data(),  (std::streamsize)(ic * sizeof(uint32_t)));
        if (!f) throw std::runtime_error("GassetCache: corrupted file " + cp);
        result.push_back(std::move(m));
    }

    HLOG(LogAsset, Verbose) << "gasset cache hit: " << srcPath << "  meshes=" << meshCount;
    return result;
}
