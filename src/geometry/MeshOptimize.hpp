#pragma once

// ── 정점 캐시 순서 (O3, 2026-11-17) ───────────────────────────────────────────────
// 삼각형 순서를 GPU 정점 캐시(변환 후 캐시)에 맞게 바꿉니다. 같은 정점을 쓰는 삼각형을 가까이 모으면 정점 셰이더가 같은
// 정점을 다시 돌리는 일이 줄어듭니다. 지표는 ACMR(삼각형당 캐시 놓침 수, 이상값 0.5 ~ 0.7, 무작위 순서는 1.5 이상)입니다.
//
// 알고리즘: Tom Forsyth "Linear-Speed Vertex Cache Optimisation"(2006). 정점마다 점수(캐시 안 위치 + 남은 삼각형 수)를 매기고
// 점수가 가장 높은 삼각형을 차례로 내보냅니다. meshoptimizer 의 optimizeVertexCache 와 같은 계열이며 내려받기 없이 씁니다.
// 화면 결과는 같습니다(삼각형 집합 · 감기 방향은 그대로, 순서만 바뀜). 같은 깊이의 겹친 면은 그리는 순서가 바뀔 수 있습니다.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hmesh {

// FIFO 캐시를 흉내 내 ACMR 을 잽니다(cacheSize 는 GPU 마다 다름, 16 ~ 32 가 흔함).
inline float acmr(const std::vector<uint32_t>& idx, uint32_t cacheSize = 16) {
    if (idx.size() < 3) return 0.f;
    std::vector<uint32_t> fifo;
    fifo.reserve(cacheSize);
    size_t misses = 0, head = 0;
    for (uint32_t v : idx) {
        if (std::find(fifo.begin(), fifo.end(), v) != fifo.end()) continue;
        ++misses;
        if (fifo.size() < cacheSize) fifo.push_back(v);
        else { fifo[head] = v; head = (head + 1) % cacheSize; }
    }
    return (float)misses / (float)(idx.size() / 3);
}

// 인덱스(삼각형 목록)의 삼각형 순서를 바꿉니다. vertexCount 는 인덱스가 가리키는 정점 수(최댓값 + 1 이상). 범위를 벗어난
// 인덱스가 있거나 삼각형이 아니면 그대로 둡니다.
inline void optimizeVertexCache(std::vector<uint32_t>& idx, uint32_t vertexCount) {
    const size_t triCount = idx.size() / 3;
    if (triCount < 2 || idx.size() % 3 != 0) return;
    for (uint32_t v : idx) if (v >= vertexCount) return;

    constexpr int   kCache        = 32;
    constexpr float kCacheDecay   = 1.5f;
    constexpr float kLastTriScore = 0.75f;
    constexpr float kValenceScale = 2.0f;
    constexpr float kValencePower = 0.5f;

    // 정점마다 쓰는 삼각형 목록(CSR)
    std::vector<uint32_t> useCount(vertexCount, 0);
    for (uint32_t v : idx) ++useCount[v];
    std::vector<uint32_t> offset(vertexCount + 1, 0);
    for (uint32_t v = 0; v < vertexCount; ++v) offset[v + 1] = offset[v] + useCount[v];
    std::vector<uint32_t> triOf(idx.size());
    {
        std::vector<uint32_t> fill(offset.begin(), offset.end() - 1);
        for (size_t t = 0; t < triCount; ++t)
            for (int k = 0; k < 3; ++k) triOf[fill[idx[t * 3 + k]]++] = (uint32_t)t;
    }
    std::vector<uint32_t> remaining = useCount;          // 정점마다 아직 내보내지 않은 삼각형 수
    std::vector<int>      cachePos(vertexCount, -1);
    std::vector<float>    vScore(vertexCount, 0.f);
    std::vector<float>    tScore(triCount, 0.f);
    std::vector<char>     emitted(triCount, 0);

    auto scoreOf = [&](uint32_t v) -> float {
        if (remaining[v] == 0) return -1.f;
        float s = 0.f;
        const int p = cachePos[v];
        if (p >= 0) {
            if (p < 3) s = kLastTriScore;
            else s = std::pow(1.f - (float)(p - 3) / (float)(kCache - 3), kCacheDecay);
        }
        s += kValenceScale * std::pow((float)remaining[v], -kValencePower);
        return s;
    };
    for (uint32_t v = 0; v < vertexCount; ++v) vScore[v] = scoreOf(v);
    for (size_t t = 0; t < triCount; ++t)
        tScore[t] = vScore[idx[t * 3]] + vScore[idx[t * 3 + 1]] + vScore[idx[t * 3 + 2]];

    std::vector<uint32_t> cache;   // 앞이 가장 최근
    cache.reserve(kCache + 3);
    std::vector<uint32_t> out;
    out.reserve(idx.size());
    size_t scanFrom = 0;           // 캐시에서 후보를 못 찾을 때 전체에서 다음 삼각형을 찾는 자리
    int64_t best = -1;
    {
        float bs = -1.f;
        for (size_t t = 0; t < triCount; ++t) if (tScore[t] > bs) { bs = tScore[t]; best = (int64_t)t; }
    }
    for (size_t n = 0; n < triCount; ++n) {
        if (best < 0) {   // 캐시 주변에 남은 삼각형이 없음: 아직 안 낸 첫 삼각형
            while (scanFrom < triCount && emitted[scanFrom]) ++scanFrom;
            best = (int64_t)scanFrom;
        }
        const size_t t = (size_t)best;
        emitted[t] = 1;
        const uint32_t tv[3] = { idx[t * 3], idx[t * 3 + 1], idx[t * 3 + 2] };
        for (int k = 0; k < 3; ++k) {
            out.push_back(tv[k]);
            --remaining[tv[k]];
            // 캐시 맨 앞으로(이미 있으면 옮김)
            auto it = std::find(cache.begin(), cache.end(), tv[k]);
            if (it != cache.end()) cache.erase(it);
            cache.insert(cache.begin(), tv[k]);
        }
        // 캐시에서 밀려난 정점
        while (cache.size() > (size_t)kCache) { cachePos[cache.back()] = -1; vScore[cache.back()] = scoreOf(cache.back()); cache.pop_back(); }
        for (size_t i = 0; i < cache.size(); ++i) cachePos[cache[i]] = (int)i;
        // 캐시 안 정점의 점수와 그 삼각형 점수를 다시 매기고 가장 높은 것을 고릅니다
        for (uint32_t v : cache) vScore[v] = scoreOf(v);
        best = -1;
        float bs = -1.f;
        for (uint32_t v : cache) {
            for (uint32_t k = offset[v]; k < offset[v + 1]; ++k) {
                const uint32_t tt = triOf[k];
                if (emitted[tt]) continue;
                const float s = vScore[idx[tt * 3]] + vScore[idx[tt * 3 + 1]] + vScore[idx[tt * 3 + 2]];
                tScore[tt] = s;
                if (s > bs) { bs = s; best = tt; }
            }
        }
    }
    idx.swap(out);
}

} // namespace hmesh
