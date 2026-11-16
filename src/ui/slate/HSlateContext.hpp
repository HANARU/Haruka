#pragma once
#include "HWidget.hpp"
#include "HFontAtlas.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>

struct UIVertex { glm::vec2 pos, uv; glm::vec4 color; };

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

// ── HSlateContext ─────────────────────────────────────────────────────────────
// CPU 전용 UI 문맥: 루트 위젯 · 배치 · 포인터 캡처 · 그리기 목록 생성을 담당하며
// Vulkan 함수를 호출하지 않습니다. 구현은 HSlateRenderer.cpp 에 있습니다.
class HSlateContext {
public:
    // 루트를 갈아 끼우면 이전 트리의 위젯은 곧 파괴됩니다(uiScale 변경 → buildWidgetTree
    // 경로). 누르는 도중이었다면 캡처는 주인 없는 상태로 남겨, 그 누름의 뗌이 새 트리의
    // 엉뚱한 위젯에 가지 않게 합니다.
    void setRootWidget(std::shared_ptr<HWidget> root) {
        root_ = std::move(root);
        captured_.reset();
    }

    const std::shared_ptr<HWidget>& rootWidget() const { return root_; }

    void   arrange(glm::vec2 size);
    HReply handlePointerEvent(HPointerEvent e);
    bool   buildDrawList(glm::vec2 size, HFontAtlas& fonts,
                         VkImageView whiteView, uint32_t maxVerts,
                         HSlateDrawList& out);

private:
    std::shared_ptr<HWidget> root_;
    // 포인터 캡처. 위젯은 약한 참조로만 붙잡습니다(DEF-76). captureActive_ 는 "누름이
    // 아직 끝나지 않았다"는 뜻이라, 위젯이 사라져도 그 누름의 뗌까지는 참으로 남습니다.
    std::weak_ptr<HWidget>   captured_;
    bool                     captureActive_ = false;

    void releaseCapture() { captured_.reset(); captureActive_ = false; }
};
