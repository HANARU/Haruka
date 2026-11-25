#pragma once

#include <cstdint>

namespace hgpu {

// 실행 중 GPU 패스 시간 주기 로그(--gpu-log)용 구간 평균 누적기(지수 이동 평균이 아닌 산술 평균).
struct GpuLogSample {
    float shadowMs  = 0.f;
    float mainMs    = 0.f;
    float aoMs      = 0.f;
    float bloomMs   = 0.f;
    float tonemapMs = 0.f;
    float uiMs      = 0.f;
    float totalMs   = 0.f;
};

struct GpuLogAverage {
    float    shadowMs  = 0.f;
    float    mainMs    = 0.f;
    float    aoMs      = 0.f;
    float    bloomMs   = 0.f;
    float    tonemapMs = 0.f;
    float    uiMs      = 0.f;
    float    totalMs   = 0.f;
    uint32_t frames    = 0;
};

class GpuLogAccumulator {
public:
    void add(const GpuLogSample& s) {
        shadowSum_  += s.shadowMs;
        mainSum_    += s.mainMs;
        aoSum_      += s.aoMs;
        bloomSum_   += s.bloomMs;
        tonemapSum_ += s.tonemapMs;
        uiSum_      += s.uiMs;
        totalSum_   += s.totalMs;
        ++frames_;
    }

    uint32_t frames() const { return frames_; }

    GpuLogAverage flush() {
        GpuLogAverage avg;
        if (frames_ > 0) {
            const double inv = 1.0 / static_cast<double>(frames_);
            avg.shadowMs  = static_cast<float>(shadowSum_  * inv);
            avg.mainMs    = static_cast<float>(mainSum_    * inv);
            avg.aoMs      = static_cast<float>(aoSum_      * inv);
            avg.bloomMs   = static_cast<float>(bloomSum_   * inv);
            avg.tonemapMs = static_cast<float>(tonemapSum_ * inv);
            avg.uiMs      = static_cast<float>(uiSum_      * inv);
            avg.totalMs   = static_cast<float>(totalSum_   * inv);
            avg.frames    = frames_;
        }
        reset();
        return avg;
    }

    void reset() {
        shadowSum_ = mainSum_ = aoSum_ = bloomSum_ = tonemapSum_ = uiSum_ = totalSum_ = 0.0;
        frames_ = 0;
    }

private:
    double   shadowSum_  = 0.0;
    double   mainSum_    = 0.0;
    double   aoSum_      = 0.0;
    double   bloomSum_   = 0.0;
    double   tonemapSum_ = 0.0;
    double   uiSum_      = 0.0;
    double   totalSum_   = 0.0;
    uint32_t frames_     = 0;
};

} // namespace hgpu
