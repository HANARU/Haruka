#pragma once
#include "../core/Actor.hpp"
#include "../core/Transform.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ── 편집 기본기의 순수 계산(2026-10-15) ────────────────────────────────────────
// 에디터 코드에서 떼어 둔 계산입니다. 화면 없이 검산합니다(editor_commands 테스트).

namespace hxform {

// 부모를 바꿔도 월드 자세가 그대로이도록 새 부모 아래의 로컬 트랜스폼을 구합니다(UE · Unity · Godot 의 에디터와 같음).
// 위치는 부모 월드 행렬의 역으로, 회전은 쿼터니언 합성으로, 크기는 부모의 월드 크기로 나눠 구합니다. 물리가 월드
// 자세를 되쓰는 Actor::setWorldPosition · setWorldRotation(DEF-53)과 같은 규칙이라, 부모가 비균등 크기 + 회전이면
// 크기는 근사입니다(그 경우 참 월드 크기가 순수 크기로 나뉘지 않음). parent 가 null 이면 월드 값이 곧 로컬입니다.
inline Transform localForWorld(const Actor* parent, const glm::vec3& worldPos, const glm::quat& worldRot,
                               const glm::vec3& worldScale) {
    Transform t;
    if (!parent) {
        t.setPosition(worldPos);
        t.setRotation(worldRot);
        t.setScale(worldScale);
        return t;
    }
    t.setPosition(glm::vec3(glm::inverse(parent->worldMatrix()) * glm::vec4(worldPos, 1.f)));
    t.setRotation(glm::normalize(glm::inverse(parent->worldRotation()) * worldRot));
    const glm::vec3 ps = parent->worldScale();
    auto div = [](float a, float b) { return std::abs(b) > 1e-12f ? a / b : a; };
    t.setScale({ div(worldScale.x, ps.x), div(worldScale.y, ps.y), div(worldScale.z, ps.z) });
    return t;
}

// 액터를 newParent 아래로 옮길 때의 로컬 트랜스폼(월드 자세 유지).
inline Transform localUnder(const Actor& a, const Actor* newParent) {
    return localForWorld(newParent, a.worldPosition(), a.worldRotation(), a.worldScale());
}

// 기즈모로 여러 액터를 함께 움직일 때: 주 선택이 start → now 로 바뀐 만큼(월드 공간의 위치 · 회전 · 크기 변화)을
// 다른 액터의 시작 월드 자세에 똑같이 적용합니다. 회전 · 크기는 주 선택의 위치를 중심으로 돕니다(UE 의 피벗 규칙).
struct WorldPose { glm::vec3 pos{ 0.f }; glm::quat rot{ 1.f, 0.f, 0.f, 0.f }; glm::vec3 scale{ 1.f }; };
inline WorldPose worldPoseOf(const Actor& a) { return { a.worldPosition(), a.worldRotation(), a.worldScale() }; }
inline WorldPose followPivot(const WorldPose& pivotStart, const WorldPose& pivotNow, const WorldPose& otherStart) {
    const glm::quat dRot = glm::normalize(pivotNow.rot * glm::inverse(pivotStart.rot));
    auto div = [](float a, float b) { return std::abs(b) > 1e-12f ? a / b : 1.f; };
    // 크기 변화는 주 선택의 로컬 축 기준 배율입니다. 다른 액터의 오프셋은 주 선택의 시작 회전 축에서 늘립니다.
    const glm::vec3 k{ div(pivotNow.scale.x, pivotStart.scale.x), div(pivotNow.scale.y, pivotStart.scale.y),
                       div(pivotNow.scale.z, pivotStart.scale.z) };
    const glm::vec3 offLocal = glm::inverse(pivotStart.rot) * (otherStart.pos - pivotStart.pos);
    WorldPose r;
    r.pos   = pivotNow.pos + pivotNow.rot * (offLocal * k);
    r.rot   = glm::normalize(dRot * otherStart.rot);
    r.scale = otherStart.scale * k;
    return r;
}

} // namespace hxform

namespace hclip {

// 액터 복사 · 붙여넣기의 클립보드 글(씬 형식의 액터 세트 앞에 머리 줄 둘). 씬 읽기는 '#' 줄을 건너뛰므로
// 머리 줄은 읽기에 영향이 없습니다. 루트마다 원래 부모 ID 를 적어, 붙여넣을 때 그 부모가 있으면 그 아래에 둡니다.
constexpr const char* kHeader = "#HARUKA_ACTORS 1";

inline std::string wrap(const std::string& actorSet, const std::vector<uint64_t>& rootParents) {
    std::string s = std::string(kHeader) + "\n#ROOT_PARENTS";
    for (uint64_t p : rootParents) s += " " + std::to_string(p);
    return s + "\n" + actorSet;
}

// Haruka 액터 클립보드가 아니면 false(다른 프로그램에서 복사한 글 등).
inline bool unwrap(const std::string& clip, std::string& actorSet, std::vector<uint64_t>& rootParents) {
    std::string text = clip;
    // 다른 프로그램을 거치며 CRLF 가 될 수 있습니다.
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    const std::string head = std::string(kHeader) + "\n";
    if (text.rfind(head, 0) != 0) return false;
    size_t pos = head.size();
    const size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) return false;
    const std::string line = text.substr(pos, eol - pos);
    const std::string tag = "#ROOT_PARENTS";
    if (line.rfind(tag, 0) != 0) return false;
    rootParents.clear();
    std::istringstream in(line.substr(tag.size()));
    for (std::string tok; in >> tok;) {
        char* end = nullptr;
        const unsigned long long v = std::strtoull(tok.c_str(), &end, 10);
        if (!end || *end != '\0') return false;
        rootParents.push_back((uint64_t)v);
    }
    actorSet = text.substr(eol + 1);
    return actorSet.find("ACTOR:") != std::string::npos;
}

// 붙여넣기 · 복제한 루트의 이름(Unity 식): "Cube" → "Cube (1)", "Cube (1)" → "Cube (2)". 이미 있는 이름은 건너뜁니다.
inline std::string nextCopyName(const std::string& name, const std::set<std::string>& existing) {
    std::string base = name;
    if (base.size() >= 4 && base.back() == ')') {
        const size_t open = base.rfind(" (");
        if (open != std::string::npos && open + 2 < base.size() - 1 &&
            std::all_of(base.begin() + (std::ptrdiff_t)open + 2, base.end() - 1, [](char c) { return c >= '0' && c <= '9'; }))
            base.resize(open);
    }
    for (int n = 1;; ++n) {
        std::string candidate = base + " (" + std::to_string(n) + ")";
        if (!existing.count(candidate)) return candidate;
    }
}

} // namespace hclip

namespace hframe {

// 반지름 radius 인 구가 화면에 꽉 차는 카메라 거리. 세로 · 가로 시야 중 좁은 쪽에 맞춥니다.
inline float fitDistance(float radius, float fovYRad, float aspect) {
    const float halfY = fovYRad * 0.5f;
    const float halfX = std::atan(std::tan(halfY) * std::max(aspect, 1e-3f));
    const float half  = std::max(std::min(halfY, halfX), 1e-3f);
    return std::max(radius, 1e-3f) / std::sin(half);
}

} // namespace hframe
