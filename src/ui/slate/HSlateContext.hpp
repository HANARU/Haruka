#pragma once
#include "HWidget.hpp"
#include "HWidgetPath.hpp"
#include "HPopupLayer.hpp"
#include "HFontAtlas.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>

struct UIVertex {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;
    glm::vec2 localPos    = {0,0};
    glm::vec2 halfSize    = {0,0};
    glm::vec4 params      = {0,0,0,0};  // radius, borderWidth, shadowBlur, isShadow
    glm::vec4 borderColor = {0,0,0,0};
};
struct HDrawElement;

struct HSlateDrawList {
    std::vector<UIVertex> vertices;
    struct Batch {
        VkImageView texture   = VK_NULL_HANDLE;
        uint32_t    firstQuad = 0;
        uint32_t    quadCount = 0;
    };
    std::vector<Batch> batches;
    bool               overflowed = false;
};

// 그리기 목록의 64비트 해시(정점 바이트 · 배치의 텍스처 · 범위). 월드 패널은 프레임마다 목록을 만들어 이 값이 직전과
// 다를 때만 GPU 로 다시 그립니다(3D UI 구간 2). 8바이트씩 섞어 정점 수천 개도 수 µs 입니다. 같은 실행 안에서만 비교합니다
// (텍스처는 핸들 값을 그대로 섞음).
inline uint64_t drawListHash(const HSlateDrawList& dl) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (uint64_t)dl.vertices.size();
    auto mix = [&h](uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 33;
    };
    static_assert(sizeof(UIVertex) % 8 == 0, "UIVertex is hashed as 64-bit words");
    const auto* w = reinterpret_cast<const uint64_t*>(dl.vertices.data());
    const size_t n = dl.vertices.size() * (sizeof(UIVertex) / 8);
    for (size_t i = 0; i < n; ++i) mix(w[i]);
    mix(dl.batches.size());
    for (const auto& b : dl.batches) {
        mix((uint64_t)(uintptr_t)b.texture);
        mix(((uint64_t)b.firstQuad << 32) | b.quadCount);
    }
    mix(dl.overflowed ? 1u : 0u);
    return h;
}

// ── HSlateContext ─────────────────────────────────────────────────────────────
// CPU 전용 UI 문맥: 루트 위젯 · 배치 · 포인터 캡처 · 그리기 목록 생성을 담당하며
// Vulkan 함수를 호출하지 않습니다. 구현은 HSlateRenderer.cpp 에 있습니다.
class HSlateContext {
public:
    struct TooltipStyle {
        glm::vec4 bg           { 0.09f, 0.09f, 0.11f, 1.f };
        glm::vec4 border       { 0.25f, 0.25f, 0.29f, 1.f };
        glm::vec4 textColor    { 0.90f, 0.90f, 0.92f, 1.f };
        float     fontSize     = 12.f;
        float     cornerRadius = 4.f;
        float     borderWidth  = 1.f;
        float     padX         = 8.f;
        float     padY         = 5.f;
        glm::vec2 offset       { 12.f, 16.f };
        glm::vec2 shadowOffset { 0.f, 2.f };
        float     shadowBlur   = 6.f;
        glm::vec4 shadowColor  { 0.f, 0.f, 0.f, 0.40f };
        float     delaySec     = 0.5f;
    };

    // 루트를 갈아 끼우면 이전 트리의 위젯은 곧 파괴됩니다(uiScale 변경 → buildWidgetTree
    // 경로). 누르는 도중이었다면 캡처는 주인 없는 상태로 남겨, 그 누름의 뗌이 새 트리의
    // 엉뚱한 위젯에 가지 않게 합니다.
    // 루트가 실제로 바뀌면 키보드 포커스도 풉니다(이전 위젯에 onFocusLost). 같은 루트를 다시
    // 넣을 때는 포커스를 유지합니다(H1a-1).
    void setRootWidget(std::shared_ptr<HWidget> root) {
        if (root != root_) clearFocus();
        layoutValid_ = false;
        root_ = std::move(root);
        captured_.reset();
        hoverTooltipTarget_.reset();
        hoverElapsed_      = 0.f;
        tooltipSuppressed_ = false;
    }

