#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "HFontAtlas.hpp"
#include "SlateLog.hpp"
#include "../../vfs/Vfs.hpp"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

static const char* kFontFallbacks[] = {
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    nullptr
};

// 레이아웃이 글자 폭을 재는 아틀라스(폰트를 읽은 마지막 인스턴스).
static const HFontAtlas* g_metricsFont = nullptr;

const HFontAtlas* HFontAtlas::metrics() { return g_metricsFont; }

// ── 폰트 파일 ─────────────────────────────────────────────────────────────────
bool HFontAtlas::loadFont(const std::string& fontPath) {
    unloadFont();
    // 논리 경로(assets/fonts/...)는 가상 파일 시스템(낱개 파일 · 묶음 파일)에서, 절대 경로(시스템 글꼴)는 그 파일을 직접
    // 읽습니다(2026-10-24).
    auto tryLoad = [&](const std::string& path) -> bool {
        std::vector<uint8_t> bytes;
        if (!hvfs::readAll(path, bytes)) return false;
        ttfData_ = std::move(bytes);
        return true;
    };

    bool loaded = false;
    if (!fontPath.empty()) loaded = tryLoad(fontPath);
    for (int i = 0; !loaded && kFontFallbacks[i]; ++i)
        loaded = tryLoad(kFontFallbacks[i]);
    if (!loaded) return false;

    auto* fi = new stbtt_fontinfo();
    if (!stbtt_InitFont(fi, ttfData_.data(), 0)) {
        delete fi;
        ttfData_.clear();
        return false;
    }
    fontInfo_     = fi;
    g_metricsFont = this;
    return true;
}

bool HFontAtlas::loadBoldFont(const std::string& fontPath) {
    delete static_cast<stbtt_fontinfo*>(boldInfo_);
    boldInfo_ = nullptr;
    boldData_.clear();
    if (fontPath.empty() || !fontInfo_ || !hvfs::readAll(fontPath, boldData_)) { boldData_.clear(); return false; }
    auto* fi = new stbtt_fontinfo();
    if (!stbtt_InitFont(fi, boldData_.data(), 0)) {
        delete fi;
        boldData_.clear();
        return false;
    }
    boldInfo_ = fi;
    // 굵게가 없을 때 면 1 은 면 0 열쇠를 썼으므로 캐시에 남은 값도 면 0 값 그대로 맞습니다(비우지 않음).
    return true;
}

void HFontAtlas::unloadFont() {
    if (g_metricsFont == this) g_metricsFont = nullptr;
    delete static_cast<stbtt_fontinfo*>(fontInfo_);
    fontInfo_ = nullptr;
    ttfData_.clear();
    delete static_cast<stbtt_fontinfo*>(boldInfo_);
    boldInfo_ = nullptr;
    boldData_.clear();
    cache_.clear();
    iconCache_.clear();
    advanceCache_.clear();
}

// ── 초기화 ────────────────────────────────────────────────────────────────────
bool HFontAtlas::initCpuOnly(const std::string& fontPath) {
    if (!loadFont(fontPath)) return false;
    cursorX_ = 0;
    cursorY_ = 0;
    rowH_    = 0;
    dirty_   = false;
    droppedGlyphs_ = 0;
    // CPU 아틀라스 버퍼 초기화 (RGBA, 불투명 흰색 × 투명)
    cpuAtlas_.assign(kAtlasW * atlasH_ * 4, 0);
    return true;
}

void HFontAtlas::init(VkDevice device, VkPhysicalDevice physDevice,
                      CmdBeginFn beginCmd, CmdEndFn endCmd,
                      const std::string& fontPath)
{
    device_    = device;
    physDevice_ = physDevice;
    beginCmd_  = beginCmd;
    endCmd_    = endCmd;

    if (!initCpuOnly(fontPath))
        throw std::runtime_error("[HFontAtlas] No usable font file found!");

    createAtlasImage();
    uploadFullAtlas();
    HLOG(LogUI, Log) << "Font atlas ready: " << kAtlasW << "x" << atlasH_;
}

void HFontAtlas::cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, atlasView_,  nullptr);
        vkDestroyImage    (device_, atlasImage_, nullptr);
        vkFreeMemory      (device_, atlasMem_,   nullptr);
    }
    unloadFont();
}

