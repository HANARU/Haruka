#pragma once
#include "../HWidget.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

// ── 속성 행 (H3a, 2026-11-22) ───────────────────────────────────────────────────────────────────────────────
// 디테일 패널의 한 줄입니다(UE 의 SDetailSingleItemRow 를 줄인 것): [들여쓰기][이름 열] | [값 열][되돌리기 자리].
//   - 이름 열 폭은 HPropertyColumns 하나를 패널 전체가 함께 씁니다. 어느 행이든 열 나눔선을 끌면 모든 행의 나눔선이 같이 움직입니다
//     (값은 비율이라 패널 폭이 바뀌어도 같은 자리). 나눔 자리는 들여쓰기와 상관없이 행 왼쪽 끝 기준이라 행마다 줄이 맞습니다.
//   - 되돌리기 자리는 늘 비워 둡니다(화살표가 나타나도 값 칸 폭이 그대로). 화살표를 보일지는 그 위젯의 visibility 가 정합니다.
//   - 포인터가 행 위에 있으면 바탕을 밝게(hover) 그립니다. 이름 열은 그 폭으로 잘라 그립니다.
//   - 누름 순서: 나눔선(±hit px) → 값 · 되돌리기 · 이름. 나눔선을 끄는 동안은 포인터를 잡습니다.
// 이 파일은 ui 모듈이라 에디터 색을 모릅니다. 색은 Style 로 받습니다(에디터는 EditorUiKit::propertyRowStyle).
struct HPropertyColumns {
    float nameFraction = 0.4f;    // 이름 열 폭 / 나눌 폭(행 폭 - 되돌리기 자리)
    float minNamePx    = 48.f;
    float minValuePx   = 64.f;
    uint64_t changes   = 0;       // 끌어서 바꾼 횟수(시험 · 저장용)

    // 행 왼쪽 끝에서 나눔선까지(px). width = 나눌 폭.
    float splitPx(float width) const {
        const float lo = std::min(minNamePx, width * 0.5f);
        const float hi = std::max(lo, width - minValuePx);
        return std::clamp(nameFraction * width, lo, hi);
    }
    // 끌기: 행 왼쪽 끝에서 x px 자리에 나눔선을 둡니다(범위 안으로 자름).
    void dragTo(float x, float width) {
        if (width <= 0.f) return;
        const float lo = std::min(minNamePx, width * 0.5f);
        const float hi = std::max(lo, width - minValuePx);
        const float f  = std::clamp(x, lo, hi) / width;
        if (f != nameFraction) { nameFraction = f; ++changes; hslate::noteLayoutChange(); }
    }
};

class SPropertyRow : public HWidget {
public:
    struct Style {
        glm::vec4 hover      { 0.16f, 0.16f, 0.18f, 1.f };
        glm::vec4 divider    { 0.10f, 0.10f, 0.11f, 1.f };
        glm::vec4 dividerHot { 0.30f, 0.45f, 0.80f, 1.f };
        float     dividerHitPx = 4.f;   // 나눔선 좌우로 이만큼이 끌기 자리
        float     gapPx        = 6.f;   // 이름 열 끝 ~ 값 열 시작
    };

