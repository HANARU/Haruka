#include "HarukaAllocator.hpp"
#include "MemoryTypeSelect.hpp"
#include <stdexcept>
#include <algorithm>
#include "../core/HCheck.hpp"
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
size_t HarukaAllocator::chunkForOffset(VkDeviceSize offset) {
    return hmem::chunkForZoneOffset(offset, kZoneBOffset, kZoneBChunkSize, kTotalChunks, kZoneCOffset);
}

bool HarukaAllocator::ensureChunk(size_t idx) {
    if (idx >= kTotalChunks) return false;
    Chunk& c = chunks_[idx];
    if (c.mem != VK_NULL_HANDLE) return true;

    if (inFrame_) {
        HLOG(LogMemory, Warning) << "Frame commit: core heap chunk " << idx << " (" << c.size / (1024 * 1024)
                                 << " MB) allocated during frame";
    }

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = c.size;
    ai.memoryTypeIndex = memType;
    const VkResult ar = vkAllocateMemory(device, &ai, nullptr, &c.mem);
    if (ar != VK_SUCCESS) {
        c.mem = VK_NULL_HANDLE;
        HLOG(LogMemory, Error) << "Core heap chunk " << idx << " (" << c.size / (1024 * 1024)
                               << " MB) vkAllocateMemory failed (VkResult " << (int)ar << ")";
        return false;
    }
    const VkResult mr = vkMapMemory(device, c.mem, 0, VK_WHOLE_SIZE, 0, &c.mapped);
    if (mr != VK_SUCCESS) {
        vkFreeMemory(device, c.mem, nullptr);
        c.mem    = VK_NULL_HANDLE;
        c.mapped = nullptr;
        HLOG(LogMemory, Error) << "Core heap chunk " << idx << " vkMapMemory failed (VkResult " << (int)mr << ")";
        return false;
    }
    heapSize += c.size;
    return true;
}

void HarukaAllocator::precommit(VkDeviceSize minZoneBFreeBytes) {
    if (device == VK_NULL_HANDLE) return;
    // Zone C (프레임별 동적 버퍼) 를 먼저 확보해 첫 프레임 안 커밋을 막습니다.
    ensureChunk(kTotalChunks - 1);

    auto committedFreeB = [&]() -> VkDeviceSize {
        VkDeviceSize sum = 0;
        for (const hmem::FreeBlock& b : freeListB) {
            const size_t c = chunkForOffset(b.offset);
            if (chunks_[c].mem != VK_NULL_HANDLE) sum += b.size;
        }
        return sum;
    };

    while (committedFreeB() < minZoneBFreeBytes) {
        size_t nextChunk = kTotalChunks;
        for (const hmem::FreeBlock& b : freeListB) {
            const size_t c = chunkForOffset(b.offset);
            if (chunks_[c].mem == VK_NULL_HANDLE && b.size > 0) {
                nextChunk = c;
                break;
            }
        }
        if (nextChunk >= kTotalChunks || !ensureChunk(nextChunk)) break;
    }
}

