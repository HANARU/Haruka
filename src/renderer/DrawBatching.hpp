#pragma once

// ── 자동 인스턴싱 (DEF-16) ─────────────────────────────────────────────────────
// 컬링을 통과한 메시들을 같은 지오메트리 · 머티리얼끼리 묶어 transform SSBO 행을 연속으로 배정하고,
// 묶음 하나를 드로우 하나(instanceCount = N)로 그립니다. 셰이더는 gl_InstanceIndex
// (= firstInstance + i)로 행을 읽으므로 바꿀 것이 없습니다. UE 의 Dynamic Instancing 과 Bevy 의
// 자동 배칭이 같은 방식입니다(상태가 같은 드로우를 모아 하나의 인스턴스 드로우로 합침).
//
// 규칙:
//   · 불투명만 묶습니다. 투명은 뒤에서 앞으로 정렬해야 해서 메시마다 드로우 하나입니다.
//   · 묶음의 순서는 처음 나온 순서입니다. 같은 지오메트리 · 머티리얼이 없는 씬에서는 행 번호와
//     드로우 순서가 묶기 전과 똑같습니다.
//   · 묶음 안에서는 라이트 안의 인스턴스를 앞에 모읍니다(안정 정렬). 섀도는 그 앞부분만 그리면
//     되므로 행을 따로 배정하지 않습니다.
//   · 카메라에 보이는 메시가 행 예산을 먼저 씁니다. 라이트에만 보이는 캐스터는 그 뒤에 지오메트리
//     기준으로 묶습니다(섀도는 머티리얼을 쓰지 않음). 예산을 넘친 수는 dropped 로 셉니다(DEF-10).
//
// Vulkan 에 의존하지 않아 HarukaBatchTest(draw_batching)가 화면 없이 검산합니다.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace hbatch {

// 컬링과 LOD 선택을 통과한 메시 하나입니다. 카메라와 라이트 어느 쪽에도 보이지 않는 메시는 넣지 않습니다.
struct Candidate {
    const void* material   = nullptr;   // 묶음 키. 렌더러는 MaterialHandle* 를 넣습니다
    uint32_t    indexCount = 0;
    uint32_t    firstIndex = 0;
    int32_t     vertexOffset = 0;
    float       distSq     = 0.f;       // 두 눈 가운데에서 피벗까지 거리 제곱(투명 정렬 키)
    bool        inCam       = false;
    bool        inLight     = false;
    bool        transparent = false;
};

// VkDrawIndexedIndirectCommand 와 같은 배치입니다(렌더러의 GpuDrawCmd 로 그대로 옮깁니다).
struct Run {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;   // 첫 행. 묶음의 행은 [firstInstance, firstInstance + instanceCount)
};

struct Result {
    std::vector<uint32_t> rowSource;     // 행 → 후보 번호. 렌더러가 트랜스폼 · 머티리얼을 채울 때 씁니다
    std::vector<Run>      opaque;        // 불투명 묶음마다 하나(깊이 프리패스와 컬러 패스가 함께 씀)
    std::vector<Run>      transparent;   // 투명 메시마다 하나, 뒤에서 앞으로
    std::vector<Run>      shadow;        // 섀도 패스
    uint32_t opaqueInstances = 0;        // 불투명으로 그리는 메시 수(묶음 안의 인스턴스 합)
    uint32_t droppedCam      = 0;        // 카메라에 보이지만 행 예산이 차서 못 그린 메시
    uint32_t droppedShadow   = 0;        // 라이트에만 보이지만 행 예산이 차서 못 그린 캐스터
};

namespace detail {
struct Key {
    const void* material;
    uint32_t    indexCount, firstIndex;
    int32_t     vertexOffset;
    bool operator==(const Key& o) const {
        return material == o.material && indexCount == o.indexCount &&
               firstIndex == o.firstIndex && vertexOffset == o.vertexOffset;
    }
};
struct KeyHash {
    size_t operator()(const Key& k) const {
        size_t h = std::hash<const void*>{}(k.material);
        auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
        mix(k.indexCount); mix(k.firstIndex); mix((uint32_t)k.vertexOffset);
        return h;
    }
};
inline Key keyOf(const Candidate& c, bool withMaterial) {
    return { withMaterial ? c.material : nullptr, c.indexCount, c.firstIndex, c.vertexOffset };
}
} // namespace detail

