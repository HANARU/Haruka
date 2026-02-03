#pragma once
#include "HGeometry.hpp"
#include "HAttr.hpp"
#include "HPaintContext.hpp"
#include <memory>

// ── HWidget (Slate의 SWidget) ─────────────────────────────────────────────────
// 모든 위젯의 베이스. enable_shared_from_this로 메서드 체인 지원.
class HWidget : public std::enable_shared_from_this<HWidget> {
public:
    virtual ~HWidget() = default;

    // ── 레이아웃 패스 ─────────────────────────────────────────────────────────
    // 하위→상위: 이 위젯이 원하는 크기를 반환
    virtual glm::vec2 computeDesiredSize() = 0;
    // 상위→하위: 실제 배치 크기가 확정됐을 때 호출 (children 배치)
    virtual void      onArrangeChildren(const HGeometry& allotted) { cachedGeo_ = allotted; }

    // ── 페인팅 패스 ───────────────────────────────────────────────────────────
    virtual void paint(HPaintContext& ctx, const HGeometry& geo) = 0;

    // ── 이벤트 (기본: Unhandled) ──────────────────────────────────────────────
    virtual HReply onMouseButton(const HPointerEvent& e) { return HReply::Unhandled(); }
    virtual HReply onMouseMove  (const HPointerEvent& e) { return HReply::Unhandled(); }
    virtual HReply onScroll     (const HPointerEvent& e) { return HReply::Unhandled(); }

    // ── 공통 속성 ─────────────────────────────────────────────────────────────
    Attr<EVisibility> visibility { EVisibility::Visible };
    Attr<bool>        enabled    { true };
    Attr<glm::vec2>   minSize    { glm::vec2(0.f) };

    // 마지막으로 배치된 Geometry (hit testing용)
    const HGeometry& cachedGeometry() const { return cachedGeo_; }

    bool isVisible() const { return visibility.get() == EVisibility::Visible; }
    bool isCollapsed() const { return visibility.get() == EVisibility::Collapsed; }

protected:
    HGeometry cachedGeo_;
};

// ── 편의 매크로 ───────────────────────────────────────────────────────────────
// HNew(SButton)->text("OK")->onClick([]{}) 스타일로 사용
#define HNew(T) (std::make_shared<T>())