    const std::shared_ptr<HWidget>& rootWidget() const { return root_; }

    void setTooltipStyle(const TooltipStyle& s) { tooltipStyle_ = s; }
    const TooltipStyle& tooltipStyle() const { return tooltipStyle_; }

    // 배치(H1b). 먼저 측정 패스를 새로 돌려 위젯마다 원하는 크기를 한 번씩 재고(HWidget::desiredSize),
    // 아래가 모두 직전 배치와 같으면 onArrangeChildren 을 건너뜁니다(배치 결과가 같으므로 geometry 를 그대로 씀).
    //   크기 · 루트가 같음, 배치 변경 알림(hslate::noteLayoutChange)이 없음, invalidateLayout 이 없음,
    //   직전 배치 뒤 누름 · 뗌 · 휠 · 키 · 글자 이벤트가 없음, 트리의 모든 위젯의 원하는 크기가 직전 패스와 같음.
    // 하나라도 다르면 예전처럼 전부 배치합니다. 배치 전에 이벤트 전달 중 맡긴 지연 작업(deferred)을 먼저 실행합니다.
    void   arrange(glm::vec2 size);
    // 다음 arrange 가 건너뛰지 않고 배치하게 합니다(밖에서 위젯 상태를 바꿔 원하는 크기에 드러나지 않을 때).
    void   invalidateLayout() { layoutValid_ = false; }
    // 배치를 실제로 한 횟수(배치 세대). 건너뛴 arrange 는 올리지 않습니다.
    uint64_t layoutGeneration() const { return layoutGeneration_; }
    struct LayoutStats {
        uint64_t arrangeCalls = 0;   // arrange 를 부른 횟수(collectElements 안의 것 포함)
        uint64_t arrangeRuns  = 0;   // 실제로 onArrangeChildren 을 돌린 횟수
        double   arrangeMs    = 0.0; // arrange 에 쓴 CPU 시간 합(측정 · 배치 · 지연 작업 제외)
    };
    const LayoutStats& layoutStats() const { return layoutStats_; }

    // ── 지연 변경 큐 (H1b) ────────────────────────────────────────────────────
    // 위젯 콜백 안에서 트리를 다시 만들면 실행 중인 위젯과 그 std::function 이 파괴됩니다. 그런 일은
    // 여기에 맡기면 이벤트 전달이 끝난 뒤 실행됩니다. 실행 시점:
    //   (1) 소유자가 flushDeferred() 를 부를 때(에디터는 입력 처리 끝 · tick 끝의 지연 리빌드 자리),
    //   (2) 이벤트 전달 중에 맡긴 일이 남아 있으면 다음 arrange 의 맨 앞.
    // 전달 중(handlePointerEvent · handleKeyEvent · handleCharEvent 안)에 flushDeferred 를 부르면 아무것도
    // 하지 않습니다. 한 번의 flush 는 시작할 때 쌓여 있던 것만 맡긴 순서대로 실행하고, 실행 중에 새로 맡긴
    // 것은 다음 flush 로 넘깁니다(예전 지연 플래그가 "다음 소진 때" 처리되던 것과 같음).
    void   deferred(std::function<void()> fn);
    void   flushDeferred();
    size_t deferredCount() const { return deferred_.size(); }
    bool   isDispatching() const { return dispatchDepth_ > 0; }

    HReply handlePointerEvent(HPointerEvent e);
    void   tickTooltip(float dt);
    bool   isTooltipVisible() const;
    bool   buildDrawList(glm::vec2 size, HFontAtlas& fonts,
                         VkImageView whiteView, uint32_t maxVerts,
                         HSlateDrawList& out);
    // S6-0: 배치 후 paint() 를 실행해 정렬된 HDrawElement 목록만 수집합니다.
    bool   collectElements(glm::vec2 size, HFontAtlas* fonts, std::vector<HDrawElement>& out);

