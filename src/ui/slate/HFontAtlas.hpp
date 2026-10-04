#pragma once
#include "HSlateTypes.hpp"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <string_view>
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

    HFontAtlas() = default;
    // 폰트를 읽은 아틀라스는 레이아웃의 글자 폭 기준으로 등록되므로, 사라질 때 등록을 풉니다.
    // 복사·이동하면 등록된 주소와 실제 객체가 갈라지므로 막습니다.
    ~HFontAtlas() { unloadFont(); }
    HFontAtlas(const HFontAtlas&)            = delete;
    HFontAtlas& operator=(const HFontAtlas&) = delete;

    void init(VkDevice device, VkPhysicalDevice physDevice,
              CmdBeginFn beginCmd, CmdEndFn endCmd,
              const std::string& fontPath = "");
    void cleanup();

    // 폰트 파일만 읽습니다(GPU 없음). init 이 부르고, 화면 없는 테스트가 글자 폭을 잴 때도
    // 씁니다. 읽으면 이 아틀라스가 레이아웃의 글자 폭 기준(metrics)으로 등록됩니다.
    bool loadFont(const std::string& fontPath = "");
    void unloadFont();

    // 레이아웃이 글자 폭을 재는 기준입니다. 위젯의 computeDesiredSize 는 페인트 컨텍스트를
    // 받지 않으므로, 폰트를 읽은 아틀라스가 스스로 등록합니다. 읽은 폰트가 없으면 nullptr
    // 입니다(폰트 없이 도는 테스트). 예전에는 STextBlock 이 "글자 수 x 크기 x 0.6" 으로
    // 자리를 잡아 실제 그려지는 폭과 달랐습니다(DEF-60).
    static const HFontAtlas* metrics();

    // 글리프 정보 조회 (없으면 래스터라이즈 → 아틀라스에 추가)
    // 스레드 안전하지 않음 (render thread only)
    HGlyphInfo getGlyph(char c, float size);

    // 텍스트 폭. 그리기(HSlateRenderer::flush)와 똑같이 글자마다 advance 를 더하므로 그려지는
    // 폭과 일치합니다. 커닝은 그리기가 쓰지 않으므로 여기서도 빼고, 여러 줄이면 가장 긴 줄입니다.
    float measureText(std::string_view text, float size) const;
    // 글자 하나의 advance(px). 그리기와 폭 측정이 이 값 하나를 씁니다.
    float glyphAdvance(char c, float size) const;

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
    // 글자 폭 캐시. 레이아웃이 프레임마다 여러 번 재므로 stb 조회를 반복하지 않습니다.
    mutable std::unordered_map<uint64_t, float> advanceCache_;
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
