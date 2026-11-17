#pragma once
#include <cstdint>
#include <utility>
#include <vector>

// ── 지연 삭제 대기열 (3D UI 패널 구간 1, 2026-11-16) ─────────────────────────────────
// GPU 가 아직 읽고 있을 수 있는 자원을 바로 지우지 않고, 그 자원을 쓸 수 있었던 마지막 프레임이 끝난 뒤에 지웁니다.
// 예전에는 패널 삭제 · 크기 변경마다 vkDeviceWaitIdle 로 GPU 전체를 기다려, 헤드셋에서 프레임이 끊길 수 있었습니다.
//
// 프레임 번호는 렌더러의 제출 번호(frameNum_)입니다. retire(item, n) 은 "제출 번호 n 인 프레임까지 이 자원을 쓸 수 있다"는
// 뜻이고, 그 프레임은 같은 프레임 슬롯을 다시 쓰는 제출 번호 n + framesInFlight 의 펜스 대기 뒤에 끝났음이 보장됩니다.
// 그래서 collect(m) 은 n + framesInFlight <= m 인 것만 지웁니다(m = 펜스를 기다린 직후의 제출 번호).
//
// 이 헤더는 Vulkan 을 부르지 않습니다. 시험(RenderExtensionTest)이 장치 없이 순서를 확인합니다.
template <class T>
class RetireQueue {
public:
    explicit RetireQueue(uint64_t framesInFlight) : delay_(framesInFlight) {}

    void retire(T item, uint64_t lastUseFrame) { items_.push_back({ std::move(item), lastUseFrame }); }

    // 끝난 것을 destroy(T&) 로 지우고 그 수를 돌려줍니다. 넣은 순서를 지킵니다.
    template <class Fn>
    size_t collect(uint64_t frame, Fn&& destroy) {
        size_t done = 0, keep = 0;
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].frame + delay_ <= frame) { destroy(items_[i].item); ++done; }
            else if (keep != i) items_[keep++] = std::move(items_[i]);
            else ++keep;
        }
        items_.resize(keep);
        return done;
    }

    // 장치가 놀고 있을 때(종료 · 장치 대기 뒤) 남은 것을 모두 지웁니다.
    template <class Fn>
    void drain(Fn&& destroy) {
        for (Entry& e : items_) destroy(e.item);
        items_.clear();
    }

    size_t size() const { return items_.size(); }
    bool   empty() const { return items_.empty(); }

private:
    struct Entry { T item; uint64_t frame; };
    uint64_t           delay_;
    std::vector<Entry> items_;
};
