// 텍스처 압축 시험(2026-11-21, 정본 Docs/Texture_Compression.md 3절).
// KTX2 쓰기 · 읽기(정상 · 손상), 형식 표 · 블록 크기, 장치 지원 판단(대체 경로), 밉 생성, 압축 → 풀기 PSNR 하한, 쿠킹 캐시,
// 시험 모델(TexProbe.glb)을 쿠킹한 뒤 런타임 찾기(attachCompressed)가 모두 붙는지(열쇠 일치)를 창 · GPU 없이 검산합니다.

#include "../src/assets/GltfLoader.hpp"
#include "../src/assets/TextureCompression.hpp"
#include "../src/geometry/Ktx2.hpp"
#include "../src/renderer/TextureUpload.hpp"
#include "../src/tools/TextureCook.hpp"
#include "../src/tools/TextureEncode.hpp"
#include "../src/vfs/Vfs.hpp"

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed;
    else ++g_failed;
}

uint32_t rd32(const std::vector<uint8_t>& b, size_t at) { uint32_t v; std::memcpy(&v, b.data() + at, 4); return v; }
void     wr32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
void     wr64(std::vector<uint8_t>& b, size_t at, uint64_t v) { std::memcpy(b.data() + at, &v, 8); }

// 시험 영상: 색 기울기 + 바둑판 + 고주파 줄무늬(블록 경계가 드러나는 내용).
std::vector<uint8_t> testImage(uint32_t w, uint32_t h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &px[((size_t)y * w + x) * 4];
            const bool chk = ((x / 8) + (y / 8)) % 2 == 0;
            p[0] = (uint8_t)(x * 255 / (w - 1));
            p[1] = (uint8_t)(y * 255 / (h - 1));
            p[2] = (uint8_t)(chk ? 200 : 40);
            if (y > h * 3 / 4) p[2] = (uint8_t)((x % 2) ? 230 : 20);
            p[3] = 255;
        }
    return px;
}

// 시험 노멀 맵: 둥근 돌기 하나 + 고주파 물결(접공간, Z 양수).
std::vector<uint8_t> testNormal(uint32_t n) {
    std::vector<uint8_t> px((size_t)n * n * 4);
    for (uint32_t y = 0; y < n; ++y)
        for (uint32_t x = 0; x < n; ++x) {
            const float u = (x + 0.5f) / n * 2.f - 1.f, v = (y + 0.5f) / n * 2.f - 1.f;
            float nx = u * 0.6f, ny = v * 0.6f;
            const float len2 = nx * nx + ny * ny;
            if (len2 > 0.8f) { nx = 0.f; ny = 0.f; }
            nx += 0.15f * std::sin(x * 1.3f);   // 고주파 물결(블록 안에서 값이 많이 바뀜)
            ny += 0.15f * std::cos(y * 0.9f + x * 0.4f);
            const float nz = std::sqrt(std::max(0.f, 1.f - nx * nx - ny * ny));
            uint8_t* p = &px[((size_t)y * n + x) * 4];
            p[0] = (uint8_t)std::lround((nx * 0.5f + 0.5f) * 255.f);
            p[1] = (uint8_t)std::lround((ny * 0.5f + 0.5f) * 255.f);
            p[2] = (uint8_t)std::lround((nz * 0.5f + 0.5f) * 255.f);
            p[3] = 255;
        }
    return px;
}

// 압축 → 풀기 0단계 PSNR.
double roundTripPsnr(const std::vector<uint8_t>& img, uint32_t w, uint32_t h, htex::Target t, htex::Use u, int channels,
                     htexenc::Rgba8* decodedOut = nullptr) {
    std::string err;
    std::vector<uint8_t> file = htexenc::encodeKtx2(img.data(), w, h, t, u, err);
    hktx2::Image k;
    if (file.empty() || !hktx2::parse(std::move(file), k, err)) { std::printf("    (%s)\n", err.c_str()); return 0.0; }
    htexenc::Rgba8 d;
    if (!htexenc::decodeLevel(k.vkFormat, k.levelData(0), (size_t)k.levels[0].length, w, h, d, err)) {
        std::printf("    (%s)\n", err.c_str());
        return 0.0;
    }
    if (decodedOut) *decodedOut = d;
    return htexenc::psnr(img.data(), d.px.data(), (size_t)w * h, channels);
}

