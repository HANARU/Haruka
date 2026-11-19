#pragma once
#include "EditorUiKit.hpp"
#include "EditorContext.hpp"
#include "../ui/slate/HSlate.hpp"

// GPU 통계 스냅샷(뷰포트 오버레이와 GPU Stats 탭이 공유).
struct GpuStatsSnapshot {
    int   fps = 0, drawCalls = 0, instances = 0, culled = 0, transparentDraws = 0, dropped = 0, lights = 0, lightsTotal = 0;
    float shadowMs = 0.f, mainMs = 0.f, aoMs = 0.f, bloomMs = 0.f,
          tonemapMs = 0.f, uiMs = 0.f, totalMs = 0.f;
    // 초당 스왑체인 재생성 횟수입니다. 0 이 아니면 그 자체가 결함입니다.
    float recreatePerSec = 0.f;
};

// 하단 GPU Stats 탭(S6c).
class GpuStatsPanel {
public:
    void init(const EditorUiKit* ui, const EditorContext* ctx, const GpuStatsSnapshot* timings, bool dumpMode);
    void buildTab(SVerticalBox& content) const;

private:
    float s(float x) const { return ui_->s(x); }

    const EditorUiKit*      ui_       = nullptr;
    const EditorContext*    ctx_      = nullptr;
    const GpuStatsSnapshot* timings_  = nullptr;
    bool                    dumpMode_ = false;
};
