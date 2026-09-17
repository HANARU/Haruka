#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "HFontAtlas.hpp"
#include "SlateLog.hpp"
#include <fstream>
#include <stdexcept>
#include <cstring>

static const char* kFontFallbacks[] = {
    "assets/fonts/default.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    nullptr
};

// ── 초기화 ────────────────────────────────────────────────────────────────────
void HFontAtlas::init(VkDevice device, VkPhysicalDevice physDevice,
                      CmdBeginFn beginCmd, CmdEndFn endCmd,
                      const std::string& fontPath)
{
    device_    = device;
    physDevice_ = physDevice;
    beginCmd_  = beginCmd;
    endCmd_    = endCmd;

    // 폰트 파일 로드
    auto tryLoad = [&](const std::string& path) -> bool {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return false;
        size_t sz = f.tellg(); f.seekg(0);
        ttfData_.resize(sz);
        f.read(reinterpret_cast<char*>(ttfData_.data()), sz);
        return true;
    };

    bool loaded = false;
    if (!fontPath.empty()) loaded = tryLoad(fontPath);
    for (int i = 0; !loaded && kFontFallbacks[i]; ++i)
        loaded = tryLoad(kFontFallbacks[i]);

    if (!loaded) throw std::runtime_error("[HFontAtlas] No font file found!");

    fontInfo_ = new stbtt_fontinfo();
    if (!stbtt_InitFont(static_cast<stbtt_fontinfo*>(fontInfo_),
                        ttfData_.data(), 0))
        throw std::runtime_error("[HFontAtlas] stbtt_InitFont failed!");

    // CPU 아틀라스 버퍼 초기화 (RGBA, 불투명 흰색 × 투명)
    cpuAtlas_.assign(kAtlasW * kAtlasH * 4, 0);

    createAtlasImage();
    uploadFullAtlas();
    HLOG(LogUI, Log) << "Font atlas ready: " << kAtlasW << "x" << kAtlasH;
}

void HFontAtlas::cleanup() {
    vkDestroyImageView(device_, atlasView_,  nullptr);
    vkDestroyImage    (device_, atlasImage_, nullptr);
    vkFreeMemory      (device_, atlasMem_,   nullptr);
    delete static_cast<stbtt_fontinfo*>(fontInfo_);
}

// ── 글리프 조회 / 래스터라이즈 ────────────────────────────────────────────────
HGlyphInfo HFontAtlas::getGlyph(char c, float size) {
    uint64_t key = glyphKey(c, size);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    HGlyphInfo info;
    rasterizeGlyph(c, size, info);
    cache_[key] = info;
    return info;
}

float HFontAtlas::measureText(const std::string& text, float size) const {
    auto* fi = static_cast<const stbtt_fontinfo*>(fontInfo_);
    float scale = stbtt_ScaleForPixelHeight(fi, size);
    float x = 0.f;
    for (size_t i = 0; i < text.size(); ++i) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(fi, text[i], &adv, &lsb);
        x += adv * scale;
        if (i + 1 < text.size())
            x += stbtt_GetCodepointKernAdvance(fi, text[i], text[i+1]) * scale;
    }
    return x;
}

// ── 내부 헬퍼 ─────────────────────────────────────────────────────────────────
uint64_t HFontAtlas::glyphKey(char c, float size) const {
    uint32_t sizeBits = static_cast<uint32_t>(size * 10.f);
    return (static_cast<uint64_t>(static_cast<uint8_t>(c)) << 32) | sizeBits;
}

