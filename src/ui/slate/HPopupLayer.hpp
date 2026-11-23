#pragma once
// ── 팝업 층 (H2a, 2026-11-22, Docs/Editor_MultiWindow_Design.md 3절 H2) ────────────────────────────────────────
//
// HSlateContext 마다 하나 있는 팝업 스택입니다(UE FMenuStack + SMenuAnchor 를 줄인 것). 문맥이 창마다 하나이므로 떠 있는 창은
// 자기 팝업 층을 가집니다(D5). 팝업은 루트 위젯 다음, 툴팁 앞에 그리고, 포인터는 루트보다 먼저 받습니다.
//
// 규칙
//   - 위치(hslate::placePopup): 앵커 아래(Below) 또는 오른쪽(Right). 창 경계에 닿으면 뒤집고(Flip) 끼웁니다(clamp).
//     값은 넣을 때(push · update) 한 번 정하고 창 크기가 바뀌어도 그대로입니다(예전 팝업 호스트와 같음). 경계는 문맥의 마지막
//     배치 크기입니다(처음 배치 전이면 0 × 0).
//   - 바깥 누름: PassThrough 팝업은 dismissOutside 를 부른 쪽이 정한 자리(에디터 입력 순서의 "팝업 바깥" 단계)에서 닫히고, 누름은
//     소비하지 않습니다(DEF-64). Consume 팝업(메뉴 드롭다운)은 팝업 밖 배경(backdropInsetTop 아래)의 누름 · 뗌 · 휠을 먹고, 같은 배경
//     위에서 누르고 떼면 닫힙니다(예전 전면 캐처 단추와 같음). 앵커 위젯 위의 누름은 바깥이 아닙니다(앵커가 토글).
//   - Esc(dismissOnEscape): closeOnEscape 인 가장 위 팝업 하나를 닫습니다.
//   - 하위 팝업: parent 를 주어 넣으면 그 부모의 다른 하위 팝업을 먼저 닫고, 부모를 닫으면 하위도 함께 닫힙니다. 바깥 판정은 하위
//     팝업 영역도 안으로 셉니다.
//   - 한 번에 하나(setSingleRoot(true), H2b, UE FMenuStack 과 같음): 부모 없는 팝업을 넣으면 이미 열린 다른 루트 팝업(과 그 하위)을
//     먼저 닫습니다. 기본은 꺼짐(여러 루트가 함께 떠 있을 수 있음, H2a 와 같음). 에디터 문맥은 모두 켭니다.
//   - overPopup: 열린 팝업 영역 안이거나 Consume 팝업이 열려 있으면 참(그 창의 스플리터 · 탭 · 기즈모가 누름을 가져가지 않게).
//   - 닫힌 팝업은 곧바로 그리기 · 판정에서 빠지지만, 위젯은 전달이 끝난 뒤(collect) 놓습니다. 팝업 안 단추의 콜백이 자기 팝업을
//     닫아도 실행 중인 위젯이 파괴되지 않습니다.
//   - 닫힐 때마다 onDismissed 를 한 번 부릅니다(소유자가 상태를 맞춤). 소유자가 스스로 닫을 때도 부릅니다.
//
// ui 모듈 파일이라 platform · editor 를 모릅니다. 창 사이 규칙(다른 창 팝업 닫기)은 pushHook 으로 소유자가 정합니다.

#include "HWidget.hpp"
#include "HWidgetPath.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct HPopupRect {
    glm::vec2 pos  { 0.f, 0.f };
    glm::vec2 size { 0.f, 0.f };
    bool contains(glm::vec2 p) const {
        return p.x >= pos.x && p.x < pos.x + size.x && p.y >= pos.y && p.y < pos.y + size.y;
    }
    static HPopupRect of(const HGeometry& g) { return { g.position, g.size }; }
};

struct HPopupPlacement {
    enum class Side  : uint8_t { Below, Above, Right }; // 앵커 아래 · 위 · 오른쪽(하위 메뉴 · 누른 점)
    enum class Align : uint8_t { Start, Center };     // Below/Above 의 가로 맞춤: 앵커 왼쪽 · 앵커 가운데
    enum class Flip  : uint8_t { None, Overflow, Half };
    // Flip: None = 뒤집지 않음, Overflow = 경계를 넘으면 반대쪽, Half = (Below 만) 앵커 가운데가 경계 아래 절반이면 위로.
    Side  side  = Side::Below;
    Align align = Align::Start;
    Flip  flip  = Flip::Overflow;
    bool  clamp = true;      // 경계 안으로 끼움(왼쪽 · 위가 먼저)
    float gap   = 0.f;       // 앵커와 팝업 사이 거리
    float insetTop = 0.f;    // 끼울 때 경계 위쪽을 이만큼 비움(메뉴 줄 아래로)
};

