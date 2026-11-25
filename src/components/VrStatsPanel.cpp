#include "VrStatsPanel.hpp"

#include "../math/Damping.hpp"
#include "../ui/slate/widgets/SBorder.hpp"
#include "../ui/slate/widgets/STextBlock.hpp"
#include "../ui/slate/widgets/SVerticalBox.hpp"

#include <algorithm>
#include <cmath>

namespace {

glm::vec4 zoneColor(hvrstats::BudgetZone z) {
    switch (z) {
        case hvrstats::BudgetZone::Under:       return { 0.35f, 0.92f, 0.45f, 1.f };
        case hvrstats::BudgetZone::Warn90To100: return { 0.98f, 0.82f, 0.25f, 1.f };
        case hvrstats::BudgetZone::Over:        return { 0.98f, 0.32f, 0.32f, 1.f };
    }
    return { 0.92f, 0.93f, 0.95f, 1.f };
}

} // namespace

void VrStatsPanel::setRequested(bool r) {
    if (requested_ == r) return;
    requested_ = r;
    updateActive();
}

void VrStatsPanel::setXrActive(bool xrActive) {
    if (xrActive_ == xrActive) return;
    xrActive_ = xrActive;
    updateActive();
}

void VrStatsPanel::updateActive() {
    const bool next = requested_ && xrActive_;
    if (active_ == next) return;
    active_ = next;
    if (active_) {
        followStarted_ = false;
        followMoving_  = false;
        refreshAccum_  = 0.5f;   // 켠 프레임에 바로 갱신
        lastLines_.clear();
        dirty_         = true;
    }
}

bool VrStatsPanel::refreshDue(float dt) {
    if (!active_) return false;
    refreshAccum_ += std::max(dt, 0.f);
    if (refreshAccum_ < 0.5f && !lastLines_.empty()) return false;
    refreshAccum_ = 0.f;
    return true;
}

void VrStatsPanel::updateData(const hvrstats::VrStatsSnapshot& snap) {
    if (!active_) return;
    rebuild(snap);
}

void VrStatsPanel::place(const glm::vec3& headPos, const glm::quat& headRot, float dt) {
    if (!active_) return;

    glm::vec3 f = headRot * glm::vec3(0.f, 0.f, -1.f);
    f.y = 0.f;
    if (glm::dot(f, f) < 1e-4f) f = glm::vec3(0.f, 0.f, -1.f);
    f = glm::normalize(f);
    const glm::quat yawRot = glm::angleAxis(std::atan2(-f.x, -f.z), glm::vec3(0.f, 1.f, 0.f));
    const glm::quat offsetRot = glm::angleAxis(glm::radians(kLeftDeg), glm::vec3(0.f, 1.f, 0.f))
                              * glm::angleAxis(glm::radians(-kDownDeg), glm::vec3(1.f, 0.f, 0.f));
    const glm::vec3 dir = glm::normalize((yawRot * offsetRot) * glm::vec3(0.f, 0.f, -1.f));

    const glm::vec3 targetPos = headPos + dir * kDistance;
    const glm::quat targetRot = glm::quatLookAt(dir, glm::vec3(0.f, 1.f, 0.f));

    if (!followStarted_) {
        pos_           = targetPos;
        rot_           = targetRot;
        followStarted_ = true;
        followMoving_  = false;
        return;
    }

    if (!followMoving_) {
        glm::vec3 to = pos_ - headPos;
        to.y = 0.f;
        const float horiz = glm::length(to);
        const float cosA  = horiz > 1e-4f ? glm::dot(f, to / horiz) : -1.f;
        const bool  turned = cosA < std::cos(glm::radians(25.f));
        const bool  moved  = glm::length(targetPos - pos_) > 0.30f;
        followMoving_ = turned || moved;
    }
    if (!followMoving_) return;

    const float halfLife = hmath::halfLifeFromTau(1.f / 5.f);
    const float dts      = std::max(dt, 0.f);
    pos_ = hmath::damp(pos_, targetPos, halfLife, dts);
    rot_ = hmath::dampRotation(rot_, targetRot, halfLife, dts);
    if (glm::length(targetPos - pos_) < 0.01f) followMoving_ = false;
}

void VrStatsPanel::rebuild(const hvrstats::VrStatsSnapshot& snap) {
    const std::vector<std::string> lines = hvrstats::formatVrStatsLines(snap);
    if (lines == lastLines_) return;
    lastLines_ = lines;

    const float budgetMs = snap.targetHz > 0.f ? (1000.f / snap.targetHz) : 11.111f;
    const glm::vec4 fpsCol = zoneColor(hvrstats::classifyFps(snap.fpsAvg, snap.targetHz));
    const glm::vec4 gpuCol = zoneColor(hvrstats::classifyBudget(snap.gpuTotalMs, budgetMs));
    const glm::vec4 cpuCol = zoneColor(hvrstats::classifyBudget(snap.cpuWorkMs, budgetMs));
    const glm::vec4 dimCol{ 0.85f, 0.88f, 0.92f, 1.f };

    auto col = std::make_shared<SVerticalBox>();
    for (size_t i = 0; i < lines.size(); ++i) {
        glm::vec4 c = dimCol;
        if (i == 0)      c = fpsCol;
        else if (i == 1) c = (snap.gpuTotalMs >= snap.cpuWorkMs) ? gpuCol : cpuCol;

        auto tb = std::make_shared<STextBlock>();
        tb->text(lines[i])->fontSize(kFontPx)->color(c)->hAlign(EHAlign::Left);
        col->addSlot(tb, SVerticalBox::ESizeRule::Auto, 0.f, HPadding(0.f, i == 0 ? 0.f : 6.f, 0.f, 0.f), EHAlign::Left);
    }

    auto border = std::make_shared<SBorder>();
    border->background(glm::vec4(0.04f, 0.05f, 0.07f, 0.78f))
          ->cornerRadius(14.f)
          ->padding(HPadding(20.f, 14.f))
          ->content(col);

    slate_.setRootWidget(std::move(border));
    dirty_ = true;
}
