// 엔진 컴포넌트의 에디터 아이콘 · 범위 표시(2026-11-18, 엔진 구조 안정화 S4). 그리는 내용은 예전 각 컴포넌트의
// drawEditorVisual · editorIcon 과 같습니다(2026-11-17).
#include "ComponentVisualizers.hpp"
#include "../core/DebugDraw.hpp"
#include "../scene/Actor.hpp"
#include "../audio/AudioComponent.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/HeightFogComponent.hpp"
#include "../components/LightComponent.hpp"
#include "../components/ReflectionCaptureComponent.hpp"
#include "../components/SkinnedMeshComponent.hpp"
#include "../audio/ReverbZoneComponent.hpp"
#include "../physics/RigidBodyComponent.hpp"
#include "../physics/JointComponent.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace {

ComponentVisualizer iconOf(const glm::vec4& color, int shape) {
    ComponentVisualizer v;
    v.hasIcon   = true;
    v.iconColor = color;
    v.iconShape = shape;
    return v;
}

// 최소 · 최대 거리 구, 콘
void drawAudio(const AudioComponent& c) {
    if (!c.owner || !c.attenuation.spatialize) return;
    const glm::vec3 p = c.owner->worldPosition();
    hdebug::sphere(p, c.attenuation.minDistance, { 0.3f, 0.9f, 0.45f, 1.f });
    hdebug::sphere(p, c.attenuation.maxDistance, { 0.3f, 0.9f, 0.45f, 0.45f });
    if (c.cone.enabled()) {
        const glm::vec3 d = c.owner->worldRotation() * glm::vec3(0.f, 0.f, -1.f);
        const float r = c.attenuation.minDistance * 2.f;
        for (float deg : { c.cone.innerDeg, c.cone.outerDeg }) {
            const float h = glm::radians(std::min(deg, 179.f)) * 0.5f;
            hdebug::circle(p + d * (r * std::cos(h)), d, r * std::sin(h), { 0.6f, 1.f, 0.6f, deg == c.cone.innerDeg ? 1.f : 0.5f });
        }
    }
}

// 절두체(가까운 면 ~ min(zFar, 5 m), 16:9)
void drawCamera(const CameraComponent& c) {
    if (!c.owner) return;
    const glm::mat4 w = glm::inverse(c.viewMatrix());
    const float t = std::tan(glm::radians(c.fovY) * 0.5f);
    const float a = 16.f / 9.f;
    const glm::vec4 col{ 0.5f, 0.75f, 1.f, 1.f };
    auto corner = [&](float d, float sx, float sy) { return glm::vec3(w * glm::vec4(sx * t * a * d, sy * t * d, -d, 1.f)); };
    const float n = c.zNear, f = std::min(c.zFar, 5.f);
    glm::vec3 pn[4], pf[4];
    const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
    for (int i = 0; i < 4; ++i) { pn[i] = corner(n, sx[i], sy[i]); pf[i] = corner(f, sx[i], sy[i]); }
    for (int i = 0; i < 4; ++i) {
        hdebug::line(pn[i], pn[(i + 1) % 4], col);
        hdebug::line(pf[i], pf[(i + 1) % 4], col);
        hdebug::line(pn[i], pf[i], col);
    }
}

// 선택한 라이트의 범위(2026-11-17): Point = 감쇠 반경 구, Spot = 바깥 원뿔(길이 = 감쇠 반경), Directional = 빛 방향 화살표.
void drawLight(const LightComponent& c) {
    if (!c.owner) return;
    const glm::vec3 p = c.owner->worldPosition();
    const glm::vec4 col{ 1.f, 0.9f, 0.4f, 1.f };
    if (c.lightType == LightType::Point) {
        hdebug::sphere(p, c.attenuationRadius, col);
    } else if (c.lightType == LightType::Spot) {
        const glm::vec3 d = c.emitDirection();
        const float     r = c.attenuationRadius;
        const float     a = glm::radians(c.outerConeAngle);
        const glm::vec3 end = p + d * (r * std::cos(a));
        const float     rr  = r * std::sin(a);
        hdebug::circle(end, d, rr, col);
        const glm::vec3 ref = std::abs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 u = glm::normalize(glm::cross(d, ref)), v = glm::cross(d, u);
        for (const glm::vec3& o : { u, -u, v, -v }) hdebug::line(p, end + o * rr, col);
        hdebug::circle(p + d * (r * std::cos(glm::radians(c.innerConeAngle))), d, r * std::sin(glm::radians(c.innerConeAngle)),
                       { 1.f, 0.9f, 0.4f, 0.5f });
    } else {
        const glm::vec3 d = c.emitDirection();
        hdebug::line(p, p + d * 2.f, col);
        hdebug::circle(p + d * 2.f, d, 0.15f, col, 12);
    }
}

