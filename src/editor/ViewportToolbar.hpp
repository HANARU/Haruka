#pragma once
#include "EditorUiKit.hpp"
#include "EditorContext.hpp"
#include "EditorGizmo.hpp"
#include "../ui/slate/HSlate.hpp"
#include "../runtime/IEngineFrontend.hpp"
#include <functional>
#include <memory>
#include <string>

class HFontAtlas;

// 뷰포트 좌상단 오버레이 툴바(S6e, Phase 2).
// 변환 도구(Q/W/E/R) · 좌표계(World/Local) · 스냅 · 카메라 속도를 표시하고
// 뷰포트 폭에 따라 5단계로 축약합니다.
class ViewportToolbar {
public:
    struct Deps {
        const EditorUiKit*   ui            = nullptr;
        const EditorContext* ctx           = nullptr;
        int*                 gizmoMode     = nullptr;
        bool*                worldSpace    = nullptr;
        HGizmoSnap*          snap          = nullptr;
        float*               camSpeed      = nullptr;
        std::function<float()>               viewportWidth;
        std::function<bool(int&, float&)>    dragReadout;
    };

    void init(Deps deps);
    std::shared_ptr<SBorder> build();

    void tick();
    void rebuild();
    void flushRebuild();

    HViewportRect fbRect() const;
    std::string   readoutText() const;
    void          cycleCamSpeed(int dir) const;

    static constexpr float kInset = 6.f;

private:
    float s(float x) const { return deps_.ui->s(x); }
    void  fill(SHorizontalBox& box, int lv, bool worstCase) const;
    void  measureLevels();
    int   computeLevel(float margin) const;

    Deps deps_;
    std::shared_ptr<SHorizontalBox> box_;
    std::shared_ptr<SBorder>        bar_;
    int               level_          = -1;
    bool              dirty_          = true;
    float             levelNeed_[5]   = {};
    float             levelNeedScale_ = -1.f;
    const HFontAtlas* levelNeedFont_  = nullptr;
};
