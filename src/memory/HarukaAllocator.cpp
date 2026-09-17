#include "HarukaAllocator.hpp"
#include <stdexcept>
#include <algorithm>
#include "../core/HLog.hpp"

static constinit HLogCategory LogMemory{ "Memory", ELogVerbosity::Log };

// ── selectMemType ─────────────────────────────────────────────────────────────
// props에서 required 플래그를 모두 만족하고, 해당 heap 크기가 minHeapSize 이상인
// 첫 번째 memoryTypeIndex를 반환한다. 없으면 UINT32_MAX.
uint32_t HarukaAllocator::selectMemType(
    const VkPhysicalDeviceMemoryProperties& props,
    VkMemoryPropertyFlags required,
    VkDeviceSize minHeapSize)
{
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
        const VkDeviceSize          avail = props.memoryHeaps[
                                                props.memoryTypes[i].heapIndex].size;
        if ((flags & required) == required && avail >= minHeapSize)
            return i;
    }
    return UINT32_MAX;
}

// ── init ──────────────────────────────────────────────────────────────────────
void HarukaAllocator::init(VkDevice dev, VkPhysicalDevice physDev) {
    device     = dev;
    physDevice = physDev;

    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physDev, &props);

    // 1순위: DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT (UMA, Quest Pro 이상적)
    constexpr VkMemoryPropertyFlags kIdeal =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT  |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // 2순위: HOST_VISIBLE | HOST_COHERENT (PC discrete GPU, DEVICE_LOCAL 없음)
    constexpr VkMemoryPropertyFlags kFallback =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    bool isUMA = true;

    // 1GB를 수용할 수 있는 이상적인 타입 탐색
    memType = selectMemType(props, kIdeal, kHeapTarget);

    if (memType == UINT32_MAX) {
        // 1GB짜리 HOST_VISIBLE 힙이 없으면 크기 제한 없이 fallback
        isUMA   = false;
        memType = selectMemType(props, kFallback, 0);
    }

    if (memType == UINT32_MAX)
        throw std::runtime_error("[Haruka] No suitable memory type for Core Heap!");

    // 실제 힙 크기 확인, 75%까지만 선점 (드라이버/OS 여유분)
    const uint32_t   heapIdx = props.memoryTypes[memType].heapIndex;
    const VkDeviceSize avail = props.memoryHeaps[heapIdx].size;
    heapSize = std::min(kHeapTarget, avail * 3 / 4);

    if (heapSize < kHeapTarget) {
        HLOG(LogMemory, Warning) << "Core Heap: target 1GB, available "
                                 << avail / (1024 * 1024) << "MB - capping to "
                                 << heapSize / (1024 * 1024) << "MB";
    }

    // 1GB 선점 시도 → 실패 시 절반씩 줄여 재시도 (최소 256MB)
    while (hHeap == VK_NULL_HANDLE && heapSize >= kHeapTarget / 4) {
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = heapSize;
        ai.memoryTypeIndex = memType;
        if (vkAllocateMemory(device, &ai, nullptr, &hHeap) != VK_SUCCESS) {
            heapSize /= 2;
        }
    }
    if (hHeap == VK_NULL_HANDLE)
        throw std::runtime_error("[Haruka] vkAllocateMemory failed for Core Heap!");

    // 전체 범위 지속 매핑 (HOST_COHERENT → flush 불필요)
    if (vkMapMemory(device, hHeap, 0, VK_WHOLE_SIZE, 0, &heapMapped) != VK_SUCCESS)
        throw std::runtime_error("[Haruka] vkMapMemory failed for Core Heap!");

    HLOG(LogMemory, Log) << "Core Heap ready: "
                         << heapSize / (1024 * 1024) << "MB"
                         << " | memType[" << memType << "]"
                         << (isUMA ? " | DEVICE_LOCAL+HOST_VISIBLE (UMA)"
                                   : " | HOST_VISIBLE only (discrete GPU)");

    // Zone B free list init
    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(physDev, &devProps);
    bigGranularity = devProps.limits.bufferImageGranularity;
    freeListB.push_back({ kZoneBOffset, kZoneBSize });
    HLOG(LogMemory, Log) << "Zone B ready: "
                         << kZoneBSize / (1024 * 1024) << "MB FreeList"
                         << " | BIG=" << bigGranularity;

    HLOG(LogMemory, Log) << "Zone C ready: "
                         << kZoneCSize / (1024 * 1024) << "MB Ring";

    // Zone A smoke test
    auto probe = allocA(256, 4);
    if (!probe.isValid())
        throw std::runtime_error("[Haruka] Zone A smoke test failed!");
    HLOG(LogMemory, Log) << "Zone A ready: offset=" << probe.offset
                         << " ptr=" << probe.mappedPtr;
    resetZoneA();
}

// ── Zone A: Bump Allocator ────────────────────────────────────────────────────
HarukaAllocation HarukaAllocator::allocA(VkDeviceSize size, VkDeviceSize alignment) {
    if (alignment == 0) alignment = 1;

    const VkDeviceSize aligned = (cursorA + alignment - 1) & ~(alignment - 1);
    const VkDeviceSize end     = kZoneAOffset + kZoneASize;

    if (aligned + size > end) {
        HLOG(LogMemory, Error) << "Zone A OOM: requested " << size
                               << " bytes, cursor=" << cursorA;
        return {};
    }

    HarukaAllocation a;
    a.offset    = aligned;
    a.size      = size;
    a.mappedPtr = static_cast<uint8_t*>(heapMapped) + aligned;
    a.zone      = AllocZone::A;

    cursorA = aligned + size;
    return a;
}

