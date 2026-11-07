#include "SkinnedMeshComponent.hpp"
#include "../assets/AssetManager.hpp"
#include "../assets/MeshSource.hpp"
#include "../assets/Skin.hpp"
#include "../renderer/MeshHandle.hpp"
#include "../core/Actor.hpp"
#include "../core/ComponentFields.hpp"
#include "../core/SceneLog.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>

namespace {
const glm::quat kIdentity{ 1.f, 0.f, 0.f, 0.f };
}

int SkinnedMeshComponent::jointCount() const { return skin_ ? (int)skin_->joints.size() : 0; }

int SkinnedMeshComponent::findJoint(const std::string& name) const {
    return skin_ ? hskin::findJoint(*skin_, name) : -1;
}

const glm::quat& SkinnedMeshComponent::jointRotation(int joint) const {
    return (joint >= 0 && joint < (int)pose_.size()) ? pose_[joint] : kIdentity;
}

const glm::quat& SkinnedMeshComponent::restRotation(int joint) const {
    return (skin_ && joint >= 0 && joint < (int)skin_->joints.size()) ? skin_->joints[joint].restR : kIdentity;
}

void SkinnedMeshComponent::setJointRotation(int joint, const glm::quat& q) {
    if (joint < 0 || joint >= (int)pose_.size()) return;
    pose_[joint] = q;
    dirty_ = true;
}

void SkinnedMeshComponent::resetPose() {
    pose_.clear();
    if (skin_) {
        pose_.reserve(skin_->joints.size());
        for (const SkinJoint& j : skin_->joints) pose_.push_back(j.restR);
    }
    dirty_ = true;
}

const std::vector<glm::mat4>& SkinnedMeshComponent::jointGlobals() const {
    if (dirty_ && skin_) {
        hskin::computeGlobals(*skin_, pose_.empty() ? nullptr : pose_.data(), globals_);
        hskin::computePalette(*skin_, globals_, palette_);
        dirty_ = false;
    }
    return globals_;
}

bool SkinnedMeshComponent::skinInto(MeshSourceVertex* out, glm::vec3& bmin, glm::vec3& bmax) const {
    if (!skin_ || !bind_ || !out) return false;
    jointGlobals();   // palette_ 갱신
    hskin::skinVertices(bind_->data(), bind_->size(), *skin_, palette_.data(), out, bmin, bmax);
    return true;
}

void SkinnedMeshComponent::setSkinData(std::shared_ptr<const SkinSourceData> skin,
                                       std::shared_ptr<const std::vector<MeshSourceVertex>> bindVertices) {
    skin_ = std::move(skin);
    bind_ = std::move(bindVertices);
    vertCount = bind_ ? (int)bind_->size() : 0;
    resetPose();
}

bool SkinnedMeshComponent::localBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    if (!baseMesh) return false;
    outMin = baseMesh->aabbMin;
    outMax = baseMesh->aabbMax;
    return true;
}

std::vector<PropDesc> SkinnedMeshComponent::buildProps() {
    auto* p = this;
    return {
        XPROP_Toggle(p, "Visible",     visible),
        XPROP_Toggle(p, "Transparent", transparent),
    };
}

std::string SkinnedMeshComponent::readOnlyInfo() const {
    if (modelPath.empty()) return {};
    const auto slash = modelPath.find_last_of("/\\");
    const std::string file = (slash != std::string::npos) ? modelPath.substr(slash + 1) : modelPath;
    char buf[160];
    if (loaded())
        std::snprintf(buf, sizeof(buf), "%s  V:%d  T:%d  joints:%d", file.c_str(), vertCount, triCount, jointCount());
    else
        std::snprintf(buf, sizeof(buf), "%s  (not loaded or no skin)", file.c_str());
    return buf;
}

// ── 직렬화 ────────────────────────────────────────────────────────────────────
// 자세는 저장하지 않습니다(손 자세는 HandPoseComponent 가 매 프레임 정함).
void SkinnedMeshComponent::writeFields(ComponentFieldWriter& w) const {
    w.b("visible",     visible);
    w.b("transparent", transparent);
    if (!modelPath.empty()) {
        w.s("model",     modelPath);
        w.i("meshIndex", meshIndex);
    }
}

bool SkinnedMeshComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if (key == "visible")          ok = fieldparse::toBool(value, visible);
    else if (key == "transparent") { ok = fieldparse::toBool(value, transparent); transparentLoaded_ = ok; }
    else if (key == "model")       modelPath = value;
    else if (key == "meshIndex")   ok = fieldparse::toInt(value, meshIndex);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}

void SkinnedMeshComponent::onFieldsLoaded(const ComponentLoadContext& ctx) {
    if (!ctx.assets || modelPath.empty()) return;
    const std::string actorName = owner ? owner->name : std::string("?");
    try {
        auto pairs = ctx.assets->loadAll(modelPath);
        if (pairs.empty()) return;
        const int idx = std::clamp(meshIndex, 0, (int)pairs.size() - 1);
        const AssetPair& a = pairs[idx];
        meshIndex = idx;
        baseMesh  = a.mesh;
        material  = a.material;
        triCount  = a.triCount;
        if (!transparentLoaded_) transparent = a.isBlend;
        if (!a.skin || !a.bindVertices) {
            HLOG(LogScene, Warning) << "actor '" << actorName << "': " << modelPath << " mesh " << idx
                                    << " has no skin; SkinnedMeshComponent draws nothing (use StaticMeshComponent)";
            setSkinData(nullptr, nullptr);
            return;
        }
        setSkinData(a.skin, a.bindVertices);
    } catch (const std::exception& ex) {
        HLOG(LogScene, Error) << "Skinned mesh load failed (" << actorName << "): " << ex.what();
    }
}
