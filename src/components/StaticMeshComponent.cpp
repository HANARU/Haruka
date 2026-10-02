#include "StaticMeshComponent.hpp"
#include "../renderer/MeshHandle.hpp"
#include <cstdio>

MeshHandle* StaticMeshComponent::getMeshForDistanceSq(float distSq) const {
    MeshHandle* result = mesh;
    for (size_t i = 0; i < lodDistancesSq.size() && i < lodMeshes.size(); ++i) {
        if (distSq > lodDistancesSq[i] && lodMeshes[i])
            result = lodMeshes[i];
    }
    return result;
}

std::vector<PropDesc> StaticMeshComponent::buildProps() {
    auto* p = this;
    return { SM_PROPS(p) };
}

std::string StaticMeshComponent::readOnlyInfo() const {
    if (primitive) {
        // 절차 도형은 경로가 없으므로 셰이프와 세그먼트를 보입니다(예: "sphere 32x16").
        PrimitiveDesc d = *primitive;
        d.canonicalize();
        char seg[32] = "";
        if (d.segU && d.segV) std::snprintf(seg, sizeof(seg), " %ux%u", (unsigned)d.segU, (unsigned)d.segV);
        else if (d.segU)      std::snprintf(seg, sizeof(seg), " %u", (unsigned)d.segU);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s%s  V:%d  T:%d", primitiveShapeName(d.shape), seg, vertCount, triCount);
        return buf;
    }
    if (modelPath.empty()) return {};
    // Show just the filename (after last slash) to keep the Details panel compact
    auto slash = modelPath.find_last_of("/\\");
    std::string file = (slash != std::string::npos) ? modelPath.substr(slash + 1) : modelPath;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s  V:%d  T:%d", file.c_str(), vertCount, triCount);
    return buf;
}

bool StaticMeshComponent::localBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    if (!mesh) return false;
    outMin = mesh->aabbMin;
    outMax = mesh->aabbMax;
    return true;
}
