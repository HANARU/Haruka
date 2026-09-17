#include "GassetCache.hpp"
#include "AssetLog.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace fs = std::filesystem;

static constexpr uint32_t kMagic   = 0x47415354u; // "GAST"
static constexpr uint32_t kVersion = 1u;

static uint64_t getMtime(const std::string& path) {
    std::error_code ec;
    auto ft = fs::last_write_time(path, ec);
    if (ec) return 0;
    return (uint64_t)ft.time_since_epoch().count();
}

std::string GassetCache::cachePath(const std::string& srcPath) {
    size_t h = std::hash<std::string>{}(srcPath);
    std::ostringstream oss;
    oss << "assets/cache/" << std::hex << std::setw(16) << std::setfill('0') << h << ".gasset";
    return oss.str();
}

bool GassetCache::isValid(const std::string& srcPath) {
    std::string cp = cachePath(srcPath);
    if (!fs::exists(cp)) return false;
    std::ifstream f(cp, std::ios::binary);
    if (!f) return false;
    uint32_t magic = 0, version = 0;
    uint64_t mtime = 0;
    f.read(reinterpret_cast<char*>(&magic),   4);
    f.read(reinterpret_cast<char*>(&version), 4);
    f.read(reinterpret_cast<char*>(&mtime),   8);
    if (!f || magic != kMagic || version != kVersion) return false;
    return mtime == getMtime(srcPath);
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

    rU32(f); rU32(f); rU64(f);          // skip magic, version, mtime
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
