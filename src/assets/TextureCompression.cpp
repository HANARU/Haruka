#include "TextureCompression.hpp"
#include "../geometry/Ktx2.hpp"
#include "../vfs/PakFormat.hpp"
#include "../vfs/Vfs.hpp"

#include <vulkan/vulkan.h>

#include <cctype>
#include <cstdio>

namespace htex {

const char* targetName(Target t) {
    switch (t) {
    case Target::BC:   return "bc";
    case Target::ASTC: return "astc";
    default:           return "off";
    }
}

bool parseTarget(std::string_view s, Target& out) {
    std::string l(s);
    for (char& c : l) c = (char)std::tolower((unsigned char)c);
    if (l == "off")  { out = Target::Off;  return true; }
    if (l == "bc")   { out = Target::BC;   return true; }
    if (l == "astc") { out = Target::ASTC; return true; }
    return false;
}

const char* useName(Use u) {
    switch (u) {
    case Use::Color:  return "color";
    case Use::Normal: return "normal";
    default:          return "linear";
    }
}

uint32_t formatFor(Target t, Use u) {
    if (t == Target::BC) {
        switch (u) {
        case Use::Color:  return VK_FORMAT_BC7_SRGB_BLOCK;
        case Use::Normal: return VK_FORMAT_BC5_UNORM_BLOCK;
        default:          return VK_FORMAT_BC7_UNORM_BLOCK;
        }
    }
    if (t == Target::ASTC) {
        switch (u) {
        case Use::Color:  return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        case Use::Normal: return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
        default:          return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        }
    }
    return 0;
}

bool eligible(int w, int h) { return w >= 4 && h >= 4; }

Use slotUse(int slot) {
    switch (slot) {
    case kAlbedo: case kEmissive: return Use::Color;
    case kNormal:                 return Use::Normal;
    default:                      return Use::Linear;
    }
}

TexSourceData& slotTex(MeshSourceData& m, int slot) {
    switch (slot) {
    case kAlbedo:     return m.albedo;
    case kNormal:     return m.normalMap;
    case kMetalRough: return m.metalRough;
    case kAo:         return m.ao;
    default:          return m.emissive;
    }
}
const TexSourceData& slotTex(const MeshSourceData& m, int slot) { return slotTex(const_cast<MeshSourceData&>(m), slot); }

uint64_t textureKey(const uint8_t* rgba, int w, int h, Use u) {
    const uint64_t pix = hpak::hash64(rgba, (size_t)w * (size_t)h * 4);
    const uint64_t meta[4] = { (uint64_t)(uint32_t)w, (uint64_t)(uint32_t)h, (uint64_t)u, kCookVersion };
    const uint64_t m = hpak::hash64(meta, sizeof(meta));
    return pix ^ (m * 0x9E3779B97F4A7C15ull) ^ (m >> 29);
}

std::string ktx2Path(Target t, uint64_t key) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)key);
    return std::string("textures/") + targetName(t) + "/" + hex + ".ktx2";
}

AttachStats attachCompressed(std::vector<MeshSourceData>& meshes, Target t) {
    AttachStats st;
    if (t == Target::Off) return st;
    for (MeshSourceData& m : meshes) {
        for (int s = 0; s < kSlotCount; ++s) {
            TexSourceData& tex = slotTex(m, s);
            tex.compressed.reset();
            if (!tex.hasData || !eligible(tex.w, tex.h) || tex.data.size() != (size_t)tex.w * (size_t)tex.h * 4) continue;
            const Use u = slotUse(s);
            const std::string path = ktx2Path(t, textureKey(tex.data.data(), tex.w, tex.h, u));
            std::vector<uint8_t> bytes;
            if (!hvfs::readAll(path, bytes)) { ++st.missing; continue; }
            auto img = std::make_shared<hktx2::Image>();
            std::string err;
            if (!hktx2::parse(std::move(bytes), *img, err) || img->vkFormat != formatFor(t, u) ||
                img->width != (uint32_t)tex.w || img->height != (uint32_t)tex.h) {
                ++st.invalid;
                continue;
            }
            tex.compressed = std::move(img);
            ++st.found;
        }
    }
    return st;
}

} // namespace htex
