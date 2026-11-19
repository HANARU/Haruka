#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "HFontAtlas.hpp"
#include "SlateLog.hpp"
#include "../../vfs/Vfs.hpp"
#include "../../core/Utf8.hpp"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

// 주 글꼴을 못 읽을 때 쓰는 시스템 글꼴(예전 그대로).
static const char* kFontFallbacks[] = {
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    nullptr
};

// 대체 글꼴 사슬(글자 체계 T4-1, 2026-11-19): 주 글꼴에 없는 글자를 차례로 찾습니다. 없는 파일은 건너뜁니다. 처음으로 주 글꼴에
// 없는 글자를 만날 때 한 번 읽습니다(시작 시간 · 메모리를 아낌). TTC(글꼴 모음)는 첫 글꼴을 씁니다.
static const char* kDefaultFallbackChain[] = {
    // Windows
    "C:/Windows/Fonts/malgun.ttf",     // 한글 · KS X 1001 한자 · 가나
    "C:/Windows/Fonts/YuGothR.ttc",    // 일본어
    "C:/Windows/Fonts/meiryo.ttc",
    "C:/Windows/Fonts/msyh.ttc",       // 중국어 간체
    "C:/Windows/Fonts/segoeui.ttf",    // 라틴 확장 · 그리스 · 키릴
    "C:/Windows/Fonts/seguisym.ttf",   // 기호
    "C:/Windows/Fonts/seguiemj.ttf",   // 이모지(윤곽선만, 한 색)
    // Android(Quest)
    "/system/fonts/NotoSansCJK-Regular.ttc",
    "/system/fonts/DroidSansFallback.ttf",
    "/system/fonts/Roboto-Regular.ttf",
    // Linux
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    nullptr
};

// 레이아웃이 글자 폭을 재는 아틀라스(폰트를 읽은 마지막 인스턴스).
static const HFontAtlas* g_metricsFont = nullptr;

const HFontAtlas* HFontAtlas::metrics() { return g_metricsFont; }

// 글꼴 파일 바이트에서 stbtt_fontinfo 를 만듭니다(TTC 면 첫 글꼴). 실패하면 nullptr.
static stbtt_fontinfo* makeFontInfo(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return nullptr;
    const int offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
    if (offset < 0) return nullptr;
    auto* fi = new stbtt_fontinfo();
    if (!stbtt_InitFont(fi, bytes.data(), offset)) {
        delete fi;
        return nullptr;
    }
    return fi;
}

