#pragma once
// ── SMenuAnchor (H2a, UE SMenuAnchor 를 줄인 것) ──────────────────────────────────────────────────────────────
// 자식 하나(보통 단추)와 "열면 띄울 내용"을 가진 위젯입니다. 열면 자기 배치 자리(cachedGeometry)를 앵커로 써서 자기 문맥의
// 팝업 층(HSlateContext::popups)에 내용을 띄웁니다. 문맥은 popupContext 로 받습니다(위젯은 자기가 어느 창 트리에 있는지 모르므로,
// 패널을 다른 창으로 옮길 수 있는 에디터는 그 패널이 있는 창의 문맥을 돌려줌).
//  - 자식은 받은 자리 그대로 배치 · 그리기 · 입력을 넘깁니다(이 위젯 자신은 그리지 않고 히트 테스트에도 걸리지 않음).
//    그래서 단추를 이 위젯으로 감싸도 화면 · 덤프는 그대로입니다.
//  - 앵커 위의 누름은 팝업의 "바깥"이 아닙니다(단추가 토글하게).
//  - 층이 팝업을 닫으면(바깥 누름 · Esc · 다른 창에서 열림 · 부모 닫힘) onOpenChanged(false) 를 부릅니다.
//  - 이 위젯을 다시 만들면(HReuse 포함) 열린 팝업과의 연결이 끊깁니다. 팝업을 여는 동안 같은 객체를 쓰고, 자식만 content 로 바꿉니다.
#include "../HWidget.hpp"
#include "../HSlateContext.hpp"
#include <functional>
#include <memory>

class SMenuAnchor : public HWidget {
public:
    using ContentFn = std::function<std::shared_ptr<HWidget>()>;
    using SpecFn    = std::function<void(HPopupLayer::Spec&)>;
    using ContextFn = std::function<HSlateContext*()>;

    std::shared_ptr<SMenuAnchor> content(std::shared_ptr<HWidget> w) { hslate::noteLayoutChange(); child_ = std::move(w); return self(); }
    std::shared_ptr<SMenuAnchor> menuContent(ContentFn fn)     { menuContent_ = std::move(fn); return self(); }
    std::shared_ptr<SMenuAnchor> placement(const HPopupPlacement& p) { placement_ = p; return self(); }
    // 바깥 누름 규칙 · 크기 등 Spec 의 나머지를 고칩니다(내용 · 앵커 · 자리 규칙 · onDismissed 는 이 위젯이 채움).
    std::shared_ptr<SMenuAnchor> configure(SpecFn fn)          { configure_ = std::move(fn); return self(); }
    std::shared_ptr<SMenuAnchor> popupContext(ContextFn fn)    { contextFn_ = std::move(fn); return self(); }
    std::shared_ptr<SMenuAnchor> popupContext(HSlateContext* c) { contextFn_ = [c] { return c; }; return self(); }
    std::shared_ptr<SMenuAnchor> onOpenChanged(std::function<void(bool)> fn) { onOpenChanged_ = std::move(fn); return self(); }

    bool isOpen() const { return ctx_ && handle_ && ctx_->popups().isOpen(handle_); }
    HSlateContext*      openContext() const { return isOpen() ? ctx_ : nullptr; }
    HPopupLayer::Handle handle() const { return isOpen() ? handle_ : 0; }

    // 엽니다. 이미 열려 있으면 내용 · 자리를 다시 만듭니다(refresh). 문맥 · 내용이 없으면 false.
    bool open() {
        if (isOpen()) return refresh();
        HSlateContext* c = contextFn_ ? contextFn_() : nullptr;
        if (!c) return false;
        HPopupLayer::Spec s;
        if (!makeSpec(s)) return false;
        ctx_    = c;
        handle_ = c->popups().push(std::move(s));
        if (!handle_) { ctx_ = nullptr; return false; }
        if (onOpenChanged_) onOpenChanged_(true);
        return true;
    }
    void close() {
        if (!isOpen()) { handle_ = 0; ctx_ = nullptr; return; }
        ctx_->popups().dismiss(handle_);   // onDismissed 가 상태를 비우고 알림
    }
    bool toggle() {
        if (isOpen()) { close(); return false; }
        return open();
    }
    // 열려 있으면 내용을 다시 만들고 지금 앵커 자리로 다시 놓습니다.
    bool refresh() {
        if (!isOpen()) return false;
        HPopupLayer::Spec s;
        if (!makeSpec(s)) return false;
        return ctx_->popups().update(handle_, std::move(s));
    }

    glm::vec2 computeDesiredSize() override { return child_ ? child_->desiredSize() : glm::vec2(0.f); }
    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
        if (child_) child_->onArrangeChildren(allotted);
    }
    void paint(HPaintContext& ctx, const HGeometry&) override {
        if (!isVisible()) return;
        if (child_ && child_->isVisible()) child_->paint(ctx, child_->cachedGeometry());
    }
    std::shared_ptr<HWidget> findTooltipWidget(glm::vec2 pos) override {
        if (!isVisible()) return nullptr;
        if (child_)
            if (auto w = child_->findTooltipWidget(pos)) return w;
        if (!cachedGeo_.contains(pos)) return nullptr;
        return tooltip.get().empty() ? nullptr : shared_from_this();
    }
    HReply onMouseButton(const HPointerEvent& e) override { return child_ ? child_->onMouseButton(e) : HReply::Unhandled(); }
    HReply onMouseMove(const HPointerEvent& e) override   { return child_ ? child_->onMouseMove(e) : HReply::Unhandled(); }
    HReply onScroll(const HPointerEvent& e) override      { return child_ ? child_->onScroll(e) : HReply::Unhandled(); }
    void forEachChild(const std::function<void(const std::shared_ptr<HWidget>&)>& fn) override {
        if (child_) fn(child_);
    }
    bool hitTestVisible() const override { return false; }

private:
    bool makeSpec(HPopupLayer::Spec& s) {
        s.content = menuContent_ ? menuContent_() : nullptr;
        if (!s.content) return false;
        if (configure_) configure_(s);
        s.anchor       = HPopupRect::of(cachedGeo_);
        s.placement    = placement_;
        s.anchorWidget = weak_from_this();
        std::weak_ptr<HWidget> weak = weak_from_this();
        s.onDismissed  = [this, weak] {
            if (!weak.lock()) return;   // 이 위젯이 먼저 사라짐
            handle_ = 0;
            ctx_    = nullptr;
            if (onOpenChanged_) onOpenChanged_(false);
        };
        return true;
    }
    std::shared_ptr<SMenuAnchor> self() { return std::static_pointer_cast<SMenuAnchor>(shared_from_this()); }

    std::shared_ptr<HWidget>   child_;
    ContentFn                  menuContent_;
    SpecFn                     configure_;
    ContextFn                  contextFn_;
    HPopupPlacement            placement_;
    std::function<void(bool)>  onOpenChanged_;
    HSlateContext*             ctx_    = nullptr;
    HPopupLayer::Handle        handle_ = 0;
};
