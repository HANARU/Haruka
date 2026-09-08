#pragma once
#include "../core/Transform.hpp"
#include "../renderer/RenderTypes.hpp"
#include <vector>
#include <cmath>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

class Actor;
class World;
class Renderer;
class HCommandStack;

// 패널을 제외한 3D 뷰포트 영역 (스케일 적용된 화면 px). UI 히트 판정과 게이팅 전용.
struct HViewportRect {
    float left = 0.f, top = 0.f, width = 0.f, height = 0.f;

    bool contains(float x, float y) const {
        return x >= left && x < left + width &&
               y >= top  && y < top  + height;
    }
};

// ── HGizmoSnap ────────────────────────────────────────────────────────────────
// 기즈모 스냅 설정입니다. 값은 1 unit = 1 meter 규약 기준이며, 스냅이 꺼져 있으면
// 양자화 함수에 0 을 넘겨 항등으로 처리합니다(조건 분기로 코드를 두 벌 만들지 않습니다).
struct HGizmoSnap {
    bool  enabled   = false;
    float move      = 0.25f;   // m
    float rotateDeg = 15.f;    // deg
    float scale     = 0.25f;   // 배율 또는 절대 스케일의 눈금
};

// ── WorldViewport ─────────────────────────────────────────────────────────────
// 마우스↔월드 변환에 필요한 좌표와 **투영 수학**을 하나로 묶습니다. **이 구조체를
// 통해서만** 월드 변환을 하게 하여, 좌표 혼용과 오프셋 누락을 원천 차단합니다. 유일한
// 생산자는 EditorApp::worldVP 이고, project()/ray() 가 기즈모·픽 공용 단일 구현입니다.
//
// 씬은 뷰포트 서브렉트에 그려지므로(Viewport_Render_Design.md), fbRect 는
// {viewportOffset, renderExtent}(프레임버퍼 픽셀)이고 mouseFb 는 스왑체인 프레임버퍼
// 픽셀 마우스입니다. project()/ray() 가 fbRect.left/top 오프셋을 일관되게 처리합니다.
struct WorldViewport {
    HViewportRect fbRect;    // {viewportOffset.x, viewportOffset.y, renderExtent.w, renderExtent.h}
    glm::vec2     mouseFb{}; // 스왑체인 프레임버퍼 픽셀 마우스
    float aspect() const { return fbRect.height > 0.f ? fbRect.width / fbRect.height : 1.f; }

    // 월드 좌표 → 화면(스왑체인 fb) 픽셀. fbRect.left/top 오프셋을 포함해, 씬이 뷰포트
    // 서브렉트에 합성되는 위치와 정확히 일치합니다.
    glm::vec2 project(const glm::mat4& vp, const glm::vec3& wp) const {
        glm::vec4 clip = vp * glm::vec4(wp, 1.f);
        if (clip.w <= 0.f) return { -9999.f, -9999.f };
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return { (ndc.x + 1.f) * 0.5f * fbRect.width  + fbRect.left,
                 (ndc.y + 1.f) * 0.5f * fbRect.height + fbRect.top };
    }

    // mouseFb → 월드 광선(project 의 정확한 역변환). 실패 시 false.
    bool ray(const glm::mat4& vp, glm::vec3& outO, glm::vec3& outD) const {
        glm::mat4 inv  = glm::inverse(vp);
        glm::vec2 frac = (mouseFb - glm::vec2(fbRect.left, fbRect.top))
                       / glm::vec2(fbRect.width, fbRect.height);
        glm::vec2 ndc  = frac * 2.f - 1.f;
        glm::vec4 pN = inv * glm::vec4(ndc, 0.f, 1.f);
        glm::vec4 pF = inv * glm::vec4(ndc, 1.f, 1.f);
        if (std::abs(pN.w) < 1e-6f || std::abs(pF.w) < 1e-6f) return false;
        glm::vec3 o = glm::vec3(pN) / pN.w;
        glm::vec3 f = glm::vec3(pF) / pF.w;
        glm::vec3 d = f - o;
        if (glm::dot(d, d) < 1e-12f) return false;
        outO = o; outD = glm::normalize(d);
        return true;
    }
};

