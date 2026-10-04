#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

// ── Zone 크기 및 오프셋 상수 ───────────────────────────────────────────────────
constexpr VkDeviceSize kZoneASize   =   64ULL * 1024 * 1024;  //  64MB : 엔진 구조체
constexpr VkDeviceSize kZoneBSize   =  896ULL * 1024 * 1024;  // 896MB : 에셋 뱅크 (4K+4xMSAA ~595MB + permanent ~216MB)
constexpr VkDeviceSize kZoneCSize   =   64ULL * 1024 * 1024;  //  64MB : 동적 버퍼 (실사용 ~0.5MB, 128x 여유)
constexpr VkDeviceSize kHeapTarget  = 1024ULL * 1024 * 1024;  //   1GB : 전체 목표

constexpr VkDeviceSize kZoneAOffset = 0;
constexpr VkDeviceSize kZoneBOffset = kZoneAOffset + kZoneASize;
constexpr VkDeviceSize kZoneCOffset = kZoneBOffset + kZoneBSize;

static_assert(kZoneASize + kZoneBSize + kZoneCSize == kHeapTarget,
              "Zone sizes must sum to kHeapTarget");

// ── 할당 결과 핸들 ─────────────────────────────────────────────────────────────
enum class AllocZone : uint8_t { A, B, C };

struct HarukaAllocation {
    VkDeviceSize   offset    = 0;
    VkDeviceSize   size      = 0;
    void*          mappedPtr = nullptr;
    AllocZone      zone      = AllocZone::A;
    VkDeviceMemory auxMem    = VK_NULL_HANDLE; // non-null on discrete GPU image fallback

    bool isValid() const { return size > 0; }
};

// ── 메모리 타입 분류 (Zone B에서 bufferImageGranularity 처리용) ────────────────
enum class ResourceType : uint8_t { Buffer, Image };

// ── Allocator statistics (for editor memory overlay) ──────────────────────────
struct AllocStats {
    VkDeviceSize zoneAUsed;   // bytes consumed in Zone A (bump)
    VkDeviceSize zoneATotal;  // Zone A capacity (64 MB)
    VkDeviceSize zoneCUsed;   // bytes consumed in Zone C (ring, from start)
    VkDeviceSize zoneCTotal;  // Zone C capacity (64 MB)
    VkDeviceSize heapTotal;   // total heap allocated (1 GB)
};

// ── HarukaAllocator ────────────────────────────────────────────────────────────
class HarukaAllocator {
public:
    // 1단계: 초기화 / 해제
    void init(VkDevice device, VkPhysicalDevice physDevice);
    void destroy();

    // Zone A: Bump Allocator (해제 없음, resetZoneA()로만 전체 리셋)
    HarukaAllocation allocA(VkDeviceSize size, VkDeviceSize alignment);
    void             resetZoneA();

    // Zone B: FreeList Allocator
    HarukaAllocation allocB(const VkMemoryRequirements& reqs, ResourceType type = ResourceType::Buffer);
    void             freeB(const HarukaAllocation& alloc);

    // Zone C: Ring Buffer (per-frame dynamic data)
    HarukaAllocation allocC(VkDeviceSize size, VkDeviceSize alignment);

    // VkBuffer / VkImage를 heap에 바인딩
    VkResult bind(VkBuffer buf, const HarukaAllocation& alloc);
    VkResult bind(VkImage  img, HarukaAllocation& alloc);  // may set auxMem on discrete GPU

    // 조회
    VkDeviceMemory heap()          const { return hHeap;      }
    void*          mappedBase()    const { return heapMapped;  }
    uint32_t       memTypeIndex()  const { return memType;     }
    VkDeviceSize   allocatedSize() const { return heapSize;    }

    AllocStats getStats() const {
        return {
            cursorA  - kZoneAOffset, kZoneASize,
            ringHeadC - kZoneCOffset, kZoneCSize,
            heapSize
        };
    }

private:
    VkDevice         device     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice = VK_NULL_HANDLE;
    VkDeviceMemory hHeap      = VK_NULL_HANDLE;
    void*          heapMapped = nullptr;
    uint32_t       memType    = UINT32_MAX;
    VkDeviceSize   heapSize   = 0;

    // Zone A
    VkDeviceSize   cursorA    = kZoneAOffset;

    // Zone B
    struct FreeBlock { VkDeviceSize offset, size; };
    std::vector<FreeBlock> freeListB;
    VkDeviceSize           bigGranularity = 1;

    // Zone C
    VkDeviceSize           ringHeadC = kZoneCOffset;

    static uint32_t selectMemType(
        const VkPhysicalDeviceMemoryProperties& props,
        VkMemoryPropertyFlags required,
        VkDeviceSize minHeapSize);
};