void HFontAtlas::rasterizeGlyph(char c, float size, HGlyphInfo& out) {
    auto* fi    = static_cast<stbtt_fontinfo*>(fontInfo_);
    float scale = stbtt_ScaleForPixelHeight(fi, size);

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(fi, c, scale, scale, &x0, &y0, &x1, &y1);
    int gw = x1 - x0, gh = y1 - y0;

    // 행이 꽉 찼으면 다음 행으로
    if (cursorX_ + gw + 1 > kAtlasW) {
        cursorX_  = 0;
        cursorY_ += rowH_ + 1;
        rowH_     = 0;
    }
    if (cursorY_ + gh + 1 > kAtlasH) {
        HLOG(LogUI, Error) << "Font atlas full";
        out = {};
        return;
    }

    // 알파 비트맵 래스터라이즈
    std::vector<uint8_t> bmp(gw * gh, 0);
    stbtt_MakeCodepointBitmap(fi, bmp.data(), gw, gh, gw, scale, scale, c);

    // CPU 아틀라스에 RGBA 픽셀 기록 (R=G=B=255, A=glyph alpha)
    for (int row = 0; row < gh; ++row) {
        for (int col = 0; col < gw; ++col) {
            int ai = row * gw + col;
            int pi = ((cursorY_ + row) * kAtlasW + (cursorX_ + col)) * 4;
            cpuAtlas_[pi+0] = 255;
            cpuAtlas_[pi+1] = 255;
            cpuAtlas_[pi+2] = 255;
            cpuAtlas_[pi+3] = bmp[ai];
        }
    }

    // UV 좌표
    out.uvMin     = { (float)cursorX_ / kAtlasW,        (float)cursorY_ / kAtlasH };
    out.uvMax     = { (float)(cursorX_+gw) / kAtlasW,   (float)(cursorY_+gh) / kAtlasH };
    out.bearing   = { (float)x0, (float)(-y0) };
    out.pixelSize = { (float)gw, (float)gh };
    out.size      = size;

    int adv, lsb;
    stbtt_GetCodepointHMetrics(fi, c, &adv, &lsb);
    out.advance = adv * scale;

    cursorX_ += gw + 1;
    rowH_     = std::max(rowH_, gh);
    dirty_    = true;
}

// ── GPU 아틀라스 텍스처 생성 ──────────────────────────────────────────────────
uint32_t HFontAtlas::findMemType(VkMemoryRequirements reqs, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physDevice_, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((reqs.memoryTypeBits >> i & 1) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("[HFontAtlas] No suitable memory type!");
}

void HFontAtlas::createAtlasImage() {
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent        = { kAtlasW, kAtlasH, 1 };
    ici.mipLevels     = 1; ici.arrayLayers = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(device_, &ici, nullptr, &atlasImage_);

    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(device_, atlasImage_, &reqs);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = findMemType(reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &ai, nullptr, &atlasMem_);
    vkBindImageMemory(device_, atlasImage_, atlasMem_, 0);

    VkImageViewCreateInfo vci{};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = atlasImage_;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(device_, &vci, nullptr, &atlasView_);
}

void HFontAtlas::uploadFullAtlas() {
    VkDeviceSize size = kAtlasW * kAtlasH * 4;

    // Staging buffer
    VkBuffer       stageBuf; VkDeviceMemory stageMem; void* stagePtr;
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device_, &bi, nullptr, &stageBuf);
    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device_, stageBuf, &reqs);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = findMemType(reqs,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &ai, nullptr, &stageMem);
    vkBindBufferMemory(device_, stageBuf, stageMem, 0);
    vkMapMemory(device_, stageMem, 0, VK_WHOLE_SIZE, 0, &stagePtr);
    memcpy(stagePtr, cpuAtlas_.data(), size);
    vkUnmapMemory(device_, stageMem);

    // One-time command: UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY
    auto cmd = beginCmd_();

    auto barrier = [&](VkImageLayout oldL, VkImageLayout newL,
                       VkAccessFlags src, VkAccessFlags dst,
                       VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = oldL; b.newLayout     = newL;
        b.srcAccessMask       = src;  b.dstAccessMask = dst;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = atlasImage_;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0,nullptr,0,nullptr,1,&b);
    };

    VkImageLayout oldLayout = firstUpload_
        ? VK_IMAGE_LAYOUT_UNDEFINED
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAccessFlags srcAccess = firstUpload_ ? 0 : VK_ACCESS_SHADER_READ_BIT;
    VkPipelineStageFlags srcStage = firstUpload_
        ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    barrier(oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            srcAccess, VK_ACCESS_TRANSFER_WRITE_BIT,
            srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { kAtlasW, kAtlasH, 1 };
    vkCmdCopyBufferToImage(cmd, stageBuf, atlasImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    endCmd_(cmd);

    vkDestroyBuffer(device_, stageBuf, nullptr);
    vkFreeMemory   (device_, stageMem, nullptr);
    firstUpload_ = false;
}

void HFontAtlas::upload() {
    uploadFullAtlas();
    dirty_ = false;
}