// ── 글리프 조회 / 래스터라이즈 ────────────────────────────────────────────────
HGlyphInfo HFontAtlas::getGlyph(uint32_t cp, float size, int face) {
    uint64_t key = glyphKey(cp, size, face);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    HGlyphInfo info;
    rasterizeGlyph(cp, size, face, info);
    cache_[key] = info;
    return info;
}

float HFontAtlas::glyphAdvance(uint32_t cp, float size, int face) const {
    if (!fontInfo_) return 0.f;
    const uint64_t key = glyphKey(cp, size, face);
    if (auto it = advanceCache_.find(key); it != advanceCache_.end()) return it->second;
    auto* fi = static_cast<const stbtt_fontinfo*>(faceInfo(face));
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(fi, (int)cp, &adv, &lsb);
    const float a = adv * stbtt_ScaleForPixelHeight(fi, size);
    advanceCache_.emplace(key, a);
    return a;
}

float HFontAtlas::measureText(std::string_view text, float size, int face) const {
    float x = 0.f, widest = 0.f;
    for (size_t i = 0; i < text.size();) {
        const uint32_t cp = nextCodepoint(text, i);
        if (cp == '\n') { widest = std::max(widest, x); x = 0.f; continue; }
        x += glyphAdvance(cp, size, face);
    }
    return std::max(widest, x);
}

uint32_t HFontAtlas::nextCodepoint(std::string_view s, size_t& i) {
    const unsigned char c0 = (unsigned char)s[i];
    if (c0 < 0x80) { ++i; return c0; }
    size_t   extra = 0;
    uint32_t cp    = 0;
    if      ((c0 & 0xE0) == 0xC0) { extra = 1; cp = c0 & 0x1Fu; }
    else if ((c0 & 0xF0) == 0xE0) { extra = 2; cp = c0 & 0x0Fu; }
    else if ((c0 & 0xF8) == 0xF0) { extra = 3; cp = c0 & 0x07u; }
    else { ++i; return '?'; }
    if (i + extra >= s.size()) { ++i; return '?'; }   // 글자가 끝에서 잘림
    for (size_t k = 1; k <= extra; ++k) {
        const unsigned char ck = (unsigned char)s[i + k];
        if ((ck & 0xC0) != 0x80) { ++i; return '?'; }
        cp = (cp << 6) | (ck & 0x3Fu);
    }
    // 너무 긴 표현 · 대리 쌍 · 범위 밖은 잘못된 글자로 봅니다.
    static constexpr uint32_t kMin[4] = { 0, 0x80, 0x800, 0x10000 };
    if (cp < kMin[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { ++i; return '?'; }
    i += extra + 1;
    return cp;
}

size_t HFontAtlas::utf8Floor(std::string_view s, size_t maxBytes) {
    if (maxBytes >= s.size()) return s.size();
    size_t n = maxBytes;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;   // 이어지는 바이트 앞에서 자르지 않음
    return n;
}

// ── 내부 헬퍼 ─────────────────────────────────────────────────────────────────
uint64_t HFontAtlas::glyphKey(uint32_t cp, float size, int face) const {
    const uint32_t sizeBits = static_cast<uint32_t>(size * 10.f);
    const uint64_t f        = (face == 1 && boldInfo_) ? 1u : 0u;   // 굵게가 없으면 보통과 같은 열쇠
    return (f << 56) | (static_cast<uint64_t>(cp & 0x1FFFFFu) << 32) | sizeBits;
}

void HFontAtlas::rasterizeGlyph(uint32_t cp, float size, int face, HGlyphInfo& out) {
    auto* fi    = static_cast<stbtt_fontinfo*>(const_cast<void*>(faceInfo(face)));
    float scale = stbtt_ScaleForPixelHeight(fi, size);
    const int c = (int)cp;

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(fi, c, scale, scale, &x0, &y0, &x1, &y1);
    int gw = x1 - x0, gh = y1 - y0;

    // 알파 비트맵 래스터라이즈
    std::vector<uint8_t> bmp(gw * gh, 0);
    stbtt_MakeCodepointBitmap(fi, bmp.data(), gw, gh, gw, scale, scale, c);

    if (!packAlpha(bmp.data(), gw, gh, out.uvMin, out.uvMax)) {
        // 넘친 글자도 캐시에 남지만 글자 · 크기 조합마다 새로 넘치므로, 오류는 처음 한 번만 남기고 수는 셉니다
        // (3D UI 패널처럼 큰 글자 · 여러 크기를 쓰면 같은 오류가 수백 줄 찍혔음).
        if (droppedGlyphs_++ == 0)
            HLOG(LogUI, Error) << "Font atlas full (" << kAtlasW << "x" << atlasH_ << "): glyph U+" << std::hex << cp
                               << std::dec << " at " << size << " px and any later new glyphs are not drawn";
        out = {};
        out.advance = glyphAdvance(cp, size, face);   // 그리지 못해도 자리는 측정과 같게 차지합니다
        return;
    }
    out.bearing   = { (float)x0, (float)(-y0) };
    out.pixelSize = { (float)gw, (float)gh };
    out.size      = size;
    out.advance   = glyphAdvance(cp, size, face);
}

bool HFontAtlas::packAlpha(const uint8_t* alpha, int w, int h, glm::vec2& uvMin, glm::vec2& uvMax) {
    // 행이 꽉 찼으면 다음 행으로
    if (cursorX_ + w + 1 > kAtlasW) {
        cursorX_  = 0;
        cursorY_ += rowH_ + 1;
        rowH_     = 0;
    }
    if (cursorY_ + h + 1 > atlasH_) return false;

    // CPU 아틀라스에 RGBA 픽셀 기록 (R=G=B=255, A=alpha)
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            const int ai = row * w + col;
            const int pi = ((cursorY_ + row) * kAtlasW + (cursorX_ + col)) * 4;
            cpuAtlas_[pi+0] = 255;
            cpuAtlas_[pi+1] = 255;
            cpuAtlas_[pi+2] = 255;
            cpuAtlas_[pi+3] = alpha[ai];
        }
    }
    uvMin = { (float)cursorX_ / kAtlasW,       (float)cursorY_ / atlasH_ };
    uvMax = { (float)(cursorX_ + w) / kAtlasW, (float)(cursorY_ + h) / atlasH_ };

    cursorX_ += w + 1;
    rowH_     = std::max(rowH_, h);
    dirty_    = true;
    return true;
}

