#pragma once
#include "HWidget.hpp"
#include <memory>
#include <vector>

// ── HWidgetPath (Slate의 FWidgetPath, H1a-1) ─────────────────────────────────
// 루트에서 어떤 위젯까지 이어지는 위젯 목록입니다. 0 번이 루트, 마지막이 가장 깊은 위젯입니다.
// 위젯은 약한 참조로만 들고 있어, 경로를 들고 있는 사이 트리가 다시 만들어져도 파괴된 위젯을
// 부르지 않습니다(DEF-76 과 같은 이유). geometry 는 경로를 만들 때의 cachedGeometry 사본입니다.
struct HWidgetPath {
    struct Entry {
        std::weak_ptr<HWidget> widget;
        HGeometry              geometry;
    };
    std::vector<Entry> entries;

    bool   empty() const { return entries.empty(); }
    size_t size()  const { return entries.size(); }

    std::shared_ptr<HWidget> widgetAt(size_t i) const {
        return i < entries.size() ? entries[i].widget.lock() : nullptr;
    }
    std::shared_ptr<HWidget> root() const { return empty() ? nullptr : widgetAt(0); }
    // 가장 깊은 위젯(히트 테스트에서는 그 자리에서 맨 위에 그려진 위젯)입니다.
    std::shared_ptr<HWidget> lastWidget() const { return empty() ? nullptr : widgetAt(entries.size() - 1); }

    bool contains(const HWidget* w) const {
        for (const auto& e : entries)
            if (auto p = e.widget.lock(); p && p.get() == w) return true;
        return false;
    }
};

// 위젯 트리 질의(H1a-1). HSlateContext 가 쓰고, 시험 · 에디터도 문맥 없이 부를 수 있습니다.
namespace hslate {

// pos 에 걸리는 경로를 out 에 채웁니다(못 찾으면 비움). 규칙:
//  - 보이지 않는 위젯(Hidden · Collapsed)과 그 아래는 건너뜁니다.
//  - 자식은 forEachChild 의 역순(위에 그려진 것 먼저)으로 봅니다. SOverlay 는 마지막 슬롯이 이깁니다.
//  - clipsChildren() 인 위젯의 영역 밖에서는 그 위젯과 자식을 보지 않습니다.
//  - 잘라 내지 않는 위젯은 자기 영역 밖이어도 자식을 봅니다(넘친 자식이 그려진 자리에서 걸리게).
//  - hitTestVisible() 이 거짓인 위젯은 자기 자리로는 걸리지 않습니다(자식은 그대로 판정).
//  - geometry 는 직전 arrange 의 cachedGeometry 입니다.
bool hitTest(const std::shared_ptr<HWidget>& root, glm::vec2 pos, HWidgetPath& out);

// root 에서 target 까지의 경로를 out 에 채웁니다. 보이는지와 무관하게 forEachChild 로 찾습니다.
bool findPath(const std::shared_ptr<HWidget>& root, const HWidget* target, HWidgetPath& out);

// 포커스를 받을 수 있는 보이는 위젯을 그리는 순서로 나열합니다(Tab 이동을 위한 바탕, 아직 쓰는 곳 없음).
void collectFocusable(const std::shared_ptr<HWidget>& root, std::vector<std::shared_ptr<HWidget>>& out);

} // namespace hslate