void HarukaAllocator::resetZoneA() {
    cursorA = kZoneAOffset;
}

// ── Zone B: FreeList Allocator ────────────────────────────────────────────────
HarukaAllocation HarukaAllocator::allocB(const VkMemoryRequirements& reqs, ResourceType type) {
    const VkDeviceSize size = reqs.size;
    // Images must be aligned to bufferImageGranularity to avoid page sharing with buffers.
    const VkDeviceSize alignment = (type == ResourceType::Image && bigGranularity > 1)
        ? std::max(reqs.alignment, bigGranularity)
        : reqs.alignment;

    for (size_t i = 0; i < freeListB.size(); ++i) {
        const VkDeviceSize blockStart = freeListB[i].offset;
        const VkDeviceSize blockEnd   = blockStart + freeListB[i].size;
        const VkDeviceSize aligned    = (blockStart + alignment - 1) & ~(alignment - 1);

        if (aligned + size > blockEnd) continue;

        HarukaAllocation a;
        a.offset    = aligned;
        a.size      = size;
        a.mappedPtr = static_cast<uint8_t*>(heapMapped) + aligned;
        a.zone      = AllocZone::B;

        const VkDeviceSize beforeSize = aligned - blockStart;
        const VkDeviceSize afterStart = aligned + size;
        const VkDeviceSize afterSize  = blockEnd - afterStart;

        freeListB.erase(freeListB.begin() + i);
        if (afterSize  > 0) freeListB.insert(freeListB.begin() + i, { afterStart, afterSize  });
        if (beforeSize > 0) freeListB.insert(freeListB.begin() + i, { blockStart, beforeSize });

        return a;
    }

    HLOG(LogMemory, Error) << "Zone B OOM: requested " << size << " bytes";
    return {};
}

// ── Zone C: Ring Buffer ───────────────────────────────────────────────────────
HarukaAllocation HarukaAllocator::allocC(VkDeviceSize size, VkDeviceSize alignment) {
    if (alignment == 0) alignment = 1;

    VkDeviceSize aligned = (ringHeadC + alignment - 1) & ~(alignment - 1);
    const VkDeviceSize zoneEnd = kZoneCOffset + kZoneCSize;

    if (aligned + size > zoneEnd) {
        aligned   = (kZoneCOffset + alignment - 1) & ~(alignment - 1); // wrap
    }
    if (aligned + size > zoneEnd) {
        HLOG(LogMemory, Error) << "Zone C OOM: requested " << size << " bytes";
        return {};
    }

    HarukaAllocation a;
    a.offset    = aligned;
    a.size      = size;
    a.mappedPtr = static_cast<uint8_t*>(heapMapped) + aligned;
    a.zone      = AllocZone::C;

    ringHeadC = aligned + size;
    return a;
}

void HarukaAllocator::freeB(const HarukaAllocation& alloc) {
    if (!alloc.isValid()) return;
    if (alloc.auxMem != VK_NULL_HANDLE) {
        vkFreeMemory(device, alloc.auxMem, nullptr);
    }
    size_t i = 0;
    while (i < freeListB.size() && freeListB[i].offset < alloc.offset) ++i;

    freeListB.insert(freeListB.begin() + i, { alloc.offset, alloc.size });

    // Coalesce with next
    if (i + 1 < freeListB.size() &&
        freeListB[i].offset + freeListB[i].size == freeListB[i + 1].offset) {
        freeListB[i].size += freeListB[i + 1].size;
        freeListB.erase(freeListB.begin() + i + 1);
    }
    // Coalesce with prev
    if (i > 0 &&
        freeListB[i - 1].offset + freeListB[i - 1].size == freeListB[i].offset) {
        freeListB[i - 1].size += freeListB[i].size;
        freeListB.erase(freeListB.begin() + i);
    }
}

// ── bind ──────────────────────────────────────────────────────────────────────
VkResult HarukaAllocator::bind(VkBuffer buf, const HarukaAllocation& alloc) {
    return vkBindBufferMemory(device, buf, hHeap, alloc.offset);
}

VkResult HarukaAllocator::bind(VkImage img, HarukaAllocation& alloc) {
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(device, img, &reqs);

    if ((reqs.memoryTypeBits >> memType) & 1) {
        return vkBindImageMemory(device, img, hHeap, alloc.offset);
    }

    // Heap memory type not compatible (discrete GPU). Allocate standalone DEVICE_LOCAL memory.
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physDevice, &props);
    uint32_t fallbackType = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((reqs.memoryTypeBits >> i) & 1 &&
            (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            fallbackType = i;
            break;
        }
    }
    if (fallbackType == UINT32_MAX) return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = fallbackType;
    VkResult r = vkAllocateMemory(device, &ai, nullptr, &alloc.auxMem);
    if (r != VK_SUCCESS) return r;
    return vkBindImageMemory(device, img, alloc.auxMem, 0);
}

// ── destroy ───────────────────────────────────────────────────────────────────
void HarukaAllocator::destroy() {
    if (heapMapped) {
        vkUnmapMemory(device, hHeap);
        heapMapped = nullptr;
    }
    if (hHeap != VK_NULL_HANDLE) {
        vkFreeMemory(device, hHeap, nullptr);
        hHeap = VK_NULL_HANDLE;
    }
    heapSize  = 0;
    memType   = UINT32_MAX;
    freeListB.clear();
    ringHeadC = kZoneCOffset;
}
