#include "Ktx2.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>

namespace hktx2 {
namespace {

constexpr uint8_t kIdentifier[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };
constexpr size_t  kHeaderBytes    = 80;   // 식별자 12 + 머리 36 + 색인 32
constexpr size_t  kLevelEntry     = 24;   // byteOffset · byteLength · uncompressedByteLength
constexpr uint32_t kMaxLevels     = 16;   // 32768² 까지

uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint64_t rd64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
void wr32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
void wr64(std::vector<uint8_t>& b, size_t at, uint64_t v) { std::memcpy(b.data() + at, &v, 8); }

uint64_t alignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }
uint32_t gcd(uint32_t a, uint32_t b) { while (b) { const uint32_t t = a % b; a = b; b = t; } return a; }
uint32_t lcm4(uint32_t blockBytes) { return blockBytes / gcd(blockBytes, 4) * 4; }

// 데이터 형식 설명(KHR Data Format) 의 색 모델 번호(khr_df.h).
constexpr uint32_t kModelBC5  = 132;
constexpr uint32_t kModelBC7  = 134;
constexpr uint32_t kModelASTC = 162;

} // namespace

BlockInfo blockInfo(uint32_t f) {
    switch (f) {
    case VK_FORMAT_BC5_UNORM_BLOCK:       return { 4, 4, 16, false };
    case VK_FORMAT_BC7_UNORM_BLOCK:       return { 4, 4, 16, false };
    case VK_FORMAT_BC7_SRGB_BLOCK:        return { 4, 4, 16, true };
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:  return { 4, 4, 16, false };
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:   return { 4, 4, 16, true };
    case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:  return { 5, 5, 16, false };
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:   return { 5, 5, 16, true };
    case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:  return { 6, 6, 16, false };
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:   return { 6, 6, 16, true };
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:  return { 8, 8, 16, false };
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:   return { 8, 8, 16, true };
    default:                              return {};
    }
}

uint64_t levelBytes(uint32_t f, uint32_t w, uint32_t h) {
    const BlockInfo b = blockInfo(f);
    if (b.bytes == 0 || w == 0 || h == 0) return 0;
    return (uint64_t)((w + b.w - 1) / b.w) * ((h + b.h - 1) / b.h) * b.bytes;
}

std::string formatName(uint32_t f) {
    switch (f) {
    case VK_FORMAT_BC5_UNORM_BLOCK:      return "BC5_UNORM";
    case VK_FORMAT_BC7_UNORM_BLOCK:      return "BC7_UNORM";
    case VK_FORMAT_BC7_SRGB_BLOCK:       return "BC7_SRGB";
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: return "ASTC_4x4_UNORM";
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:  return "ASTC_4x4_SRGB";
    case VK_FORMAT_ASTC_5x5_UNORM_BLOCK: return "ASTC_5x5_UNORM";
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:  return "ASTC_5x5_SRGB";
    case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: return "ASTC_6x6_UNORM";
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:  return "ASTC_6x6_SRGB";
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: return "ASTC_8x8_UNORM";
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:  return "ASTC_8x8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM:       return "RGBA8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:        return "RGBA8_SRGB";
    default:                             return "format " + std::to_string(f);
    }
}

