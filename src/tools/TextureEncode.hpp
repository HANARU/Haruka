#pragma once
#include "../assets/TextureCompression.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ── 텍스처 인코더 감싸개 (2026-11-21, Docs/Texture_Compression.md D3 · D4 · D7) ───────────────────────────
// 묶음 도구(HarukaPak)와 시험만 씁니다. astc-encoder(ASTC) · bc7enc(BC7) · rgbcx(BC5)를 부르고, 밉은 여기서 CPU 로 만듭니다.
// 게임 런타임에는 들어가지 않습니다.
namespace htexenc {

struct Rgba8 {
    std::vector<uint8_t> px;
    uint32_t w = 0, h = 0;
};

// 밉 사슬(D4): [0] = 입력, 1x1 까지. 2x2 상자 필터, 크기는 Vulkan 규칙(max(1, n >> i)).
// 색은 선형으로 풀어 평균한 뒤 sRGB 로, 노멀은 벡터 평균 뒤 정규화, 선형은 그대로 평균합니다. 알파는 언제나 그대로 평균.
std::vector<Rgba8> buildMips(const uint8_t* rgba, uint32_t w, uint32_t h, htex::Use use);

// 한 단계를 vkFormat 블록으로 압축합니다. out 길이는 hktx2::levelBytes 와 같습니다.
bool compressLevel(const Rgba8& img, uint32_t vkFormat, htex::Use use, std::vector<uint8_t>& out, std::string& err);
// 한 단계를 RGBA8 로 풉니다(시험 · 측정). BC5 는 B = 0 · A = 255 로 채웁니다.
bool decodeLevel(uint32_t vkFormat, const uint8_t* data, size_t len, uint32_t w, uint32_t h, Rgba8& out, std::string& err);

// 입력(최종 RGBA8) → 밉 → 압축 → KTX2 파일 바이트. 실패하면 빈 결과와 이유.
std::vector<uint8_t> encodeKtx2(const uint8_t* rgba, uint32_t w, uint32_t h, htex::Target t, htex::Use u, std::string& err);

// PSNR(dB). channels = 3(RGB) 또는 2(RG, 노멀). 두 영상이 같으면 99.
double psnr(const uint8_t* a, const uint8_t* b, size_t pixels, int channels);

} // namespace htexenc
