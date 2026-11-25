#pragma once

#include "GpuLogAccumulator.hpp"
#include "StressRampLogic.hpp"
#include "../renderer/Renderer.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class World;
class AssetManager;
class Actor;
class SubtitleQueue;
struct MeshHandle;
struct MaterialHandle;

// --stress 부하 램프 실행기(QP2, 2026-11-24).
// 씬 파일 없이 코드로 축별 부하를 단계적으로 늘리고 GPU/CPU 시간 · 무릎점 · CSV 를 남깁니다.
class StressRunner {
public:
    void init(const hstress::StressSpec& spec, World& world, AssetManager& assets, Renderer& renderer,
              SubtitleQueue* subtitles);

    // 매 프레임 drawFrame 뒤 호출. 끝나면 true 를 돌려줍니다(엔진이 창을 닫음).
    bool onFrame(float cpuWorkMs, float frameMs, float refreshHz, bool overlayOn = false);
    bool finished() const { return schedule_.finished; }

private:
    void setupBaseScene();
    void spawnStepActors(uint32_t n);
    void clearStepActors();
    void writeCsv() const;
    MeshHandle* buildUvSphere(uint32_t segU, uint32_t segV);

    hstress::StressSpec spec_{};
    World*              world_     = nullptr;
    AssetManager*       assets_    = nullptr;
    Renderer*           renderer_  = nullptr;

    hstress::StressSchedule          schedule_{};
    hgpu::GpuLogAccumulator          gpuAccum_{};
    double                           cpuWorkSumMs_ = 0.0;
    double                           frameSumMs_   = 0.0;
    uint32_t                         cpuFrames_    = 0;
    uint32_t                         lastTris_     = 0;
    uint32_t                         lastDraws_    = 0;
    uint32_t                         lastInsts_    = 0;
    std::vector<hstress::StressStepRecord> records_;
    std::vector<std::string>               csvRows_;
    std::vector<Actor*>                    stepActors_;

    std::chrono::steady_clock::time_point stepStart_{};
    bool                                  discardCleared_ = false;
    float                                 lastRefreshHz_  = 0.f;
};