bool HFontAtlas::getIcon(std::string_view name, int px, HGlyphInfo& out) {
    if (!iconSource_ || px <= 0 || cpuAtlas_.empty()) return false;
    std::string key(name);
    key += '@';
    key += std::to_string(px);
    if (auto it = iconCache_.find(key); it != iconCache_.end()) {
        out = it->second;
        return out.pixelSize.x > 0.f;
    }
    HGlyphInfo info{};
    std::vector<uint8_t> alpha;
    if (iconSource_->rasterize(name, px, alpha) && alpha.size() == (size_t)px * (size_t)px) {
        if (packAlpha(alpha.data(), px, px, info.uvMin, info.uvMax)) {
            info.pixelSize = { (float)px, (float)px };
            info.size      = (float)px;
            info.advance   = (float)px;
        } else if (droppedGlyphs_++ == 0) {
            HLOG(LogUI, Error) << "Font atlas full (" << kAtlasW << "x" << atlasH_ << "): icon '" << key
                               << "' and any later new glyphs are not drawn";
        }
    }
    iconCache_.emplace(std::move(key), info);
    out = info;
    return out.pixelSize.x > 0.f;
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
    ici.extent        = { (uint32_t)kAtlasW, (uint32_t)atlasH_, 1 };
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
    if (device_ == VK_NULL_HANDLE || atlasImage_ == VK_NULL_HANDLE) return;
    VkDeviceSize size = kAtlasW * atlasH_ * 4;

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
    region.imageExtent      = { (uint32_t)kAtlasW, (uint32_t)atlasH_, 1 };
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
    if (device_ == VK_NULL_HANDLE || atlasImage_ == VK_NULL_HANDLE) return;
    uploadFullAtlas();
    dirty_ = false;
}
