#pragma once
#include "HSlateTypes.hpp"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>

// ── HFontAtlas ────────────────────────────────────────────────────────────────
// stb_truetype로 ASCII(32-127) 글리프를 RGBA 아틀라스에 래스터라이즈.
// 여러 폰트 크기를 지원하며, 아틀라스는 단일 2048×512 RGBA 텍스처.
class HFontAtlas {
public:
    // beginCmd/endCmd: Renderer의 one-time command 헬퍼를 재사용
    using CmdBeginFn = std::function<VkCommandBuffer()>;
    using CmdEndFn   = std::function<void(VkCommandBuffer)>;

    void init(VkDevice device, VkPhysicalDevice physDevice,
              CmdBeginFn beginCmd, CmdEndFn endCmd,
              const std::string& fontPath = "");
    void cleanup();

    // 글리프 정보 조회 (없으면 래스터라이즈 → 아틀라스에 추가)
    // 스레드 안전하지 않음 (render thread only)
    HGlyphInfo getGlyph(char c, float size);

    // 텍스트 전체 너비 계산
    float measureText(const std::string& text, float size) const;

    VkImageView atlasView() const { return atlasView_; }

    // 아틀라스에 업데이트가 생기면 true, record 전에 확인
    bool isDirty() const { return dirty_; }
    void upload(); // 더티 패치를 GPU에 업로드
    void clearDirty() { dirty_ = false; }

private:
    static constexpr int kAtlasW = 2048;
    static constexpr int kAtlasH = 512;

    VkDevice         device_     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    CmdBeginFn       beginCmd_;
    CmdEndFn         endCmd_;

    // GPU 아틀라스 텍스처
    VkImage       atlasImage_  = VK_NULL_HANDLE;
    VkDeviceMemory atlasMem_   = VK_NULL_HANDLE;
    VkImageView   atlasView_   = VK_NULL_HANDLE;

    // CPU 사이드 픽셀 버퍼 (RGBA)
    std::vector<uint8_t> cpuAtlas_; // kAtlasW * kAtlasH * 4

    // 아틀라스 패킹 커서 (간단한 행-기반 packing)
    int cursorX_ = 0;
    int cursorY_ = 0;
    int rowH_    = 0;

    // 글리프 캐시: key = (char << 16 | size_bits)
    std::unordered_map<uint64_t, HGlyphInfo> cache_;
    bool dirty_       = false;
    bool firstUpload_ = true;  // UNDEFINED→TRANSFER_DST vs SHADER_READ_ONLY→TRANSFER_DST

    // stb_truetype 데이터
    std::vector<uint8_t> ttfData_;
    void* fontInfo_ = nullptr; // stbtt_fontinfo (opaque)

    uint64_t glyphKey(char c, float size) const;
    void     rasterizeGlyph(char c, float size, HGlyphInfo& out);
    uint32_t findMemType(VkMemoryRequirements reqs, VkMemoryPropertyFlags flags);
    void     createAtlasImage();
    void     uploadFullAtlas();
};
