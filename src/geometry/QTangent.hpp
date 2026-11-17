#pragma once
// QTangent 부호화 · 복호화(수학 보강 C2, 2026-11-17). Vulkan 의존이 없어 디바이스 없이 시험합니다. 정점 형식은
// renderer/Vertex.hpp 의 PackedVertex, 셰이더 쪽은 haruka_shared.h 의 hkDecodeQTangent 입니다.
#include <cmath>
#include <cstdint>

namespace hqtan {
inline void normalize3(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-12f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
inline void cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
// 법선 n · 접선 t(xyz, w = 손잡이 ±1) → 쿼터니언 q(x, y, z, w). 접선은 법선에 직교화하고, 접선이 없거나 법선과 나란하면
// 아무 직교 방향을 고릅니다(노멀 맵이 없는 메시는 접선이 0 일 수 있음).
inline void encode(const float nIn[3], const float tIn[4], float q[4]) {
    float n[3] = { nIn[0], nIn[1], nIn[2] };
    normalize3(n);
    if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 0.5f) { n[0] = 0.f; n[1] = 0.f; n[2] = 1.f; }
    float t[3] = { tIn[0], tIn[1], tIn[2] };
    float d = t[0] * n[0] + t[1] * n[1] + t[2] * n[2];
    for (int i = 0; i < 3; ++i) t[i] -= d * n[i];
    if (t[0] * t[0] + t[1] * t[1] + t[2] * t[2] < 1e-12f) {
        const float ax[3] = { std::fabs(n[0]) < 0.9f ? 1.f : 0.f, std::fabs(n[0]) < 0.9f ? 0.f : 1.f, 0.f };
        d = ax[0] * n[0] + ax[1] * n[1] + ax[2] * n[2];
        for (int i = 0; i < 3; ++i) t[i] = ax[i] - d * n[i];
    }
    normalize3(t);
    float b[3];
    cross3(n, t, b);   // 오른손 기저 (T, B, N)
    // 열이 T, B, N 인 회전 행렬 → 쿼터니언(Shepperd).
    const float m00 = t[0], m10 = t[1], m20 = t[2];
    const float m01 = b[0], m11 = b[1], m21 = b[2];
    const float m02 = n[0], m12 = n[1], m22 = n[2];
    const float tr = m00 + m11 + m22;
    float x, y, z, w;
    if (tr > 0.f) {
        const float s = std::sqrt(tr + 1.f) * 2.f;
        w = 0.25f * s; x = (m21 - m12) / s; y = (m02 - m20) / s; z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.f + m00 - m11 - m22) * 2.f;
        w = (m21 - m12) / s; x = 0.25f * s; y = (m01 + m10) / s; z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.f + m11 - m00 - m22) * 2.f;
        w = (m02 - m20) / s; x = (m01 + m10) / s; y = 0.25f * s; z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.f + m22 - m00 - m11) * 2.f;
        w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = 0.25f * s;
    }
    const float l = std::sqrt(x * x + y * y + z * z + w * w);
    x /= l; y /= l; z /= l; w /= l;
    if (w < 0.f) { x = -x; y = -y; z = -z; w = -w; }
    constexpr float kBias = 1.f / 32767.f;
    if (w < kBias) {                      // 부호를 담을 수 있게 w 를 띄우고 나머지를 줄여 단위 길이를 지킴
        const float k = std::sqrt(1.f - kBias * kBias);
        x *= k; y *= k; z *= k; w = kBias;
    }
    if (tIn[3] < 0.f) { x = -x; y = -y; z = -z; w = -w; }
    q[0] = x; q[1] = y; q[2] = z; q[3] = w;
}
// 쿼터니언 → 법선 · 접선 · 손잡이(셰이더 hkDecodeQTangent 와 같은 식).
inline void decode(const float qIn[4], float n[3], float t[3], float& sign) {
    float q[4] = { qIn[0], qIn[1], qIn[2], qIn[3] };
    const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l > 1e-12f) for (float& c : q) c /= l;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    t[0] = 1.f - 2.f * (y * y + z * z); t[1] = 2.f * (x * y + w * z); t[2] = 2.f * (x * z - w * y);
    n[0] = 2.f * (x * z + w * y); n[1] = 2.f * (y * z - w * x); n[2] = 1.f - 2.f * (x * x + y * y);
    sign = w < 0.f ? -1.f : 1.f;
}
inline int16_t toSnorm16(float v) {
    v = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
    return (int16_t)std::lround(v * 32767.f);
}
inline float fromSnorm16(int16_t v) { const float f = float(v) / 32767.f; return f < -1.f ? -1.f : f; }
} // namespace hqtan