// ── 폰트 파일 ─────────────────────────────────────────────────────────────────
bool HFontAtlas::loadFont(const std::string& fontPath) {
    unloadFont();
    // 논리 경로(assets/fonts/...)는 가상 파일 시스템(낱개 파일 · 묶음 파일)에서, 절대 경로(시스템 글꼴)는 그 파일을 직접
    // 읽습니다(2026-10-24).
    auto tryLoad = [&](const std::string& path) -> bool {
        std::vector<uint8_t> bytes;
        if (!hvfs::readAll(path, bytes)) return false;
        ttfData_  = std::move(bytes);
        fontPath_ = path;
        return true;
    };

    bool loaded = false;
    if (!fontPath.empty()) loaded = tryLoad(fontPath);
    for (int i = 0; !loaded && kFontFallbacks[i]; ++i)
        loaded = tryLoad(kFontFallbacks[i]);
    if (!loaded) return false;

    stbtt_fontinfo* fi = makeFontInfo(ttfData_);
    if (!fi) {
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
    stbtt_fontinfo* fi = makeFontInfo(boldData_);
    if (!fi) {
        boldData_.clear();
        return false;
    }
    boldInfo_ = fi;
    fontForCache_.clear();
    // 굵게가 없을 때 면 1 은 면 0 열쇠를 썼으므로 캐시에 남은 값도 면 0 값 그대로 맞습니다(비우지 않음).
    return true;
}

void HFontAtlas::setFallbackChain(std::vector<std::string> paths) {
    clearFallbacks();
    fallbackPaths_    = std::move(paths);
    fallbackCustom_   = true;
}

void HFontAtlas::clearFallbacks() const {
    for (auto& f : fallbacks_) delete static_cast<stbtt_fontinfo*>(f.info);
    fallbacks_.clear();
    fallbacksLoaded_ = false;
    fontForCache_.clear();
}

void HFontAtlas::loadFallbacks() const {
    if (fallbacksLoaded_) return;
    fallbacksLoaded_ = true;
    std::vector<std::string> paths = fallbackPaths_;
    if (!fallbackCustom_)
        for (int i = 0; kDefaultFallbackChain[i]; ++i) paths.push_back(kDefaultFallbackChain[i]);
    for (const std::string& p : paths) {
        Fallback f;
        if (!hvfs::readAll(p, f.data)) continue;
        f.info = makeFontInfo(f.data);
        if (!f.info) continue;
        f.path = p;
        fallbacks_.push_back(std::move(f));
    }
    // 벡터가 다시 할당되면 stbtt_fontinfo 가 가리키는 바이트 주소가 바뀌므로, 다 넣은 뒤 다시 만듭니다.
    for (auto& f : fallbacks_) {
        delete static_cast<stbtt_fontinfo*>(f.info);
        f.info = makeFontInfo(f.data);
    }
}

size_t HFontAtlas::fallbackCount() const {
    loadFallbacks();
    return fallbacks_.size();
}

const void* HFontAtlas::fontFor(uint32_t cp, int face) const {
    const void* primary = faceInfo(face);
    if (!primary) return nullptr;
    // 제어 문자 · 공백류는 주 글꼴(자리만 차지)
    if (cp < 0x20 || cp == 0x7F) return primary;
    const uint64_t key = (uint64_t)((face == 1 && boldInfo_) ? 1u : 0u) << 32 | cp;
    if (auto it = fontForCache_.find(key); it != fontForCache_.end()) return it->second;
    const void* found = nullptr;
    if (stbtt_FindGlyphIndex(static_cast<const stbtt_fontinfo*>(primary), (int)cp)) found = primary;
    // 굵게 면에 없으면 보통 면, 그다음 대체 글꼴 사슬
    if (!found && primary != fontInfo_ && stbtt_FindGlyphIndex(static_cast<const stbtt_fontinfo*>(fontInfo_), (int)cp))
        found = fontInfo_;
    if (!found) {
        loadFallbacks();
        for (const auto& f : fallbacks_)
            if (stbtt_FindGlyphIndex(static_cast<const stbtt_fontinfo*>(f.info), (int)cp)) { found = f.info; break; }
    }
    if (!found) {
        // 어디에도 없으면 주 글꼴의 .notdef(네모)를 그립니다. 경고는 글자마다 한 번, 앞의 몇 개만.
        found = primary;
        if (missingCodepoints_++ < 8)
            HLOG(LogUI, Warning) << "Font: no loaded font has U+" << std::hex << std::uppercase << cp << std::dec
                                 << "; drawing the missing-glyph box";
    }
    fontForCache_.emplace(key, found);
    return found;
}

void HFontAtlas::unloadFont() {
    if (g_metricsFont == this) g_metricsFont = nullptr;
    delete static_cast<stbtt_fontinfo*>(fontInfo_);
    fontInfo_ = nullptr;
    ttfData_.clear();
    fontPath_.clear();
    delete static_cast<stbtt_fontinfo*>(boldInfo_);
    boldInfo_ = nullptr;
    boldData_.clear();
    clearFallbacks();
    missingCodepoints_ = 0;
    cache_.clear();
    iconCache_.clear();
    advanceCache_.clear();
}

// ── 초기화 ────────────────────────────────────────────────────────────────────
bool HFontAtlas::initCpuOnly(const std::string& fontPath) {
    if (!loadFont(fontPath)) return false;
    destroyPageImages();
    pages_.clear();
    addPage();
    dirty_   = false;
    droppedGlyphs_ = 0;
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

    if (pages_[0].image == VK_NULL_HANDLE) createPageImage(pages_[0]);
    uploadPage(pages_[0]);
    HLOG(LogUI, Log) << "Font atlas ready: " << kAtlasW << "x" << atlasH_ << " (" << fontPath_ << ")";
}

void HFontAtlas::destroyPageImages() {
    if (device_ == VK_NULL_HANDLE) return;
    for (Page& p : pages_) {
        if (p.view)  vkDestroyImageView(device_, p.view,  nullptr);
        if (p.image) vkDestroyImage    (device_, p.image, nullptr);
        if (p.mem)   vkFreeMemory      (device_, p.mem,   nullptr);
        p.view  = VK_NULL_HANDLE;
        p.image = VK_NULL_HANDLE;
        p.mem   = VK_NULL_HANDLE;
        p.firstUpload = true;
    }
}

void HFontAtlas::cleanup() {
    destroyPageImages();
    pages_.clear();
    unloadFont();
}

void HFontAtlas::addPage() {
    Page p;
    p.cpu.assign((size_t)kAtlasW * (size_t)atlasH_ * 4, 0);   // RGBA, 흰색 × 알파
    pages_.push_back(std::move(p));
    // GPU 가 있으면 텍스처를 곧바로 만듭니다. 그리기 목록을 펼칠 때(업로드 전) 이미 이 페이지의 뷰가 필요합니다.
    if (device_ != VK_NULL_HANDLE) createPageImage(pages_.back());
}

VkImageView HFontAtlas::pageView(int page) const {
    if (page < 0 || page >= (int)pages_.size()) return pages_.empty() ? VK_NULL_HANDLE : pages_[0].view;
    return pages_[page].view;
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
    auto* fi = static_cast<const stbtt_fontinfo*>(fontFor(cp, face));
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

// UTF-8 읽기 · 자르기는 core/Utf8.hpp 에 있습니다(글자 체계 T1). 예전 이름은 그대로 둡니다.
uint32_t HFontAtlas::nextCodepoint(std::string_view s, size_t& i) {
    return utf8::next(s, i);
}

size_t HFontAtlas::utf8Floor(std::string_view s, size_t maxBytes) {
    return utf8::floorBoundary(s, maxBytes);
}

// ── 내부 헬퍼 ─────────────────────────────────────────────────────────────────
uint64_t HFontAtlas::glyphKey(uint32_t cp, float size, int face) const {
    const uint32_t sizeBits = static_cast<uint32_t>(size * 10.f);
    const uint64_t f        = (face == 1 && boldInfo_) ? 1u : 0u;   // 굵게가 없으면 보통과 같은 열쇠
    return (f << 56) | (static_cast<uint64_t>(cp & 0x1FFFFFu) << 32) | sizeBits;
}

void HFontAtlas::rasterizeGlyph(uint32_t cp, float size, int face, HGlyphInfo& out) {
    auto* fi    = static_cast<stbtt_fontinfo*>(const_cast<void*>(fontFor(cp, face)));
    float scale = stbtt_ScaleForPixelHeight(fi, size);
    const int c = (int)cp;

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(fi, c, scale, scale, &x0, &y0, &x1, &y1);
    int gw = x1 - x0, gh = y1 - y0;

    // 알파 비트맵 래스터라이즈
    std::vector<uint8_t> bmp(gw * gh, 0);
    stbtt_MakeCodepointBitmap(fi, bmp.data(), gw, gh, gw, scale, scale, c);

    if (!packAlpha(bmp.data(), gw, gh, out.uvMin, out.uvMax, out.page)) {
        // 넘친 글자도 캐시에 남지만 글자 · 크기 조합마다 새로 넘치므로, 오류는 처음 한 번만 남기고 수는 셉니다
        // (3D UI 패널처럼 큰 글자 · 여러 크기를 쓰면 같은 오류가 수백 줄 찍혔음).
        if (droppedGlyphs_++ == 0)
            HLOG(LogUI, Error) << "Font atlas full (" << kMaxPages << " pages of " << kAtlasW << "x" << atlasH_ << "): glyph U+"
                               << std::hex << cp << std::dec << " at " << size << " px and any later new glyphs are not drawn";
        out = {};
        out.advance = glyphAdvance(cp, size, face);   // 그리지 못해도 자리는 측정과 같게 차지합니다
        return;
    }
    out.bearing   = { (float)x0, (float)(-y0) };
    out.pixelSize = { (float)gw, (float)gh };
    out.size      = size;
    out.advance   = glyphAdvance(cp, size, face);
}

// 지금 페이지에 자리가 없으면 새 페이지를 엽니다(글자 체계 T4-3, 최대 kMaxPages). 예전에는 한 장이 차면 그 뒤 글자를 그리지
// 않았습니다. 비우고 다시 채우는 방식(UE FSlateFontCache)은 같은 아틀라스를 다른 시점에 쓰는 3D 패널의 그리기 목록이 낡은 자리를
// 가리킬 수 있어 쓰지 않습니다. 글리프마다 자기 페이지 텍스처로 그리므로 렌더러는 그대로입니다.
bool HFontAtlas::packAlpha(const uint8_t* alpha, int w, int h, glm::vec2& uvMin, glm::vec2& uvMax, int& pageOut) {
    if (pages_.empty() || w > kAtlasW - 1 || h > atlasH_ - 1) return false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        Page& p = pages_.back();
        // 행이 꽉 찼으면 다음 행으로
        if (p.cursorX + w + 1 > kAtlasW) {
            p.cursorX  = 0;
            p.cursorY += p.rowH + 1;
            p.rowH     = 0;
        }
        if (p.cursorY + h + 1 > atlasH_) {
            if ((int)pages_.size() >= kMaxPages) return false;
            addPage();
            continue;
        }
        // CPU 아틀라스에 RGBA 픽셀 기록 (R=G=B=255, A=alpha)
        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                const int ai = row * w + col;
                const size_t pi = ((size_t)(p.cursorY + row) * kAtlasW + (size_t)(p.cursorX + col)) * 4;
                p.cpu[pi+0] = 255;
                p.cpu[pi+1] = 255;
                p.cpu[pi+2] = 255;
                p.cpu[pi+3] = alpha[ai];
            }
        }
        uvMin = { (float)p.cursorX / kAtlasW,       (float)p.cursorY / atlasH_ };
        uvMax = { (float)(p.cursorX + w) / kAtlasW, (float)(p.cursorY + h) / atlasH_ };
        pageOut = (int)pages_.size() - 1;

        p.cursorX += w + 1;
        p.rowH     = std::max(p.rowH, h);
        p.dirty    = true;
        dirty_     = true;
        return true;
    }
    return false;
}

