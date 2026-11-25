#pragma once

#include "../renderer/RenderSceneInput.hpp"
#include "../core/VrStatsLogic.hpp"
#include "../ui/slate/HSlateContext.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

// 헤드셋 안 성능 오버레이 패널(VS1, 2026-11-25).
// SubtitlePanel 과 같이 엔진이 소유한 IRenderPanelSource 이며 액터/씬에 저장되지 않습니다.
class VrStatsPanel : public IRenderPanelSource {
public:
    static constexpr float    kDistance     = 1.1f;    // m
    static constexpr float    kLeftDeg      = 20.f;    // 시선 왼쪽 20도
    static constexpr float    kDownDeg      = 18.f;    // 시선 아래 18도
    static constexpr float    kWidthMeters  = 0.70f;
    static constexpr float    kHeightMeters = 0.30f;
    static constexpr uint32_t kTexWidth     = 1024;
    static constexpr uint32_t kTexHeight    = 440;
    static constexpr float    kFontPx       = 36.f;

    bool requested() const { return requested_; }
    bool visible() const   { return active_; }
    void setRequested(bool r);
    bool toggleRequested() { setRequested(!requested_); return requested_; }
    void setXrActive(bool xrActive);

    bool refreshDue(float dt);
    void updateData(const hvrstats::VrStatsSnapshot& snap);
    void place(const glm::vec3& headPos, const glm::quat& headRot, float dt);

    glm::mat4 model() const {
        return glm::translate(glm::mat4(1.f), pos_) * glm::mat4_cast(rot_)
             * glm::scale(glm::mat4(1.f), glm::vec3(kWidthMeters, kHeightMeters, 1.f));
    }
    glm::uvec2 textureSize() const { return { kTexWidth, kTexHeight }; }

    HSlateContext& slate() override { return slate_; }
    bool takeDirty() override { const bool d = dirty_; dirty_ = false; return d; }
    void markDirty() override { dirty_ = true; }

private:
    void rebuild(const hvrstats::VrStatsSnapshot& snap);
    void updateActive();

    HSlateContext            slate_;
    bool                     requested_     = false;
    bool                     xrActive_      = false;
    bool                     active_        = false;
    bool                     dirty_         = true;
    bool                     followStarted_ = false;
    bool                     followMoving_  = false;
    glm::vec3                pos_{ 0.f };
    glm::quat                rot_{ 1.f, 0.f, 0.f, 0.f };
    float                    refreshAccum_  = 0.5f;
    std::vector<std::string> lastLines_;
};
