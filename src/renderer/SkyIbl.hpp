#pragma once
// CPU 하늘 IBL 굽기. glm 만 쓰는 헤더라 단위 시험(HarukaLightTest 7절)에서 장치 없이 검산합니다.
//
//  - 확산 조도: 하늘을 SH 3밴드(9계수 x RGB)로 투영해 평가합니다(Ramamoorthi · Hanrahan 2001,
//    입체각 가중은 Sloan "Stupid SH Tricks"). 예전 텍셀마다 몬테카를로 64샘플은 텍셀 노이즈가 상대 RMS 2.7%
//    (최대 10%)였고, SH 는 0.2%(최대 0.9%)이며 시작 시간도 약 30 ms → 0.2 ms 입니다.
//  - 스페큘러 프리필터: GGX(α = 지각 거칠기², pbr.frag D_GGX 와 같은 규약) 중요도 샘플링, N = V = R 가정.
//    예전에는 skyColor 와 연결되지 않은 평균색 상수와 섞어 거친 mip 의 색이 치우쳤습니다.
//  - 저장 규약은 그대로 "그 방향에서 오는 평균 복사휘도(E/π)" 입니다. π 를 곱해 저장하지 않습니다.

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hibl {

// 큐브맵 면과 UV([-1, 1]) → 월드 방향. 면 규약(+X, -X, +Y, -Y, +Z, -Z)은 Vulkan 큐브 규약입니다.
inline glm::vec3 cubeDir(int face, float u, float v) {
    switch (face) {
        case 0: return { 1.f, -v, -u };  // +X
        case 1: return {-1.f, -v,  u };  // -X
        case 2: return {  u,  1.f,  v };  // +Y
        case 3: return {  u, -1.f, -v };  // -Y
        case 4: return {  u, -v,  1.f };  // +Z
        case 5: return { -u, -v, -1.f };  // -Z
        default: return {0,0,1};
    }
}

struct SH9Rgb { glm::vec3 c[9]{}; };

// 실수 SH 기저(l ≤ 2). d 는 단위 벡터.
inline void shBasis9(const glm::vec3& d, float y[9]) {
    y[0] = 0.282095f;
    y[1] = 0.488603f * d.y;
    y[2] = 0.488603f * d.z;
    y[3] = 0.488603f * d.x;
    y[4] = 1.092548f * d.x * d.y;
    y[5] = 1.092548f * d.y * d.z;
    y[6] = 0.315392f * (3.f * d.z * d.z - 1.f);
    y[7] = 1.092548f * d.x * d.z;
    y[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

// 6면 x res² 텍셀 중심 방향으로 투영합니다. 입체각 가중 w = 4 / (1 + u² + v²)^1.5, 끝에 4π / Σw 로 정규화.
template <class SkyFn>
SH9Rgb projectSH9(const SkyFn& sky, int res) {
    SH9Rgb sh;
    double wsum = 0.0;
    for (int face = 0; face < 6; ++face)
        for (int iy = 0; iy < res; ++iy)
            for (int ix = 0; ix < res; ++ix) {
                const float u = (ix + 0.5f) / res * 2.f - 1.f;
                const float v = (iy + 0.5f) / res * 2.f - 1.f;
                const float t = 1.f + u * u + v * v;
                const float w = 4.f / (t * std::sqrt(t));
                const glm::vec3 d = glm::normalize(cubeDir(face, u, v));
                const glm::vec3 L = sky(d);
                float y[9];
                shBasis9(d, y);
                for (int k = 0; k < 9; ++k) sh.c[k] += L * (y[k] * w);
                wsum += w;
            }
    const float norm = (float)(4.0 * 3.14159265358979 / wsum);
    for (int k = 0; k < 9; ++k) sh.c[k] *= norm;
    return sh;
}

// 법선 n 의 평균 복사휘도(E/π). 대역 가중 A0 = π, A1 = 2π/3, A2 = π/4 를 곱한 뒤 π 로 나눕니다.
inline glm::vec3 shMeanRadiance(const SH9Rgb& sh, const glm::vec3& n) {
    float y[9];
    shBasis9(n, y);
    constexpr float A[3] = { 1.f, 2.f / 3.f, 0.25f };   // A_l / π
    glm::vec3 r(0.f);
    for (int k = 0; k < 9; ++k) r += sh.c[k] * (y[k] * A[k == 0 ? 0 : (k < 4 ? 1 : 2)]);
    return r;
}

inline float radicalInverse(uint32_t b) {
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return float(b) * 2.3283064365386963e-10f;
}

// GGX 프리필터(Karis 2013 의 N = V = R 근사, N·L 가중). perceptualRoughness ≤ 0 이면 sky(R) 그대로.
template <class SkyFn>
glm::vec3 prefilterGGX(const SkyFn& sky, glm::vec3 R, float perceptualRoughness, uint32_t n) {
    if (perceptualRoughness <= 0.f) return sky(R);
    const glm::vec3 N  = glm::normalize(R);
    const float     a  = perceptualRoughness * perceptualRoughness;
    const float     a2 = a * a;
    const glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    const glm::vec3 T  = glm::normalize(glm::cross(up, N));
    const glm::vec3 B  = glm::cross(N, T);
    glm::vec3 sum(0.f);
    float     wsum = 0.f;
    for (uint32_t i = 0; i < n; ++i) {
        const float xi1 = (float)i / (float)n, xi2 = radicalInverse(i);
        const float phi = 2.f * 3.14159265f * xi1;
        const float cosT = std::sqrt((1.f - xi2) / (1.f + (a2 - 1.f) * xi2));
        const float sinT = std::sqrt(std::max(0.f, 1.f - cosT * cosT));
        const glm::vec3 H = glm::normalize(T * (sinT * std::cos(phi)) + B * (sinT * std::sin(phi)) + N * cosT);
        const glm::vec3 L = 2.f * glm::dot(N, H) * H - N;
        const float NdotL = glm::dot(N, L);
        if (NdotL > 0.f) { sum += sky(L) * NdotL; wsum += NdotL; }
    }
    return wsum > 0.f ? sum / wsum : sky(R);
}

} // namespace hibl
