#include "GpuStatsPanel.hpp"
#include "../core/FrameTimings.hpp"
#include <cstdio>

void GpuStatsPanel::init(const EditorUiKit* ui, const EditorContext* ctx,
                         const GpuStatsSnapshot* timings, bool dumpMode) {
    ui_       = ui;
    ctx_      = ctx;
    timings_  = timings;
    dumpMode_ = dumpMode;
}

void GpuStatsPanel::buildTab(SVerticalBox& content) const {
    if (!ui_ || !ctx_ || !timings_) return;
    auto addStat = [&](const char* label, AttrStr val) {
        content.addSlot(
            HNew(SHorizontalBox)
                ->addSlot(ui_->spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
                ->addSlot(HNew(STextBlock)->text(label)->fontSize(s(12.f))->color(EditorUiKit::kTextDim)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fixed, s(140.f))
                ->addSlot(HNew(STextBlock)->text(std::move(val))->fontSize(s(12.f))->color(EditorUiKit::kTextBody)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill),
            SVerticalBox::ESizeRule::Fixed, s(22.f));
    };

    const GpuStatsSnapshot* t = timings_;
    const bool dm = dumpMode_;
    addStat("FPS",        AttrStr([t, dm]{ return dm ? std::string("#") : std::to_string(t->fps); }));
    addStat("Draw Calls", AttrStr([t]{ return std::to_string(t->drawCalls); }));
    // 그 드로우들이 그리는 메시 수. 같은 지오메트리 · 머티리얼은 드로우 하나로 묶입니다(DEF-16).
    addStat("Instances",  AttrStr([t]{ return std::to_string(t->instances); }));
    addStat("Culled",     AttrStr([t]{ return std::to_string(t->culled); }));
    // 보이지만 행 예산(render.maxDrawRows)이 차서 못 그린 메시 수. 0 이 아니면 씬이 상한을 넘은 것입니다.
    addStat("Dropped",    AttrStr([t]{ return std::to_string(t->dropped); }));
    addStat("Transparent",AttrStr([t]{ return std::to_string(t->transparentDraws); }));
    // 올린 라이트 / 켜진 라이트. 차이는 어느 눈에도 영향이 없어 거른 것과 상한을 넘어 뺀 것입니다(DEF-33).
    addStat("Lights",     AttrStr([t]{ return std::to_string(t->lights) + " / " + std::to_string(t->lightsTotal); }));
    addStat("Shadow",     AttrStr([t, dm]{ if (dm) return std::string("#.### ms"); char b[32]; snprintf(b,32,"%.3f ms",t->shadowMs); return std::string(b); }));
    addStat("Main Pass",  AttrStr([t, dm]{ if (dm) return std::string("#.### ms"); char b[32]; snprintf(b,32,"%.3f ms",t->mainMs);   return std::string(b); }));
    addStat("AO",         AttrStr([t, dm]{ if (dm) return std::string("#.### ms"); char b[32]; snprintf(b,32,"%.3f ms",t->aoMs);      return std::string(b); }));
    addStat("Bloom",      AttrStr([t, dm]{ if (dm) return std::string("#.### ms"); char b[32]; snprintf(b,32,"%.3f ms",t->bloomMs);   return std::string(b); }));
    addStat("Tonemap",    AttrStr([t, dm]{ if (dm) return std::string("#.### ms"); char b[32]; snprintf(b,32,"%.3f ms",t->tonemapMs);return std::string(b); }));
    addStat("UI Pass",    AttrStr([t, dm]{ if (dm) return std::string("#.### ms"); char b[32]; snprintf(b,32,"%.3f ms",t->uiMs);     return std::string(b); }));
    addStat("CPU Frame",  AttrStr([dm]{ if (dm) return std::string("#.### ms"); char b[32]; snprintf(b,32,"%.3f ms",gFrameTimings().loopTotal.v); return std::string(b); }));
    addStat("Recreate/s", AttrStr([t, dm]{ if (dm) return std::string("#"); char b[32]; snprintf(b,32,"%.0f",t->recreatePerSec); return std::string(b); }));
    addStat("Total GPU",  AttrStr([t, dm]{ if (dm) return std::string("#.### ms"); char b[32]; snprintf(b,32,"%.3f ms",t->totalMs);  return std::string(b); }));
    addStat("MSAA",       AttrStr([c = ctx_]{ return std::string(c->renderer ? "4x" : "N/A"); }));
}