std::vector<std::vector<uint8_t>> fakeLevels(uint32_t f, uint32_t w, uint32_t h, uint32_t count) {
    std::vector<std::vector<uint8_t>> lv(count);
    for (uint32_t i = 0; i < count; ++i) {
        lv[i].resize((size_t)hktx2::levelBytes(f, std::max(1u, w >> i), std::max(1u, h >> i)));
        for (size_t j = 0; j < lv[i].size(); ++j) lv[i][j] = (uint8_t)(i * 31 + j);
    }
    return lv;
}

// 손상 파일을 거부하는지 보고, 이유를 메시지에 붙입니다(이유는 parse 를 부른 뒤에 읽음).
bool parses(std::vector<uint8_t> b, std::string* why = nullptr);
void rejects(std::vector<uint8_t> b, const std::string& what) {
    std::string why;
    const bool rejected = !parses(std::move(b), &why);
    check(rejected, what + "(" + why + ")");
}

bool parses(std::vector<uint8_t> b, std::string* why) {
    hktx2::Image img;
    std::string err;
    const bool ok = hktx2::parse(std::move(b), img, err);
    if (why) *why = err;
    return ok;
}

} // namespace

int main() {
    const fs::path tmp = fs::temp_directory_path() / "haruka_tex_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    // ── 1. KTX2 쓰기 · 읽기 ─────────────────────────────────────────────────────────
    std::printf("== 1. KTX2 쓰기 · 읽기 ==\n");
    const uint32_t fBc7 = VK_FORMAT_BC7_SRGB_BLOCK;
    const std::vector<std::vector<uint8_t>> lv = fakeLevels(fBc7, 13, 7, 4);
    const std::vector<uint8_t> file = hktx2::write(fBc7, 13, 7, lv);
    check(!file.empty(), "13x7 BC7 4단계 파일을 씀");
    {
        hktx2::Image img;
        std::string err;
        std::vector<uint8_t> copy = file;
        const bool ok = hktx2::parse(std::move(copy), img, err);
        check(ok && img.vkFormat == fBc7 && img.width == 13 && img.height == 7 && img.levels.size() == 4, "다시 읽으면 형식 · 크기 · 단계 수가 같음");
        bool same = ok;
        bool aligned = ok;
        for (size_t i = 0; ok && i < img.levels.size(); ++i) {
            same = same && img.levels[i].length == lv[i].size() && std::memcmp(img.levelData(i), lv[i].data(), lv[i].size()) == 0;
            aligned = aligned && img.levels[i].offset % 16 == 0;
        }
        check(same, "단계 데이터가 그대로");
        check(aligned, "단계 오프셋은 16바이트 정렬");
        check(ok && img.levels[3].offset < img.levels[0].offset, "파일 안에서는 작은 밉이 앞(규격 순서)");
        check(rd32(file, 48) == 80 + 4 * 24 && rd32(file, 52) > 0, "DFD 는 단계 색인 바로 뒤");
    }
    check(hktx2::write(fBc7, 13, 7, fakeLevels(fBc7, 13, 7, 1)).size() > 80, "1단계 파일도 씀");
    {
        auto bad = lv;
        bad[1].pop_back();
        check(hktx2::write(fBc7, 13, 7, bad).empty(), "단계 길이가 틀리면 쓰지 않음");
    }
    {
        const uint32_t fBc5 = VK_FORMAT_BC5_UNORM_BLOCK;
        const std::vector<uint8_t> b5 = hktx2::write(fBc5, 8, 8, fakeLevels(fBc5, 8, 8, 4));
        check(!b5.empty() && rd32(b5, 52) == 4 + 24 + 32, "BC5 DFD 는 표본 둘(빨강 · 초록)");
        check(!b5.empty() && (rd32(b5, 80 + 4 * 24 + 12) & 0xFF) == 132, "BC5 DFD 색 모델 132(KHR_DF_MODEL_BC5)");
        const uint32_t fA = VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        const std::vector<uint8_t> ba = hktx2::write(fA, 20, 10, fakeLevels(fA, 20, 10, 5));
        check(!ba.empty() && (rd32(ba, 80 + 5 * 24 + 12) & 0xFF) == 162 && ((rd32(ba, 80 + 5 * 24 + 12) >> 16) & 0xFF) == 2,
              "ASTC sRGB DFD: 색 모델 162 · 전달 함수 sRGB(2)");
        check(!ba.empty() && (rd32(ba, 80 + 5 * 24 + 16) & 0xFFFF) == 0x0505, "ASTC 6x6 DFD 블록 크기 5 · 5(크기 - 1)");
    }

    // ── 2. 손상 파일 ─────────────────────────────────────────────────────────────
    std::printf("\n== 2. 손상 파일 ==\n");
    {
        auto b = file; b[1] = 'X';
        rejects(b, "식별자가 다르면 거부");
        rejects(std::vector<uint8_t>(file.begin(), file.begin() + 79), "머리보다 짧으면 거부");
        rejects(std::vector<uint8_t>(file.begin(), file.end() - 1), "마지막 단계가 잘리면 거부");
        b = file; wr32(b, 44, 2);
        rejects(b, "supercompression(zstd = 2)은 거부");
        b = file; wr32(b, 36, 6);
        rejects(b, "큐브 맵(면 6)은 거부");
        b = file; wr32(b, 40, 9);
        rejects(b, "단계 수가 밉 사슬보다 많으면 거부");
        b = file; wr32(b, 40, 0);
        rejects(b, "단계 수 0(읽을 때 밉 생성)은 거부");
        b = file; wr32(b, 12, VK_FORMAT_R8G8B8A8_SRGB);
        rejects(b, "블록 압축이 아닌 형식은 거부");
        b = file; wr64(b, 80 + 8, 32);
        rejects(b, "단계 길이가 형식 · 크기와 다르면 거부");
        b = file; wr64(b, 80, (uint64_t)file.size());
        rejects(b, "단계가 파일 밖이면 거부");
        b = file; wr64(b, 80, rd32(file, 80) + 4);
        rejects(b, "단계 오프셋이 정렬되지 않으면 거부");
        b = file; wr32(b, 52, 0xFFFFFF);
        rejects(b, "DFD 가 파일 밖이면 거부");
        b = file; wr32(b, 20, 0);
        rejects(b, "너비 0 은 거부");
        b = file; wr64(b, 72, 16);
        rejects(b, "supercompression 전역 데이터가 있으면 거부");
    }

    // ── 3. 형식 표 · 블록 크기 ───────────────────────────────────────────────────────
    std::printf("\n== 3. 형식 표 ==\n");
    using htex::Target; using htex::Use;
    check(htex::formatFor(Target::BC, Use::Color) == VK_FORMAT_BC7_SRGB_BLOCK &&
          htex::formatFor(Target::BC, Use::Normal) == VK_FORMAT_BC5_UNORM_BLOCK &&
          htex::formatFor(Target::BC, Use::Linear) == VK_FORMAT_BC7_UNORM_BLOCK, "BC: 색 BC7 sRGB · 노멀 BC5 · 선형 BC7");
    check(htex::formatFor(Target::ASTC, Use::Color) == VK_FORMAT_ASTC_6x6_SRGB_BLOCK &&
          htex::formatFor(Target::ASTC, Use::Normal) == VK_FORMAT_ASTC_5x5_UNORM_BLOCK &&
          htex::formatFor(Target::ASTC, Use::Linear) == VK_FORMAT_ASTC_6x6_UNORM_BLOCK, "ASTC: 색 6x6 sRGB · 노멀 5x5 · 선형 6x6");
    check(htex::formatFor(Target::Off, Use::Color) == 0, "Off 는 형식 없음");
    check(htex::slotUse(htex::kAlbedo) == Use::Color && htex::slotUse(htex::kEmissive) == Use::Color &&
          htex::slotUse(htex::kNormal) == Use::Normal && htex::slotUse(htex::kMetalRough) == Use::Linear &&
          htex::slotUse(htex::kAo) == Use::Linear, "칸 용도: albedo · emissive = 색, normal = 노멀, metalRough · ao = 선형");
    check(hktx2::levelBytes(VK_FORMAT_ASTC_6x6_SRGB_BLOCK, 512, 512) == 86ull * 86 * 16, "ASTC 6x6 512² = 86 x 86 블록");
    check(hktx2::levelBytes(VK_FORMAT_BC7_SRGB_BLOCK, 1, 1) == 16 && hktx2::levelBytes(VK_FORMAT_BC5_UNORM_BLOCK, 5, 9) == 2 * 3 * 16,
          "BC 1x1 = 블록 하나, 5x9 = 2 x 3 블록");
    check(!htex::eligible(1, 1) && !htex::eligible(3, 64) && htex::eligible(4, 4), "한 변이 4보다 작으면 압축하지 않음");
    Target parsed = Target::Off;
    check(htex::parseTarget("ASTC", parsed) && parsed == Target::ASTC && !htex::parseTarget("etc2", parsed), "대상 이름 읽기(대소문자 무시)");
    check(htex::ktx2Path(Target::BC, 0x0123456789abcdefull) == "textures/bc/0123456789abcdef.ktx2", "논리 경로 textures/<대상>/<열쇠 16자리>.ktx2");
    {
        std::vector<uint8_t> a = testImage(16, 16), b = a;
        b[100] ^= 1;
        const uint64_t k = htex::textureKey(a.data(), 16, 16, Use::Color);
        check(k == htex::textureKey(a.data(), 16, 16, Use::Color), "같은 화소는 같은 열쇠");
        check(k != htex::textureKey(b.data(), 16, 16, Use::Color), "한 바이트만 달라도 열쇠가 다름");
        check(k != htex::textureKey(a.data(), 16, 16, Use::Linear), "용도가 다르면 열쇠가 다름(같은 이미지를 색 · 선형으로 쓸 때)");
        check(k != htex::textureKey(a.data(), 8, 32, Use::Color), "크기가 다르면 열쇠가 다름");
    }

    // ── 4. 장치 지원 판단(대체 경로) ──────────────────────────────────────────────────
    std::printf("\n== 4. 장치 지원 · 대체 ==\n");
    const VkFormatFeatureFlags full = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                      VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    check(htexup::canSample(VK_FORMAT_BC7_SRGB_BLOCK, true, false, full), "BC 기능 켜짐 + 형식 지원 → 압축 경로");
    check(!htexup::canSample(VK_FORMAT_BC7_SRGB_BLOCK, false, true, full), "BC 기능이 꺼졌으면 → RGBA8 대체");
    check(!htexup::canSample(VK_FORMAT_ASTC_6x6_SRGB_BLOCK, true, false, full), "ASTC 기능이 없으면(데스크톱 NVIDIA) → RGBA8 대체");
    check(htexup::canSample(VK_FORMAT_ASTC_5x5_UNORM_BLOCK, false, true, full), "ASTC 기능 켜짐(Quest) → 압축 경로");
    check(!htexup::canSample(VK_FORMAT_BC5_UNORM_BLOCK, true, true, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT), "선형 필터가 없으면 → RGBA8 대체");
    check(!htexup::canSample(VK_FORMAT_R8G8B8A8_SRGB, true, true, full), "블록 압축이 아닌 형식은 압축 경로가 아님");
    check(htexup::rgbaChainBytes(4, 2, 3) == (8 + 2 + 1) * 4ull, "RGBA8 밉 사슬 바이트(4x2 → 2x1 → 1x1)");

    // ── 5. 밉 생성 ──────────────────────────────────────────────────────────────────
    std::printf("\n== 5. 밉 ==\n");
    {
        const std::vector<uint8_t> img = testImage(13, 7);
        const auto mips = htexenc::buildMips(img.data(), 13, 7, Use::Color);
        check(mips.size() == 4 && mips[1].w == 6 && mips[1].h == 3 && mips[2].w == 3 && mips[2].h == 1 && mips[3].w == 1 && mips[3].h == 1,
              "13x7 → 6x3 → 3x1 → 1x1(Vulkan 크기 규칙)");
        std::vector<uint8_t> bw = { 0, 0, 0, 255,   255, 255, 255, 255,   255, 255, 255, 255,   0, 0, 0, 255 };
        const auto c = htexenc::buildMips(bw.data(), 2, 2, Use::Color);
        check(c.size() == 2 && c[1].px[0] == 188 && c[1].px[3] == 255, "색 밉은 선형 평균 뒤 sRGB(검정 · 흰색 반반 → 188)");
        const auto l = htexenc::buildMips(bw.data(), 2, 2, Use::Linear);
        check(l.size() == 2 && l[1].px[0] == 128, "선형 밉은 그대로 평균(→ 128)");
        // 노멀: +X 로 기운 것과 -X 로 기운 것의 평균 → 위(0, 0, 1).
        std::vector<uint8_t> nm = { 218, 128, 218, 255,   38, 128, 218, 255,   218, 128, 218, 255,   38, 128, 218, 255 };
        const auto n = htexenc::buildMips(nm.data(), 2, 2, Use::Normal);
        check(n.size() == 2 && std::abs((int)n[1].px[0] - 128) <= 1 && n[1].px[2] == 255, "노멀 밉은 벡터 평균 뒤 정규화(→ 위를 봄)");
    }

    // ── 6. 압축 → 풀기 PSNR ──────────────────────────────────────────────────────────
    std::printf("\n== 6. PSNR ==\n");
    {
        const std::vector<uint8_t> img = testImage(64, 64);
        const std::vector<uint8_t> nrm = testNormal(64);
        const double bc7  = roundTripPsnr(img, 64, 64, Target::BC, Use::Color, 3);
        const double bc7l = roundTripPsnr(img, 64, 64, Target::BC, Use::Linear, 3);
        htexenc::Rgba8 d5, dA;
        const double bc5  = roundTripPsnr(nrm, 64, 64, Target::BC, Use::Normal, 2, &d5);
        const double a6   = roundTripPsnr(img, 64, 64, Target::ASTC, Use::Color, 3);
        const double a6l  = roundTripPsnr(img, 64, 64, Target::ASTC, Use::Linear, 3);
        const double a5   = roundTripPsnr(nrm, 64, 64, Target::ASTC, Use::Normal, 2, &dA);
        std::printf("    BC7 색 %.2f dB, BC7 선형 %.2f dB, BC5 노멀 %.2f dB, ASTC 6x6 색 %.2f dB, ASTC 6x6 선형 %.2f dB, ASTC 5x5 노멀 %.2f dB\n",
                    bc7, bc7l, bc5, a6, a6l, a5);
        check(bc7 >= 38.0, "BC7 색 PSNR ≥ 38 dB");
        check(bc7l >= 38.0, "BC7 선형 PSNR ≥ 38 dB");
        check(bc5 >= 42.0, "BC5 노멀(RG) PSNR ≥ 42 dB");
        check(a6 >= 30.0, "ASTC 6x6 색 PSNR ≥ 30 dB");
        check(a6l >= 30.0, "ASTC 6x6 선형 PSNR ≥ 30 dB");
        check(a5 >= 36.0, "ASTC 5x5 노멀(RG) PSNR ≥ 36 dB");
        bool b0 = !d5.px.empty() && !dA.px.empty();
        for (size_t i = 0; b0 && i < d5.px.size(); i += 4) b0 = d5.px[i + 2] == 0 && dA.px[i + 2] == 0;
        check(b0, "두 채널 노멀은 풀면 B = 0(셰이더가 Z 를 복원하는 표시)");
        // 1x1 끝 단계까지 모두 풀림(블록 하나, 가장자리 반복).
        std::string err;
        std::vector<uint8_t> f = htexenc::encodeKtx2(img.data(), 64, 64, Target::ASTC, Use::Color, err);
        hktx2::Image k;
        bool all = !f.empty() && hktx2::parse(std::move(f), k, err) && k.levels.size() == 7;
        for (size_t i = 0; all && i < k.levels.size(); ++i) {
            htexenc::Rgba8 d;
            all = htexenc::decodeLevel(k.vkFormat, k.levelData(i), (size_t)k.levels[i].length,
                                       std::max(1u, 64u >> i), std::max(1u, 64u >> i), d, err);
        }
        check(all, "ASTC 64² 7단계(1x1 까지) 모두 풀림");
    }

    // ── 7. 쿠킹 캐시 ─────────────────────────────────────────────────────────────────
    std::printf("\n== 7. 캐시 ==\n");
    {
        const fs::path cache = tmp / "cache";
        const std::vector<uint8_t> img = testImage(32, 32);
        std::vector<uint8_t> a, b, c;
        bool fromA = true, fromB = false, fromC = true;
        std::string err;
        const bool okA = htexcook::cookTexture(img.data(), 32, 32, Use::Color, Target::BC, cache, a, fromA, err);
        const bool okB = htexcook::cookTexture(img.data(), 32, 32, Use::Color, Target::BC, cache, b, fromB, err);
        check(okA && !fromA, "처음에는 압축해 캐시에 씀");
        check(okB && fromB && a == b, "두 번째는 캐시에서 같은 바이트를 읽음(다시 압축하지 않음)");
        const fs::path f = cache / fs::path(htex::ktx2Path(Target::BC, htex::textureKey(img.data(), 32, 32, Use::Color)));
        check(fs::exists(f), "캐시 파일은 <캐시>/textures/bc/<열쇠>.ktx2");
        { std::ofstream o(f, std::ios::binary | std::ios::trunc); o << "broken"; }
        const bool okC = htexcook::cookTexture(img.data(), 32, 32, Use::Color, Target::BC, cache, c, fromC, err);
        check(okC && !fromC && c == a, "손상된 캐시 파일은 다시 만듦");
    }

    // ── 8. 시험 모델 쿠킹 → 런타임 찾기(열쇠 일치) ─────────────────────────────────────────
    std::printf("\n== 8. TexProbe 쿠킹 → 찾기 ==\n");
    {
        hvfs::reset();
        hvfs::mountDirectoryAs(fs::path(HARUKA_SOURCE_DIR) / "Projects" / "Samples" / "Content", "/Game/", 0);
        const std::string model = "/Game/models/TexProbe.glb";
        const fs::path cooked = tmp / "cooked";
        htexcook::Stats st;
        std::vector<htexcook::Output> outs;
        std::vector<std::string> seen;
        std::string err;
        const bool ok = htexcook::cookModel(model, ImportSettings{}, Target::BC, cooked, outs, seen, st, err);
        std::printf("    %d textures, RGBA8 %.2f MB -> %.2f MB, %.2f s\n", st.textures, st.rgbaBytes / 1048576.0,
                    st.ktxBytes / 1048576.0, st.seconds);
        check(ok && st.models == 1 && st.textures == 4 && outs.size() == 4,
              "TexProbe: 서로 다른 텍스처 넷(albedo · normal · metalRough = ao · emissive)");
        check(st.rgbaBytes > 0 && (double)st.ktxBytes / (double)st.rgbaBytes < 0.26, "BC7 · BC5(1 바이트/화소)는 RGBA8 의 약 1/4(작은 밉은 블록 하나)");

        hvfs::mountDirectory(cooked, "textures/", 1);
        std::vector<MeshSourceData> meshes = GltfLoader::loadAll(model, ImportSettings{}, nullptr);
        const htex::AttachStats bc = htex::attachCompressed(meshes, Target::BC);
        check(meshes.size() == 1 && bc.found == 5 && bc.missing == 0 && bc.invalid == 0,
              "런타임 찾기: 다섯 칸 모두 붙음(같은 로더 · 같은 열쇠)");
        check(meshes.size() == 1 && meshes[0].normalMap.compressed &&
              meshes[0].normalMap.compressed->vkFormat == VK_FORMAT_BC5_UNORM_BLOCK &&
              meshes[0].albedo.compressed->levels.size() == 10, "노멀은 BC5, albedo 512² 는 10단계");
        const htex::AttachStats astc = htex::attachCompressed(meshes, Target::ASTC);
        check(astc.found == 0 && astc.missing == 5 && !meshes[0].albedo.compressed, "다른 대상(ASTC)은 없음 → 붙이지 않음(RGBA8 원본)");
        const htex::AttachStats off = htex::attachCompressed(meshes, Target::Off);
        check(off.found == 0 && off.missing == 0, "Off 면 찾지 않음");
        // 텍스처별 PSNR(0단계, 측정값은 문서 4절). ASTC 는 이 PC 에서 그릴 수 없어(대체 경로) 풀어서 잽니다.
        {
            const TexSourceData* texs[4] = { &meshes[0].albedo, &meshes[0].normalMap, &meshes[0].metalRough, &meshes[0].emissive };
            const Use uses[4] = { Use::Color, Use::Normal, Use::Linear, Use::Color };
            const char* names[4] = { "albedo", "normal", "metalRough", "emissive" };
            double minBc = 99.0, minAstc = 99.0;
            for (int i = 0; i < 4; ++i) {
                const TexSourceData& t = *texs[i];
                const int ch = uses[i] == Use::Normal ? 2 : 3;
                const double pb = roundTripPsnr(t.data, (uint32_t)t.w, (uint32_t)t.h, Target::BC, uses[i], ch);
                const double pa = roundTripPsnr(t.data, (uint32_t)t.w, (uint32_t)t.h, Target::ASTC, uses[i], ch);
                std::printf("    TexProbe %-10s %dx%d  BC %.2f dB  ASTC %.2f dB\n", names[i], t.w, t.h, pb, pa);
                minBc = std::min(minBc, pb);
                minAstc = std::min(minAstc, pa);
            }
            check(minBc >= 42.0, "TexProbe 텍스처 넷 모두 BC PSNR ≥ 42 dB");
            check(minAstc >= 38.0, "TexProbe 텍스처 넷 모두 ASTC PSNR ≥ 38 dB");
        }
        // 한 파일을 손상시키면 그 칸만 invalid.
        const fs::path albedoFile = cooked / fs::path(htex::ktx2Path(Target::BC,
            htex::textureKey(meshes[0].albedo.data.data(), meshes[0].albedo.w, meshes[0].albedo.h, Use::Color)));
        { std::ofstream o(albedoFile, std::ios::binary | std::ios::trunc); o << "KTX"; }
        const htex::AttachStats bad = htex::attachCompressed(meshes, Target::BC);
        check(bad.found == 4 && bad.invalid == 1 && !meshes[0].albedo.compressed, "손상된 KTX2 는 붙이지 않음(그 칸은 RGBA8)");
        // 화소가 달라지면(원본을 바꾸고 다시 묶지 않음) 찾지 못함.
        meshes[0].emissive.data[0] ^= 0x10;
        const htex::AttachStats changed = htex::attachCompressed(meshes, Target::BC);
        check(!meshes[0].emissive.compressed && changed.missing == 1, "원본 화소가 바뀌면 찾지 못함 → RGBA8");
        hvfs::reset();
    }

    fs::remove_all(tmp, ec);
    std::printf("\n(%d passed, %d failed)\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
