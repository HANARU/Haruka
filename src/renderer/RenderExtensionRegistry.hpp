#pragma once

// ── 패스 레지스트리 (DEF-17) ───────────────────────────────────────────────────
// 기본 패스(렌더러의 구체 멤버)와 등록된 확장을 한 목록으로 다룹니다. 예전에는 렌더러가 생성 · 파괴 ·
// 크기 변경 · 핫 리로드 경로마다 패스를 손으로 열거해, 패스를 더하려면 그 경로를 모두 고쳐야 했습니다.
//
// 순서 규칙: 앞으로(forward)는 기본 패스를 프레임 순서대로, 그다음 확장을 등록 순서대로입니다(획득 ·
// 파이프라인 재생성). 뒤로(reverse)는 그 정반대입니다(해제 · 파괴). 확장은 기본 패스의 자원(HDR · 깊이)을
// 쓸 수 있으므로 기본 패스보다 늦게 만들고 먼저 정리합니다.
//
// Vulkan 을 호출하지 않아 HarukaRenderExtTest(render_extensions)가 화면 없이 검산합니다.

#include <algorithm>
#include <memory>
#include <vector>

#include "RenderExtension.hpp"

class RenderExtensionRegistry {
public:
    // 기본 패스. 프레임 순서대로 넣습니다(렌더러 초기화 때 한 번).
    void setBuiltins(std::vector<HPass*> passes) { builtins_ = std::move(passes); }

    // 이미 있거나 null 이면 false.
    bool add(std::shared_ptr<IRenderExtension> ext) {
        if (!ext || contains(ext.get())) return false;
        exts_.push_back(std::move(ext));
        return true;
    }
    // 빼서 돌려줍니다. 없으면 null.
    std::shared_ptr<IRenderExtension> remove(IRenderExtension* ext) {
        auto it = std::find_if(exts_.begin(), exts_.end(),
                               [ext](const auto& e) { return e.get() == ext; });
        if (it == exts_.end()) return nullptr;
        auto out = std::move(*it);
        exts_.erase(it);
        return out;
    }
    bool contains(const IRenderExtension* ext) const {
        return std::any_of(exts_.begin(), exts_.end(), [ext](const auto& e) { return e.get() == ext; });
    }
    size_t extensionCount() const { return exts_.size(); }

    // 그 지점에 등록된 확장이 하나라도 있는지(배리어를 둘지 정할 때).
    bool any(HRenderStage stage) const {
        const uint32_t bit = stageBit(stage);
        return std::any_of(exts_.begin(), exts_.end(), [bit](const auto& e) { return (e->stages() & bit) != 0; });
    }
    // 그 지점의 확장을 등록 순서대로 부릅니다. 부른 수를 돌려줍니다.
    uint32_t dispatch(const HRenderStageContext& ctx) const {
        const uint32_t bit = stageBit(ctx.stage);
        uint32_t n = 0;
        for (const auto& e : exts_)
            if (e->stages() & bit) { e->record(ctx); ++n; }
        return n;
    }

    // 기본 패스 → 확장 순서.
    template <class F> void forEachForward(F&& f) const {
        for (HPass* p : builtins_) f(*p);
        for (const auto& e : exts_) f(static_cast<HPass&>(*e));
    }
    // 확장(나중에 넣은 것부터) → 기본 패스(뒤에서부터) 순서.
    template <class F> void forEachReverse(F&& f) const {
        for (auto it = exts_.rbegin(); it != exts_.rend(); ++it) f(static_cast<HPass&>(**it));
        for (auto it = builtins_.rbegin(); it != builtins_.rend(); ++it) f(**it);
    }

    // 확장만 뒤에서부터 빼며 돌려줍니다(렌더러가 끝날 때 정리용).
    std::vector<std::shared_ptr<IRenderExtension>> takeExtensionsReversed() {
        std::vector<std::shared_ptr<IRenderExtension>> out(exts_.rbegin(), exts_.rend());
        exts_.clear();
        return out;
    }

private:
    std::vector<HPass*>                            builtins_;
    std::vector<std::shared_ptr<IRenderExtension>> exts_;
};
