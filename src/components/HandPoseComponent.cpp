#include "HandPoseComponent.hpp"
#include "SkinnedMeshComponent.hpp"
#include "../assets/Skin.hpp"
#include "../scene/Actor.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/SceneLog.hpp"
#include "../input/HInputSystem.hpp"
#include "../xr/HandInputMap.hpp"
#include "../math/Damping.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

HandPoseComponent::HandPoseComponent() {
    for (int f = 0; f < hhand::kFingerCount; ++f)
        for (int k = 0; k < 3; ++k) maxFlexDeg[f][k] = hhand::kDefaultMaxFlexDeg[f][k];
    for (int& j : jointMap_) j = -1;
}

bool HandPoseComponent::map(SkinnedMeshComponent& mesh) {
    const SkinSourceData* skin = mesh.skin();
    if (!skin) return false;
    if (mappedSkin_ == skin) return true;
    mappedSkin_ = skin;
    mapped_ = 0;
    axisViolations_.clear();
    std::string missing;
    for (int i = 0; i < hhand::kJointCount; ++i) {
        jointMap_[i] = hskin::findJoint(*skin, hhand::kHandJointNames[i]);
        if (jointMap_[i] >= 0) ++mapped_;
        else { if (!missing.empty()) missing += ", "; missing += hhand::kHandJointNames[i]; }
    }
    const std::string actorName = owner ? owner->name : std::string("?");
    if (!missing.empty()) {
        HLOG(LogScene, Warning) << "actor '" << actorName << "': hand model is missing OpenXR joint(s): " << missing
                                << " (fingers that use them do not bend)";
    }

    // 축 규약: 굽힘 관절의 자식(같은 손가락의 다음 관절)이 그 관절의 로컬 -Z 쪽에 있어야 합니다.
    for (int f = 0; f < hhand::kFingerCount; ++f) {
        for (int k = 0; k < 3; ++k) {
            const int j     = jointMap_[(int)hhand::kFingerFlexJoints[f][k]];
            const int child = jointMap_[k < 2 ? (int)hhand::kFingerFlexJoints[f][k + 1] : (int)hhand::kFingerTip[f]];
            if (j < 0 || child < 0 || skin->joints[child].parent != j) continue;
            const glm::vec3 t = skin->joints[child].restT;
            const float len = glm::length(t);
            if (len < 1e-6f) continue;
            if (glm::dot(t / len, glm::vec3(0.f, 0.f, -1.f)) < 0.9f)
                axisViolations_.push_back(skin->joints[j].name);
        }
    }
    if (!axisViolations_.empty()) {
        std::string names;
        for (const auto& n : axisViolations_) { if (!names.empty()) names += ", "; names += n; }
        HLOG(LogScene, Warning) << "actor '" << actorName << "': hand joint axes do not follow the OpenXR convention "
                                << "(child joint not on local -Z): " << names << "; those fingers bend the wrong way";
    }
    return true;
}

void HandPoseComponent::writeFlex(SkinnedMeshComponent& mesh, const hgrasp::Flex& flex) {
    for (int f = 0; f < hhand::kFingerCount; ++f)
        for (int k = 0; k < 3; ++k) {
            const int j = jointMap_[(int)hhand::kFingerFlexJoints[f][k]];
            if (j < 0) continue;
            const float angle = -glm::radians(maxFlexDeg[f][k]) * std::clamp(flex[f][k], 0.f, 1.f);
            mesh.setJointRotation(j, mesh.restRotation(j) * glm::angleAxis(angle, glm::vec3(1.f, 0.f, 0.f)));
        }
}

float HandPoseComponent::graspWeight() const {
    return graspFrom_ + (graspTo_ - graspFrom_) * hmath::ease::inOutCubic(graspT_);
}

bool HandPoseComponent::apply() {
    if (!owner) return false;
    auto* mesh = owner->getComponent<SkinnedMeshComponent>();
    if (!mesh || !mesh->hasSkin() || !map(*mesh)) return false;
    const float w = graspWeight();
    hgrasp::Flex flex{};
    for (int f = 0; f < hhand::kFingerCount; ++f) {
        const float c = std::clamp(curl[f], 0.f, 1.f);
        for (int k = 0; k < 3; ++k) flex[f][k] = c + (graspFlex_[f][k] - c) * w;
    }
    writeFlex(*mesh, flex);
    return true;
}

void HandPoseComponent::setGrasp(const hgrasp::Flex& flex, float blendTime) {
    const float w = graspWeight();
    graspFlex_  = flex;
    graspFrom_  = w;
    graspTo_    = 1.f;
    graspBlend_ = blendTime;
    graspT_     = blendTime > 0.f ? 0.f : 1.f;
}

void HandPoseComponent::clearGrasp(float blendTime) {
    graspFrom_  = graspWeight();
    graspTo_    = 0.f;
    graspBlend_ = blendTime;
    graspT_     = blendTime > 0.f ? 0.f : 1.f;
}

