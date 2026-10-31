#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

// ── Zone 크기 및 오프셋 상수 ───────────────────────────────────────────────────
// 핵심 힙은 언제나 정확히 kHeapTarget 입니다. 한 번에 잡고, 못 잡으면 이유를 남기고 멈춥니다(DEF-42, 축소 경로 없음).
// 그래서 Zone 오프셋은 컴파일 타임 상수 그대로 맞습니다. 지연 할당 메모리가 있는 기기(Quest)에서는 MSAA 컬러가 Zone B 밖에
// 따로 놓입니다(DEF-06).
constexpr VkDeviceSize kZoneASize   =   64ULL * 1024 * 1024;  //  64MB : 엔진 구조체
constexpr VkDeviceSize kZoneBSize   =  896ULL * 1024 * 1024;  // 896MB : 에셋 뱅크 (4K 크기 의존 타깃 + permanent ~216MB)
constexpr VkDeviceSize kZoneCSize   =   64ULL * 1024 * 1024;  //  64MB : 동적 버퍼 (실사용 ~0.5MB, 128x 여유)
constexpr VkDeviceSize kHeapTarget  = 1024ULL * 1024 * 1024;  //   1GB : 정확히 이 크기로 한 번 잡음

constexpr VkDeviceSize kZoneAOffset = 0;
constexpr VkDeviceSize kZoneBOffset = kZoneAOffset + kZoneASize;
constexpr VkDeviceSize kZoneCOffset = kZoneBOffset + kZoneBSize;

static_assert(kZoneASize + kZoneBSize + kZoneCSize == kHeapTarget,
              "Zone sizes must sum to kHeapTarget");
static_assert(kZoneCOffset + kZoneCSize == kHeapTarget, "Zone C must end at the heap end");

// ── 할당 결과 핸들 ─────────────────────────────────────────────────────────────
// Dedicated: 핵심 힙 밖에 따로 잡은 메모리(지연 할당 MSAA 컬러, DEF-06). freeB 로 돌려줍니다.
enum class AllocZone : uint8_t { A, B, C, Dedicated };

// 이미지 메모리를 어디서 잡을지(RenderContext::createImage). LazyPreferred 는 지연 할당 타입을 먼저 시도하고, 없으면 Zone B
// 입니다. TRANSIENT 첨부 용도인 이미지에만 씁니다(다른 이미지의 memoryTypeBits 에는 지연 할당 타입이 없음).
enum class ImageMemory : uint8_t { ZoneB, LazyPreferred };

struct HarukaAllocation {
    VkDeviceSize   offset    = 0;
    VkDeviceSize   size      = 0;
    void*          mappedPtr = nullptr;
    AllocZone      zone      = AllocZone::A;
    // Zone B 이미지의 독립 GPU 대체 메모리(ReBAR 없는 PC 등), 또는 Dedicated 할당의 메모리 자체
    VkDeviceMemory auxMem    = VK_NULL_HANDLE;

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
    VkDeviceSize heapTotal;   // total heap allocated (언제나 1 GB)
    VkDeviceSize dedicatedBytes;   // 핵심 힙 밖 별도 할당(지연 할당 MSAA 컬러 등)
};

// ── HarukaAllocator ────────────────────────────────────────────────────────────
class HarukaAllocator {
public:
    // 1단계: 초기화 / 해제. maxAllocationSize 는 장치의 maxMemoryAllocationSize(0 = 모름, 1.1 코어 Maintenance3).
    // 1GB 를 한 번에 못 잡으면 이유를 적은 예외로 멈춥니다(DEF-42).
    void init(VkDevice device, VkPhysicalDevice physDevice, VkDeviceSize maxAllocationSize = 0);
    void destroy();
    // 개발 스위치(--vk-dedicated-msaa): 지연 할당 타입 대신 장치 로컬 타입으로 별도 할당해, 데스크톱에서도 Dedicated
    // 할당 · 해제 경로를 탑니다. init 전에 부릅니다.
    void setForceDedicated(bool on) { forceDedicated_ = on; }

    // TRANSIENT 첨부 이미지(MSAA 컬러)를 핵심 힙 밖 지연 할당 메모리에 놓고 바인드합니다(DEF-06). 그런 타입이 없거나
    // 할당이 실패하면 빈 할당을 돌려주고, 호출하는 쪽이 Zone B 로 갑니다.
    HarukaAllocation allocLazyImage(VkImage img, const VkMemoryRequirements& reqs, const char* what);

    // Zone A: Bump Allocator (해제 없음, resetZoneA()로만 전체 리셋)
    HarukaAllocation allocA(VkDeviceSize size, VkDeviceSize alignment);
    void             resetZoneA();

    // Zone B: FreeList Allocator
    HarukaAllocation allocB(const VkMemoryRequirements& reqs, ResourceType type = ResourceType::Buffer);
    void             freeB(const HarukaAllocation& alloc);   // Dedicated 할당도 여기로 돌려줍니다

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
            heapSize,
            dedicatedBytes_
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

    // 메모리 타입 선택은 hmem(MemoryTypeSelect.hpp)이 합니다. 표는 init 에서 한 번 읽어 둡니다.
    VkPhysicalDeviceMemoryProperties memProps_{};
    uint32_t     dedicatedCount_ = 0;
    VkDeviceSize dedicatedBytes_ = 0;
    bool         forceDedicated_ = false;
    bool         loggedLazy_     = false;   // 지연 할당을 처음 썼을 때 한 번 알림(그 뒤는 Verbose)
    bool         loggedNoLazy_   = false;   // 지연 할당 타입이 없음을 한 번 알림
    bool         loggedAux_      = false;   // Zone B 이미지의 대체 메모리를 처음 썼을 때 한 번 알림
};