namespace hslate {
// 팝업 왼쪽 위 자리. 예전 팝업 네 종류의 식을 그대로 옮긴 것이라 계산 순서를 바꾸지 않습니다(덤프가 1px 도 같아야 함).
inline glm::vec2 placePopup(const HPopupPlacement& pl, const HPopupRect& a, glm::vec2 size, const HPopupRect& b) {
    using P = HPopupPlacement;
    const float W = size.x, H = size.y;
    const float right  = b.pos.x + b.size.x;
    const float bottom = b.pos.y + b.size.y;
    float left = 0.f, top = 0.f;
    if (pl.side == P::Side::Below) {
        if (pl.flip == P::Flip::Half) {
            const bool up = (a.pos.y + a.size.y * 0.5f) > (b.pos.y + b.size.y * 0.5f);
            top = up ? a.pos.y - H - pl.gap : a.pos.y + a.size.y + pl.gap;
        } else {
            top = a.pos.y + a.size.y + pl.gap;
            if (pl.flip == P::Flip::Overflow && top + H > bottom) top = a.pos.y - H - pl.gap;
        }
        left = (pl.align == P::Align::Center) ? a.pos.x + (a.size.x - W) * 0.5f : a.pos.x;
    } else if (pl.side == P::Side::Above) {
        top = a.pos.y - H - pl.gap;
        if (pl.flip == P::Flip::Overflow && top < b.pos.y + pl.insetTop) top = a.pos.y + a.size.y + pl.gap;
        left = (pl.align == P::Align::Center) ? a.pos.x + (a.size.x - W) * 0.5f : a.pos.x;
    } else {
        left = a.pos.x + a.size.x + pl.gap;
        if (pl.flip != P::Flip::None && left + W > right) left = a.pos.x - W - pl.gap;
        top = a.pos.y;
        if (pl.flip != P::Flip::None && top + H > bottom) top = a.pos.y + a.size.y - H;
    }
    if (pl.clamp) {
        const float minT = b.pos.y + pl.insetTop;
        top  = std::clamp(top, minT, std::max(minT, bottom - H));
        left = std::clamp(left, b.pos.x, std::max(b.pos.x, right - W));
    }
    return { left, top };
}
} // namespace hslate

class HPopupLayer {
public:
    using Handle = uint64_t;   // 0 = 없음
    enum class EOutside : uint8_t { PassThrough, Consume };

    struct Spec {
        std::shared_ptr<HWidget> content;
        HPopupRect               anchor;                 // 이 문맥 좌표(넣을 때의 값)
        HPopupPlacement          placement;
        glm::vec2                size { -1.f, -1.f };    // 음수면 넣을 때 content->desiredSize()
        EOutside                 outside = EOutside::PassThrough;
        bool                     closeOnLeftOutside  = true;
        bool                     closeOnRightOutside = false;
        bool                     closeOnEscape       = false;
        float                    backdropInsetTop    = 0.f;   // Consume: 이 높이 위(메뉴 줄)는 배경이 아님
        std::weak_ptr<HWidget>   anchorWidget;                // 이 위젯 위 누름은 바깥이 아님(지금 배치 값)
        Handle                   parent = 0;
        std::function<void()>    onDismissed;
    };

