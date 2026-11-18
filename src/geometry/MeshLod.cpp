#include "MeshLod.hpp"
#include <meshoptimizer.h>
#include <cmath>
#include <cstddef>
#include <utility>

namespace hmesh {

static_assert(offsetof(MeshSourceVertex, uv) == offsetof(MeshSourceVertex, normal) + 3 * sizeof(float),
              "MeshSourceVertex normal[3] and uv[2] must be contiguous for meshopt_simplifyWithAttributes");

std::vector<LodLevel> buildLods(const std::vector<MeshSourceVertex>& vertices,
                                const std::vector<uint32_t>& indices,
                                float boundRadius,
                                bool lockBorder) {
    std::vector<LodLevel> out;
    if (vertices.empty() || indices.size() < kLodMinTriangles * 3 || !(boundRadius > 0.f))
        return out;

    const float scale = meshopt_simplifyScale(&vertices[0].pos[0], vertices.size(),
                                              sizeof(MeshSourceVertex));
    if (!(scale > 0.f)) return out;

    // 법선(0.5) · UV(1.0) 가중치
    constexpr float kAttrWeights[5] = { 0.5f, 0.5f, 0.5f, 1.0f, 1.0f };
    const unsigned int options = meshopt_SimplifyPermissive |
                                 (lockBorder ? meshopt_SimplifyLockBorder : 0u);

    // 같은 위치에 UV가 1e-4 넘게 다른 정점이 있으면 그 위치의 정점을 UV 솔기로 보고 보호
    std::vector<unsigned int> posRemap(vertices.size());
    meshopt_generatePositionRemap(posRemap.data(), &vertices[0].pos[0],
                                  vertices.size(), sizeof(MeshSourceVertex));
    std::vector<unsigned char> posHasUvSplit(vertices.size(), 0);
    for (size_t i = 0; i < vertices.size(); ++i) {
        const unsigned int rep = posRemap[i];
        if (std::fabs(vertices[i].uv[0] - vertices[rep].uv[0]) > 1e-4f ||
            std::fabs(vertices[i].uv[1] - vertices[rep].uv[1]) > 1e-4f) {
            posHasUvSplit[rep] = 1;
        }
    }
    std::vector<unsigned char> vertexLock(vertices.size(), 0);
    for (size_t i = 0; i < vertices.size(); ++i) {
        if (posHasUvSplit[posRemap[i]])
            vertexLock[i] = meshopt_SimplifyVertex_Protect;
    }

    const size_t origTris = indices.size() / 3;
    size_t prevCount = indices.size();
    out.reserve(kLodMaxLevels);

    for (uint32_t level = 0; level < kLodMaxLevels; ++level) {
        const size_t targetTris = origTris >> (level + 1);
        if (targetTris == 0) break;
        const size_t targetCount = targetTris * 3;

        std::vector<uint32_t> dst(indices.size());
        float resultError = 0.f;
        const size_t count = meshopt_simplifyWithAttributes(
            dst.data(),
            indices.data(),
            indices.size(),
            &vertices[0].pos[0],
            vertices.size(),
            sizeof(MeshSourceVertex),
            &vertices[0].normal[0],
            sizeof(MeshSourceVertex),
            kAttrWeights,
            5,
            vertexLock.data(),
            targetCount,
            kLodTargetError,
            options,
            &resultError);

        if (count == 0 || (float)count > (float)prevCount * kLodStopRatio)
            break;

        const float err = resultError * scale;
        if (!(err > 0.f))
            break;

        const float screenSize = (2.f * kLodPixelTolerance * boundRadius) / (err * kLodRefScreenHeight);
        if (!out.empty() && !(screenSize < out.back().screenSize))
            break;

        dst.resize(count);
        out.push_back({ std::move(dst), err, screenSize });
        prevCount = count;
    }

    return out;
}

} // namespace hmesh
