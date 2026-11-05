#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

// ── 특수화 상수(specialization constant) 묶음 (2026-11-01) ──────────────────────────
// 셰이더의 layout(constant_id = N) const int … 에 파이프라인을 만들 때 값을 넣습니다. 분기를 드라이버가 접으므로
// 유니폼 분기와 달리 실행 비용이 없고, 값이 바뀌면 파이프라인을 다시 만들어야 합니다.
//  - 번호(N)는 shaders/include/haruka_shared.h 의 HARUKA_SPEC_* 한 곳에서 정합니다(겹침 방지).
//  - 값은 모두 4바이트 정수입니다(GLSL int · bool 모두 4바이트). entry.offset = 순서 × 4.
//  - info() 가 돌려준 구조체는 이 객체의 entries · data 를 가리킵니다. vkCreateGraphicsPipelines 가 끝날 때까지
//    이 객체를 살려 두고, info() 뒤에 addInt 를 더 부르지 마십시오(벡터가 다시 할당되면 포인터가 무효).
struct SpecConstants {
    std::vector<VkSpecializationMapEntry> entries;
    std::vector<int32_t>                  data;

    void addInt(uint32_t constantId, int32_t value) {
        VkSpecializationMapEntry e{};
        e.constantID = constantId;
        e.offset     = (uint32_t)(data.size() * sizeof(int32_t));
        e.size       = sizeof(int32_t);
        entries.push_back(e);
        data.push_back(value);
    }

    VkSpecializationInfo info() const {
        VkSpecializationInfo si{};
        si.mapEntryCount = (uint32_t)entries.size();
        si.pMapEntries   = entries.data();
        si.dataSize      = data.size() * sizeof(int32_t);
        si.pData         = data.data();
        return si;
    }
};