// 영향 반경 구
void drawReflectionCapture(const ReflectionCaptureComponent& c) {
    if (!c.owner) return;
    hdebug::sphere(c.owner->worldPosition(), c.radius, { 0.8f, 0.85f, 1.f, 1.f });
    hdebug::sphere(c.owner->worldPosition(), c.radius * 0.7f, { 0.8f, 0.85f, 1.f, 0.4f });   // 섞기 시작
}

// 구역 경계(상자 · 구)
void drawReverbZone(const ReverbZoneComponent& c) {
    if (!c.owner || c.unbounded) return;
    const glm::vec4 col{ 0.45f, 0.7f, 1.f, 1.f };
    if (c.shape == EReverbZoneShape::Box) {
        const glm::mat4 m = glm::translate(glm::mat4(1.f), c.owner->worldPosition()) * glm::mat4_cast(c.owner->worldRotation());
        hdebug::box(m, glm::vec3(0.f), c.halfExtent * glm::abs(c.owner->worldScale()), col);
    } else {
        const glm::vec3 s = glm::abs(c.owner->worldScale());
        hdebug::sphere(c.owner->worldPosition(), c.radius * std::max({ s.x, s.y, s.z }), col);
    }
}

// 충돌체(상자 · 구 · 캡슐, 중심 오프셋 포함)
void drawRigidBody(const RigidBodyComponent& c) {
    if (!c.owner) return;
    // 트리거는 주황(2026-11-20, Physics_Joints_And_Events D3)
    const glm::vec4 col = c.trigger ? glm::vec4(1.f, 0.6f, 0.2f, 1.f) : glm::vec4(0.3f, 1.f, 0.6f, 1.f);
    const glm::vec3 sc = glm::abs(c.owner->worldScale());
    const glm::mat4 rt = glm::translate(glm::mat4(1.f), c.owner->worldPosition()) * glm::mat4_cast(c.owner->worldRotation());
    const glm::vec3 off = c.center * c.owner->worldScale();
    const float maxS = std::max({ sc.x, sc.y, sc.z });
    switch (c.shape) {
        case EPhysicsShape::Box:
            hdebug::box(rt, off, glm::vec3(c.halfExtent.x, c.halfExtent.y, c.halfExtent.z) * sc, col);
            break;
        case EPhysicsShape::Sphere:
            hdebug::sphere(glm::vec3(rt * glm::vec4(off, 1.f)), c.halfExtent.x * maxS, col);
            break;
        case EPhysicsShape::Capsule: {
            const float r = c.halfExtent.x * maxS, h = c.halfExtent.y * maxS;
            const glm::vec3 up(rt[1]);
            const glm::vec3 mid = glm::vec3(rt * glm::vec4(off, 1.f));
            hdebug::sphere(mid + up * h, r, col);
            hdebug::sphere(mid - up * h, r, col);
            const glm::vec3 x(rt[0]), z(rt[2]);
            for (const glm::vec3& o : { x, -x, z, -z }) hdebug::line(mid + up * h + o * r, mid - up * h + o * r, col);
            break;
        }
        default: break;   // 볼록 껍질 · 메시는 메시 자체가 모양
    }
}