bool HandPoseComponent::jointWorldPositions(const hgrasp::Flex& flex, hgrasp::JointPos& out) {
    if (!owner) return false;
    auto* mesh = owner->getComponent<SkinnedMeshComponent>();
    if (!mesh || !mesh->hasSkin() || !map(*mesh)) return false;
    const glm::mat4 world = owner->worldMatrix();
    std::vector<glm::quat> saved((size_t)mesh->jointCount());
    for (int j = 0; j < mesh->jointCount(); ++j) saved[(size_t)j] = mesh->jointRotation(j);
    writeFlex(*mesh, flex);
    const std::vector<glm::mat4>& g = mesh->jointGlobals();
    for (int i = 0; i < hhand::kJointCount; ++i) {
        const int j = jointMap_[i];
        out[(size_t)i] = j >= 0 && j < (int)g.size() ? glm::vec3(world * g[(size_t)j][3]) : glm::vec3(world[3]);
    }
    for (int j = 0; j < mesh->jointCount(); ++j) mesh->setJointRotation(j, saved[(size_t)j]);
    return true;
}

bool HandPoseComponent::jointWorld(hhand::EJoint joint, glm::mat4& out) {
    if (!owner) return false;
    auto* mesh = owner->getComponent<SkinnedMeshComponent>();
    if (!mesh || !mesh->hasSkin() || !map(*mesh)) return false;
    const int j = jointMap_[(int)joint];
    const std::vector<glm::mat4>& g = mesh->jointGlobals();
    if (j < 0 || j >= (int)g.size()) return false;
    out = owner->worldMatrix() * g[(size_t)j];
    return true;
}

bool HandPoseComponent::updateFromInput(float dt) {
    if (hand == EHandSide::None) return false;
    const HXrInputState& xr = HInputSystem::get().xr();
    const HHandInput&    h  = xr.hands[hand == EHandSide::Left ? 0 : 1];
    if (!xr.focused || !h.active) return false;
    float target[hhand::kFingerCount];
    hhand::curlTargets(h, target);
    for (int f = 0; f < hhand::kFingerCount; ++f) curl[f] = hhand::smoothCurl(curl[f], target[f], dt);
    return true;
}

void HandPoseComponent::tick(float dt) {
    updateFromInput(dt);
    if (graspT_ < 1.f) graspT_ = graspBlend_ > 0.f ? std::min(1.f, graspT_ + dt / graspBlend_) : 1.f;
    apply();
}

std::vector<PropDesc> HandPoseComponent::buildProps() {
    auto* p = this;
    return {
        makePropEnum("Hand", { "None (manual)", "Left", "Right" },
                     [p] { return (int)p->hand; }, [p](int v) { p->hand = (EHandSide)std::clamp(v, 0, 2); }),
        XPROP_FloatClamp(p, "Thumb",  PropColor::Neutral, 0.05f, curl[0], 0.f, 1.f),
        XPROP_FloatClamp(p, "Index",  PropColor::Neutral, 0.05f, curl[1], 0.f, 1.f),
        XPROP_FloatClamp(p, "Middle", PropColor::Neutral, 0.05f, curl[2], 0.f, 1.f),
        XPROP_FloatClamp(p, "Ring",   PropColor::Neutral, 0.05f, curl[3], 0.f, 1.f),
        XPROP_FloatClamp(p, "Little", PropColor::Neutral, 0.05f, curl[4], 0.f, 1.f),
    };
}

std::string HandPoseComponent::readOnlyInfo() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Finger curl 0 (open) .. 1 (fist). Joints %d/%d%s", mapped_, hhand::kJointCount,
                  axisViolations_.empty() ? "" : ", axis warning");
    return buf;
}

// ── 직렬화 ────────────────────────────────────────────────────────────────────
// curl = "엄지 검지 중지 약지 소지", maxFlex = 손가락 순서로 관절 세 개씩 15개.
void HandPoseComponent::writeFields(ComponentFieldWriter& w) const {
    std::string c, m;
    for (int f = 0; f < hhand::kFingerCount; ++f) {
        if (f) c += ' ';
        c += floatToText(curl[f]);
        for (int k = 0; k < 3; ++k) { if (!m.empty()) m += ' '; m += floatToText(maxFlexDeg[f][k]); }
    }
    w.s("hand", hand == EHandSide::Left ? "left" : hand == EHandSide::Right ? "right" : "none");
    w.s("curl", c);
    w.s("maxFlex", m);
}

bool HandPoseComponent::readField(const std::string& key, const std::string& value) {
    auto readFloats = [&](float* out, int n) {
        std::istringstream is(value);
        std::vector<float> v;
        std::string tok;
        while (is >> tok) { float x; if (!fieldparse::toFloat(tok, x)) return false; v.push_back(x); }
        if ((int)v.size() != n) return false;
        std::copy(v.begin(), v.end(), out);
        return true;
    };
    bool ok = true;
    if (key == "hand") {
        if      (value == "none")  hand = EHandSide::None;
        else if (value == "left")  hand = EHandSide::Left;
        else if (value == "right") hand = EHandSide::Right;
        else ok = false;
    } else if (key == "curl") {
        float tmp[hhand::kFingerCount];
        ok = readFloats(tmp, hhand::kFingerCount);
        if (ok) for (int f = 0; f < hhand::kFingerCount; ++f) curl[f] = std::clamp(tmp[f], 0.f, 1.f);
    } else if (key == "maxFlex") {
        float tmp[hhand::kFingerCount * 3];
        ok = readFloats(tmp, hhand::kFingerCount * 3);
        if (ok) for (int i = 0; i < hhand::kFingerCount * 3; ++i) maxFlexDeg[i / 3][i % 3] = std::clamp(tmp[i], -180.f, 180.f);
    } else {
        return false;
    }
    if (!ok) reportBadField(key, value);
    return true;
}

void HandPoseComponent::onFieldsLoaded(const ComponentLoadContext& /*ctx*/) { apply(); }