bool HFontAtlas::getIcon(std::string_view name, int px, HGlyphInfo& out) {
    if (!iconSource_ || px <= 0 || pages_.empty()) return false;
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
        if (packAlpha(alpha.data(), px, px, info.uvMin, info.uvMax, info.page)) {
            info.pixelSize = { (float)px, (float)px };
            info.size      = (float)px;
            info.advance   = (float)px;
        } else if (droppedGlyphs_++ == 0) {
            HLOG(LogUI, Error) << "Font atlas full (" << kMaxPages << " pages of " << kAtlasW << "x" << atlasH_ << "): icon '"
                               << key << "' and any later new glyphs are not drawn";
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

void HFontAtlas::createPageImage(Page& page) {
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
    vkCreateImage(device_, &ici, nullptr, &page.image);

    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(device_, page.image, &reqs);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = findMemType(reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &ai, nullptr, &page.mem);
    vkBindImageMemory(device_, page.image, page.mem, 0);

    VkImageViewCreateInfo vci{};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = page.image;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(device_, &vci, nullptr, &page.view);
    page.firstUpload = true;
}

void HFontAtlas::uploadPage(Page& page) {
    if (device_ == VK_NULL_HANDLE || page.image == VK_NULL_HANDLE) return;
    VkDeviceSize size = (VkDeviceSize)kAtlasW * (VkDeviceSize)atlasH_ * 4;

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
    memcpy(stagePtr, page.cpu.data(), size);
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
        b.image               = page.image;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0,nullptr,0,nullptr,1,&b);
    };

    VkImageLayout oldLayout = page.firstUpload
        ? VK_IMAGE_LAYOUT_UNDEFINED
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAccessFlags srcAccess = page.firstUpload ? 0 : VK_ACCESS_SHADER_READ_BIT;
    VkPipelineStageFlags srcStage = page.firstUpload
        ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    barrier(oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            srcAccess, VK_ACCESS_TRANSFER_WRITE_BIT,
            srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { (uint32_t)kAtlasW, (uint32_t)atlasH_, 1 };
    vkCmdCopyBufferToImage(cmd, stageBuf, page.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    endCmd_(cmd);

    vkDestroyBuffer(device_, stageBuf, nullptr);
    vkFreeMemory   (device_, stageMem, nullptr);
    page.firstUpload = false;
    page.dirty       = false;
}

void HFontAtlas::upload() {
    if (device_ == VK_NULL_HANDLE) return;
    for (Page& p : pages_) {
        if (!p.dirty) continue;
        if (p.image == VK_NULL_HANDLE) createPageImage(p);
        if (p.firstUpload && &p != pages_.data())
            HLOG(LogUI, Log) << "Font atlas: page " << (&p - pages_.data()) + 1 << " of " << kMaxPages << " opened";
        uploadPage(p);
    }
    dirty_ = false;
}
