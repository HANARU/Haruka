#pragma once
#include "../HWidget.hpp"
#include <vulkan/vulkan.h>

// ── SImage ────────────────────────────────────────────────────────────────────
// VkImageView를 직접 렌더하는 위젯. 3D 뷰포트 패스스루에 사용.
class SImage : public HWidget {
public:
    std::shared_ptr<SImage> image(VkImageView view) {
        view_ = view; return self();
    }
    std::shared_ptr<SImage> image(Attr<VkImageView> attr) {
        viewAttr_ = std::move(attr); return self();
    }
    std::shared_ptr<SImage> tint(glm::vec4 c) {
        tint_ = c; return self();
    }
    std::shared_ptr<SImage> desiredSize(glm::vec2 s) {
        desiredSize_ = s; return self();
    }
    std::shared_ptr<SImage> uvRect(glm::vec4 uv) {
        uv_ = uv; return self();
    }

    glm::vec2 computeDesiredSize() override { return desiredSize_; }

    void onArrangeChildren(const HGeometry& allotted) override {
        cachedGeo_ = allotted;
    }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;
        VkImageView v = viewAttr_.get() ? viewAttr_.get() : view_;
        if (!v) return;
        ctx.drawTexture(geo, v, tint_, uv_);
    }

private:
    VkImageView          view_     = VK_NULL_HANDLE;
    Attr<VkImageView>    viewAttr_ { VK_NULL_HANDLE };
    glm::vec4            tint_     { 1.f, 1.f, 1.f, 1.f };
    glm::vec4            uv_       { 0.f, 0.f, 1.f, 1.f };
    glm::vec2            desiredSize_ { 64.f, 64.f };

    std::shared_ptr<SImage> self() {
        return std::static_pointer_cast<SImage>(shared_from_this());
    }
};
