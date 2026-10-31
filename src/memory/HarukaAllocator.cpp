#include "HarukaAllocator.hpp"
#include "MemoryTypeSelect.hpp"
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include <string>
#include "../core/HLog.hpp"

static constinit HLogCategory LogMemory{ "Memory", ELogVerbosity::Log };

static const char* zoneName(AllocZone z) {
    switch (z) {
        case AllocZone::A:         return "A";
        case AllocZone::B:         return "B";
        case AllocZone::C:         return "C";
        case AllocZone::Dedicated: return "Dedicated";
    }
    return "?";
}

// ── init ──────────────────────────────────────────────────────────────────────
// 핵심 힙은 1GB 를 한 번에 잡습니다(DEF-42). 예전에는 힙의 75% 로 줄이고, 실패하면 절반씩 256MB 까지 줄여 다시 잡았는데,
// Zone 오프셋은 1GB 기준 상수라 줄어든 힙에서는 Zone B · C 가 할당 범위 밖을 가리켰습니다. 이제 못 잡으면 이유(필요한
// 크기 · 고른 타입 · 힙 크기 · VkResult · 최대 단일 할당)를 적은 예외로 멈춥니다. Quest 는 maxMemoryAllocationSize 가
// 정확히 1GiB 이고 힙이 12GB 넘게 있어 이 경로를 타지 않습니다. 타입 고르기 규칙은 hmem::planCoreHeap 입니다.
void HarukaAllocator::init(VkDevice dev, VkPhysicalDevice physDev, VkDeviceSize maxAllocationSize) {
    device     = dev;
    physDevice = physDev;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps_);

    const hmem::CoreHeapPlan plan = hmem::planCoreHeap(memProps_, kHeapTarget);
    if (plan.fail != hmem::CoreHeapFail::None)
        throw std::runtime_error(hmem::describeCoreHeapFailure(plan, VK_SUCCESS, maxAllocationSize, kHeapTarget));
    memType = plan.memType;
    if (plan.tight) {
        HLOG(LogMemory, Warning) << "Core heap takes more than 75% of heap " << plan.heapIndex << " ("
                                 << plan.heapBytes / (1024 * 1024) << " MB); the driver and OS have little left";
    }
    if (maxAllocationSize && kHeapTarget > maxAllocationSize) {
        HLOG(LogMemory, Warning) << "Core heap (1024 MB) is larger than the device's max single allocation ("
                                 << maxAllocationSize / (1024 * 1024) << " MB); trying anyway";
    }

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = kHeapTarget;
    ai.memoryTypeIndex = memType;
    const VkResult ar = vkAllocateMemory(device, &ai, nullptr, &hHeap);
    if (ar != VK_SUCCESS) {
        hHeap = VK_NULL_HANDLE;
        throw std::runtime_error(hmem::describeCoreHeapFailure(plan, ar, maxAllocationSize, kHeapTarget));
    }

    // 전체 범위 지속 매핑 (HOST_COHERENT → flush 불필요)
    const VkResult mr = vkMapMemory(device, hHeap, 0, VK_WHOLE_SIZE, 0, &heapMapped);
    if (mr != VK_SUCCESS) {
        vkFreeMemory(device, hHeap, nullptr);
        hHeap = VK_NULL_HANDLE;
        heapMapped = nullptr;
        throw std::runtime_error("Core heap: vkMapMemory failed (VkResult " + std::to_string((int)mr) + ")");
    }
    heapSize = kHeapTarget;

    HLOG(LogMemory, Log) << "Core Heap ready: "
                         << heapSize / (1024 * 1024) << "MB"
                         << " | memType[" << memType << "]"
                         // 이 조합은 UMA(Quest 등)뿐 아니라 ReBAR 를 켠 독립 GPU 에도 있어 UMA 라고 단정하지 않습니다.
                         << (plan.deviceLocal ? " | DEVICE_LOCAL+HOST_VISIBLE (UMA or ReBAR)"
                                              : " | HOST_VISIBLE only (discrete GPU)");
    if (forceDedicated_) {
        HLOG(LogMemory, Log) << "--vk-dedicated-msaa: MSAA color goes to a separate device-local allocation";
    }

    // Zone B free list init
    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(physDev, &devProps);
    bigGranularity = devProps.limits.bufferImageGranularity;
    assert(heapSize == kHeapTarget);   // Zone 오프셋은 1GB 기준 상수입니다
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
    // 핵심 힙 밖 별도 할당(DEF-06)은 메모리를 풀기만 합니다. 빈 목록에 넣으면 { 0, size } 가 Zone A 위에 빈 칸으로 광고되므로
    // 이 갈래가 Zone 검사보다 먼저입니다.
    if (alloc.zone == AllocZone::Dedicated) {
        if (alloc.auxMem != VK_NULL_HANDLE) vkFreeMemory(device, alloc.auxMem, nullptr);
        if (dedicatedCount_ > 0) --dedicatedCount_;
        dedicatedBytes_ -= std::min(dedicatedBytes_, alloc.size);
        return;
    }
    // Zone B 가 아닌 할당을 받으면 빈 목록에 엉뚱한 범위가 들어가, 다음 allocB 가 Zone C 위에
    // 이미지를 놓을 수 있습니다(DEF-100: AOPass 가 Zone C 버퍼를 여기로 돌려줬음). Zone A · C 는
    // 하나씩 돌려받지 않습니다(A 는 resetZoneA, C 는 링).
    if (alloc.zone != AllocZone::B) {
        HLOG(LogMemory, Warning) << "freeB ignored an allocation from zone " << zoneName(alloc.zone)
                                 << " (offset " << alloc.offset << ", size " << alloc.size << ")";
        return;
    }
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
    assert(alloc.zone != AllocZone::Dedicated);   // allocLazyImage 가 이미 바인드함
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(device, img, &reqs);

    if ((reqs.memoryTypeBits >> memType) & 1) {
        return vkBindImageMemory(device, img, hHeap, alloc.offset);
    }

    // Heap memory type not compatible (discrete GPU). Allocate standalone DEVICE_LOCAL memory.
    const uint32_t fallbackType = hmem::selectMemType(memProps_, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, reqs.memoryTypeBits);
    if (fallbackType == UINT32_MAX) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (!loggedAux_) {
        loggedAux_ = true;
        HLOG(LogMemory, Log) << "An image cannot use the core heap's memory type " << memType << "; using a separate "
                                "device-local allocation (type " << fallbackType << ") for such images";
    }

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = fallbackType;
    VkResult r = vkAllocateMemory(device, &ai, nullptr, &alloc.auxMem);
    if (r != VK_SUCCESS) return r;
    return vkBindImageMemory(device, img, alloc.auxMem, 0);
}

