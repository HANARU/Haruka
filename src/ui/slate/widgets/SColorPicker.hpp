#pragma once
#include "../HWidget.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

// ── 색 선택기 조각 (H3b, 2026-11-22) ───────────────────────────────────────────────────────────────────────────
// ui 모듈이라 에디터 · 색 공간 규칙을 모릅니다. 칠할 색은 부르는 쪽이 colorAt 으로 줍니다(렌더러에 넘길 선형 RGBA).
//   SColorPlane : 2차원 판(채도 · 명도 사각형). 칸을 cells × cells 로 나눠 칸마다 네 모서리 색을 잇는 그라데이션으로 칠합니다
//                 (선형 보간이 색 공간 곡선과 어긋나는 것을 칸 크기만큼으로 줄임). 누르고 끌면 (u, v)(0 ~ 1, 왼쪽 위 = 0, 0)를 알림.
//   SColorBar   : 1차원 막대(색상 · 알파). vertical 이면 위 = 0. checker 가 켜지면 바둑판 바탕(알파 막대).
// 둘 다 누르면 포인터를 잡고, 떼면 놓으며 onEnd 를 부릅니다. 표시점은 marker 함수가 정합니다.

namespace hslate_color {
// 바둑판(알파 바탕). 칸 크기 px, 두 색.
inline void drawChecker(HPaintContext& ctx, const HGeometry& geo, float cell, glm::vec4 a, glm::vec4 b) {
    ctx.drawRect(geo, a);
    if (cell < 1.f) return;
    const int nx = (int)std::ceil(geo.size.x / cell), ny = (int)std::ceil(geo.size.y / cell);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            if (((i + j) & 1) == 0) continue;
            const float x = i * cell, y = j * cell;
            ctx.drawRect(geo.makeChild({ x, y }, { std::min(cell, geo.size.x - x), std::min(cell, geo.size.y - y) }), b);
        }
}
// 둥근 표시점(바깥 검정 · 안 흰 고리).
inline void drawRingMarker(HPaintContext& ctx, glm::vec2 c, float r) {
    const HGeometry o{ { c.x - r - 1.f, c.y - r - 1.f }, { 2.f * r + 2.f, 2.f * r + 2.f }, 1.f };
    const HGeometry i{ { c.x - r, c.y - r }, { 2.f * r, 2.f * r }, 1.f };
    ctx.drawRoundedRect(o, { 0.f, 0.f, 0.f, 0.f }, r + 1.f, { 0.f, 0.f, 0.f, 0.85f }, 1.f);
    ctx.drawRoundedRect(i, { 0.f, 0.f, 0.f, 0.f }, r, { 1.f, 1.f, 1.f, 1.f }, 1.5f);
}
} // namespace hslate_color

