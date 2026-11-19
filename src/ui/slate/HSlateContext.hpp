#pragma once
#include "HWidget.hpp"
#include "HFontAtlas.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>

struct UIVertex { glm::vec2 pos, uv; glm::vec4 color; };
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
    // S6-0: 배치 후 paint() 를 실행해 정렬된 HDrawElement 목록만 수집합니다.
    bool   collectElements(glm::vec2 size, HFontAtlas* fonts, std::vector<HDrawElement>& out);

private:
    std::shared_ptr<HWidget> root_;
    // 포인터 캡처. 위젯은 약한 참조로만 붙잡습니다(DEF-76). captureActive_ 는 "누름이
    // 아직 끝나지 않았다"는 뜻이라, 위젯이 사라져도 그 누름의 뗌까지는 참으로 남습니다.
    std::weak_ptr<HWidget>   captured_;
    bool                     captureActive_ = false;

    void releaseCapture() { captured_.reset(); captureActive_ = false; }
};