    // ── 소유자 API ────────────────────────────────────────────────────────────
    Handle push(Spec s) {
        if (!s.content) return 0;
        if (s.parent != 0) {
            const int pi = indexOf(s.parent);
            if (pi < 0) return 0;
            dismissDescendants(s.parent);
        } else if (singleRoot_) {
            dismissAll();   // 다른 루트(와 그 하위)를 닫고 알린 뒤 넣습니다
        }
        Entry e;
        e.handle = ++nextHandle_;
        e.parent = s.parent;
        setSpec(e, std::move(s));
        entries_.push_back(std::move(e));
        const Handle h = entries_.back().handle;
        if (pushHook_) pushHook_(h);
        return h;
    }
    // 열린 팝업의 내용 · 자리 · 규칙을 바꿉니다(스택 자리 · 부모는 그대로). 닫혔으면 false.
    bool update(Handle h, Spec s) {
        const int i = indexOf(h);
        if (i < 0 || !s.content) return false;
        Entry& e = entries_[(size_t)i];
        if (e.content && e.content != s.content) graveyard_.push_back(e.content);
        setSpec(e, std::move(s));
        return true;
    }
    bool isOpen(Handle h) const { return indexOf(h) >= 0; }
    // 루트 팝업은 한 번에 하나(H2b). 켜는 순간 이미 열린 것은 그대로 둡니다(다음 push 부터).
    void setSingleRoot(bool on) { singleRoot_ = on; }
    bool singleRoot() const { return singleRoot_; }
    // h 와 그 하위 팝업을 닫습니다(위에서부터). 이미 닫혔으면 아무것도 안 합니다.
    void dismiss(Handle h) {
        if (indexOf(h) < 0) return;
        std::vector<Handle> close;
        for (size_t i = entries_.size(); i-- > 0;)
            if (entries_[i].live && (entries_[i].handle == h || isDescendant(entries_[i].handle, h)))
                close.push_back(entries_[i].handle);
        closeList(close);
    }
    void dismissAll() {
        std::vector<Handle> close;
        for (size_t i = entries_.size(); i-- > 0;)
            if (entries_[i].live) close.push_back(entries_[i].handle);
        closeList(close);
    }
    // 바깥 누름(button 0 = 왼쪽, 1 = 오른쪽). PassThrough 팝업 중 그 단추로 닫히는 것만 봅니다. 닫았으면 true.
    bool dismissOutside(glm::vec2 p, int button) {
        std::vector<Handle> close;
        for (size_t i = entries_.size(); i-- > 0;) {
            const Entry& e = entries_[i];
            if (!e.live || e.outside != EOutside::PassThrough) continue;
            if (!((button == 0 && e.closeOnLeft) || (button == 1 && e.closeOnRight))) continue;
            if (insideChain(e.handle, p)) continue;
            if (auto a = e.anchorWidget.lock(); a && a->cachedGeometry().contains(p)) continue;
            close.push_back(e.handle);
        }
        if (close.empty()) return false;
        for (Handle h : close) dismiss(h);
        return true;
    }
    // Esc: closeOnEscape 인 가장 위 팝업 하나(와 그 하위)를 닫습니다.
    bool dismissOnEscape() {
        for (size_t i = entries_.size(); i-- > 0;) {
            if (entries_[i].live && entries_[i].closeOnEscape) { dismiss(entries_[i].handle); return true; }
        }
        return false;
    }

    size_t openCount() const {
        size_t n = 0;
        for (const Entry& e : entries_) n += e.live ? 1u : 0u;
        return n;
    }
    bool empty() const { return openCount() == 0; }
    // 열린 팝업 영역(마지막 배치 값) 안인가. 아직 배치 전인 팝업은 영역이 비어 있습니다(예전 팝업 호스트와 같음).
    bool contains(glm::vec2 p) const {
        for (const Entry& e : entries_)
            if (e.live && e.content && e.content->isVisible() && e.content->cachedGeometry().contains(p)) return true;
        return false;
    }
    bool hasConsume() const {
        for (const Entry& e : entries_)
            if (e.live && e.outside == EOutside::Consume) return true;
        return false;
    }
    // 그 창의 다른 포인터 판정(스플리터 · 탭 · 기즈모)을 막아야 하는가.
    bool overPopup(glm::vec2 p) const { return hasConsume() || contains(p); }
    // 넣을 때 정한 자리(배치 전에도 앎). 닫혔으면 false.
    bool placedRect(Handle h, HPopupRect& out) const {
        const int i = indexOf(h);
        if (i < 0) return false;
        out = { entries_[(size_t)i].pos, entries_[(size_t)i].size };
        return true;
    }
    std::shared_ptr<HWidget> contentOf(Handle h) const {
        const int i = indexOf(h);
        return i < 0 ? nullptr : entries_[(size_t)i].content;
    }

    // 새 팝업을 넣은 뒤 부릅니다(소유자: 다른 창의 팝업 닫기 등). 훅 안에서 이 층의 팝업을 닫아도 됩니다.
    using PushHook = std::function<void(Handle)>;
    void setPushHook(PushHook fn) { pushHook_ = std::move(fn); }

