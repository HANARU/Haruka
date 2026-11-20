#include "TextureEncode.hpp"
#include "../geometry/Ktx2.hpp"

#include <astcenc.h>
#include <bc7decomp.h>
#include <bc7enc.h>
#include <rgbcx.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace htexenc {
namespace {

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float linearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f; }
uint8_t toByte(float v) { return (uint8_t)std::lround(std::clamp(v, 0.f, 1.f) * 255.f); }

const float* srgbLut() {
    static float lut[256];
    static std::once_flag once;
    std::call_once(once, [] { for (int i = 0; i < 256; ++i) lut[i] = srgbToLinear(i / 255.f); });
    return lut;
}

Rgba8 downsample(const Rgba8& s, htex::Use use) {
    Rgba8 d;
    d.w = std::max(1u, s.w >> 1);
    d.h = std::max(1u, s.h >> 1);
    d.px.resize((size_t)d.w * d.h * 4);
    const float* lut = srgbLut();
    for (uint32_t y = 0; y < d.h; ++y) {
        const uint32_t y0 = std::min(2 * y, s.h - 1), y1 = std::min(2 * y + 1, s.h - 1);
        for (uint32_t x = 0; x < d.w; ++x) {
            const uint32_t x0 = std::min(2 * x, s.w - 1), x1 = std::min(2 * x + 1, s.w - 1);
            const uint8_t* p[4] = { &s.px[((size_t)y0 * s.w + x0) * 4], &s.px[((size_t)y0 * s.w + x1) * 4],
                                    &s.px[((size_t)y1 * s.w + x0) * 4], &s.px[((size_t)y1 * s.w + x1) * 4] };
            uint8_t* o = &d.px[((size_t)y * d.w + x) * 4];
            const int a = p[0][3] + p[1][3] + p[2][3] + p[3][3];
            o[3] = (uint8_t)((a + 2) / 4);
            if (use == htex::Use::Color) {
                for (int c = 0; c < 3; ++c)
                    o[c] = toByte(linearToSrgb((lut[p[0][c]] + lut[p[1][c]] + lut[p[2][c]] + lut[p[3][c]]) * 0.25f));
            } else if (use == htex::Use::Normal) {
                float v[3] = { 0.f, 0.f, 0.f };
                for (int k = 0; k < 4; ++k)
                    for (int c = 0; c < 3; ++c) v[c] += p[k][c] / 255.f * 2.f - 1.f;
                const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
                else             { v[0] = 0.f; v[1] = 0.f; v[2] = 1.f; }
                for (int c = 0; c < 3; ++c) o[c] = toByte(v[c] * 0.5f + 0.5f);
            } else {
                for (int c = 0; c < 3; ++c) o[c] = (uint8_t)((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
            }
        }
    }
    return d;
}

// 4x4 블록의 화소를 모읍니다(가장자리 밖은 마지막 화소를 반복).
void gatherBlock(const Rgba8& img, uint32_t bx, uint32_t by, uint8_t out[64]) {
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x) {
            const uint32_t sx = std::min(bx * 4 + x, img.w - 1), sy = std::min(by * 4 + y, img.h - 1);
            std::memcpy(out + (y * 4 + x) * 4, &img.px[((size_t)sy * img.w + sx) * 4], 4);
        }
}

void initBcOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        bc7enc_compress_block_init();
        rgbcx::init();
    });
}

bool astcBlock(uint32_t f, uint32_t& bx, uint32_t& by, astcenc_profile& prof) {
    const hktx2::BlockInfo b = hktx2::blockInfo(f);
    if (b.bytes == 0 || f < VK_FORMAT_ASTC_4x4_UNORM_BLOCK || f > VK_FORMAT_ASTC_12x12_SRGB_BLOCK) return false;
    bx = b.w; by = b.h;
    prof = b.srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR;
    return true;
}

