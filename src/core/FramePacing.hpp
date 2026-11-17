#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// ── 프레임 시간 분포와 예산 (2026-11-18, 성능 안정화 1-1) ─────────────────────────────────────────
//
// VR 은 평균보다 최악 프레임이 중요합니다(놓친 프레임은 재투영 떨림으로 보임). 지수 이동 평균(FrameTimings)만으로는
// 가끔 튀는 프레임이 보이지 않아, 프레임마다 시간을 모아 중앙값 · 99 분위 · 최악을 냅니다. 엔진이 XR 세션 · 합성 눈
// 프레임에서 모으고, 끝날 때(세션 끝 · 자동 실행 끝) 한 줄로 남깁니다. 창 · GPU 없이 시험합니다(HarukaHeadlessTest).
namespace hperf {

// 헤드셋이 주사율을 알려 주지 않으면(합성 눈 등) Quest 목표 90 Hz 로 봅니다.
inline constexpr float kDefaultHz = 90.f;

inline float frameBudgetMs(float hz) { return 1000.f / (hz > 1.f ? hz : kDefaultHz); }

// 예산을 넘었는지. paced 는 루프가 헤드셋 대기(xrWaitFrame)를 포함하는지입니다. 그때 프레임 간격은 늘 예산 근처라,
// 1.5 배를 넘을 때(한 번 이상 놓침)만 넘은 것으로 봅니다. 대기가 없으면(합성 눈) 예산을 넘는 순간 넘은 것입니다.
inline bool overBudget(float frameMs, float budgetMs, bool paced) {
    return frameMs > (paced ? budgetMs * 1.5f : budgetMs);
}

struct Distribution {
    uint32_t count  = 0;
    float    median = 0.f;
    float    p99    = 0.f;
    float    max    = 0.f;
};

// 가장 가까운 순위(nearest-rank) 백분위수입니다. p 는 0 ~ 1.
inline float percentileSorted(const std::vector<float>& sorted, float p) {
    if (sorted.empty()) return 0.f;
    const float  rank = std::ceil(p * (float)sorted.size());
    const size_t idx  = rank <= 1.f ? 0 : std::min(sorted.size() - 1, (size_t)rank - 1);
    return sorted[idx];
}

inline Distribution summarize(std::vector<float> samples) {
    Distribution d;
    if (samples.empty()) return d;
    std::sort(samples.begin(), samples.end());
    d.count  = (uint32_t)samples.size();
    d.median = percentileSorted(samples, 0.5f);
    d.p99    = percentileSorted(samples, 0.99f);
    d.max    = samples.back();
    return d;
}

// 최근 capacity 개를 모읍니다(고리 버퍼). 넘치면 오래된 것부터 덮어씁니다. 최악값은 덮어쓴 것까지 기억합니다.
class FrameHistory {
public:
    explicit FrameHistory(size_t capacity = 16384) : cap_(capacity ? capacity : 1) { s_.reserve(cap_); }
    void add(float ms) {
        if (s_.size() < cap_) s_.push_back(ms);
        else { s_[next_] = ms; next_ = (next_ + 1) % cap_; }
        ++total_;
        worst_ = std::max(worst_, ms);
    }
    void clear() { s_.clear(); next_ = 0; total_ = 0; worst_ = 0.f; }
    uint64_t total() const { return total_; }
    Distribution summary() const {
        Distribution d = summarize(s_);
        d.count = (uint32_t)std::min<uint64_t>(total_, UINT32_MAX);
        d.max   = worst_;
        return d;
    }
private:
    std::vector<float> s_;
    size_t             cap_;
    size_t             next_  = 0;
    uint64_t           total_ = 0;
    float              worst_ = 0.f;
};

} // namespace hperf