    // ── 히트 경로 (H1a-1) ─────────────────────────────────────────────────────
    // pos 에 걸리는 루트 → 가장 깊은 위젯 경로입니다(규칙은 hslate::hitTest). 직전 arrange 의
    // geometry 를 씁니다. 루트가 없거나 보이지 않으면 빈 경로입니다.
    HWidgetPath hitTest(glm::vec2 pos) const;

    // ── 키보드 포커스 (H1a-1) ─────────────────────────────────────────────────
    // 포커스 위젯은 약한 참조로만 붙잡습니다. 바뀌면 이전 위젯에 onFocusLost, 새 위젯에
    // onFocusReceived 를 이 순서로 부릅니다. supportsKeyboardFocus() 가 거짓이거나 null 이면
    // 거절하고 false 를 돌려줍니다(지금 포커스는 그대로). 이미 포커스인 위젯이면 아무것도 안 합니다.
    // setFocus · clearFocus 는 헤더에 둡니다. setRootWidget 이 부르고, HSlateRenderer.cpp 를 링크하지 않는
    // 시험(글자 체계 시험의 TextInputController)도 쓰기 때문입니다.
    bool setFocus(const std::shared_ptr<HWidget>& w) {
        if (!w || !w->supportsKeyboardFocus()) return false;
        auto prev = focused_.lock();
        if (prev == w) return true;
        // 먼저 바꿔 둔 뒤 알립니다. onFocusLost 안에서 다시 포커스를 옮기면 그 뜻을 따르고,
        // 이 위젯에는 Received 를 보내지 않습니다.
        focused_ = w;
        if (prev) prev->onFocusLost();
        if (focused_.lock() == w) {
            w->onFocusReceived();
            if (focusHook_ && focused_.lock() == w) focusHook_(prev, w);
        }
        return true;
    }
    void clearFocus() {
        auto prev = focused_.lock();
        focused_.reset();
        if (!prev) return;
        prev->onFocusLost();
        if (focusHook_ && !focused_.lock()) focusHook_(prev, nullptr);
    }
    // 포커스가 바뀐 **뒤** 부르는 훅(H1a-2). 위젯의 onFocusLost · onFocusReceived 다음에 한 번, 최종 결과가
    // 이 변경일 때만 부릅니다(Lost 안에서 다시 옮겼으면 그 안쪽 변경이 따로 부름). 에디터는 이 훅으로 IME ·
    // 글 칸 상태를 맞춥니다(ui 모듈은 platform 을 include 할 수 없으므로 IME 는 에디터 몫).
    using FocusHook = std::function<void(const std::shared_ptr<HWidget>& prev, const std::shared_ptr<HWidget>& next)>;
    void setFocusHook(FocusHook fn) { focusHook_ = std::move(fn); }
    // 파괴된 위젯이면 null 입니다. 트리에서 빠졌는지는 여기서 보지 않습니다(다음 키 이벤트 때 판정).
    std::shared_ptr<HWidget> focusedWidget() const { return focused_.lock(); }

    // 키: 포커스 위젯에서 루트 쪽으로 처리될 때까지 보냅니다(버블). 포커스 위젯이 파괴됐거나
    // 트리에서 빠졌거나 경로에 보이지 않는 위젯이 있으면 포커스를 풀고 Unhandled 입니다.
    // 처리한 위젯의 응답에 포커스 요청이 있으면 적용합니다.
    HReply handleKeyEvent(const HKeyEvent& e);
    // 글자: 포커스 위젯에만 보냅니다(버블하지 않음). 포커스 확인 규칙은 키와 같습니다.
    HReply handleCharEvent(const HCharEvent& e);

