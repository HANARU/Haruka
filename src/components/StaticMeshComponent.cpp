#include "StaticMeshComponent.hpp"
#include "../renderer/MeshHandle.hpp"
#include "../assets/AssetManager.hpp"
#include "../core/Actor.hpp"
#include "../core/ComponentFields.hpp"
#include "../core/SceneLog.hpp"
#include <algorithm>
#include <exception>
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

// ── 직렬화 (DEF-22) ─────────────────────────────────────────────────────────────
void StaticMeshComponent::writeFields(ComponentFieldWriter& w) const {
    w.b("visible",     visible);
    w.b("transparent", transparent);
    if (primitive) {
        w.s("prim",         primitive->toKey());
        w.s("primMaterial", primitiveMaterial.toText());
    } else if (!modelPath.empty()) {
        w.s("model",     modelPath);
        w.i("meshIndex", meshIndex);
    }
}

bool StaticMeshComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if (key == "visible") {
        ok = fieldparse::toBool(value, visible);
    } else if (key == "transparent") {
        ok = fieldparse::toBool(value, transparent);
        transparentLoaded_ = ok;
    } else if (key == "prim") {
        // 값이 깨졌으면 메시 없는 컴포넌트가 됩니다. 다른 도형으로 대체하지 않습니다(설계 D4).
        PrimitiveDesc d;
        if (PrimitiveDesc::fromKey(value, d)) primitive = d;
        else ok = false;
    } else if (key == "primMaterial") {
        // 머티리얼이 깨진 것은 도형을 버릴 이유가 아닙니다. 기본 머티리얼로 둡니다.
        PrimitiveMaterialDesc m;
        if (PrimitiveMaterialDesc::fromText(value, m)) primitiveMaterial = m;
        else ok = false;
    } else if (key == "model") {
        modelPath = value;
    } else if (key == "meshIndex") {
        ok = fieldparse::toInt(value, meshIndex);
    } else {
        return false;
    }
    if (!ok) reportBadField(key, value);
    return true;
}

// 값을 다 읽은 뒤 메시와 머티리얼을 불러옵니다. 실패하면 AssetManager 가 이유를 남기고, 컴포넌트는 메시
// 없이 남되 도형 · 경로 값은 그대로 두어 다시 저장해도 사라지지 않습니다.
void StaticMeshComponent::onFieldsLoaded(const ComponentLoadContext& ctx) {
    if (!ctx.assets) return;
    const std::string actorName = owner ? owner->name : std::string("?");
    if (primitive) {
        if (!modelPath.empty())
            HLOG(LogScene, Warning) << "actor '" << actorName << "' has both prim and model; using prim";
        try {
            AssetPair pair = ctx.assets->acquirePrimitive(*primitive);
            if (pair.mesh) {
                mesh      = pair.mesh;
                material  = ctx.assets->acquirePrimitiveMaterial(primitiveMaterial);
                vertCount = pair.vertCount;
                triCount  = pair.triCount;
            }
        } catch (const std::exception& ex) {
            HLOG(LogScene, Error) << "Primitive create failed (" << actorName << "): " << ex.what();
        }
    } else if (!modelPath.empty()) {
        try {
            auto pairs = ctx.assets->loadAll(modelPath);
            if (!pairs.empty()) {
                const int idx = std::clamp(meshIndex, 0, (int)pairs.size() - 1);
                mesh      = pairs[idx].mesh;
                material  = pairs[idx].material;
                meshIndex = idx;
                vertCount = pairs[idx].vertCount;
                triCount  = pairs[idx].triCount;
                // 파일에 transparent 가 있으면 그 값(사용자가 디테일에서 바꾼 값)을 지킵니다(DEF-101).
                if (!transparentLoaded_) transparent = pairs[idx].isBlend;
            }
        } catch (const std::exception& ex) {
            HLOG(LogScene, Error) << "Mesh load failed (" << actorName << "): " << ex.what();
        }
    }
}
