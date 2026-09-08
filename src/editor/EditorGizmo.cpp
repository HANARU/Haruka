#include "EditorGizmo.hpp"
#include "EditorCommands.hpp"
#include "../core/World.hpp"
#include "../core/Actor.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../renderer/Renderer.hpp"
#include "../renderer/MeshHandle.hpp"
#include "../geometry/PrimitiveBuilder.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

static const glm::vec3 kAxes[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
static constexpr int   kRingSeg = 40;
static constexpr float kTwoPi   = 6.28318530718f;

static const glm::vec4 kAxisColor[3] = {
    {0.90f, 0.20f, 0.20f, 1.f}, {0.30f, 0.85f, 0.25f, 1.f}, {0.25f, 0.45f, 1.0f, 1.f}
};
static const glm::vec4 kHovColor  { 1.f, 0.85f, 0.15f, 1.f };
static const glm::vec4 kWhiteCol  { 0.9f, 0.9f, 0.9f, 1.f };
static const glm::vec4 kBoxCol    { 1.f, 0.9f, 0.2f, 1.f };

// ── 공용 헬퍼 ────────────────────────────────────────────────────────────────
// 화면 투영·역투영은 WorldViewport::project / ray 로 단일화했습니다(오프셋 처리를 한 곳에).
// 기즈모와 픽(EditorApp::pickActor)이 같은 구현을 공유합니다.

static float segDist(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    glm::vec2 ab = b - a, ap = p - a;
    float l2 = glm::dot(ab, ab);
    if (l2 < 1e-6f) return glm::length(ap);
    return glm::length(ap - ab * glm::clamp(glm::dot(ap, ab) / l2, 0.f, 1.f));
}

static void planeBasis(const glm::vec3& n, glm::vec3& u, glm::vec3& v) {
    glm::vec3 ref = (std::abs(n.y) < 0.99f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    u = glm::normalize(glm::cross(ref, n));
    v = glm::cross(n, u);
}

// -Z(화살표 로컬 전방)를 dir 로 보내는 회전. 화살표 메시를 각 축으로 겨눕니다.
static glm::quat fromNegZTo(const glm::vec3& dir) {
    const glm::vec3 f(0.f, 0.f, -1.f);
    float d = glm::dot(f, dir);
    if (d >  0.9999f) return glm::quat(1,0,0,0);
    if (d < -0.9999f) return glm::angleAxis(3.14159265f, glm::vec3(0,1,0));
    glm::vec3 ax = glm::normalize(glm::cross(f, dir));
    return glm::angleAxis(std::acos(glm::clamp(d, -1.f, 1.f)), ax);
}

static void selectedAabb(Actor* a, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(-0.5f); hi = glm::vec3(0.5f);
    if (auto* sm = a->getComponent<StaticMeshComponent>(); sm && sm->mesh) {
        lo = sm->mesh->aabbMin;
        hi = sm->mesh->aabbMax;
    }
}

// 스칼라 양자화. step 이 0 이면 항등이라, 스냅 끄기를 조건 분기가 아니라 값으로
// 표현합니다(코드 경로가 한 벌이라 켠 쪽만 검증되고 끈 쪽이 썩는 일이 없습니다).
static inline float snapTo(float v, float step) {
    return step > 1e-6f ? std::roundf(v / step) * step : v;
}

// 드래그 전후 트랜스폼이 사실상 같은지. 회전은 q 와 -q 가 같은 회전이라 |dot| 로 봅니다.
static bool nearlyEqualT(const Transform& a, const Transform& b) {
    const glm::vec3 dp = a.position() - b.position();
    const glm::vec3 ds = a.scale()    - b.scale();
    if (glm::dot(dp, dp) > 1e-12f || glm::dot(ds, ds) > 1e-12f) return false;
    return std::abs(glm::dot(a.rotation(), b.rotation())) > 1.f - 1e-6f;
}

static glm::vec3 axisWorldDir(Actor* a, int i, bool worldSpace) {
    // kAxes 는 3원소입니다. 크기 모드의 균등 핸들은 axis_ == 3 이라, 드래그 중 모드가
    // 바뀌면 이동·회전 분기가 여기로 3 을 넘겨 범위 밖을 읽습니다. 호출부에서도 막지만
    // 생산자 쪽에서 한 번 더 잠급니다.
    i = std::clamp(i, 0, 2);
    if (worldSpace) return kAxes[i];
    return glm::normalize(a->worldRotation() * kAxes[i]);
}

// 화살표 메시는 한 번만 만들어 캐시합니다 (모든 액터가 공유).
static const MeshSourceData& arrowMesh() {
    static MeshSourceData mesh = [] {
        PrimitiveDesc d; d.shape = EPrimitiveShape::Arrow; d.segU = 16; d.ratio = 0.32f;
        return PrimitiveBuilder::build(d);
    }();
    return mesh;
}

// 화살표 메시를 model 로 변환해 색 col 의 삼각형으로 out 에 밀어 넣습니다.
static void emitArrow(std::vector<LineVertex>& out,
                      const glm::mat4& model, const glm::vec4& col) {
    const MeshSourceData& m = arrowMesh();
    out.reserve(out.size() + m.indices.size());
    for (uint32_t idx : m.indices) {
        const auto& v = m.vertices[idx];
        glm::vec3 p = glm::vec3(model * glm::vec4(v.pos[0], v.pos[1], v.pos[2], 1.f));
        out.push_back({ p, col });
    }
}

// 중심 c, 반쪽 크기 h 의 축 정렬 상자를 삼각형으로 밀어 넣습니다 (크기 핸들용).
static void emitBox(std::vector<LineVertex>& out, const glm::vec3& c,
                    float h, const glm::vec4& col) {
    glm::vec3 v[8];
    for (int k = 0; k < 8; ++k)
        v[k] = c + glm::vec3((k&1)?h:-h, (k&2)?h:-h, (k&4)?h:-h);
    static const int faces[6][4] = {
        {0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}
    };
    for (auto& f : faces) {
        out.push_back({v[f[0]],col}); out.push_back({v[f[1]],col}); out.push_back({v[f[2]],col});
        out.push_back({v[f[0]],col}); out.push_back({v[f[2]],col}); out.push_back({v[f[3]],col});
    }
}

// ── tick ───────────────────────────────────────────────────────────────────────
void EditorGizmo::tick(Actor* selected, World& world,
                       const WorldViewport& wvp, float hitRadius,
                       int mode, bool worldSpace, bool mouseInView,
                       std::vector<LineVertex>& lines, std::vector<LineVertex>& tris) {
    const glm::vec2&      mouse = wvp.mouseFb;
    const HViewportRect&  rect  = wvp.fbRect;
    if (!selected) { hovered_ = -1; return; }

    // ── 바운드 와이어 박스 (DEF-54) ──────────────────────────────────────────
    glm::vec3 lo, hi;
    selectedAabb(selected, lo, hi);
    const glm::mat4 wm = selected->worldMatrix();
    glm::vec3 c[8];
    for (int k = 0; k < 8; ++k) {
        glm::vec3 corner{ (k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z };
        c[k] = glm::vec3(wm * glm::vec4(corner, 1.f));
    }
    auto edge = [&](int a, int b){ lines.push_back({c[a],kBoxCol}); lines.push_back({c[b],kBoxCol}); };
    edge(0,1); edge(1,3); edge(3,2); edge(2,0);
    edge(4,5); edge(5,7); edge(7,6); edge(6,4);
    edge(0,4); edge(1,5); edge(2,6); edge(3,7);

    CameraComponent* cam = world.getActiveCamera();
    const bool haveCam = (mode >= 1 && mode <= 3) && cam && cam->owner &&
                         rect.width > 0.f && rect.height > 0.f;
    if (!haveCam) {
        if (!dragging_) hovered_ = -1;
        return;   // 바운드 박스만 남기고 종료 (lines 에 이미 덧붙였음)
    }

    const glm::vec3 pos  = selected->worldPosition();
    const float     dist = glm::length(cam->owner->worldPosition() - pos);
    const float     gsz  = std::max(0.02f, dist * sizeFactor_);
    const glm::mat4 vp   = cam->projMatrix(rect.width / rect.height) * cam->viewMatrix();

    glm::vec3 dir[3];
    for (int i = 0; i < 3; ++i) dir[i] = axisWorldDir(selected, i, worldSpace);

    // ── 호버 판정 (드래그 중이 아니고 마우스가 뷰포트 안일 때) ───────────────
    if (!dragging_ && !mouseInView) hovered_ = -1;
    if (!dragging_ && mouseInView) {
        hovered_ = -1;
        float best = hitRadius;
        const glm::vec2 s0 = wvp.project(vp,pos);

        if (mode == 1 || mode == 3) {
            for (int i = 0; i < 3; ++i) {
                glm::vec2 s1 = wvp.project(vp,pos + dir[i] * gsz);
                float d = segDist(mouse, s0, s1);
                if (d < best) { best = d; hovered_ = i; }
            }
            if (mode == 3) {
                float dc = glm::length(mouse - s0);
                if (dc < hitRadius * 1.3f && dc < best + 4.f) hovered_ = 3;
            }
        } else if (mode == 2) {
            for (int i = 0; i < 3; ++i) {
                glm::vec3 u, v; planeBasis(dir[i], u, v);
                glm::vec2 prev = wvp.project(vp,pos + u * gsz);
                float ringBest = 1e9f;
                for (int k = 1; k <= kRingSeg; ++k) {
                    float a = kTwoPi * (float)k / (float)kRingSeg;
                    glm::vec2 cur = wvp.project(vp,
                        pos + (u * std::cos(a) + v * std::sin(a)) * gsz);
                    ringBest = std::min(ringBest, segDist(mouse, prev, cur));
                    prev = cur;
                }
                if (ringBest < best) { best = ringBest; hovered_ = i; }
            }
        }
    }

    auto isActive = [&](int i){ return i == hovered_ || (dragging_ && axis_ == i); };
    auto colOf    = [&](int i){ return isActive(i) ? kHovColor : kAxisColor[i]; };

    // ── 핸들 그리기 ──────────────────────────────────────────────────────────
    if (mode == 1) {                    // 이동: 솔리드 화살표 3개
        for (int i = 0; i < 3; ++i) {
            glm::mat4 model = glm::translate(glm::mat4(1.f), pos)
                            * glm::mat4_cast(fromNegZTo(dir[i]))
                            * glm::scale(glm::mat4(1.f), glm::vec3(gsz));
            emitArrow(tris, model, colOf(i));
        }
    } else if (mode == 2) {             // 회전: 원호 3개 (라인)
        for (int i = 0; i < 3; ++i) {
            glm::vec4 col = colOf(i);
            glm::vec3 u, v; planeBasis(dir[i], u, v);
            glm::vec3 prev = pos + u * gsz;
            for (int k = 1; k <= kRingSeg; ++k) {
                float a = kTwoPi * (float)k / (float)kRingSeg;
                glm::vec3 cur = pos + (u * std::cos(a) + v * std::sin(a)) * gsz;
                lines.push_back({ prev, col }); lines.push_back({ cur, col });
                prev = cur;
            }
        }
    } else if (mode == 3) {             // 크기: 축 라인 + 끝 솔리드 상자 + 중앙 상자
        for (int i = 0; i < 3; ++i) {
            glm::vec3 tip = pos + dir[i] * gsz;
            glm::vec4 col = colOf(i);
            lines.push_back({ pos, col }); lines.push_back({ tip, col });
            emitBox(tris, tip, gsz * 0.06f, col);
        }
        glm::vec4 cc = (hovered_ == 3 || (dragging_ && axis_ == 3)) ? kHovColor : kWhiteCol;
        emitBox(tris, pos, gsz * 0.05f, cc);
    }

}

// ── handleDrag ─────────────────────────────────────────────────────────────────
bool EditorGizmo::handleDrag(Actor* selected, World& world, HCommandStack& cmdStack,
                             const WorldViewport& wvp, bool leftDown, bool prevLeftDown,
                             int mode, bool worldSpace, bool mouseInView) {
    const glm::vec2&     mouse = wvp.mouseFb;
    const HViewportRect& rect  = wvp.fbRect;
    if (!selected || mode < 1 || mode > 3) return false;

    CameraComponent* cam = world.getActiveCamera();
    const bool haveCam = cam && cam->owner && rect.width > 0.f && rect.height > 0.f;
    glm::mat4 vp = haveCam ? cam->projMatrix(rect.width / rect.height) * cam->viewMatrix()
                           : glm::mat4(1.f);

    bool consumed = false;

    // ── 드래그 시작 (마우스가 뷰포트 안이고 핸들 위일 때만) ──────────────────
    if (leftDown && !prevLeftDown && hovered_ >= 0 && haveCam && mouseInView) {
        dragging_   = true;
        axis_       = hovered_;
        undoSnap_   = selected->transform();
        prevMouse_  = mouse;
        startMouse_ = mouse;

        // 드래그 내내 고정할 기준값입니다. 매 프레임 현재 상태에서 다시 뽑으면, 스냅으로
        // 위치·크기가 계단처럼 튈 때 그 결과가 다시 감도를 바꾸는 되먹임이 생깁니다.
        dragAxisW_     = axisWorldDir(selected, axis_, worldSpace);   // DEF-51
        dragStartPosW_ = selected->worldPosition();
        accumMove_     = 0.f;
        dragReadout_   = 0.f;

        if (mode == 2) {
            planeBasis(dragAxisW_, planeU_, planeV_);
            glm::vec3 pos = selected->worldPosition();
            glm::vec3 ro, rd;
            prevAngle_  = 0.f;
            accumAngle_ = 0.f;
            if (wvp.ray(vp,ro, rd)) {
                float denom = glm::dot(rd, dragAxisW_);
                if (std::abs(denom) > 1e-5f) {
                    float t = glm::dot(pos - ro, dragAxisW_) / denom;
                    glm::vec3 p = ro + rd * t - pos;
                    prevAngle_ = std::atan2(glm::dot(p, planeV_), glm::dot(p, planeU_));
                }
            }
        }
        consumed = true;
    }

    // ── 드래그 진행 ──────────────────────────────────────────────────────────
    if (dragging_ && leftDown && haveCam) {
        glm::vec3 pos = selected->worldPosition();

        if (mode == 1) {                                       // ── 이동 ──
            // 축과 화면 투영을 시작 시점 값으로 고정합니다(위 주석 참고).
            glm::vec2 sa  = wvp.project(vp, dragStartPosW_ + dragAxisW_)
                          - wvp.project(vp, dragStartPosW_);
            float     sl2 = glm::dot(sa, sa);
            if (sl2 > 0.1f) {
                accumMove_ += glm::dot(mouse - prevMouse_, sa) / sl2;
                // 스냅 대상은 "축 위 이동 거리"입니다. 월드 좌표를 격자에 맞추는 방식은
                // 로컬 축이 임의 방향이라 축을 벗어나거나 값이 얼어붙습니다.
                // 프레임 델타에 바로 반올림을 걸면 한 프레임 이동량이 눈금보다 작아
                // 언제나 0 이 되므로, 누적값을 양자화해야 합니다.
                const float d = snapTo(accumMove_, snap_.enabled ? snap_.move : 0.f);
                glm::vec3 worldDel = dragAxisW_ * d;
                Actor* parent = selected->getParent();           // DEF-52
                glm::vec3 localDel = worldDel;
                if (parent)
                    localDel = glm::vec3(glm::inverse(parent->worldMatrix()) * glm::vec4(worldDel, 0.f));
                // 증분 누적이 아니라 시작 트랜스폼 기준의 절대 위치입니다. 축과 부모 행렬이
                // 드래그 중 상수이므로, 스냅이 꺼져 있으면 궤적은 기존 증분식과 같습니다.
                selected->setLocalPosition(undoSnap_.position() + localDel);
                dragReadout_ = d;
            }
        } else if (mode == 2) {                                // ── 회전 ──
            glm::vec3 ro, rd;
            if (wvp.ray(vp,ro, rd)) {
                float denom = glm::dot(rd, dragAxisW_);
                if (std::abs(denom) > 1e-5f) {
                    float t = glm::dot(pos - ro, dragAxisW_) / denom;
                    glm::vec3 p = ro + rd * t - pos;
                    float ang = std::atan2(glm::dot(p, planeV_), glm::dot(p, planeU_));
                    float d = ang - prevAngle_;
                    while (d >  3.14159265f) d -= kTwoPi;
                    while (d < -3.14159265f) d += kTwoPi;
                    accumAngle_ += d;
                    prevAngle_   = ang;

                    // accumAngle_ 원본은 건드리지 않습니다. prevAngle_ 과 함께 누적
                    // 기준이라, 스냅값을 되쓰면 되감김과 히스테리시스가 생깁니다.
                    // 쿼터니언을 오일러로 풀어 반올림하는 방식도 금물입니다(짐벌 구간에서
                    // 표현이 유일하지 않아 축이 뒤집힙니다).
                    const float snapAng = snapTo(accumAngle_,
                        snap_.enabled ? glm::radians(snap_.rotateDeg) : 0.f);
                    dragReadout_ = glm::degrees(snapAng);
                    glm::quat dq = glm::angleAxis(snapAng, dragAxisW_);   // DEF-51
                    Actor*    parent = selected->getParent();
                    glm::quat pWorld = parent ? parent->worldRotation() : glm::quat(1,0,0,0);
                    glm::quat newLocal = glm::inverse(pWorld) * dq * pWorld * undoSnap_.rotation();
                    selected->setLocalRotation(glm::normalize(newLocal));
                }
            }
        } else if (mode == 3) {                                // ── 크기 (언제나 로컬) ──
            const glm::vec3 s0 = undoSnap_.scale();
            const float     st = snap_.enabled ? snap_.scale : 0.f;
            if (axis_ == 3) {
                const float amount = (startMouse_.y - mouse.y) / 120.f;
                // 균등 핸들의 계약은 비율 보존이라 배율 쪽을 양자화합니다. 세 성분을 각각
                // 스냅하면 시작 스케일이 비균등일 때 그 균등성이 깨집니다.
                const float factor = std::max(0.05f, snapTo(1.f + amount, st));
                selected->setLocalScale(s0 * factor);
                dragReadout_ = factor;
            } else {
                glm::vec3 dirW = glm::normalize(selected->worldRotation() * kAxes[axis_]);
                // 기준 길이는 **매 프레임** 카메라 거리로 잽니다. 길이가 거리에 비례하면
                // 화면 투영 길이에서 거리가 상쇄되어 감도가 거리에 무관해집니다. 시작
                // 시점에 고정하면 오히려 반대로, 드래그 중 휠 돌리가 배율을 끌고 갑니다.
                float refLen = std::max(0.02f,
                    glm::length(cam->owner->worldPosition() - pos) * sizeFactor_);
                glm::vec2 sa  = wvp.project(vp, pos + dirW * refLen)
                              - wvp.project(vp, pos);
                float     sl2 = glm::dot(sa, sa);
                if (sl2 > 0.1f) {
                    const float amount = glm::dot(mouse - startMouse_, sa) / sl2;
                    // 균등 핸들과 같이 **배율**을 양자화합니다. 최종 절대 스케일을
                    // 양자화하면 (a) 시작 스케일이 눈금 위에 없을 때 잡기만 해도 값이 튀어
                    // 빈 undo 가 쌓이고, (b) 음수(미러) 스케일이 하한에 걸려 부호를 잃습니다.
                    const float factor = std::max(0.05f, snapTo(1.f + amount, st));
                    glm::vec3 ns = s0;
                    ns[axis_] = s0[axis_] * factor;
                    selected->setLocalScale(ns);
                    // 균등 핸들과 같은 양(배율)을 보고합니다. 절대 스케일을 보고하면
                    // 시작 스케일이 1 이 아닐 때 스텝이 눈금과 달라 보여, 스냅이 걸리는지
                    // 확인시킨다는 리드아웃의 목적이 무너집니다.
                    dragReadout_ = factor;
                }
            }
        }

        prevMouse_ = mouse;
        consumed   = true;
    } else if (dragging_ && leftDown) {
        consumed = true;
    }

    // ── 드래그 종료 ──────────────────────────────────────────────────────────
    if (dragging_ && !leftDown) {
        const Transform after = selected->transform();
        // 잡았다가 그대로 놓은 드래그로 64칸 커맨드 스택이 빈 이력에 차지 않게 합니다.
        if (!nearlyEqualT(undoSnap_, after))
            cmdStack.push(std::make_unique<SetTransformCmd>(selected, undoSnap_, after));
        dragging_ = false;
        axis_     = -1;
        accumMove_ = accumAngle_ = dragReadout_ = 0.f;
        // 종료 프레임의 마우스-업을 여기서 소비합니다. 그러지 않으면 릴리즈가 슬레이트로
        // 흘러가, 커서 아래에 있던 위젯(뷰포트 툴바 버튼 등)이 함께 발화합니다.
        consumed  = true;
    }

    return consumed;
}