    // ── 팝업 층 (H2a) ─────────────────────────────────────────────────────────
    // 이 문맥의 팝업 스택입니다(규칙은 HPopupLayer.hpp). 팝업은 루트 다음 · 툴팁 앞에 그리고, 포인터 · 휠 · 툴팁 ·
    // 히트 경로는 루트보다 먼저 봅니다. 경계는 arrange 크기입니다. 루트를 갈아 끼워도 팝업은 닫지 않습니다(소유자 몫).
    HPopupLayer&       popups()       { return popups_; }
    const HPopupLayer& popups() const { return popups_; }
    HPopupLayer::Handle pushPopup(HPopupLayer::Spec s) { return popups_.push(std::move(s)); }
    HPopupLayer::Handle pushPopup(std::shared_ptr<HWidget> w, const HPopupRect& anchor, const HPopupPlacement& placement) {
        HPopupLayer::Spec s;
        s.content   = std::move(w);
        s.anchor    = anchor;
        s.placement = placement;
        return popups_.push(std::move(s));
    }
    void dismissPopup(HPopupLayer::Handle h) { popups_.dismiss(h); }
    void dismissAllPopups() { popups_.dismissAll(); }

    // "누르면 포커스": 마우스 누름이 처리된 뒤, 응답에 포커스 요청이 없으면 누른 자리의 히트
    // 경로에서 가장 깊은 supportsKeyboardFocus() 위젯에 포커스를 줍니다. 그런 위젯이 없으면
    // 포커스를 그대로 둡니다(풀지 않음). 기본값은 켜짐입니다. 지금 포커스를 받는 위젯이 하나도
    // 없으므로 켜져 있어도 동작은 바뀌지 않습니다.
    void setClickToFocus(bool on) { clickToFocus_ = on; }
    bool clickToFocus() const { return clickToFocus_; }

private:
    std::shared_ptr<HWidget> root_;
    // 포인터 캡처. 위젯은 약한 참조로만 붙잡습니다(DEF-76). captureActive_ 는 "누름이
    // 아직 끝나지 않았다"는 뜻이라, 위젯이 사라져도 그 누름의 뗌까지는 참으로 남습니다.
    std::weak_ptr<HWidget>   captured_;
    bool                     captureActive_ = false;

    TooltipStyle             tooltipStyle_;
    glm::vec2                pointerPos_{ -10000.f, -10000.f };
    bool                     pointerValid_      = false;
    std::weak_ptr<HWidget>   hoverTooltipTarget_;
    float                    hoverElapsed_      = 0.f;
    bool                     tooltipSuppressed_ = false;

    // 키보드 포커스(H1a-1). 약한 참조라 위젯이 파괴되면 저절로 비습니다.
    std::weak_ptr<HWidget>   focused_;
    bool                     clickToFocus_ = true;
    FocusHook                focusHook_;

    // 배치 건너뛰기(H1b). 직전 배치의 입력을 기억합니다. 루트는 주소로만 비교합니다(setRootWidget 이 무효화).
    bool                     layoutValid_      = false;
    glm::vec2                lastLayoutSize_   { -1.f, -1.f };
    const HWidget*           lastLayoutRoot_   = nullptr;
    uint64_t                 lastMutation_     = 0;
    uint64_t                 lastPass_         = 0;
    uint64_t                 layoutGeneration_ = 0;
    LayoutStats              layoutStats_;

    // 팝업 층(H2a).
    HPopupLayer              popups_;

    // 지연 변경 큐(H1b).
    std::vector<std::function<void()>> deferred_;
    bool                     deferredFromDispatch_ = false;
    int                      dispatchDepth_        = 0;

    void releaseCapture() { captured_.reset(); captureActive_ = false; }
    HReply routePointerEvent(const HPointerEvent& e);
    void   applyFocusRequest(const HReply& r);
    // 포커스 위젯까지의 경로를 만듭니다. 쓸 수 없으면(파괴 · 트리에서 빠짐 · 경로에 보이지 않는
    // 위젯) 포커스를 풀고 false 입니다.
    bool   validFocusPath(HWidgetPath& out);
};