void HarukaAllocator::init(VkDevice dev, VkPhysicalDevice physDev, VkDeviceSize maxAllocationSize) {
    device     = dev;
    physDevice = physDev;
    maxAllocationSize_ = maxAllocationSize;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps_);

    plan_ = hmem::planCoreHeap(memProps_, kHeapTarget);
    if (plan_.fail != hmem::CoreHeapFail::None)
        throw std::runtime_error(hmem::describeCoreHeapFailure(plan_, VK_SUCCESS, maxAllocationSize, kHeapTarget));
    memType = plan_.memType;
    if (plan_.tight) {
        HLOG(LogMemory, Warning) << "Core heap takes more than 75% of heap " << plan_.heapIndex << " ("
                                 << plan_.heapBytes / (1024 * 1024) << " MB); the driver and OS have little left";
    }
    if (maxAllocationSize && kZoneBChunkSize > maxAllocationSize) {
        HLOG(LogMemory, Warning) << "Core heap chunk (128 MB) is larger than the device's max single allocation ("
                                 << maxAllocationSize / (1024 * 1024) << " MB); trying anyway";
    }

    chunks_[0] = { VK_NULL_HANDLE, nullptr, kZoneAOffset, kZoneASize };
    for (size_t i = 0; i < kZoneBChunkCount; ++i)
        chunks_[1 + i] = { VK_NULL_HANDLE, nullptr, kZoneBOffset + i * kZoneBChunkSize, kZoneBChunkSize };
    chunks_[kTotalChunks - 1] = { VK_NULL_HANDLE, nullptr, kZoneCOffset, kZoneCSize };
    heapSize = 0;

    HLOG(LogMemory, Log) << "Core Heap ready: "
                         << kHeapTarget / (1024 * 1024) << "MB virtual (on-demand 128MB chunks)"
                         << " | memType[" << memType << "]"
                         // 이 조합은 UMA(Quest 등)뿐 아니라 ReBAR 를 켠 독립 GPU 에도 있어 UMA 라고 단정하지 않습니다.
                         << (plan_.deviceLocal ? " | DEVICE_LOCAL+HOST_VISIBLE (UMA or ReBAR)"
                                               : " | HOST_VISIBLE only (discrete GPU)");
    if (forceDedicated_) {
        HLOG(LogMemory, Log) << "--vk-dedicated-msaa: MSAA color goes to a separate device-local allocation";
    }

    // Zone B free list init (청크 경계를 넘지 않도록 청크마다 별도 블록으로 초기화)
    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(physDev, &devProps);
    bigGranularity = devProps.limits.bufferImageGranularity;
    hmem::initZoneBFreeList(freeListB, kZoneBOffset, kZoneBChunkSize, kZoneBChunkCount);
    HLOG(LogMemory, Log) << "Zone B ready: "
                         << kZoneBSize / (1024 * 1024) << "MB FreeList (" << kZoneBChunkCount << "x128MB)"
                         << " | BIG=" << bigGranularity;

    HLOG(LogMemory, Log) << "Zone C ready: "
                         << kZoneCSize / (1024 * 1024) << "MB Ring";
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
    if (!ensureChunk(0)) return {};

    HarukaAllocation a;
    a.offset    = aligned;
    a.size      = size;
    a.mappedPtr = static_cast<uint8_t*>(chunks_[0].mapped) + (aligned - chunks_[0].base);
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

    VkDeviceSize aligned = 0;
    if (hmem::allocFromFreeList(freeListB, size, alignment, aligned)) {
        const size_t c = chunkForOffset(aligned);
        void* mappedPtr = nullptr;
        if (type == ResourceType::Buffer) {
            if (!ensureChunk(c)) return {};
            mappedPtr = static_cast<uint8_t*>(chunks_[c].mapped) + (aligned - chunks_[c].base);
        }
        HarukaAllocation a;
        a.offset    = aligned;
        a.size      = size;
        a.mappedPtr = mappedPtr;
        a.zone      = AllocZone::B;
        return a;
    }

    // 128 MB 청크에 들어가지 않는 큰 요청(예: 4K/Quest Pro MSAA 색 타깃)은 Zone B 예산 안에서 전용 할당으로 잡습니다.
    if (zoneBUsed() + size <= kZoneBSize) {
        HarukaAllocation a;
        a.offset  = kZoneBOffset;
        a.size    = size;
        a.zone    = AllocZone::B;
        a.isLarge = true;
        if (type == ResourceType::Buffer) {
            VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            ai.allocationSize  = size;
            ai.memoryTypeIndex = memType;
            if (vkAllocateMemory(device, &ai, nullptr, &a.auxMem) != VK_SUCCESS) {
                HLOG(LogMemory, Error) << "Zone B large buffer alloc failed: requested " << size << " bytes";
                return {};
            }
            if (vkMapMemory(device, a.auxMem, 0, VK_WHOLE_SIZE, 0, &a.mappedPtr) != VK_SUCCESS) {
                vkFreeMemory(device, a.auxMem, nullptr);
                a.auxMem = VK_NULL_HANDLE;
                return {};
            }
            a.auxCoreMem = true;
            heapSize += size;
        }
        largeBBytes_ += size;
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
    const size_t c = kTotalChunks - 1;
    if (!ensureChunk(c)) return {};

    HarukaAllocation a;
    a.offset    = aligned;
    a.size      = size;
    a.mappedPtr = static_cast<uint8_t*>(chunks_[c].mapped) + (aligned - chunks_[c].base);
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
    if (alloc.isLarge) {
        if (alloc.mappedPtr && alloc.auxMem != VK_NULL_HANDLE)
            vkUnmapMemory(device, alloc.auxMem);
        if (alloc.auxMem != VK_NULL_HANDLE)
            vkFreeMemory(device, alloc.auxMem, nullptr);
        if (alloc.auxCoreMem)
            heapSize -= std::min(heapSize, alloc.size);
        largeBBytes_ -= std::min(largeBBytes_, alloc.size);
        return;
    }
    if (alloc.auxMem != VK_NULL_HANDLE) {
        vkFreeMemory(device, alloc.auxMem, nullptr);
    }
    hmem::freeToFreeList(freeListB, alloc.offset, alloc.size, kZoneBOffset, kZoneBChunkSize, kTotalChunks, kZoneCOffset);
}

// ── bind ──────────────────────────────────────────────────────────────────────
VkResult HarukaAllocator::bind(VkBuffer buf, const HarukaAllocation& alloc) {
    if (alloc.isLarge)
        return vkBindBufferMemory(device, buf, alloc.auxMem, 0);
    const size_t c = chunkForOffset(alloc.offset);
    if (!ensureChunk(c)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return vkBindBufferMemory(device, buf, chunks_[c].mem, alloc.offset - chunks_[c].base);
}

VkResult HarukaAllocator::bind(VkImage img, HarukaAllocation& alloc) {
    HCHECK(alloc.zone != AllocZone::Dedicated);   // allocLazyImage 가 이미 바인드함
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(device, img, &reqs);

    if ((reqs.memoryTypeBits >> memType) & 1) {
        if (alloc.isLarge) {
            VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            ai.allocationSize  = reqs.size;
            ai.memoryTypeIndex = memType;
            const VkResult r = vkAllocateMemory(device, &ai, nullptr, &alloc.auxMem);
            if (r != VK_SUCCESS) return r;
            alloc.auxCoreMem = true;
            heapSize += reqs.size;
            return vkBindImageMemory(device, img, alloc.auxMem, 0);
        }
        const size_t c = chunkForOffset(alloc.offset);
        if (!ensureChunk(c)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        return vkBindImageMemory(device, img, chunks_[c].mem, alloc.offset - chunks_[c].base);
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
    for (size_t i = 0; i < kTotalChunks; ++i) {
        if (chunks_[i].mapped) {
            vkUnmapMemory(device, chunks_[i].mem);
            chunks_[i].mapped = nullptr;
        }
        if (chunks_[i].mem != VK_NULL_HANDLE) {
            vkFreeMemory(device, chunks_[i].mem, nullptr);
            chunks_[i].mem = VK_NULL_HANDLE;
        }
        chunks_[i].base = 0;
        chunks_[i].size = 0;
    }
    heapSize     = 0;
    largeBBytes_ = 0;
    inFrame_     = false;
    memType      = UINT32_MAX;
    freeListB.clear();
    ringHeadC = kZoneCOffset;
}