// ── EditorGizmo ───────────────────────────────────────────────────────────────
// 선택 액터의 이동 기즈모: 와이어 박스 + 축 라인 렌더링, 축 호버 판정,
// 축 구속 드래그 이동과 undo 커맨드 기록까지 담당한다.
class EditorGizmo {
public:
    // 호버 판정 + 기즈모/하이라이트 라인 생성. 매 프레임 호출.
    // hitRadius: 축 호버 허용 거리(px, uiScale 적용된 값)
    // mode: 0=select 1=move 2=rotate 3=scale. worldSpace: 축을 월드로 그릴지 로컬로 그릴지.
    // wvp 는 프레임버퍼 기준 좌표를 담습니다(WorldViewport 주석 참조). mouseInView 가
    // false 면 호버를 잡지 않습니다.
    //
    // 렌더러에 직접 그리지 않고 outLines/outTris 에 **덧붙입니다.** 에디터가 아이콘 등
    // 다른 오버레이와 합쳐 한 번에 설정하기 위해서입니다.
    void tick(Actor* selected, World& world,
              const WorldViewport& wvp, float hitRadius,
              int mode, bool worldSpace, bool mouseInView,
              std::vector<LineVertex>& outLines, std::vector<LineVertex>& outTris);

    // 기즈모 크기 배율 (카메라 거리에 곱해 화면상 크기를 정합니다). 설정에서 조정 가능.
    void  setSizeFactor(float f) { sizeFactor_ = f; }
    float sizeFactor() const     { return sizeFactor_; }

    // 스냅 설정. 소유자는 EditorApp 이고 여기에는 매 프레임 사본이 들어옵니다
    // (tick/handleDrag 시그니처가 이미 길어 setSizeFactor 선례를 따릅니다).
    void              setSnap(const HGizmoSnap& s) { snap_ = s; }
    const HGizmoSnap& snap() const                 { return snap_; }

    // 드래그 중이면 축과 현재 값(이동=m, 회전=deg, 크기=배율 또는 절대 스케일)을 돌려줍니다.
    // 툴바가 이 값을 읽어, 스냅이 실제로 걸리고 있는지 눈으로 확인할 수 있게 합니다.
    bool dragReadout(int& outAxis, float& outValue) const {
        if (!dragging_) return false;
        outAxis  = axis_;
        outValue = dragReadout_;
        return true;
    }

    // 드래그 시작/이동 처리. 이벤트를 소비했으면 true.
    // (선택 가능 여부 — 에디터 카메라 제외 등 — 은 호출자가 가드)
    bool handleDrag(Actor* selected, World& world, HCommandStack& cmdStack,
                    const WorldViewport& wvp, bool leftDown, bool prevLeftDown,
                    int mode, bool worldSpace, bool mouseInView);

    bool isDragging() const { return dragging_; }

    // 선택 삭제 등으로 드래그/호버 상태를 강제 해제
    void cancel() {
        dragging_ = false; hovered_ = -1; axis_ = -1;
        accumMove_ = accumAngle_ = dragReadout_ = 0.f;
        dragStartPosW_ = glm::vec3(0.f);
    }

private:
    // hovered_/axis_: 0=X, 1=Y, 2=Z, 3=중앙(크기 균등). -1=없음.
    int       hovered_  = -1;
    bool      dragging_ = false;
    int       axis_     = -1;
    glm::vec2 prevMouse_{};    // 이동 드래그의 이전 프레임 마우스
    glm::vec2 startMouse_{};   // 회전/크기 드래그 시작 마우스
    Transform undoSnap_{};

    // 회전 드래그 상태 — 시작 시점에 고정합니다.
    glm::vec3 dragAxisW_{0.f}; // 회전 축 (월드 공간)
    glm::vec3 planeU_{0.f};    // 회전 평면 기저 벡터
    glm::vec3 planeV_{0.f};
    float     prevAngle_  = 0.f;   // 직전 프레임의 각도 (증분 누적용)
    float     accumAngle_ = 0.f;   // 시작 이후 누적 회전각 (2pi 감김을 흡수)
    float     sizeFactor_ = 0.12f; // 카메라 거리에 곱하는 기즈모 크기 배율

    // ── 스냅 (Phase 2) ────────────────────────────────────────────────────────
    HGizmoSnap snap_{};
    // 이동은 축 위 "누적 이동 거리"를 양자화합니다. 프레임 델타에 바로 반올림을 걸면
    // 한 프레임 이동량이 스텝보다 작아 언제나 0 이 되어 기즈모가 멈춥니다.
    float      accumMove_    = 0.f;
    glm::vec3  dragStartPosW_{0.f};  // 축의 화면 투영을 시작 위치에 고정(감도 흔들림 방지)
    float      dragReadout_  = 0.f;  // 툴바에 보여 줄 현재 값
};
