#include "GassetCache.hpp"
#include "AssetLog.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace fs = std::filesystem;

static constexpr uint32_t kMagic   = 0x47415354u; // "GAST"
// 2: 원본의 크기 · 내용 해시를 함께 기록합니다(DEF-105). 버전 1 캐시는 다시 만듭니다.
static constexpr uint32_t kVersion = 2u;
// 머리: magic(4) version(4) mtime(8) size(8) hash(8) meshCount(4)
static constexpr std::streamoff kMtimeOffset = 8;

static uint64_t getMtime(const std::string& path) {
    std::error_code ec;
    auto ft = fs::last_write_time(path, ec);
    if (ec) return 0;
    return (uint64_t)ft.time_since_epoch().count();
}

static uint64_t getSize(const std::string& path) {
    std::error_code ec;
    const auto n = fs::file_size(path, ec);
    return ec ? 0 : (uint64_t)n;
}

// 원본 내용의 FNV-1a 64비트 해시. 수정 시각이 바뀌었을 때만 계산합니다.
static uint64_t hashFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    uint64_t h = 1469598103934665603ull;
    char buf[1 << 16];
    while (f) {
        f.read(buf, sizeof(buf));
        const std::streamsize n = f.gcount();
        for (std::streamsize i = 0; i < n; ++i) { h ^= (uint8_t)buf[i]; h *= 1099511628211ull; }
    }
    return h;
}

std::string GassetCache::cachePath(const std::string& srcPath) {
    size_t h = std::hash<std::string>{}(srcPath);
    std::ostringstream oss;
    oss << "assets/cache/" << std::hex << std::setw(16) << std::setfill('0') << h << ".gasset";
    return oss.str();
}

// 수정 시각과 크기가 같으면 그대로 씁니다. 수정 시각만 다르면 내용 해시를 비교해, 같으면 캐시를 쓰고 기록한
// 수정 시각을 새 값으로 고쳐 다음부터는 해시를 다시 계산하지 않습니다. 예전에는 수정 시각만 비교해, 빌드가
// 에셋을 실행 폴더로 복사하며 시각을 바꿀 때마다 GLTF 를 다시 읽었습니다(DEF-105). Godot 의 가져오기도
// 수정 시각이 다르면 내용 해시(md5)로 다시 확인합니다.
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
    const uint64_t curSize = getSize(srcPath);
    if (size != curSize) return false;
    const uint64_t curMtime = getMtime(srcPath);
    if (mtime == curMtime) return true;
    if (hash != hashFile(srcPath)) return false;

    std::fstream rw(cp, std::ios::binary | std::ios::in | std::ios::out);
    if (rw) {
        rw.seekp(kMtimeOffset);
        rw.write(reinterpret_cast<const char*>(&curMtime), 8);
    }
    HLOG(LogAsset, Verbose) << "gasset cache kept (same content, new timestamp): " << srcPath;
    return true;
}

// ── write helpers ─────────────────────────────────────────────────────────────

static void wU8 (std::ofstream& f, uint8_t  v) { f.write((const char*)&v, 1); }
static void wU32(std::ofstream& f, uint32_t v) { f.write((const char*)&v, 4); }
static void wU64(std::ofstream& f, uint64_t v) { f.write((const char*)&v, 8); }

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
    fs::create_directories("assets/cache");
    std::string cp  = cachePath(srcPath);
    std::string tmp = cp + ".tmp";

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { HLOG(LogAsset, Warning) << "gasset cache cannot write: " << tmp; return; }

        wU32(f, kMagic);
        wU32(f, kVersion);
        wU64(f, getMtime(srcPath));
        wU64(f, getSize(srcPath));
        wU64(f, hashFile(srcPath));
        wU32(f, (uint32_t)meshes.size());

        for (const auto& m : meshes) {
            wStr(f, m.name);
            wU8 (f, m.isBlend ? 1u : 0u);
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