bool parse(std::vector<uint8_t>&& bytes, Image& out, std::string& err) {
    out = Image{};
    const uint64_t size = bytes.size();
    if (size < kHeaderBytes) { err = "file is shorter than the KTX2 header"; return false; }
    const uint8_t* p = bytes.data();
    if (std::memcmp(p, kIdentifier, sizeof(kIdentifier)) != 0) { err = "not a KTX2 file (identifier)"; return false; }
    const uint32_t vkFormat = rd32(p + 12), typeSize = rd32(p + 16);
    const uint32_t w = rd32(p + 20), h = rd32(p + 24), depth = rd32(p + 28);
    const uint32_t layers = rd32(p + 32), faces = rd32(p + 36), levelCount = rd32(p + 40), scheme = rd32(p + 44);
    const uint32_t dfdOff = rd32(p + 48), dfdLen = rd32(p + 52), kvdOff = rd32(p + 56), kvdLen = rd32(p + 60);
    const uint64_t sgdOff = rd64(p + 64), sgdLen = rd64(p + 72);
    const BlockInfo bi = blockInfo(vkFormat);
    if (bi.bytes == 0)       { err = "unsupported vkFormat " + std::to_string(vkFormat); return false; }
    if (typeSize != 1)       { err = "typeSize must be 1 for block-compressed formats"; return false; }
    if (w == 0 || h == 0)    { err = "zero width or height"; return false; }
    if (w > 32768 || h > 32768) { err = "image is larger than 32768"; return false; }
    if (depth != 0)          { err = "3D textures are not supported"; return false; }
    if (layers > 1)          { err = "texture arrays are not supported"; return false; }
    if (faces != 1)          { err = "cube maps are not supported"; return false; }
    if (scheme != 0)         { err = "supercompression scheme " + std::to_string(scheme) + " is not supported"; return false; }
    if (levelCount == 0)     { err = "levelCount 0 (mips generated at load) is not supported"; return false; }
    uint32_t maxLevels = 1;
    for (uint32_t s = std::max(w, h); s > 1; s >>= 1) ++maxLevels;
    if (levelCount > maxLevels || levelCount > kMaxLevels) { err = "levelCount is larger than the mip chain"; return false; }
    const uint64_t indexEnd = kHeaderBytes + (uint64_t)levelCount * kLevelEntry;
    if (indexEnd > size)     { err = "level index runs past the end of the file"; return false; }
    auto inFile = [&](uint64_t off, uint64_t len) { return off <= size && len <= size - off; };
    if (dfdLen != 0 && (!inFile(dfdOff, dfdLen) || dfdOff < indexEnd)) { err = "DFD is outside the file"; return false; }
    if (kvdLen != 0 && (!inFile(kvdOff, kvdLen) || kvdOff < indexEnd)) { err = "key/value data is outside the file"; return false; }
    if (sgdLen != 0) { err = "supercompression global data is not supported"; return false; }
    (void)sgdOff;

    std::vector<Level> levels(levelCount);
    const uint32_t align = lcm4(bi.bytes);
    for (uint32_t i = 0; i < levelCount; ++i) {
        const uint8_t* e = p + kHeaderBytes + (size_t)i * kLevelEntry;
        const uint64_t off = rd64(e), len = rd64(e + 8), raw = rd64(e + 16);
        const uint32_t lw = std::max(1u, w >> i), lh = std::max(1u, h >> i);
        const uint64_t want = levelBytes(vkFormat, lw, lh);
        if (len != want || raw != want) {
            err = "level " + std::to_string(i) + " has " + std::to_string(len) + " bytes, expected " + std::to_string(want);
            return false;
        }
        if (off < indexEnd || !inFile(off, len)) { err = "level " + std::to_string(i) + " is outside the file"; return false; }
        if (off % align != 0)  { err = "level " + std::to_string(i) + " is not aligned"; return false; }
        levels[i] = { off, len };
    }
    out.vkFormat = vkFormat;
    out.width    = w;
    out.height   = h;
    out.levels   = std::move(levels);
    out.bytes    = std::move(bytes);
    return true;
}

