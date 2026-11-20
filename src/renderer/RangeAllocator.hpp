#pragma once
// 구간 할당자(2026-11-20, Docs/Async_Loading.md D5). 메가 버퍼의 정점 · 인덱스 구역을 원소 단위로 나눠 줍니다.
// 빈 구간 목록을 시작 위치 순으로 두고 첫 맞춤(first fit)으로 잡으며, 놓을 때 이웃 빈 구간과 합칩니다.
// GPU 의존이 없는 순수 계산이라 장치 없이 시험합니다. 메시 하나를 올리고 지우는 빈도(씬 전환 · 프리팹)에는 선형 탐색으로 충분합니다.

#include <cstddef>
#include <cstdint>
#include <vector>

class RangeAllocator {
public:
    static constexpr uint32_t kFail = 0xFFFFFFFFu;

    RangeAllocator() = default;
    explicit RangeAllocator(uint32_t capacity) { reset(capacity); }

    void reset(uint32_t capacity) {
        capacity_ = capacity;
        used_ = 0;
        free_.clear();
        if (capacity) free_.push_back({ 0, capacity });
    }

    // count 개를 잡아 시작 위치를 돌려줍니다. 0 개이거나 맞는 빈 구간이 없으면 kFail.
    uint32_t allocate(uint32_t count) {
        if (count == 0) return kFail;
        for (size_t i = 0; i < free_.size(); ++i) {
            Range& r = free_[i];
            if (r.count < count) continue;
            const uint32_t at = r.offset;
            r.offset += count;
            r.count  -= count;
            if (r.count == 0) free_.erase(free_.begin() + (std::ptrdiff_t)i);
            used_ += count;
            return at;
        }
        return kFail;
    }

    // 잡았던 구간을 돌려줍니다. 범위 밖이거나 이미 빈 곳과 겹치면 false 이고 아무것도 바꾸지 않습니다.
    bool release(uint32_t offset, uint32_t count) {
        if (count == 0 || offset > capacity_ || count > capacity_ - offset) return false;
        size_t i = 0;
        while (i < free_.size() && free_[i].offset < offset) ++i;
        // 겹침 검사: 앞 빈 구간의 끝 · 뒤 빈 구간의 시작
        if (i > 0 && free_[i - 1].offset + free_[i - 1].count > offset) return false;
        if (i < free_.size() && offset + count > free_[i].offset) return false;
        const bool joinPrev = i > 0 && free_[i - 1].offset + free_[i - 1].count == offset;
        const bool joinNext = i < free_.size() && offset + count == free_[i].offset;
        if (joinPrev && joinNext) {
            free_[i - 1].count += count + free_[i].count;
            free_.erase(free_.begin() + (std::ptrdiff_t)i);
        } else if (joinPrev) {
            free_[i - 1].count += count;
        } else if (joinNext) {
            free_[i].offset = offset;
            free_[i].count += count;
        } else {
            free_.insert(free_.begin() + (std::ptrdiff_t)i, Range{ offset, count });
        }
        used_ -= count;
        return true;
    }

    uint32_t capacity() const { return capacity_; }
    uint32_t used() const { return used_; }
    // 지금 잡힌 곳 가운데 가장 뒤의 끝(범프 할당일 때의 "쓴 양"과 같은 뜻).
    uint32_t highWater() const {
        if (free_.empty()) return capacity_;
        const Range& last = free_.back();
        return last.offset + last.count == capacity_ ? last.offset : capacity_;
    }
    size_t freeRanges() const { return free_.size(); }
    uint32_t largestFree() const {
        uint32_t m = 0;
        for (const Range& r : free_) if (r.count > m) m = r.count;
        return m;
    }

private:
    struct Range { uint32_t offset, count; };
    std::vector<Range> free_;   // 시작 위치 오름차순, 서로 떨어져 있음(이웃이면 합침)
    uint32_t capacity_ = 0;
    uint32_t used_     = 0;
};
