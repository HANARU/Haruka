#pragma once
#include "HSlateTypes.hpp"
#include "HIconSource.hpp"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

// ── HFontAtlas ────────────────────────────────────────────────────────────────
// stb_truetype 로 글리프를 RGBA 아틀라스에 래스터라이즈합니다. 글자는 UTF-8 로 받아 코드 포인트로 찾고(D4, 2026-11-19:
// 한글 등), 글꼴은 보통(0) · 굵게(1) 두 벌까지 둡니다(굵게가 없으면 보통). 아틀라스는 2048 × 높이(기본 512) RGBA 텍스처.
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
    // 굵은 글꼴(면 1)을 읽습니다(D4). 없거나 못 읽으면 굵은 글자도 보통 글꼴로 그립니다. loadFont 뒤에 부릅니다.
    bool loadBoldFont(const std::string& fontPath);
    bool hasBoldFont() const { return boldInfo_ != nullptr; }
    // 아틀라스 높이(px). init · initCpuOnly 전에만 바꿉니다(에디터는 한글 때문에 크게, 게임 쪽 3D 패널은 기본).
    void setAtlasHeight(int h) { if (h >= 64 && h <= 8192) atlasH_ = h; }
    int  atlasHeight() const { return atlasH_; }
    // GPU 없이 폰트를 읽고 CPU 아틀라스 버퍼까지 준비합니다(화면 없는 그리기 목록 생성용).
    bool initCpuOnly(const std::string& fontPath = "");
    void unloadFont();

    // 레이아웃이 글자 폭을 재는 기준입니다. 위젯의 computeDesiredSize 는 페인트 컨텍스트를
    // 받지 않으므로, 폰트를 읽은 아틀라스가 스스로 등록합니다. 읽은 폰트가 없으면 nullptr
    // 입니다(폰트 없이 도는 테스트). 예전에는 STextBlock 이 "글자 수 x 크기 x 0.6" 으로
    // 자리를 잡아 실제 그려지는 폭과 달랐습니다(DEF-60).
    static const HFontAtlas* metrics();

    // 글리프 정보 조회 (없으면 래스터라이즈 → 아틀라스에 추가)
    // 스레드 안전하지 않음 (render thread only)
    // cp 는 유니코드 코드 포인트, face 는 0 = 보통 · 1 = 굵게입니다.
    HGlyphInfo getGlyph(uint32_t cp, float size, int face = 0);

    // 텍스트 폭. 그리기(HSlateRenderer::flush)와 똑같이 글자마다 advance 를 더하므로 그려지는
    // 폭과 일치합니다. 커닝은 그리기가 쓰지 않으므로 여기서도 빼고, 여러 줄이면 가장 긴 줄입니다.
    float measureText(std::string_view text, float size, int face = 0) const;
    // 글자 하나의 advance(px). 그리기와 폭 측정이 이 값 하나를 씁니다.
    float glyphAdvance(uint32_t cp, float size, int face = 0) const;

    // UTF-8 에서 코드 포인트 하나를 읽고 i 를 다음 글자로 옮깁니다. 잘못된 바이트는 '?' 하나로 읽습니다(D4).
    static uint32_t nextCodepoint(std::string_view s, size_t& i);
    // 글자 중간을 자르지 않는 앞부분 바이트 수(maxBytes 이하).
    static size_t utf8Floor(std::string_view s, size_t maxBytes);

    VkImageView atlasView() const { return atlasView_; }

    // ── 아이콘(D3, 2026-11-19) ──────────────────────────────────────────────
    // 아이콘 공급자를 붙입니다(에디터). 아이콘은 글리프와 같은 아틀라스에 이름 · 픽셀 크기마다 한 번 래스터화합니다.
    void setIconSource(const HIconSource* src) { iconSource_ = src; }
    bool hasIcon(std::string_view name) const { return iconSource_ && iconSource_->has(name); }
    // 이름 · 크기의 아이콘 자리(uv, 픽셀 크기)를 out 에 씁니다. 공급자가 없거나 모르는 이름 · 아틀라스가 가득 차면 거짓입니다.
    bool getIcon(std::string_view name, int px, HGlyphInfo& out);

    // 아틀라스에 업데이트가 생기면 true, record 전에 확인
    bool isDirty() const { return dirty_; }
    void upload(); // 더티 패치를 GPU에 업로드
    void clearDirty() { dirty_ = false; }
    // 아틀라스가 가득 차 그리지 못한 글자(글자 · 크기 조합) 수. 오류 로그는 처음 하나만 남깁니다.
    uint32_t droppedGlyphs() const { return droppedGlyphs_; }

private:
    static constexpr int kAtlasW = 2048;
    static constexpr int kDefaultAtlasH = 512;
    int atlasH_ = kDefaultAtlasH;

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
    // 아이콘 캐시: key = "<이름>@<px>". 실패(가득 참)도 pixelSize 0 으로 남겨 다시 래스터화하지 않습니다.
    const HIconSource* iconSource_ = nullptr;
    std::unordered_map<std::string, HGlyphInfo> iconCache_;
    // 글자 폭 캐시. 레이아웃이 프레임마다 여러 번 재므로 stb 조회를 반복하지 않습니다.
    mutable std::unordered_map<uint64_t, float> advanceCache_;
    bool dirty_       = false;
    uint32_t droppedGlyphs_ = 0;
    bool firstUpload_ = true;  // UNDEFINED→TRANSFER_DST vs SHADER_READ_ONLY→TRANSFER_DST

    // stb_truetype 데이터(면 0 = 보통, 면 1 = 굵게)
    std::vector<uint8_t> ttfData_;
    void* fontInfo_ = nullptr; // stbtt_fontinfo (opaque)
    std::vector<uint8_t> boldData_;
    void* boldInfo_ = nullptr;

    const void* faceInfo(int face) const { return (face == 1 && boldInfo_) ? boldInfo_ : fontInfo_; }
    uint64_t glyphKey(uint32_t cp, float size, int face) const;
    void     rasterizeGlyph(uint32_t cp, float size, int face, HGlyphInfo& out);
    // w × h 알파 비트맵을 아틀라스 다음 자리에 넣습니다. 자리가 없으면 거짓입니다(글리프 · 아이콘 공용).
    bool     packAlpha(const uint8_t* alpha, int w, int h, glm::vec2& uvMin, glm::vec2& uvMax);
    uint32_t findMemType(VkMemoryRequirements reqs, VkMemoryPropertyFlags flags);
    void     createAtlasImage();
    void     uploadFullAtlas();
};
