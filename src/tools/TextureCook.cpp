#include "TextureCook.hpp"
#include "TextureEncode.hpp"
#include "../assets/GltfLoader.hpp"
#include "../geometry/Ktx2.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace htexcook {
namespace {

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFileAtomic(const fs::path& p, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = fs::path(p.string() + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

uint64_t rgbaChain(uint32_t w, uint32_t h, size_t levels) {
    uint64_t sum = 0;
    for (size_t i = 0; i < levels; ++i) sum += (uint64_t)std::max(1u, w >> i) * std::max(1u, h >> i) * 4u;
    return sum;
}

uint64_t levelSum(const hktx2::Image& img) {
    uint64_t s = 0;
    for (const auto& l : img.levels) s += l.length;
    return s;
}

} // namespace

bool cookTexture(const uint8_t* rgba, int w, int h, htex::Use u, htex::Target t, const fs::path& cacheDir,
                 std::vector<uint8_t>& ktx, bool& fromCache, std::string& err) {
    fromCache = false;
    const uint64_t key  = htex::textureKey(rgba, w, h, u);
    const fs::path file = cacheDir / fs::path(htex::ktx2Path(t, key));
    std::vector<uint8_t> cached;
    if (readFile(file, cached)) {
        hktx2::Image img;
        std::string perr;
        std::vector<uint8_t> copy = cached;
        if (hktx2::parse(std::move(copy), img, perr) && img.vkFormat == htex::formatFor(t, u) &&
            img.width == (uint32_t)w && img.height == (uint32_t)h) {
            ktx = std::move(cached);
            fromCache = true;
            return true;
        }
        // 손상되었거나 다른 판의 파일: 다시 만듭니다.
    }
    ktx = htexenc::encodeKtx2(rgba, (uint32_t)w, (uint32_t)h, t, u, err);
    if (ktx.empty()) return false;
    if (!writeFileAtomic(file, ktx)) { err = "cannot write " + file.string(); return false; }
    return true;
}

bool cookModel(const std::string& modelPath, const ImportSettings& settingsIn, htex::Target t, const fs::path& cacheDir,
               std::vector<Output>& out, std::vector<std::string>& seen, Stats& st, std::string& err) {
    const auto start = std::chrono::steady_clock::now();
    ++st.models;
    ImportSettings settings = settingsIn;
    settings.autoLod = false;   // LOD 는 텍스처와 상관없음(쿠킹 시간만 듦)
    std::vector<MeshSourceData> meshes;
    try {
        std::vector<std::string> deps;
        meshes = GltfLoader::loadAll(modelPath, settings, &deps);
    } catch (const std::exception& e) {
        ++st.failedModels;
        err = e.what();
        return false;
    }
    bool ok = true;
    for (const MeshSourceData& m : meshes) {
        for (int s = 0; s < htex::kSlotCount; ++s) {
            const TexSourceData& tex = htex::slotTex(m, s);
            if (!tex.hasData || tex.data.size() != (size_t)tex.w * (size_t)tex.h * 4) continue;
            if (!htex::eligible(tex.w, tex.h)) { ++st.small; continue; }
            const htex::Use u = htex::slotUse(s);
            const std::string logical = htex::ktx2Path(t, htex::textureKey(tex.data.data(), tex.w, tex.h, u));
            if (std::find(seen.begin(), seen.end(), logical) != seen.end()) continue;
            Output o;
            o.logicalPath = logical;
            bool fromCache = false;
            std::string terr;
            if (!cookTexture(tex.data.data(), tex.w, tex.h, u, t, cacheDir, o.bytes, fromCache, terr)) {
                err = modelPath + " (" + m.name + ", " + htex::useName(u) + "): " + terr;
                ok = false;
                continue;
            }
            hktx2::Image img;
            std::string perr;
            std::vector<uint8_t> copy = o.bytes;
            if (hktx2::parse(std::move(copy), img, perr)) {
                st.ktxBytes  += levelSum(img);
                st.rgbaBytes += rgbaChain(img.width, img.height, img.levels.size());
            }
            ++st.textures;
            if (fromCache) ++st.cached; else ++st.encoded;
            seen.push_back(logical);
            out.push_back(std::move(o));
        }
    }
    st.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return ok;
}

} // namespace htexcook
