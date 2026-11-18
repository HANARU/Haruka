#include "StaticMeshComponent.hpp"
#include "LightChannels.hpp"
#include "../renderer/MeshHandle.hpp"
#include "../assets/AssetManager.hpp"
#include "../scene/Actor.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/SceneLog.hpp"
#include "../scene/World.hpp"
#include "../renderer/ScreenSize.hpp"
#include <algorithm>
#include <exception>
#include <cstdio>

MeshHandle* StaticMeshComponent::selectMeshForScreenSize(float screenSize) {
    if (!autoLod) { lodCurrent_ = 0; return mesh; }
    const size_t n = std::min(lodScreenSizes.size(), lodMeshes.size());
    lodCurrent_    = hscreen::selectLod(screenSize, std::span<const float>(lodScreenSizes.data(), n), lodCurrent_);
    // 비어 있는 단계는 더 고운 쪽으로 물러납니다.
    for (int i = lodCurrent_; i > 0; --i)
        if (lodMeshes[i - 1]) return lodMeshes[i - 1];
    return mesh;
}

void StaticMeshComponent::assignMeshFromPair(const AssetPair& pair) {
    mesh           = pair.mesh;
    collision      = pair.collision;
    vertCount      = pair.vertCount;
    triCount       = pair.triCount;
    lodMeshes      = pair.lods;
    lodScreenSizes = pair.lodScreenSizes;
    lodCurrent_    = 0;
}

std::vector<PropDesc> StaticMeshComponent::buildProps() {
    auto* p = this;
    std::vector<PropDesc> props{ SM_PROPS(p) };
    hchan::addProps(props, &lightChannels);
    if (primitive) {
        PrimitiveMaterialDesc* m = &primitiveMaterial;
        props.push_back(makePropToggle("Checker", [m] { return m->checker; }, [m](bool v) { m->checker = v; }));
        if (!m->checker) {
            // 색은 sRGB 표시값 0 ~ 1(저장은 8비트 그대로). shift 는 rgba8 안의 자리입니다.
            auto channel = [m](const char* lbl, glm::vec4 col, int shift) {
                return makePropFloat(lbl, col, 0.005f,
                    [m, shift] { return float((m->rgba8 >> shift) & 0xFFu) / 255.f; },
                    [m, shift](float v) {
                        const uint32_t c = uint32_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
                        m->rgba8 = (m->rgba8 & ~(0xFFu << shift)) | (c << shift);
                    }, PropUnit::Ratio);
            };
            props.push_back(channel("R", PropColor::Red,   24));
            props.push_back(channel("G", PropColor::Green, 16));
            props.push_back(channel("B", PropColor::Blue,  8));
            props.push_back(makePropFloat("Metallic", PropColor::Neutral, 0.01f, [m] { return m->metallic; },
                                          [m](float v) { m->metallic = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio));
            props.push_back(makePropFloat("Roughness", PropColor::Neutral, 0.01f, [m] { return m->roughness; },
                                          [m](float v) { m->roughness = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio));
        }
    }
    return props;
}

void StaticMeshComponent::onPropsCommitted() {
    if (primitive && assets_) material = assets_->acquirePrimitiveMaterial(primitiveMaterial);
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
    if (!autoLod) w.b("autoLod", false);
    hchan::write(w, lightChannels);
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
    } else if (key == "lightChannels") {
        ok = hchan::read(value, lightChannels);
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
    } else if (key == "autoLod") {
        ok = fieldparse::toBool(value, autoLod);
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
    assets_ = ctx.assets;
    const std::string actorName = owner ? owner->name : std::string("?");
    if (primitive) {
        if (!modelPath.empty())
            HLOG(LogScene, Warning) << "actor '" << actorName << "' has both prim and model; using prim";
        try {
            AssetPair pair = ctx.assets->acquirePrimitive(*primitive);
            if (pair.mesh) {
                mesh      = pair.mesh;
                collision = pair.collision;
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
                assignMeshFromPair(pairs[idx]);
                material  = pairs[idx].material;
                meshIndex = idx;
                // 파일에 transparent 가 있으면 그 값(사용자가 디테일에서 바꾼 값)을 지킵니다(DEF-101).
                if (!transparentLoaded_) transparent = pairs[idx].isBlend;
            }
        } catch (const std::exception& ex) {
            HLOG(LogScene, Error) << "Mesh load failed (" << actorName << "): " << ex.what();
        }
    }
}

std::vector<StaticMeshComponent*> StaticMeshComponent::visibleIn(const World& world) {
    std::vector<StaticMeshComponent*> result;
    for (StaticMeshComponent* m : world.componentsOf<StaticMeshComponent>())
        if (m->owner->activeInHierarchy())
            if (m->isEnabled() && m->visible && m->mesh)
                result.push_back(m);
    return result;
}