bool compressAstc(const Rgba8& img, uint32_t f, htex::Use use, std::vector<uint8_t>& out, std::string& err) {
    uint32_t bx = 0, by = 0;
    astcenc_profile prof{};
    if (!astcBlock(f, bx, by, prof)) { err = "not an ASTC format"; return false; }
    astcenc_config cfg{};
    astcenc_error e = astcenc_config_init(prof, bx, by, 1, ASTCENC_PRE_MEDIUM, 0, &cfg);
    if (e != ASTCENC_SUCCESS) { err = std::string("astcenc_config_init: ") + astcenc_get_error_string(e); return false; }
    const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    astcenc_context* ctx = nullptr;
    e = astcenc_context_alloc(&cfg, threads, &ctx, nullptr);
    if (e != ASTCENC_SUCCESS) { err = std::string("astcenc_context_alloc: ") + astcenc_get_error_string(e); return false; }
    void* slice = const_cast<uint8_t*>(img.px.data());
    astcenc_image ai{ img.w, img.h, 1, ASTCENC_TYPE_U8, &slice };
    // 노멀은 두 채널(D2): B = 0 · A = 1 로 바꿔 압축하면 푼 값의 B 가 0 이 되어 셰이더가 Z 를 복원합니다.
    const astcenc_swizzle swz = use == htex::Use::Normal
        ? astcenc_swizzle{ ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_0, ASTCENC_SWZ_1 }
        : astcenc_swizzle{ ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
    out.assign((size_t)hktx2::levelBytes(f, img.w, img.h), 0);
    std::vector<astcenc_error> errs(threads, ASTCENC_SUCCESS);
    {
        std::vector<std::thread> pool;
        for (unsigned i = 1; i < threads; ++i)
            pool.emplace_back([&, i] { errs[i] = astcenc_compress_image(ctx, &ai, &swz, out.data(), out.size(), i); });
        errs[0] = astcenc_compress_image(ctx, &ai, &swz, out.data(), out.size(), 0);
        for (auto& t : pool) t.join();
    }
    astcenc_context_free(ctx);
    for (astcenc_error x : errs)
        if (x != ASTCENC_SUCCESS) { err = std::string("astcenc_compress_image: ") + astcenc_get_error_string(x); return false; }
    return true;
}

} // namespace

std::vector<Rgba8> buildMips(const uint8_t* rgba, uint32_t w, uint32_t h, htex::Use use) {
    std::vector<Rgba8> mips(1);
    mips[0].w = w;
    mips[0].h = h;
    mips[0].px.assign(rgba, rgba + (size_t)w * h * 4);
    while (mips.back().w > 1 || mips.back().h > 1) {
        Rgba8 next = downsample(mips.back(), use);
        mips.push_back(std::move(next));
    }
    return mips;
}

bool compressLevel(const Rgba8& img, uint32_t f, htex::Use use, std::vector<uint8_t>& out, std::string& err) {
    if (img.w == 0 || img.h == 0 || img.px.size() != (size_t)img.w * img.h * 4) { err = "bad image"; return false; }
    if (f == VK_FORMAT_BC7_SRGB_BLOCK || f == VK_FORMAT_BC7_UNORM_BLOCK || f == VK_FORMAT_BC5_UNORM_BLOCK) {
        initBcOnce();
        const uint32_t bw = (img.w + 3) / 4, bh = (img.h + 3) / 4;
        out.assign((size_t)bw * bh * 16, 0);
        bc7enc_compress_block_params p;
        bc7enc_compress_block_params_init(&p);
        p.m_uber_level = 1;
        if (use != htex::Use::Color) bc7enc_compress_block_params_init_linear_weights(&p);
        uint8_t block[64];
        for (uint32_t by = 0; by < bh; ++by)
            for (uint32_t bx = 0; bx < bw; ++bx) {
                gatherBlock(img, bx, by, block);
                uint8_t* dst = &out[((size_t)by * bw + bx) * 16];
                if (f == VK_FORMAT_BC5_UNORM_BLOCK) rgbcx::encode_bc5_hq(dst, block, 0, 1, 4);
                else                                bc7enc_compress_block(dst, block, &p);
            }
        return true;
    }
    return compressAstc(img, f, use, out, err);
}