    // ── 문맥이 부르는 것 ──────────────────────────────────────────────────────
    void setBounds(const HPopupRect& b) { bounds_ = b; }
    const HPopupRect& bounds() const { return bounds_; }

    void arrange() {
        for (Entry& e : entries_) {
            if (!e.live || !e.content) continue;
            HGeometry g;
            g.position = e.pos;
            g.size     = e.content->desiredSize();
            e.content->onArrangeChildren(g);
        }
    }
    void paint(HPaintContext& ctx) {
        for (Entry& e : entries_)
            if (e.live && e.content && e.content->isVisible()) e.content->paint(ctx, e.content->cachedGeometry());
    }
    // 누름 · 뗌. 팝업 영역 · Consume 배경이면 소비(true)하고 out 에 응답을 둡니다.
    bool routeButton(const HPointerEvent& ev, HReply& out) {
        for (size_t i = entries_.size(); i-- > 0;) {
            if (!entries_[i].live || !entries_[i].content) continue;
            std::shared_ptr<HWidget> c = entries_[i].content;   // 콜백이 항목을 바꿔도 살아 있게
            const Handle h = entries_[i].handle;
            if (c->isVisible() && c->cachedGeometry().contains(ev.position)) {
                HReply r = c->onMouseButton(ev);
                if (r.handled) { out = r; return true; }
                const int j = indexOf(h);
                if (j >= 0 && entries_[(size_t)j].outside == EOutside::Consume && inBackdrop(entries_[(size_t)j], ev.position))
                    backdropButton(h, ev);
                out = HReply::Handled();
                return true;
            }
            if (entries_[i].outside == EOutside::Consume && inBackdrop(entries_[i], ev.position)) {
                backdropButton(h, ev);
                out = HReply::Handled();
                return true;
            }
        }
        return false;
    }
    bool routeWheel(const HPointerEvent& ev, HReply& out) {
        for (size_t i = entries_.size(); i-- > 0;) {
            if (!entries_[i].live || !entries_[i].content) continue;
            std::shared_ptr<HWidget> c = entries_[i].content;
            if (c->isVisible() && c->cachedGeometry().contains(ev.position)) {
                HReply r = c->onScroll(ev);
                out = r.handled ? r : HReply::Handled();
                return true;
            }
            if (entries_[i].outside == EOutside::Consume && inBackdrop(entries_[i], ev.position)) {
                out = HReply::Handled();
                return true;
            }
        }
        return false;
    }
    // 이동은 열린 팝업 모두에 보냅니다(위에서부터, SOverlay 와 같은 규칙). 배경 눌림은 버튼이 떨어진 이동이면 풉니다.
    HReply routeMove(const HPointerEvent& ev) {
        HReply ret = HReply::Unhandled();
        std::vector<std::shared_ptr<HWidget>> live;
        for (size_t i = entries_.size(); i-- > 0;) {
            if (!entries_[i].live || !entries_[i].content) continue;
            if (!ev.down) entries_[i].backdropArmed = false;
            live.push_back(entries_[i].content);
        }
        for (auto& c : live) ret.merge(c->onMouseMove(ev));
        return ret;
    }
    std::shared_ptr<HWidget> findTooltip(glm::vec2 p) {
        for (size_t i = entries_.size(); i-- > 0;)
            if (entries_[i].live && entries_[i].content)
                if (auto w = entries_[i].content->findTooltipWidget(p)) return w;
        return nullptr;
    }
    // 팝업에 걸리면 true(경로는 팝업 내용 → 가장 깊은 위젯, 영역 안 빈 곳 · Consume 배경이면 내용만 · 빈 경로).
    bool hitTest(glm::vec2 p, HWidgetPath& out) const {
        for (size_t i = entries_.size(); i-- > 0;) {
            const Entry& e = entries_[i];
            if (!e.live || !e.content || !e.content->isVisible()) continue;
            if (hslate::hitTest(e.content, p, out)) return true;
            if (e.content->cachedGeometry().contains(p)) {
                out.entries.clear();
                out.entries.push_back({ e.content, e.content->cachedGeometry() });
                return true;
            }
            if (e.outside == EOutside::Consume && inBackdrop(e, p)) { out.entries.clear(); return true; }
        }
        out.entries.clear();
        return false;
    }
    bool findPath(const HWidget* target, HWidgetPath& out) const {
        for (const Entry& e : entries_)
            if (e.live && e.content && hslate::findPath(e.content, target, out)) return true;
        out.entries.clear();
        return false;
    }
    // 닫힌 팝업의 위젯을 놓습니다. 이벤트 전달 밖에서만 부릅니다.
    void collect() {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.live; }), entries_.end());
        graveyard_.clear();
    }