    std::shared_ptr<SPropertyRow> columns(std::shared_ptr<HPropertyColumns> c) { cols_ = std::move(c); hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPropertyRow> name(std::shared_ptr<HWidget> w)   { name_  = std::move(w); hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPropertyRow> value(std::shared_ptr<HWidget> w)  { value_ = std::move(w); hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPropertyRow> reset(std::shared_ptr<HWidget> w)  { reset_ = std::move(w); hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPropertyRow> resetWidth(float px)               { resetW_ = px; hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPropertyRow> indent(float px)                   { indent_ = px; hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPropertyRow> rightPad(float px)                 { rightPad_ = px; hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPropertyRow> valueVPad(float px)                { vpad_ = px; hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPropertyRow> height(float px)                   { height_ = px; hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SPropertyRow> style(const Style& s)              { style_ = s; return self(); }

    const std::shared_ptr<HPropertyColumns>& columnState() const { return cols_; }
    const std::shared_ptr<HWidget>& nameWidget() const  { return name_; }
    const std::shared_ptr<HWidget>& valueWidget() const { return value_; }
    const std::shared_ptr<HWidget>& resetWidget() const { return reset_; }
    bool hovered() const { return hovered_; }
    bool draggingDivider() const { return dragging_; }
    // 지금 배치의 나눔선 x(문맥 좌표).
    float dividerX() const { return cachedGeo_.position.x + splitPx(); }

    glm::vec2 computeDesiredSize() override {
        float h = height_;
        float w = indent_ + resetW_ + rightPad_ + style_.gapPx;
        if (name_)  { const glm::vec2 d = name_->desiredSize();  w += d.x; h = std::max(h, d.y); }
        if (value_) { const glm::vec2 d = value_->desiredSize(); w += d.x; h = std::max(h, d.y + vpad_ * 2.f); }
        if (reset_) reset_->desiredSize();
        return { w, h };
    }

    void onArrangeChildren(const HGeometry& a) override {
        cachedGeo_ = a;
        const float split = splitPx();
        const float x0 = indent_;
        const float nameW  = std::max(0.f, split - x0 - style_.gapPx * 0.5f);
        const float valueX = split + style_.gapPx * 0.5f;
        const float valueW = std::max(0.f, splitWidth() - valueX);
        if (name_) {
            const glm::vec2 d = name_->desiredSize();
            const float h = std::min(d.y, a.size.y);
            name_->onArrangeChildren(a.makeChild({ x0, (a.size.y - h) * 0.5f }, { nameW, h }));
        }
        if (value_) value_->onArrangeChildren(a.makeChild({ valueX, vpad_ }, { valueW, std::max(0.f, a.size.y - vpad_ * 2.f) }));
        if (reset_) {
            const float s = std::min(resetW_, a.size.y);
            reset_->onArrangeChildren(a.makeChild({ splitWidth() + (resetW_ - s) * 0.5f, (a.size.y - s) * 0.5f }, { s, s }));
        }
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        if ((hovered_ || dragging_) && style_.hover.a > 0.f) ctx.drawRect(geo, style_.hover);
        if (name_ && name_->isVisible()) {
            HGeometry clip = geo;
            clip.size.x = std::max(0.f, splitPx() - style_.gapPx * 0.5f);
            ctx.pushClip(clip);
            name_->paint(ctx, name_->cachedGeometry());
            ctx.popClip();
        }
        // 나눔선: 1px. 그 위에 포인터가 있거나 끄는 중이면 강조색.
        const glm::vec4 dc = (dragging_ || hotDivider_) ? style_.dividerHot : style_.divider;
        if (dc.a > 0.f) {
            HGeometry dg = geo;
            dg.position.x = geo.position.x + splitPx() - 0.5f;
            dg.size.x     = 1.f;
            ctx.drawRect(dg, dc);
        }
        if (value_ && value_->isVisible()) value_->paint(ctx, value_->cachedGeometry());
        if (reset_ && reset_->isVisible()) reset_->paint(ctx, reset_->cachedGeometry());
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button != 0) return HReply::Unhandled();
        if (e.down) {
            if (cols_ && cachedGeo_.contains(e.position) && overDivider(e.position)) {
                dragging_ = true;
                return HReply::Handled().captureMouse(this);
            }
        } else if (dragging_) {
            dragging_ = false;
            return HReply::Handled().releaseMouse();
        }
        for (const auto& w : { reset_, value_, name_ }) {
            if (!w || !w->isVisible()) continue;
            if (w->cachedGeometry().contains(e.position))
                if (auto r = w->onMouseButton(e); r.handled) return r;
        }
        return HReply::Unhandled();
    }

    HReply onMouseMove(const HPointerEvent& e) override {
        hovered_    = cachedGeo_.contains(e.position);
        hotDivider_ = hovered_ && cols_ && overDivider(e.position);
        if (dragging_) {
            if (!e.down) { dragging_ = false; return HReply::Handled().releaseMouse(); }
            if (cols_) cols_->dragTo(e.position.x - cachedGeo_.position.x, splitWidth());
            return HReply::Handled();
        }
        HReply ret = HReply::Unhandled();
        for (const auto& w : { name_, value_, reset_ })
            if (w) ret.merge(w->onMouseMove(e));
        return ret;
    }
    HReply onScroll(const HPointerEvent& e) override {
        for (const auto& w : { reset_, value_, name_ }) {
            if (!w || !w->isVisible()) continue;
            if (w->cachedGeometry().contains(e.position))
                if (auto r = w->onScroll(e); r.handled) return r;
        }
        return HReply::Unhandled();
    }
    std::shared_ptr<HWidget> findTooltipWidget(glm::vec2 pos) override {
        if (!isVisible() || !cachedGeo_.contains(pos)) return nullptr;
        for (const auto& w : { reset_, value_, name_ })
            if (w) if (auto t = w->findTooltipWidget(pos)) return t;
        return tooltip.get().empty() ? nullptr : shared_from_this();
    }
    void forEachChild(const std::function<void(const std::shared_ptr<HWidget>&)>& fn) override {
        if (name_)  fn(name_);
        if (value_) fn(value_);
        if (reset_) fn(reset_);
    }

private:
    float splitWidth() const { return std::max(0.f, cachedGeo_.size.x - resetW_ - rightPad_); }
    float splitPx() const {
        const float w = splitWidth();
        return cols_ ? cols_->splitPx(w) : w * 0.4f;
    }
    bool overDivider(glm::vec2 p) const { return std::abs(p.x - dividerX()) <= style_.dividerHitPx; }
    std::shared_ptr<SPropertyRow> self() { return std::static_pointer_cast<SPropertyRow>(shared_from_this()); }

    std::shared_ptr<HPropertyColumns> cols_;
    std::shared_ptr<HWidget> name_, value_, reset_;
    float resetW_   = 18.f;
    float indent_   = 12.f;
    float rightPad_ = 4.f;
    float vpad_     = 1.f;
    float height_   = 24.f;
    Style style_;
    bool  hovered_    = false;
    bool  hotDivider_ = false;
    bool  dragging_   = false;
};

// ── 색 견본 (H3a) ─────────────────────────────────────────────────────────────────────────────────────────────
// 지금 색을 채운 사각형입니다. 색은 렌더러에 넘기는 선형 값입니다(sRGB 로 저장한 값은 부르는 쪽이 바꿔 줌). 알파가 1 보다 작으면
// 오른쪽 절반을 알파 그대로, 왼쪽 절반을 불투명으로 그려 차이가 보이게 합니다(UE 의 색 견본과 같은 생각). 누르면 onClick(H3b 의
// 색 선택기 자리, 지금은 비어 있어도 됨).
class SColorSwatch : public HWidget {
public:
    std::shared_ptr<SColorSwatch> color(std::function<glm::vec4()> fn) { color_ = std::move(fn); return self(); }
    std::shared_ptr<SColorSwatch> border(glm::vec4 c, float w)          { border_ = c; borderW_ = w; return self(); }
    std::shared_ptr<SColorSwatch> backdrop(glm::vec4 c)                 { backdrop_ = c; return self(); }
    std::shared_ptr<SColorSwatch> cornerRadius(float r)                 { radius_ = r; return self(); }
    std::shared_ptr<SColorSwatch> onClick(std::function<void()> fn)     { onClick_ = std::move(fn); return self(); }
    std::shared_ptr<SColorSwatch> size(glm::vec2 s)                     { size_ = s; hslate::noteLayoutChange(); return self(); }

    glm::vec4 currentColor() const { return color_ ? color_() : glm::vec4(0.f, 0.f, 0.f, 1.f); }

    glm::vec2 computeDesiredSize() override { return size_; }
    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        const glm::vec4 c = currentColor();
        const glm::vec4 opaque{ c.r, c.g, c.b, 1.f };
        ctx.drawRoundedRect(geo, backdrop_, radius_, border_, borderW_);
        HGeometry in = geo;
        const float inset = borderW_ + 1.f;
        in.position += glm::vec2(inset);
        in.size = glm::max(glm::vec2(0.f), geo.size - glm::vec2(inset * 2.f));
        if (c.a >= 1.f) {
            ctx.drawRect(in, opaque);
        } else {
            HGeometry l = in, r = in;
            l.size.x = in.size.x * 0.5f;
            r.position.x = in.position.x + l.size.x;
            r.size.x = in.size.x - l.size.x;
            ctx.drawRect(l, opaque);
            ctx.drawRect(r, c);
        }
    }
    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button != 0) return HReply::Unhandled();
        if (e.down) { armed_ = cachedGeo_.contains(e.position); return HReply::Handled(); }
        if (armed_ && cachedGeo_.contains(e.position) && onClick_) onClick_();
        armed_ = false;
        return HReply::Handled();
    }

private:
    std::shared_ptr<SColorSwatch> self() { return std::static_pointer_cast<SColorSwatch>(shared_from_this()); }
    std::function<glm::vec4()> color_;
    std::function<void()>      onClick_;
    glm::vec4 border_   { 0.3f, 0.3f, 0.32f, 1.f };
    glm::vec4 backdrop_ { 0.05f, 0.05f, 0.05f, 1.f };
    float     borderW_ = 1.f;
    float     radius_  = 2.f;
    glm::vec2 size_ { 24.f, 18.f };
    bool      armed_ = false;
};
