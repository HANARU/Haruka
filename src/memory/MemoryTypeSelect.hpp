#pragma once

// ── 메모리 타입 선택 (DEF-42 · DEF-06) ────────────────────────────────────────────────────────
// 1GB 핵심 힙(HarukaAllocator)과 MSAA 지연 할당 이미지가 쓸 메모리 타입을 고르는 규칙입니다.
//
// 규칙:
//   · 핵심 힙은 DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT 타입을 먼저 찾고(UMA · ReBAR), 없으면 HOST_VISIBLE |
//     HOST_COHERENT 를 찾습니다. 두 단계 모두 힙이 목표(1GB) 이상이어야 합니다. 예전에는 둘째 단계의 최소 힙이 0 이라,
//     DL|HV|HC 타입이 시스템 메모리 타입보다 앞에 있는 GPU 에서 256MB BAR 힙을 골라 결국 실패할 수 있었습니다.
//   · 둘 다 없으면 멈춥니다(축소 경로 없음, DEF-42). 이유는 사람이 읽을 한 줄로 남깁니다(describeCoreHeapFailure).
//   · 목표가 힙의 75% 를 넘으면 tight 로 표시합니다. 경고만 하고 계속합니다(사용자 결정 2026-10-31).
//   · 지연 할당(LAZILY_ALLOCATED)은 이미지의 memoryTypeBits 안에서 PROTECTED 가 아닌 첫 타입입니다. 없으면 이미지를
//     지금처럼 Zone B 에 둡니다. TRANSIENT 가 아닌 이미지의 memoryTypeBits 에는 지연 할당 타입이 들어가지 않습니다.
//
// Vulkan 형만 쓰고 Vulkan 함수는 부르지 않으므로 링크 없이 HarukaMemoryTest(memory_types)가 Quest Pro 같은 메모리 표로
// 창 · 장치 없이 검산합니다. vk_enum_string_helper.h 는 쓰지 않습니다(NDK 에 있는지 확인하지 못함).

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace hmem {

// required 플래그를 모두 갖고, typeBits 에 있고, 힙이 minHeap 이상인 첫 타입. 없으면 UINT32_MAX.
inline uint32_t selectMemType(const VkPhysicalDeviceMemoryProperties& props, VkMemoryPropertyFlags required,
                              VkDeviceSize minHeap, uint32_t typeBits = ~0u) {
    for (uint32_t i = 0; i < props.memoryTypeCount && i < 32; ++i) {
        if (!(typeBits & (1u << i))) continue;
        const VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
        const VkDeviceSize          heap  = props.memoryHeaps[props.memoryTypes[i].heapIndex].size;
        if ((flags & required) == required && heap >= minHeap) return i;
    }
    return UINT32_MAX;
}

enum class CoreHeapFail { None, NoHostVisibleType, HeapTooSmall };

struct CoreHeapPlan {
    uint32_t     memType     = UINT32_MAX;   // HeapTooSmall 이면 가장 큰 HV 힙의 타입(메시지용)
    uint32_t     heapIndex   = UINT32_MAX;
    VkDeviceSize heapBytes   = 0;
    bool         deviceLocal = false;
    bool         tight       = false;        // 목표 > 힙의 75%(경고용, 멈추지 않음)
    CoreHeapFail fail        = CoreHeapFail::None;
};

inline constexpr VkMemoryPropertyFlags kCoreIdeal =
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
inline constexpr VkMemoryPropertyFlags kCoreFallback =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

inline CoreHeapPlan planCoreHeap(const VkPhysicalDeviceMemoryProperties& props, VkDeviceSize target) {
    CoreHeapPlan p;
    uint32_t t = selectMemType(props, kCoreIdeal, target);
    if (t == UINT32_MAX) t = selectMemType(props, kCoreFallback, target);
    if (t != UINT32_MAX) {
        p.memType     = t;
        p.heapIndex   = props.memoryTypes[t].heapIndex;
        p.heapBytes   = props.memoryHeaps[p.heapIndex].size;
        p.deviceLocal = (props.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        p.tight       = target > p.heapBytes / 4 * 3;
        return p;
    }
    // 실패 이유: HV|HC 타입이 있는데 힙이 작은지, 아예 없는지.
    for (uint32_t i = 0; i < props.memoryTypeCount && i < 32; ++i) {
        if ((props.memoryTypes[i].propertyFlags & kCoreFallback) != kCoreFallback) continue;
        const uint32_t     h    = props.memoryTypes[i].heapIndex;
        const VkDeviceSize size = props.memoryHeaps[h].size;
        if (p.memType == UINT32_MAX || size > p.heapBytes) {
            p.memType   = i;
            p.heapIndex = h;
            p.heapBytes = size;
            p.deviceLocal = (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        }
    }
    p.fail = (p.memType == UINT32_MAX) ? CoreHeapFail::NoHostVisibleType : CoreHeapFail::HeapTooSmall;
    return p;
}

inline uint32_t selectLazyMemType(const VkPhysicalDeviceMemoryProperties& props, uint32_t typeBits) {
    for (uint32_t i = 0; i < props.memoryTypeCount && i < 32; ++i) {
        if (!(typeBits & (1u << i))) continue;
        const VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) && !(flags & VK_MEMORY_PROPERTY_PROTECTED_BIT)) return i;
    }
    return UINT32_MAX;
}

// 메모리 오류에서 나올 만한 코드만 이름으로 씁니다. 나머지는 nullptr(숫자만 씀).
inline const char* vkResultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS:                     return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY:    return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:  return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:           return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:     return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_TOO_MANY_OBJECTS:      return "VK_ERROR_TOO_MANY_OBJECTS";
        default:                             return nullptr;
    }
}

// 사람이 읽을 영어 한 줄. plan.fail 이 None 이면 고른 타입에서 할당(r)이 실패한 경우입니다. r 이 VK_SUCCESS 이거나
// maxAlloc 이 0 이면 그 부분을 뺍니다.
inline std::string describeCoreHeapFailure(const CoreHeapPlan& plan, VkResult r, VkDeviceSize maxAlloc,
                                           VkDeviceSize target) {
    auto mb = [](VkDeviceSize b) { return std::to_string(b / (1024ull * 1024ull)) + " MB"; };
    std::string s = "Core heap: Haruka needs one " + mb(target) + " block of host-visible memory";
    switch (plan.fail) {
        case CoreHeapFail::NoHostVisibleType:
            s += "; the GPU has no host-visible, host-coherent memory type";
            break;
        case CoreHeapFail::HeapTooSmall:
            s += "; the largest host-visible heap is memory type " + std::to_string(plan.memType) + " on heap " +
                 std::to_string(plan.heapIndex) + " (" + mb(plan.heapBytes) + ")";
            break;
        case CoreHeapFail::None:
            s += "; allocating it from memory type " + std::to_string(plan.memType) + " on heap " +
                 std::to_string(plan.heapIndex) + " (" + mb(plan.heapBytes) + ") failed";
            break;
    }
    if (r != VK_SUCCESS) {
        const char* name = vkResultName(r);
        s += std::string(", VkResult ") + (name ? name : "") + (name ? " (" : "") + std::to_string((int)r) +
             (name ? ")" : "");
    }
    if (maxAlloc) s += ", max single allocation " + mb(maxAlloc);
    return s;
}

} // namespace hmem