class SColorPlane : public HWidget {
public:
    using ColorFn = std::function<glm::vec4(float u, float v)>;
    std::shared_ptr<SColorPlane> colorAt(ColorFn fn)                             { color_ = std::move(fn); return self(); }
    std::shared_ptr<SColorPlane> marker(std::function<glm::vec2()> fn)           { marker_ = std::move(fn); return self(); }
    std::shared_ptr<SColorPlane> onChange(std::function<void(float, float)> fn)  { change_ = std::move(fn); return self(); }
    std::shared_ptr<SColorPlane> onBegin(std::function<void()> fn)               { begin_ = std::move(fn); return self(); }
    std::shared_ptr<SColorPlane> onEnd(std::function<void()> fn)                 { end_ = std::move(fn); return self(); }
    std::shared_ptr<SColorPlane> cells(int n)                                    { cells_ = std::clamp(n, 1, 32); return self(); }
    std::shared_ptr<SColorPlane> size(glm::vec2 s)                               { size_ = s; hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SColorPlane> border(glm::vec4 c)                             { border_ = c; return self(); }
    std::shared_ptr<SColorPlane> markerRadius(float r)                           { markerR_ = r; return self(); }

    bool dragging() const { return dragging_; }
    // 판 위 자리 p(문맥 좌표) → (u, v), 0 ~ 1 로 자름.
    glm::vec2 toUv(glm::vec2 p) const {
        const HGeometry& g = cachedGeo_;
        return { g.size.x > 0.f ? std::clamp((p.x - g.position.x) / g.size.x, 0.f, 1.f) : 0.f,
                 g.size.y > 0.f ? std::clamp((p.y - g.position.y) / g.size.y, 0.f, 1.f) : 0.f };
    }

    glm::vec2 computeDesiredSize() override { return size_; }
    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        if (color_) {
            const float cw = geo.size.x / (float)cells_, ch = geo.size.y / (float)cells_;
            for (int j = 0; j < cells_; ++j)
                for (int i = 0; i < cells_; ++i) {
                    const float u0 = (float)i / cells_, u1 = (float)(i + 1) / cells_;
                    const float v0 = (float)j / cells_, v1 = (float)(j + 1) / cells_;
                    // 칸 경계에 실금이 생기지 않게 오른쪽 · 아래 칸 끝을 다음 칸 시작에 붙입니다.
                    const float x0 = std::floor(i * cw), x1 = (i + 1 == cells_) ? geo.size.x : std::floor((i + 1) * cw);
                    const float y0 = std::floor(j * ch), y1 = (j + 1 == cells_) ? geo.size.y : std::floor((j + 1) * ch);
                    ctx.drawGradientRect(geo.makeChild({ x0, y0 }, { x1 - x0, y1 - y0 }),
                                         color_(u0, v0), color_(u1, v0), color_(u1, v1), color_(u0, v1));
                }
        }
        if (border_.a > 0.f) ctx.drawBorder(geo, border_, 1.f);
        if (marker_) {
            const glm::vec2 m = glm::clamp(marker_(), glm::vec2(0.f), glm::vec2(1.f));
            ctx.pushClip(geo);
            hslate_color::drawRingMarker(ctx, geo.position + m * geo.size, markerR_);
            ctx.popClip();
        }
    }
    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button != 0) return HReply::Unhandled();
        if (e.down) {
            if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
            dragging_ = true;
            if (begin_) begin_();
            report(e.position);
            return HReply::Handled().captureMouse(this);
        }
        if (!dragging_) return HReply::Unhandled();
        dragging_ = false;
        if (end_) end_();
        return HReply::Handled().releaseMouse();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        if (!dragging_) return HReply::Unhandled();
        if (!e.down) {   // 뗌을 놓쳤으면(다른 창으로 감) 여기서 끝냄
            dragging_ = false;
            if (end_) end_();
            return HReply::Handled().releaseMouse();
        }
        report(e.position);
        return HReply::Handled();
    }

private:
    void report(glm::vec2 p) { if (change_) { const glm::vec2 uv = toUv(p); change_(uv.x, uv.y); } }
    std::shared_ptr<SColorPlane> self() { return std::static_pointer_cast<SColorPlane>(shared_from_this()); }
    ColorFn                            color_;
    std::function<glm::vec2()>         marker_;
    std::function<void(float, float)>  change_;
    std::function<void()>              begin_, end_;
    int       cells_   = 8;
    glm::vec2 size_    { 160.f, 160.f };
    glm::vec4 border_  { 0.f, 0.f, 0.f, 0.f };
    float     markerR_ = 5.f;
    bool      dragging_ = false;
};

class SColorBar : public HWidget {
public:
    using ColorFn = std::function<glm::vec4(float t)>;
    std::shared_ptr<SColorBar> colorAt(ColorFn fn)                      { color_ = std::move(fn); return self(); }
    std::shared_ptr<SColorBar> marker(std::function<float()> fn)        { marker_ = std::move(fn); return self(); }
    std::shared_ptr<SColorBar> onChange(std::function<void(float)> fn)  { change_ = std::move(fn); return self(); }
    std::shared_ptr<SColorBar> onBegin(std::function<void()> fn)        { begin_ = std::move(fn); return self(); }
    std::shared_ptr<SColorBar> onEnd(std::function<void()> fn)          { end_ = std::move(fn); return self(); }
    std::shared_ptr<SColorBar> segments(int n)                          { segments_ = std::clamp(n, 1, 64); return self(); }
    std::shared_ptr<SColorBar> vertical(bool v)                         { vertical_ = v; return self(); }
    std::shared_ptr<SColorBar> size(glm::vec2 s)                        { size_ = s; hslate::noteLayoutChange(); return self(); }
    std::shared_ptr<SColorBar> border(glm::vec4 c)                      { border_ = c; return self(); }
    // 바둑판 바탕(알파 막대): 칸 크기 px(0 = 없음), 두 색.
    std::shared_ptr<SColorBar> checker(float cell, glm::vec4 a, glm::vec4 b) { checker_ = cell; chkA_ = a; chkB_ = b; return self(); }