// sortTransparent 가 거짓이면(카메라가 없을 때) 투명은 행 순서 그대로 둡니다.
inline Result build(const std::vector<Candidate>& cands, uint32_t maxRows, bool sortTransparent = true) {
    using detail::Key;
    using detail::KeyHash;
    Result r;
    r.rowSource.reserve(std::min<size_t>(cands.size(), maxRows));

    // 1단계: 카메라에 보이는 메시. 불투명은 묶음 하나가 슬롯 하나, 투명은 메시 하나가 슬롯 하나이고,
    // 슬롯은 처음 나온 순서대로 행을 받습니다.
    struct Slot { bool group; uint32_t index; };
    std::vector<Slot> slots;
    std::vector<std::vector<uint32_t>> groups;
    std::unordered_map<Key, uint32_t, KeyHash> groupOf;
    for (uint32_t i = 0; i < (uint32_t)cands.size(); ++i) {
        const Candidate& c = cands[i];
        if (!c.inCam) continue;
        if (c.transparent) { slots.push_back({ false, i }); continue; }
        auto [it, inserted] = groupOf.try_emplace(detail::keyOf(c, true), (uint32_t)groups.size());
        if (inserted) { groups.emplace_back(); slots.push_back({ true, it->second }); }
        groups[it->second].push_back(i);
    }

    struct TranspKeyed { float distSq; Run run; };
    std::vector<TranspKeyed> transp;
    uint32_t row = 0;
    for (const Slot& s : slots) {
        if (!s.group) {
            const Candidate& c = cands[s.index];
            if (row >= maxRows) { ++r.droppedCam; continue; }
            const Run run{ c.indexCount, 1, c.firstIndex, c.vertexOffset, row++ };
            r.rowSource.push_back(s.index);
            transp.push_back({ c.distSq, run });
            if (c.inLight) r.shadow.push_back(run);   // 카메라에도 보이면서 라이트 안이면 같은 행으로 그림자
            continue;
        }
        std::vector<uint32_t>& m = groups[s.index];
        std::stable_partition(m.begin(), m.end(), [&](uint32_t i) { return cands[i].inLight; });
        const uint32_t room = maxRows > row ? maxRows - row : 0;
        const uint32_t n    = std::min((uint32_t)m.size(), room);
        r.droppedCam += (uint32_t)m.size() - n;
        if (n == 0) continue;
        const Candidate& c0 = cands[m[0]];
        const Run run{ c0.indexCount, n, c0.firstIndex, c0.vertexOffset, row };
        uint32_t lit = 0;
        for (uint32_t k = 0; k < n; ++k) {
            r.rowSource.push_back(m[k]);
            if (cands[m[k]].inLight) ++lit;
        }
        row += n;
        r.opaque.push_back(run);
        r.opaqueInstances += n;
        if (lit) { Run sh = run; sh.instanceCount = lit; r.shadow.push_back(sh); }
    }

    // 2단계: 라이트에만 보이는 캐스터. 카메라 행을 밀어내지 않도록 남은 예산만 씁니다. 섀도는 머티리얼을
    // 쓰지 않으므로 지오메트리만으로 묶습니다(행마다 머티리얼은 그대로 기록되어 행-머티리얼 1:1 은 유지).
    std::vector<std::vector<uint32_t>> casters;
    std::unordered_map<Key, uint32_t, KeyHash> casterOf;
    for (uint32_t i = 0; i < (uint32_t)cands.size(); ++i) {
        const Candidate& c = cands[i];
        if (c.inCam || !c.inLight) continue;
        auto [it, inserted] = casterOf.try_emplace(detail::keyOf(c, false), (uint32_t)casters.size());
        if (inserted) casters.emplace_back();
        casters[it->second].push_back(i);
    }
    for (const auto& m : casters) {
        const uint32_t room = maxRows > row ? maxRows - row : 0;
        const uint32_t n    = std::min((uint32_t)m.size(), room);
        r.droppedShadow += (uint32_t)m.size() - n;
        if (n == 0) continue;
        const Candidate& c0 = cands[m[0]];
        r.shadow.push_back({ c0.indexCount, n, c0.firstIndex, c0.vertexOffset, row });
        for (uint32_t k = 0; k < n; ++k) r.rowSource.push_back(m[k]);
        row += n;
    }

    // 투명은 뒤에서 앞으로. 거리가 같으면 행 번호 순이라 결과가 흔들리지 않습니다.
    if (sortTransparent)
        std::sort(transp.begin(), transp.end(), [](const TranspKeyed& a, const TranspKeyed& b) {
            if (a.distSq != b.distSq) return a.distSq > b.distSq;
            return a.run.firstInstance < b.run.firstInstance;
        });
    r.transparent.reserve(transp.size());
    for (const auto& t : transp) r.transparent.push_back(t.run);
    return r;
}

} // namespace hbatch
