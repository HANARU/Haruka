#pragma once
#include "SHorizontalBox.hpp"
#include "SNumberField.hpp"
#include "SBox.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ── SVectorField (H3a, 2026-11-22) ───────────────────────────────────────────────────────────────────────────
// 숫자 칸 셋(또는 넷)을 한 줄에 같은 폭으로 놓은 성분 칸입니다(UE 의 SVectorInputBox). 성분마다 왼쪽 끝 색 띠와 이름
// (X · Y · Z · W 또는 R · G · B · A)을 붙입니다. 끌기 감도 · 수정 키 · 입력 모드 · 확정 규칙은 각 성분의 SNumberField 그대로입니다
// (DEF-46 · DEF-77 · DEF-92). 값은 성분 번호로 읽고 씁니다(get(i) · set(i, v)). 끌기 시작 · 끝 알림도 성분 번호와 함께 옵니다.
// 성분 칸은 처음 잴 때(또는 field() 를 부를 때) 만들고, 그 뒤 설정을 바꾸면 다시 만듭니다.
class SVectorField : public HWidget {
public:
    using GetFn  = std::function<float(int)>;
    using SetFn  = std::function<void(int, float)>;
    using AxisFn = std::function<void(int)>;

    std::shared_ptr<SVectorField> components(int n)                   { n_ = std::clamp(n, 1, 4); dirty(); return self(); }
    std::shared_ptr<SVectorField> get(GetFn fn)                        { get_ = std::move(fn); dirty(); return self(); }
    std::shared_ptr<SVectorField> set(SetFn fn)                        { set_ = std::move(fn); dirty(); return self(); }
    std::shared_ptr<SVectorField> step(float s)                        { step_ = s; dirty(); return self(); }
    std::shared_ptr<SVectorField> unit(ENumberUnit u)                  { unit_ = u; dirty(); return self(); }
    std::shared_ptr<SVectorField> labels(std::vector<std::string> l)   { labels_ = std::move(l); dirty(); return self(); }
    std::shared_ptr<SVectorField> colors(std::vector<glm::vec4> c)     { colors_ = std::move(c); dirty(); return self(); }
    std::shared_ptr<SVectorField> style(const SNumberField::Style& s)  { style_ = s; dirty(); return self(); }
    std::shared_ptr<SVectorField> fontSize(float f)                    { fontSize_ = f; dirty(); return self(); }
    std::shared_ptr<SVectorField> gap(float px)                        { gap_ = px; dirty(); return self(); }
    std::shared_ptr<SVectorField> barWidth(float px)                   { barW_ = px; dirty(); return self(); }
    std::shared_ptr<SVectorField> onDragBegin(AxisFn fn)               { begin_ = std::move(fn); dirty(); return self(); }
    std::shared_ptr<SVectorField> onDragEnd(AxisFn fn)                 { end_ = std::move(fn); dirty(); return self(); }

    int count() const { return n_; }
    // 성분 칸(시험 · 에디터 포커스 판정). 범위 밖이면 null.
    std::shared_ptr<SNumberField> field(int i) {
        ensure();
        return (i >= 0 && i < (int)fields_.size()) ? fields_[(size_t)i] : nullptr;
    }

    glm::vec2 computeDesiredSize() override { ensure(); return box_->desiredSize(); }
    void onArrangeChildren(const HGeometry& a) override {
        ensure();
        cachedGeo_ = a;
        box_->onArrangeChildren(a);
    }
    void paint(HPaintContext& ctx, const HGeometry&) override {
        if (!isVisible() || !box_) return;
        box_->paint(ctx, box_->cachedGeometry());
    }
    HReply onMouseButton(const HPointerEvent& e) override { return box_ ? box_->onMouseButton(e) : HReply::Unhandled(); }
    HReply onMouseMove(const HPointerEvent& e) override   { return box_ ? box_->onMouseMove(e) : HReply::Unhandled(); }
    HReply onScroll(const HPointerEvent& e) override      { return box_ ? box_->onScroll(e) : HReply::Unhandled(); }
    std::shared_ptr<HWidget> findTooltipWidget(glm::vec2 pos) override {
        if (!isVisible() || !box_) return nullptr;
        if (auto w = box_->findTooltipWidget(pos)) return w;
        return (cachedGeo_.contains(pos) && !tooltip.get().empty()) ? shared_from_this() : nullptr;
    }
    void forEachChild(const std::function<void(const std::shared_ptr<HWidget>&)>& fn) override {
        ensure();
        fn(box_);
    }

private:
    void dirty() { built_ = false; hslate::noteLayoutChange(); }
    void ensure() {
        if (built_ && box_) return;
        built_ = true;
        box_ = HNew(SHorizontalBox);
        fields_.clear();
        static const char* const kNames[4] = { "X", "Y", "Z", "W" };
        for (int i = 0; i < n_; ++i) {
            if (i > 0) box_->addSlot(HNew(SBox), SHorizontalBox::ESizeRule::Fixed, gap_);
            const std::string lbl = i < (int)labels_.size() ? labels_[(size_t)i] : std::string(kNames[i]);
            auto f = HNew(SNumberField)
                ->style(style_)->label(lbl)->fontSize(fontSize_)->step(step_)->unit(unit_)
                ->clipText(true)->dragHint(false);
            if (i < (int)colors_.size()) f->labelColor(colors_[(size_t)i])->axisBar(colors_[(size_t)i], barW_);
            GetFn g = get_;
            SetFn st = set_;
            f->get([g, i] { return g ? g(i) : 0.f; });
            f->set([st, i](float v) { if (st) st(i, v); });
            AxisFn b = begin_, e = end_;
            if (b) f->onDragBegin([b, i] { b(i); });
            if (e) f->onDragEnd([e, i] { e(i); });
            fields_.push_back(f);
            box_->addSlot(f, SHorizontalBox::ESizeRule::Fill, 1.f);
        }
    }
    std::shared_ptr<SVectorField> self() { return std::static_pointer_cast<SVectorField>(shared_from_this()); }

    int                         n_ = 3;
    GetFn                       get_;
    SetFn                       set_;
    AxisFn                      begin_, end_;
    float                       step_ = 0.01f;
    ENumberUnit                 unit_ = ENumberUnit::Scalar;
    std::vector<std::string>    labels_;
    std::vector<glm::vec4>      colors_;
    SNumberField::Style         style_;
    float                       fontSize_ = 12.f;
    float                       gap_  = 2.f;
    float                       barW_ = 3.f;
    bool                        built_ = false;
    std::shared_ptr<SHorizontalBox>            box_;
    std::vector<std::shared_ptr<SNumberField>> fields_;
};