std::vector<uint8_t> write(uint32_t vkFormat, uint32_t w, uint32_t h, const std::vector<std::vector<uint8_t>>& levels) {
    const BlockInfo bi = blockInfo(vkFormat);
    if (bi.bytes == 0 || w == 0 || h == 0 || levels.empty() || levels.size() > kMaxLevels) return {};
    for (size_t i = 0; i < levels.size(); ++i) {
        const uint32_t lw = std::max(1u, w >> i), lh = std::max(1u, h >> i);
        if (levels[i].size() != levelBytes(vkFormat, lw, lh)) return {};
    }
    const uint32_t levelCount = (uint32_t)levels.size();

    // DFD: 전체 크기(4) + 기본 블록(24 + 표본 16 x n).
    const bool     bc5     = vkFormat == VK_FORMAT_BC5_UNORM_BLOCK;
    const bool     astc    = vkFormat >= VK_FORMAT_ASTC_4x4_UNORM_BLOCK && vkFormat <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
    const uint32_t samples = bc5 ? 2u : 1u;
    const uint32_t blockSz = 24 + 16 * samples;
    const uint32_t dfdLen  = 4 + blockSz;
    std::vector<uint8_t> dfd(dfdLen, 0);
    wr32(dfd, 0, dfdLen);
    wr32(dfd, 4, 0);                                    // vendorId 0 (Khronos) · descriptorType 0 (basic)
    wr32(dfd, 8, 2u | (blockSz << 16));                 // versionNumber 2 · descriptorBlockSize
    const uint32_t model = bc5 ? kModelBC5 : astc ? kModelASTC : kModelBC7;
    const uint32_t xfer  = bi.srgb ? 2u : 1u;           // KHR_DF_TRANSFER_SRGB · LINEAR
    wr32(dfd, 12, model | (1u << 8) | (xfer << 16));    // 원색 BT.709, 알파는 곱하지 않음
    wr32(dfd, 16, (bi.w - 1) | ((bi.h - 1) << 8));      // 블록 크기 - 1 (깊이 · 네 번째 0)
    wr32(dfd, 20, bi.bytes);                            // bytesPlane0
    for (uint32_t s = 0; s < samples; ++s) {
        const size_t   at     = 28 + 16 * (size_t)s;
        const uint32_t bits   = bc5 ? 64u : 128u;
        const uint32_t offset = bc5 ? 64u * s : 0u;
        const uint32_t chan   = bc5 ? s : 0u;            // BC5: 0 = 빨강 · 1 = 초록, BC7 · ASTC: 0 = 색 데이터
        wr32(dfd, at, offset | ((bits - 1) << 16) | (chan << 24));
        wr32(dfd, at + 4, 0);
        wr32(dfd, at + 8, 0);
        wr32(dfd, at + 12, 0xFFFFFFFFu);
    }

    const uint64_t dfdOff = kHeaderBytes + (uint64_t)levelCount * kLevelEntry;
    const uint32_t align  = lcm4(bi.bytes);
    // 단계 데이터는 작은 밉부터 놓습니다(규격의 mip 순서).
    std::vector<uint64_t> offs(levelCount);
    uint64_t cursor = dfdOff + dfdLen;
    for (uint32_t i = levelCount; i-- > 0;) {
        cursor  = alignUp(cursor, align);
        offs[i] = cursor;
        cursor += levels[i].size();
    }
    std::vector<uint8_t> b((size_t)cursor, 0);
    std::memcpy(b.data(), kIdentifier, sizeof(kIdentifier));
    wr32(b, 12, vkFormat);
    wr32(b, 16, 1);            // typeSize
    wr32(b, 20, w);
    wr32(b, 24, h);
    wr32(b, 28, 0);            // pixelDepth
    wr32(b, 32, 0);            // layerCount
    wr32(b, 36, 1);            // faceCount
    wr32(b, 40, levelCount);
    wr32(b, 44, 0);            // supercompressionScheme
    wr32(b, 48, (uint32_t)dfdOff);
    wr32(b, 52, dfdLen);
    wr32(b, 56, 0);            // kvd 없음
    wr32(b, 60, 0);
    wr64(b, 64, 0);            // sgd 없음
    wr64(b, 72, 0);
    for (uint32_t i = 0; i < levelCount; ++i) {
        const size_t e = kHeaderBytes + (size_t)i * kLevelEntry;
        wr64(b, e, offs[i]);
        wr64(b, e + 8, levels[i].size());
        wr64(b, e + 16, levels[i].size());
        std::memcpy(b.data() + offs[i], levels[i].data(), levels[i].size());
    }
    std::memcpy(b.data() + dfdOff, dfd.data(), dfdLen);
    return b;
}

} // namespace hktx2
