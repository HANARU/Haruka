#pragma once
#include "../HWidget.hpp"
#include <functional>
#include <string>

// ── SCollapseSection ──────────────────────────────────────────────────────────
// 클릭으로 펼치기/접기 가능한 섹션. 헤더 클릭 → expanded_ 토글.
// computeDesiredSize()가 매 프레임 재계산되므로 부모 VBox 레이아웃이 자동 갱신됨.
class SCollapseSection : public HWidget {
public:
    std::shared_ptr<SCollapseSection> title(std::string t)
        { title_ = std::move(t); return self(); }
    std::shared_ptr<SCollapseSection> body(std::shared_ptr<HWidget> w)
        { body_ = std::move(w); return self(); }
    std::shared_ptr<SCollapseSection> expanded(bool e)
        { expanded_ = e; return self(); }
    std::shared_ptr<SCollapseSection> headerHeight(float h)
        { headerH_ = h; return self(); }
    std::shared_ptr<SCollapseSection> fontSize(float f)
        { fontSize_ = f; return self(); }
    // 헤더 클릭으로 펼침이 바뀔 때 새 상태를 알립니다. 섹션은 리빌드마다 새로 만들어지므로,
    // 상태를 기억하려면 소유자가 이 콜백으로 받아 두었다가 expanded() 로 되돌려 줘야 합니다.
    std::shared_ptr<SCollapseSection> onToggle(std::function<void(bool)> fn)
        { onToggle_ = std::move(fn); return self(); }

    bool isExpanded() const { return expanded_; }

    glm::vec2 computeDesiredSize() override {
        float h = headerH_;
        if (expanded_ && body_)
            h += body_->computeDesiredSize().y;
        return { 0.f, h };
    }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
        if (!expanded_ || !body_) return;
        float bodyH = std::max(0.f, allotted.size.y - headerH_);
        body_->onArrangeChildren(
            allotted.makeChild({0.f, headerH_}, {allotted.size.x, bodyH}));
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;

        HGeometry hdrGeo = geo.makeChild({0.f, 0.f}, {geo.size.x, headerH_});
        glm::vec4 hdrBg = isHovered_ ? glm::vec4{0.13f, 0.13f, 0.15f, 1.f}
                                      : glm::vec4{0.09f, 0.09f, 0.10f, 1.f};
        ctx.drawRect(hdrGeo, hdrBg);

        // Bottom divider on header
        ctx.drawRect(geo.makeChild({0.f, headerH_ - 1.f}, {geo.size.x, 1.f}),
                     glm::vec4{0.06f, 0.06f, 0.07f, 1.f});

        // Arrow glyph
        float fs       = fontSize_ / 10.f;
        float arrSize  = 10.f * fs;
        float arrW     = 12.f * fs;
        HGeometry arrowGeo = hdrGeo.makeChild({5.f, (headerH_ - arrSize) * 0.5f}, {arrW, arrSize});
        ctx.drawText(arrowGeo, expanded_ ? "v" : ">", fontSize_, {0.55f, 0.55f, 0.60f, 1.f});

        // Title
        float titH = 11.f * fs;
        HGeometry titleGeo = hdrGeo.makeChild({5.f + arrW + 3.f, (headerH_ - titH) * 0.5f},
                                              {geo.size.x - (8.f + arrW + 3.f), titH});
        ctx.drawText(titleGeo, title_, fontSize_, {0.82f, 0.82f, 0.85f, 1.f});

        if (expanded_ && body_)
            body_->paint(ctx, body_->cachedGeometry());
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        HGeometry hdrGeo = cachedGeo_.makeChild({0.f, 0.f}, {cachedGeo_.size.x, headerH_});
        // 헤더는 누름과 뗌이 모두 헤더 위일 때만 토글합니다(버튼류와 같은 프레스 추적).
        // 예전에는 다른 곳에서 누르고 헤더 위에서 떼도 접혔고, 헤더의 누름은 body 로
        // 새어 들어갔습니다(DEF-63).
        if (e.button == 0 && hdrGeo.contains(e.position)) {
            if (e.down) {
                hdrArmed_ = true;
            } else if (hdrArmed_) {
                hdrArmed_ = false;
                expanded_ = !expanded_;
                if (onToggle_) onToggle_(expanded_);
            }
            return HReply::Handled();
        }
        if (e.button == 0 && !e.down) hdrArmed_ = false;
        if (expanded_ && body_) return body_->onMouseButton(e);
        return HReply::Unhandled();
    }
    HReply onMouseMove(const HPointerEvent& e) override {
        HGeometry hdrGeo = cachedGeo_.makeChild({0.f, 0.f}, {cachedGeo_.size.x, headerH_});
        isHovered_ = hdrGeo.contains(e.position);
        if (!e.down) hdrArmed_ = false;   // 헤더에서 눌러 밖에서 뗀 경우
        HReply r;
        if (expanded_ && body_) r = body_->onMouseMove(e);
        return r;
    }

private:
    std::string              title_;
    std::shared_ptr<HWidget> body_;
    bool  expanded_  = true;
    float headerH_   = 22.f;
    float fontSize_  = 10.f;
    bool  isHovered_ = false;
    bool  hdrArmed_  = false;   // 헤더 위에서 눌렸는가
    std::function<void(bool)> onToggle_;

    std::shared_ptr<SCollapseSection> self() {
        return std::static_pointer_cast<SCollapseSection>(shared_from_this());
    }
};