// ── 지연 할당 이미지 (DEF-06) ────────────────────────────────────────────────────
// 타일 GPU(Quest)에서 MSAA 컬러는 타일 안에서만 쓰이고 resolve 뒤 버려집니다. LAZILY_ALLOCATED 타입에 두면 실제 메모리가
// 거의 잡히지 않아 Zone B 에서 눈 해상도 4x RGBA16F 만큼(약 100MB)을 아낍니다. 데스크톱 GPU 에는 이 타입이 없어 지금처럼
// Zone B 로 갑니다. --vk-dedicated-msaa 면 장치 로컬 타입으로 별도 할당해 이 경로를 데스크톱에서도 탑니다.
HarukaAllocation HarukaAllocator::allocLazyImage(VkImage img, const VkMemoryRequirements& reqs, const char* what) {
    uint32_t t = hmem::selectLazyMemType(memProps_, reqs.memoryTypeBits);
    const bool forced = (t == UINT32_MAX && forceDedicated_);
    if (forced) {
        // 장치 로컬이면서 호스트에서 보이지 않는 타입(데스크톱 VRAM). 없으면 장치 로컬 아무것이나.
        for (uint32_t i = 0; i < memProps_.memoryTypeCount && i < 32 && t == UINT32_MAX; ++i) {
            const VkMemoryPropertyFlags f = memProps_.memoryTypes[i].propertyFlags;
            if ((reqs.memoryTypeBits >> i & 1) && (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
                !(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
                t = i;
        }
        if (t == UINT32_MAX) t = hmem::selectMemType(memProps_, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, reqs.memoryTypeBits);
    }
    if (t == UINT32_MAX) {
        if (!loggedNoLazy_) {
            loggedNoLazy_ = true;
            HLOG(LogMemory, Log) << what << ": no lazily allocated memory type, using Zone B";
        }
        return {};
    }
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = reqs.size;
    ai.memoryTypeIndex = t;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkResult r = vkAllocateMemory(device, &ai, nullptr, &mem);
    if (r == VK_SUCCESS) r = vkBindImageMemory(device, img, mem, 0);
    if (r != VK_SUCCESS) {
        if (mem) vkFreeMemory(device, mem, nullptr);
        HLOG(LogMemory, Warning) << what << ": separate allocation on memory type " << t << " failed (VkResult " << (int)r
                                 << "); using Zone B";
        return {};
    }
    HarukaAllocation a;
    a.offset = 0;
    a.size   = reqs.size;
    a.zone   = AllocZone::Dedicated;
    a.auxMem = mem;
    ++dedicatedCount_;
    dedicatedBytes_ += reqs.size;
    if (!loggedLazy_) {
        loggedLazy_ = true;
        HLOG(LogMemory, Log) << what << ": " << (forced ? "forced separate" : "lazily allocated") << " memory type " << t
                             << ", " << reqs.size / (1024 * 1024) << " MB outside the core heap";
    } else {
        HLOG(LogMemory, Verbose) << what << ": memory type " << t << ", " << reqs.size / (1024 * 1024) << " MB";
    }
    return a;
}

// ── destroy ───────────────────────────────────────────────────────────────────
void HarukaAllocator::destroy() {
    if (dedicatedCount_ != 0) {
        HLOG(LogMemory, Warning) << dedicatedCount_ << " dedicated allocations (" << dedicatedBytes_ / (1024 * 1024)
                                 << " MB) were not freed";
    }
    dedicatedCount_ = 0;
    dedicatedBytes_ = 0;
    loggedLazy_ = loggedNoLazy_ = loggedAux_ = false;
    memProps_ = {};
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
