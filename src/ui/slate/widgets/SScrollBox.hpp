#pragma once
#include "../HWidget.hpp"
#include <algorithm>
#include <cmath>

// ── SScrollBox ────────────────────────────────────────────────────────────────
// 세로 스크롤 영역입니다. 자식을 원하는 높이로 배치해 스크롤 위치만큼 위로 밀어 보여 주고,
// 영역 밖은 잘라 냅니다(DEF-56). 휠은 자식이 먼저 받고(안쪽 스크롤 우선), 자식이 받지 않으면
// 이 상자가 스크롤합니다(DEF-57). 내용이 영역보다 크면 오른쪽에 위치 표시 막대를 그립니다
// (막대를 끌어 스크롤하는 기능은 없습니다).
//
// 내용을 다시 만들어도 이 상자를 다시 만들지 않으면 스크롤 위치가 남습니다. 내용이 줄면 배치할
// 때 위치를 범위 안으로 다시 당깁니다. 자식은 영역보다 작아도 영역 높이로 배치되므로, 자식
// 안의 Fill 슬롯은 넘치지 않을 때 보통 컨테이너 안에서와 같게 동작합니다. 넘칠 때는 오른쪽에
// 막대 자리를 비우고 그만큼 좁게 배치해, 막대가 내용의 오른쪽 끝(삭제 x 등)을 덮지 않게 합니다.
class SScrollBox : public HWidget {
public:
    std::shared_ptr<SScrollBox> content(std::shared_ptr<HWidget> w) {
        child_ = std::move(w); return self();
    }
    // 휠 한 칸에 움직일 거리(px)입니다. 터치패드의 작은 휠 값은 그 비율만큼 움직입니다.
    std::shared_ptr<SScrollBox> wheelStep(float px) {
        step_ = px; return self();
    }
    std::shared_ptr<SScrollBox> barWidth(float px) {
        barW_ = px; return self();
    }

    float scrollOffset()    const { return offset_; }
    float maxScrollOffset() const { return maxOffset_; }   // 마지막 배치 기준
    void  setScrollOffset(float y) { offset_ = std::clamp(y, 0.f, maxOffset_); }
    void  scrollToTop() { offset_ = 0.f; }

    glm::vec2 computeDesiredSize() override {
        return child_ ? child_->computeDesiredSize() : glm::vec2(0.f);
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
        if (!child_) { offset_ = maxOffset_ = 0.f; return; }
        const float contentH = child_->computeDesiredSize().y;
        maxOffset_ = std::max(0.f, contentH - allotted.size.y);
        offset_    = std::clamp(offset_, 0.f, maxOffset_);
        // 글자가 픽셀 경계에 놓이도록 정수 픽셀로 밉니다(소수 위치는 글리프가 번집니다).
        const float shift  = std::floor(offset_ + 0.5f);
        const float gutter = (maxOffset_ > 0.f) ? std::min(barW_ + 4.f, allotted.size.x) : 0.f;
        child_->onArrangeChildren(allotted.makeChild(
            { 0.f, -shift }, { allotted.size.x - gutter, std::max(contentH, allotted.size.y) }));
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        if (child_ && child_->isVisible()) {
            ctx.pushClip(geo);
            child_->paint(ctx, child_->cachedGeometry());
            ctx.popClip();
        }
        if (maxOffset_ > 0.f && geo.size.y > 0.f) {
            // 위치 표시 막대. 보이는 비율만큼의 길이를 스크롤 위치에 맞춰 둡니다.
            const float viewH  = geo.size.y;
            const float thumbH = std::max(viewH * viewH / (viewH + maxOffset_), std::min(viewH, 20.f));
            const float thumbY = (viewH - thumbH) * (offset_ / maxOffset_);
            const float x      = geo.size.x - barW_ - 2.f;
            ctx.drawRect(geo.makeChild({ x, 0.f },    { barW_, viewH }),  { 1.f, 1.f, 1.f, 0.04f });
            ctx.drawRect(geo.makeChild({ x, thumbY }, { barW_, thumbH }), { 1.f, 1.f, 1.f, 0.22f });
        }
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (!child_ || !cachedGeo_.contains(e.position)) return HReply::Unhandled();
        return child_->onMouseButton(e);
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        if (!child_) return HReply::Unhandled();
        // 영역 밖의 이동은 "바깥" 으로 알립니다. 위로 밀려 잘린 행이 그 자리(패널 머리 등)에서
        // 호버를 켜지 않게 합니다.
        return child_->onMouseMove(cachedGeo_.contains(e.position) ? e : HOutsideEvent(e));
    }
    HReply onScroll(const HPointerEvent& e) override {
        if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
        if (child_)
            if (auto r = child_->onScroll(e); r.handled) return r;
        if (maxOffset_ <= 0.f) return HReply::Unhandled();   // 넘치지 않으면 바깥이 받게 둡니다
        // 휠을 위로(양수) 굴리면 내용이 내려와 위쪽이 보입니다.
        offset_ = std::clamp(offset_ - e.wheelDelta * step_, 0.f, maxOffset_);
        return HReply::Handled();
    }

private:
    std::shared_ptr<HWidget> child_;
    float offset_    = 0.f;
    float maxOffset_ = 0.f;
    float step_      = 48.f;
    float barW_      = 4.f;

    std::shared_ptr<SScrollBox> self() {
        return std::static_pointer_cast<SScrollBox>(shared_from_this());
    }
};