bool decodeLevel(uint32_t f, const uint8_t* data, size_t len, uint32_t w, uint32_t h, Rgba8& out, std::string& err) {
    if (len != hktx2::levelBytes(f, w, h)) { err = "level size does not match the format"; return false; }
    out.w = w;
    out.h = h;
    out.px.assign((size_t)w * h * 4, 0);
    if (f == VK_FORMAT_BC7_SRGB_BLOCK || f == VK_FORMAT_BC7_UNORM_BLOCK || f == VK_FORMAT_BC5_UNORM_BLOCK) {
        const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        uint8_t px[64];
        for (uint32_t by = 0; by < bh; ++by)
            for (uint32_t bx = 0; bx < bw; ++bx) {
                const uint8_t* src = data + ((size_t)by * bw + bx) * 16;
                if (f == VK_FORMAT_BC5_UNORM_BLOCK) {
                    for (int i = 0; i < 16; ++i) { px[i * 4 + 2] = 0; px[i * 4 + 3] = 255; }
                    rgbcx::unpack_bc5(src, px, 0, 1, 4);
                } else if (!bc7decomp::unpack_bc7(src, reinterpret_cast<bc7decomp::color_rgba*>(px))) {
                    err = "invalid BC7 block";
                    return false;
                }
                for (uint32_t y = 0; y < 4; ++y)
                    for (uint32_t x = 0; x < 4; ++x) {
                        const uint32_t ox = bx * 4 + x, oy = by * 4 + y;
                        if (ox < w && oy < h) std::memcpy(&out.px[((size_t)oy * w + ox) * 4], px + (y * 4 + x) * 4, 4);
                    }
            }
        return true;
    }
    uint32_t bx = 0, by = 0;
    astcenc_profile prof{};
    if (!astcBlock(f, bx, by, prof)) { err = "unsupported format"; return false; }
    astcenc_config cfg{};
    astcenc_error e = astcenc_config_init(prof, bx, by, 1, ASTCENC_PRE_FASTEST, ASTCENC_FLG_DECOMPRESS_ONLY, &cfg);
    astcenc_context* ctx = nullptr;
    if (e == ASTCENC_SUCCESS) e = astcenc_context_alloc(&cfg, 1, &ctx, nullptr);
    if (e != ASTCENC_SUCCESS) { err = std::string("astcenc: ") + astcenc_get_error_string(e); return false; }
    void* slice = out.px.data();
    astcenc_image ai{ w, h, 1, ASTCENC_TYPE_U8, &slice };
    const astcenc_swizzle id{ ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
    e = astcenc_decompress_image(ctx, data, len, &ai, &id, 0);
    astcenc_context_free(ctx);
    if (e != ASTCENC_SUCCESS) { err = std::string("astcenc_decompress_image: ") + astcenc_get_error_string(e); return false; }
    return true;
}

std::vector<uint8_t> encodeKtx2(const uint8_t* rgba, uint32_t w, uint32_t h, htex::Target t, htex::Use u, std::string& err) {
    const uint32_t f = htex::formatFor(t, u);
    if (f == 0) { err = "no format for this target"; return {}; }
    const std::vector<Rgba8> mips = buildMips(rgba, w, h, u);
    std::vector<std::vector<uint8_t>> levels(mips.size());
    for (size_t i = 0; i < mips.size(); ++i)
        if (!compressLevel(mips[i], f, u, levels[i], err)) return {};
    std::vector<uint8_t> file = hktx2::write(f, w, h, levels);
    if (file.empty()) err = "KTX2 write failed";
    return file;
}

double psnr(const uint8_t* a, const uint8_t* b, size_t pixels, int channels) {
    double se = 0.0;
    for (size_t i = 0; i < pixels; ++i)
        for (int c = 0; c < channels; ++c) {
            const double d = (double)a[i * 4 + c] - (double)b[i * 4 + c];
            se += d * d;
        }
    const double mse = se / ((double)pixels * channels);
    if (mse <= 0.0) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace htexenc
