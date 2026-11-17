#pragma once
#include <vulkan/vulkan.h>
#include "../geometry/QTangent.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    float tangent[4];   // xyz=tangent direction, w=handedness (+1 or -1)

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription b{};
        b.binding   = 0;
        b.stride    = sizeof(Vertex);
        b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return b;
    }

    static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> a{};
        a[0].binding = 0; a[0].location = 0;
        a[0].format  = VK_FORMAT_R32G32B32_SFLOAT;
        a[0].offset  = offsetof(Vertex, pos);
        a[1].binding = 0; a[1].location = 1;
        a[1].format  = VK_FORMAT_R32G32B32_SFLOAT;
        a[1].offset  = offsetof(Vertex, normal);
        a[2].binding = 0; a[2].location = 2;
        a[2].format  = VK_FORMAT_R32G32_SFLOAT;
        a[2].offset  = offsetof(Vertex, uv);
        a[3].binding = 0; a[3].location = 3;
        a[3].format  = VK_FORMAT_R32G32B32A32_SFLOAT;
        a[3].offset  = offsetof(Vertex, tangent);
        return a;
    }
};

// ── GPU 정점(수학 보강 C2 QTangent, 2026-11-17) ───────────────────────────────────────────────────────────────
// 로더 · 절차 도형 · CPU 스키닝은 위 Vertex(48 B)를 그대로 쓰고, 메가 버퍼에 올릴 때만 이 28 B 형식으로 줄입니다. 법선 · 접선 ·
// 바이탱전트 방향은 그 세 축을 열로 갖는 회전의 쿼터니언 하나(snorm16 넷)에 담고, 바이탱전트의 손잡이는 w 의 부호로 담습니다
// (Crytek "Spherical Skinning with Dual-Quaternions and QTangents", SIGGRAPH 2011). q 와 −q 는 같은 회전이라 부호를 정보로
// 쓸 수 있고, w 가 0 이면 부호가 사라지므로 |w| 를 1/32767 이상으로 둡니다. 계산은 geometry/QTangent.hpp, 셰이더는
// hkDecodeQTangent 로 풉니다.
struct PackedVertex {
    float   pos[3];
    float   uv[2];
    int16_t qtangent[4];   // x, y, z, w (snorm16)

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription b{};
        b.binding   = 0;
        b.stride    = sizeof(PackedVertex);
        b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return b;
    }
    // location 0 = 위치, 2 = UV, 3 = QTangent(예전 1 = 법선 자리는 비움).
    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> a{};
        a[0].binding = 0; a[0].location = 0;
        a[0].format  = VK_FORMAT_R32G32B32_SFLOAT;
        a[0].offset  = offsetof(PackedVertex, pos);
        a[1].binding = 0; a[1].location = 2;
        a[1].format  = VK_FORMAT_R32G32_SFLOAT;
        a[1].offset  = offsetof(PackedVertex, uv);
        a[2].binding = 0; a[2].location = 3;
        a[2].format  = VK_FORMAT_R16G16B16A16_SNORM;
        a[2].offset  = offsetof(PackedVertex, qtangent);
        return a;
    }
};
static_assert(sizeof(PackedVertex) == 28, "PackedVertex is 28 bytes");

namespace hqtan {
inline PackedVertex pack(const Vertex& v) {
    PackedVertex o{};
    o.pos[0] = v.pos[0]; o.pos[1] = v.pos[1]; o.pos[2] = v.pos[2];
    o.uv[0] = v.uv[0]; o.uv[1] = v.uv[1];
    float q[4];
    encode(v.normal, v.tangent, q);
    for (int i = 0; i < 4; ++i) o.qtangent[i] = toSnorm16(q[i]);
    // 반올림으로 w 가 0 이 되면 손잡이를 잃으므로 ±1 로 둡니다.
    if (o.qtangent[3] == 0) o.qtangent[3] = q[3] < 0.f ? int16_t(-1) : int16_t(1);
    return o;
}
inline void packRange(const Vertex* src, PackedVertex* dst, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = pack(src[i]);
}
} // namespace hqtan