// 조인트(2026-11-20, Physics_Joints_And_Events D5): 앵커 원, 축 선, 경첩 한계 부채꼴 · 슬라이더 범위 · 거리 조인트 줄 · 상대까지 선.
// Play 전 자세 기준입니다(각도 0 = 지금 자세).
void drawJoint(const JointComponent& c) {
    if (!c.owner) return;
    const Actor& a = *c.owner;
    const glm::vec4 col{ 1.f, 0.4f, 0.9f, 1.f }, dim{ 1.f, 0.4f, 0.9f, 0.45f };
    const glm::vec3 p = a.worldPosition() + a.worldRotation() * (c.anchor * a.worldScale());
    glm::vec3 axis = a.worldRotation() * c.axis;
    axis = glm::length(axis) > 1e-6f ? glm::normalize(axis) : glm::vec3(0.f, 1.f, 0.f);
    const Actor* t = c.target.resolveFrom(&a);
    hdebug::circle(p, axis, 0.05f, col, 12);
    if (t) hdebug::line(p, t->worldPosition(), dim);
    switch (c.type) {
        case EJointType::Hinge: {
            hdebug::line(p - axis * 0.3f, p + axis * 0.3f, col);
            // 액터 중심 쪽(경첩에서 문으로)을 0 도로 보고 한계 사이를 부채꼴로 그립니다.
            glm::vec3 arm = a.worldPosition() - p;
            arm -= axis * glm::dot(arm, axis);
            if (glm::length(arm) < 1e-4f) arm = std::abs(axis.y) < 0.9f ? glm::cross(axis, glm::vec3(0, 1, 0)) : glm::cross(axis, glm::vec3(1, 0, 0));
            arm = glm::normalize(arm) * 0.4f;
            const float lo = c.limits ? std::clamp(c.limitMin, -180.f, 0.f) : -180.f;
            const float hi = c.limits ? std::clamp(c.limitMax, 0.f, 180.f) : 180.f;
            glm::vec3 prev = p + glm::angleAxis(glm::radians(lo), axis) * arm;
            hdebug::line(p, prev, col);
            const int n = 16;
            for (int i = 1; i <= n; ++i) {
                const glm::vec3 q = p + glm::angleAxis(glm::radians(lo + (hi - lo) * float(i) / n), axis) * arm;
                hdebug::line(prev, q, dim);
                prev = q;
            }
            hdebug::line(p, prev, col);
            break;
        }
        case EJointType::Slider: {
            const float lo = c.limits ? std::min(c.limitMin, 0.f) : -0.5f, hi = c.limits ? std::max(c.limitMax, 0.f) : 0.5f;
            hdebug::line(p + axis * lo, p + axis * hi, c.limits ? col : dim);
            break;
        }
        case EJointType::Distance: {
            const glm::vec3 q = t ? t->worldPosition() + t->worldRotation() * (c.connectedAnchor * t->worldScale()) : c.connectedAnchor;
            hdebug::line(p, q, col);
            hdebug::circle(q, axis, 0.04f, col, 12);
            break;
        }
        default: break;   // 고정: 상대까지 선만
    }
}

// 뼈대(애니메이션 A6-2): 관절마다 부모 관절까지 선, 관절 자리에 작은 원. 지금 자세(jointGlobals)를 액터 월드 행렬로 옮깁니다.
void drawSkeleton(const SkinnedMeshComponent& c) {
    if (!c.owner || !c.hasSkin()) return;
    const SkinSourceData& s = *c.skin();
    const std::vector<glm::mat4>& g = c.jointGlobals();
    const glm::mat4 w = c.owner->worldMatrix();
    const glm::vec4 col{ 1.f, 0.55f, 0.85f, 1.f };
    for (size_t j = 0; j < s.joints.size() && j < g.size(); ++j) {
        const glm::vec3 p = glm::vec3(w * g[j][3]);
        const int parent = s.joints[j].parent;
        if (parent >= 0 && parent < (int)g.size()) hdebug::line(glm::vec3(w * g[(size_t)parent][3]), p, col);
        hdebug::circle(p, glm::normalize(glm::vec3(w[1]) + glm::vec3(1e-6f)), 0.015f, col, 8);
    }
}

} // namespace

ComponentVisualizers::ComponentVisualizers() {
    add<AudioComponent>(iconOf({ 0.3f, 0.85f, 0.4f, 1.f }, 3), drawAudio);
    add<CameraComponent>(iconOf({ 0.35f, 0.6f, 1.f, 1.f }, 2), drawCamera);
    add<HeightFogComponent>(iconOf({ 0.7f, 0.75f, 0.8f, 1.f }, 5));
    add<LightComponent>(iconOf({ 1.f, 0.85f, 0.2f, 1.f }, 1), drawLight);
    add<ReflectionCaptureComponent>(iconOf({ 0.8f, 0.85f, 1.f, 1.f }, 6), drawReflectionCapture);
    add<ReverbZoneComponent>(iconOf({ 0.45f, 0.7f, 1.f, 1.f }, 4), drawReverbZone);
    add<RigidBodyComponent>(ComponentVisualizer{}, drawRigidBody);   // 아이콘 없음(메시가 잡힘)
    add<JointComponent>(ComponentVisualizer{}, drawJoint);           // 아이콘 없음(메시가 잡힘), 2026-11-20
    add<SkinnedMeshComponent>(ComponentVisualizer{}, drawSkeleton);   // 아이콘 없음(메시가 잡힘), 2026-11-20
}