private:
    struct Entry {
        Handle                   handle = 0;
        Handle                   parent = 0;
        bool                     live   = true;
        std::shared_ptr<HWidget> content;
        glm::vec2                pos  { 0.f, 0.f };
        glm::vec2                size { 0.f, 0.f };
        EOutside                 outside = EOutside::PassThrough;
        bool                     closeOnLeft   = true;
        bool                     closeOnRight  = false;
        bool                     closeOnEscape = false;
        float                    insetTop      = 0.f;
        bool                     backdropArmed = false;
        std::weak_ptr<HWidget>   anchorWidget;
        std::function<void()>    onDismissed;
    };

    void setSpec(Entry& e, Spec s) {
        const glm::vec2 sz = (s.size.x >= 0.f && s.size.y >= 0.f) ? s.size : s.content->desiredSize();
        e.content       = std::move(s.content);
        e.size          = sz;
        e.pos           = hslate::placePopup(s.placement, s.anchor, sz, bounds_);
        e.outside       = s.outside;
        e.closeOnLeft   = s.closeOnLeftOutside;
        e.closeOnRight  = s.closeOnRightOutside;
        e.closeOnEscape = s.closeOnEscape;
        e.insetTop      = s.backdropInsetTop;
        e.anchorWidget  = std::move(s.anchorWidget);
        e.onDismissed   = std::move(s.onDismissed);
    }
    int indexOf(Handle h) const {
        if (h == 0) return -1;
        for (size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].handle == h) return entries_[i].live ? (int)i : -1;
        return -1;
    }
    Handle parentOf(Handle h) const {
        for (const Entry& e : entries_)
            if (e.handle == h) return e.parent;
        return 0;
    }
    bool isDescendant(Handle h, Handle ancestor) const {
        for (Handle p = parentOf(h); p != 0; p = parentOf(p))
            if (p == ancestor) return true;
        return false;
    }
    void dismissDescendants(Handle parent) {
        std::vector<Handle> close;
        for (size_t i = entries_.size(); i-- > 0;)
            if (entries_[i].live && isDescendant(entries_[i].handle, parent)) close.push_back(entries_[i].handle);
        closeList(close);
    }
    bool insideChain(Handle h, glm::vec2 p) const {
        for (const Entry& e : entries_) {
            if (!e.live || !e.content || !e.content->isVisible()) continue;
            if ((e.handle == h || isDescendant(e.handle, h)) && e.content->cachedGeometry().contains(p)) return true;
        }
        return false;
    }
    bool inBackdrop(const Entry& e, glm::vec2 p) const {
        return bounds_.contains(p) && p.y >= bounds_.pos.y + e.insetTop;
    }
    void backdropButton(Handle h, const HPointerEvent& ev) {
        const int i = indexOf(h);
        if (i < 0 || ev.button != 0) return;
        Entry& e = entries_[(size_t)i];
        if (ev.down) { e.backdropArmed = true; return; }
        const bool armed = e.backdropArmed;
        e.backdropArmed = false;
        if (armed) dismiss(h);   // 배경에서 누르고 뗌 = 닫기(예전 캐처 단추의 클릭)
    }
    // 목록 순서(위 → 아래)로 닫고, 모두 닫은 뒤 알립니다. 알림 안에서 다시 닫거나 넣어도 됩니다.
    void closeList(const std::vector<Handle>& hs) {
        std::vector<std::function<void()>> notes;
        for (Handle h : hs) {
            for (Entry& e : entries_) {
                if (e.handle != h || !e.live) continue;
                e.live = false;
                e.backdropArmed = false;
                if (e.onDismissed) notes.push_back(std::move(e.onDismissed));
                e.onDismissed = nullptr;
            }
        }
        for (auto& fn : notes) fn();
    }

    std::vector<Entry>                    entries_;
    std::vector<std::shared_ptr<HWidget>> graveyard_;
    HPopupRect                            bounds_;
    Handle                                nextHandle_ = 0;
    PushHook                              pushHook_;
    bool                                  singleRoot_ = false;
};