    bool dragging() const { return dragging_; }
    float toT(glm::vec2 p) const {
        const HGeometry& g = cachedGeo_;
        if (vertical_) return g.size.y > 0.f ? std::clamp((p.y - g.position.y) / g.size.y, 0.f, 1.f) : 0.f;
        return g.size.x > 0.f ? std::clamp((p.x - g.position.x) / g.size.x, 0.f, 1.f) : 0.f;
    }

    glm::vec2 computeDesiredSize() override { return size_; }
    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        if (checker_ > 0.f) hslate_color::drawChecker(ctx, geo, checker_, chkA_, chkB_);
        if (color_) {
            const float len = vertical_ ? geo.size.y : geo.size.x;
            for (int k = 0; k < segments_; ++k) {
                const float t0 = (float)k / segments_, t1 = (float)(k + 1) / segments_;
                const float a = std::floor(k * len / segments_), b = (k + 1 == segments_) ? len : std::floor((k + 1) * len / segments_);
                const glm::vec4 c0 = color_(t0), c1 = color_(t1);
                if (vertical_) ctx.drawGradientRect(geo.makeChild({ 0.f, a }, { geo.size.x, b - a }), c0, c0, c1, c1);
                else           ctx.drawGradientRect(geo.makeChild({ a, 0.f }, { b - a, geo.size.y }), c0, c1, c1, c0);
            }
        }
        if (border_.a > 0.f) ctx.drawBorder(geo, border_, 1.f);
        if (marker_) {
            // 표시점: 막대를 가로지르는 띠(바깥 검정 1px · 안 흰 2px).
            const float t = std::clamp(marker_(), 0.f, 1.f);
            if (vertical_) {
                const float y = std::floor(geo.position.y + t * geo.size.y);
                ctx.drawRect({ { geo.position.x - 2.f, y - 2.f }, { geo.size.x + 4.f, 5.f }, 1.f }, { 0.f, 0.f, 0.f, 0.85f });
                ctx.drawRect({ { geo.position.x - 1.f, y - 1.f }, { geo.size.x + 2.f, 3.f }, 1.f }, { 1.f, 1.f, 1.f, 1.f });
            } else {
                const float x = std::floor(geo.position.x + t * geo.size.x);
                ctx.drawRect({ { x - 2.f, geo.position.y - 2.f }, { 5.f, geo.size.y + 4.f }, 1.f }, { 0.f, 0.f, 0.f, 0.85f });
                ctx.drawRect({ { x - 1.f, geo.position.y - 1.f }, { 3.f, geo.size.y + 2.f }, 1.f }, { 1.f, 1.f, 1.f, 1.f });
            }
        }
    }
    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button != 0) return HReply::Unhandled();
        if (e.down) {
            if (!cachedGeo_.contains(e.position)) return HReply::Unhandled();
            dragging_ = true;
            if (begin_) begin_();
            if (change_) change_(toT(e.position));
            return HReply::Handled().captureMouse(this);
        }
        if (!dragging_) return HReply::Unhandled();
        dragging_ = false;
        if (end_) end_();
        return HReply::Handled().releaseMouse();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        if (!dragging_) return HReply::Unhandled();
        if (!e.down) {
            dragging_ = false;
            if (end_) end_();
            return HReply::Handled().releaseMouse();
        }
        if (change_) change_(toT(e.position));
        return HReply::Handled();
    }

private:
    std::shared_ptr<SColorBar> self() { return std::static_pointer_cast<SColorBar>(shared_from_this()); }
    ColorFn                    color_;
    std::function<float()>     marker_;
    std::function<void(float)> change_;
    std::function<void()>      begin_, end_;
    int       segments_ = 12;
    bool      vertical_ = true;
    glm::vec2 size_     { 16.f, 160.f };
    glm::vec4 border_   { 0.f, 0.f, 0.f, 0.f };
    float     checker_  = 0.f;
    glm::vec4 chkA_     { 0.8f, 0.8f, 0.8f, 1.f };
    glm::vec4 chkB_     { 0.5f, 0.5f, 0.5f, 1.f };
    bool      dragging_ = false;
};
