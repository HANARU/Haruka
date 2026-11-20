#pragma once
#include "RenderSceneInput.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// ── 파티클 그리기 순서 (2026-11-20, Docs/Particles_And_Decals.md D9) ───────────────────────────────────────
// 이미터들의 인스턴스를 한 줄로 이어 GPU 인스턴스 버퍼에 올릴 순서를 정합니다. Vulkan 을 부르지 않아 시험이 바로 확인합니다.
//  - 이미터끼리: 이미터 중심과 eye(두 눈 가운데)의 거리로 먼 것부터(같으면 입력 순서).
//  - 이미터 안: 알파 이미터만 먼 것부터 정렬합니다. 가산은 순서가 결과를 바꾸지 않아 그대로 둡니다.
//  - 합이 cap 을 넘으면 먼 이미터부터 잘립니다(가까운 효과를 남김).
//  - 이웃한 같은 섞기 구간은 드로 하나로 합칩니다.

struct ParticleRange {
    uint32_t first    = 0;
    uint32_t count    = 0;
    bool     additive = true;
};

struct ParticleOrderScratch {
    std::vector<std::pair<float, uint32_t>> batchKeys;
    std::vector<uint32_t>                   alloc;
    vfx::SortScratch                        sort;
};

inline void buildParticleStream(const std::vector<RenderParticleBatch>& batches, const glm::vec3& eye, uint32_t cap,
                                std::vector<vfx::ParticleInstance>& out, std::vector<ParticleRange>& ranges,
                                ParticleOrderScratch& s) {
    out.clear();
    ranges.clear();
    if (batches.empty() || cap == 0) return;
    s.batchKeys.resize(batches.size());
    for (size_t i = 0; i < batches.size(); ++i) {
        const glm::vec3 d = batches[i].center - eye;
        s.batchKeys[i] = { glm::dot(d, d), (uint32_t)i };
    }
    std::sort(s.batchKeys.begin(), s.batchKeys.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    // 예산은 가까운 이미터부터 나눕니다.
    s.alloc.assign(batches.size(), 0);
    uint32_t left = cap;
    for (size_t k = batches.size(); k-- > 0 && left > 0;) {
        const uint32_t i = s.batchKeys[k].second;
        const uint32_t n = std::min(batches[i].count, left);
        s.alloc[i] = n;
        left -= n;
    }
    out.reserve(cap - left);
    for (const auto& bk : s.batchKeys) {
        const RenderParticleBatch& b = batches[bk.second];
        const uint32_t n = s.alloc[bk.second];
        if (n == 0 || !b.data) continue;
        const uint32_t first = (uint32_t)out.size();
        out.insert(out.end(), b.data, b.data + n);
        const bool additive = b.blend == vfx::Blend::Additive;
        if (!additive) vfx::sortBackToFront(out.data() + first, n, eye, s.sort);
        if (!ranges.empty() && ranges.back().additive == additive && ranges.back().first + ranges.back().count == first)
            ranges.back().count += n;
        else
            ranges.push_back({ first, n, additive });
    }
}
