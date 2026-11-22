#pragma once
#include "HGeometry.hpp"
#include "HAttr.hpp"
#include "HPaintContext.hpp"
#include "../../core/HText.hpp"
#include <functional>
#include <memory>
#include <cstdint>

// ── 배치 측정 패스 (H1b) ──────────────────────────────────────────────────────
// HSlateContext::arrange 가 시작할 때 측정 패스 번호를 새로 받고, 그 패스 안에서는 위젯마다
// computeDesiredSize 를 한 번만 부릅니다(HWidget::desiredSize 가 결과를 붙잡음). 예전에는 상자가
// 측정할 때 · 배치할 때(Auto 슬롯은 두 번) 자식을 다시 재고 자식은 또 자기 자식을 재서, Auto 가
// 겹친 깊이만큼 같은 위젯을 여러 번 쟀습니다. 패스 밖(pass == 0: 에디터가 버튼 폭을 미리 재는 곳,
// 시험)에서는 붙잡지 않고 늘 새로 잽니다. 패스를 arrange 마다 새로 시작하므로 캐시가 다음 배치까지
// 낡은 채 남지 않습니다(위젯 단위 무효화는 아직 없음).
//
// mutation 은 "배치 결과를 바꿀 수 있는 트리 · 슬롯 변경" 이 있었다는 표시입니다. 상자의 슬롯 추가 ·
// 비우기 · 가중치, 단일 자식 컨테이너의 내용 · 여백 · 정렬, 스크롤 위치, 접기 섹션 펼침이 올립니다.
// HSlateContext 는 이 값과 원하는 크기가 모두 그대로일 때만 같은 크기의 배치를 건너뜁니다.
// 실행 단위는 한 스레드이므로(UI 는 렌더 스레드 하나) thread_local 로 둡니다.
namespace hslate {
struct LayoutCounters {
    uint64_t pass      = 0;      // 지금 측정 패스(0 = 패스 밖, 캐시 안 씀)
    uint64_t prevPass  = 0;      // 이 문맥의 직전 패스. 이번 측정값을 그때 값과 비교합니다
    uint64_t nextPass  = 0;      // 패스 번호 발급
    bool     changed   = false;  // 이번 패스에서 원하는 크기가 바뀌었거나 처음 잰 위젯이 있음
    uint64_t mutation  = 0;      // 배치에 영향을 주는 트리 · 슬롯 변경 횟수
    uint64_t measures  = 0;      // computeDesiredSize 를 실제로 부른 횟수(계측 · 시험용, 누적)
    bool     cacheEnabled = true; // 거짓이면 예전 방식(캐시 없음 · 배치 건너뛰기 없음). 계측 · 시험 비교용
};
inline LayoutCounters& layoutCounters() { thread_local LayoutCounters c; return c; }
// 배치 결과를 바꿀 수 있는 변경을 알립니다. 위젯의 슬롯 · 내용 설정 함수가 부릅니다.
inline void noteLayoutChange() { ++layoutCounters().mutation; }
} // namespace hslate

// ── HWidget (Slate의 SWidget) ─────────────────────────────────────────────────
// 모든 위젯의 베이스. enable_shared_from_this로 메서드 체인 지원.
class HWidget : public std::enable_shared_from_this<HWidget> {
public:
    virtual ~HWidget() = default;

    // ── 레이아웃 패스 ─────────────────────────────────────────────────────────
    // 하위→상위: 이 위젯이 원하는 크기를 계산합니다. 자식의 크기는 child->desiredSize() 로 읽습니다.
    virtual glm::vec2 computeDesiredSize() = 0;
    // 원하는 크기(H1b). 측정 패스 안에서는 패스마다 한 번만 computeDesiredSize 를 부르고 그 값을
    // 돌려줍니다. 컨테이너의 computeDesiredSize · onArrangeChildren 은 자식에게 이것을 부릅니다.
    // 측정은 배치 크기에 기대지 않아야 합니다(지금 위젯은 모두 그렇습니다. 줄 바꿈 글자는 없음).
    glm::vec2 desiredSize() {
        hslate::LayoutCounters& lc = hslate::layoutCounters();
        if (lc.pass == 0) { ++lc.measures; return computeDesiredSize(); }
        if (measuredPass_ == lc.pass) return desired_;
        ++lc.measures;
        const glm::vec2 d = computeDesiredSize();
        if (measuredPass_ != lc.prevPass || d != desired_) lc.changed = true;
        desired_      = d;
        measuredPass_ = lc.pass;
        return d;
    }
    // 상위→하위: 실제 배치 크기가 확정됐을 때 호출 (children 배치)
    virtual void      onArrangeChildren(const HGeometry& allotted) { cachedGeo_ = allotted; }

    // ── 페인팅 패스 ───────────────────────────────────────────────────────────
    virtual void paint(HPaintContext& ctx, const HGeometry& geo) = 0;

    // ── 이벤트 (기본: Unhandled) ──────────────────────────────────────────────
    virtual HReply onMouseButton(const HPointerEvent& e) { return HReply::Unhandled(); }
    virtual HReply onMouseMove  (const HPointerEvent& e) { return HReply::Unhandled(); }
    virtual HReply onScroll     (const HPointerEvent& e) { return HReply::Unhandled(); }

    // ── 키보드 · 포커스 (H1a-1) ───────────────────────────────────────────────
    // 키는 HSlateContext::handleKeyEvent 가 포커스 위젯에서 루트 쪽으로 거슬러 올라가며(버블)
    // 처리될 때까지 보냅니다. 글자는 포커스 위젯에만 갑니다.
    virtual HReply onKeyDown(const HKeyEvent&)  { return HReply::Unhandled(); }
    virtual HReply onKeyUp  (const HKeyEvent&)  { return HReply::Unhandled(); }
    virtual HReply onChar   (const HCharEvent&) { return HReply::Unhandled(); }
    // 키보드 포커스를 받을 수 있는가. 거짓이면 HSlateContext::setFocus 가 거절하고,
    // "누르면 포커스" 규칙도 이 위젯을 고르지 않습니다.
    virtual bool supportsKeyboardFocus() const { return false; }
    virtual void onFocusReceived() {}
    virtual void onFocusLost()     {}

    // ── 위젯 트리 순회 (H1a-1) ────────────────────────────────────────────────
    // 자식 위젯을 **그리는 순서**대로 fn 에 넘깁니다(뒤에 그려져 위에 보이는 것이 나중).
    // 히트 테스트는 이 순서를 거꾸로 돌아 위에 있는 자식을 먼저 봅니다. 지금 배치 · 그리기에
    // 들어가지 않는 자식(접힌 섹션의 body 등)은 넘기지 않습니다. 보이지 않는 자식(Hidden ·
    // Collapsed)은 넘기되, 거르는 것은 부르는 쪽 몫입니다. 자식이 있는 위젯은 반드시 구현합니다.
    virtual void forEachChild(const std::function<void(const std::shared_ptr<HWidget>&)>&) {}
    // 이 위젯 자신이 히트 테스트에 걸리는가. 거짓이면 이 자리는 아래(먼저 그려진) 위젯으로
    // 통과합니다. 자식은 그대로 판정합니다(UE 의 SelfHitTestInvisible 에 해당).
    virtual bool hitTestVisible() const { return true; }
    // 자식을 자기 영역으로 잘라 그리는가. 참이면 히트 테스트가 영역 밖에서 자식을 보지 않습니다
    // (그려지지 않은 자리에서 걸리지 않게, DEF-56 의 HOutsideEvent 와 같은 기준).
    virtual bool clipsChildren() const { return false; }

    // ── 공통 속성 ─────────────────────────────────────────────────────────────
    Attr<EVisibility> visibility { EVisibility::Visible };
    Attr<bool>        enabled    { true };
    Attr<glm::vec2>   minSize    { glm::vec2(0.f) };
    Attr<std::string> tooltip    { std::string() };
    // 툴팁을 표시 글로(글자 체계 T3): 띄울 때 지금 문화권으로 풉니다.
    void setTooltip(const HText& t) { tooltip.bind([t] { return t.toString(); }); }

    // 포인터 아래에서 툴팁이 있는 가장 깊은 위젯을 찾습니다(enabled 무관).
    virtual std::shared_ptr<HWidget> findTooltipWidget(glm::vec2 pos) {
        if (!isVisible() || !cachedGeo_.contains(pos)) return nullptr;
        return tooltip.get().empty() ? nullptr : shared_from_this();
    }

    // 마지막으로 배치된 Geometry (hit testing용)
    const HGeometry& cachedGeometry() const { return cachedGeo_; }

    bool isVisible() const { return visibility.get() == EVisibility::Visible; }
    bool isCollapsed() const { return visibility.get() == EVisibility::Collapsed; }

protected:
    HGeometry cachedGeo_;

private:
    // desiredSize 캐시(H1b). measuredPass_ 는 이 값을 잰 측정 패스 번호입니다(0 = 아직 안 잼).
    glm::vec2 desired_      { 0.f, 0.f };
    uint64_t  measuredPass_ = 0;
};

// ── 편의 매크로 ───────────────────────────────────────────────────────────────
// HNew(SButton)->text("OK")->onClick([]{}) 스타일로 사용
#define HNew(T) (std::make_shared<T>())

// 위젯 객체 하나를 새로 만든 것처럼 되돌려 다시 씁니다(H1a-2). HSlateContext 는 포커스 위젯을 약한 참조로
// 붙잡으므로, 패널을 다시 만들 때마다 글 칸을 새로 만들면 포커스가 풀립니다. 값 대입으로 설정 · 상태를 모두
// 처음 값으로 돌리고(배치 · 호버 · 누름 상태까지 새 위젯과 같음) 객체의 정체성만 남깁니다. enable_shared_from_this
// 의 소유 정보는 대입으로 바뀌지 않습니다. 위젯 자신의 이벤트 처리 안에서 부르면 안 됩니다(실행 중인 콜백이 지워짐).
// 되돌린 위젯은 배치를 새로 해야 하므로 배치 변경으로 알립니다(H1b).
template <class T>
std::shared_ptr<T> HReuse(std::shared_ptr<T>& keep) {
    hslate::noteLayoutChange();
    if (keep) *keep = T();
    else      keep = std::make_shared<T>();
    return keep;
}
